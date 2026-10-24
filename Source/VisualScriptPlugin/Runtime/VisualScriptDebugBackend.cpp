#include <VisualScriptPlugin/VisualScriptPluginPCH.h>

#include <Core/Scripting/ScriptDiagnostic.h>
#include <Core/World/Component.h>
#include <Core/World/GameObject.h>
#include <VisualScriptPlugin/Runtime/VisualScript.h>
#include <VisualScriptPlugin/Runtime/VisualScriptDebugBackend.h>
#include <VisualScriptPlugin/Runtime/VisualScriptInstance.h>

namespace
{
  constexpr plUInt32 s_uiMaxContexts = 32;

  /// The execution contexts running on this thread, innermost last, with the call scope depth each started at.
  thread_local const plVisualScriptExecutionContext* s_Contexts[s_uiMaxContexts] = {};
  thread_local plUInt32 s_ContextDepths[s_uiMaxContexts] = {};
  thread_local plUInt32 s_uiNumContexts = 0;

  plVisualScriptDebugBackend s_Backend;
  bool s_bRegistered = false;

  plScriptDebugLocation MakeLocation(const plVisualScriptGraphDescription* pDesc, plUInt32 uiNodeIndex)
  {
    plScriptDebugLocation location;
    if (pDesc != nullptr)
    {
      location.m_sFile = pDesc->GetDebugInfo().m_sAsset;
      location.m_NodeGuid = pDesc->GetNodeGuid(uiNodeIndex);
    }
    return location;
  }

  plString MakeFunctionName(const plVisualScriptGraphDescription* pDesc, plUInt32 uiNodeIndex)
  {
    plStringBuilder sName = pDesc->GetDebugInfo().m_sFunction;
    if (const auto* pNode = pDesc->GetNode(uiNodeIndex))
    {
      sName.Append(sName.IsEmpty() ? "" : " - ", plVisualScriptNodeDescription::Type::GetName(pNode->m_Type));
    }
    return sName;
  }

  plStringView GetInstanceName(const plScriptInstance& instance)
  {
    const plReflectedClass& owner = instance.GetOwner();
    if (owner.IsInstanceOf<plComponent>())
    {
      const plGameObject* pObject = static_cast<const plComponent&>(owner).GetOwner();
      if (pObject != nullptr)
        return pObject->GetName();
    }
    return {};
  }

  const plVisualScriptExecutionContext* FindContext(const plScriptCallFrame& entry)
  {
    for (plUInt32 i = s_uiNumContexts; i-- > 0;)
    {
      if (s_ContextDepths[i] == entry.m_uiDepth)
        return s_Contexts[i];
    }
    return nullptr;
  }

  plVisualScriptInstance* GetInstance(const plScriptCallFrame& entry)
  {
    if (const plVisualScriptExecutionContext* pContext = FindContext(entry))
      return &const_cast<plVisualScriptExecutionContext*>(pContext)->GetInstance();
    return nullptr;
  }
} // namespace

plVisualScriptDebugBackend* plVisualScriptDebugBackend::GetSingleton()
{
  return &s_Backend;
}

void plVisualScriptDebugBackend::Startup()
{
  if (!s_bRegistered)
  {
    plScriptDebugger::RegisterBackend(&s_Backend);
    s_bRegistered = true;
  }
}

void plVisualScriptDebugBackend::Shutdown()
{
  if (s_bRegistered)
  {
    plScriptDebugger::UnregisterBackend(&s_Backend);
    s_bRegistered = false;
  }
}

void plVisualScriptDebugBackend::PushContext(const plVisualScriptExecutionContext* pContext)
{
  if (s_uiNumContexts < s_uiMaxContexts)
  {
    s_Contexts[s_uiNumContexts] = pContext;
    s_ContextDepths[s_uiNumContexts] = plScriptCallScope::GetDepth();
  }
  ++s_uiNumContexts;
}

void plVisualScriptDebugBackend::PopContext()
{
  PL_ASSERT_DEBUG(s_uiNumContexts > 0, "Unbalanced visual script context stack");
  --s_uiNumContexts;
}

void plVisualScriptDebugBackend::OnNode(const plVisualScriptExecutionContext& context)
{
  const plVisualScriptGraphDescription* pDesc = context.GetCurrentDesc();
  if (pDesc == nullptr || pDesc->GetDebugInfo().m_NodeGuids.IsEmpty())
    return;

  if (!plScriptDebugger::IsStepping() && !plScriptDebugger::HasBreakpointInFile(pDesc->GetDebugInfo().m_sAsset))
    return;

  const plScriptDebugLocation location = MakeLocation(pDesc, context.GetCurrentNodeIndex());
  if (!location.m_NodeGuid.IsValid())
    return;

  plScriptDebugger::OnLine(this, location, context.GetCallDepth() + 1);
}

void plVisualScriptDebugBackend::OnError(const plVisualScriptExecutionContext& context)
{
  const plVisualScriptGraphDescription* pDesc = context.GetCurrentDesc();
  if (pDesc == nullptr)
    return;

  auto& instance = const_cast<plVisualScriptExecutionContext&>(context).GetInstance();

  plScriptDiagnostic diag;
  diag.m_sLanguage = "Visual Script";
  diag.m_Origin = plScriptDiagnosticOrigin::Runtime;
  diag.m_sAsset = pDesc->GetDebugInfo().m_sAsset;
  diag.m_NodeGuid = pDesc->GetNodeGuid(context.GetCurrentNodeIndex());
  diag.m_sInstance = GetInstanceName(instance);

  plStringBuilder sMessage;
  sMessage.SetFormat("Execution stopped at node '{}' in '{}'. See the log for the cause.", MakeFunctionName(pDesc, context.GetCurrentNodeIndex()), pDesc->GetDebugInfo().m_sFunction);
  diag.m_sMessage = sMessage;

  plStringBuilder sStack;
  for (plUInt32 i = context.GetCallDepth(); i-- > 0;)
  {
    const plVisualScriptGraphDescription* pCaller = nullptr;
    plUInt32 uiNode = 0;
    context.GetCallFrame(i, pCaller, uiNode);
    if (pCaller != nullptr)
      sStack.Append(MakeFunctionName(pCaller, uiNode), "\n");
  }
  diag.m_sStack = sStack;

  plScriptDiagnostics::Report(diag);

  if (plScriptDebugger::IsAttached())
    plScriptDebugger::OnException(&s_Backend, MakeLocation(pDesc, context.GetCurrentNodeIndex()), diag.m_sMessage);
}

void plVisualScriptDebugBackend::CollectFrames(const plScriptCallFrame& entry, plDynamicArray<plScriptDebugFrame>& out_frames)
{
  const plVisualScriptExecutionContext* pContext = FindContext(entry);
  if (pContext == nullptr || pContext->GetCurrentDesc() == nullptr)
    return;

  const plStringView sInstance = GetInstanceName(const_cast<plVisualScriptExecutionContext*>(pContext)->GetInstance());

  auto addFrame = [&](const plVisualScriptGraphDescription* pDesc, plUInt32 uiNode)
  {
    plScriptDebugFrame& frame = out_frames.ExpandAndGetRef();
    frame.m_sLanguage = "Visual Script";
    frame.m_sFunction = MakeFunctionName(pDesc, uiNode);
    frame.m_sInstance = sInstance;
    frame.m_Location = MakeLocation(pDesc, uiNode);
    frame.m_bInspectable = true;
  };

  addFrame(pContext->GetCurrentDesc(), pContext->GetCurrentNodeIndex());

  for (plUInt32 i = pContext->GetCallDepth(); i-- > 0;)
  {
    const plVisualScriptGraphDescription* pCaller = nullptr;
    plUInt32 uiNode = 0;
    pContext->GetCallFrame(i, pCaller, uiNode);
    if (pCaller != nullptr)
      addFrame(pCaller, uiNode);
  }
}

void plVisualScriptDebugBackend::GetVariables(const plScriptCallFrame& entry, plUInt32 uiLocalFrame, plDynamicArray<plScriptDebugVariable>& out_variables)
{
  PL_IGNORE_UNUSED(uiLocalFrame);

  // Graph locals have no names at runtime; the instance variables are what a graph author knows.
  plVisualScriptInstance* pInstance = GetInstance(entry);
  if (pInstance == nullptr)
    return;

  plArrayMap<plHashedString, plVariant> variables;
  pInstance->GetInstanceVariables(variables);

  for (plUInt32 i = 0; i < variables.GetCount(); ++i)
  {
    const plVariant& value = variables.GetValue(i);

    plScriptDebugVariable& var = out_variables.ExpandAndGetRef();
    var.m_sName = variables.GetKey(i).GetView();
    if (const plRTTI* pType = value.GetReflectedType())
      var.m_sType = pType->GetTypeName();
    var.m_sValue = value.ConvertTo<plString>();
    var.m_bEditable = value.IsNumber() || value.IsString() || value.IsA<bool>();
  }
}

plResult plVisualScriptDebugBackend::SetVariable(const plScriptCallFrame& entry, plUInt32 uiLocalFrame, plStringView sName, plStringView sValue)
{
  PL_IGNORE_UNUSED(uiLocalFrame);

  plVisualScriptInstance* pInstance = GetInstance(entry);
  if (pInstance == nullptr)
    return PL_FAILURE;

  plHashedString sKey;
  sKey.Assign(sName);

  const plVariant current = pInstance->GetInstanceVariable(sKey);
  if (!current.IsValid())
    return PL_FAILURE;

  plResult result = PL_FAILURE;
  const plVariant value = plVariant(plString(sValue)).ConvertTo(current.GetType(), &result);
  if (result.Failed())
    return PL_FAILURE;

  pInstance->SetInstanceVariable(sKey, value);
  return PL_SUCCESS;
}
