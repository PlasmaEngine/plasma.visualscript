#include <VisualScriptPlugin/VisualScriptPluginPCH.h>

#include <VisualScriptPlugin/Runtime/VisualScriptInstance.h>

plVisualScriptInstance::plVisualScriptInstance(plReflectedClass& inout_owner, plWorld* pWorld, const plSharedPtr<plVisualScriptDataStorage>& pConstantDataStorage, const plSharedPtr<const plVisualScriptDataDescription>& pInstanceDataDesc, const plSharedPtr<plVisualScriptInstanceDataMapping>& pInstanceDataMapping)
  : plScriptInstance(inout_owner, pWorld)
  , m_pConstantDataStorage(pConstantDataStorage)
  , m_pInstanceDataMapping(pInstanceDataMapping)
  , m_InstanceDataStorage(pInstanceDataDesc)
{
  if (pInstanceDataDesc != nullptr)
  {
    m_InstanceDataStorage.AllocateStorage(plScriptAllocator::GetAllocator());

    for (auto& it : m_pInstanceDataMapping->m_Content)
    {
      auto& instanceData = it.Value();
      m_InstanceDataStorage.SetDataFromVariant(instanceData.m_DataOffset, instanceData.m_DefaultValue, 0);
    }
  }
}

void plVisualScriptInstance::SetInstanceVariable(const plHashedString& sName, const plVariant& value)
{
  if (m_pInstanceDataMapping == nullptr)
    return;

  plVisualScriptInstanceData* pInstanceData = nullptr;
  if (m_pInstanceDataMapping->m_Content.TryGetValue(sName, pInstanceData) == false)
    return;

  plResult conversionStatus = PL_FAILURE;
  plVariantType::Enum targetType = plVisualScriptDataType::GetVariantType(pInstanceData->m_DataOffset.GetType());

  plVariant convertedValue = value.ConvertTo(targetType, &conversionStatus);
  if (conversionStatus.Failed())
  {
    plLog::Error("Can't apply instance variable '{}' because the given value of type '{}' can't be converted the expected target type '{}'", sName, value.GetType(), targetType);
    return;
  }

  m_InstanceDataStorage.SetDataFromVariant(pInstanceData->m_DataOffset, convertedValue, 0);
}

plVariant plVisualScriptInstance::GetInstanceVariable(const plHashedString& sName)
{
  if (m_pInstanceDataMapping == nullptr)
    return plVariant();

  plVisualScriptInstanceData* pInstanceData = nullptr;
  if (m_pInstanceDataMapping->m_Content.TryGetValue(sName, pInstanceData) == false)
    return plVariant();

  return m_InstanceDataStorage.GetDataAsVariant(pInstanceData->m_DataOffset, nullptr, 0);
}
