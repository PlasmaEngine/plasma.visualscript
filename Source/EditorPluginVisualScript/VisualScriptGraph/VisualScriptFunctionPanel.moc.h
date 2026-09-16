#pragma once

#include <EditorPluginVisualScript/EditorPluginVisualScriptDLL.h>
#include <Foundation/Types/Uuid.h>

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QToolButton;
class plDocument;
class plDocumentObject;
class plQtVisualScriptNodeScene;
class plVisualScriptNodeManager;
struct plVisualGraphObjectManagerEvent;
struct plDocumentObjectPropertyEvent;

/// Lists the graphs of a visual script document: the event graph and one entry per function.
///
/// Clicking an entry shows that graph, double-clicking (or F2) renames it, the toolbar adds and removes functions.
/// Removing a function removes its entry node and everything in its graph; the event graph cannot be removed.
class PL_EDITORPLUGINVISUALSCRIPT_DLL plQtVisualScriptFunctionPanel : public QWidget
{
  Q_OBJECT

public:
  plQtVisualScriptFunctionPanel(QWidget* pParent, plDocument* pDocument, plQtVisualScriptNodeScene* pScene);
  ~plQtVisualScriptFunctionPanel();

  /// \brief Highlights the graph that contains \a scope.
  void SetCurrentScope(const plUuid& scope);

  /// \brief Creates a new function and shows its graph.
  void AddFunction();

private Q_SLOTS:
  void OnAddClicked();
  void OnRemoveClicked();
  void OnItemClicked(QListWidgetItem* pItem);
  void OnItemChanged(QListWidgetItem* pItem);
  void OnContextMenu(const QPoint& pos);

private:
  void NodeEventsHandler(const plVisualGraphObjectManagerEvent& e);
  void PropertyEventsHandler(const plDocumentObjectPropertyEvent& e);
  void ScheduleRefresh();
  void Refresh();
  void RemoveFunction(const plUuid& functionGuid);
  void ShowGraph(const plUuid& scope);

  static plUuid GetItemGuid(const QListWidgetItem* pItem);
  QListWidgetItem* FindItem(const plUuid& guid) const;

  plDocument* m_pDocument = nullptr;
  plQtVisualScriptNodeScene* m_pScene = nullptr;
  const plVisualScriptNodeManager* m_pManager = nullptr;

  QListWidget* m_pList = nullptr;
  QToolButton* m_pAddButton = nullptr;
  QToolButton* m_pRemoveButton = nullptr;

  plUuid m_CurrentRoot;
  bool m_bRefreshPending = false;
  bool m_bRefreshing = false;
};
