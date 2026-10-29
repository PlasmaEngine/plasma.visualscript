#pragma once

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraph.h>

class plVisualScriptCompiler
{
public:
  plVisualScriptCompiler(plVisualScriptNodeManager& ref_nodeManager);
  ~plVisualScriptCompiler();

  void InitModule(plStringView sBaseClassName, plStringView sScriptClassName);

  /// The asset the debug table names, so runtime errors and breakpoints find their graph.
  void SetDebugAsset(plStringView sAsset) { m_Module.m_sDebugAsset = sAsset; }

  plResult AddFunction(plStringView sName, const plDocumentObject* pEntryObject, const plDocumentObject* pParentObject = nullptr);

  plResult Compile(plStringView sDebugAstOutputPath = plStringView());

  /// The graph node the compiler is working on in this thread, or null between nodes.
  /// Errors logged while compiling belong to this node.
  static const plDocumentObject* GetCurrentNode();

  struct CompiledFunction
  {
    plString m_sName;
    plEnum<plVisualScriptNodeDescription::Type> m_Type;
    plEnum<plScriptCoroutineCreationMode> m_CoroutineCreationMode;
    plDynamicArray<plVisualScriptNodeDescription> m_NodeDescriptions;
    plDynamicArray<plUuid> m_NodeGuids; ///< the graph node of each node description; invalid for nodes the compiler added
    plVisualScriptDataDescription m_LocalDataDesc;
  };

  struct CompiledModule
  {
    CompiledModule();

    plResult Serialize(plStreamWriter& inout_stream) const;

    plString m_sBaseClassName;
    plString m_sScriptClassName;
    plString m_sDebugAsset;
    plHybridArray<CompiledFunction, 16> m_Functions;

    plVisualScriptDataDescription m_InstanceDataDesc;
    plVisualScriptInstanceDataMapping m_InstanceDataMapping;

    plVisualScriptDataDescription m_ConstantDataDesc;
    plVisualScriptDataStorage m_ConstantDataStorage;
    plHashTable<plVariant, plUInt32> m_ConstantDataToIndex;
  };

  const CompiledModule& GetCompiledModule() const { return m_Module; }

  using DataOffset = plVisualScriptDataDescription::DataOffset;
  struct AstNode;

  struct ExecInput
  {
    PL_DECLARE_POD_TYPE();

    AstNode* m_pSourceNode = nullptr;
    plUInt32 m_uiSourcePinIndex = 0;
#if PL_ENABLED(PL_PLATFORM_64BIT)
    plUInt32 m_uiPadding = 0;
#endif
  };

  struct ExecOutput
  {
    PL_DECLARE_POD_TYPE();

    AstNode* m_pTargetNode = nullptr;
  };

  struct DataInput
  {
    PL_DECLARE_POD_TYPE();

    AstNode* m_pSourceNode = nullptr;
    plUInt32 m_uiSourcePinIndex = 0;
    DataOffset m_DataOffset;

    PL_ALWAYS_INLINE bool IsConnected() const { return m_pSourceNode != nullptr; }
    PL_ALWAYS_INLINE bool IsConnectedAndLocal() const { return IsConnected() && m_DataOffset.GetSource() == DataOffset::Source::Local; }
  };

  struct DataOutput
  {
    PL_DECLARE_POD_TYPE();

    DataOffset m_DataOffset;

    PL_ALWAYS_INLINE bool IsValid() const { return m_DataOffset.IsValid(); }
    PL_ALWAYS_INLINE bool IsValidAndLocal() const { return IsValid() && m_DataOffset.GetSource() == DataOffset::Source::Local; }
  };

  struct AstNode
  {
    const plDocumentObject* m_pObject = nullptr;

    plEnum<plVisualScriptNodeDescription::Type> m_Type;
    plEnum<plVisualScriptDataType> m_DeductedDataType;
    bool m_bImplicitExecution = false;
    bool m_bIsCoroutineCall = false; ///< Builtin_Call / Builtin_CallLibrary of a coroutine function: makes the enclosing entry a coroutine

    plHashedString m_sTargetTypeName;
    plVariant m_Value;

    plSmallArray<ExecInput, 2> m_ExecInputs;
    plSmallArray<ExecOutput, 2> m_ExecOutputs;
    plSmallArray<DataInput, 7> m_DataInputs;
    plSmallArray<DataOutput, 4> m_DataOutputs;
  };

#if PL_ENABLED(PL_PLATFORM_64BIT)
  static_assert(sizeof(AstNode) == 256);
#endif

private:
  PL_ALWAYS_INLINE static plStringView GetNiceTypeName(const plDocumentObject* pObject)
  {
    return plVisualScriptNodeManager::GetNiceTypeName(pObject);
  }

  // Ast node creation
  AstNode& CreateAstNode(plVisualScriptNodeDescription::Type::Enum type, plVisualScriptDataType::Enum deductedDataType = plVisualScriptDataType::Invalid, bool bImplicitExecution = false);
  PL_ALWAYS_INLINE AstNode& CreateAstNode(plVisualScriptNodeDescription::Type::Enum type, bool bImplicitExecution)
  {
    return CreateAstNode(type, plVisualScriptDataType::Invalid, bImplicitExecution);
  }

  AstNode& CreateJumpNode(AstNode* pTargetNode);
  AstNode* CreateAstNodeFromObject(const plDocumentObject* pObject, const plVisualScriptNodeRegistry::NodeDesc* pNodeDesc, const plDocumentObject* pEntryObject, bool bImplicitOnly = false);
  DataInput GetOrCreateDefaultPointerNode(const AstNode& node, const plRTTI* pRtti);

  void MarkAsCoroutine(AstNode* pEntryAstNode);

  // Pins, inputs and outputs
  void AddConstantDataInput(AstNode& node, const plVariant& value);
  plResult AddConstantDataInput(AstNode& node, const plDocumentObject* pObject, const plVisualScriptPin* pPin, plVisualScriptDataType::Enum dataType);
  void AddDataInput(AstNode& node, AstNode* pSourceNode, plUInt32 uiSourcePinIndex, plVisualScriptDataType::Enum dataType);
  void AddDataOutput(AstNode& node, plVisualScriptDataType::Enum dataType);
  DataOutput& GetDataOutputFromInput(const DataInput& dataInput);

  void ConnectExecution(AstNode& sourceNode, AstNode& targetNode, plUInt32 uiSourcePinIndex = plInvalidIndex);
  void DisconnectExecution(AstNode& sourceNode, AstNode& targetNode, plUInt32 uiSourcePinIndex);
  void ExecuteBefore(AstNode& node, AstNode& firstNewNode, AstNode& lastNewNode);
  void ExecuteAfter(AstNode& node, AstNode& firstNewNode, AstNode& lastNewNode);
  void ReplaceExecution(AstNode& oldNode, AstNode& newNode);

  DataOffset GetInstanceDataOffset(plHashedString sName, plVisualScriptDataType::Enum dataType);

  // Compilation steps
  plResult BuildInstanceDataMapping();
  AstNode* BuildExecutionFlow(const plDocumentObject* pEntryObject);

  plResult BuildDataStack(AstNode* pEntryAstNode, AstNode*& out_pFirstDataNode, AstNode*& out_pLastDataNode);
  plResult BuildDataExecutions(AstNode* pEntryAstNode);

  plResult InsertTypeConversions(AstNode* pEntryAstNode);

  plResult ReplaceLoop(AstNode* pEntryAstNode);
  plResult ReplaceUnsupportedNodes(AstNode* pEntryAstNode);

  plResult AssignInstanceVariables(AstNode* pEntryAstNode);
  plResult AssignLocalVariables(AstNode* pEntryAstNode, plVisualScriptDataDescription& inout_localDataDesc);
  plResult CopyOutputsToInputs(AstNode* pEntryAstNode);

  /// Functions always end in a Builtin_Return: every dangling execution output gets one that yields the declared default outputs.
  plResult InsertImplicitReturn(AstNode* pEntryAstNode, const plDocumentObject* pEntryObject);

public:
  /// True when the function body (transitively, through calls) contains a node that suspends execution.
  bool IsCoroutineFunction(plStringView sFunctionName);

private:
  bool IsCoroutineEntry(const plDocumentObject* pEntryObject, plHashSet<const plDocumentObject*>& inout_visitedEntries);

  plResult BuildNodeDescriptions(AstNode* pEntryAstNode, plDynamicArray<plVisualScriptNodeDescription>& out_NodeDescriptions, plDynamicArray<plUuid>& out_NodeGuids);

  plResult FinalizeConstantData();

  enum class VisitorResult
  {
    Continue,
    Skip,
    Error,
  };

  // Does allow modifications to the AST structure while iterating
  plResult TraverseAstDepthFirst(AstNode* pEntryAstNode, plDelegate<VisitorResult(AstNode*& pAstNode)> func);

  // Does NOT allow modifications to the AST structure while iterating
  plResult TraverseAstTopologicalOrder(const AstNode* pEntryAstNode, plDelegate<VisitorResult(const AstNode* pAstNode)> func);

  void DumpAST(AstNode* pEntryAstNode, plStringView sOutputPath, plStringView sFunctionName, plStringView sSuffix);
  void DumpGraph(plArrayPtr<const plVisualScriptNodeDescription> nodeDescriptions, plStringView sOutputPath, plStringView sFunctionName, plStringView sSuffix);

  plVisualScriptNodeManager& m_NodeManager;

  plDeque<AstNode> m_AstNodes;
  plHybridArray<const plDocumentObject*, 8> m_EntryObjects;

  struct LiveLocalVar
  {
    PL_DECLARE_POD_TYPE();

    plUInt32 m_uiId = plInvalidIndex;
    DataOffset m_DataOffset;

    plUInt32 m_uiStart = plInvalidIndex;
    plUInt32 m_uiEnd = 0;
  };

  struct CompilationState
  {
    plHashTable<const plDocumentObject*, AstNode*> m_ExecObjectToAstNode;
    plHashTable<const plDocumentObject*, AstNode*> m_DataObjectToAstNode;

    plHashSet<const AstNode*> m_VisitedNodes;

    plDynamicArray<LiveLocalVar> m_LiveLocalVars;
    plUInt32 m_uiNextLocalVarId = 0;

    AstNode* m_pGetScriptOwnerNode = nullptr;

    void Clear()
    {
      m_ExecObjectToAstNode.Clear();
      m_DataObjectToAstNode.Clear();

      m_VisitedNodes.Clear();

      m_LiveLocalVars.Clear();
      m_uiNextLocalVarId = 0;

      m_pGetScriptOwnerNode = nullptr;
    }
  };

  CompilationState m_CompilationState;

  plHashTable<plString, bool> m_CoroutineFunctionCache;

  CompiledModule m_Module;
};
