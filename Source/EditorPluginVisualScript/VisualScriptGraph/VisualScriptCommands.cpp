#include <EditorPluginVisualScript/EditorPluginVisualScriptPCH.h>

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptCommands.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraph.h>
#include <ToolsFoundation/Command/TreeCommands.h>
#include <ToolsFoundation/Command/VisualGraphCommands.h>
#include <ToolsFoundation/CommandHistory/CommandHistory.h>
#include <ToolsFoundation/Document/Document.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptRecreatePinsCommand, 1, plRTTIDefaultAllocator<plVisualScriptRecreatePinsCommand>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("ObjectGuid", m_Object),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptCommandAccessor, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

//////////////////////////////////////////////////////////////////////////

plVisualScriptRecreatePinsCommand::plVisualScriptRecreatePinsCommand() = default;

plStatus plVisualScriptRecreatePinsCommand::DoInternal(bool bRedo)
{
  auto pManager = static_cast<plVisualScriptNodeManager*>(GetDocument()->GetObjectManager());
  if (const plDocumentObject* pObject = pManager->GetObject(m_Object))
  {
    pManager->RecreatePins(pObject);
  }
  return plStatus(PL_SUCCESS);
}

plStatus plVisualScriptRecreatePinsCommand::UndoInternal(bool bFireEvents)
{
  auto pManager = static_cast<plVisualScriptNodeManager*>(GetDocument()->GetObjectManager());
  if (const plDocumentObject* pObject = pManager->GetObject(m_Object))
  {
    pManager->RecreatePins(pObject);
  }
  return plStatus(PL_SUCCESS);
}

//////////////////////////////////////////////////////////////////////////

// static
plStatus plVisualScriptMirroredPins::Disconnect(plCommandHistory* pHistory, const plVisualScriptNodeManager* pManager, const plDocumentObject* pObject, plDynamicArray<SavedConnection>& inout_saved)
{
  plHybridArray<plUuid, 16> connectionObjects;

  auto Collect = [&](plArrayPtr<const plUniquePtr<const plVisualGraphPin>> pins)
  {
    for (auto& pPin : pins)
    {
      for (const plVisualGraphConnection* pConnection : pManager->GetConnections(*pPin))
      {
        auto& saved = inout_saved.ExpandAndGetRef();
        saved.m_Source = pConnection->GetSourcePin().GetParent()->GetGuid();
        saved.m_Target = pConnection->GetTargetPin().GetParent()->GetGuid();
        saved.m_sSourcePin = pConnection->GetSourcePin().GetName();
        saved.m_sTargetPin = pConnection->GetTargetPin().GetName();

        connectionObjects.PushBack(pConnection->GetParent()->GetGuid());
      }
    }
  };

  Collect(pManager->GetInputPins(pObject));
  Collect(pManager->GetOutputPins(pObject));

  for (const plUuid& connectionObject : connectionObjects)
  {
    PL_SUCCEED_OR_RETURN(plNodeCommands::DisconnectAndRemoveCommand(pHistory, connectionObject));
  }

  return plStatus(PL_SUCCESS);
}

// static
plStatus plVisualScriptMirroredPins::Reconnect(plCommandHistory* pHistory, const plVisualScriptNodeManager* pManager, const plDynamicArray<SavedConnection>& saved)
{
  const plRTTI* pConnectionType = pManager->GetConnectionType();

  for (const auto& connection : saved)
  {
    const plDocumentObject* pSource = pManager->GetObject(connection.m_Source);
    const plDocumentObject* pTarget = pManager->GetObject(connection.m_Target);
    if (pSource == nullptr || pTarget == nullptr)
      continue;

    const plVisualGraphPin* pSourcePin = pManager->GetOutputPinByName(pSource, connection.m_sSourcePin);
    const plVisualGraphPin* pTargetPin = pManager->GetInputPinByName(pTarget, connection.m_sTargetPin);

    // a pin that no longer exists simply loses its connection
    if (pSourcePin == nullptr || pTargetPin == nullptr)
      continue;

    // the pin may exist with a different type now
    plVisualGraphObjectManager::CanConnectResult res;
    if (pManager->CanConnect(pConnectionType, *pSourcePin, *pTargetPin, res).Failed())
      continue;

    PL_SUCCEED_OR_RETURN(plNodeCommands::AddAndConnectCommand(pHistory, pConnectionType, *pSourcePin, *pTargetPin));
  }

  return plStatus(PL_SUCCESS);
}

// static
plStatus plVisualScriptMirroredPins::RecreatePins(plCommandHistory* pHistory, const plDocumentObject* pObject)
{
  plVisualScriptRecreatePinsCommand cmd;
  cmd.m_Object = pObject->GetGuid();
  return pHistory->AddCommand(cmd);
}

// static
plStatus plVisualScriptMirroredPins::ApplyWithDependents(plCommandHistory* pHistory, const plVisualScriptNodeManager* pManager, plArrayPtr<const plDocumentObject* const> dependents, plDelegate<plStatus()> change)
{
  if (dependents.IsEmpty())
    return change();

  plHybridArray<SavedConnection, 32> saved;
  plHybridArray<plUuid, 8> dependentGuids;
  for (auto pDependent : dependents)
  {
    dependentGuids.PushBack(pDependent->GetGuid());
  }

  pHistory->StartTransaction("Update Mirrored Pins");

  plStatus res(PL_SUCCESS);

  // 1. take every connection off the dependents so no pin rebuild ever destroys a connection object owned by the history
  for (const plUuid& guid : dependentGuids)
  {
    if (const plDocumentObject* pDependent = pManager->GetObject(guid))
      res = Disconnect(pHistory, pManager, pDependent, saved);
    if (res.Failed())
      break;
  }

  // 2. a rebuild before the change: on undo this runs last and restores the old pins
  for (const plUuid& guid : dependentGuids)
  {
    if (res.Failed())
      break;
    if (const plDocumentObject* pDependent = pManager->GetObject(guid))
      res = RecreatePins(pHistory, pDependent);
  }

  // 3. the actual change
  if (res.Succeeded())
    res = change();

  // 4. a rebuild after the change: on redo this produces the new pins
  for (const plUuid& guid : dependentGuids)
  {
    if (res.Failed())
      break;
    if (const plDocumentObject* pDependent = pManager->GetObject(guid))
      res = RecreatePins(pHistory, pDependent);
  }

  // 5. put back what still fits
  if (res.Succeeded())
    res = Reconnect(pHistory, pManager, saved);

  if (res.Failed())
    pHistory->CancelTransaction();
  else
    pHistory->FinishTransaction();

  return res;
}

//////////////////////////////////////////////////////////////////////////

plVisualScriptCommandAccessor::plVisualScriptCommandAccessor(plCommandHistory* pHistory)
  : plVisualGraphCommandAccessor(pHistory)
{
}

plVisualScriptCommandAccessor::~plVisualScriptCommandAccessor() = default;

const plVisualScriptNodeManager* plVisualScriptCommandAccessor::GetManager(const plDocumentObject* pObject) const
{
  return static_cast<const plVisualScriptNodeManager*>(pObject->GetDocumentObjectManager());
}

void plVisualScriptCommandAccessor::GetDependents(const plDocumentObject* pObject, const plAbstractProperty* pProp, plDynamicArray<const plDocumentObject*>& out_dependents) const
{
  out_dependents.Clear();

  if (pObject == nullptr || pProp == nullptr)
    return;

  GetManager(pObject)->GetMirroredPinDependents(pObject, pProp->GetPropertyName(), out_dependents);
}

plStatus plVisualScriptCommandAccessor::SetValue(const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& newValue, plVariant index /*= plVariant()*/)
{
  if (m_pHistory->InTemporaryTransaction())
    return plVisualGraphCommandAccessor::SetValue(pObject, pProp, newValue, index);

  plHybridArray<const plDocumentObject*, 8> dependents;
  GetDependents(pObject, pProp, dependents);

  if (dependents.IsEmpty())
    return plVisualGraphCommandAccessor::SetValue(pObject, pProp, newValue, index);

  const plVisualScriptNodeManager* pManager = GetManager(pObject);

  // Renaming a function: the calls and returns keep referencing it by the new name
  const bool bRename = pManager->IsFunctionEntry(pObject) && plStringUtils::IsEqual(pProp->GetPropertyName(), "Name");

  return plVisualScriptMirroredPins::ApplyWithDependents(m_pHistory, pManager, dependents, [&]() -> plStatus
    {
      PL_SUCCEED_OR_RETURN(plVisualGraphCommandAccessor::SetValue(pObject, pProp, newValue, index));

      if (bRename)
      {
        for (auto pDependent : dependents)
        {
          plSetObjectPropertyCommand cmd;
          cmd.m_Object = pDependent->GetGuid();
          cmd.m_sProperty = "Function";
          cmd.m_NewValue = newValue;
          PL_SUCCEED_OR_RETURN(m_pHistory->AddCommand(cmd));
        }
      }

      return plStatus(PL_SUCCESS);
    });
}

plStatus plVisualScriptCommandAccessor::InsertValue(const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& newValue, plVariant index /*= plVariant()*/)
{
  plHybridArray<const plDocumentObject*, 8> dependents;
  GetDependents(pObject, pProp, dependents);

  return plVisualScriptMirroredPins::ApplyWithDependents(m_pHistory, GetManager(pObject), dependents, [&]() -> plStatus
    { return plVisualGraphCommandAccessor::InsertValue(pObject, pProp, newValue, index); });
}

plStatus plVisualScriptCommandAccessor::RemoveValue(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index /*= plVariant()*/)
{
  plHybridArray<const plDocumentObject*, 8> dependents;
  GetDependents(pObject, pProp, dependents);

  return plVisualScriptMirroredPins::ApplyWithDependents(m_pHistory, GetManager(pObject), dependents, [&]() -> plStatus
    { return plVisualGraphCommandAccessor::RemoveValue(pObject, pProp, index); });
}

plStatus plVisualScriptCommandAccessor::MoveValue(const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& oldIndex, const plVariant& newIndex)
{
  plHybridArray<const plDocumentObject*, 8> dependents;
  GetDependents(pObject, pProp, dependents);

  return plVisualScriptMirroredPins::ApplyWithDependents(m_pHistory, GetManager(pObject), dependents, [&]() -> plStatus
    { return plVisualGraphCommandAccessor::MoveValue(pObject, pProp, oldIndex, newIndex); });
}

plStatus plVisualScriptCommandAccessor::AddObject(const plDocumentObject* pParent, const plAbstractProperty* pParentProp, const plVariant& index, const plRTTI* pType, plUuid& inout_objectGuid)
{
  plHybridArray<const plDocumentObject*, 8> dependents;
  GetDependents(pParent, pParentProp, dependents);

  return plVisualScriptMirroredPins::ApplyWithDependents(m_pHistory, pParent != nullptr ? GetManager(pParent) : nullptr, dependents, [&]() -> plStatus
    { return plVisualGraphCommandAccessor::AddObject(pParent, pParentProp, index, pType, inout_objectGuid); });
}

plStatus plVisualScriptCommandAccessor::RemoveObject(const plDocumentObject* pObject)
{
  plHybridArray<const plDocumentObject*, 8> dependents;
  if (const plDocumentObject* pParent = pObject->GetParent())
  {
    GetDependents(pParent, pParent->GetType()->FindPropertyByName(pObject->GetParentProperty()), dependents);
  }

  return plVisualScriptMirroredPins::ApplyWithDependents(m_pHistory, GetManager(pObject), dependents, [&]() -> plStatus
    { return plVisualGraphCommandAccessor::RemoveObject(pObject); });
}
