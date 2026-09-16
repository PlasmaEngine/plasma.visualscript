#pragma once

#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptVariable.moc.h>
#include <ToolsFoundation/VisualGraph/VisualGraphObjectManager.h>

class plVisualScriptClassAssetProperties : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptClassAssetProperties, plReflectedClass);

public:
  plString m_sBaseClass;
  plString m_sGraphName; ///< Display name of the event graph in the Functions panel
  plDynamicArray<plVisualScriptVariable> m_Variables;
  bool m_bDumpAST;
};

class plVisualScriptNodeManager;
class plVisualScriptCompiler;

/// Copy/paste of visual script graphs: sub-graph nodes take their contents along, pasted nodes land in the shown scope.
struct plVisualScriptGraphClipboard
{
  static bool CopySelectedObjects(const plVisualScriptNodeManager& manager, plAbstractObjectGraph& out_objectGraph);
  static bool PasteObjects(plVisualScriptNodeManager& ref_manager, const plArrayPtr<plDocument::PasteInfo>& info, const plAbstractObjectGraph& objectGraph, const plVec2& vPickedPosition, bool bAllowPickedPosition);
};

class plVisualScriptClassAssetDocument : public plSimpleAssetDocument<plVisualScriptClassAssetProperties>
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptClassAssetDocument, plSimpleAssetDocument<plVisualScriptClassAssetProperties>);

public:
  plVisualScriptClassAssetDocument(plStringView sDocumentPath);

  /// \brief Registers every entry node (event handlers, message handlers, functions) of the document as a function of the compiler.
  static plStatus AddEntryFunctions(plVisualScriptNodeManager& ref_manager, plVisualScriptCompiler& ref_compiler, const plHashedString& sBaseClass);

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;
  virtual void UpdateAssetDocumentInfo(plAssetDocumentInfo* pInfo) const override;

  virtual void GetSupportedMimeTypesForPasting(plHybridArray<plString, 4>& out_MimeTypes) const override;
  virtual bool CopySelectedObjects(plAbstractObjectGraph& out_objectGraph, plStringBuilder& out_MimeType) const override;
  virtual bool Paste(
    const plArrayPtr<PasteInfo>& info, const plAbstractObjectGraph& objectGraph, bool bAllowPickedPosition, plStringView sMimeType) override;

  virtual void InternalGetMetaDataHash(const plDocumentObject* pObject, plUInt64& inout_uiHash) const override;
  virtual void AttachMetaDataBeforeSaving(plAbstractObjectGraph& graph) const override;
  virtual void RestoreMetaDataAfterLoading(const plAbstractObjectGraph& graph, bool bUndoable) override;
};
