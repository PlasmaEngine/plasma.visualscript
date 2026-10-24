#include <EditorPluginAssets/EditorPluginAssetsPCH.h>

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptFunctionPanel.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraph.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraphView.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptWindow.h>
#include <GuiFoundation/EngineUi/EngineGraphView.h>

plVisualScriptWindow::plVisualScriptWindow(plDocument* pDocument)
  : plDocumentWindow(pDocument)
{
  SetMenuBarActions("VisualScriptAssetMenuBar");
  SetToolBarActions("VisualScriptAssetToolBar");

  CreateGraph();

  AddPropertiesPanel({"VisualScriptAssetDockWidget", "Properties", plEditorUiDockRole::Right});

  // Functions panel: the event graph and one entry per function
  m_pFunctionPanel = AddPanel<plVisualScriptFunctionPanel>({"VisualScriptFunctionsDockWidget", "Functions", plEditorUiDockRole::Left}, pDocument, GetGraph());

  GetDocument()->GetSelectionManager()->m_Events.AddEventHandler(plMakeDelegate(&plVisualScriptWindow::SelectionEventHandler, this));

  SelectionEventHandler(plSelectionManagerEvent());

  // a library has no event graph worth looking at; open its first function
  auto pManager = static_cast<const plVisualScriptNodeManager*>(pDocument->GetObjectManager());
  if (pManager->m_bIsLibrary)
  {
    plHybridArray<const plDocumentObject*, 16> entries;
    pManager->GetFunctionEntries(entries);
    if (entries.IsEmpty() == false)
    {
      GetGraph()->NavigateToScope(entries[0]->GetGuid());
    }
  }
}

plVisualScriptWindow::~plVisualScriptWindow()
{
  // the graph panel, which owns the controller, goes after this destructor's body
  if (m_pController != nullptr)
    m_pController->m_ScopeChanged.RemoveEventHandler(plMakeDelegate(&plVisualScriptWindow::OnScopeChanged, this));

  if (GetDocument() != nullptr)
  {
    GetDocument()->GetSelectionManager()->m_Events.RemoveEventHandler(
      plMakeDelegate(&plVisualScriptWindow::SelectionEventHandler, this));
  }
}

void plVisualScriptWindow::CreateGraph()
{
  plUniquePtr<plVisualGraphController> pController = PL_DEFAULT_NEW(plVisualScriptGraphController);
  pController->Init(static_cast<const plVisualGraphObjectManager*>(GetDocument()->GetObjectManager()));
  m_pController = static_cast<plVisualScriptGraphController*>(pController.Borrow());
  m_pController->m_ScopeChanged.AddEventHandler(plMakeDelegate(&plVisualScriptWindow::OnScopeChanged, this));

  // the engine view draws the breadcrumbs itself
  m_pGraph = AddPanel<plEditorUiGraphView>({"VisualScriptView", "Script"}, std::move(pController));
  m_pController->SetView(m_pGraph);
}

plVisualScriptGraphHost* plVisualScriptWindow::GetGraph() const
{
  return m_pController;
}

void plVisualScriptWindow::OnScopeChanged(const plUuid& newScope)
{
  if (m_pFunctionPanel)
    m_pFunctionPanel->SetCurrentScope(newScope);

  if (m_pGraph)
    m_pGraph->RequestFrameAll();
}

void plVisualScriptWindow::SelectionEventHandler(const plSelectionManagerEvent& e)
{
  auto pDocument = GetDocument();
  auto pSelectionManager = pDocument->GetSelectionManager();
  auto pManager = static_cast<const plVisualScriptNodeManager*>(pDocument->GetObjectManager());

  // the dynamic enums of the property grid (portal names, function names, library functions) follow the selected node
  const plDocumentObject* pSelected = pSelectionManager->GetSelection().IsEmpty() ? nullptr : pSelectionManager->GetSelection()[0];
  pManager->RefreshDynamicEnums(pSelected);

  if (pSelectionManager->IsSelectionEmpty())
  {
    // delayed execution
    m_Timers.Start(plTime::MakeZero(),
      [this]()
      {
        auto pDocument = GetDocument();
        auto pSelectionManager = pDocument->GetSelectionManager();

        // Check again if the selection is empty. This could have changed due to the delayed execution.
        if (pSelectionManager->IsSelectionEmpty())
        {
          pSelectionManager->SetSelection(pDocument->GetObjectManager()->GetRootObject()->GetChildren()[0]);
        }
      });
  }
}
