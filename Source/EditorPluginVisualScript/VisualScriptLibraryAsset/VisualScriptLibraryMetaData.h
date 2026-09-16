#pragma once

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptVariable.moc.h>
#include <Foundation/Reflection/Reflection.h>

/// One pin of a library function as published in the library's asset meta data.
struct plVisualScriptLibraryPinInfo
{
  plString m_sName;
  plEnum<plVisualScriptPinType> m_Type;
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptLibraryPinInfo);

/// Signature of one function of a visual script library.
struct plVisualScriptLibraryFunctionInfo
{
  plString m_sName;
  plDynamicArray<plVisualScriptLibraryPinInfo> m_Inputs;
  plDynamicArray<plVisualScriptLibraryPinInfo> m_Outputs;
  bool m_bCoroutine = false;
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptLibraryFunctionInfo);

/// Published by a visual script library document so that callers in other documents can build the pins of
/// 'Call Library Function' nodes without opening the library.
class PL_EDITORPLUGINVISUALSCRIPT_DLL plVisualScriptLibraryMetaData : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptLibraryMetaData, plReflectedClass);

public:
  plDynamicArray<plVisualScriptLibraryFunctionInfo> m_Functions;

  const plVisualScriptLibraryFunctionInfo* FindFunction(plStringView sName) const;
};
