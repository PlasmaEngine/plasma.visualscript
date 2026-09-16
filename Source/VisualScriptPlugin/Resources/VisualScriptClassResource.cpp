#include <VisualScriptPlugin/VisualScriptPluginPCH.h>

#include <Foundation/Configuration/Startup.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <VisualScriptPlugin/Resources/VisualScriptClassResource.h>
#include <VisualScriptPlugin/Runtime/VisualScriptCoroutine.h>
#include <VisualScriptPlugin/Runtime/VisualScriptFunctionProperty.h>
#include <VisualScriptPlugin/Runtime/VisualScriptInstance.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptClassResource, 1, plRTTIDefaultAllocator<plVisualScriptClassResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;
PL_RESOURCE_IMPLEMENT_COMMON_CODE(plVisualScriptClassResource);

PL_BEGIN_SUBSYSTEM_DECLARATION(VisualScript, VisualScriptResource)

  BEGIN_SUBSYSTEM_DEPENDENCIES
    "ResourceManager" 
  END_SUBSYSTEM_DEPENDENCIES

  ON_CORESYSTEMS_STARTUP 
  {
    plResourceManager::RegisterResourceForAssetType("VisualScriptClass", plGetStaticRTTI<plVisualScriptClassResource>());
    plResourceManager::RegisterResourceOverrideType(plGetStaticRTTI<plVisualScriptClassResource>(), [](const plStringBuilder& sResourceID) -> bool  {
        return sResourceID.HasExtension(".plBinVisualScriptClass");
      });
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
    plResourceManager::UnregisterResourceOverrideType(plGetStaticRTTI<plVisualScriptClassResource>());
  }

PL_END_SUBSYSTEM_DECLARATION;
// clang-format on

plVisualScriptClassResource::plVisualScriptClassResource() = default;
plVisualScriptClassResource::~plVisualScriptClassResource() = default;

plResourceLoadDesc plVisualScriptClassResource::UnloadData(Unload WhatToUnload)
{
  DeleteScriptType();
  DeleteAllScriptCoroutineTypes();
  m_Module.Clear();

  plResourceLoadDesc ld;
  ld.m_State = plResourceState::Unloaded;
  ld.m_uiQualityLevelsDiscardable = 0;
  ld.m_uiQualityLevelsLoadable = 0;

  return ld;
}

plResourceLoadDesc plVisualScriptClassResource::UpdateContent(plStreamReader* pStream)
{
  plResourceLoadDesc ld;
  ld.m_uiQualityLevelsDiscardable = 0;
  ld.m_uiQualityLevelsLoadable = 0;
  ld.m_State = plResourceState::LoadedResourceMissing;

  if (pStream == nullptr)
  {
    return ld;
  }

  // the standard file reader writes the absolute file path into the stream
  plString sAbsFilePath;
  (*pStream) >> sAbsFilePath;

  // skip the asset file header at the start of the file
  plAssetFileHeader AssetHash;
  AssetHash.Read(*pStream).IgnoreResult();

  const plRTTI* pBaseClassType = nullptr;
  if (plVisualScriptModuleReader::Read(*pStream, m_Module, true, pBaseClassType).Failed())
  {
    m_Module.Clear();
    return ld;
  }

  // Patch the call nodes before any function can run. Graph-internal functions stay in m_Module and are never exposed on the script type.
  if (plVisualScriptModuleReader::ResolveCallSites(m_Module).Failed())
  {
    m_Module.Clear();
    return ld;
  }

  const plString& sScriptClassName = m_Module.m_sScriptClassName;
  plScriptRTTI::FunctionList functions;
  plScriptRTTI::MessageHandlerList messageHandlers;

  for (auto& function : m_Module.m_Functions)
  {
    const plString& sFunctionName = function.m_sName;
    plSharedPtr<const plVisualScriptGraphDescription> pDesc = function.m_pDesc;
    const auto functionType = function.m_Type;
    const auto coroutineCreationMode = function.m_CoroutineCreationMode;

    if (functionType == plVisualScriptNodeDescription::Type::EntryCall)
    {
      plUniquePtr<plVisualScriptFunctionProperty> pFunctionProperty = PL_SCRIPT_NEW(plVisualScriptFunctionProperty, sFunctionName, pDesc);
      functions.PushBack(std::move(pFunctionProperty));
    }
    else if (functionType == plVisualScriptNodeDescription::Type::EntryCall_Coroutine)
    {
      plUniquePtr<plVisualScriptCoroutineAllocator> pCoroutineAllocator = PL_SCRIPT_NEW(plVisualScriptCoroutineAllocator, pDesc);
      auto pCoroutineType = CreateScriptCoroutineType(sScriptClassName, sFunctionName, std::move(pCoroutineAllocator));
      plUniquePtr<plScriptCoroutineFunctionProperty> pFunctionProperty = PL_SCRIPT_NEW(plScriptCoroutineFunctionProperty, sFunctionName, pCoroutineType, coroutineCreationMode);
      functions.PushBack(std::move(pFunctionProperty));
    }
    else if (functionType == plVisualScriptNodeDescription::Type::MessageHandler)
    {
      auto desc = pDesc->GetMessageDesc();
      plUniquePtr<plVisualScriptMessageHandler> pMessageHandler = PL_SCRIPT_NEW(plVisualScriptMessageHandler, desc, pDesc);
      messageHandlers.PushBack(std::move(pMessageHandler));
    }
    else if (functionType == plVisualScriptNodeDescription::Type::MessageHandler_Coroutine)
    {
      auto desc = pDesc->GetMessageDesc();
      plUniquePtr<plVisualScriptCoroutineAllocator> pCoroutineAllocator = PL_SCRIPT_NEW(plVisualScriptCoroutineAllocator, pDesc);
      auto pCoroutineType = CreateScriptCoroutineType(sScriptClassName, sFunctionName, std::move(pCoroutineAllocator));
      plUniquePtr<plScriptCoroutineMessageHandler> pMessageHandler = PL_SCRIPT_NEW(plScriptCoroutineMessageHandler, sFunctionName, desc, pCoroutineType, coroutineCreationMode);
      messageHandlers.PushBack(std::move(pMessageHandler));
    }
    else if (plVisualScriptNodeDescription::Type::IsFunctionEntry(functionType))
    {
      // Graph-internal function, only reachable through call nodes (see m_Module.m_FunctionsByName)
    }
    else
    {
      plLog::Error("Invalid event handler type '{}' for event handler '{}'", plVisualScriptNodeDescription::Type::GetName(functionType), sFunctionName);
      m_Module.Clear();
      return ld;
    }
  }

  CreateScriptType(sScriptClassName, pBaseClassType, std::move(functions), std::move(messageHandlers));

  ld.m_State = plResourceState::Loaded;
  return ld;
}

void plVisualScriptClassResource::UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage)
{
  out_NewMemoryUsage.m_uiMemoryCPU = (plUInt32)sizeof(plVisualScriptClassResource);
  out_NewMemoryUsage.m_uiMemoryGPU = 0;
}

plUniquePtr<plScriptInstance> plVisualScriptClassResource::Instantiate(plReflectedClass& inout_owner, plWorld* pWorld) const
{
  return PL_SCRIPT_NEW(plVisualScriptInstance, inout_owner, pWorld, m_Module.m_pConstantDataStorage, m_Module.m_pInstanceDataDesc, m_Module.m_pInstanceDataMapping);
}


PL_STATICLINK_FILE(VisualScriptPlugin, VisualScriptPlugin_Resources_VisualScriptClassResource);
