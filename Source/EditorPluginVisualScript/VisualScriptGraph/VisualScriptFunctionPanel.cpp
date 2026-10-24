#include <EditorPluginVisualScript/EditorPluginVisualScriptPCH.h>

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptFunctionPanel.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraph.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraphView.h>
#include <EditorUi/MenuBuilder.h>
#include <EditorUi/Surface.h>
#include <EditorUi/Tree.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Algorithm/HashingUtils.h>
#include <Foundation/Math/ColorScheme.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Document/Document.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

namespace
{
  /// The event graph's row; functions use a hash of their entry's guid, which stays the same across refreshes.
  constexpr plUInt64 s_uiEventGraphItem = 1;

  constexpr const char* s_szEventGraphIcon = ":/EditorPluginVisualScript/Icons/SubGraph.svg";
  constexpr const char* s_szFunctionIcon = ":/EditorPluginVisualScript/Icons/Function.svg";

  constexpr const char* s_szAddAction = "VisualScript.Functions.Add";
  constexpr const char* s_szRemoveAction = "VisualScript.Functions.Remove";
  constexpr const char* s_szNoFunctionEntry = "VisualScript.Functions.NoFunction";

  /// The row of Add Function and Remove Function under the list.
  constexpr float s_fButtonRowHeight = plEditorUi::ControlHeight + 12.0f;
} // namespace

//////////////////////////////////////////////////////////////////////////
// plVisualScriptFunctionPanel::Adapter

/// The rows of the tree: the panel's entries, flat, in the panel's order.
class plVisualScriptFunctionPanel::Adapter : public plEditorUiTreeAdapter
{
public:
  explicit Adapter(plVisualScriptFunctionPanel& ref_panel)
    : m_Panel(ref_panel)
  {
  }

  void GetChildren(plUInt64 uiParent, plDynamicArray<plUInt64>& out_children) const override
  {
    out_children.Clear();
    if (uiParent != 0)
      return;

    for (const Entry& entry : m_Panel.m_Entries)
      out_children.PushBack(entry.m_uiItem);
  }

  plUInt64 GetParent(plUInt64 /*uiItem*/) const override { return 0; }

  void GetRow(plUInt64 uiItem, plEditorUiTreeRow& out_row) const override
  {
    const Entry* pEntry = m_Panel.FindEntry(uiItem);
    if (pEntry == nullptr)
      return;

    out_row.m_sText = pEntry->m_sName;
    out_row.m_sTooltip = pEntry->m_sTooltip;
    out_row.m_sIcon = pEntry->m_Guid.IsValid() ? s_szFunctionIcon : s_szEventGraphIcon;
    out_row.m_IconColor = plColorScheme::GetCategoryColor("Scripting", plColorScheme::CategoryColorUsage::OverlayIcon);
  }

  bool CanRename(plUInt64 uiItem) const override { return m_Panel.FindEntry(uiItem) != nullptr; }

  plStatus Rename(plUInt64 uiItem, plStringView sText) override
  {
    m_Panel.RenameGraph(uiItem, sText);
    return plStatus(PL_SUCCESS);
  }

  /// Delete removes the selected function (the list selects one row at a time).
  void Delete(plArrayPtr<const plUInt64> /*items*/) override { m_Panel.RemoveSelectedFunction(); }

  /// Double-click and Enter rename.
  void Activate(plUInt64 uiItem) override { m_Panel.m_pTree->BeginRename(uiItem); }

private:
  plVisualScriptFunctionPanel& m_Panel;
};

//////////////////////////////////////////////////////////////////////////
// plVisualScriptFunctionPanel

plVisualScriptFunctionPanel::plVisualScriptFunctionPanel(plDocument* pDocument, plVisualScriptGraphHost* pGraph)
  : plEditorUiContent("VisualScript.Functions")
  , m_pDocument(pDocument)
  , m_pGraph(pGraph)
{
  m_pManager = static_cast<const plVisualScriptNodeManager*>(pDocument->GetObjectManager());
  m_pAdapter = PL_DEFAULT_NEW(Adapter, *this);

  if (IsReady())
  {
    m_pTree = PL_DEFAULT_NEW(plEditorUiTree, *GetSurface(), "VisualScript.FunctionTree", *m_pAdapter);
    m_pTree->m_Events.AddEventHandler(plMakeDelegate(&plVisualScriptFunctionPanel::OnTreeEvent, this));

    // after the tree's handler, so a right-click on a row is the tree's
    m_PointerSubscription = GetSurface()->m_PointerEvents.AddEventHandler(plMakeDelegate(&plVisualScriptFunctionPanel::OnPointer, this));
  }

  auto pManager = const_cast<plVisualScriptNodeManager*>(m_pManager);
  pManager->m_NodeEvents.AddEventHandler(plMakeDelegate(&plVisualScriptFunctionPanel::NodeEventsHandler, this));
  pManager->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plVisualScriptFunctionPanel::PropertyEventsHandler, this));

  Refresh();
}

plVisualScriptFunctionPanel::~plVisualScriptFunctionPanel()
{
  auto pManager = const_cast<plVisualScriptNodeManager*>(m_pManager);
  pManager->m_NodeEvents.RemoveEventHandler(plMakeDelegate(&plVisualScriptFunctionPanel::NodeEventsHandler, this));
  pManager->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plVisualScriptFunctionPanel::PropertyEventsHandler, this));

  if (m_pTree)
  {
    m_pTree->m_Events.RemoveEventHandler(plMakeDelegate(&plVisualScriptFunctionPanel::OnTreeEvent, this));
    GetSurface()->m_PointerEvents.RemoveEventHandler(m_PointerSubscription);
  }
}

// static
plUInt64 plVisualScriptFunctionPanel::GetItemId(const plUuid& guid)
{
  if (!guid.IsValid())
    return s_uiEventGraphItem;

  // 0 is the tree's root
  const plUInt64 uiId = plHashingUtils::xxHash64(&guid, sizeof(plUuid));
  return uiId > s_uiEventGraphItem ? uiId : uiId + 2;
}

const plVisualScriptFunctionPanel::Entry* plVisualScriptFunctionPanel::FindEntry(plUInt64 uiItem) const
{
  for (const Entry& entry : m_Entries)
  {
    if (entry.m_uiItem == uiItem)
      return &entry;
  }
  return nullptr;
}

const plVisualScriptFunctionPanel::Entry* plVisualScriptFunctionPanel::GetSelectedEntry() const
{
  if (!m_pTree)
    return nullptr;

  plDynamicArray<plUInt64> selection;
  m_pTree->GetSelection(selection);
  return selection.IsEmpty() ? nullptr : FindEntry(selection[0]);
}

void plVisualScriptFunctionPanel::SetCurrentScope(const plUuid& scope)
{
  const plDocumentObject* pFunction = m_pManager->GetScopeFunction(scope);
  m_CurrentRoot = pFunction != nullptr ? pFunction->GetGuid() : plUuid();

  SyncSelection();
}

void plVisualScriptFunctionPanel::SyncSelection()
{
  if (!m_pTree)
    return;

  // one selected row: the graph that is shown
  const plUInt64 uiItem = GetItemId(m_CurrentRoot);
  if (FindEntry(uiItem) != nullptr)
  {
    m_pTree->SetSelection(plMakeArrayPtr(&uiItem, 1));
    m_pTree->Reveal(uiItem);
  }
  else
  {
    m_pTree->SetSelection({});
  }

  GetSurface()->GetBlackboard().SetEntryValue(s_szNoFunctionEntry, !m_CurrentRoot.IsValid());
  RequestRedraw();
}

void plVisualScriptFunctionPanel::OnUiUpdate()
{
  if (!m_pTree || GetSurface()->GetLogicalSize() == m_vBuiltSize)
    return;

  m_vBuiltSize = GetSurface()->GetLogicalSize();
  BuildLayout();
}

void plVisualScriptFunctionPanel::BuildLayout()
{
  using namespace plEditorUi;

  plUiPanel* pRoot = Panel();
  pRoot->m_Color = Surface();

  plUiPanel* pList = Panel(pRoot);
  Fill(pList, plVec4(0, 0, 0, s_fButtonRowHeight));
  pList->m_Color = Surface();
  m_pTree->Build(pList);

  // Add Function and Remove Function under the list
  plUiPanel* pButtons = Panel(pRoot);
  pButtons->m_vAnchorMin.y = 1;
  pButtons->m_vOffsets = plVec4(0, -s_fButtonRowHeight, 0, s_fButtonRowHeight);
  pButtons->m_Color = Surface();

  plUiPanel* pLine = Panel(pButtons);
  pLine->m_vAnchorMax.y = 0;
  pLine->m_vOffsets = plVec4(0, 0, 0, 1);
  pLine->m_Color = Line();

  // text buttons as wide as their text; Add Function leads with its plus
  const plStringView sAdd = "Add Function";
  const plStringView sRemove = "Remove Function";
  const float fAddWidth = plMath::Ceil(MeasureText(sAdd, FontSize - 1)) + 40.0f;
  const float fRemoveWidth = plMath::Ceil(MeasureText(sRemove, FontSize - 1)) + 20.0f;

  plUiButton* pAdd = TextButton(pButtons, sAdd, s_szAddAction);
  pAdd->m_vAnchorMin = pAdd->m_vAnchorMax = plVec2::MakeZero();
  pAdd->m_vOffsets = plVec4(8, 6, fAddWidth, ControlHeight);
  static_cast<plUiText*>(pAdd->m_Children[0])->m_vOffsets.x = 26;

  plUiImage* pPlus = ButtonIcon(pAdd, Icon::Add);
  pPlus->m_vAnchorMin = pPlus->m_vAnchorMax = plVec2(0, 0.5f);
  pPlus->m_vOffsets = plVec4(8, -7, 14, 14);
  pPlus->m_Color = Muted();

  plUiButton* pRemove = TextButton(pButtons, sRemove, s_szRemoveAction);
  pRemove->m_vAnchorMin = pRemove->m_vAnchorMax = plVec2::MakeZero();
  pRemove->m_vOffsets = plVec4(16 + fAddWidth, 6, fRemoveWidth, ControlHeight);
  pRemove->m_sDisabledBinding = s_szNoFunctionEntry;

  plBlackboard& board = GetSurface()->GetBlackboard();
  SetToolTip(board, s_szAddAction, "Adds a function with its own graph");
  SetToolTip(board, s_szRemoveAction, "Removes the selected function and everything in its graph");

  SetDocument(Finish(pRoot));

  m_pTree->Refresh();
  SyncSelection();
}

void plVisualScriptFunctionPanel::OnUiEvent(const plMsgUiEvent& event)
{
  if (event.m_sIdentifier.GetView() == s_szAddAction)
  {
    Defer([this]()
      { AddFunction(); });
  }
  else if (event.m_sIdentifier.GetView() == s_szRemoveAction)
  {
    RemoveSelectedFunction();
  }
}

void plVisualScriptFunctionPanel::OnTreeEvent(const plEditorUiTreeEvent& e)
{
  switch (e.m_Type)
  {
    case plEditorUiTreeEvent::Type::SelectionChanged:
      OnSelectionChanged();
      break;

    case plEditorUiTreeEvent::Type::ContextMenu:
      ShowContextMenu(e.m_vPosition, FindEntry(m_pTree->GetCurrent()));
      break;

    default:
      break;
  }
}

void plVisualScriptFunctionPanel::OnSelectionChanged()
{
  // the row clicked or moved to with the keys; Ctrl and Shift selections collapse to it
  const Entry* pEntry = FindEntry(m_pTree->GetCurrent());
  if (pEntry == nullptr || pEntry->m_Guid == m_pGraph->GetCurrentScope())
  {
    SyncSelection();
    return;
  }

  ShowGraph(pEntry->m_Guid);
}

void plVisualScriptFunctionPanel::OnPointer(const plEditorUiPointerEvent& e)
{
  // a right-click on the list below the rows offers Add Function; the rows' menu comes from the tree
  if (e.m_Type != plEditorUiPointerEvent::Type::Press || e.m_Button != plEditorUiPointerEvent::Button::Right || e.m_iItemIndex >= 0)
    return;

  if (e.m_vPosition.y >= m_vBuiltSize.y - s_fButtonRowHeight)
    return;

  ShowContextMenu(e.m_vPosition, nullptr);
}

void plVisualScriptFunctionPanel::ShowContextMenu(const plVec2& vPosition, const Entry* pEntry)
{
  // the picked entry's work runs deferred, like the buttons' (adding renames in place, removing asks first)
  auto deferred = [this](std::function<void()> work) -> plEditorUiMenuBuilder::Work
  {
    return [this, work]()
    { Defer(work); };
  };

  plEditorUiMenuBuilder menu;

  auto addFunction = [this]()
  { AddFunction(); };
  menu.AddAction("Add Function", deferred(addFunction)).m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Add);

  if (pEntry != nullptr)
  {
    const plUInt64 uiItem = pEntry->m_uiItem;
    auto rename = [this, uiItem]()
    { m_pTree->BeginRename(uiItem); };
    menu.AddAction("Rename", deferred(rename), "F2").m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Rename);
  }

  if (pEntry != nullptr && pEntry->m_Guid.IsValid())
  {
    const plUuid guid = pEntry->m_Guid;
    auto removeFunction = [this, guid]()
    { RemoveFunction(guid); };

    plEditorUiMenuItem& remove = menu.AddAction("Remove Function", deferred(removeFunction), "Del");
    remove.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Remove);
    remove.m_bDanger = true;
  }

  menu.Show(*GetSurface(), vPosition);
}

void plVisualScriptFunctionPanel::NodeEventsHandler(const plVisualGraphObjectManagerEvent& e)
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

void plVisualScriptFunctionPanel::PropertyEventsHandler(const plDocumentObjectPropertyEvent& e)
{
  if (e.m_sProperty == "GraphName" || (e.m_sProperty == "Name" && m_pManager->IsNode(e.m_pObject) && m_pManager->IsFunctionEntry(e.m_pObject)))
  {
    ScheduleRefresh();
  }
}

void plVisualScriptFunctionPanel::Defer(std::function<void()> action)
{
  m_Timers.Start(plTime::MakeZero(), std::move(action));
}

void plVisualScriptFunctionPanel::ScheduleRefresh()
{
  // events fire in the middle of transactions; the list is rebuilt once the document has settled
  if (m_bRefreshPending)
    return;

  m_bRefreshPending = true;
  Defer([this]()
    {
      m_bRefreshPending = false;
      Refresh(); });
}

void plVisualScriptFunctionPanel::Refresh()
{
  m_Entries.Clear();

  if (m_pManager->m_bIsLibrary == false)
  {
    Entry& root = m_Entries.ExpandAndGetRef();
    root.m_uiItem = s_uiEventGraphItem;
    root.m_sName = m_pManager->GetRootGraphName();
    root.m_sTooltip = "The event graph: event and message handlers live here";
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
    Entry& entry = m_Entries.ExpandAndGetRef();
    entry.m_Guid = pEntry->GetGuid();
    entry.m_uiItem = GetItemId(entry.m_Guid);
    entry.m_sName = m_pManager->GetFunctionName(pEntry);

    if (entry.m_sName.IsEmpty())
      entry.m_sName = "<unnamed>";

    if (m_pManager->IsCoroutine(pEntry))
      entry.m_sTooltip = "Coroutine function";
  }

  if (m_pTree)
    m_pTree->Refresh();

  SetCurrentScope(m_pGraph->GetCurrentScope());
}

void plVisualScriptFunctionPanel::ShowGraph(const plUuid& scope)
{
  m_pGraph->NavigateToScope(scope);

  // the function entry carries the inputs and outputs, so it is what the property grid should show
  if (scope.IsValid())
  {
    if (const plDocumentObject* pEntry = m_pManager->GetObject(scope))
    {
      m_pDocument->GetSelectionManager()->SetSelection(pEntry);
    }
  }
}

void plVisualScriptFunctionPanel::AddFunction()
{
  const plUuid guid = m_pGraph->AddFunction("NewFunction");
  if (guid.IsValid() == false)
    return;

  ShowGraph(guid);

  Refresh();

  // the tree can only edit a row once it is built into the surface
  if (m_pTree && !m_vBuiltSize.IsZero())
  {
    m_pTree->BeginRename(GetItemId(guid));
  }
}

void plVisualScriptFunctionPanel::RenameGraph(plUInt64 uiItem, plStringView sText)
{
  const Entry* pEntry = FindEntry(uiItem);
  if (pEntry == nullptr)
    return;

  plStringBuilder sNewName = sText;
  sNewName.Trim();

  const plUuid guid = pEntry->m_Guid;

  if (guid.IsValid())
  {
    if (sNewName.IsEmpty() || sNewName == m_pManager->GetFunctionName(m_pManager->GetObject(guid)))
    {
      ScheduleRefresh();
      return;
    }

    m_pGraph->RenameFunction(guid, sNewName);
  }
  else
  {
    // the tree commits unchanged names too; those leave the document alone
    if (sNewName == m_pManager->GetRootGraphName())
      return;

    // the event graph's name is a document property
    if (m_pManager->GetRootObject()->GetChildren().IsEmpty())
      return;

    const plDocumentObject* pProperties = m_pManager->GetRootObject()->GetChildren()[0];
    plObjectAccessorBase* pAccessor = m_pDocument->GetObjectAccessor();
    pAccessor->StartTransaction("Rename Graph");
    if (pAccessor->SetValueByName(pProperties, "GraphName", plString(sNewName)).Failed())
      pAccessor->CancelTransaction();
    else
      pAccessor->FinishTransaction();
  }

  ScheduleRefresh();
}

void plVisualScriptFunctionPanel::RemoveSelectedFunction()
{
  const Entry* pEntry = GetSelectedEntry();
  if (pEntry == nullptr || !pEntry->m_Guid.IsValid())
    return;

  // the question is a modal box; it opens outside of the surface's event dispatch
  const plUuid guid = pEntry->m_Guid;
  Defer([this, guid]()
    { RemoveFunction(guid); });
}

void plVisualScriptFunctionPanel::RemoveFunction(const plUuid& functionGuid)
{
  const plDocumentObject* pEntry = m_pManager->GetObject(functionGuid);
  if (pEntry == nullptr || m_pManager->IsFunctionEntry(pEntry) == false)
    return;

  const plString sName = m_pManager->GetFunctionName(pEntry);
  if (plEditorUiServices::MessageBoxQuestion(plFmt("Remove the function '{}' and everything in its graph?", sName), plEditorUiMessageButton::Yes | plEditorUiMessageButton::No, plEditorUiMessageButton::No) != plEditorUiMessageButton::Yes)
    return;

  m_pGraph->RemoveFunction(functionGuid);
}
