#pragma once

#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <ToolsFoundation/VisualGraph/VisualGraphObjectManager.h>

class plVisualScriptLibraryAssetProperties : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptLibraryAssetProperties, plReflectedClass);

public:
  bool m_bDumpAST = false;
};

/// A visual script library: a graph document that holds only functions, compiled to a .plBinVisualScriptLibrary.
/// Other visual scripts call them through 'Call Library Function' nodes; the signatures are published as asset meta data.
class plVisualScriptLibraryAssetDocument : public plSimpleAssetDocument<plVisualScriptLibraryAssetProperties>
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptLibraryAssetDocument, plSimpleAssetDocument<plVisualScriptLibraryAssetProperties>);

public:
  plVisualScriptLibraryAssetDocument(plStringView sDocumentPath);

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;
  plTransformStatus CompileGraph(plStreamWriter& stream);
  virtual void UpdateAssetDocumentInfo(plAssetDocumentInfo* pInfo) const override;

  virtual void GetSupportedMimeTypesForPasting(plHybridArray<plString, 4>& out_MimeTypes) const override;
  virtual bool CopySelectedObjects(plAbstractObjectGraph& out_objectGraph, plStringBuilder& out_MimeType) const override;
  virtual bool Paste(const plArrayPtr<PasteInfo>& info, const plAbstractObjectGraph& objectGraph, bool bAllowPickedPosition, plStringView sMimeType) override;

  virtual void InternalGetMetaDataHash(const plDocumentObject* pObject, plUInt64& inout_uiHash) const override;
  virtual void AttachMetaDataBeforeSaving(plAbstractObjectGraph& graph) const override;
  virtual void RestoreMetaDataAfterLoading(const plAbstractObjectGraph& graph, bool bUndoable) override;
};
