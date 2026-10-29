#include <EditorPluginVisualScript/EditorPluginVisualScriptPCH.h>

#include <EditorFramework/Actions/AssetActions.h>
#include <EditorFramework/Actions/ProjectActions.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <GuiFoundation/Action/CommandHistoryActions.h>
#include <GuiFoundation/Action/DocumentActions.h>
#include <GuiFoundation/Action/EditActions.h>
#include <GuiFoundation/Action/StandardMenus.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptActions.h>
#include <GuiFoundation/Action/VisualGraphActions.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorFramework/EditorApp/EditorApp.h>
#include <EditorFramework/EngineShell/WindowContent.h>
#include <EditorFramework/Scripting/ScriptTools.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Configuration/Plugin.h>

/// The package's icon folder: the icons under their ids (":/AssetIcons/VisualScript.svg" is
/// "EditorIcons/AssetIcons/VisualScript.svg"). The plugin binary lives in <package>/Bin/<configuration>.
static plString s_sIconFolder;

static void RegisterIconFolder()
{
  plStringBuilder sPlugin, sCopy;
  plPlugin::GetPluginPaths("plEditorPluginVisualScript", sPlugin, sCopy, 0);

  plStringBuilder sFolder = sPlugin.GetFileDirectory();
  sFolder.AppendPath("..", "..", "EditorIcons");
  sFolder.MakeCleanPath();

  s_sIconFolder = sFolder;
  plEditorUi::AddIconFolder(s_sIconFolder);
}

static void UnregisterIconFolder()
{
  plEditorUi::RemoveIconFolder(s_sIconFolder);
  s_sIconFolder.Clear();
}

/// The visual script panels in the engine shell: the functions left of the graph, the properties right of it.
static void ArrangeVisualScript(plEditorUiDockManager& ref_panels)
{
  ref_panels.DockPanel("VisualScriptAssetDockWidget", "VisualScriptView", plEditorUiDockSide::Right, 0.25f);
  ref_panels.DockPanel("VisualScriptFunctionsDockWidget", "VisualScriptView", plEditorUiDockSide::Left, 0.25f);
  ref_panels.SetPanelIcon("VisualScriptFunctionsDockWidget", ":/EditorPluginVisualScript/Icons/Function.svg");
}

// A graph diagnostic opens its asset and selects the node it belongs to.
static bool OpenVisualScriptLocation(const plScriptDiagnostic& location)
{
  if (location.m_sAsset.IsEmpty())
    return false;

  plStringBuilder sDocument;
  {
    auto pAsset = plAssetCurator::GetSingleton()->FindSubAsset(location.m_sAsset);
    if (!pAsset.isValid())
      return false;
    sDocument = pAsset->m_pAssetInfo->m_Path.GetAbsolutePath();
  }

  plDocument* pDocument = plEditorApp::GetSingleton()->OpenDocument(sDocument, plDocumentFlags::RequestWindow | plDocumentFlags::AddToRecentFilesList);
  if (pDocument == nullptr)
    return false;

  if (location.m_NodeGuid.IsValid())
  {
    if (const plDocumentObject* pNode = pDocument->GetObjectManager()->GetObject(location.m_NodeGuid))
      pDocument->GetSelectionManager()->SetSelection(pNode);
  }

  return true;
}

void OnLoadPlugin()
{
  RegisterIconFolder();

  plWindowContentProvider::SetDefaultArrangement("VisualScriptClass", &ArrangeVisualScript);
  plWindowContentProvider::SetDefaultArrangement("VisualScriptLibrary", &ArrangeVisualScript);

  plVisualScriptActions::RegisterActions();

  if (plScriptTools* pTools = plScriptTools::GetSingleton())
    pTools->RegisterLocationOpener("Visual Script", plScriptTools::LocationOpener(&OpenVisualScriptLocation));

  // VisualScript
  {
    // Menu Bar
    {
      plActionMapManager::RegisterActionMap("VisualScriptAssetMenuBar").IgnoreResult();
      plStandardMenus::MapActions("VisualScriptAssetMenuBar", plStandardMenuTypes::Default | plStandardMenuTypes::Edit);
      plProjectActions::MapActions("VisualScriptAssetMenuBar");
      plDocumentActions::MapMenuActions("VisualScriptAssetMenuBar");
      plAssetActions::MapMenuActions("VisualScriptAssetMenuBar");
      plCommandHistoryActions::MapActions("VisualScriptAssetMenuBar");
      plEditActions::MapActions("VisualScriptAssetMenuBar", false, false);
      plVisualGraphActions::MapActions("VisualScriptAssetMenuBar", "G.Edit");
      plVisualScriptActions::MapActions("VisualScriptAssetMenuBar", "G.Edit");
    }

    // Tool Bar
    {
      plActionMapManager::RegisterActionMap("VisualScriptAssetToolBar").IgnoreResult();
      plDocumentActions::MapToolbarActions("VisualScriptAssetToolBar");
      plCommandHistoryActions::MapActions("VisualScriptAssetToolBar", "");
      plAssetActions::MapToolBarActions("VisualScriptAssetToolBar", true);
      plVisualGraphActions::MapActions("VisualScriptAssetToolBar");
      plVisualScriptActions::MapActions("VisualScriptAssetToolBar");
    }
  }
}

void OnUnloadPlugin()
{
  if (plScriptTools* pTools = plScriptTools::GetSingleton())
    pTools->UnregisterLocationOpener("Visual Script");

  plWindowContentProvider::SetDefaultArrangement("VisualScriptClass", nullptr);
  plWindowContentProvider::SetDefaultArrangement("VisualScriptLibrary", nullptr);

  plVisualScriptActions::UnregisterActions();

  UnregisterIconFolder();
}

PL_PLUGIN_ON_LOADED()
{
  OnLoadPlugin();
}

PL_PLUGIN_ON_UNLOADED()
{
  OnUnloadPlugin();
}
