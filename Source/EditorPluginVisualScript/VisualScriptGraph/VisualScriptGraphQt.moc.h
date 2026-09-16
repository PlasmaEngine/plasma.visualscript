#pragma once

#include <GuiFoundation/VisualGraph/Connection.h>
#include <GuiFoundation/VisualGraph/Node.h>
#include <GuiFoundation/VisualGraph/Pin.h>
#include <GuiFoundation/VisualGraph/Scene.moc.h>
#include <VisualScriptPlugin/Runtime/VisualScriptDataType.h>

class plVisualScriptNodeManager;

/// Qt graphics item for visual script pins.
///
/// Displays pins with color coding based on data type and provides tooltips with detailed type information.
/// Handles both execution pins and data pins with appropriate visual styling.
class plQtVisualScriptPin : public plQtVisualGraphPin
{
public:
  plQtVisualScriptPin();

  virtual void SetPin(const plVisualGraphPin& pin) override;
  virtual bool UpdatePinColors(const plColorGammaUB* pOverwriteColor = nullptr) override;

  /// \brief Centre of the pin shape in the pin's own coordinates.
  QPointF GetPinCenterLocal() const { return m_PinCenter; }

  /// \brief Hides the label and shrinks the pin to its shape only (reroute dots).
  void SetCompact(bool bCompact);

private:
  void UpdateTooltip();
};

/// Qt graphics item for visual script connections.
///
/// Renders connections between visual script pins, representing both execution flow and data flow.
class plQtVisualScriptConnection : public plQtVisualGraphConnection
{
public:
  plQtVisualScriptConnection();
};

/// Qt graphics item for visual script nodes.
///
/// Visual representation of nodes in a visual script, such as function calls, variables, or control flow nodes.
/// Displays special icons for coroutine and loop nodes and a red outline with a tooltip when the node has an error.
class plQtVisualScriptNode : public plQtVisualGraphNode
{
public:
  plQtVisualScriptNode();

  virtual void UpdateState() override;
  virtual void ExtendContextMenu(QMenu& ref_menu) override;

protected:
  virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

  void UpdateErrorState();
  QColor GetErrorColor() const;

  bool m_bHasError = false;
};

/// A small dot that forwards one connection, for tidying up long or crossing connections.
class plQtVisualScriptRerouteNode : public plQtVisualScriptNode
{
public:
  plQtVisualScriptRerouteNode();

  virtual void InitNode(const plVisualGraphObjectManager* pManager, const plDocumentObject* pObject) override;
  virtual void UpdateGeometry() override;
  virtual void UpdateState() override;

  /// The pins sit on top of the dot and are hidden by default so a click selects and drags the node.
  /// They are shown while a connection is dragged (drop target) or when a drag starts from the dot with Ctrl held.
  void SetPinsVisible(bool bVisible);

protected:
  virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

  static constexpr qreal s_fRadius = 7.0;
  bool m_bPinsVisible = false;
};

/// Pill-shaped node showing the portal name with an arrow glyph pointing into (in) or out of (out) the body.
class plQtVisualScriptPortalNode : public plQtVisualScriptNode
{
public:
  plQtVisualScriptPortalNode();

  virtual void UpdateGeometry() override;
  virtual void UpdateState() override;

protected:
  virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

/// A sub-graph node: double-click enters it, the context menu can expand it back into the parent graph.
class plQtVisualScriptSubGraphNode : public plQtVisualScriptNode
{
public:
  plQtVisualScriptSubGraphNode();

  virtual void ExtendContextMenu(QMenu& ref_menu) override;
};

/// Qt scene for visual script graphs.
///
/// On top of the base scene it shows one sub-graph scope at a time, enters and leaves sub-graphs, collapses a selection
/// into a sub-graph and expands one again, inserts reroutes into connections and keeps 'Return' nodes attached to the
/// function they belong to.
class plQtVisualScriptNodeScene : public plQtVisualGraphScene
{
  Q_OBJECT

public:
  plQtVisualScriptNodeScene(QObject* pParent = nullptr);
  ~plQtVisualScriptNodeScene();

  virtual void InitScene(const plVisualGraphObjectManager* pManager);

  const QPixmap& GetCoroutineIcon() const { return m_CoroutineIcon; }
  const QPixmap& GetLoopIcon() const { return m_LoopIcon; }

  const plVisualScriptNodeManager* GetScriptManager() const;

  // Scope navigation (event graph, function graphs, sub-graphs)
  const plUuid& GetCurrentScope() const { return m_CurrentScope; }
  void NavigateToScope(const plUuid& scope);
  void NavigateToParentScope();

  // Functions: each has its own graph whose root is the function entry node
  /// \brief Creates a function entry with a unique name based on \a sName and returns its guid (the scope of the new graph).
  plUuid AddFunction(plStringView sName);
  /// \brief Removes the function entry and everything in its graph.
  void RemoveFunction(const plUuid& functionGuid);
  void RenameFunction(const plUuid& functionGuid, plStringView sNewName);

  // Sub-graph editing
  void CollapseSelectionToSubGraph();
  void ExpandSubGraph(const plDocumentObject* pSubGraph);
  void ExpandSelectedSubGraph();

  // Reroutes and portals
  void InsertRerouteOnConnection(const plDocumentObject* pConnectionObject, const plVec2& vPos);
  void InsertPortalOnConnection(const plDocumentObject* pConnectionObject, const plVec2& vPos);

Q_SIGNALS:
  void ScopeChanged(const plUuid& scope);

protected:
  /// Execution pins are the flow that "Arrange Nodes" keeps straight; data nodes stack next to the node that reads them.
  virtual bool IsLayoutFlowPin(const plVisualGraphPin& pin) const override;

  virtual void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
  virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
  virtual void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
  virtual void contextMenuEvent(QGraphicsSceneContextMenuEvent* contextMenuEvent) override;
  virtual void keyPressEvent(QKeyEvent* event) override;

  virtual void RemoveSelectedNodesAction() override;
  virtual void ConnectPinsAction(const plVisualGraphPin& sourcePin, const plVisualGraphPin& targetPin) override;
  virtual void DisconnectPinsAction(plQtVisualGraphConnection* pConnection) override;
  virtual void DisconnectPinsAction(plQtVisualGraphPin* pPin) override;

private:
  void NodeChangedHandler(const plDocumentObject* pObject);
  void ScopeNodeEventsHandler(const plVisualGraphObjectManagerEvent& e);
  void ScopePropertyEventsHandler(const plDocumentObjectPropertyEvent& e);
  void ApplyScopeFilter();
  void RefreshReturnOwners();
  void GetSelectedNodeObjectsInScope(plDynamicArray<const plDocumentObject*>& out_nodes) const;
  plStatus EnsureSubGraphBoundaries(const plUuid& scope);
  void ShowReroutePins(bool bVisible);
  QGraphicsItem* FindGraphItemAt(const QPointF& scenePos, int& out_iType) const;

  QPixmap m_CoroutineIcon;
  QPixmap m_LoopIcon;
  plUuid m_CurrentScope;
  plQtVisualGraphPin* m_pDragStartPin = nullptr;
  bool m_bScopeFilterPending = false;
};
