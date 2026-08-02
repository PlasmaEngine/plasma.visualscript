#pragma once

#include <Core/Scripting/ScriptClassResource.h>
#include <Foundation/Containers/Blob.h>
#include <VisualScriptPlugin/Runtime/VisualScript.h>

class PL_VISUALSCRIPTPLUGIN_DLL plVisualScriptInstance : public plScriptInstance
{
public:
  plVisualScriptInstance(plReflectedClass& inout_owner, plWorld* pWorld, const plSharedPtr<plVisualScriptDataStorage>& pConstantDataStorage, const plSharedPtr<const plVisualScriptDataDescription>& pInstanceDataDesc, const plSharedPtr<plVisualScriptInstanceDataMapping>& pInstanceDataMapping);

  virtual void SetInstanceVariable(const plHashedString& sName, const plVariant& value) override;
  virtual plVariant GetInstanceVariable(const plHashedString& sName) override;

  plVisualScriptDataStorage* GetConstantDataStorage() { return m_pConstantDataStorage.Borrow(); }
  plVisualScriptDataStorage* GetInstanceDataStorage() { return &m_InstanceDataStorage; }

private:
  plSharedPtr<plVisualScriptDataStorage> m_pConstantDataStorage;
  plSharedPtr<plVisualScriptInstanceDataMapping> m_pInstanceDataMapping;

  plVisualScriptDataStorage m_InstanceDataStorage;
};
