#include <EditorPluginVisualScript/EditorPluginVisualScriptPCH.h>

#include <EditorPluginVisualScript/VisualScriptLibraryAsset/VisualScriptLibraryMetaData.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_TYPE(plVisualScriptLibraryPinInfo, plNoBase, 1, plRTTIDefaultAllocator<plVisualScriptLibraryPinInfo>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_ENUM_MEMBER_PROPERTY("Type", plVisualScriptPinType, m_Type),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;

PL_BEGIN_STATIC_REFLECTED_TYPE(plVisualScriptLibraryFunctionInfo, plNoBase, 1, plRTTIDefaultAllocator<plVisualScriptLibraryFunctionInfo>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_ARRAY_MEMBER_PROPERTY("Inputs", m_Inputs),
    PL_ARRAY_MEMBER_PROPERTY("Outputs", m_Outputs),
    PL_MEMBER_PROPERTY("Coroutine", m_bCoroutine),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptLibraryMetaData, 1, plRTTIDefaultAllocator<plVisualScriptLibraryMetaData>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ARRAY_MEMBER_PROPERTY("Functions", m_Functions),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

const plVisualScriptLibraryFunctionInfo* plVisualScriptLibraryMetaData::FindFunction(plStringView sName) const
{
  for (const auto& function : m_Functions)
  {
    if (function.m_sName == sName)
      return &function;
  }

  return nullptr;
}
