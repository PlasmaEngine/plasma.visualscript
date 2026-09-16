#pragma once

#include <Core/Scripting/ScriptCoroutine.h>
#include <VisualScriptPlugin/Runtime/VisualScriptData.h>

class plVisualScriptInstance;
class plVisualScriptExecutionContext;

struct PL_VISUALSCRIPTPLUGIN_DLL plVisualScriptNodeDescription
{
  /// \brief Native node types for visual script graphs.
  /// Editor only types are not supported at runtime and will be replaced by the visual script compiler during asset transform.
  struct PL_VISUALSCRIPTPLUGIN_DLL Type
  {
    using StorageType = plUInt8;

    enum Enum
    {
      Invalid,
      EntryCall,
      EntryCall_Coroutine,
      MessageHandler,
      MessageHandler_Coroutine,
      FunctionEntry,           // Entry of a graph-internal function, invoked through Builtin_Call / Builtin_CallLibrary
      FunctionEntry_Coroutine, // Same, but the function body contains coroutine nodes
      ReflectedFunction,
      GetReflectedProperty,
      SetReflectedProperty,
      InplaceCoroutine,
      GetScriptOwner,
      SendMessage,

      FirstBuiltin,

      Builtin_Constant,    // Editor only
      Builtin_GetVariable, // Editor only
      Builtin_SetVariable,
      Builtin_IncVariable,
      Builtin_DecVariable,
      Builtin_TempVariable,

      Builtin_Branch,
      Builtin_Switch,
      Builtin_WhileLoop,          // Editor only
      Builtin_ForLoop,            // Editor only
      Builtin_ForEachLoop,        // Editor only
      Builtin_ReverseForEachLoop, // Editor only
      Builtin_Break,              // Editor only
      Builtin_Jump,               // Editor only

      Builtin_And,
      Builtin_Or,
      Builtin_Not,
      Builtin_Compare,
      Builtin_CompareExec, // Editor only
      Builtin_IsValid,
      Builtin_Select,

      Builtin_Add,
      Builtin_Subtract,
      Builtin_Multiply,
      Builtin_Divide,
      Builtin_Expression,

      Builtin_ToBool,
      Builtin_ToByte,
      Builtin_ToInt,
      Builtin_ToInt64,
      Builtin_ToFloat,
      Builtin_ToDouble,
      Builtin_ToString,
      Builtin_String_Format,
      Builtin_ToHashedString,
      Builtin_ToVariant,
      Builtin_Variant_ConvertTo,

      Builtin_MakeArray,
      Builtin_Array_GetElement,
      Builtin_Array_SetElement,
      Builtin_Array_GetCount,
      Builtin_Array_IsEmpty,
      Builtin_Array_Clear,
      Builtin_Array_Contains,
      Builtin_Array_IndexOf,
      Builtin_Array_Insert,
      Builtin_Array_PushBack,
      Builtin_Array_PushBackRange,
      Builtin_Array_Remove,
      Builtin_Array_RemoveAt,

      Builtin_TryGetComponentOfBaseType,

      Builtin_StartCoroutine,
      Builtin_StopCoroutine,
      Builtin_StopAllCoroutines,
      Builtin_WaitForAll,
      Builtin_WaitForAny,
      Builtin_Yield,

      Builtin_Call,        // Calls a function of the same module, pushes a call frame
      Builtin_CallLibrary, // Calls a function of a visual script library resource, pushes a call frame
      Builtin_Return,      // Pops the current call frame and writes the function outputs

      Builtin_Reroute,         // Editor only
      Builtin_PortalIn,        // Editor only
      Builtin_PortalOut,       // Editor only
      Builtin_SubGraph,        // Editor only
      Builtin_SubGraphInputs,  // Editor only
      Builtin_SubGraphOutputs, // Editor only

      LastBuiltin,

      Count,
      Default = Invalid
    };

    PL_ALWAYS_INLINE static bool IsEntry(Enum type) { return type >= EntryCall && type <= FunctionEntry_Coroutine; }
    PL_ALWAYS_INLINE static bool IsFunctionEntry(Enum type) { return type == FunctionEntry || type == FunctionEntry_Coroutine; }
    PL_ALWAYS_INLINE static bool IsCall(Enum type) { return type == Builtin_Call || type == Builtin_CallLibrary; }
    PL_ALWAYS_INLINE static bool IsLoop(Enum type) { return type >= Builtin_WhileLoop && type <= Builtin_ReverseForEachLoop; }

    /// \brief Editor-only nodes that only forward connections (reroutes, portals, sub-graph boundaries). The compiler resolves through them and never creates AST nodes for them.
    PL_ALWAYS_INLINE static bool IsTransparent(Enum type) { return type >= Builtin_Reroute && type <= Builtin_SubGraphOutputs; }

    PL_ALWAYS_INLINE static bool MakesOuterCoroutine(Enum type) { return type == InplaceCoroutine || (type >= Builtin_WaitForAll && type <= Builtin_Yield); }

    PL_ALWAYS_INLINE static bool IsBuiltin(Enum type) { return type > FirstBuiltin && type < LastBuiltin; }

    static Enum GetConversionType(plVisualScriptDataType::Enum targetDataType);

    static const char* GetName(Enum type);
  };

  using DataOffset = plVisualScriptDataDescription::DataOffset;

  plEnum<Type> m_Type;
  plEnum<plVisualScriptDataType> m_DeductedDataType;
  plSmallArray<plUInt16, 4> m_ExecutionIndices;
  plSmallArray<DataOffset, 4> m_InputDataOffsets;
  plSmallArray<DataOffset, 2> m_OutputDataOffsets;

  plHashedString m_sTargetTypeName;

  plVariant m_Value;

  void AppendUserDataName(plStringBuilder& out_sResult) const;
};

class PL_VISUALSCRIPTPLUGIN_DLL plVisualScriptGraphDescription : public plRefCounted
{
  PL_DISALLOW_COPY_AND_ASSIGN(plVisualScriptGraphDescription);

public:
  plVisualScriptGraphDescription();
  ~plVisualScriptGraphDescription();

  static plResult Serialize(plArrayPtr<const plVisualScriptNodeDescription> nodes, const plVisualScriptDataDescription& localDataDesc, plStreamWriter& inout_stream);
  plResult Deserialize(plStreamReader& inout_stream, const plVisualScriptDataDescription& instanceDataDesc, const plVisualScriptDataDescription& constantDataDesc);

  template <typename T, plUInt32 Size>
  struct EmbeddedArrayOrPointer
  {
    union
    {
      T m_Embedded[Size] = {};
      T* m_Ptr;
    };

    static void AddAdditionalDataSize(plArrayPtr<const T> a, plUInt32& inout_uiAdditionalDataSize);
    static void AddAdditionalDataSize(plUInt32 uiSize, plUInt32 uiAlignment, plUInt32& inout_uiAdditionalDataSize);

    T* Init(plUInt8 uiCount, plUInt32 uiAlignment, plUInt8*& inout_pAdditionalData);
    plResult ReadFromStream(plUInt8& out_uiCount, plStreamReader& inout_stream, plUInt8*& inout_pAdditionalData);
  };

  struct ExecResult
  {
    struct State
    {
      enum Enum
      {
        Completed = 0,
        ContinueLater = -1,
        FrameChanged = -2, ///< A call frame was pushed or popped, the context already points at the next node

        Error = -100,
      };
    };

    static PL_ALWAYS_INLINE ExecResult Completed() { return {0}; }
    static PL_ALWAYS_INLINE ExecResult RunNext(int iExecSlot) { return {iExecSlot}; }
    static PL_ALWAYS_INLINE ExecResult ContinueLater(plTime maxDelay) { return {State::ContinueLater, maxDelay}; }
    static PL_ALWAYS_INLINE ExecResult FrameChanged() { return {State::FrameChanged}; }
    static PL_ALWAYS_INLINE ExecResult Error() { return {State::Error}; }

    int m_NextExecAndState = 0;
    plTime m_MaxDelay = plTime::MakeZero();
  };

  struct Node;
  using ExecuteFunction = ExecResult (*)(plVisualScriptExecutionContext& inout_context, const Node& node);
  using DataOffset = plVisualScriptDataDescription::DataOffset;
  using ExecutionIndicesArray = EmbeddedArrayOrPointer<plUInt16, 4>;
  using InputDataOffsetsArray = EmbeddedArrayOrPointer<DataOffset, 4>;
  using OutputDataOffsetsArray = EmbeddedArrayOrPointer<DataOffset, 2>;
  using UserDataArray = EmbeddedArrayOrPointer<plUInt32, 4>;

  struct Node
  {
    ExecuteFunction m_Function = nullptr;
#if PL_ENABLED(PL_PLATFORM_32BIT)
    plUInt32 m_uiPadding = 0;
#endif

    ExecutionIndicesArray m_ExecutionIndices;
    InputDataOffsetsArray m_InputDataOffsets;
    OutputDataOffsetsArray m_OutputDataOffsets;
    UserDataArray m_UserData;

    plEnum<plVisualScriptNodeDescription::Type> m_Type;
    plUInt8 m_NumExecutionIndices;
    plUInt8 m_NumInputDataOffsets;
    plUInt8 m_NumOutputDataOffsets;

    plUInt16 m_UserDataByteSize;
    plEnum<plVisualScriptDataType> m_DeductedDataType;
    plUInt8 m_Reserved = 0;

    plUInt32 GetExecutionIndex(plUInt32 uiSlot) const;
    DataOffset GetInputDataOffset(plUInt32 uiSlot) const;
    DataOffset GetOutputDataOffset(plUInt32 uiSlot) const;

    DataOffset* GetInputDataOffsets();
    DataOffset* GetOutputDataOffsets();

    template <typename T>
    static constexpr plUInt32 GetUserDataAlignment();

    template <typename T>
    const T& GetUserData() const;

    template <typename T>
    T& GetWritableUserData();

    template <typename T>
    T& InitUserData(plUInt8*& inout_pAdditionalData, plUInt32 uiByteSize = sizeof(T), plUInt32 uiAlignment = GetUserDataAlignment<T>());
  };

  const Node* GetNode(plUInt32 uiIndex) const;
  plUInt32 GetNodeCount() const { return m_Nodes.GetCount(); }

  bool IsCoroutine() const;
  bool IsFunction() const;
  plScriptMessageDesc GetMessageDesc() const;

  /// \brief Number of arguments the entry node accepts (its output data pins).
  plUInt32 GetNumEntryArguments() const;

  /// \brief Calls \a func for every Builtin_Call / Builtin_CallLibrary node so the owning resource can patch the callee pointers into the node user data.
  plResult PatchCallSites(plDelegate<plResult(Node& ref_node)> func);

  const plSharedPtr<const plVisualScriptDataDescription>& GetLocalDataDesc() const;

private:
  plArrayPtr<const Node> m_Nodes;
  plBlob m_Storage;

  plSharedPtr<const plVisualScriptDataDescription> m_pLocalDataDesc;
};


class PL_VISUALSCRIPTPLUGIN_DLL plVisualScriptExecutionContext
{
public:
  plVisualScriptExecutionContext(const plSharedPtr<const plVisualScriptGraphDescription>& pDesc, plAllocator* pAllocator);
  ~plVisualScriptExecutionContext();

  void Initialize(plVisualScriptInstance& inout_instance, plArrayPtr<plVariant> arguments);
  void Deinitialize();

  using ExecResult = plVisualScriptGraphDescription::ExecResult;
  ExecResult Execute(plTime deltaTimeSinceLastExecution);

  plVisualScriptInstance& GetInstance() { return *m_pInstance; }

  using DataOffset = plVisualScriptDataDescription::DataOffset;

  template <typename T>
  const T& GetData(DataOffset dataOffset) const;

  template <typename T>
  T& GetWritableData(DataOffset dataOffset);

  template <typename T>
  void SetData(DataOffset dataOffset, const T& value);

  plTypedPointer GetPointerData(DataOffset dataOffset);

  template <typename T>
  void SetPointerData(DataOffset dataOffset, T ptr, const plRTTI* pType = nullptr);

  plVariant GetDataAsVariant(DataOffset dataOffset, const plRTTI* pExpectedType) const;
  void SetDataFromVariant(DataOffset dataOffset, const plVariant& value);

  plScriptCoroutine* GetCurrentCoroutine() { return m_pCurrentCoroutine; }
  void SetCurrentCoroutine(plScriptCoroutine* pCoroutine);

  plTime GetDeltaTimeSinceLastExecution();

  /// \brief Pushes a call frame for \a pCalleeDesc. The inputs of \a callNode become the arguments of the callee's entry node.
  /// \a pCalleeConstants is the constant data storage of the callee's module; nullptr keeps the current one (same module).
  ExecResult PushFrame(const plVisualScriptGraphDescription* pCalleeDesc, plVisualScriptDataStorage* pCalleeConstants, const plVisualScriptGraphDescription::Node& callNode);

  /// \brief Pops the current call frame. The inputs of \a pReturnNode (may be nullptr for an implicit return) are written to the outputs of the call node that pushed the frame.
  /// With an empty call stack the values are kept as the return values of the whole execution instead.
  ExecResult PopFrame(const plVisualScriptGraphDescription::Node* pReturnNode);

  plUInt32 GetCallDepth() const { return m_CallStack.GetCount(); }
  plArrayPtr<const plVariant> GetReturnValues() const { return m_ReturnValues; }

private:
  struct CallFrame
  {
    const plVisualScriptGraphDescription* m_pDesc = nullptr;
    plVisualScriptDataStorage* m_pLocalDataStorage = nullptr;
    plVisualScriptDataStorage* m_pConstantDataStorage = nullptr;
    plScriptCoroutine* m_pCurrentCoroutine = nullptr;
    plUInt32 m_uiCallNodeIndex = 0;
  };

  void UnwindCallStack();
  void StopCurrentCoroutine();

  plSharedPtr<const plVisualScriptGraphDescription> m_pDesc;
  const plVisualScriptGraphDescription* m_pCurrentDesc = nullptr;
  plVisualScriptInstance* m_pInstance = nullptr;
  plAllocator* m_pAllocator = nullptr;
  plUInt32 m_uiCurrentNode = 0;
  plUInt32 m_uiExecutionCounter = 0;
  plTime m_DeltaTimeSinceLastExecution;
  bool m_bIsCoroutine = false;

  plVisualScriptDataStorage m_LocalDataStorage;
  plVisualScriptDataStorage* m_DataStorage[DataOffset::Source::Count] = {};

  plScriptCoroutine* m_pCurrentCoroutine = nullptr;

  plHybridArray<CallFrame, 4> m_CallStack;
  plHybridArray<plVariant, 2> m_ReturnValues;
};

struct plVisualScriptSendMessageMode
{
  using StorageType = plUInt8;

  enum Enum
  {
    Direct,    ///< Directly send the message to the target game object
    Recursive, ///< Send the message to the target game object and its children
    Event,     ///< Send the message as event. \sa plGameObject::SendEventMessage()

    Default = Direct
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_VISUALSCRIPTPLUGIN_DLL, plVisualScriptSendMessageMode);

#include <VisualScriptPlugin/Runtime/VisualScript_inl.h>
