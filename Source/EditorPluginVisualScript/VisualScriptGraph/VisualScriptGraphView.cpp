#include <EditorPluginVisualScript/EditorPluginVisualScriptPCH.h>

#include <Core/Scripting/ScriptApiDocs.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/Scripting/ScriptTools.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptCommands.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraph.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraphView.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptNodeRegistry.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptVariable.h>
#include <EditorUi/Surface.h>
#include <Foundation/Math/ColorScheme.h>
#include <GuiFoundation/EngineUi/EngineGraphView.h>
#include <GuiFoundation/EngineUi/EnginePrompt.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Command/TreeCommands.h>
#include <ToolsFoundation/Command/VisualGraphCommands.h>

namespace
{
  plString GetNodeName(const plDocumentObject* pObject)
  {
    plVariant name = pObject->GetTypeAccessor().GetValue("Name");
    return name.IsA<plString>() ? name.Get<plString>() : plString();
  }

  /// A property value as it appears in a node title.
  void FormatTitleValue(const plVisualScriptNodeManager* pManager, const plDocumentObject* pNode, const plAbstractProperty* pProperty, const plVariant& value, plStringBuilder& out_sValue)
  {
    if (pProperty->GetSpecificType()->IsDerivedFrom<plEnumBase>() || pProperty->GetSpecificType()->IsDerivedFrom<plBitflagsBase>())
    {
      plReflectionUtils::EnumerationToString(pProperty->GetSpecificType(), value.ConvertTo<plInt64>(), out_sValue);
      out_sValue = plTranslate(out_sValue);
      return;
    }

    if (value.IsA<plString>() || value.IsA<plHashedString>())
    {
      out_sValue = value.ConvertTo<plString>();

      if (pProperty->GetAttributeByType<plAssetBrowserAttribute>() && plConversionUtils::IsStringUuid(out_sValue))
      {
        auto pAsset = plAssetCurator::GetSingleton()->GetSubAsset(plConversionUtils::ConvertStringToUuid(out_sValue));
        if (pAsset)
          out_sValue = pAsset->m_pAssetInfo->m_Path.GetDataDirRelativePath().GetFileName();
        else
          out_sValue = "<unknown>";
      }

      out_sValue.ReplaceAll("\n", " ");
      out_sValue.ReplaceAll("\t", " ");

      if (out_sValue.GetCharacterCount() > 23)
      {
        out_sValue.Shrink(0, out_sValue.GetCharacterCount() - 21);
        out_sValue.Append("...");
      }

      // names of graph elements read better without quotes
      const bool bIsGraphName = pManager->IsFunctionEntry(pNode) || pManager->IsCall(pNode) || pManager->IsCallLibrary(pNode) || pManager->IsSubGraph(pNode) || pManager->IsPortalIn(pNode) || pManager->IsPortalOut(pNode);
      if (bIsGraphName == false)
      {
        out_sValue.Prepend("\"");
        out_sValue.Append("\"");
      }

      return;
    }

    if (value.CanConvertTo<plString>())
      out_sValue = value.ConvertTo<plString>();
    else
      out_sValue = "<Invalid>";
  }

  /// A plTitleAttribute title with "{Pin}", "{Property}" and "{?Property}" (empty when zero) filled in.
  void FillTitleAttribute(const plVisualScriptNodeManager* pManager, const plDocumentObject* pNode, plStringBuilder& ref_sTitle)
  {
    plStringBuilder sPlaceholder;

    // connected inputs show their pin name instead of the property value; connected strings show nothing
    for (const auto& pPin : pManager->GetInputPins(pNode))
    {
      if (pManager->HasConnections(*pPin) == false)
        continue;

      sPlaceholder.Set("{", pPin->GetName(), "}");
      if (static_cast<const plVisualScriptPin&>(*pPin).GetScriptDataType() == plVisualScriptDataType::String)
        ref_sTitle.ReplaceAll(sPlaceholder, "");
      else
        ref_sTitle.ReplaceAll(sPlaceholder, pPin->GetName());

      sPlaceholder.Set("{?", pPin->GetName(), "}");
      ref_sTitle.ReplaceAll(sPlaceholder, "");
    }

    plHybridArray<const plAbstractProperty*, 32> properties;
    pNode->GetType()->GetAllProperties(properties);

    plStringBuilder sValue;
    for (const plAbstractProperty* pProperty : properties)
    {
      const plVariant value = pNode->GetTypeAccessor().GetValue(pProperty->GetPropertyName());
      FormatTitleValue(pManager, pNode, pProperty, value, sValue);

      sPlaceholder.Set("{", pProperty->GetPropertyName(), "}");
      ref_sTitle.ReplaceAll(sPlaceholder, sValue);

      sPlaceholder.Set("{?", pProperty->GetPropertyName(), "}");
      ref_sTitle.ReplaceAll(sPlaceholder, value == plVariant(0) ? plStringView() : sValue.GetView());
    }
  }

  /// Adds a node of a type to a scope at a position. Runs inside the caller's transaction.
  plStatus AddScopedNode(plCommandHistory* pHistory, const plRTTI* pType, const plUuid& guid, const plUuid& scope, const plVec2& vPos)
  {
    plAddObjectCommand cmd;
    cmd.m_pType = pType;
    cmd.m_NewObjectGuid = guid;
    cmd.m_Index = -1;
    PL_SUCCEED_OR_RETURN(pHistory->AddCommand(cmd));

    if (scope.IsValid())
    {
      plSetObjectPropertyCommand setScope;
      setScope.m_Object = guid;
      setScope.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
      setScope.m_NewValue = scope;
      PL_SUCCEED_OR_RETURN(pHistory->AddCommand(setScope));
    }

    plMoveNodeCommand move;
    move.m_Object = guid;
    move.m_NewPos = vPos;
    return pHistory->AddCommand(move);
  }

  /// Finishes or cancels the current transaction and reports a failure.
  void CommitTransaction(plCommandHistory* pHistory, const plStatus& res, const char* szFailureMessage)
  {
    if (res.Failed())
    {
      pHistory->CancelTransaction();
      plEditorUiServices::MessageBoxStatus(res, szFailureMessage);
      return;
    }

    pHistory->FinishTransaction();
  }

  /// The message the "Insert Portal" prompt shows under a data portal's name; empty when the name can be used. Data
  /// portals have exactly one source, so a name that a data Portal In already has is refused. An empty name passes
  /// here and inserts nothing.
  plString CheckDataPortalName(const plVisualScriptNodeManager* pManager, plStringView sText)
  {
    plStringBuilder sName = sText;
    sName.Trim();

    if (sName.IsEmpty() || pManager->FindPortalIn(sName, false) == nullptr)
      return plString();

    return plString("A data portal with this name already exists. Create a 'Portal Out' for it instead, or choose another name.");
  }

  /// Asks for the name of a new portal. Empty when the prompt was cancelled or left empty: that inserts nothing.
  /// Execution portals may have several ins, so their prompt takes any name and only refuses an empty one.
  plString AskPortalName(const plVisualScriptNodeManager* pManager, bool bExec, plStringView sDefault)
  {
    plEnginePrompt::Validator validator;
    if (!bExec)
    {
      validator = [pManager](plStringView sText)
      { return CheckDataPortalName(pManager, sText); };
    }

    bool bOk = false;
    plStringBuilder sName = plEnginePrompt::GetText("Insert Portal", bExec ? "Name of the execution portal:" : "Name of the data portal:", sDefault, &bOk, validator);
    sName.Trim();

    return bOk ? plString(sName) : plString();
  }
} // namespace

//////////////////////////////////////////////////////////////////////////
// Look

void plVisualScriptGraphEditing::ComputeNodeTitle(const plVisualScriptNodeManager* pManager, const plDocumentObject* pNode, plStringBuilder& out_sTitle, plStringBuilder& out_sSubtitle)
{
  const plRTTI* pType = pNode->GetType();

  if (const plTitleAttribute* pTitleAttribute = pType->GetAttributeByType<plTitleAttribute>())
  {
    out_sTitle = pTitleAttribute->GetTitle();
    FillTitleAttribute(pManager, pNode, out_sTitle);
  }
  else
  {
    out_sTitle = plVisualScriptNodeManager::GetNiceTypeName(pNode);
  }

  out_sSubtitle.Clear();

  if (const char* szSeparator = out_sTitle.FindSubString("::"))
  {
    out_sSubtitle = plStringView(out_sTitle.GetData(), szSeparator);
    out_sSubtitle.Trim("\"");
    out_sTitle = plStringBuilder(szSeparator + 2);
    return;
  }

  auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pType);
  if (pNodeDesc != nullptr && pNodeDesc->NeedsTypeDeduction())
  {
    const plVisualScriptDataType::Enum deductedType = pManager->GetDeductedType(pNode);
    out_sSubtitle = deductedType != plVisualScriptDataType::Invalid ? plVisualScriptDataType::GetName(deductedType) : "Unknown";
  }
}

plStringView plVisualScriptGraphEditing::GetNodeIcon(const plVisualScriptNodeManager* pManager, const plDocumentObject* pNode)
{
  if (pManager->IsCoroutine(pNode))
    return ":/EditorPluginVisualScript/Icons/Coroutine.svg";

  if (pManager->IsLoop(pNode))
    return ":/EditorPluginVisualScript/Icons/Loop.svg";

  return {};
}

plColorGammaUB plVisualScriptGraphEditing::GetRoutingNodeColor(const plVisualScriptNodeManager* pManager, const plDocumentObject* pNode)
{
  bool bExec = false;
  if (pManager->IsReroute(pNode))
  {
    auto inputs = pManager->GetInputPins(pNode);
    bExec = inputs.IsEmpty() == false && static_cast<const plVisualScriptPin&>(*inputs[0]).IsExecutionPin();
  }
  else
  {
    bExec = pManager->IsExecPortal(pNode);
  }

  if (bExec)
    return plColorScheme::DarkUI(plColorScheme::Gray);

  return plVisualScriptNodeRegistry::PinDesc::GetColorForScriptDataType(pManager->GetDeductedType(pNode));
}

bool plVisualScriptGraphEditing::GetDeducedPinColor(const plVisualScriptPin& pin, plColorGammaUB& out_color)
{
  if (pin.NeedsTypeDeduction() == false)
    return false;

  out_color = plVisualScriptNodeRegistry::PinDesc::GetColorForScriptDataType(pin.GetResolvedScriptDataType());
  return true;
}

bool plVisualScriptGraphEditing::IsMissingRequiredConnection(const plVisualScriptNodeManager* pManager, const plVisualScriptPin& pin)
{
  const bool bOwnerFallback = pin.GetResolvedScriptDataType() == plVisualScriptDataType::GameObject && pManager->m_bIsLibrary == false;
  return pManager->IsPinRequired(pin) && bOwnerFallback == false && pManager->HasConnections(pin) == false;
}

void plVisualScriptGraphEditing::GetPinTooltip(const plVisualScriptNodeManager* pManager, const plVisualScriptPin& pin, plStringBuilder& out_sTooltip)
{
  out_sTooltip = pin.GetName();

  if (pin.IsDataPin() == false)
    return;

  out_sTooltip.Append(": ", pin.GetDataTypeName());

  if (pManager->IsPinRequired(pin))
    out_sTooltip.Append(" (Required)");
}

bool plVisualScriptGraphEditing::IsLayoutFlowPin(const plVisualGraphPin& pin)
{
  return plStaticCast<const plVisualScriptPin&>(pin).IsExecutionPin();
}

//////////////////////////////////////////////////////////////////////////
// Scopes

plUuid plVisualScriptGraphEditing::ValidateScope(const plVisualScriptNodeManager* pManager, const plUuid& scope)
{
  if (scope.IsValid() == false)
    return plUuid();

  const plDocumentObject* pOwner = pManager->GetObject(scope);
  if (pOwner == nullptr || pManager->IsNode(pOwner) == false || pManager->IsScopeOwner(pOwner) == false)
    return plUuid();

  return scope;
}

plStatus plVisualScriptGraphEditing::EnsureSubGraphBoundaries(const plVisualScriptNodeManager* pManager, const plUuid& scope)
{
  const plDocumentObject* pSubGraph = pManager->GetObject(scope);
  if (pSubGraph == nullptr || pManager->IsSubGraph(pSubGraph) == false)
    return plStatus(PL_SUCCESS);

  const bool bHasInputs = pManager->GetSubGraphBoundary(pSubGraph, true) != nullptr;
  const bool bHasOutputs = pManager->GetSubGraphBoundary(pSubGraph, false) != nullptr;
  if (bHasInputs && bHasOutputs)
    return plStatus(PL_SUCCESS);

  plCommandHistory* pHistory = pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Create Sub-graph Boundary");

  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();

  plStatus res(PL_SUCCESS);
  if (bHasInputs == false)
    res = AddScopedNode(pHistory, pRegistry->GetSubGraphInputsType(), plUuid::MakeUuid(), scope, plVec2(-400, 0));

  if (res.Succeeded() && bHasOutputs == false)
    res = AddScopedNode(pHistory, pRegistry->GetSubGraphOutputsType(), plUuid::MakeUuid(), scope, plVec2(400, 0));

  if (res.Failed())
    pHistory->CancelTransaction();
  else
    pHistory->FinishTransaction();

  return res;
}

void plVisualScriptGraphEditing::FilterNodesInScope(const plVisualScriptNodeManager* pManager, const plUuid& scope, plArrayPtr<const plDocumentObject* const> selection, plDynamicArray<const plDocumentObject*>& out_nodes)
{
  out_nodes.Clear();

  for (const plDocumentObject* pObject : selection)
  {
    if (pManager->IsNode(pObject) == false || pManager->IsComment(pObject))
      continue;

    if (pManager->GetNodeScope(pObject) != scope)
      continue;

    out_nodes.PushBack(pObject);
  }
}

//////////////////////////////////////////////////////////////////////////
// Functions

plUuid plVisualScriptGraphEditing::AddFunction(const plVisualScriptNodeManager* pManager, plStringView sName)
{
  plCommandHistory* pHistory = pManager->GetDocument()->GetCommandHistory();

  plStringBuilder sUniqueName = sName.IsEmpty() ? plStringView("NewFunction") : sName;
  for (plUInt32 i = 2; pManager->FindFunctionEntry(sUniqueName) != nullptr; ++i)
  {
    sUniqueName.SetFormat("{}{}", sName.IsEmpty() ? plStringView("NewFunction") : sName, i);
  }

  pHistory->StartTransaction("Add Function");

  plAddObjectCommand add;
  add.m_pType = plVisualScriptNodeRegistry::GetSingleton()->GetFunctionEntryType();
  add.m_NewObjectGuid = plUuid::MakeUuid();
  add.m_Index = -1;
  plStatus res = pHistory->AddCommand(add);

  if (res.Succeeded())
  {
    plSetObjectPropertyCommand setName;
    setName.m_Object = add.m_NewObjectGuid;
    setName.m_sProperty = "Name";
    setName.m_NewValue = plString(sUniqueName);
    res = pHistory->AddCommand(setName);
  }

  if (res.Succeeded())
  {
    plMoveNodeCommand move;
    move.m_Object = add.m_NewObjectGuid;
    move.m_NewPos = plVec2(-300, 0);
    res = pHistory->AddCommand(move);
  }

  CommitTransaction(pHistory, res, "Failed to add the function");
  return res.Succeeded() ? add.m_NewObjectGuid : plUuid();
}

void plVisualScriptGraphEditing::RemoveFunction(const plVisualScriptNodeManager* pManager, const plDocumentObject* pEntry)
{
  plHybridArray<const plDocumentObject*, 64> toRemove;
  pManager->GetNodesInScope(pEntry->GetGuid(), toRemove, true);
  toRemove.PushBack(pEntry);

  plCommandHistory* pHistory = pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Remove Function");

  plStatus res(PL_SUCCESS);
  for (const plDocumentObject* pObject : toRemove)
  {
    plRemoveNodeCommand cmd;
    cmd.m_Object = pObject->GetGuid();
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      break;
  }

  if (res.Succeeded())
    RefreshReturnOwners(pManager);

  CommitTransaction(pHistory, res, "Failed to remove the function");
}

void plVisualScriptGraphEditing::RenameFunction(const plVisualScriptNodeManager* pManager, const plUuid& functionGuid, plStringView sNewName)
{
  const plDocumentObject* pEntry = pManager->GetObject(functionGuid);
  if (pEntry == nullptr || pManager->IsNode(pEntry) == false || pManager->IsFunctionEntry(pEntry) == false || sNewName.IsEmpty())
    return;

  if (GetNodeName(pEntry) == sNewName)
    return;

  if (const plDocumentObject* pOther = pManager->FindFunctionEntry(sNewName))
  {
    if (pOther != pEntry)
    {
      plEditorUiServices::MessageBoxInformation(plFmt("There is already a function named '{}'.", sNewName));
      return;
    }
  }

  // the document's accessor renames the calls and returns of the function along with it
  plObjectAccessorBase* pAccessor = pManager->GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Rename Function");
  plStatus res = pAccessor->SetValueByName(pEntry, "Name", plString(sNewName));
  if (res.Failed())
  {
    pAccessor->CancelTransaction();
    plEditorUiServices::MessageBoxStatus(res, "Failed to rename the function");
    return;
  }
  pAccessor->FinishTransaction();
}

void plVisualScriptGraphEditing::RefreshReturnOwners(const plVisualScriptNodeManager* pManager)
{
  plCommandHistory* pHistory = pManager->GetDocument()->GetCommandHistory();

  plHybridArray<const plDocumentObject*, 8> returns;
  for (auto pObject : pManager->GetRootObject()->GetChildren())
  {
    if (pManager->IsNode(pObject) && pManager->IsReturn(pObject))
      returns.PushBack(pObject);
  }

  for (const plDocumentObject* pReturn : returns)
  {
    const plDocumentObject* pOwner = pManager->FindReturnOwner(pReturn);
    const plString sOwnerName = pOwner != nullptr ? GetNodeName(pOwner) : plString();

    plVariant currentFunction = pReturn->GetTypeAccessor().GetValue("Function");
    if (currentFunction.IsA<plString>() && currentFunction.Get<plString>() == sOwnerName)
      continue;

    // the owner decides the pins, so this goes through the mirrored-pin choreography
    const plDocumentObject* dependents[] = {pReturn};
    plVisualScriptMirroredPins::ApplyWithDependents(pHistory, pManager, plMakeArrayPtr(dependents), [&]() -> plStatus
      {
        plSetObjectPropertyCommand cmd;
        cmd.m_Object = pReturn->GetGuid();
        cmd.m_sProperty = "Function";
        cmd.m_NewValue = sOwnerName;
        return pHistory->AddCommand(cmd);
      })
      .IgnoreResult();
  }
}

//////////////////////////////////////////////////////////////////////////
// Edits

void plVisualScriptGraphEditing::RemoveNodes(const plVisualScriptNodeManager* pManager, plArrayPtr<const plDocumentObject* const> nodes)
{
  if (nodes.IsEmpty())
    return;

  // a sub-graph node takes its contents with it; the boundary nodes of the shown sub-graph and the entry of a function graph stay
  plHybridArray<const plDocumentObject*, 64> toRemove;
  plHybridArray<const plDocumentObject*, 32> contents;
  for (const plDocumentObject* pObject : nodes)
  {
    if (pManager->IsSubGraphBoundary(pObject) || pManager->IsFunctionEntry(pObject))
      continue;

    if (toRemove.Contains(pObject) == false)
      toRemove.PushBack(pObject);

    if (pManager->IsSubGraph(pObject))
    {
      pManager->GetNodesInScope(pObject->GetGuid(), contents, true);
      for (const plDocumentObject* pContent : contents)
      {
        if (toRemove.Contains(pContent) == false)
          toRemove.PushBack(pContent);
      }
    }
  }

  if (toRemove.IsEmpty())
  {
    plEditorUiServices::MessageBoxInformation("The inputs and outputs of a sub-graph and the entry of a function cannot be removed. Expand the sub-graph or remove the function from the Functions panel instead.");
    return;
  }

  plCommandHistory* pHistory = pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Remove Nodes");

  plStatus res(PL_SUCCESS);
  for (const plDocumentObject* pObject : toRemove)
  {
    res = pManager->CanRemove(pObject);
    if (res.Failed())
      break;

    plRemoveNodeCommand cmd;
    cmd.m_Object = pObject->GetGuid();
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      break;
  }

  if (res.Succeeded())
    RefreshReturnOwners(pManager);

  CommitTransaction(pHistory, res, "Failed to remove node");
}

void plVisualScriptGraphEditing::EditConnections(const plVisualScriptNodeManager* pManager, plStringView sTransaction, plDelegate<void()> edit)
{
  plCommandHistory* pHistory = pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction(sTransaction);

  edit();
  RefreshReturnOwners(pManager);

  pHistory->FinishTransaction();
}

void plVisualScriptGraphEditing::InsertRerouteOnConnection(const plVisualScriptNodeManager* pManager, const plDocumentObject* pConnectionObject, const plVec2& vPos)
{
  const plVisualGraphConnection* pConnection = pManager->GetConnectionIfExists(pConnectionObject);
  if (pConnection == nullptr)
    return;

  auto& sourcePin = static_cast<const plVisualScriptPin&>(pConnection->GetSourcePin());
  auto& targetPin = static_cast<const plVisualScriptPin&>(pConnection->GetTargetPin());

  const plUuid sourceGuid = sourcePin.GetParent()->GetGuid();
  const plUuid targetGuid = targetPin.GetParent()->GetGuid();
  const plString sSourcePin = sourcePin.GetName();
  const plString sTargetPin = targetPin.GetName();
  const bool bExec = sourcePin.IsExecutionPin();
  const plUuid scope = pManager->GetNodeScope(sourcePin.GetParent());

  plCommandHistory* pHistory = pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Insert Reroute");

  plStatus res = plNodeCommands::DisconnectAndRemoveCommand(pHistory, pConnectionObject->GetGuid());

  plUuid rerouteGuid = plUuid::MakeUuid();
  if (res.Succeeded())
  {
    plAddObjectCommand cmd;
    cmd.m_pType = plVisualScriptNodeRegistry::GetSingleton()->GetRerouteType();
    cmd.m_NewObjectGuid = rerouteGuid;
    cmd.m_Index = -1;
    res = pHistory->AddCommand(cmd);
  }

  if (res.Succeeded())
  {
    plSetObjectPropertyCommand cmd;
    cmd.m_Object = rerouteGuid;
    cmd.m_sProperty = "Kind";
    cmd.m_NewValue = plInt64(bExec ? plVisualScriptPinKind::Exec : plVisualScriptPinKind::Data);
    res = pHistory->AddCommand(cmd);
  }

  if (res.Succeeded() && scope.IsValid())
  {
    plSetObjectPropertyCommand cmd;
    cmd.m_Object = rerouteGuid;
    cmd.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
    cmd.m_NewValue = scope;
    res = pHistory->AddCommand(cmd);
  }

  if (res.Succeeded())
  {
    plMoveNodeCommand move;
    move.m_Object = rerouteGuid;
    move.m_NewPos = vPos;
    res = pHistory->AddCommand(move);
  }

  if (res.Succeeded())
  {
    const plDocumentObject* pReroute = pManager->GetObject(rerouteGuid);
    const plDocumentObject* pSource = pManager->GetObject(sourceGuid);
    const plDocumentObject* pTarget = pManager->GetObject(targetGuid);

    const plVisualGraphPin* pSourcePin = pSource ? pManager->GetOutputPinByName(pSource, sSourcePin) : nullptr;
    const plVisualGraphPin* pTargetPin = pTarget ? pManager->GetInputPinByName(pTarget, sTargetPin) : nullptr;
    const plVisualGraphPin* pRerouteIn = pReroute ? pManager->GetInputPinByName(pReroute, "In") : nullptr;
    const plVisualGraphPin* pRerouteOut = pReroute ? pManager->GetOutputPinByName(pReroute, "Out") : nullptr;

    if (pSourcePin && pTargetPin && pRerouteIn && pRerouteOut)
    {
      res = plNodeCommands::AddAndConnectCommand(pHistory, pManager->GetConnectionType(), *pSourcePin, *pRerouteIn);
      if (res.Succeeded())
        res = plNodeCommands::AddAndConnectCommand(pHistory, pManager->GetConnectionType(), *pRerouteOut, *pTargetPin);
    }
    else
    {
      res = plStatus("The reroute pins could not be created");
    }
  }

  CommitTransaction(pHistory, res, "Inserting the reroute failed");
}

void plVisualScriptGraphEditing::InsertPortalOnConnection(const plVisualScriptNodeManager* pManager, const plDocumentObject* pConnectionObject, const plVec2& vPos)
{
  const plVisualGraphConnection* pConnection = pManager->GetConnectionIfExists(pConnectionObject);
  if (pConnection == nullptr)
    return;

  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
  auto& sourcePin = static_cast<const plVisualScriptPin&>(pConnection->GetSourcePin());
  auto& targetPin = static_cast<const plVisualScriptPin&>(pConnection->GetTargetPin());

  const plUuid sourceGuid = sourcePin.GetParent()->GetGuid();
  const plUuid targetGuid = targetPin.GetParent()->GetGuid();
  const plString sSourcePin = sourcePin.GetName();
  const plString sTargetPin = targetPin.GetName();
  const bool bExec = sourcePin.IsExecutionPin();
  const plUuid scope = pManager->GetNodeScope(sourcePin.GetParent());

  // a default name that is not taken yet
  plStringBuilder sDefault;
  {
    plStringView sBase = sSourcePin.IsEmpty() ? (bExec ? "Flow" : "Value") : sSourcePin.GetView();
    sDefault = sBase;
    for (plUInt32 i = 2; pManager->FindPortalIn(sDefault, bExec) != nullptr; ++i)
      sDefault.SetFormat("{}{}", sBase, i);
  }

  const plString sPortalName = AskPortalName(pManager, bExec, sDefault);
  if (sPortalName.IsEmpty())
    return;

  plCommandHistory* pHistory = pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Insert Portal");

  plStatus res = plNodeCommands::DisconnectAndRemoveCommand(pHistory, pConnectionObject->GetGuid());

  auto AddPortal = [&](bool bIn, const plUuid& guid, const plVec2& vNodePos)
  {
    if (res.Failed())
      return;

    res = AddScopedNode(pHistory, pRegistry->GetPortalType(bIn, bExec), guid, scope, vNodePos);
    if (res.Failed())
      return;

    plSetObjectPropertyCommand setName;
    setName.m_Object = guid;
    setName.m_sProperty = "Name";
    setName.m_NewValue = sPortalName;
    res = pHistory->AddCommand(setName);
  };

  const plUuid inGuid = plUuid::MakeUuid();
  const plUuid outGuid = plUuid::MakeUuid();
  AddPortal(true, inGuid, vPos + plVec2(-160.0f, -12.0f));
  AddPortal(false, outGuid, vPos + plVec2(40.0f, -12.0f));

  if (res.Succeeded())
  {
    const plDocumentObject* pSource = pManager->GetObject(sourceGuid);
    const plDocumentObject* pTarget = pManager->GetObject(targetGuid);
    const plDocumentObject* pIn = pManager->GetObject(inGuid);
    const plDocumentObject* pOut = pManager->GetObject(outGuid);

    const plVisualGraphPin* pSourcePin = pSource ? pManager->GetOutputPinByName(pSource, sSourcePin) : nullptr;
    const plVisualGraphPin* pTargetPin = pTarget ? pManager->GetInputPinByName(pTarget, sTargetPin) : nullptr;
    const plVisualGraphPin* pInPin = pIn ? pManager->GetInputPinByName(pIn, "In") : nullptr;
    const plVisualGraphPin* pOutPin = pOut ? pManager->GetOutputPinByName(pOut, "Out") : nullptr;

    if (pSourcePin && pTargetPin && pInPin && pOutPin)
    {
      res = plNodeCommands::AddAndConnectCommand(pHistory, pManager->GetConnectionType(), *pSourcePin, *pInPin);
      if (res.Succeeded())
        res = plNodeCommands::AddAndConnectCommand(pHistory, pManager->GetConnectionType(), *pOutPin, *pTargetPin);
    }
    else
    {
      res = plStatus("The portal pins could not be created");
    }
  }

  CommitTransaction(pHistory, res, "Inserting the portal failed");
}

bool plVisualScriptGraphEditing::CanCollapse(const plVisualScriptNodeManager* pManager, const plDocumentObject* pNode)
{
  return pManager->IsEntryNode(pNode) == false && pManager->IsReturn(pNode) == false && pManager->IsSubGraphBoundary(pNode) == false;
}

plUuid plVisualScriptGraphEditing::CollapseToSubGraph(const plVisualScriptNodeManager* pManager, plArrayPtr<const plDocumentObject* const> selection, const plRectFloat& bounds, const plUuid& scope)
{
  if (selection.IsEmpty())
  {
    plEditorUiServices::MessageBoxInformation("Select the nodes to collapse first.");
    return plUuid();
  }

  for (const plDocumentObject* pObject : selection)
  {
    if (CanCollapse(pManager, pObject) == false)
    {
      plEditorUiServices::MessageBoxInformation("Entry nodes, Return nodes and sub-graph boundaries cannot be collapsed into a sub-graph.");
      return plUuid();
    }
  }

  plHashSet<const plDocumentObject*> selected;
  for (const plDocumentObject* pObject : selection)
    selected.Insert(pObject);

  // crossing connections become the pins of the sub-graph
  struct Crossing
  {
    plUuid m_Source;
    plString m_sSourcePin;
    plUuid m_Target;
    plString m_sTargetPin;
    plUuid m_ConnectionObject;
    plString m_sDeclName;
    plVisualScriptPinType::Enum m_Type;
    bool m_bInbound;
  };

  plHybridArray<Crossing, 16> crossings;
  plHashSet<plString> usedInputNames, usedOutputNames;

  auto MakeUniqueName = [](plHashSet<plString>& ref_used, plStringView sWanted, plStringBuilder& out_sName)
  {
    out_sName = sWanted.IsEmpty() ? "Pin" : sWanted;
    plStringBuilder sBase = out_sName;
    for (plUInt32 i = 2; ref_used.Contains(out_sName); ++i)
      out_sName.SetFormat("{}{}", sBase, i);
    ref_used.Insert(out_sName);
  };

  // Invalid doubles as "execution" for pin declarations, so an undeduced data pin must never end up there
  auto IsUsable = [](plVisualScriptDataType::Enum type)
  { return type > plVisualScriptDataType::Invalid && type < plVisualScriptDataType::Count; };

  // inbound: keyed by source pin so one outside value feeds one sub-graph input
  plMap<plString, plString> inboundBySource;

  auto AddCrossing = [&](const plVisualGraphConnection* pConnection)
  {
    auto& sourcePin = static_cast<const plVisualScriptPin&>(pConnection->GetSourcePin());
    auto& targetPin = static_cast<const plVisualScriptPin&>(pConnection->GetTargetPin());
    const bool bSourceIn = selected.Contains(sourcePin.GetParent());
    const bool bTargetIn = selected.Contains(targetPin.GetParent());
    if (bSourceIn == bTargetIn)
      return;

    auto& crossing = crossings.ExpandAndGetRef();
    crossing.m_Source = sourcePin.GetParent()->GetGuid();
    crossing.m_sSourcePin = sourcePin.GetName();
    crossing.m_Target = targetPin.GetParent()->GetGuid();
    crossing.m_sTargetPin = targetPin.GetName();
    crossing.m_ConnectionObject = pConnection->GetParent()->GetGuid();
    crossing.m_bInbound = bTargetIn;

    plVisualScriptDataType::Enum dataType = plVisualScriptDataType::Invalid;
    if (sourcePin.IsExecutionPin() == false)
    {
      dataType = sourcePin.GetResolvedScriptDataType();
      if (IsUsable(dataType) == false)
        dataType = targetPin.GetResolvedScriptDataType();
      if (dataType == plVisualScriptDataType::EnumValue || dataType == plVisualScriptDataType::BitflagValue)
        dataType = plVisualScriptDataType::Int64;
      if (IsUsable(dataType) == false)
        dataType = plVisualScriptDataType::Variant;
    }
    crossing.m_Type = static_cast<plVisualScriptPinType::Enum>(dataType);

    plStringBuilder sName;
    if (crossing.m_bInbound)
    {
      plStringBuilder sKey;
      sKey.Set(plConversionUtils::ToString(crossing.m_Source, sKey).GetView(), ".", crossing.m_sSourcePin);
      auto itExisting = inboundBySource.Find(sKey);
      if (itExisting.IsValid())
      {
        crossing.m_sDeclName = itExisting.Value();
      }
      else
      {
        plStringView sWanted = crossing.m_sTargetPin;
        if (sWanted.IsEmpty())
          sWanted = sourcePin.IsExecutionPin() ? "Exec" : "In";
        MakeUniqueName(usedInputNames, sWanted, sName);
        crossing.m_sDeclName = sName;
        inboundBySource.Insert(sKey, sName);
      }
    }
    else
    {
      plStringView sWanted = crossing.m_sSourcePin;
      if (sWanted.IsEmpty())
        sWanted = sourcePin.IsExecutionPin() ? "Exec" : "Out";
      MakeUniqueName(usedOutputNames, sWanted, sName);
      crossing.m_sDeclName = sName;
    }
  };

  // a crossing connection has exactly one selected end, so it is seen exactly once
  for (const plDocumentObject* pObject : selection)
  {
    for (auto& pPin : pManager->GetInputPins(pObject))
      for (const plVisualGraphConnection* pConnection : pManager->GetConnections(*pPin))
        AddCrossing(pConnection);

    for (auto& pPin : pManager->GetOutputPins(pObject))
      for (const plVisualGraphConnection* pConnection : pManager->GetConnections(*pPin))
        AddCrossing(pConnection);
  }

  // outbound data pins shared by several outside targets get one declaration
  {
    plMap<plString, plString> outboundBySource;
    for (auto& crossing : crossings)
    {
      if (crossing.m_bInbound)
        continue;

      plStringBuilder sKey;
      sKey.Set(plConversionUtils::ToString(crossing.m_Source, sKey).GetView(), ".", crossing.m_sSourcePin);
      auto itExisting = outboundBySource.Find(sKey);
      if (itExisting.IsValid())
        crossing.m_sDeclName = itExisting.Value();
      else
        outboundBySource.Insert(sKey, crossing.m_sDeclName);
    }
  }

  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
  plCommandHistory* pHistory = pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Collapse to Sub-graph");

  const plUuid subGraphGuid = plUuid::MakeUuid();
  const plUuid inputsGuid = plUuid::MakeUuid();
  const plUuid outputsGuid = plUuid::MakeUuid();

  const plVec2 vCenter = bounds.GetCenter();
  plStatus res = AddScopedNode(pHistory, pRegistry->GetSubGraphType(), subGraphGuid, scope, vCenter);
  if (res.Succeeded())
    res = AddScopedNode(pHistory, pRegistry->GetSubGraphInputsType(), inputsGuid, subGraphGuid, plVec2(bounds.Left() - 350.0f, vCenter.y));
  if (res.Succeeded())
    res = AddScopedNode(pHistory, pRegistry->GetSubGraphOutputsType(), outputsGuid, subGraphGuid, plVec2(bounds.Right() + 350.0f, vCenter.y));

  // 1. cut the crossing connections
  for (auto& crossing : crossings)
  {
    if (res.Failed())
      break;
    res = plNodeCommands::DisconnectAndRemoveCommand(pHistory, crossing.m_ConnectionObject);
  }

  // 2. move the selection inside
  for (const plDocumentObject* pObject : selection)
  {
    if (res.Failed())
      break;

    plSetObjectPropertyCommand cmd;
    cmd.m_Object = pObject->GetGuid();
    cmd.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
    cmd.m_NewValue = subGraphGuid;
    res = pHistory->AddCommand(cmd);
  }

  // 3. declare the boundary pins (one per unique declaration name)
  auto AddDecl = [&](const plUuid& boundaryGuid, plStringView sName, plVisualScriptPinType::Enum type)
  {
    if (res.Failed())
      return;

    plAddObjectCommand cmd;
    cmd.m_pType = plGetStaticRTTI<plVisualScriptPinDecl>();
    cmd.m_Parent = boundaryGuid;
    cmd.m_sParentProperty = "Pins";
    cmd.m_Index = -1;
    cmd.m_NewObjectGuid = plUuid::MakeUuid();
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      return;

    plSetObjectPropertyCommand setName;
    setName.m_Object = cmd.m_NewObjectGuid;
    setName.m_sProperty = "Name";
    plHashedString sHashedName;
    sHashedName.Assign(sName);
    setName.m_NewValue = sHashedName;
    res = pHistory->AddCommand(setName);
    if (res.Failed())
      return;

    plSetObjectPropertyCommand setType;
    setType.m_Object = cmd.m_NewObjectGuid;
    setType.m_sProperty = "Type";
    setType.m_NewValue = plInt64(type);
    res = pHistory->AddCommand(setType);
  };

  {
    plHashSet<plString> declared;
    for (auto& crossing : crossings)
    {
      plStringBuilder sKey;
      sKey.Set(crossing.m_bInbound ? "in:" : "out:", crossing.m_sDeclName);
      if (declared.Insert(sKey))
        continue;

      AddDecl(crossing.m_bInbound ? inputsGuid : outputsGuid, crossing.m_sDeclName, crossing.m_Type);
    }
  }

  // 4. the sub-graph node mirrors the boundary declarations
  if (res.Succeeded())
  {
    if (const plDocumentObject* pSubGraph = pManager->GetObject(subGraphGuid))
      res = plVisualScriptMirroredPins::RecreatePins(pHistory, pSubGraph);
  }

  // 5. rewire through the boundary
  for (auto& crossing : crossings)
  {
    if (res.Failed())
      break;

    const plDocumentObject* pSubGraph = pManager->GetObject(subGraphGuid);
    const plDocumentObject* pInputs = pManager->GetObject(inputsGuid);
    const plDocumentObject* pOutputs = pManager->GetObject(outputsGuid);
    const plDocumentObject* pSource = pManager->GetObject(crossing.m_Source);
    const plDocumentObject* pTarget = pManager->GetObject(crossing.m_Target);
    if (pSubGraph == nullptr || pInputs == nullptr || pOutputs == nullptr || pSource == nullptr || pTarget == nullptr)
    {
      res = plStatus("A node of the collapsed selection could not be found");
      break;
    }

    const plVisualGraphPin* pSourcePin = pManager->GetOutputPinByName(pSource, crossing.m_sSourcePin);
    const plVisualGraphPin* pTargetPin = pManager->GetInputPinByName(pTarget, crossing.m_sTargetPin);

    if (crossing.m_bInbound)
    {
      const plVisualGraphPin* pSubGraphIn = pManager->GetInputPinByName(pSubGraph, crossing.m_sDeclName);
      const plVisualGraphPin* pBoundaryOut = pManager->GetOutputPinByName(pInputs, crossing.m_sDeclName);
      if (pSourcePin && pSubGraphIn && pManager->HasConnections(*pSubGraphIn) == false)
        res = plNodeCommands::AddAndConnectCommand(pHistory, pManager->GetConnectionType(), *pSourcePin, *pSubGraphIn);
      if (res.Succeeded() && pBoundaryOut && pTargetPin)
        res = plNodeCommands::AddAndConnectCommand(pHistory, pManager->GetConnectionType(), *pBoundaryOut, *pTargetPin);
    }
    else
    {
      const plVisualGraphPin* pBoundaryIn = pManager->GetInputPinByName(pOutputs, crossing.m_sDeclName);
      const plVisualGraphPin* pSubGraphOut = pManager->GetOutputPinByName(pSubGraph, crossing.m_sDeclName);
      if (pSourcePin && pBoundaryIn && pManager->HasConnections(*pBoundaryIn) == false)
        res = plNodeCommands::AddAndConnectCommand(pHistory, pManager->GetConnectionType(), *pSourcePin, *pBoundaryIn);
      if (res.Succeeded() && pSubGraphOut && pTargetPin)
        res = plNodeCommands::AddAndConnectCommand(pHistory, pManager->GetConnectionType(), *pSubGraphOut, *pTargetPin);
    }
  }

  if (res.Succeeded())
    RefreshReturnOwners(pManager);

  CommitTransaction(pHistory, res, "Collapsing the selection failed");
  return res.Succeeded() ? subGraphGuid : plUuid();
}

void plVisualScriptGraphEditing::ExpandSelectedSubGraph(const plVisualScriptNodeManager* pManager, plArrayPtr<const plDocumentObject* const> selection)
{
  const plDocumentObject* pSubGraph = nullptr;
  for (const plDocumentObject* pObject : selection)
  {
    if (pManager->IsSubGraph(pObject) == false)
      continue;

    if (pSubGraph != nullptr)
    {
      plEditorUiServices::MessageBoxInformation("Select a single sub-graph to expand.");
      return;
    }

    pSubGraph = pObject;
  }

  if (pSubGraph == nullptr)
  {
    plEditorUiServices::MessageBoxInformation("Select the sub-graph to expand first.");
    return;
  }

  ExpandSubGraph(pManager, pSubGraph);
}

void plVisualScriptGraphEditing::ExpandSubGraph(const plVisualScriptNodeManager* pManager, const plDocumentObject* pSubGraph)
{
  if (pSubGraph == nullptr || pManager->IsSubGraph(pSubGraph) == false)
    return;

  const plUuid subGraphGuid = pSubGraph->GetGuid();
  const plUuid parentScope = pManager->GetNodeScope(pSubGraph);
  const plDocumentObject* pInputs = pManager->GetSubGraphBoundary(pSubGraph, true);
  const plDocumentObject* pOutputs = pManager->GetSubGraphBoundary(pSubGraph, false);

  struct Rewire
  {
    plUuid m_Source;
    plString m_sSourcePin;
    plUuid m_Target;
    plString m_sTargetPin;
  };
  plHybridArray<Rewire, 16> rewires;
  plHybridArray<plUuid, 32> connectionsToRemove;

  auto CollectConnections = [&](const plDocumentObject* pNode)
  {
    if (pNode == nullptr)
      return;
    for (auto& pPin : pManager->GetInputPins(pNode))
      for (auto pConnection : pManager->GetConnections(*pPin))
        if (connectionsToRemove.Contains(pConnection->GetParent()->GetGuid()) == false)
          connectionsToRemove.PushBack(pConnection->GetParent()->GetGuid());
    for (auto& pPin : pManager->GetOutputPins(pNode))
      for (auto pConnection : pManager->GetConnections(*pPin))
        if (connectionsToRemove.Contains(pConnection->GetParent()->GetGuid()) == false)
          connectionsToRemove.PushBack(pConnection->GetParent()->GetGuid());
  };

  // outside source -> sub-graph input X, inputs.X -> inside target  ==>  outside source -> inside target
  for (auto& pPin : pManager->GetInputPins(pSubGraph))
  {
    const plVisualGraphPin* pBoundaryOut = pInputs ? pManager->GetOutputPinByName(pInputs, pPin->GetName()) : nullptr;
    if (pBoundaryOut == nullptr)
      continue;

    for (auto pOuter : pManager->GetConnections(*pPin))
    {
      for (auto pInner : pManager->GetConnections(*pBoundaryOut))
      {
        auto& rewire = rewires.ExpandAndGetRef();
        rewire.m_Source = pOuter->GetSourcePin().GetParent()->GetGuid();
        rewire.m_sSourcePin = pOuter->GetSourcePin().GetName();
        rewire.m_Target = pInner->GetTargetPin().GetParent()->GetGuid();
        rewire.m_sTargetPin = pInner->GetTargetPin().GetName();
      }
    }
  }

  // inside source -> outputs.Y, sub-graph output Y -> outside target  ==>  inside source -> outside target
  for (auto& pPin : pManager->GetOutputPins(pSubGraph))
  {
    const plVisualGraphPin* pBoundaryIn = pOutputs ? pManager->GetInputPinByName(pOutputs, pPin->GetName()) : nullptr;
    if (pBoundaryIn == nullptr)
      continue;

    for (auto pInner : pManager->GetConnections(*pBoundaryIn))
    {
      for (auto pOuter : pManager->GetConnections(*pPin))
      {
        auto& rewire = rewires.ExpandAndGetRef();
        rewire.m_Source = pInner->GetSourcePin().GetParent()->GetGuid();
        rewire.m_sSourcePin = pInner->GetSourcePin().GetName();
        rewire.m_Target = pOuter->GetTargetPin().GetParent()->GetGuid();
        rewire.m_sTargetPin = pOuter->GetTargetPin().GetName();
      }
    }
  }

  CollectConnections(pSubGraph);
  CollectConnections(pInputs);
  CollectConnections(pOutputs);

  plHybridArray<const plDocumentObject*, 32> contents;
  pManager->GetNodesInScope(subGraphGuid, contents, false);

  // keep the relative layout, centred on where the sub-graph node was
  plVec2 vContentCenter = plVec2::MakeZero();
  plUInt32 uiCounted = 0;
  for (const plDocumentObject* pContent : contents)
  {
    if (pContent == pInputs || pContent == pOutputs)
      continue;
    vContentCenter += pManager->GetNodePos(pContent);
    ++uiCounted;
  }
  if (uiCounted > 0)
    vContentCenter /= static_cast<float>(uiCounted);
  const plVec2 vOffset = pManager->GetNodePos(pSubGraph) - vContentCenter;

  plCommandHistory* pHistory = pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Expand Sub-graph");

  plStatus res(PL_SUCCESS);

  for (const plUuid& connection : connectionsToRemove)
  {
    if (res.Failed())
      break;
    res = plNodeCommands::DisconnectAndRemoveCommand(pHistory, connection);
  }

  for (const plDocumentObject* pContent : contents)
  {
    if (res.Failed())
      break;
    if (pContent == pInputs || pContent == pOutputs)
      continue;

    plSetObjectPropertyCommand cmd;
    cmd.m_Object = pContent->GetGuid();
    cmd.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
    cmd.m_NewValue = parentScope;
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      break;

    plMoveNodeCommand move;
    move.m_Object = pContent->GetGuid();
    move.m_NewPos = pManager->GetNodePos(pContent) + vOffset;
    res = pHistory->AddCommand(move);
  }

  for (auto& rewire : rewires)
  {
    if (res.Failed())
      break;

    const plDocumentObject* pSource = pManager->GetObject(rewire.m_Source);
    const plDocumentObject* pTarget = pManager->GetObject(rewire.m_Target);
    const plVisualGraphPin* pSourcePin = pSource ? pManager->GetOutputPinByName(pSource, rewire.m_sSourcePin) : nullptr;
    const plVisualGraphPin* pTargetPin = pTarget ? pManager->GetInputPinByName(pTarget, rewire.m_sTargetPin) : nullptr;
    if (pSourcePin == nullptr || pTargetPin == nullptr)
      continue;

    plVisualGraphObjectManager::CanConnectResult canConnect;
    if (pManager->CanConnect(pManager->GetConnectionType(), *pSourcePin, *pTargetPin, canConnect).Failed())
      continue;

    res = plNodeCommands::AddAndConnectCommand(pHistory, pManager->GetConnectionType(), *pSourcePin, *pTargetPin);
  }

  auto RemoveNode = [&](const plDocumentObject* pNode)
  {
    if (res.Failed() || pNode == nullptr)
      return;
    plRemoveNodeCommand cmd;
    cmd.m_Object = pNode->GetGuid();
    res = pHistory->AddCommand(cmd);
  };
  RemoveNode(pInputs);
  RemoveNode(pOutputs);
  RemoveNode(pSubGraph);

  if (res.Succeeded())
    RefreshReturnOwners(pManager);

  CommitTransaction(pHistory, res, "Expanding the sub-graph failed");
}

//////////////////////////////////////////////////////////////////////////
// plVisualScriptGraphController

plVisualScriptGraphController::plVisualScriptGraphController() = default;

plVisualScriptGraphController::~plVisualScriptGraphController()
{
  if (plScriptTools* pTools = plScriptTools::GetSingleton(); pTools != nullptr && m_ScriptToolsSubscription != 0)
    pTools->m_Events.RemoveEventHandler(m_ScriptToolsSubscription);

  if (m_pManager != nullptr)
    const_cast<plVisualScriptNodeManager*>(GetScriptManager())->m_NodeChangedEvent.RemoveEventHandler(plMakeDelegate(&plVisualScriptGraphController::NodeChangedHandler, this));
}

void plVisualScriptGraphController::Init(const plVisualGraphObjectManager* pManager)
{
  plVisualGraphController::Init(pManager);

  auto pScriptManager = const_cast<plVisualScriptNodeManager*>(GetScriptManager());

  // deduced types and coroutine flags change pins and titles without a document event
  pScriptManager->m_NodeChangedEvent.AddEventHandler(plMakeDelegate(&plVisualScriptGraphController::NodeChangedHandler, this));

  m_CurrentScope = plUuid();
  pScriptManager->SetCurrentViewScope(m_CurrentScope);

  if (plScriptTools* pTools = plScriptTools::GetSingleton(); pTools != nullptr && m_ScriptToolsSubscription == 0)
    m_ScriptToolsSubscription = pTools->m_Events.AddEventHandler(plMakeDelegate(&plVisualScriptGraphController::ScriptToolsEventHandler, this));
}

void plVisualScriptGraphController::ScriptToolsEventHandler(const plScriptToolsEvent& e)
{
  if (e.m_Type != plScriptToolsEvent::Type::VariablesChanged && e.m_Type != plScriptToolsEvent::Type::ReloadsChanged)
    InvalidateLook();
}

plString plVisualScriptGraphController::GetAssetGuid() const
{
  plStringBuilder sGuid;
  plConversionUtils::ToString(m_pManager->GetDocument()->GetGuid(), sGuid);
  return sGuid;
}

const plVisualScriptNodeManager* plVisualScriptGraphController::GetScriptManager() const
{
  return static_cast<const plVisualScriptNodeManager*>(m_pManager);
}

void plVisualScriptGraphController::NodeChangedHandler(const plDocumentObject* /*pObject*/)
{
  InvalidateLook();
}

// Look
//////////////////////////////////////////////////////////////////////////

void plVisualScriptGraphController::GetNodeLook(const plDocumentObject* pNode, plVisualGraphNodeLook& out_look) const
{
  if (m_pManager->IsComment(pNode))
  {
    plVisualGraphController::GetNodeLook(pNode, out_look);
    return;
  }

  const plVisualScriptNodeManager* pManager = GetScriptManager();

  plStringBuilder sError;
  out_look.m_bError = pManager->GetNodeError(pNode, sError);
  out_look.m_sTooltip = sError;

  if (pManager->IsReroute(pNode))
  {
    out_look.m_Kind = plVisualGraphNodeLook::Kind::Reroute;

    if (out_look.m_bError == false)
    {
      const plVisualScriptDataType::Enum type = pManager->GetDeductedType(pNode);
      sError.Set("Reroute: ", type != plVisualScriptDataType::Invalid ? plVisualScriptDataType::GetName(type) : "Unknown");
      out_look.m_sTooltip = sError;
    }

    return;
  }

  if (pManager->IsPortalIn(pNode) || pManager->IsPortalOut(pNode))
  {
    const plString sName = pManager->GetPortalName(pNode);
    out_look.m_Kind = plVisualGraphNodeLook::Kind::Portal;
    out_look.m_sTitle = sName.IsEmpty() ? plString("<unnamed>") : sName;
    return;
  }

  plStringBuilder sTitle;
  plStringBuilder sSubtitle;
  plVisualScriptGraphEditing::ComputeNodeTitle(pManager, pNode, sTitle, sSubtitle);
  out_look.m_sTitle = sTitle;
  out_look.m_sSubtitle = sSubtitle;
  out_look.m_sIcon = plVisualScriptGraphEditing::GetNodeIcon(pManager, pNode);

  if (const plColorAttribute* pColor = pNode->GetType()->GetAttributeByType<plColorAttribute>())
    out_look.m_HeaderColor = pColor->GetColor();

  // Without an error to show, the tooltip is the description of the engine function, property or type the node calls.
  if (!out_look.m_bError)
  {
    if (const plVisualScriptNodeRegistry::NodeDesc* pDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pNode->GetType()); pDesc != nullptr && pDesc->m_pTargetType != nullptr)
    {
      plScriptApiDocs::EnsureLoaded();
      const plStringView sTypeName = pDesc->m_pTargetType->GetTypeName();
      plStringView sText = pDesc->m_TargetProperties.IsEmpty() ? plStringView() : plScriptApiDocs::GetMemberDescription(sTypeName, pDesc->m_TargetProperties[0]->GetPropertyName());
      if (sText.IsEmpty())
        sText = plScriptApiDocs::GetTypeDescription(sTypeName);
      out_look.m_sTooltip = sText;
    }
  }

  const plScriptTools* pTools = plScriptTools::GetSingleton();
  if (pTools == nullptr)
    return;

  // The last compile's problems on this node.
  for (const plScriptDiagnostic& diag : pTools->GetProblems())
  {
    if (diag.m_NodeGuid != pNode->GetGuid())
      continue;

    if (diag.m_Severity == plScriptDiagnosticSeverity::Error)
    {
      out_look.m_bError = true;
      out_look.m_sTooltip = diag.m_sMessage;
    }
    else
    {
      out_look.m_BadgeLines.PushBack({diag.m_sMessage, plColorGammaUB(214, 170, 90)});
    }
  }

  if (pTools->HasNodeBreakpoint(pNode->GetGuid()))
    out_look.m_BadgeLines.PushBack({"\xE2\x97\x8F Breakpoint", plColorGammaUB(224, 96, 96)});

  // While stopped, the node of the selected frame is the one that runs.
  if (pTools->IsStopped() && pTools->GetSelectedFrame() < pTools->GetFrames().GetCount())
    out_look.m_bHighlighted = pTools->GetFrames()[pTools->GetSelectedFrame()].m_Location.m_NodeGuid == pNode->GetGuid();
}

plColorGammaUB plVisualScriptGraphController::GetPinColor(const plVisualScriptPin& pin) const
{
  const plDocumentObject* pNode = pin.GetParent();
  if (GetScriptManager()->IsReroute(pNode) || GetScriptManager()->IsPortalIn(pNode) || GetScriptManager()->IsPortalOut(pNode))
    return plVisualScriptGraphEditing::GetRoutingNodeColor(GetScriptManager(), pNode);

  plColorGammaUB color = pin.GetColor();
  plVisualScriptGraphEditing::GetDeducedPinColor(pin, color);
  return color;
}

void plVisualScriptGraphController::GetPinLook(const plVisualGraphPin& pin, plVisualGraphPinLook& out_look) const
{
  plVisualGraphController::GetPinLook(pin, out_look);

  const plVisualScriptNodeManager* pManager = GetScriptManager();
  const plVisualScriptPin& vsPin = plStaticCast<const plVisualScriptPin&>(pin);
  const plDocumentObject* pNode = pin.GetParent();

  out_look.m_Color = GetPinColor(vsPin);
  out_look.m_bRequired = plVisualScriptGraphEditing::IsMissingRequiredConnection(pManager, vsPin);

  // reroutes and portals are small pills: their pins have no labels
  const bool bRouting = pManager->IsReroute(pNode) || pManager->IsPortalIn(pNode) || pManager->IsPortalOut(pNode);
  out_look.m_sLabel = bRouting ? plString() : plString(pin.GetName());

  plStringBuilder sTooltip;
  plVisualScriptGraphEditing::GetPinTooltip(pManager, vsPin, sTooltip);
  out_look.m_sTooltip = sTooltip;
}

void plVisualScriptGraphController::GetConnectionLook(const plDocumentObject* pConnection, plVisualGraphConnectionLook& out_look) const
{
  const plVisualGraphConnection& connection = m_pManager->GetConnection(pConnection);
  out_look.m_SourceColor = GetPinColor(plStaticCast<const plVisualScriptPin&>(connection.GetSourcePin()));
  out_look.m_TargetColor = GetPinColor(plStaticCast<const plVisualScriptPin&>(connection.GetTargetPin()));
}

bool plVisualScriptGraphController::IsNodeShown(const plDocumentObject* pNode) const
{
  return GetScriptManager()->GetDisplayScope(pNode) == m_CurrentScope;
}

bool plVisualScriptGraphController::IsLayoutFlowPin(const plVisualGraphPin& pin) const
{
  return plVisualScriptGraphEditing::IsLayoutFlowPin(pin);
}

// Editing
//////////////////////////////////////////////////////////////////////////

void plVisualScriptGraphController::RemoveNodes(plArrayPtr<const plDocumentObject* const> nodes)
{
  plVisualScriptGraphEditing::RemoveNodes(GetScriptManager(), nodes);
}

void plVisualScriptGraphController::ConnectPins(const plVisualGraphPin& sourcePin, const plVisualGraphPin& targetPin)
{
  plVisualScriptGraphEditing::EditConnections(GetScriptManager(), "Connect Pins", [&]()
    { plVisualGraphController::ConnectPins(sourcePin, targetPin); });
}

void plVisualScriptGraphController::DisconnectConnection(const plDocumentObject* pConnection)
{
  plVisualScriptGraphEditing::EditConnections(GetScriptManager(), "Disconnect Pins", [&]()
    { plVisualGraphController::DisconnectConnection(pConnection); });
}

void plVisualScriptGraphController::DisconnectPin(const plVisualGraphPin& pin)
{
  plVisualScriptGraphEditing::EditConnections(GetScriptManager(), "Disconnect Pins", [&]()
    { plVisualGraphController::DisconnectPin(pin); });
}

// Interaction
//////////////////////////////////////////////////////////////////////////

void plVisualScriptGraphController::ExtendNodeMenu(const plDocumentObject* pNode, plVisualGraphMenu& ref_menu)
{
  const plVisualScriptNodeManager* pManager = GetScriptManager();

  if (plScriptTools* pTools = plScriptTools::GetSingleton(); pTools != nullptr && !pManager->IsComment(pNode))
  {
    const plUuid node = pNode->GetGuid();
    ref_menu.AddSeparator();
    ref_menu.AddAction(pTools->HasNodeBreakpoint(node) ? "Remove Breakpoint" : "Add Breakpoint", [this, node]()
      {
        if (plScriptTools* pTools = plScriptTools::GetSingleton())
          pTools->ToggleNodeBreakpoint(GetAssetGuid(), node);
      });
  }

  if (pManager->IsSubGraph(pNode))
  {
    ref_menu.AddSeparator();

    const plUuid scope = pNode->GetGuid();
    ref_menu.AddAction("Enter Sub-graph", [this, scope]()
      { NavigateToScope(scope); });

    ref_menu.AddAction("Expand Sub-graph", [this, pNode]()
      { plVisualScriptGraphEditing::ExpandSubGraph(GetScriptManager(), pNode); });
  }

  if (plVisualScriptGraphEditing::CanCollapse(pManager, pNode) == false)
    return;

  ref_menu.AddSeparator();
  ref_menu.AddAction("Collapse to Sub-graph", [this]()
    { CollapseSelectionToSubGraph(); });
}

void plVisualScriptGraphController::ExtendConnectionMenu(const plDocumentObject* pConnection, plVisualGraphMenu& ref_menu)
{
  // the view records the graph position of the click that opened the menu
  const plVec2 vPos = plEditorUiGraphView::GetLastPointerGraphPos();

  // the view adds "Insert Reroute" (see InsertReroute)
  ref_menu.AddAction("Insert Portal...", [this, pConnection, vPos]()
    { plVisualScriptGraphEditing::InsertPortalOnConnection(GetScriptManager(), pConnection, vPos); });
}

void plVisualScriptGraphController::InsertReroute(const plDocumentObject* pConnection, const plVec2& vPosition)
{
  plVisualScriptGraphEditing::InsertRerouteOnConnection(GetScriptManager(), pConnection, vPosition);
}

void plVisualScriptGraphController::GetNodeTemplates(plDynamicArray<plVisualGraphNodeDesc>& out_templates, plDeque<plVisualGraphNodeProperty>& ref_storage, const plVisualGraphPin* pFromPin) const
{
  auto pScriptManager = const_cast<plVisualScriptNodeManager*>(GetScriptManager());

  if (const plVisualScriptPin* pScriptPin = plDynamicCast<const plVisualScriptPin*>(pFromPin))
  {
    pScriptManager->m_CreationHint.m_bValid = true;
    pScriptManager->m_CreationHint.m_bExec = pScriptPin->IsExecutionPin();
  }

  plVisualGraphController::GetNodeTemplates(out_templates, ref_storage, pFromPin);
  pScriptManager->m_CreationHint = plVisualScriptNodeManager::CreationHint();
}

bool plVisualScriptGraphController::OnKey(const plEditorUiKeyEvent& e)
{
  plUuid parent;
  if (e.m_Key != plEditorUiKeyEvent::Key::Backspace || e.m_uiModifiers != 0 || GetScriptManager()->GetParentScope(m_CurrentScope, parent) == false)
    return false;

  NavigateToScope(parent);
  return true;
}

bool plVisualScriptGraphController::OnNodeDoubleClicked(const plDocumentObject* pNode)
{
  if (GetScriptManager()->IsSubGraph(pNode))
  {
    NavigateToScope(pNode->GetGuid());
    return true;
  }

  if (GetScriptManager()->IsSubGraphBoundary(pNode))
  {
    NavigateToParentScope();
    return true;
  }

  return false;
}

void plVisualScriptGraphController::GetBreadcrumbs(plDynamicArray<plString>& out_crumbs) const
{
  plDynamicArray<plVisualScriptBreadcrumbEntry> path;
  GetScriptManager()->GetBreadcrumbPath(path);

  for (const plVisualScriptBreadcrumbEntry& entry : path)
    out_crumbs.PushBack(entry.m_sDisplayName);
}

void plVisualScriptGraphController::OnBreadcrumbClicked(plUInt32 uiIndex)
{
  plDynamicArray<plVisualScriptBreadcrumbEntry> path;
  GetScriptManager()->GetBreadcrumbPath(path);

  if (uiIndex < path.GetCount())
    NavigateToScope(path[uiIndex].m_Guid);
}

// plVisualScriptGraphHost
//////////////////////////////////////////////////////////////////////////

void plVisualScriptGraphController::NavigateToScope(const plUuid& scope)
{
  const plUuid newScope = plVisualScriptGraphEditing::ValidateScope(GetScriptManager(), scope);

  GetDocument()->GetSelectionManager()->Clear();

  // the scope is view state that the manager keeps, so new nodes land in it
  m_CurrentScope = newScope;
  const_cast<plVisualScriptNodeManager*>(GetScriptManager())->SetCurrentViewScope(m_CurrentScope);

  if (m_CurrentScope.IsValid())
    plVisualScriptGraphEditing::EnsureSubGraphBoundaries(GetScriptManager(), m_CurrentScope).IgnoreResult();

  InvalidateLook();
  m_ScopeChanged.Broadcast(m_CurrentScope);
}

void plVisualScriptGraphController::NavigateToParentScope()
{
  plUuid parent;
  if (GetScriptManager()->GetParentScope(m_CurrentScope, parent))
    NavigateToScope(parent);
}

plUuid plVisualScriptGraphController::AddFunction(plStringView sName)
{
  return plVisualScriptGraphEditing::AddFunction(GetScriptManager(), sName);
}

void plVisualScriptGraphController::RemoveFunction(const plUuid& functionGuid)
{
  const plVisualScriptNodeManager* pManager = GetScriptManager();
  const plDocumentObject* pEntry = pManager->GetObject(functionGuid);
  if (pEntry == nullptr || pManager->IsNode(pEntry) == false || pManager->IsFunctionEntry(pEntry) == false)
    return;

  // leave the graph that is about to disappear first
  if (pManager->GetScopeFunction(m_CurrentScope) == pEntry)
    NavigateToScope(plUuid());

  plVisualScriptGraphEditing::RemoveFunction(pManager, pEntry);
}

void plVisualScriptGraphController::RenameFunction(const plUuid& functionGuid, plStringView sNewName)
{
  plVisualScriptGraphEditing::RenameFunction(GetScriptManager(), functionGuid, sNewName);
}

void plVisualScriptGraphController::GetSelectedNodesInScope(plDynamicArray<const plDocumentObject*>& out_nodes) const
{
  const plDeque<const plDocumentObject*>& selection = GetDocument()->GetSelectionManager()->GetSelection();

  plHybridArray<const plDocumentObject*, 32> selected;
  for (const plDocumentObject* pObject : selection)
    selected.PushBack(pObject);

  plVisualScriptGraphEditing::FilterNodesInScope(GetScriptManager(), m_CurrentScope, selected, out_nodes);
}

plRectFloat plVisualScriptGraphController::GetBounds(plArrayPtr<const plDocumentObject* const> nodes) const
{
  plRectFloat bounds = plRectFloat::MakeInvalid();
  for (const plDocumentObject* pNode : nodes)
  {
    // the drawn node, else its position
    plRectFloat rect;
    if (m_pView == nullptr || m_pView->GetNodeRect(pNode, rect) == false)
    {
      const plVec2 vPos = m_pManager->GetNodePos(pNode);
      rect = plRectFloat(vPos.x, vPos.y, 0.0f, 0.0f);
    }

    if (bounds.IsValid())
      bounds.ExpandToInclude(rect);
    else
      bounds = rect;
  }

  return bounds;
}

void plVisualScriptGraphController::CollapseSelectionToSubGraph()
{
  plHybridArray<const plDocumentObject*, 32> selection;
  GetSelectedNodesInScope(selection);

  const plUuid subGraph = plVisualScriptGraphEditing::CollapseToSubGraph(GetScriptManager(), selection, GetBounds(selection), m_CurrentScope);

  if (const plDocumentObject* pSubGraph = subGraph.IsValid() ? m_pManager->GetObject(subGraph) : nullptr)
    GetDocument()->GetSelectionManager()->SetSelection(pSubGraph);
}

void plVisualScriptGraphController::ExpandSelectedSubGraph()
{
  plHybridArray<const plDocumentObject*, 32> selection;
  GetSelectedNodesInScope(selection);

  plVisualScriptGraphEditing::ExpandSelectedSubGraph(GetScriptManager(), selection);
}
