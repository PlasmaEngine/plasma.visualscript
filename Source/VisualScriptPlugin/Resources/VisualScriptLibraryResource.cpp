#include <VisualScriptPlugin/VisualScriptPluginPCH.h>

#include <Foundation/Configuration/Startup.h>
#include <Foundation/Utilities/AssetFileHeader.h>
#include <VisualScriptPlugin/Resources/VisualScriptLibraryResource.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptLibraryResource, 1, plRTTIDefaultAllocator<plVisualScriptLibraryResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;
PL_RESOURCE_IMPLEMENT_COMMON_CODE(plVisualScriptLibraryResource);

PL_BEGIN_SUBSYSTEM_DECLARATION(VisualScript, VisualScriptLibraryResource)

  BEGIN_SUBSYSTEM_DEPENDENCIES
    "ResourceManager"
  END_SUBSYSTEM_DEPENDENCIES

  ON_CORESYSTEMS_STARTUP
  {
    plResourceManager::RegisterResourceForAssetType("VisualScriptLibrary", plGetStaticRTTI<plVisualScriptLibraryResource>());
    plResourceManager::RegisterResourceOverrideType(plGetStaticRTTI<plVisualScriptLibraryResource>(), [](const plStringBuilder& sResourceID) -> bool  {
        return sResourceID.HasExtension(".plBinVisualScriptLibrary");
      });
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
    plResourceManager::UnregisterResourceOverrideType(plGetStaticRTTI<plVisualScriptLibraryResource>());
  }

PL_END_SUBSYSTEM_DECLARATION;
// clang-format on

plVisualScriptLibraryResource::plVisualScriptLibraryResource()
  : plResource(DoUpdate::OnAnyThread, 1)
{
}

plVisualScriptLibraryResource::~plVisualScriptLibraryResource() = default;

plResourceLoadDesc plVisualScriptLibraryResource::UnloadData(Unload WhatToUnload)
{
  m_Module.Clear();

  plResourceLoadDesc ld;
  ld.m_State = plResourceState::Unloaded;
  ld.m_uiQualityLevelsDiscardable = 0;
  ld.m_uiQualityLevelsLoadable = 0;

  return ld;
}

plResourceLoadDesc plVisualScriptLibraryResource::UpdateContent(plStreamReader* pStream)
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
  if (plVisualScriptModuleReader::Read(*pStream, m_Module, false, pBaseClassType).Failed())
  {
    m_Module.Clear();
    return ld;
  }

  for (auto& function : m_Module.m_Functions)
  {
    if (plVisualScriptNodeDescription::Type::IsFunctionEntry(function.m_Type) == false)
    {
      plLog::Error("Visual script library '{}' contains '{}' which is not a function", GetResourceIdOrDescription(), function.m_sName);
      m_Module.Clear();
      return ld;
    }
  }

  if (plVisualScriptModuleReader::ResolveCallSites(m_Module).Failed())
  {
    m_Module.Clear();
    return ld;
  }

  ld.m_State = plResourceState::Loaded;
  return ld;
}

void plVisualScriptLibraryResource::UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage)
{
  out_NewMemoryUsage.m_uiMemoryCPU = (plUInt32)sizeof(plVisualScriptLibraryResource);
  out_NewMemoryUsage.m_uiMemoryGPU = 0;
}

PL_STATICLINK_FILE(VisualScriptPlugin, VisualScriptPlugin_Resources_VisualScriptLibraryResource);
