#pragma once

#include <EditorPluginVisualScript/EditorPluginVisualScriptDLL.h>
#include <EditorUi/Content.h>
#include <EditorUi/Runtime.h>
#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Strings/String.h>
#include <Foundation/Types/UniquePtr.h>
#include <Foundation/Types/Uuid.h>

#include <functional>

class plDocument;
class plEditorUiTree;
class plVisualScriptGraphHost;
class plVisualScriptNodeManager;
struct plDocumentObjectPropertyEvent;
struct plEditorUiPointerEvent;
struct plEditorUiTreeEvent;
struct plMsgUiEvent;
struct plVisualGraphObjectManagerEvent;

/// Lists the graphs of a visual script document: the event graph and one entry per function.
///
/// Clicking an entry, or moving to it with the arrow keys, shows that graph. Double-click, Enter or F2 renames the entry
/// in place. "Add Function" and "Remove Function" under the list, the context menu and Delete add and remove functions.
/// Removing a function asks first and removes its entry node and everything in its graph; the event graph cannot be
/// removed.
///
/// The visual script window adds it as its "Functions" panel.
class PL_EDITORPLUGINVISUALSCRIPT_DLL plVisualScriptFunctionPanel : public plEditorUiContent
{
public:
  /// \a pGraph is the graph the window shows (the engine graph controller).
  plVisualScriptFunctionPanel(plDocument* pDocument, plVisualScriptGraphHost* pGraph);
  ~plVisualScriptFunctionPanel();

  /// \brief Highlights the graph that contains \a scope.
  void SetCurrentScope(const plUuid& scope);

  /// \brief Creates a new function, shows its graph and starts renaming it.
  void AddFunction();

protected:
  void OnUiUpdate() override;
  void OnUiEvent(const plMsgUiEvent& event) override;

private:
  class Adapter;
  friend class Adapter;

  /// One row: the event graph (invalid guid) or a function.
  struct Entry
  {
    plUInt64 m_uiItem = 0;
    plUuid m_Guid;
    plString m_sName;
    plString m_sTooltip;
  };

  void BuildLayout();

  void NodeEventsHandler(const plVisualGraphObjectManagerEvent& e);
  void PropertyEventsHandler(const plDocumentObjectPropertyEvent& e);
  void OnTreeEvent(const plEditorUiTreeEvent& e);
  void OnPointer(const plEditorUiPointerEvent& e);
  void OnSelectionChanged();
  void ShowContextMenu(const plVec2& vPosition, const Entry* pEntry);

  void ScheduleRefresh();
  void Refresh();
  void SyncSelection();
  void ShowGraph(const plUuid& scope);
  void RenameGraph(plUInt64 uiItem, plStringView sText);
  void RemoveFunction(const plUuid& functionGuid);
  void RemoveSelectedFunction();

  /// Runs the action on the next editor UI frame, outside of the surface's event dispatch, while the panel lives.
  void Defer(std::function<void()> action);

  const Entry* FindEntry(plUInt64 uiItem) const;
  const Entry* GetSelectedEntry() const;
  static plUInt64 GetItemId(const plUuid& guid);

  plDocument* m_pDocument = nullptr;
  plVisualScriptGraphHost* m_pGraph = nullptr;
  const plVisualScriptNodeManager* m_pManager = nullptr;

  plUniquePtr<Adapter> m_pAdapter;
  plUniquePtr<plEditorUiTree> m_pTree;
  plDynamicArray<Entry> m_Entries;
  plEventSubscriptionID m_PointerSubscription = 0;
  plVec2 m_vBuiltSize = plVec2::MakeZero();

  plUuid m_CurrentRoot;
  bool m_bRefreshPending = false;

  /// Deferred actions; destroying the panel stops them.
  plEditorUiTimerGroup m_Timers;
};
