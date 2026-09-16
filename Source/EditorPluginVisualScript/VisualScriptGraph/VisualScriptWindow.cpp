#include <EditorFramework/Assets/AssetStatusIndicator.moc.h>
#include <EditorPluginAssets/EditorPluginAssetsPCH.h>

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptBreadcrumb.moc.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptFunctionPanel.moc.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraph.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraphQt.moc.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptWindow.moc.h>
#include <GuiFoundation/ActionViews/MenuBarActionMapView.moc.h>
#include <GuiFoundation/ActionViews/ToolBarActionMapView.moc.h>
#include <GuiFoundation/DockPanels/DocumentPanel.moc.h>
#include <GuiFoundation/PropertyGrid/PropertyGridWidget.moc.h>
#include <GuiFoundation/VisualGraph/View.moc.h>

plQtVisualScriptWindow::plQtVisualScriptWindow(plDocument* pDocument)
  : plQtDocumentWindow(pDocument)
{

  // Menu Bar
  {
    plQtMenuBarActionMapView* pMenuBar = static_cast<plQtMenuBarActionMapView*>(menuBar());
    plActionContext context;
    context.m_sMapping = "VisualScriptAssetMenuBar";
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pMenuBar->SetActionContext(context);
  }

  // Tool Bar
  {
    plQtToolBarActionMapView* pToolBar = new plQtToolBarActionMapView("Toolbar", this);
    plActionContext context;
    context.m_sMapping = "VisualScriptAssetToolBar";
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pToolBar->SetActionContext(context);
    pToolBar->setObjectName("VisualScriptAssetWindowToolBar");
    addToolBar(pToolBar);
  }

  // Central widget: breadcrumb above the graph view
  {
    m_pScene = new plQtVisualScriptNodeScene(this);
    m_pScene->InitScene(static_cast<const plVisualGraphObjectManager*>(pDocument->GetObjectManager()));

    m_pView = new plQtVisualGraphView(this);
    m_pView->SetScene(m_pScene);

    m_pBreadcrumb = new plQtVisualScriptBreadcrumb(this);

    QWidget* pCentralContainer = new QWidget(this);
    QVBoxLayout* pLayout = new QVBoxLayout(pCentralContainer);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->setSpacing(0);
    pLayout->addWidget(m_pBreadcrumb);
    pLayout->addWidget(m_pView, 1);
    pCentralContainer->setLayout(pLayout);

    plQtDocumentPanel* pCentral = new plQtDocumentPanel(this, pDocument);
    pCentral->setObjectName("VisualScriptView");
    pCentral->setWindowTitle("Script");
    pCentral->setWidget(pCentralContainer);

    m_pDockManager->setCentralWidget(pCentral);

    connect(m_pBreadcrumb, &plQtVisualScriptBreadcrumb::NavigateToScope, this, &plQtVisualScriptWindow::OnNavigateToScope);
    connect(m_pScene, &plQtVisualScriptNodeScene::ScopeChanged, this, &plQtVisualScriptWindow::OnScopeChanged);

    m_pBreadcrumb->UpdateFromNodeManager(static_cast<const plVisualScriptNodeManager*>(pDocument->GetObjectManager()));
  }

  {
    plQtDocumentPanel* pPropertyPanel = new plQtDocumentPanel(this, pDocument);
    pPropertyPanel->setObjectName("VisualScriptAssetDockWidget");
    pPropertyPanel->setWindowTitle("Properties");
    pPropertyPanel->show();

    plQtPropertyGridWidget* pPropertyGrid = new plQtPropertyGridWidget(pPropertyPanel, pDocument);

    QWidget* pWidget = new QWidget();
    pWidget->setObjectName("Group");
    pWidget->setLayout(new QVBoxLayout());
    pWidget->setContentsMargins(0, 0, 0, 0);

    pWidget->layout()->setContentsMargins(0, 0, 0, 0);
    pWidget->layout()->addWidget(new plQtAssetStatusIndicator((plAssetDocument*)GetDocument()));
    pWidget->layout()->addWidget(pPropertyGrid);

    pPropertyPanel->setWidget(pWidget, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidgetTab(ads::RightDockWidgetArea, pPropertyPanel);
  }

  // Functions panel: the event graph and one entry per function
  {
    plQtDocumentPanel* pFunctionsPanel = new plQtDocumentPanel(this, pDocument);
    pFunctionsPanel->setObjectName("VisualScriptFunctionsDockWidget");
    pFunctionsPanel->setWindowTitle("Functions");
    pFunctionsPanel->show();

    m_pFunctionPanel = new plQtVisualScriptFunctionPanel(pFunctionsPanel, pDocument, m_pScene);
    pFunctionsPanel->setWidget(m_pFunctionPanel, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidgetTab(ads::LeftDockWidgetArea, pFunctionsPanel);
  }

  GetDocument()->GetSelectionManager()->m_Events.AddEventHandler(plMakeDelegate(&plQtVisualScriptWindow::SelectionEventHandler, this));

  FinishWindowCreation();

  SelectionEventHandler(plSelectionManagerEvent());

  // a library has no event graph worth looking at; open its first function
  auto pManager = static_cast<const plVisualScriptNodeManager*>(pDocument->GetObjectManager());
  if (pManager->m_bIsLibrary)
  {
    plHybridArray<const plDocumentObject*, 16> entries;
    pManager->GetFunctionEntries(entries);
    if (entries.IsEmpty() == false)
    {
      m_pScene->NavigateToScope(entries[0]->GetGuid());
    }
  }
}

plQtVisualScriptWindow::~plQtVisualScriptWindow()
{
  if (GetDocument() != nullptr)
  {
    GetDocument()->GetSelectionManager()->m_Events.RemoveEventHandler(
      plMakeDelegate(&plQtVisualScriptWindow::SelectionEventHandler, this));
  }
}

void plQtVisualScriptWindow::OnNavigateToScope(const plUuid& scopeGuid)
{
  m_pScene->NavigateToScope(scopeGuid);
}

void plQtVisualScriptWindow::OnScopeChanged(const plUuid& newScope)
{
  m_pBreadcrumb->UpdateFromNodeManager(static_cast<const plVisualScriptNodeManager*>(GetDocument()->GetObjectManager()));
  if (m_pFunctionPanel)
    m_pFunctionPanel->SetCurrentScope(newScope);

  // the scope filter refreshes connections one event loop iteration later; frame after that
  if (m_pView)
  {
    QTimer::singleShot(10, this, [this]()
      {
        if (m_pView)
          m_pView->FrameContent();
      });
  }
}

void plQtVisualScriptWindow::SelectionEventHandler(const plSelectionManagerEvent& e)
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
    QTimer::singleShot(1,
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
