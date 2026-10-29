#pragma once

#include <EditorPluginVisualScript/EditorPluginVisualScriptDLL.h>
#include <EditorUi/Runtime.h>
#include <Foundation/Communication/Event.h>
#include <Foundation/Math/Rect.h>
#include <Foundation/Types/Uuid.h>
#include <GuiFoundation/VisualGraph/VisualGraphController.h>

class plEditorUiGraphView;
struct plScriptToolsEvent;
class plVisualScriptNodeManager;
class plVisualScriptPin;

/// \brief What the Functions panel and the visual script actions need from the graph a window shows.
///
/// Implemented by the engine graph controller (plVisualScriptGraphController).
class PL_EDITORPLUGINVISUALSCRIPT_DLL plVisualScriptGraphHost
{
public:
  virtual ~plVisualScriptGraphHost() = default;

  /// The shown scope: invalid for the event graph, else the guid of a function entry or sub-graph node.
  virtual const plUuid& GetCurrentScope() const = 0;
  virtual void NavigateToScope(const plUuid& scope) = 0;
  virtual void NavigateToParentScope() = 0;

  /// \brief Creates a function entry with a unique name based on \a sName and returns its guid (the scope of the new graph).
  virtual plUuid AddFunction(plStringView sName) = 0;
  /// \brief Removes the function entry and everything in its graph.
  virtual void RemoveFunction(const plUuid& functionGuid) = 0;
  virtual void RenameFunction(const plUuid& functionGuid, plStringView sNewName) = 0;

  virtual void CollapseSelectionToSubGraph() = 0;
  virtual void ExpandSelectedSubGraph() = 0;
};

/// \brief The visual script graph rules and edits of the engine graph controller.
///
/// Every edit is one undoable transaction and reports failures in a message box.
class PL_EDITORPLUGINVISUALSCRIPT_DLL plVisualScriptGraphEditing
{
public:
  // Look

  /// The node title (plTitleAttribute with property values filled in, or the nice type name) and its subtitle: the part
  /// before "::" in the title, else the deduced type of type-deduction nodes.
  static void ComputeNodeTitle(const plVisualScriptNodeManager* pManager, const plDocumentObject* pNode, plStringBuilder& out_sTitle, plStringBuilder& out_sSubtitle);

  /// The coroutine or loop icon of a node (icon id), empty for other nodes.
  static plStringView GetNodeIcon(const plVisualScriptNodeManager* pManager, const plDocumentObject* pNode);

  /// The colour of a reroute or portal node: grey for execution flow, the data type colour otherwise.
  static plColorGammaUB GetRoutingNodeColor(const plVisualScriptNodeManager* pManager, const plDocumentObject* pNode);

  /// The colour of the deduced type for pins whose type is deduced; false for all other pins.
  static bool GetDeducedPinColor(const plVisualScriptPin& pin, plColorGammaUB& out_color);

  /// An unconnected input that must be connected. Game object targets fall back to the script owner unless the document is a library.
  static bool IsMissingRequiredConnection(const plVisualScriptNodeManager* pManager, const plVisualScriptPin& pin);

  /// "Name", or "Name: Type" with " (Required)" for data pins.
  static void GetPinTooltip(const plVisualScriptNodeManager* pManager, const plVisualScriptPin& pin, plStringBuilder& out_sTooltip);

  /// Execution pins are the flow that "Arrange Nodes" keeps straight; data nodes stack next to the node that reads them.
  static bool IsLayoutFlowPin(const plVisualGraphPin& pin);

  // Scopes

  /// \a scope when it is a function entry or sub-graph node, else the invalid scope (the event graph).
  static plUuid ValidateScope(const plVisualScriptNodeManager* pManager, const plUuid& scope);

  /// Adds the input and output boundary nodes a sub-graph scope is missing.
  static plStatus EnsureSubGraphBoundaries(const plVisualScriptNodeManager* pManager, const plUuid& scope);

  /// The nodes of \a selection that are stored in \a scope, without comments.
  static void FilterNodesInScope(const plVisualScriptNodeManager* pManager, const plUuid& scope, plArrayPtr<const plDocumentObject* const> selection, plDynamicArray<const plDocumentObject*>& out_nodes);

  // Functions

  static plUuid AddFunction(const plVisualScriptNodeManager* pManager, plStringView sName);
  /// Removes the entry and every node of its graph. The caller leaves the graph first if it is shown.
  static void RemoveFunction(const plVisualScriptNodeManager* pManager, const plDocumentObject* pEntry);
  static void RenameFunction(const plVisualScriptNodeManager* pManager, const plUuid& functionGuid, plStringView sNewName);

  /// Sets the 'Function' of each Return node to the function whose graph reaches it. Runs inside the caller's transaction.
  static void RefreshReturnOwners(const plVisualScriptNodeManager* pManager);

  // Edits

  /// Removes the nodes with the visual script rules: a sub-graph node takes its contents with it, sub-graph boundaries and
  /// function entries stay.
  static void RemoveNodes(const plVisualScriptNodeManager* pManager, plArrayPtr<const plDocumentObject* const> nodes);

  /// Wraps a pin edit (\a edit adds its own transaction) into one transaction that also refreshes the Return owners.
  static void EditConnections(const plVisualScriptNodeManager* pManager, plStringView sTransaction, plDelegate<void()> edit);

  static void InsertRerouteOnConnection(const plVisualScriptNodeManager* pManager, const plDocumentObject* pConnectionObject, const plVec2& vPos);

  /// Asks for a portal name and replaces the connection by a 'Portal In' and a 'Portal Out' of that name.
  static void InsertPortalOnConnection(const plVisualScriptNodeManager* pManager, const plDocumentObject* pConnectionObject, const plVec2& vPos);

  /// Whether "Collapse to Sub-graph" applies to a node: entries, Returns and sub-graph boundaries cannot be collapsed.
  static bool CanCollapse(const plVisualScriptNodeManager* pManager, const plDocumentObject* pNode);

  /// Moves \a selection (nodes of \a scope, see FilterNodesInScope) into a new sub-graph centred on \a bounds; the crossing
  /// connections become its pins. Returns the sub-graph node, invalid on failure.
  static plUuid CollapseToSubGraph(const plVisualScriptNodeManager* pManager, plArrayPtr<const plDocumentObject* const> selection, const plRectFloat& bounds, const plUuid& scope);

  /// Expands the one sub-graph node in \a selection.
  static void ExpandSelectedSubGraph(const plVisualScriptNodeManager* pManager, plArrayPtr<const plDocumentObject* const> selection);

  /// Moves the contents of the sub-graph into its parent graph and removes the sub-graph node and its boundaries.
  static void ExpandSubGraph(const plVisualScriptNodeManager* pManager, const plDocumentObject* pSubGraph);
};

/// \brief The visual script graph in the engine graph view.
///
/// The view shows one scope at a time: the event graph, a function graph or a sub-graph, with breadcrumbs back up.
/// Double-clicking a sub-graph node enters it, double-clicking a boundary node or Backspace goes up. Nodes show
/// their coroutine or loop icon and a red outline with the reason as tooltip when they have an error; pins take the
/// colour of their deduced type and a red frame when a required input is unconnected. Reroutes and portals draw as
/// pills. The context menus add "Collapse to Sub-graph", "Enter Sub-graph", "Expand Sub-graph", "Insert Reroute" and
/// "Insert Portal...". Connecting, disconnecting and removing keep the Return nodes attached to their function.
class PL_EDITORPLUGINVISUALSCRIPT_DLL plVisualScriptGraphController : public plVisualGraphController, public plVisualScriptGraphHost
{
public:
  plVisualScriptGraphController();
  ~plVisualScriptGraphController();

  void Init(const plVisualGraphObjectManager* pManager) override;

  /// The view that owns this controller; collapsing uses its node bounds.
  void SetView(const plEditorUiGraphView* pView) { m_pView = pView; }

  /// Called with the new scope after navigating.
  plEvent<const plUuid&> m_ScopeChanged;

  // Look

  void GetNodeLook(const plDocumentObject* pNode, plVisualGraphNodeLook& out_look) const override;
  void GetPinLook(const plVisualGraphPin& pin, plVisualGraphPinLook& out_look) const override;
  void GetConnectionLook(const plDocumentObject* pConnection, plVisualGraphConnectionLook& out_look) const override;
  bool IsNodeShown(const plDocumentObject* pNode) const override;
  bool IsLayoutFlowPin(const plVisualGraphPin& pin) const override;

  // Editing

  void RemoveNodes(plArrayPtr<const plDocumentObject* const> nodes) override;
  void ConnectPins(const plVisualGraphPin& sourcePin, const plVisualGraphPin& targetPin) override;
  void DisconnectConnection(const plDocumentObject* pConnection) override;
  void DisconnectPin(const plVisualGraphPin& pin) override;
  bool CanInsertReroute(const plDocumentObject* pConnection) const override { return true; }
  void InsertReroute(const plDocumentObject* pConnection, const plVec2& vPosition) override;

  /// A reroute picked after dragging a wire takes the kind (execution or data) of the pin it came from.
  void GetNodeTemplates(plDynamicArray<plVisualGraphNodeDesc>& out_templates, plDeque<plVisualGraphNodeProperty>& ref_storage, const plVisualGraphPin* pFromPin = nullptr) const override;

  // Interaction

  void ExtendNodeMenu(const plDocumentObject* pNode, plVisualGraphMenu& ref_menu) override;
  void ExtendConnectionMenu(const plDocumentObject* pConnection, plVisualGraphMenu& ref_menu) override;
  bool OnKey(const plEditorUiKeyEvent& e) override;
  bool OnNodeDoubleClicked(const plDocumentObject* pNode) override;
  void GetBreadcrumbs(plDynamicArray<plString>& out_crumbs) const override;
  void OnBreadcrumbClicked(plUInt32 uiIndex) override;

  // plVisualScriptGraphHost

  const plUuid& GetCurrentScope() const override { return m_CurrentScope; }
  void NavigateToScope(const plUuid& scope) override;
  void NavigateToParentScope() override;
  plUuid AddFunction(plStringView sName) override;
  void RemoveFunction(const plUuid& functionGuid) override;
  void RenameFunction(const plUuid& functionGuid, plStringView sNewName) override;
  void CollapseSelectionToSubGraph() override;
  void ExpandSelectedSubGraph() override;

private:
  const plVisualScriptNodeManager* GetScriptManager() const;
  void NodeChangedHandler(const plDocumentObject* pObject);

  /// Breakpoints, the stopped node and compile problems change node looks.
  void ScriptToolsEventHandler(const plScriptToolsEvent& e);

  /// The graph document's asset GUID, as diagnostics and node breakpoints name it.
  plString GetAssetGuid() const;

  plEventSubscriptionID m_ScriptToolsSubscription = 0;

  /// The graph's live flow is recorded while this view shows it; the timer redraws once flowing wires have faded.
  plString m_sWatchedAsset;
  plEditorUiTimerGroup m_FlowTimers;
  plUInt32 m_uiFlowFadeTimer = 0;

  void GetSelectedNodesInScope(plDynamicArray<const plDocumentObject*>& out_nodes) const;
  plRectFloat GetBounds(plArrayPtr<const plDocumentObject* const> nodes) const;
  plColorGammaUB GetPinColor(const plVisualScriptPin& pin) const;

  const plEditorUiGraphView* m_pView = nullptr;
  plUuid m_CurrentScope;
};
