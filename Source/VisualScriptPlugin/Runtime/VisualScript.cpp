#include <VisualScriptPlugin/VisualScriptPluginPCH.h>

#include <Core/Scripting/ScriptWorldModule.h>
#include <Foundation/Configuration/CVar.h>
#include <Foundation/IO/StringDeduplicationContext.h>
#include <Core/Scripting/ScriptDebugger.h>
#include <Core/Scripting/ScriptFlow.h>
#include <Foundation/Types/ScopeExit.h>
#include <VisualScriptPlugin/Runtime/VisualScriptDebugBackend.h>
#include <VisualScriptPlugin/Runtime/VisualScriptInstance.h>
#include <VisualScriptPlugin/Runtime/VisualScriptNodeUserData.h>

plVisualScriptGraphDescription::ExecuteFunction GetExecuteFunction(plVisualScriptNodeDescription::Type::Enum nodeType, plVisualScriptDataType::Enum dataType);

namespace
{
  static const char* s_NodeDescTypeNames[] = {
    "", // Invalid,
    "EntryCall",
    "EntryCall_Coroutine",
    "MessageHandler",
    "MessageHandler_Coroutine",
    "FunctionEntry",
    "FunctionEntry_Coroutine",
    "ReflectedFunction",
    "GetReflectedProperty",
    "SetReflectedProperty",
    "InplaceCoroutine",
    "GetScriptOwner",
    "SendMessage",

    "", // FirstBuiltin,

    "Builtin_Constant",
    "Builtin_GetVariable",
    "Builtin_SetVariable",
    "Builtin_IncVariable",
    "Builtin_DecVariable",
    "Builtin_TempVariable",

    "Builtin_Branch",
    "Builtin_Switch",
    "Builtin_WhileLoop",
    "Builtin_ForLoop",
    "Builtin_ForEachLoop",
    "Builtin_ReverseForEachLoop",
    "Builtin_Break",
    "Builtin_Jump",

    "Builtin_And",
    "Builtin_Or",
    "Builtin_Not",
    "Builtin_Compare",
    "Builtin_CompareExec",
    "Builtin_IsValid",
    "Builtin_Select",

    "Builtin_Add",
    "Builtin_Subtract",
    "Builtin_Multiply",
    "Builtin_Divide",
    "Builtin_Expression",

    "Builtin_ToBool",
    "Builtin_ToByte",
    "Builtin_ToInt",
    "Builtin_ToInt64",
    "Builtin_ToFloat",
    "Builtin_ToDouble",
    "Builtin_ToString",
    "Builtin_String_Format",
    "Builtin_ToHashedString",
    "Builtin_ToVariant",
    "Builtin_Variant_ConvertTo",

    "Builtin_MakeArray",
    "Builtin_Array_GetElement",
    "Builtin_Array_SetElement",
    "Builtin_Array_GetCount",
    "Builtin_Array_IsEmpty",
    "Builtin_Array_Clear",
    "Builtin_Array_Contains",
    "Builtin_Array_IndexOf",
    "Builtin_Array_Insert",
    "Builtin_Array_PushBack",
    "Builtin_Array_PushBackRange",
    "Builtin_Array_Remove",
    "Builtin_Array_RemoveAt",

    "Builtin_TryGetComponentOfBaseType",

    "Builtin_StartCoroutine",
    "Builtin_StopCoroutine",
    "Builtin_StopAllCoroutines",
    "Builtin_WaitForAll",
    "Builtin_WaitForAny",
    "Builtin_Yield",

    "Builtin_Call",
    "Builtin_CallLibrary",
    "Builtin_Return",

    "Builtin_Reroute",
    "Builtin_PortalIn",
    "Builtin_PortalOut",
    "Builtin_SubGraph",
    "Builtin_SubGraphInputs",
    "Builtin_SubGraphOutputs",

    "", // LastBuiltin,
  };
  static_assert(PL_ARRAY_SIZE(s_NodeDescTypeNames) == (size_t)plVisualScriptNodeDescription::Type::Count);

  template <typename T>
  plResult WriteNodeArray(plArrayPtr<T> a, plStreamWriter& inout_stream)
  {
    plUInt16 uiCount = static_cast<plUInt16>(a.GetCount());
    inout_stream << uiCount;

    return inout_stream.WriteBytes(a.GetPtr(), a.GetCount() * sizeof(T));
  }

} // namespace

// static
plVisualScriptNodeDescription::Type::Enum plVisualScriptNodeDescription::Type::GetConversionType(plVisualScriptDataType::Enum targetDataType)
{
  static_assert(Builtin_ToBool + (plVisualScriptDataType::Bool - plVisualScriptDataType::Bool) == Builtin_ToBool);
  static_assert(Builtin_ToBool + (plVisualScriptDataType::Byte - plVisualScriptDataType::Bool) == Builtin_ToByte);
  static_assert(Builtin_ToBool + (plVisualScriptDataType::Int - plVisualScriptDataType::Bool) == Builtin_ToInt);
  static_assert(Builtin_ToBool + (plVisualScriptDataType::Int64 - plVisualScriptDataType::Bool) == Builtin_ToInt64);
  static_assert(Builtin_ToBool + (plVisualScriptDataType::Float - plVisualScriptDataType::Bool) == Builtin_ToFloat);
  static_assert(Builtin_ToBool + (plVisualScriptDataType::Double - plVisualScriptDataType::Bool) == Builtin_ToDouble);

  if (plVisualScriptDataType::IsNumberOrBool(targetDataType))
    return static_cast<Enum>(Builtin_ToBool + (targetDataType - plVisualScriptDataType::Bool));

  if (targetDataType == plVisualScriptDataType::String)
    return Builtin_ToString;

  if (targetDataType == plVisualScriptDataType::HashedString)
    return Builtin_ToHashedString;

  if (targetDataType == plVisualScriptDataType::Variant)
    return Builtin_ToVariant;

  PL_ASSERT_NOT_IMPLEMENTED;
  return Invalid;
}

// static
const char* plVisualScriptNodeDescription::Type::GetName(Enum type)
{
  PL_ASSERT_DEBUG(type >= 0 && static_cast<plUInt32>(type) < PL_ARRAY_SIZE(s_NodeDescTypeNames), "Out of bounds access");
  return s_NodeDescTypeNames[type];
}

void plVisualScriptNodeDescription::AppendUserDataName(plStringBuilder& out_sResult) const
{
  if (auto func = GetUserDataContext(m_Type).m_ToStringFunc)
  {
    out_sResult.Append(" ");

    func(*this, out_sResult);
  }
}

//////////////////////////////////////////////////////////////////////////

plVisualScriptGraphDescription::plVisualScriptGraphDescription()
{
  static_assert(sizeof(Node) == 64);
}

plVisualScriptGraphDescription::~plVisualScriptGraphDescription() = default;

static const plTypeVersion s_uiVisualScriptGraphDescriptionVersion = 8;

// static
plResult plVisualScriptGraphDescription::Serialize(plArrayPtr<const plVisualScriptNodeDescription> nodes, const plVisualScriptDataDescription& localDataDesc, plStreamWriter& inout_stream)
{
  inout_stream.WriteVersion(s_uiVisualScriptGraphDescriptionVersion);

  PL_SUCCEED_OR_RETURN(localDataDesc.Serialize(inout_stream));

  plDefaultMemoryStreamStorage streamStorage;
  plMemoryStreamWriter stream(&streamStorage);
  plUInt32 additionalDataSize = 0;
  {
    for (auto& nodeDesc : nodes)
    {
      stream << nodeDesc.m_Type;
      stream << nodeDesc.m_DeductedDataType;
      PL_SUCCEED_OR_RETURN(WriteNodeArray(nodeDesc.m_ExecutionIndices.GetArrayPtr(), stream));
      PL_SUCCEED_OR_RETURN(WriteNodeArray(nodeDesc.m_InputDataOffsets.GetArrayPtr(), stream));
      PL_SUCCEED_OR_RETURN(WriteNodeArray(nodeDesc.m_OutputDataOffsets.GetArrayPtr(), stream));

      ExecutionIndicesArray::AddAdditionalDataSize(nodeDesc.m_ExecutionIndices, additionalDataSize);
      InputDataOffsetsArray::AddAdditionalDataSize(nodeDesc.m_InputDataOffsets, additionalDataSize);
      OutputDataOffsetsArray::AddAdditionalDataSize(nodeDesc.m_OutputDataOffsets, additionalDataSize);

      if (auto func = GetUserDataContext(nodeDesc.m_Type).m_SerializeFunc)
      {
        plUInt32 uiSize = 0;
        plUInt32 uiAlignment = 0;
        PL_SUCCEED_OR_RETURN(func(nodeDesc, stream, uiSize, uiAlignment));

        UserDataArray::AddAdditionalDataSize(uiSize, uiAlignment, additionalDataSize);
      }
    }
  }

  const plUInt32 uiRequiredStorageSize = nodes.GetCount() * sizeof(Node) + additionalDataSize;
  inout_stream << uiRequiredStorageSize;
  inout_stream << nodes.GetCount();

  PL_SUCCEED_OR_RETURN(streamStorage.CopyToStream(inout_stream));

  return PL_SUCCESS;
}

plResult plVisualScriptGraphDescription::Deserialize(plStreamReader& inout_stream, const plVisualScriptDataDescription& instanceDataDesc, const plVisualScriptDataDescription& constantDataDesc)
{
  plTypeVersion uiVersion = inout_stream.ReadVersion(s_uiVisualScriptGraphDescriptionVersion);
  if (uiVersion < s_uiVisualScriptGraphDescriptionVersion)
  {
    plLog::Error("Invalid visual script desc version. Expected >= {} but got {}. Visual Script needs re-export", s_uiVisualScriptGraphDescriptionVersion, uiVersion);
    return PL_FAILURE;
  }

  {
    plSharedPtr<plVisualScriptDataDescription> pLocalDataDesc = PL_SCRIPT_NEW(plVisualScriptDataDescription);
    PL_SUCCEED_OR_RETURN(pLocalDataDesc->Deserialize(inout_stream));
    m_pLocalDataDesc = std::move(pLocalDataDesc);
  }

  {
    plUInt32 uiStorageSize;
    inout_stream >> uiStorageSize;

    m_Storage.SetCountUninitialized(uiStorageSize);
    m_Storage.ZeroFill();
  }

  plUInt32 uiNumNodes;
  inout_stream >> uiNumNodes;

  auto pData = m_Storage.GetByteBlobPtr().GetPtr();
  auto nodes = plMakeArrayPtr(reinterpret_cast<Node*>(pData), uiNumNodes);

  plUInt8* pAdditionalData = pData + uiNumNodes * sizeof(Node);

  auto GetDataDesc = [&](DataOffset dataOffset) -> const plVisualScriptDataDescription*
  {
    switch (dataOffset.GetSource())
    {
      case DataOffset::Source::Local:
        return m_pLocalDataDesc.Borrow();
      case DataOffset::Source::Instance:
        return &instanceDataDesc;
      case DataOffset::Source::Constant:
        return &constantDataDesc;
        PL_DEFAULT_CASE_NOT_IMPLEMENTED;
    }

    return nullptr;
  };

  auto CalculateDataOffsets = [&](DataOffset* pDataOffsets, plUInt32 uiNumDataOffsets)
  {
    DataOffset* pDataOffsetsEnd = pDataOffsets + uiNumDataOffsets;
    while (pDataOffsets < pDataOffsetsEnd)
    {
      auto& dataOffset = *pDataOffsets;
      dataOffset = GetDataDesc(dataOffset)->GetOffset(dataOffset.GetType(), dataOffset.m_uiByteOffset, dataOffset.GetSource());
      ++pDataOffsets;
    }
  };

  for (auto& node : nodes)
  {
    inout_stream >> node.m_Type;
    inout_stream >> node.m_DeductedDataType;

    node.m_Function = GetExecuteFunction(node.m_Type, node.m_DeductedDataType);

    PL_SUCCEED_OR_RETURN(node.m_ExecutionIndices.ReadFromStream(node.m_NumExecutionIndices, inout_stream, pAdditionalData));
    PL_SUCCEED_OR_RETURN(node.m_InputDataOffsets.ReadFromStream(node.m_NumInputDataOffsets, inout_stream, pAdditionalData));
    PL_SUCCEED_OR_RETURN(node.m_OutputDataOffsets.ReadFromStream(node.m_NumOutputDataOffsets, inout_stream, pAdditionalData));

    CalculateDataOffsets(node.GetInputDataOffsets(), node.m_NumInputDataOffsets);
    CalculateDataOffsets(node.GetOutputDataOffsets(), node.m_NumOutputDataOffsets);

    if (auto func = GetUserDataContext(node.m_Type).m_DeserializeFunc)
    {
      PL_SUCCEED_OR_RETURN(func(node, inout_stream, pAdditionalData));
    }
  }

  m_Nodes = nodes;

  return PL_SUCCESS;
}

plUInt32 plVisualScriptGraphDescription::GetNumEntryArguments() const
{
  auto pEntryNode = GetNode(0);
  return pEntryNode != nullptr ? pEntryNode->m_NumOutputDataOffsets : 0;
}

plResult plVisualScriptGraphDescription::PatchCallSites(plDelegate<plResult(Node& ref_node)> func)
{
  // The nodes live in m_Storage which is owned by this description, so writing to them is legal here.
  Node* pNodes = const_cast<Node*>(m_Nodes.GetPtr());
  for (plUInt32 i = 0; i < m_Nodes.GetCount(); ++i)
  {
    Node& node = pNodes[i];
    if (plVisualScriptNodeDescription::Type::IsCall(node.m_Type))
    {
      PL_SUCCEED_OR_RETURN(func(node));
    }
  }

  return PL_SUCCESS;
}

plScriptMessageDesc plVisualScriptGraphDescription::GetMessageDesc() const
{
  auto pEntryNode = GetNode(0);
  PL_ASSERT_DEBUG(pEntryNode != nullptr &&
                    (pEntryNode->m_Type == plVisualScriptNodeDescription::Type::MessageHandler ||
                      pEntryNode->m_Type == plVisualScriptNodeDescription::Type::MessageHandler_Coroutine ||
                      pEntryNode->m_Type == plVisualScriptNodeDescription::Type::SendMessage),
    "Entry node is invalid or not a message handler");

  auto& userData = pEntryNode->GetUserData<NodeUserData_TypeAndProperties>();

  plScriptMessageDesc desc;
  desc.m_pType = userData.m_pType;
  desc.m_Properties = plMakeArrayPtr(userData.m_Properties, userData.m_uiNumProperties);
  return desc;
}

//////////////////////////////////////////////////////////////////////////

plCVarInt cvar_MaxNodeExecutions("VisualScript.MaxNodeExecutions", 100000, plCVarFlags::Default, "The maximum number of nodes executed within a script invocation");
plCVarInt cvar_MaxCallDepth("VisualScript.MaxCallDepth", 128, plCVarFlags::Default, "The maximum depth of nested visual script function calls");

plVisualScriptExecutionContext::plVisualScriptExecutionContext(const plSharedPtr<const plVisualScriptGraphDescription>& pDesc, plAllocator* pAllocator)
  : m_pDesc(pDesc)
  , m_pCurrentDesc(pDesc.Borrow())
  , m_pAllocator(pAllocator)
  , m_bIsCoroutine(pDesc->IsCoroutine())
  , m_LocalDataStorage(pDesc->GetLocalDataDesc())
{
  m_LocalDataStorage.AllocateStorage(pAllocator);
  m_DataStorage[DataOffset::Source::Local] = &m_LocalDataStorage;
}

plVisualScriptExecutionContext::~plVisualScriptExecutionContext()
{
  Deinitialize();
}

void plVisualScriptExecutionContext::Initialize(plVisualScriptInstance& inout_instance, plArrayPtr<plVariant> arguments)
{
  PL_ASSERT_DEV(m_CallStack.IsEmpty(), "Context is still executing a function call");

  m_pInstance = &inout_instance;
  m_pCurrentDesc = m_pDesc.Borrow();
  m_ReturnValues.Clear();

  m_DataStorage[DataOffset::Source::Instance] = inout_instance.GetInstanceDataStorage();
  m_DataStorage[DataOffset::Source::Constant] = inout_instance.GetConstantDataStorage();

  auto pNode = m_pCurrentDesc->GetNode(0);
  PL_ASSERT_DEV(plVisualScriptNodeDescription::Type::IsEntry(pNode->m_Type), "Invalid entry node");

  for (plUInt32 i = 0; i < arguments.GetCount(); ++i)
  {
    SetDataFromVariant(pNode->GetOutputDataOffset(i), arguments[i]);
  }

  m_uiCurrentNode = pNode->GetExecutionIndex(0);
}

void plVisualScriptExecutionContext::StopCurrentCoroutine()
{
  // 0x1 is a marker value to indicate that we are in a yield
  if (m_pCurrentCoroutine > reinterpret_cast<plScriptCoroutine*>(0x1))
  {
    auto pModule = m_pInstance->GetWorld()->GetOrCreateModule<plScriptWorldModule>();
    pModule->StopAndDeleteCoroutine(m_pCurrentCoroutine->GetHandle());
  }
  m_pCurrentCoroutine = nullptr;
}

void plVisualScriptExecutionContext::UnwindCallStack()
{
  while (m_CallStack.IsEmpty() == false)
  {
    StopCurrentCoroutine();

    CallFrame frame = m_CallStack.PeekBack();
    m_CallStack.PopBack();

    plVisualScriptDataStorage* pCalleeLocals = m_DataStorage[DataOffset::Source::Local];
    PL_DELETE(m_pAllocator, pCalleeLocals);

    m_pCurrentDesc = frame.m_pDesc;
    m_DataStorage[DataOffset::Source::Local] = frame.m_pLocalDataStorage;
    m_DataStorage[DataOffset::Source::Constant] = frame.m_pConstantDataStorage;
    m_pCurrentCoroutine = frame.m_pCurrentCoroutine;
    m_uiCurrentNode = frame.m_uiCallNodeIndex;
  }
}

void plVisualScriptExecutionContext::Deinitialize()
{
  if (m_pInstance == nullptr)
    return;

  UnwindCallStack();
  StopCurrentCoroutine();
}

plVisualScriptExecutionContext::ExecResult plVisualScriptExecutionContext::PushFrame(const plVisualScriptGraphDescription* pCalleeDesc, plVisualScriptDataStorage* pCalleeConstants, const plVisualScriptGraphDescription::Node& callNode)
{
  if (m_CallStack.GetCount() >= plUInt32(cvar_MaxCallDepth))
  {
    plLog::Error("Maximum visual script call depth ({}) reached, execution will be aborted. Does the script contain an unbounded recursion?", cvar_MaxCallDepth);
    return ExecResult::Error();
  }

  auto pEntryNode = pCalleeDesc->GetNode(0);
  if (pEntryNode == nullptr || plVisualScriptNodeDescription::Type::IsFunctionEntry(pEntryNode->m_Type) == false)
  {
    plLog::Error("Called visual script function does not start with a function entry node");
    return ExecResult::Error();
  }

  // Read the arguments while the caller's storages are still bound
  plHybridArray<plVariant, 8> arguments;
  for (plUInt32 i = 0; i < callNode.m_NumInputDataOffsets; ++i)
  {
    arguments.PushBack(GetDataAsVariant(callNode.GetInputDataOffset(i), nullptr));
  }

  plVisualScriptDataStorage* pCalleeLocals = PL_NEW(m_pAllocator, plVisualScriptDataStorage, pCalleeDesc->GetLocalDataDesc());
  pCalleeLocals->AllocateStorage(m_pAllocator);

  CallFrame& frame = m_CallStack.ExpandAndGetRef();
  frame.m_pDesc = m_pCurrentDesc;
  frame.m_pLocalDataStorage = m_DataStorage[DataOffset::Source::Local];
  frame.m_pConstantDataStorage = m_DataStorage[DataOffset::Source::Constant];
  frame.m_pCurrentCoroutine = m_pCurrentCoroutine;
  frame.m_uiCallNodeIndex = m_uiCurrentNode;

  m_pCurrentDesc = pCalleeDesc;
  m_DataStorage[DataOffset::Source::Local] = pCalleeLocals;
  if (pCalleeConstants != nullptr)
  {
    m_DataStorage[DataOffset::Source::Constant] = pCalleeConstants;
  }
  m_pCurrentCoroutine = nullptr;

  for (plUInt32 i = 0; i < arguments.GetCount(); ++i)
  {
    SetDataFromVariant(pEntryNode->GetOutputDataOffset(i), arguments[i]);
  }

  m_uiCurrentNode = pEntryNode->GetExecutionIndex(0);

  return ExecResult::FrameChanged();
}

plVisualScriptExecutionContext::ExecResult plVisualScriptExecutionContext::PopFrame(const plVisualScriptGraphDescription::Node* pReturnNode)
{
  // Read the return values while the callee's storages are still bound
  plHybridArray<plVariant, 8> values;
  if (pReturnNode != nullptr)
  {
    for (plUInt32 i = 0; i < pReturnNode->m_NumInputDataOffsets; ++i)
    {
      DataOffset dataOffset = pReturnNode->GetInputDataOffset(i);
      values.PushBack(dataOffset.IsValid() ? GetDataAsVariant(dataOffset, nullptr) : plVariant());
    }
  }

  if (m_CallStack.IsEmpty())
  {
    // Returning from the root function: keep the values for the caller of Execute
    m_ReturnValues = values;
    return ExecResult::Completed();
  }

  CallFrame frame = m_CallStack.PeekBack();
  m_CallStack.PopBack();

  plVisualScriptDataStorage* pCalleeLocals = m_DataStorage[DataOffset::Source::Local];
  PL_DELETE(m_pAllocator, pCalleeLocals);

  m_pCurrentDesc = frame.m_pDesc;
  m_DataStorage[DataOffset::Source::Local] = frame.m_pLocalDataStorage;
  m_DataStorage[DataOffset::Source::Constant] = frame.m_pConstantDataStorage;
  m_pCurrentCoroutine = frame.m_pCurrentCoroutine;

  auto pCallNode = m_pCurrentDesc->GetNode(frame.m_uiCallNodeIndex);
  PL_ASSERT_DEBUG(pCallNode != nullptr && plVisualScriptNodeDescription::Type::IsCall(pCallNode->m_Type), "Call frame does not point at a call node");

  const plUInt32 uiNumOutputs = plMath::Min<plUInt32>(pCallNode->m_NumOutputDataOffsets, values.GetCount());
  for (plUInt32 i = 0; i < uiNumOutputs; ++i)
  {
    if (values[i].IsValid())
    {
      SetDataFromVariant(pCallNode->GetOutputDataOffset(i), values[i]);
    }
  }

  m_uiCurrentNode = pCallNode->GetExecutionIndex(0);

  return ExecResult::FrameChanged();
}

plVisualScriptExecutionContext::ExecResult plVisualScriptExecutionContext::Execute(plTime deltaTimeSinceLastExecution)
{
  PL_ASSERT_DEV(m_pInstance != nullptr, "Invalid instance");
  ++m_uiExecutionCounter;
  m_DeltaTimeSinceLastExecution = deltaTimeSinceLastExecution;

#if PL_ENABLED(PL_COMPILE_FOR_DEVELOPMENT)
  plUInt32 uiCounter = 0;

  plVisualScriptDebugBackend::PushContext(this);
  PL_SCOPE_EXIT(plVisualScriptDebugBackend::PopContext());
#endif

#if PL_ENABLED(PL_COMPILE_FOR_DEVELOPMENT)
  // the editor's live flow: the last editor node that ran, so a step skips the nodes the compiler added in between
  const plVisualScriptGraphDescription* pFlowDesc = nullptr;
  plUuid lastFlowNode;

  // Initialize starts after the entry node (the event), so a fresh call counts the entry here and steps from it.
  if (plScriptFlow::IsWatching() && m_CallStack.IsEmpty() && m_pCurrentDesc == m_pDesc.Borrow())
  {
    const auto* pEntry = m_pCurrentDesc->GetNode(0);
    const plVisualScriptGraphDescription::DebugInfo& debugInfo = m_pCurrentDesc->GetDebugInfo();
    const plUuid entry = m_pCurrentDesc->GetNodeGuid(0);
    if (pEntry != nullptr && entry.IsValid() && m_uiCurrentNode == pEntry->GetExecutionIndex(0) && plScriptFlow::IsWatched(debugInfo.m_uiAssetHash))
    {
      plScriptFlow::RecordNode(debugInfo.m_uiAssetHash, entry);
      pFlowDesc = m_pCurrentDesc;
      lastFlowNode = entry;
    }
  }
#endif

  auto pNode = m_pCurrentDesc->GetNode(m_uiCurrentNode);
  while (pNode != nullptr)
  {
#if PL_ENABLED(PL_COMPILE_FOR_DEVELOPMENT)
    if (plScriptDebugger::IsAttached())
      plVisualScriptDebugBackend::GetSingleton()->OnNode(*this);

    // the node is timed (with everything it calls) when its graph is watched
    plUuid flowNode;
    plUInt64 uiFlowAsset = 0;
    plTime flowStart;
    if (plScriptFlow::IsWatching())
    {
      const plVisualScriptGraphDescription::DebugInfo& debugInfo = m_pCurrentDesc->GetDebugInfo();
      if (m_pCurrentDesc != pFlowDesc)
      {
        pFlowDesc = m_pCurrentDesc;
        lastFlowNode = plUuid();
      }

      const plUuid node = m_pCurrentDesc->GetNodeGuid(m_uiCurrentNode);
      if (node.IsValid() && plScriptFlow::IsWatched(debugInfo.m_uiAssetHash))
      {
        if (lastFlowNode.IsValid())
          plScriptFlow::RecordStep(debugInfo.m_uiAssetHash, lastFlowNode, node);
        lastFlowNode = node;

        flowNode = node;
        uiFlowAsset = debugInfo.m_uiAssetHash;
        flowStart = plTime::Now();
      }
    }
#endif

#if PL_ENABLED(PL_COMPILE_FOR_DEVELOPMENT)
    if (pNode->m_Function == nullptr)
    {
      plLog::Error("Node '{}' is not supported by runtime and should have been removed by the compiler.", plVisualScriptNodeDescription::Type::GetName(pNode->m_Type));
      return ExecResult::Error();
    }
#endif

    ExecResult result = pNode->m_Function(*this, *pNode);

#if PL_ENABLED(PL_COMPILE_FOR_DEVELOPMENT)
    if (flowNode.IsValid())
      plScriptFlow::RecordNode(uiFlowAsset, flowNode, plTime::Now() - flowStart);

    ++uiCounter;
    if (uiCounter >= plUInt32(cvar_MaxNodeExecutions))
    {
      plLog::Error("Maximum node executions ({}) reached, execution will be aborted. Does the script contain an infinite loop?", cvar_MaxNodeExecutions);
      return ExecResult::Error();
    }
#endif

    if (result.m_NextExecAndState == ExecResult::State::FrameChanged)
    {
      // PushFrame / PopFrame already selected the next node and description
      pNode = m_pCurrentDesc->GetNode(m_uiCurrentNode);
      continue;
    }

    if (result.m_NextExecAndState < ExecResult::State::Completed)
    {
#if PL_ENABLED(PL_COMPILE_FOR_DEVELOPMENT)
      if (result.m_NextExecAndState == ExecResult::State::Error)
        plVisualScriptDebugBackend::OnError(*this);
#endif
      return result;
    }

    m_uiCurrentNode = pNode->GetExecutionIndex(result.m_NextExecAndState);
    m_pCurrentCoroutine = nullptr;

    pNode = m_pCurrentDesc->GetNode(m_uiCurrentNode);

    if (pNode == nullptr && m_CallStack.IsEmpty() == false)
    {
      // The compiler always emits a return node at the end of a function; never leak a frame though.
      PL_ASSERT_DEBUG(false, "Visual script function ended without a return node");
      PopFrame(nullptr);
      pNode = m_pCurrentDesc->GetNode(m_uiCurrentNode);
    }
  }

  return ExecResult::RunNext(0);
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plVisualScriptSendMessageMode, 1)
  PL_ENUM_CONSTANTS(plVisualScriptSendMessageMode::Direct, plVisualScriptSendMessageMode::Recursive, plVisualScriptSendMessageMode::Event)
PL_END_STATIC_REFLECTED_ENUM;
// clang-format on


PL_STATICLINK_FILE(VisualScriptPlugin, VisualScriptPlugin_Runtime_VisualScript);
