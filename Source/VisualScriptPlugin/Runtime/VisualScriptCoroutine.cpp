#include <VisualScriptPlugin/VisualScriptPluginPCH.h>

#include <Core/Scripting/ScriptCallScope.h>
#include <Core/Scripting/ScriptComponent.h>
#include <VisualScriptPlugin/Runtime/VisualScriptCoroutine.h>
#include <VisualScriptPlugin/Runtime/VisualScriptInstance.h>

plVisualScriptCoroutine::plVisualScriptCoroutine(const plSharedPtr<const plVisualScriptGraphDescription>& pDesc)
  : m_Context(pDesc, plScriptAllocator::GetAllocator())
{
}

plVisualScriptCoroutine::~plVisualScriptCoroutine() = default;

void plVisualScriptCoroutine::StartWithVarargs(plArrayPtr<plVariant> arguments)
{
  auto pVisualScriptInstance = static_cast<plVisualScriptInstance*>(GetScriptInstance());
  m_Context.Initialize(*pVisualScriptInstance, arguments);
}

void plVisualScriptCoroutine::Stop()
{
  m_Context.Deinitialize();
}

plScriptCoroutine::Result plVisualScriptCoroutine::Update(plTime deltaTimeSinceLastUpdate)
{
  const plScriptInstance* pInstance = GetScriptInstance();
  const plScriptComponent* pComponent = pInstance != nullptr ? plDynamicCast<const plScriptComponent*>(&pInstance->GetOwner()) : nullptr;
  plScriptCallScope scope(pInstance, pComponent != nullptr ? pComponent->GetScriptType() : nullptr, GetName().GetStartPointer());

  auto result = m_Context.Execute(deltaTimeSinceLastUpdate);
  if (result.m_NextExecAndState == plVisualScriptExecutionContext::ExecResult::State::ContinueLater)
  {
    return Result::Running(result.m_MaxDelay);
  }

  return Result::Completed();
}

//////////////////////////////////////////////////////////////////////////

plVisualScriptCoroutineAllocator::plVisualScriptCoroutineAllocator(const plSharedPtr<const plVisualScriptGraphDescription>& pDesc)
  : m_pDesc(pDesc)
{
}

void plVisualScriptCoroutineAllocator::Deallocate(void* pObject, plAllocator* pAllocator /*= nullptr*/)
{
  PL_REPORT_FAILURE("Deallocate is not supported");
}

plInternal::NewInstance<void> plVisualScriptCoroutineAllocator::AllocateInternal(plAllocator* pAllocator)
{
  return PL_SCRIPT_NEW(plVisualScriptCoroutine, m_pDesc);
}
