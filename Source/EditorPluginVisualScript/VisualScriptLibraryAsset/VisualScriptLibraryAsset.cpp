#include <EditorPluginAssets/EditorPluginAssetsPCH.h>

#include <EditorPluginVisualScript/VisualScriptClassAsset/VisualScriptClassAsset.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptCommands.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptCompiler.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraphQt.moc.h>
#include <EditorPluginVisualScript/VisualScriptLibraryAsset/VisualScriptLibraryAsset.h>
#include <EditorPluginVisualScript/VisualScriptLibraryAsset/VisualScriptLibraryMetaData.h>
#include <GuiFoundation/VisualGraph/Scene.moc.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptLibraryAssetProperties, 1, plRTTIDefaultAllocator<plVisualScriptLibraryAssetProperties>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("DumpAST", m_bDumpAST),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptLibraryAssetDocument, 2, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plVisualScriptLibraryAssetDocument::plVisualScriptLibraryAssetDocument(plStringView sDocumentPath)
  : plSimpleAssetDocument<plVisualScriptLibraryAssetProperties>(PL_DEFAULT_NEW(plVisualScriptNodeManager), sDocumentPath, plAssetDocEngineConnection::None)
{
  static_cast<plVisualScriptNodeManager*>(GetObjectManager())->m_bIsLibrary = true;
  m_pObjectAccessor = PL_DEFAULT_NEW(plVisualScriptCommandAccessor, GetCommandHistory());
}

plTransformStatus plVisualScriptLibraryAssetDocument::InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  auto pManager = static_cast<plVisualScriptNodeManager*>(GetObjectManager());

  PL_SUCCEED_OR_RETURN(pManager->ValidateGraph());

  for (const plDocumentObject* pObject : pManager->GetRootObject()->GetChildren())
  {
    if (pManager->IsNode(pObject) && pManager->IsComment(pObject) == false && pManager->IsEntryNode(pObject) && pManager->IsFunctionEntry(pObject) == false)
    {
      return plStatus(plFmt("A library can only contain functions, '{}' is not allowed", plVisualScriptNodeManager::GetNiceTypeName(pObject)));
    }
  }

  plStringView sScriptClassName = plPathUtils::GetFileName(GetDocumentPath());

  // no base class: a library has no owner and no instance
  plVisualScriptCompiler compiler(*pManager);
  compiler.InitModule(plStringView(), sScriptClassName);

  PL_SUCCEED_OR_RETURN(plVisualScriptClassAssetDocument::AddEntryFunctions(*pManager, compiler, plHashedString()));

  plStringBuilder sDumpPath;
  if (GetProperties()->m_bDumpAST)
  {
    sDumpPath.SetFormat(":appdata/{}_AST.dgml", sScriptClassName);
  }
  PL_SUCCEED_OR_RETURN(compiler.Compile(sDumpPath));

  auto& compiledModule = compiler.GetCompiledModule();
  PL_SUCCEED_OR_RETURN(compiledModule.Serialize(stream));

  return plStatus(PL_SUCCESS);
}

void plVisualScriptLibraryAssetDocument::UpdateAssetDocumentInfo(plAssetDocumentInfo* pInfo) const
{
  SUPER::UpdateAssetDocumentInfo(pInfo);

  auto pManager = static_cast<const plVisualScriptNodeManager*>(GetObjectManager());

  plVisualScriptLibraryMetaData* pMeta = PL_DEFAULT_NEW(plVisualScriptLibraryMetaData);

  auto ReadDecls = [&](const plDocumentObject* pEntry, plStringView sArrayProperty, plDynamicArray<plVisualScriptLibraryPinInfo>& out_pins)
  {
    auto& typeAccessor = pEntry->GetTypeAccessor();
    const plInt32 iCount = typeAccessor.GetCount(sArrayProperty);
    for (plInt32 i = 0; i < iCount; ++i)
    {
      plVariant declUuid = typeAccessor.GetValue(sArrayProperty, i);
      const plDocumentObject* pDecl = declUuid.IsA<plUuid>() ? pManager->GetObject(declUuid.Get<plUuid>()) : nullptr;
      if (pDecl == nullptr)
        continue;

      auto& pin = out_pins.ExpandAndGetRef();
      pin.m_sName = pDecl->GetTypeAccessor().GetValue("Name").ConvertTo<plString>();
      plVariant typeVar = pDecl->GetTypeAccessor().GetValue("Type");
      pin.m_Type = typeVar.IsA<plInt64>() ? static_cast<plVisualScriptPinType::Enum>(typeVar.Get<plInt64>()) : plVisualScriptPinType::Default;
    }
  };

  for (const plDocumentObject* pObject : pManager->GetRootObject()->GetChildren())
  {
    if (pManager->IsNode(pObject) == false || pManager->IsFunctionEntry(pObject) == false)
      continue;

    auto& function = pMeta->m_Functions.ExpandAndGetRef();
    function.m_sName = pObject->GetTypeAccessor().GetValue("Name").ConvertTo<plString>();
    function.m_bCoroutine = pManager->IsCoroutine(pObject);
    ReadDecls(pObject, "Inputs", function.m_Inputs);
    ReadDecls(pObject, "Outputs", function.m_Outputs);
  }

  // pInfo takes ownership
  pInfo->m_MetaInfo.PushBack(pMeta);
}

void plVisualScriptLibraryAssetDocument::InternalGetMetaDataHash(const plDocumentObject* pObject, plUInt64& inout_uiHash) const
{
  auto pManager = static_cast<const plVisualGraphObjectManager*>(GetObjectManager());
  pManager->GetMetaDataHash(pObject, inout_uiHash);
}

void plVisualScriptLibraryAssetDocument::AttachMetaDataBeforeSaving(plAbstractObjectGraph& graph) const
{
  SUPER::AttachMetaDataBeforeSaving(graph);
  const auto pManager = static_cast<const plVisualGraphObjectManager*>(GetObjectManager());
  pManager->AttachMetaDataBeforeSaving(graph);
}

void plVisualScriptLibraryAssetDocument::RestoreMetaDataAfterLoading(const plAbstractObjectGraph& graph, bool bUndoable)
{
  SUPER::RestoreMetaDataAfterLoading(graph, bUndoable);
  auto pManager = static_cast<plVisualScriptNodeManager*>(GetObjectManager());
  pManager->RestoreMetaDataAfterLoading(graph, bUndoable);

  // document load (not paste): function bodies saved in the root graph move into their function's graph
  if (bUndoable == false)
  {
    pManager->MigrateFunctionScopes();
  }
}

void plVisualScriptLibraryAssetDocument::GetSupportedMimeTypesForPasting(plHybridArray<plString, 4>& out_MimeTypes) const
{
  out_MimeTypes.PushBack("application/plEditor.VisualScriptClassGraph");
}

bool plVisualScriptLibraryAssetDocument::CopySelectedObjects(plAbstractObjectGraph& out_objectGraph, plStringBuilder& out_MimeType) const
{
  out_MimeType = "application/plEditor.VisualScriptClassGraph";

  const plVisualScriptNodeManager* pManager = static_cast<const plVisualScriptNodeManager*>(GetObjectManager());
  return plVisualScriptGraphClipboard::CopySelectedObjects(*pManager, out_objectGraph);
}

bool plVisualScriptLibraryAssetDocument::Paste(const plArrayPtr<PasteInfo>& info, const plAbstractObjectGraph& objectGraph, bool bAllowPickedPosition, plStringView sMimeType)
{
  plVisualScriptNodeManager* pManager = static_cast<plVisualScriptNodeManager*>(GetObjectManager());
  return plVisualScriptGraphClipboard::PasteObjects(*pManager, info, objectGraph, plQtVisualGraphScene::GetLastMouseInteractionPos(), bAllowPickedPosition);
}
