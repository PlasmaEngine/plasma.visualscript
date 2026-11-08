#pragma once

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptNodeRegistry.h>
#include <ToolsFoundation/VisualGraph/VisualGraphObjectManager.h>

struct plVisualScriptVariable;
struct plAssetCuratorEvent;
class plVisualScriptLibraryMetaData;

/// One entry of the sub-graph navigation path shown above the graph.
struct plVisualScriptBreadcrumbEntry
{
  plUuid m_Guid; ///< invalid = root graph
  plString m_sDisplayName;
};

/// Visual graph pin for visual script nodes.
///
/// Extends the base pin with visual scripting metadata including execution flow pins, data type information,
/// and support for type deduction. Pins can represent both execution flow and data connections.
class plVisualScriptPin : public plVisualGraphPin
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptPin, plVisualGraphPin);

public:
  plVisualScriptPin(Type type, plStringView sName, const plVisualScriptNodeRegistry::PinDesc& pinDesc, const plDocumentObject* pObject, plUInt32 uiDataPinIndex, plUInt32 uiElementIndex);
  ~plVisualScriptPin();

  PL_ALWAYS_INLINE bool IsExecutionPin() const { return m_pDesc->IsExecutionPin(); }
  PL_ALWAYS_INLINE bool IsDataPin() const { return m_pDesc->IsDataPin(); }

  PL_ALWAYS_INLINE const plRTTI* GetDataType() const { return m_pDesc->m_pDataType; }
  PL_ALWAYS_INLINE plVisualScriptDataType::Enum GetScriptDataType() const { return m_pDesc->m_ScriptDataType; }
  plVisualScriptDataType::Enum GetResolvedScriptDataType() const;
  plStringView GetDataTypeName() const;
  PL_ALWAYS_INLINE plUInt32 GetDataPinIndex() const { return m_uiDataPinIndex; }
  PL_ALWAYS_INLINE plUInt32 GetElementIndex() const { return m_uiElementIndex; }
  PL_ALWAYS_INLINE bool IsRequired() const { return m_pDesc->m_bRequired; }
  PL_ALWAYS_INLINE bool HasDynamicPinProperty() const { return m_pDesc->m_sDynamicPinProperty.IsEmpty() == false; }
  PL_ALWAYS_INLINE bool SplitExecution() const { return m_pDesc->m_bSplitExecution; }
  PL_ALWAYS_INLINE bool ReplaceWithArray() const { return m_pDesc->m_bReplaceWithArray; }
  PL_ALWAYS_INLINE bool NeedsTypeDeduction() const { return m_pDesc->m_DeductTypeFunc != nullptr; }

  PL_ALWAYS_INLINE const plHashedString& GetDynamicPinProperty() const { return m_pDesc->m_sDynamicPinProperty; }
  PL_ALWAYS_INLINE plVisualScriptNodeRegistry::PinDesc::DeductTypeFunc GetDeductTypeFunc() const { return m_pDesc->m_DeductTypeFunc; }

  bool CanConvertTo(const plVisualScriptPin& targetPin, bool bUseResolvedDataTypes = true) const;

private:
  const plVisualScriptNodeRegistry::PinDesc* m_pDesc = nullptr;
  plUInt32 m_uiDataPinIndex = 0;
  plUInt32 m_uiElementIndex = 0;
};

/// Object manager for visual script graphs.
///
/// Manages visual script nodes and their connections, including both execution flow and data flow.
/// Handles complex features such as type deduction, dynamic pin creation, variable management,
/// and coroutine detection. Validates connections based on script data types and execution flow rules.
class plVisualScriptNodeManager : public plVisualGraphObjectManager
{
public:
  plVisualScriptNodeManager();
  ~plVisualScriptNodeManager();

  plHashedString GetScriptBaseClass() const;
  bool IsFilteredByBaseClass(const plRTTI* pNodeType, const plVisualScriptNodeRegistry::NodeDesc& nodeDesc, const plHashedString& sBaseClass, bool bLogWarning = false) const;

  plVisualScriptDataType::Enum GetVariableType(plTempHashedString sName) const;
  plResult GetVariable(plTempHashedString sName, plVisualScriptVariable& out_variable) const;
  void GetAllVariables(plDynamicArray<plVisualScriptVariable>& out_variables) const;

  void GetInputExecutionPins(const plDocumentObject* pObject, plDynamicArray<const plVisualScriptPin*>& out_pins) const;
  void GetOutputExecutionPins(const plDocumentObject* pObject, plDynamicArray<const plVisualScriptPin*>& out_pins) const;

  void GetInputDataPins(const plDocumentObject* pObject, plDynamicArray<const plVisualScriptPin*>& out_pins) const;
  void GetOutputDataPins(const plDocumentObject* pObject, plDynamicArray<const plVisualScriptPin*>& out_pins) const;

  void GetEntryNodes(const plDocumentObject* pObject, plDynamicArray<const plDocumentObject*>& out_entryNodes) const;

  static plStringView GetNiceTypeName(const plDocumentObject* pObject);
  static plStringView GetNiceFunctionName(const plDocumentObject* pObject);

  plVisualScriptDataType::Enum GetDeductedType(const plVisualScriptPin& pin) const;
  plVisualScriptDataType::Enum GetDeductedType(const plDocumentObject* pObject) const;

  /// Node palette ranking: the same data type (or a class the target derives from) is exact; pins that infer their
  /// type and numeric or vector conversions convert; String, Variant and pointer-to-bool targets accept anything.
  virtual plUInt8 ScorePinMatch(const plVisualGraphPin& fromPin, const plVisualGraphPin& candidate) const override;
  virtual plString GetPinTypeName(const plVisualGraphPin& pin) const override;

  bool IsCoroutine(const plDocumentObject* pObject) const;
  bool IsLoop(const plDocumentObject* pObject) const;

  plEvent<const plDocumentObject*> m_NodeChangedEvent;

  // Node kinds ////////////////////////////////////////////////////////////

  plVisualScriptNodeDescription::Type::Enum GetNodeType(const plDocumentObject* pObject) const;
  bool IsTransparentNode(const plDocumentObject* pObject) const;
  bool IsReroute(const plDocumentObject* pObject) const;
  bool IsPortalIn(const plDocumentObject* pObject) const;
  bool IsPortalOut(const plDocumentObject* pObject) const;
  bool IsExecPortal(const plDocumentObject* pObject) const;
  bool IsSubGraph(const plDocumentObject* pObject) const;
  bool IsSubGraphInputs(const plDocumentObject* pObject) const;
  bool IsSubGraphOutputs(const plDocumentObject* pObject) const;
  bool IsSubGraphBoundary(const plDocumentObject* pObject) const { return IsSubGraphInputs(pObject) || IsSubGraphOutputs(pObject); }
  bool IsFunctionEntry(const plDocumentObject* pObject) const;
  bool IsReturn(const plDocumentObject* pObject) const;
  bool IsCall(const plDocumentObject* pObject) const;
  bool IsCallLibrary(const plDocumentObject* pObject) const;
  bool IsEntryNode(const plDocumentObject* pObject) const;

  // Scopes: sub-graphs and function graphs /////////////////////////////////
  //
  // A scope is identified by the guid of its owner node. Sub-graph nodes own the scope of their contents and live in
  // their parent's scope themselves. Function entries own the scope of the function body and are the root of that
  // graph: they are shown inside it, never in the event graph. The event graph is the invalid scope.

  /// \brief The scope this node is stored in; invalid for the event graph, comments and connections.
  plUuid GetNodeScope(const plDocumentObject* pObject) const;
  /// \brief The scope this node is shown in: a function entry is shown inside its own graph, everything else where it is stored.
  plUuid GetDisplayScope(const plDocumentObject* pObject) const;
  void GetNodesInScope(const plUuid& scope, plDynamicArray<const plDocumentObject*>& out_nodes, bool bRecursive) const;
  const plDocumentObject* GetSubGraphBoundary(const plDocumentObject* pSubGraph, bool bInputs) const;
  const plDocumentObject* GetSubGraphOwner(const plDocumentObject* pBoundary) const;

  bool IsScopeOwner(const plDocumentObject* pObject) const { return IsSubGraph(pObject) || IsFunctionEntry(pObject); }
  /// \brief The scope one level up; the event graph and function graphs have none (returns false).
  bool GetParentScope(const plUuid& scope, plUuid& out_parent) const;
  /// \brief The function entry whose graph contains \a scope (directly or through sub-graphs), nullptr for the event graph.
  const plDocumentObject* GetScopeFunction(const plUuid& scope) const;
  /// \brief The name of the event graph, editable through the document properties.
  plString GetRootGraphName() const;

  void SetCurrentViewScope(const plUuid& scope) { m_CurrentViewScope = scope; }
  const plUuid& GetCurrentViewScope() const { return m_CurrentViewScope; }
  void GetBreadcrumbPath(plDynamicArray<plVisualScriptBreadcrumbEntry>& out_path) const;

  /// \brief Documents saved before functions had their own graphs keep function bodies in the event graph; moves them into the function's scope.
  void MigrateFunctionScopes();

  // Logical graph walking (through reroutes, portals and sub-graph boundaries) ////

  /// \brief The real input pin an output execution pin flows into, or nullptr when the chain ends unconnected. Fails on a broken portal or a cycle.
  plResult ResolveExecTarget(const plVisualScriptPin& outputPin, const plVisualScriptPin*& out_pTargetPin) const;
  /// \brief The real output pin that feeds an input data pin, or nullptr when the chain ends unconnected.
  plResult ResolveDataSource(const plVisualScriptPin& inputPin, const plVisualScriptPin*& out_pSourcePin) const;

  /// \brief Real input pins reached from an output pin, following transparent nodes. \a pIgnoredConnection is treated as already removed.
  void GetLogicalTargets(const plVisualScriptPin& outputPin, plDynamicArray<const plVisualScriptPin*>& out_targets, const plVisualGraphConnection* pIgnoredConnection = nullptr) const;
  /// \brief Real output pins feeding an input pin, following transparent nodes.
  void GetLogicalSources(const plVisualScriptPin& inputPin, plDynamicArray<const plVisualScriptPin*>& out_sources) const;
  /// \brief True when \a pTarget can be reached from \a pSource through any chain of connections (execution or data).
  bool CanReachNodeLogically(const plDocumentObject* pSource, const plDocumentObject* pTarget) const;

  // Functions, portals, libraries /////////////////////////////////////////

  plString GetPortalName(const plDocumentObject* pPortal) const;
  const plDocumentObject* FindPortalIn(plStringView sName, bool bExec) const;
  void GetPortalOuts(plStringView sName, bool bExec, plDynamicArray<const plDocumentObject*>& out_portals) const;
  const plDocumentObject* FindFunctionEntry(plStringView sName) const;
  void GetFunctionEntries(plDynamicArray<const plDocumentObject*>& out_entries) const;
  /// \brief The 'Name' property of a function entry (or any other node that has one).
  plString GetFunctionName(const plDocumentObject* pEntry) const;
  const plDocumentObject* FindReturnOwner(const plDocumentObject* pReturn) const;
  const plVisualScriptLibraryMetaData* GetLibraryMetaData(plStringView sLibrary) const;

  /// \brief Nodes whose pins mirror a declaration on \a pObject.\a sProperty and must be rebuilt when it changes.
  void GetMirroredPinDependents(const plDocumentObject* pObject, plStringView sProperty, plDynamicArray<const plDocumentObject*>& out_dependents) const;
  void RecreatePins(const plDocumentObject* pObject) { TryRecreatePins(pObject); }
  void RecomputeAllCoroutines();

  /// \brief Refills the global dynamic string enums (portal names, function names, library functions) from this document.
  void RefreshDynamicEnums(const plDocumentObject* pSelectedNode = nullptr) const;

  // Validation ////////////////////////////////////////////////////////////

  plStatus ValidateGraph() const;
  bool GetNodeError(const plDocumentObject* pObject, plStringBuilder& out_sError) const;
  bool IsPinRequired(const plVisualScriptPin& pin) const;

  /// Set by the scene while a connection is dragged onto empty space so a newly created reroute gets the right kind.
  struct CreationHint
  {
    bool m_bValid = false;
    bool m_bExec = false;
  };
  CreationHint m_CreationHint;

  /// Library documents hold only functions: no base class, no variables, no event handlers, explicit targets required.
  bool m_bIsLibrary = false;

private:
  virtual bool InternalIsNode(const plDocumentObject* pObject) const override;
  virtual bool InternalIsDynamicPinProperty(const plDocumentObject* pObject, const plAbstractProperty* pProp) const override;
  virtual plStatus InternalCanConnect(const plVisualGraphPin& source, const plVisualGraphPin& target, CanConnectResult& out_Result) const override;

  virtual void InternalCreatePins(const plDocumentObject* pObject, NodeInternal& node) override;

  virtual void GetNodeCreationTemplates(plDynamicArray<plVisualGraphNodeDesc>& out_templates) const override;

  void NodeEventsHandler(const plVisualGraphObjectManagerEvent& e);
  void PropertyEventsHandler(const plDocumentObjectPropertyEvent& e);
  void StructureEventsHandler(const plDocumentObjectStructureEvent& e);
  void AssetCuratorEventHandler(const plAssetCuratorEvent& e);

  friend class plVisualScriptPin;
  void RemoveDeductedPinType(const plVisualScriptPin& pin);
  void DeductNodeTypeAndAllPinTypes(const plDocumentObject* pObject, const plVisualGraphPin* pDisconnectedPin = nullptr);
  void UpdateCoroutine(const plDocumentObject* pTargetNode, const plVisualGraphConnection& changedConnection, bool bIsAboutToDisconnect = false);
  bool IsConnectedToCoroutine(const plDocumentObject* pEntryNode, const plVisualGraphConnection* pIgnoredConnection) const;

  bool CreateMirroredPins(const plDocumentObject* pObject, const plVisualScriptNodeRegistry::NodeDesc& nodeDesc, NodeInternal& ref_node);
  void AddDeclaredPins(const plDocumentObject* pDeclOwner, plStringView sArrayProperty, plVisualGraphPin::Type type, const plDocumentObject* pPinOwner, plDynamicArray<plUniquePtr<plVisualGraphPin>>& out_pins, plUInt32& inout_uiDataPinIndex);
  void AddLibraryPins(const plDocumentObject* pObject, NodeInternal& ref_node);

  void ForwardThroughTransparent(const plVisualScriptPin& inputPin, plDynamicArray<const plVisualScriptPin*>& out_targets, plHashSet<const plVisualScriptPin*>& inout_visited, const plVisualGraphConnection* pIgnoredConnection) const;
  void BackwardThroughTransparent(const plVisualScriptPin& outputPin, plDynamicArray<const plVisualScriptPin*>& out_sources, plHashSet<const plVisualScriptPin*>& inout_visited) const;

  plHashTable<const plDocumentObject*, plEnum<plVisualScriptDataType>> m_ObjectToDeductedType;
  plHashTable<const plVisualScriptPin*, plEnum<plVisualScriptDataType>> m_PinToDeductedType;
  plHashSet<const plDocumentObject*> m_CoroutineObjects;

  plUuid m_CurrentViewScope;

  mutable plDynamicArray<plVisualGraphNodeProperty> m_PropertyValues;
  mutable plDeque<plString> m_VariableNodeTypeNames;
  mutable plDeque<plHashedString> m_TemplateCategories;
};
