#include <EditorPluginAssets/EditorPluginAssetsPCH.h>

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptWindow.h>
#include <EditorPluginVisualScript/VisualScriptLibraryAsset/VisualScriptLibraryAsset.h>
#include <EditorPluginVisualScript/VisualScriptLibraryAsset/VisualScriptLibraryAssetManager.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptLibraryAssetManager, 1, plRTTIDefaultAllocator<plVisualScriptLibraryAssetManager>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plVisualScriptLibraryAssetManager::plVisualScriptLibraryAssetManager()
{
  plDocumentManager::s_Events.AddEventHandler(plMakeDelegate(&plVisualScriptLibraryAssetManager::OnDocumentManagerEvent, this));

  m_DocTypeDesc.m_sDocumentTypeName = "VisualScriptLibrary";
  m_DocTypeDesc.m_sFileExtension = "plVisualScriptLibraryAsset";
  m_DocTypeDesc.m_sIcon = ":/AssetIcons/VisualScriptLibrary.svg";
  m_DocTypeDesc.m_sAssetCategory = "Scripting";
  m_DocTypeDesc.m_pDocumentType = plGetStaticRTTI<plVisualScriptLibraryAssetDocument>();
  m_DocTypeDesc.m_pManager = this;
  m_DocTypeDesc.m_CompatibleTypes.PushBack("CompatibleAsset_VisualScriptLibrary");

  m_DocTypeDesc.m_sResourceFileExtension = "plBinVisualScriptLibrary";
  m_DocTypeDesc.m_AssetDocumentFlags = plAssetDocumentFlags::AutoTransformOnSave;
}

plVisualScriptLibraryAssetManager::~plVisualScriptLibraryAssetManager()
{
  plDocumentManager::s_Events.RemoveEventHandler(plMakeDelegate(&plVisualScriptLibraryAssetManager::OnDocumentManagerEvent, this));
}

void plVisualScriptLibraryAssetManager::OnDocumentManagerEvent(const plDocumentManager::Event& e)
{
  switch (e.m_Type)
  {
    case plDocumentManager::Event::Type::DocumentWindowRequested:
    {
      if (e.m_pDocument->GetDynamicRTTI() == plGetStaticRTTI<plVisualScriptLibraryAssetDocument>())
      {
        new plVisualScriptWindow(e.m_pDocument); // NOLINT: Not a memory leak
      }
    }
    break;

    default:
      break;
  }
}

void plVisualScriptLibraryAssetManager::InternalCreateDocument(
  plStringView sDocumentTypeName, plStringView sPath, bool bCreateNewDocument, plDocument*& out_pDocument, const plDocumentObject* pOpenContext)
{
  out_pDocument = new plVisualScriptLibraryAssetDocument(sPath);
}

void plVisualScriptLibraryAssetManager::InternalGetSupportedDocumentTypes(plDynamicArray<const plDocumentTypeDescriptor*>& inout_DocumentTypes) const
{
  inout_DocumentTypes.PushBack(&m_DocTypeDesc);
}
