#pragma once

#include <Core/Scripting/ScriptClassResource.h>
#include <VisualScriptPlugin/Resources/VisualScriptModuleReader.h>
#include <VisualScriptPlugin/Runtime/VisualScriptData.h>

class PL_VISUALSCRIPTPLUGIN_DLL plVisualScriptClassResource : public plScriptClassResource
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptClassResource, plScriptClassResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plVisualScriptClassResource);

public:
  plVisualScriptClassResource();
  ~plVisualScriptClassResource();

  /// \brief Graph-internal functions of this class by name.
  const plSharedPtr<const plVisualScriptGraphDescription>* GetFunctionDesc(plTempHashedString sName) const { return m_Module.GetFunctionDesc(sName); }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* pStream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_NewMemoryUsage) override;

  virtual plUniquePtr<plScriptInstance> Instantiate(plReflectedClass& inout_owner, plWorld* pWorld) const override;

  /// Owns every graph description, the constant/instance data and the referenced libraries. Function properties and message handlers share the descriptions.
  plVisualScriptModuleData m_Module;
};
