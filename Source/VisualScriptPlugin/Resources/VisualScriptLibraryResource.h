#pragma once

#include <Core/ResourceManager/Resource.h>
#include <VisualScriptPlugin/Resources/VisualScriptModuleReader.h>

/// \brief A compiled visual script library: a set of functions without base class or instance state that other visual scripts call through 'Call Library Function' nodes.
class PL_VISUALSCRIPTPLUGIN_DLL plVisualScriptLibraryResource : public plResource
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptLibraryResource, plResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plVisualScriptLibraryResource);

public:
  plVisualScriptLibraryResource();
  ~plVisualScriptLibraryResource();

  const plSharedPtr<const plVisualScriptGraphDescription>* GetFunctionDesc(plTempHashedString sName) const { return m_Module.GetFunctionDesc(sName); }
  const plSharedPtr<plVisualScriptDataStorage>& GetConstantDataStorage() const { return m_Module.m_pConstantDataStorage; }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* pStream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage) override;

  plVisualScriptModuleData m_Module;
};
