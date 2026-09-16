#pragma once

#include <Core/ResourceManager/ResourceHandle.h>
#include <Core/Scripting/ScriptCoroutine.h>
#include <VisualScriptPlugin/Runtime/VisualScript.h>

using plVisualScriptLibraryResourceHandle = plTypedResourceHandle<class plVisualScriptLibraryResource>;

/// \brief The deserialized content of a compiled visual script module (class or library), shared by both resource types.
struct PL_VISUALSCRIPTPLUGIN_DLL plVisualScriptModuleData
{
  struct Function
  {
    plString m_sName;
    plEnum<plVisualScriptNodeDescription::Type> m_Type;
    plEnum<plScriptCoroutineCreationMode> m_CoroutineCreationMode;
    plSharedPtr<plVisualScriptGraphDescription> m_pDesc;
  };

  plString m_sBaseClassName;
  plString m_sScriptClassName;

  plSharedPtr<plVisualScriptDataStorage> m_pConstantDataStorage;
  plSharedPtr<const plVisualScriptDataDescription> m_pInstanceDataDesc;
  plSharedPtr<plVisualScriptInstanceDataMapping> m_pInstanceDataMapping;

  plDynamicArray<Function> m_Functions;

  /// \brief Graph-internal functions (FunctionEntry graphs) by name, filled by ResolveCallSites.
  plHashTable<plHashedString, plSharedPtr<const plVisualScriptGraphDescription>> m_FunctionsByName;

  /// \brief Libraries referenced by call nodes, kept alive together with the descriptions and constant data taken from them.
  plDynamicArray<plVisualScriptLibraryResourceHandle> m_hLibraries;
  plDynamicArray<plSharedPtr<const plVisualScriptGraphDescription>> m_LibraryDescs;
  plDynamicArray<plSharedPtr<plVisualScriptDataStorage>> m_LibraryConstants;

  void Clear();

  const plSharedPtr<const plVisualScriptGraphDescription>* GetFunctionDesc(plTempHashedString sName) const;
};

struct PL_VISUALSCRIPTPLUGIN_DLL plVisualScriptModuleReader
{
  /// \brief Reads the chunk stream written by the visual script compiler. The stream must already be positioned behind the asset file header.
  /// \a bRequireBaseClass fails the read when the header names no valid base class (class modules), libraries pass false.
  static plResult Read(plStreamReader& inout_stream, plVisualScriptModuleData& out_module, bool bRequireBaseClass, const plRTTI*& out_pBaseClassType);

  /// \brief Patches every Builtin_Call / Builtin_CallLibrary node of the module. Library resources are loaded blocking; the module keeps them alive.
  static plResult ResolveCallSites(plVisualScriptModuleData& inout_module);
};
