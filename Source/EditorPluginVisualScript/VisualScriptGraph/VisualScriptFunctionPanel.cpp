#include <EditorPluginVisualScript/EditorPluginVisualScriptPCH.h>

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptFunctionPanel.moc.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraph.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraphQt.moc.h>
#include <GuiFoundation/UIServices/UIServices.moc.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
  constexpr int s_iGuidRole = Qt::UserRole + 1;
}

plQtVisualScriptFunctionPanel::plQtVisualScriptFunctionPanel(QWidget* pParent, plDocument* pDocument, plQtVisualScriptNodeScene* pScene)
  : QWidget(pParent)
  , m_pDocument(pDocument)
  , m_pScene(pScene)
{
  m_pManager = static_cast<const plVisualScriptNodeManager*>(pDocument->GetObjectManager());

  QVBoxLayout* pLayout = new QVBoxLayout(this);
  pLayout->setContentsMargins(0, 0, 0, 0);
  pLayout->setSpacing(0);

  {
    QWidget* pToolbar = new QWidget(this);
    QHBoxLayout* pToolbarLayout = new QHBoxLayout(pToolbar);
    pToolbarLayout->setContentsMargins(2, 2, 2, 2);
    pToolbarLayout->setSpacing(2);

    m_pAddButton = new QToolButton(pToolbar);
    m_pAddButton->setIcon(QIcon(":/EditorPluginVisualScript/Icons/Function.svg"));
    m_pAddButton->setText("Add Function");
    m_pAddButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pAddButton->setToolTip("Adds a function with its own graph");
    m_pAddButton->setAutoRaise(true);

    m_pRemoveButton = new QToolButton(pToolbar);
    m_pRemoveButton->setIcon(QIcon(":/GuiFoundation/Icons/Delete.svg"));
    m_pRemoveButton->setToolTip("Removes the selected function and everything in its graph");
    m_pRemoveButton->setAutoRaise(true);

    pToolbarLayout->addWidget(m_pAddButton);
    pToolbarLayout->addStretch();
    pToolbarLayout->addWidget(m_pRemoveButton);

    pLayout->addWidget(pToolbar);
  }

  m_pList = new QListWidget(this);
  m_pList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_pList->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  m_pList->setContextMenuPolicy(Qt::CustomContextMenu);
  pLayout->addWidget(m_pList, 1);

  setLayout(pLayout);

  connect(m_pAddButton, &QToolButton::clicked, this, &plQtVisualScriptFunctionPanel::OnAddClicked);
  connect(m_pRemoveButton, &QToolButton::clicked, this, &plQtVisualScriptFunctionPanel::OnRemoveClicked);
  connect(m_pList, &QListWidget::itemClicked, this, &plQtVisualScriptFunctionPanel::OnItemClicked);
  connect(m_pList, &QListWidget::itemChanged, this, &plQtVisualScriptFunctionPanel::OnItemChanged);
  connect(m_pList, &QListWidget::customContextMenuRequested, this, &plQtVisualScriptFunctionPanel::OnContextMenu);

  auto pManager = const_cast<plVisualScriptNodeManager*>(m_pManager);
  pManager->m_NodeEvents.AddEventHandler(plMakeDelegate(&plQtVisualScriptFunctionPanel::NodeEventsHandler, this));
  pManager->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plQtVisualScriptFunctionPanel::PropertyEventsHandler, this));

  Refresh();
}

plQtVisualScriptFunctionPanel::~plQtVisualScriptFunctionPanel()
{
  auto pManager = const_cast<plVisualScriptNodeManager*>(m_pManager);
  pManager->m_NodeEvents.RemoveEventHandler(plMakeDelegate(&plQtVisualScriptFunctionPanel::NodeEventsHandler, this));
  pManager->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plQtVisualScriptFunctionPanel::PropertyEventsHandler, this));
}

// static
plUuid plQtVisualScriptFunctionPanel::GetItemGuid(const QListWidgetItem* pItem)
{
  plUuid guid;
  if (pItem == nullptr)
    return guid;

  QByteArray data = pItem->data(s_iGuidRole).toByteArray();
  if (data.size() == sizeof(plUuid))
  {
    plMemoryUtils::Copy(&guid, reinterpret_cast<const plUuid*>(data.constData()), 1);
  }
  return guid;
}

QListWidgetItem* plQtVisualScriptFunctionPanel::FindItem(const plUuid& guid) const
{
  for (int i = 0; i < m_pList->count(); ++i)
  {
    if (GetItemGuid(m_pList->item(i)) == guid)
      return m_pList->item(i);
  }
  return nullptr;
}

void plQtVisualScriptFunctionPanel::SetCurrentScope(const plUuid& scope)
{
  const plDocumentObject* pFunction = m_pManager->GetScopeFunction(scope);
  m_CurrentRoot = pFunction != nullptr ? pFunction->GetGuid() : plUuid();

  m_bRefreshing = true;
  m_pList->clearSelection();
  if (QListWidgetItem* pItem = FindItem(m_CurrentRoot))
  {
    pItem->setSelected(true);
    m_pList->setCurrentItem(pItem);
  }
  m_bRefreshing = false;

  m_pRemoveButton->setEnabled(m_CurrentRoot.IsValid());
}

void plQtVisualScriptFunctionPanel::NodeEventsHandler(const plVisualGraphObjectManagerEvent& e)
{
  switch (e.m_EventType)
  {
    case plVisualGraphObjectManagerEvent::Type::AfterNodeAdded:
    case plVisualGraphObjectManagerEvent::Type::AfterNodeRemoved:
      if (m_pManager->IsFunctionEntry(e.m_pObject))
      {
        ScheduleRefresh();
      }
      break;

    default:
      break;
  }
}

void plQtVisualScriptFunctionPanel::PropertyEventsHandler(const plDocumentObjectPropertyEvent& e)
{
  if (e.m_sProperty == "GraphName" || (e.m_sProperty == "Name" && m_pManager->IsNode(e.m_pObject) && m_pManager->IsFunctionEntry(e.m_pObject)))
  {
    ScheduleRefresh();
  }
}

void plQtVisualScriptFunctionPanel::ScheduleRefresh()
{
  // events fire in the middle of transactions; the list is rebuilt once the document has settled
  if (m_bRefreshPending)
    return;

  m_bRefreshPending = true;
  QTimer::singleShot(0, this, [this]()
    {
      m_bRefreshPending = false;
      Refresh();
    });
}

void plQtVisualScriptFunctionPanel::Refresh()
{
  m_bRefreshing = true;
  m_pList->clear();

  if (m_pManager->m_bIsLibrary == false)
  {
    QListWidgetItem* pRoot = new QListWidgetItem(QIcon(":/EditorPluginVisualScript/Icons/SubGraph.svg"), m_pManager->GetRootGraphName().GetData(), m_pList);
    pRoot->setFlags(pRoot->flags() | Qt::ItemIsEditable);
    pRoot->setToolTip("The event graph: event and message handlers live here");
    pRoot->setData(s_iGuidRole, QByteArray());
    QFont font = pRoot->font();
    font.setBold(true);
    pRoot->setFont(font);
  }

  plHybridArray<const plDocumentObject*, 16> entries;
  m_pManager->GetFunctionEntries(entries);

  // alphabetical, so the list is stable while functions are added
  struct ByName
  {
    const plVisualScriptNodeManager* m_pManager;
    bool Less(const plDocumentObject* a, const plDocumentObject* b) const
    {
      return m_pManager->GetFunctionName(a).Compare_NoCase(m_pManager->GetFunctionName(b)) < 0;
    }
  };
  entries.Sort(ByName{m_pManager});

  for (const plDocumentObject* pEntry : entries)
  {
    plString sName = m_pManager->GetFunctionName(pEntry);
    if (sName.IsEmpty())
      sName = "<unnamed>";

    QListWidgetItem* pItem = new QListWidgetItem(QIcon(":/EditorPluginVisualScript/Icons/Function.svg"), sName.GetData(), m_pList);
    pItem->setFlags(pItem->flags() | Qt::ItemIsEditable);

    const plUuid guid = pEntry->GetGuid();
    pItem->setData(s_iGuidRole, QByteArray(reinterpret_cast<const char*>(&guid), sizeof(plUuid)));

    if (m_pManager->IsCoroutine(pEntry))
      pItem->setToolTip("Coroutine function");
  }

  m_bRefreshing = false;

  SetCurrentScope(m_pScene->GetCurrentScope());
}

void plQtVisualScriptFunctionPanel::ShowGraph(const plUuid& scope)
{
  m_pScene->NavigateToScope(scope);

  // the function entry carries the inputs and outputs, so it is what the property grid should show
  if (scope.IsValid())
  {
    if (const plDocumentObject* pEntry = m_pManager->GetObject(scope))
    {
      m_pDocument->GetSelectionManager()->SetSelection(pEntry);
    }
  }
}

void plQtVisualScriptFunctionPanel::AddFunction()
{
  const plUuid guid = m_pScene->AddFunction("NewFunction");
  if (guid.IsValid() == false)
    return;

  ShowGraph(guid);

  Refresh();
  if (QListWidgetItem* pItem = FindItem(guid))
  {
    m_pList->editItem(pItem);
  }
}

void plQtVisualScriptFunctionPanel::RemoveFunction(const plUuid& functionGuid)
{
  const plDocumentObject* pEntry = m_pManager->GetObject(functionGuid);
  if (pEntry == nullptr || m_pManager->IsFunctionEntry(pEntry) == false)
    return;

  const plString sName = m_pManager->GetFunctionName(pEntry);
  if (plQtUiServices::GetSingleton()->MessageBoxQuestion(plFmt("Remove the function '{}' and everything in its graph?", sName), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    return;

  m_pScene->RemoveFunction(functionGuid);
}

void plQtVisualScriptFunctionPanel::OnAddClicked()
{
  AddFunction();
}

void plQtVisualScriptFunctionPanel::OnRemoveClicked()
{
  const plUuid guid = GetItemGuid(m_pList->currentItem());
  if (guid.IsValid())
  {
    RemoveFunction(guid);
  }
}

void plQtVisualScriptFunctionPanel::OnItemClicked(QListWidgetItem* pItem)
{
  if (m_bRefreshing || pItem == nullptr)
    return;

  const plUuid guid = GetItemGuid(pItem);
  if (guid == m_pScene->GetCurrentScope())
    return;

  ShowGraph(guid);
}

void plQtVisualScriptFunctionPanel::OnItemChanged(QListWidgetItem* pItem)
{
  if (m_bRefreshing || pItem == nullptr)
    return;

  const plString sNewName = pItem->text().trimmed().toUtf8().data();
  const plUuid guid = GetItemGuid(pItem);

  if (guid.IsValid())
  {
    if (sNewName.IsEmpty() || sNewName == m_pManager->GetFunctionName(m_pManager->GetObject(guid)))
    {
      ScheduleRefresh();
      return;
    }

    m_pScene->RenameFunction(guid, sNewName);
  }
  else
  {
    // the event graph's name is a document property
    if (m_pManager->GetRootObject()->GetChildren().IsEmpty())
      return;

    const plDocumentObject* pProperties = m_pManager->GetRootObject()->GetChildren()[0];
    plObjectAccessorBase* pAccessor = m_pDocument->GetObjectAccessor();
    pAccessor->StartTransaction("Rename Graph");
    if (pAccessor->SetValueByName(pProperties, "GraphName", sNewName).Failed())
      pAccessor->CancelTransaction();
    else
      pAccessor->FinishTransaction();
  }

  ScheduleRefresh();
}

void plQtVisualScriptFunctionPanel::OnContextMenu(const QPoint& pos)
{
  QListWidgetItem* pItem = m_pList->itemAt(pos);
  const plUuid guid = GetItemGuid(pItem);

  QMenu menu(this);
  QAction* pAdd = menu.addAction("Add Function");
  QAction* pRename = pItem != nullptr ? menu.addAction("Rename") : nullptr;
  QAction* pRemove = guid.IsValid() ? menu.addAction("Remove Function") : nullptr;

  QAction* pChosen = menu.exec(m_pList->viewport()->mapToGlobal(pos));
  if (pChosen == nullptr)
    return;

  if (pChosen == pAdd)
    AddFunction();
  else if (pChosen == pRename)
    m_pList->editItem(pItem);
  else if (pChosen == pRemove)
    RemoveFunction(guid);
}
