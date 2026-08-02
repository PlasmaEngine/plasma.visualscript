#include <EditorPluginAssets/EditorPluginAssetsPCH.h>

#include <Core/World/World.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptCompiler.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptTypeDeduction.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptVariable.moc.h>
#include <Foundation/CodeUtils/Expression/ExpressionByteCode.h>
#include <Foundation/CodeUtils/Expression/ExpressionCompiler.h>
#include <Foundation/CodeUtils/Expression/ExpressionParser.h>
#include <Foundation/IO/ChunkStream.h>
#include <Foundation/IO/StringDeduplicationContext.h>
#include <Foundation/SimdMath/SimdRandom.h>
#include <Foundation/Utilities/DGMLWriter.h>

namespace
{
  void MakeSubfunctionName(const plDocumentObject* pObject, const plDocumentObject* pEntryObject, plStringBuilder& out_sName)
  {
    plVariant sNameProperty = pObject->GetTypeAccessor().GetValue("Name");
    plUInt32 uiHash = plHashHelper<plUuid>::Hash(pObject->GetGuid());

    out_sName.SetFormat("{}_{}_{}", pEntryObject != nullptr ? plVisualScriptNodeManager::GetNiceFunctionName(pEntryObject) : "", sNameProperty, plArgU(uiHash, 8, true, 16));
  }

  plVisualScriptDataType::Enum FinalizeDataType(plVisualScriptDataType::Enum dataType)
  {
    plVisualScriptDataType::Enum result = dataType;
    if (result == plVisualScriptDataType::EnumValue || result == plVisualScriptDataType::BitflagValue)
      result = plVisualScriptDataType::Int64;

    if (result == plVisualScriptDataType::Resource)
      result = plVisualScriptDataType::String;

    return result;
  }

  using FillUserDataFunction = plResult (*)(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject);

  static plResult FillUserData_CoroutineMode(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject)
  {
    inout_astNode.m_Value = pObject->GetTypeAccessor().GetValue("CoroutineMode");
    return PL_SUCCESS;
  }

  static plResult FillUserData_ReflectedPropertyOrFunction(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject)
  {
    auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pObject->GetType());
    if (pNodeDesc->m_pTargetType != nullptr)
      inout_astNode.m_sTargetTypeName.Assign(pNodeDesc->m_pTargetType->GetTypeName());

    plVariantArray propertyNames;
    for (auto& pProp : pNodeDesc->m_TargetProperties)
    {
      plHashedString sPropertyName;
      sPropertyName.Assign(pProp->GetPropertyName());
      propertyNames.PushBack(sPropertyName);
    }

    inout_astNode.m_Value = propertyNames;

    return PL_SUCCESS;
  }

  static plResult FillUserData_DynamicReflectedProperty(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject)
  {
    auto pTargetType = plVisualScriptTypeDeduction::GetReflectedType(pObject);
    auto pTargetProperty = plVisualScriptTypeDeduction::GetReflectedProperty(pObject);
    if (pTargetType == nullptr || pTargetProperty == nullptr)
      return PL_FAILURE;

    inout_astNode.m_sTargetTypeName.Assign(pTargetType->GetTypeName());

    plVariantArray propertyNames;
    {
      plHashedString sPropertyName;
      sPropertyName.Assign(pTargetProperty->GetPropertyName());
      propertyNames.PushBack(sPropertyName);
    }

    inout_astNode.m_Value = propertyNames;

    return PL_SUCCESS;
  }

  static plResult FillUserData_VariableName(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject)
  {
    inout_astNode.m_Value = pObject->GetTypeAccessor().GetValue("Name");

    plStringView sName = inout_astNode.m_Value.Get<plString>().GetView();

    plVisualScriptVariable v;
    if (static_cast<const plVisualScriptNodeManager*>(pObject->GetDocumentObjectManager())->GetVariable(plTempHashedString(sName), v).Failed())
    {
      plLog::Error("Invalid variable named '{}'", sName);
      return PL_FAILURE;
    }

    return PL_SUCCESS;
  }

  static plResult FillUserData_Switch(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject)
  {
    inout_astNode.m_DeductedDataType = plVisualScriptDataType::Int64;

    plVariantArray casesVarArray;

    auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pObject->GetType());
    if (pNodeDesc->m_pTargetType != nullptr)
    {
      plHybridArray<plReflectionUtils::EnumKeyValuePair, 16> enumKeysAndValues;
      plReflectionUtils::GetEnumKeysAndValues(pNodeDesc->m_pTargetType, enumKeysAndValues, plReflectionUtils::EnumConversionMode::ValueNameOnly);
      for (auto& keyAndValue : enumKeysAndValues)
      {
        casesVarArray.PushBack(keyAndValue.m_iValue);
      }
    }
    else
    {
      plVariant casesVar = pObject->GetTypeAccessor().GetValue("Cases");
      casesVarArray = casesVar.Get<plVariantArray>();
      for (auto& caseVar : casesVarArray)
      {
        if (caseVar.IsA<plString>())
        {
          inout_astNode.m_DeductedDataType = plVisualScriptDataType::HashedString;
          caseVar = plTempHashedString(caseVar.Get<plString>()).GetHash();
        }
        else if (caseVar.IsA<plHashedString>())
        {
          inout_astNode.m_DeductedDataType = plVisualScriptDataType::HashedString;
          caseVar = caseVar.Get<plHashedString>().GetHash();
        }
      }
    }

    inout_astNode.m_Value = casesVarArray;
    return PL_SUCCESS;
  }

  static plResult FillUserData_Builtin_Compare(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject)
  {
    inout_astNode.m_Value = pObject->GetTypeAccessor().GetValue("Operator");
    return PL_SUCCESS;
  }

  static plResult FillUserData_Builtin_Expression(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject)
  {
    auto pManager = static_cast<const plVisualScriptNodeManager*>(pObject->GetDocumentObjectManager());

    plHybridArray<const plVisualScriptPin*, 16> pins;

    plHybridArray<plExpression::StreamDesc, 8> inputs;
    pManager->GetInputDataPins(pObject, pins);
    for (auto pPin : pins)
    {
      auto& input = inputs.ExpandAndGetRef();
      input.m_sName.Assign(pPin->GetName());
      input.m_DataType = plVisualScriptDataType::GetStreamDataType(pPin->GetResolvedScriptDataType());
    }

    plHybridArray<plExpression::StreamDesc, 8> outputs;
    pManager->GetOutputDataPins(pObject, pins);
    for (auto pPin : pins)
    {
      auto& output = outputs.ExpandAndGetRef();
      output.m_sName.Assign(pPin->GetName());
      output.m_DataType = plVisualScriptDataType::GetStreamDataType(pPin->GetResolvedScriptDataType());
    }

    plString sExpressionSource = pObject->GetTypeAccessor().GetValue("Expression").Get<plString>();

    plExpressionParser parser;
    plExpressionParser::Options options = {};
    plExpressionAST ast;
    PL_SUCCEED_OR_RETURN(parser.Parse(sExpressionSource, inputs, outputs, options, ast));

    plExpressionCompiler compiler;
    plExpressionByteCode byteCode;
    PL_SUCCEED_OR_RETURN(compiler.Compile(ast, byteCode));

    inout_astNode.m_Value = byteCode;

    return PL_SUCCESS;
  }

  static plResult FillUserData_Builtin_TryGetComponentOfBaseType(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject)
  {
    auto typeName = pObject->GetTypeAccessor().GetValue("TypeName");
    const plRTTI* pType = plRTTI::FindTypeByName(typeName.Get<plString>());
    if (pType == nullptr)
    {
      plLog::Error("Invalid type '{}' for GameObject::TryGetComponentOfBaseType node.", typeName);
      return PL_FAILURE;
    }

    inout_astNode.m_sTargetTypeName.Assign(pType->GetTypeName());
    return PL_SUCCESS;
  }

  static plResult FillUserData_Builtin_StartCoroutine(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject)
  {
    PL_SUCCEED_OR_RETURN(FillUserData_CoroutineMode(inout_astNode, pCompiler, pObject, pEntryObject));

    auto pManager = static_cast<const plVisualScriptNodeManager*>(pObject->GetDocumentObjectManager());
    plHybridArray<const plVisualScriptPin*, 16> pins;
    pManager->GetOutputExecutionPins(pObject, pins);

    const plUInt32 uiCoroutineBodyIndex = 1;
    auto connections = pManager->GetConnections(*pins[uiCoroutineBodyIndex]);
    if (connections.IsEmpty() == false)
    {
      plStringBuilder sFunctionName;
      MakeSubfunctionName(pObject, pEntryObject, sFunctionName);

      plStringBuilder sFullName;
      sFullName.Set(pCompiler->GetCompiledModule().m_sScriptClassName, "::", sFunctionName, "<Coroutine>");

      inout_astNode.m_sTargetTypeName.Assign(sFullName);

      return pCompiler->AddFunction(sFunctionName, connections[0]->GetTargetPin().GetParent(), pObject);
    }

    return PL_SUCCESS;
  }

  static FillUserDataFunction s_TypeToFillUserDataFunctions[] = {
    nullptr,                                         // Invalid,
    &FillUserData_CoroutineMode,                     // EntryCall,
    &FillUserData_CoroutineMode,                     // EntryCall_Coroutine,
    &FillUserData_ReflectedPropertyOrFunction,       // MessageHandler,
    &FillUserData_ReflectedPropertyOrFunction,       // MessageHandler_Coroutine,
    &FillUserData_ReflectedPropertyOrFunction,       // ReflectedFunction,
    &FillUserData_DynamicReflectedProperty,          // GetReflectedProperty,
    &FillUserData_DynamicReflectedProperty,          // SetReflectedProperty,
    &FillUserData_ReflectedPropertyOrFunction,       // InplaceCoroutine,
    nullptr,                                         // GetOwner,
    &FillUserData_ReflectedPropertyOrFunction,       // SendMessage,

    nullptr,                                         // FirstBuiltin,

    nullptr,                                         // Builtin_Constant,
    &FillUserData_VariableName,                      // Builtin_GetVariable,
    &FillUserData_VariableName,                      // Builtin_SetVariable,
    &FillUserData_VariableName,                      // Builtin_IncVariable,
    &FillUserData_VariableName,                      // Builtin_DecVariable,
    nullptr,                                         // Builtin_TempVariable,

    nullptr,                                         // Builtin_Branch,
    &FillUserData_Switch,                            // Builtin_Switch,
    nullptr,                                         // Builtin_WhileLoop,
    nullptr,                                         // Builtin_ForLoop,
    nullptr,                                         // Builtin_ForEachLoop,
    nullptr,                                         // Builtin_ReverseForEachLoop,
    nullptr,                                         // Builtin_Break,
    nullptr,                                         // Builtin_Jump,

    nullptr,                                         // Builtin_And,
    nullptr,                                         // Builtin_Or,
    nullptr,                                         // Builtin_Not,
    &FillUserData_Builtin_Compare,                   // Builtin_Compare,
    &FillUserData_Builtin_Compare,                   // Builtin_CompareExec,
    nullptr,                                         // Builtin_IsValid,
    nullptr,                                         // Builtin_Select,

    nullptr,                                         // Builtin_Add,
    nullptr,                                         // Builtin_Subtract,
    nullptr,                                         // Builtin_Multiply,
    nullptr,                                         // Builtin_Divide,
    &FillUserData_Builtin_Expression,                // Builtin_Expression,

    nullptr,                                         // Builtin_ToBool,
    nullptr,                                         // Builtin_ToByte,
    nullptr,                                         // Builtin_ToInt,
    nullptr,                                         // Builtin_ToInt64,
    nullptr,                                         // Builtin_ToFloat,
    nullptr,                                         // Builtin_ToDouble,
    nullptr,                                         // Builtin_ToString,
    nullptr,                                         // Builtin_String_Format,
    nullptr,                                         // Builtin_ToHashedString,
    nullptr,                                         // Builtin_ToVariant,
    nullptr,                                         // Builtin_Variant_ConvertTo,

    nullptr,                                         // Builtin_MakeArray
    nullptr,                                         // Builtin_Array_GetElement,
    nullptr,                                         // Builtin_Array_SetElement,
    nullptr,                                         // Builtin_Array_GetCount,
    nullptr,                                         // Builtin_Array_IsEmpty,
    nullptr,                                         // Builtin_Array_Clear,
    nullptr,                                         // Builtin_Array_Contains,
    nullptr,                                         // Builtin_Array_IndexOf,
    nullptr,                                         // Builtin_Array_Insert,
    nullptr,                                         // Builtin_Array_PushBack,
    nullptr,                                         // Builtin_Array_PushBackRange,
    nullptr,                                         // Builtin_Array_Remove,
    nullptr,                                         // Builtin_Array_RemoveAt,

    &FillUserData_Builtin_TryGetComponentOfBaseType, // Builtin_TryGetComponentOfBaseType

    &FillUserData_Builtin_StartCoroutine,            // Builtin_StartCoroutine,
    nullptr,                                         // Builtin_StopCoroutine,
    nullptr,                                         // Builtin_StopAllCoroutines,
    nullptr,                                         // Builtin_WaitForAll,
    nullptr,                                         // Builtin_WaitForAny,
    nullptr,                                         // Builtin_Yield,

    nullptr,                                         // LastBuiltin,
  };

  static_assert(PL_ARRAY_SIZE(s_TypeToFillUserDataFunctions) == plVisualScriptNodeDescription::Type::Count);

  plResult FillUserData(plVisualScriptCompiler::AstNode& inout_astNode, plVisualScriptCompiler* pCompiler, const plDocumentObject* pObject, const plDocumentObject* pEntryObject)
  {
    if (pObject == nullptr)
      return PL_SUCCESS;

    auto nodeType = inout_astNode.m_Type;
    PL_ASSERT_DEBUG(nodeType >= 0 && nodeType < PL_ARRAY_SIZE(s_TypeToFillUserDataFunctions), "Out of bounds access");
    auto func = s_TypeToFillUserDataFunctions[nodeType];

    if (func != nullptr)
    {
      PL_SUCCEED_OR_RETURN(func(inout_astNode, pCompiler, pObject, pEntryObject));
    }

    return PL_SUCCESS;
  }

} // namespace

//////////////////////////////////////////////////////////////////////////

plVisualScriptCompiler::CompiledModule::CompiledModule()
  : m_ConstantDataStorage(plSharedPtr<plVisualScriptDataDescription>(&m_ConstantDataDesc, nullptr))
{
  // Prevent the data desc from being deleted by fake shared ptr above
  m_ConstantDataDesc.AddRef();
}

plResult plVisualScriptCompiler::CompiledModule::Serialize(plStreamWriter& inout_stream) const
{
  PL_ASSERT_DEV(m_sScriptClassName.IsEmpty() == false, "Invalid script class name");

  plStringDeduplicationWriteContext stringDedup(inout_stream);

  plChunkStreamWriter chunk(stringDedup.Begin());
  chunk.BeginStream(1);

  {
    chunk.BeginChunk("Header", 1);
    chunk << m_sBaseClassName;
    chunk << m_sScriptClassName;
    chunk.EndChunk();
  }

  {
    chunk.BeginChunk("ConstantData", 1);
    PL_SUCCEED_OR_RETURN(m_ConstantDataDesc.Serialize(chunk));
    PL_SUCCEED_OR_RETURN(m_ConstantDataStorage.Serialize(chunk));
    chunk.EndChunk();
  }

  {
    chunk.BeginChunk("InstanceData", 1);
    PL_SUCCEED_OR_RETURN(m_InstanceDataDesc.Serialize(chunk));
    PL_SUCCEED_OR_RETURN(chunk.WriteHashTable(m_InstanceDataMapping.m_Content));
    chunk.EndChunk();
  }

  {
    chunk.BeginChunk("FunctionGraphs", 1);
    chunk << m_Functions.GetCount();

    // Reverse order so functions that are added later (e.g. coroutines) are loaded first
    for (plUInt32 i = m_Functions.GetCount(); i-- > 0;)
    {
      auto& function = m_Functions[i];

      chunk << function.m_sName;
      chunk << function.m_Type;
      chunk << function.m_CoroutineCreationMode;

      PL_SUCCEED_OR_RETURN(plVisualScriptGraphDescription::Serialize(function.m_NodeDescriptions, function.m_LocalDataDesc, chunk));
    }

    chunk.EndChunk();
  }

  chunk.EndStream();

  return stringDedup.End();
}

//////////////////////////////////////////////////////////////////////////

plVisualScriptCompiler::plVisualScriptCompiler(plVisualScriptNodeManager& ref_nodeManager)
  : m_NodeManager(ref_nodeManager)
{
}

plVisualScriptCompiler::~plVisualScriptCompiler() = default;

void plVisualScriptCompiler::InitModule(plStringView sBaseClassName, plStringView sScriptClassName)
{
  m_Module.m_sBaseClassName = sBaseClassName;
  m_Module.m_sScriptClassName = sScriptClassName;
}

plResult plVisualScriptCompiler::AddFunction(plStringView sName, const plDocumentObject* pEntryObject, const plDocumentObject* pParentObject)
{
  PL_ASSERT_DEV(&m_NodeManager == pEntryObject->GetDocumentObjectManager(), "Can't add functions from different document");

  for (auto& existingFunction : m_Module.m_Functions)
  {
    if (existingFunction.m_sName == sName)
    {
      plLog::Error("A function named '{}' already exists. Function names need to unique.", sName);
      return PL_FAILURE;
    }
  }

  auto& function = m_Module.m_Functions.ExpandAndGetRef();
  function.m_sName = sName;

  {
    auto pObjectWithCoroutineMode = pParentObject != nullptr ? pParentObject : pEntryObject;
    auto mode = pObjectWithCoroutineMode->GetTypeAccessor().GetValue("CoroutineMode");
    if (mode.IsA<plInt64>())
    {
      function.m_CoroutineCreationMode = static_cast<plScriptCoroutineCreationMode::Enum>(mode.Get<plInt64>());
    }
    else
    {
      function.m_CoroutineCreationMode = plScriptCoroutineCreationMode::AllowOverlap;
    }
  }

  m_EntryObjects.PushBack(pEntryObject);
  PL_ASSERT_DEBUG(m_Module.m_Functions.GetCount() == m_EntryObjects.GetCount(), "");

  return PL_SUCCESS;
}

plResult plVisualScriptCompiler::Compile(plStringView sDebugAstOutputPath)
{
  PL_SUCCEED_OR_RETURN(BuildInstanceDataMapping());

  for (plUInt32 i = 0; i < m_Module.m_Functions.GetCount(); ++i)
  {
    auto& function = m_Module.m_Functions[i];
    const plDocumentObject* pEntryObject = m_EntryObjects[i];

    AstNode* pEntryAstNode = BuildExecutionFlow(pEntryObject);
    if (pEntryAstNode == nullptr)
    {
      plLog::Error("Failed to build execution flow for function '{}'", function.m_sName);
      return PL_FAILURE;
    }

    function.m_Type = pEntryAstNode->m_Type;

    PL_SUCCEED_OR_RETURN(BuildDataExecutions(pEntryAstNode));

    DumpAST(pEntryAstNode, sDebugAstOutputPath, function.m_sName, "_00");

    PL_SUCCEED_OR_RETURN(InsertTypeConversions(pEntryAstNode));

    DumpAST(pEntryAstNode, sDebugAstOutputPath, function.m_sName, "_01_TypeConv");

    PL_SUCCEED_OR_RETURN(AssignInstanceVariables(pEntryAstNode));

    DumpAST(pEntryAstNode, sDebugAstOutputPath, function.m_sName, "_02_InstanceVarsAssigned");

    PL_SUCCEED_OR_RETURN(ReplaceUnsupportedNodes(pEntryAstNode));

    DumpAST(pEntryAstNode, sDebugAstOutputPath, function.m_sName, "_03_Replaced");

    PL_SUCCEED_OR_RETURN(CopyOutputsToInputs(pEntryAstNode));
    PL_SUCCEED_OR_RETURN(AssignLocalVariables(pEntryAstNode, function.m_LocalDataDesc));

    DumpAST(pEntryAstNode, sDebugAstOutputPath, function.m_sName, "_04_LocalVarsAssigned");

    PL_SUCCEED_OR_RETURN(BuildNodeDescriptions(pEntryAstNode, function.m_NodeDescriptions));

    DumpGraph(function.m_NodeDescriptions, sDebugAstOutputPath, function.m_sName, "_Graph");

    m_CompilationState.Clear();
  }

  PL_SUCCEED_OR_RETURN(FinalizeConstantData());

  return PL_SUCCESS;
}

// Ast node creation
//////////////////////////////////////////////////////////////////////////

plVisualScriptCompiler::AstNode& plVisualScriptCompiler::CreateAstNode(plVisualScriptNodeDescription::Type::Enum type, plVisualScriptDataType::Enum deductedDataType, bool bImplicitExecution /*= false*/)
{
  auto& node = m_AstNodes.ExpandAndGetRef();
  node.m_Type = type;
  node.m_DeductedDataType = deductedDataType;
  node.m_bImplicitExecution = bImplicitExecution;
  return node;
}

plVisualScriptCompiler::AstNode& plVisualScriptCompiler::CreateJumpNode(AstNode* pTargetNode)
{
  auto& jumpNode = CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Jump);
  jumpNode.m_Value = plUInt64(*reinterpret_cast<size_t*>(&pTargetNode));

  return jumpNode;
}

plVisualScriptCompiler::AstNode* plVisualScriptCompiler::CreateAstNodeFromObject(const plDocumentObject* pObject, const plVisualScriptNodeRegistry::NodeDesc* pNodeDesc, const plDocumentObject* pEntryObject, bool bImplicitOnly /*= false*/)
{
  if (pNodeDesc == nullptr)
  {
    pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pObject->GetType());
  }
  PL_ASSERT_DEV(pNodeDesc != nullptr && pNodeDesc->m_Type != plVisualScriptNodeDescription::Type::GetScriptOwner, "Invalid node type");

  if (bImplicitOnly && !pNodeDesc->m_bImplicitExecution)
    return nullptr;

  auto& astNode = CreateAstNode(pNodeDesc->m_Type, FinalizeDataType(m_NodeManager.GetDeductedType(pObject)), pNodeDesc->m_bImplicitExecution);
  astNode.m_pObject = pObject;

  if (FillUserData(astNode, this, pObject, pEntryObject).Failed())
    return nullptr;

  if (pNodeDesc->m_bImplicitExecution)
  {
    m_CompilationState.m_DataObjectToAstNode.Insert(pObject, &astNode);
  }
  else
  {
    m_CompilationState.m_ExecObjectToAstNode.Insert(pObject, &astNode);
  }

  return &astNode;
}

plVisualScriptCompiler::DataInput plVisualScriptCompiler::GetOrCreateDefaultPointerNode(const AstNode& node, const plRTTI* pRtti)
{
  if (pRtti == nullptr)
  {
    pRtti = plRTTI::FindTypeByNameHash(node.m_sTargetTypeName.GetHash());
  }

  const bool bIsGameObject = pRtti == plGetStaticRTTI<plGameObject>() || pRtti == plGetStaticRTTI<plGameObjectHandle>();
  const bool bIsWorld = pRtti == plGetStaticRTTI<plWorld>();

  if (bIsWorld || bIsGameObject)
  {
    DataInput dataInput;
    dataInput.m_pSourceNode = m_CompilationState.m_pGetScriptOwnerNode;
    dataInput.m_uiSourcePinIndex = bIsGameObject ? 1 : 0;
    dataInput.m_DataOffset = m_CompilationState.m_pGetScriptOwnerNode->m_DataOutputs[dataInput.m_uiSourcePinIndex].m_DataOffset;
    return dataInput;
  }

  return DataInput();
}

void plVisualScriptCompiler::MarkAsCoroutine(AstNode* pEntryAstNode)
{
  switch (pEntryAstNode->m_Type)
  {
    case plVisualScriptNodeDescription::Type::EntryCall:
      pEntryAstNode->m_Type = plVisualScriptNodeDescription::Type::EntryCall_Coroutine;
      break;
    case plVisualScriptNodeDescription::Type::MessageHandler:
      pEntryAstNode->m_Type = plVisualScriptNodeDescription::Type::MessageHandler_Coroutine;
      break;
    case plVisualScriptNodeDescription::Type::EntryCall_Coroutine:
    case plVisualScriptNodeDescription::Type::MessageHandler_Coroutine:
      // Already a coroutine
      break;
      PL_DEFAULT_CASE_NOT_IMPLEMENTED;
  }
}

// Pins, inputs and outputs
//////////////////////////////////////////////////////////////////////////

void plVisualScriptCompiler::AddConstantDataInput(AstNode& node, const plVariant& value)
{
  plVisualScriptDataType::Enum dataType = plVisualScriptDataType::FromVariantType(value.GetType());

  plUInt32 uiIndex = plInvalidIndex;
  if (m_Module.m_ConstantDataToIndex.TryGetValue(value, uiIndex) == false)
  {
    auto& offsetAndCount = m_Module.m_ConstantDataDesc.m_PerTypeInfo[dataType];
    uiIndex = offsetAndCount.m_uiCount;
    ++offsetAndCount.m_uiCount;

    m_Module.m_ConstantDataToIndex.Insert(value, uiIndex);
  }

  auto& dataInput = node.m_DataInputs.ExpandAndGetRef();
  dataInput.m_pSourceNode = nullptr;
  dataInput.m_uiSourcePinIndex = 0;
  dataInput.m_DataOffset = DataOffset(uiIndex, dataType, DataOffset::Source::Constant);
}

plResult plVisualScriptCompiler::AddConstantDataInput(AstNode& node, const plDocumentObject* pObject, const plVisualScriptPin* pPin, plVisualScriptDataType::Enum dataType)
{
  plStringView sPropertyName = pPin->HasDynamicPinProperty() ? pPin->GetDynamicPinProperty() : pPin->GetName();

  plVariant value = pObject->GetTypeAccessor().GetValue(sPropertyName);
  if (value.IsValid() && pPin->HasDynamicPinProperty())
  {
    PL_ASSERT_DEBUG(value.IsA<plVariantArray>(), "Implementation error");
    value = value.Get<plVariantArray>()[pPin->GetElementIndex()];
  }

  if (value.IsA<plUuid>())
  {
    value = 0;
  }

  plVisualScriptDataType::Enum valueDataType = plVisualScriptDataType::FromVariantType(value.GetType());
  if (dataType != plVisualScriptDataType::Variant)
  {
    value = value.ConvertTo(plVisualScriptDataType::GetVariantType(dataType));
    if (value.IsValid() == false)
    {
      plLog::Error("Failed to convert '{}.{}' of type '{}' to '{}'.", GetNiceTypeName(pObject), pPin->GetName(), plVisualScriptDataType::GetName(valueDataType), plVisualScriptDataType::GetName(dataType));
      return PL_FAILURE;
    }
  }

  AddConstantDataInput(node, value);

  return PL_SUCCESS;
}

void plVisualScriptCompiler::AddDataInput(AstNode& node, AstNode* pSourceNode, plUInt32 uiSourcePinIndex, plVisualScriptDataType::Enum dataType)
{
  auto& dataInput = node.m_DataInputs.ExpandAndGetRef();
  dataInput.m_pSourceNode = pSourceNode;
  dataInput.m_uiSourcePinIndex = uiSourcePinIndex;
  dataInput.m_DataOffset.m_uiType = dataType;
}

void plVisualScriptCompiler::AddDataOutput(AstNode& node, plVisualScriptDataType::Enum dataType)
{
  auto& dataOutput = node.m_DataOutputs.ExpandAndGetRef();
  dataOutput.m_DataOffset = DataOffset(m_CompilationState.m_uiNextLocalVarId++, dataType, DataOffset::Source::Local);
}

plVisualScriptCompiler::DataOutput& plVisualScriptCompiler::GetDataOutputFromInput(const DataInput& dataInput)
{
  if (dataInput.m_uiSourcePinIndex < dataInput.m_pSourceNode->m_DataOutputs.GetCount())
  {
    return dataInput.m_pSourceNode->m_DataOutputs[dataInput.m_uiSourcePinIndex];
  }

  PL_ASSERT_DEBUG(false, "This code should be never reached");
  static DataOutput dummy;
  return dummy;
}

void plVisualScriptCompiler::ConnectExecution(AstNode& sourceNode, AstNode& targetNode, plUInt32 uiSourcePinIndex /*= plInvalidIndex*/)
{
  if (uiSourcePinIndex == plInvalidIndex)
  {
    // New connection
    ExecInput execInput;
    execInput.m_pSourceNode = &sourceNode;
    execInput.m_uiSourcePinIndex = sourceNode.m_ExecOutputs.GetCount();

    targetNode.m_ExecInputs.PushBack(execInput);

    ExecOutput execOutput;
    execOutput.m_pTargetNode = &targetNode;

    sourceNode.m_ExecOutputs.PushBack(execOutput);
  }
  else
  {
    ExecInput execInput;
    execInput.m_pSourceNode = &sourceNode;
    execInput.m_uiSourcePinIndex = uiSourcePinIndex;

    targetNode.m_ExecInputs.PushBack(execInput);

    sourceNode.m_ExecOutputs.EnsureCount(uiSourcePinIndex + 1);
    auto& execOutput = sourceNode.m_ExecOutputs[uiSourcePinIndex];
    execOutput.m_pTargetNode = &targetNode;
  }
}

void plVisualScriptCompiler::DisconnectExecution(AstNode& sourceNode, AstNode& targetNode, plUInt32 uiSourcePinIndex)
{
  ExecInput execInputToRemove;
  execInputToRemove.m_pSourceNode = &sourceNode;
  execInputToRemove.m_uiSourcePinIndex = uiSourcePinIndex;
  PL_VERIFY(targetNode.m_ExecInputs.RemoveAndCopy(execInputToRemove), "");

  sourceNode.m_ExecOutputs[uiSourcePinIndex].m_pTargetNode = nullptr;
}

void plVisualScriptCompiler::ExecuteBefore(AstNode& node, AstNode& firstNewNode, AstNode& lastNewNode)
{
  for (auto& execInput : node.m_ExecInputs)
  {
    ConnectExecution(*execInput.m_pSourceNode, firstNewNode, execInput.m_uiSourcePinIndex);
  }
  node.m_ExecInputs.Clear();

  ConnectExecution(lastNewNode, node);
}

void plVisualScriptCompiler::ExecuteAfter(AstNode& node, AstNode& firstNewNode, AstNode& lastNewNode)
{
  PL_ASSERT_DEV(node.m_ExecOutputs.GetCount() == 1, "This function only works for nodes with a single exec output pin");
  AstNode* pNodeAfter = node.m_ExecOutputs[0].m_pTargetNode;

  ConnectExecution(node, firstNewNode, 0);

  pNodeAfter->m_ExecInputs.Clear();
  ConnectExecution(lastNewNode, *pNodeAfter);
}

void plVisualScriptCompiler::ReplaceExecution(AstNode& oldNode, AstNode& newNode)
{
  PL_ASSERT_DEBUG(newNode.m_ExecInputs.IsEmpty() && newNode.m_ExecOutputs.IsEmpty(), "New node must not have any connections yet");

  for (auto& execInput : oldNode.m_ExecInputs)
  {
    ConnectExecution(*execInput.m_pSourceNode, newNode, execInput.m_uiSourcePinIndex);
  }
  oldNode.m_ExecInputs.Clear();

  for (plUInt32 i = 0; i < oldNode.m_ExecOutputs.GetCount(); ++i)
  {
    AstNode* pTargetNode = oldNode.m_ExecOutputs[i].m_pTargetNode;
    if (pTargetNode != nullptr)
    {
      DisconnectExecution(oldNode, *pTargetNode, i);
      ConnectExecution(newNode, *pTargetNode, i);
    }
    else
    {
      newNode.m_ExecOutputs.PushBack({nullptr});
    }
  }
  oldNode.m_ExecOutputs.Clear();
}

plVisualScriptCompiler::DataOffset plVisualScriptCompiler::GetInstanceDataOffset(plHashedString sName, plVisualScriptDataType::Enum dataType)
{
  plVisualScriptInstanceData instanceData;
  if (m_Module.m_InstanceDataMapping.m_Content.TryGetValue(sName, instanceData) == false)
  {
    plLog::Error("Invalid variable named '{}'", sName);
    return DataOffset();
  }

  PL_ASSERT_DEBUG(instanceData.m_DataOffset.m_uiType == dataType, "Data type mismatch");
  return instanceData.m_DataOffset;
}

// Compilation steps
//////////////////////////////////////////////////////////////////////////

plResult plVisualScriptCompiler::BuildInstanceDataMapping()
{
  plHybridArray<plVisualScriptVariable, 16> variables;
  m_NodeManager.GetAllVariables(variables);

  for (auto& variable : variables)
  {
    auto dataType = variable.m_TypeDecl.GetDataType();

    PL_ASSERT_DEV(dataType == plVisualScriptDataType::Variant ||
                    ((dataType == plVisualScriptDataType::GameObject || dataType == plVisualScriptDataType::Component) && variable.m_DefaultValue.IsValid() == false) ||
                    dataType == plVisualScriptDataType::FromVariantType(variable.m_DefaultValue.GetType()),
      "Data type mismatch");

    auto& offsetAndCount = m_Module.m_InstanceDataDesc.m_PerTypeInfo[dataType];
    plVisualScriptInstanceData instanceData;

    instanceData.m_DataOffset.m_uiByteOffset = offsetAndCount.m_uiCount;
    instanceData.m_DataOffset.m_uiType = dataType;
    instanceData.m_DataOffset.m_uiSource = DataOffset::Source::Instance;
    instanceData.m_DefaultValue = variable.m_DefaultValue;

    ++offsetAndCount.m_uiCount;

    m_Module.m_InstanceDataMapping.m_Content.Insert(variable.m_sName, instanceData);
  }

  return PL_SUCCESS;
}

plVisualScriptCompiler::AstNode* plVisualScriptCompiler::BuildExecutionFlow(const plDocumentObject* pEntryObject)
{
  AstNode* pEntryAstNode = CreateAstNodeFromObject(pEntryObject, nullptr, pEntryObject);
  if (pEntryAstNode == nullptr)
    return nullptr;

  if (plVisualScriptNodeDescription::Type::IsEntry(pEntryAstNode->m_Type) == false)
  {
    auto& astNode = CreateAstNode(plVisualScriptNodeDescription::Type::EntryCall);
    ConnectExecution(astNode, *pEntryAstNode);

    pEntryAstNode = &astNode;
  }

  plHybridArray<const plVisualScriptPin*, 16> pins;
  plHybridArray<const plDocumentObject*, 64> objectStack;
  objectStack.PushBack(pEntryObject);

  while (objectStack.IsEmpty() == false)
  {
    const plDocumentObject* pObject = objectStack.PeekBack();
    objectStack.PopBack();

    AstNode* pAstNode = nullptr;
    PL_VERIFY(m_CompilationState.m_ExecObjectToAstNode.TryGetValue(pObject, pAstNode), "Implementation error");

    if (plVisualScriptNodeDescription::Type::MakesOuterCoroutine(pAstNode->m_Type))
    {
      MarkAsCoroutine(pEntryAstNode);
    }

    m_NodeManager.GetOutputExecutionPins(pObject, pins);
    for (auto pPin : pins)
    {
      auto connections = m_NodeManager.GetConnections(*pPin);
      if (connections.IsEmpty() || pPin->SplitExecution())
      {
        pAstNode->m_ExecOutputs.PushBack({nullptr});
        continue;
      }

      PL_ASSERT_DEV(connections.GetCount() == 1, "Output execution pins should only have one connection");
      const plDocumentObject* pNextObject = connections[0]->GetTargetPin().GetParent();

      AstNode* pNextAstNode;
      if (m_CompilationState.m_ExecObjectToAstNode.TryGetValue(pNextObject, pNextAstNode) == false)
      {
        pNextAstNode = CreateAstNodeFromObject(pNextObject, nullptr, pEntryObject);
        if (pNextAstNode == nullptr)
          return nullptr;

        objectStack.PushBack(pNextObject);
      }

      ConnectExecution(*pAstNode, *pNextAstNode);
    }
  }

  // Always add a GetScriptOwner node directly after the entry node
  {
    PL_ASSERT_DEBUG(m_CompilationState.m_pGetScriptOwnerNode == nullptr, "");

    auto& getScriptOwnerNode = CreateAstNode(plVisualScriptNodeDescription::Type::GetScriptOwner);
    AddDataOutput(getScriptOwnerNode, plVisualScriptDataType::TypedPointer);
    AddDataOutput(getScriptOwnerNode, plVisualScriptDataType::GameObject);
    AddDataOutput(getScriptOwnerNode, plVisualScriptDataType::Component);

    m_CompilationState.m_pGetScriptOwnerNode = &getScriptOwnerNode;

    ExecuteAfter(*pEntryAstNode, getScriptOwnerNode, getScriptOwnerNode);
  }

  return pEntryAstNode;
}

plResult plVisualScriptCompiler::BuildDataStack(AstNode* pEntryAstNode, AstNode*& out_pFirstDataNode, AstNode*& out_pLastDataNode)
{
  if (pEntryAstNode->m_pObject == nullptr)
    return PL_SUCCESS;

  plHybridArray<const plVisualScriptPin*, 16> pins;

  struct ObjectContext
  {
    const plDocumentObject* m_pObject = nullptr;
    const plVisualScriptNodeRegistry::NodeDesc* m_pNodeDesc = nullptr;
  };

  plHybridArray<ObjectContext, 32> objectStack;
  objectStack.PushBack({pEntryAstNode->m_pObject, plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pEntryAstNode->m_pObject->GetType())});

  // Start from scratch for each data stack
  m_CompilationState.m_DataObjectToAstNode.Clear();
  m_CompilationState.m_DataObjectToAstNode.Insert(pEntryAstNode->m_pObject, pEntryAstNode);

  plHybridArray<AstNode*, 32> dataStack;

  while (objectStack.IsEmpty() == false)
  {
    auto& objCtx = objectStack.PeekBack();

    const plDocumentObject* pObject = objCtx.m_pObject;
    const plVisualScriptNodeRegistry::NodeDesc* pNodeDesc = objCtx.m_pNodeDesc;

    objectStack.PopBack();

    AstNode* pAstNode = nullptr;
    PL_VERIFY(m_CompilationState.m_DataObjectToAstNode.TryGetValue(pObject, pAstNode), "Implementation error");

    if (pAstNode != pEntryAstNode)
    {
      dataStack.PushBack(pAstNode);
    }

    m_NodeManager.GetInputDataPins(pObject, pins);
    plUInt32 uiNextInputPinIndex = 0;

    for (auto& pinDesc : pNodeDesc->m_InputPins)
    {
      if (pinDesc.IsExecutionPin())
        continue;

      AstNode* pAstNodeToAddInput = pAstNode;
      bool bArrayInput = false;
      if (pinDesc.m_bReplaceWithArray && pinDesc.m_sDynamicPinProperty.IsEmpty() == false)
      {
        const plAbstractProperty* pProp = pObject->GetType()->FindPropertyByName(pinDesc.m_sDynamicPinProperty);
        if (pProp == nullptr)
          return PL_FAILURE;

        if (pProp->GetCategory() == plPropertyCategory::Array)
        {
          auto pMakeArrayAstNode = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_MakeArray, true);
          pMakeArrayAstNode->m_pObject = pObject;
          AddDataOutput(*pMakeArrayAstNode, plVisualScriptDataType::Array);

          AddDataInput(*pAstNode, pMakeArrayAstNode, 0, plVisualScriptDataType::Array);

          dataStack.PushBack(pMakeArrayAstNode);

          pAstNodeToAddInput = pMakeArrayAstNode;
          bArrayInput = true;
        }
      }

      while (uiNextInputPinIndex < pins.GetCount())
      {
        auto pPin = pins[uiNextInputPinIndex];

        if (pPin->GetDynamicPinProperty() != pinDesc.m_sDynamicPinProperty)
          break;

        plVisualScriptDataType::Enum targetDataType = pPin->GetResolvedScriptDataType();
        if (targetDataType == plVisualScriptDataType::Invalid)
        {
          plLog::Error("Can't deduct type for pin '{}.{}'. The pin is not connected or all node properties are invalid.", GetNiceTypeName(pObject), pPin->GetName());
          return PL_FAILURE;
        }

        auto connections = m_NodeManager.GetConnections(*pPin);
        if (pPin->IsRequired() && targetDataType != plVisualScriptDataType::GameObject && connections.IsEmpty())
        {
          plLog::Error("Required input '{}' for '{}' is not connected", pPin->GetName(), GetNiceTypeName(pObject));
          return PL_FAILURE;
        }

        auto dataInputType = bArrayInput ? plVisualScriptDataType::Variant : FinalizeDataType(targetDataType);

        if (connections.IsEmpty())
        {
          if (plVisualScriptDataType::IsPointer(dataInputType))
          {
            auto defaultInput = GetOrCreateDefaultPointerNode(*pAstNode, pPin->GetDataType());
            AddDataInput(*pAstNodeToAddInput, defaultInput.m_pSourceNode, defaultInput.m_uiSourcePinIndex, defaultInput.m_DataOffset.GetType());

            if (defaultInput.m_pSourceNode != nullptr)
            {
              if (defaultInput.m_pSourceNode->m_ExecInputs.IsEmpty())
              {
                dataStack.RemoveAndCopy(defaultInput.m_pSourceNode);
                dataStack.PushBack(defaultInput.m_pSourceNode);
              }
            }
          }
          else
          {
            PL_SUCCEED_OR_RETURN(AddConstantDataInput(*pAstNodeToAddInput, pObject, pPin, dataInputType));
          }
        }
        else
        {
          auto& sourcePin = static_cast<const plVisualScriptPin&>(connections[0]->GetSourcePin());
          const plDocumentObject* pSourceObject = sourcePin.GetParent();
          auto pSourceNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pSourceObject->GetType());

          if (pSourceNodeDesc->m_Type == plVisualScriptNodeDescription::Type::GetScriptOwner)
          {
            AstNode* pSourceAstNode = m_CompilationState.m_pGetScriptOwnerNode;
            AddDataInput(*pAstNodeToAddInput, pSourceAstNode, sourcePin.GetDataPinIndex(), dataInputType);
          }
          else if (pSourceNodeDesc->m_Type == plVisualScriptNodeDescription::Type::Builtin_Constant)
          {
            plVariant value = pSourceObject->GetTypeAccessor().GetValue("Value");
            AddConstantDataInput(*pAstNodeToAddInput, value);
          }
          else
          {
            AstNode* pSourceAstNode;
            if (m_CompilationState.m_DataObjectToAstNode.TryGetValue(pSourceObject, pSourceAstNode) == false)
            {
              pSourceAstNode = CreateAstNodeFromObject(pSourceObject, pSourceNodeDesc, nullptr, true);
              if (pSourceAstNode != nullptr)
              {
                objectStack.PushBack({pSourceObject, pSourceNodeDesc});
              }
              else if (pSourceAstNode == nullptr)
              {
                if (m_CompilationState.m_ExecObjectToAstNode.TryGetValue(pSourceObject, pSourceAstNode) == false)
                {
                  plLog::Error("The source node '{}' is not executed in the current function.", GetNiceTypeName(pSourceObject));
                  return PL_FAILURE;
                }
              }
            }

            plVisualScriptDataType::Enum sourceDataType = sourcePin.GetResolvedScriptDataType();
            if (sourceDataType == plVisualScriptDataType::Invalid)
            {
              plLog::Error("Can't deduct type for pin '{}.{}'. The pin is not connected or all node properties are invalid.", GetNiceTypeName(pSourceObject), sourcePin.GetName());
              return PL_FAILURE;
            }

            if (sourcePin.CanConvertTo(*pPin) == false)
            {
              plLog::Error("Can't implicitly convert pin '{}.{}' of type '{}' connected to pin '{}.{}' of type '{}'", GetNiceTypeName(pSourceObject), sourcePin.GetName(), sourcePin.GetDataTypeName(), GetNiceTypeName(pObject), pPin->GetName(), pPin->GetDataTypeName());
              return PL_FAILURE;
            }

            AddDataInput(*pAstNodeToAddInput, pSourceAstNode, sourcePin.GetDataPinIndex(), dataInputType);
          }
        }

        ++uiNextInputPinIndex;
      }
    }

    m_NodeManager.GetOutputDataPins(pObject, pins);
    for (auto pPin : pins)
    {
      AddDataOutput(*pAstNode, FinalizeDataType(pPin->GetResolvedScriptDataType()));
    }
  }

  // Traverse in topological order and connect executions
  plHybridArray<AstNode*, 32> sortedDataStack;

  while (dataStack.IsEmpty() == false)
  {
    // Find next node with all data dependencies already in the sorted stack
    AstNode* nextNode = nullptr;
    for (plUInt32 i = dataStack.GetCount(); i-- > 0;)
    {
      AstNode* nextNodeCandiate = dataStack[i];
      const bool bValidCandidate = [&]()
      {
        m_NodeManager.GetInputDataPins(nextNodeCandiate->m_pObject, pins);
        for (auto pPin : pins)
        {
          auto connections = m_NodeManager.GetConnections(*pPin);
          for (auto pConnection : connections)
          {
            const plDocumentObject* pSourceObject = pConnection->GetSourcePin().GetParent();

            AstNode* pSourceAstNode = nullptr;
            if (!m_CompilationState.m_DataObjectToAstNode.TryGetValue(pSourceObject, pSourceAstNode))
            {
              // Not part of the data stack so we can ignore it
              continue;
            }

            if (!sortedDataStack.Contains(pSourceAstNode))
            {
              return false;
            }
          }
        }

        return true;
      }();

      if (bValidCandidate)
      {
        nextNode = nextNodeCandiate;
        dataStack.RemoveAtAndCopy(i);
        break;
      }
    }

    if (nextNode == nullptr)
    {
      PL_REPORT_FAILURE("Data connection corrupted or loop detected");
      return PL_FAILURE;
    }

    if (sortedDataStack.IsEmpty() == false)
    {
      ConnectExecution(*sortedDataStack.PeekBack(), *nextNode);
    }
    sortedDataStack.PushBack(nextNode);
  }

  if (sortedDataStack.IsEmpty())
  {
    out_pFirstDataNode = nullptr;
    out_pLastDataNode = nullptr;
  }
  else
  {
    out_pFirstDataNode = sortedDataStack[0];
    out_pLastDataNode = sortedDataStack.PeekBack();
  }

  return PL_SUCCESS;
}

plResult plVisualScriptCompiler::BuildDataExecutions(AstNode* pEntryAstNode)
{
  return TraverseAstDepthFirst(pEntryAstNode,
    [&](AstNode*& pAstNode)
    {
      if (pAstNode->m_bImplicitExecution)
        return VisitorResult::Continue;

      AstNode* pFirstDataNode = nullptr;
      AstNode* pLastDataNode = nullptr;
      if (BuildDataStack(pAstNode, pFirstDataNode, pLastDataNode).Failed())
        return VisitorResult::Error;

      if (pFirstDataNode == nullptr || pLastDataNode == nullptr)
        return VisitorResult::Continue;

      // Connect data stack
      ExecuteBefore(*pAstNode, *pFirstDataNode, *pLastDataNode);

      return VisitorResult::Continue;
    });
}

plResult plVisualScriptCompiler::InsertTypeConversions(AstNode* pEntryAstNode)
{
  auto InsertBuiltinTypeConversion = [&](AstNode& node, DataInput& dataInput, plVisualScriptDataType::Enum inputDataType, plVisualScriptDataType::Enum outputDataType) -> AstNode&
  {
    auto nodeType = plVisualScriptNodeDescription::Type::GetConversionType(inputDataType);

    auto& conversionNode = CreateAstNode(nodeType, outputDataType, true);
    AddDataInput(conversionNode, dataInput.m_pSourceNode, dataInput.m_uiSourcePinIndex, outputDataType);
    AddDataOutput(conversionNode, inputDataType);

    dataInput.m_pSourceNode = &conversionNode;
    dataInput.m_uiSourcePinIndex = 0;

    ExecuteBefore(node, conversionNode, conversionNode);

    return conversionNode;
  };

  return TraverseAstDepthFirst(pEntryAstNode,
    [&](AstNode*& pAstNode)
    {
      for (auto& dataInput : pAstNode->m_DataInputs)
      {
        if (dataInput.IsConnected() == false)
          continue;

        auto& dataOutput = GetDataOutputFromInput(dataInput);

        auto inputDataType = dataInput.m_DataOffset.GetType();
        auto outputDataType = dataOutput.m_DataOffset.GetType();

        if (dataOutput.m_DataOffset.GetType() != inputDataType)
        {
          if (plVisualScriptDataType::IsNumberOrBool(outputDataType) && inputDataType == plVisualScriptDataType::Vector3)
          {
            AstNode* pSourceNode = dataInput.m_pSourceNode;
            plUInt32 uiSourcePinIndex = dataInput.m_uiSourcePinIndex;
            if (outputDataType != plVisualScriptDataType::Float)
            {
              auto& conversionNode = InsertBuiltinTypeConversion(*pAstNode, dataInput, plVisualScriptDataType::Float, outputDataType);
              pSourceNode = &conversionNode;
              uiSourcePinIndex = 0;
            }

            auto& makeVec3Node = CreateAstNode(plVisualScriptNodeDescription::Type::ReflectedFunction, inputDataType, true);
            makeVec3Node.m_sTargetTypeName.Assign("plVec3");

            plVariantArray a;
            a.PushBack(plMakeHashedString("Make"));
            makeVec3Node.m_Value = a;

            AddDataInput(makeVec3Node, pSourceNode, uiSourcePinIndex, plVisualScriptDataType::Float);
            AddDataInput(makeVec3Node, pSourceNode, uiSourcePinIndex, plVisualScriptDataType::Float);
            AddDataInput(makeVec3Node, pSourceNode, uiSourcePinIndex, plVisualScriptDataType::Float);
            AddDataOutput(makeVec3Node, plVisualScriptDataType::Vector3);

            dataInput.m_pSourceNode = &makeVec3Node;
            dataInput.m_uiSourcePinIndex = 0;

            ExecuteBefore(*pAstNode, makeVec3Node, makeVec3Node);
          }
          else if (outputDataType == plVisualScriptDataType::Vector3 && inputDataType == plVisualScriptDataType::Transform)
          {
            auto& makeTransformNode = CreateAstNode(plVisualScriptNodeDescription::Type::ReflectedFunction, inputDataType, true);
            makeTransformNode.m_sTargetTypeName.Assign("plTransform");

            plVariantArray a;
            a.PushBack(plMakeHashedString("Make"));
            makeTransformNode.m_Value = a;

            AddDataInput(makeTransformNode, dataInput.m_pSourceNode, dataInput.m_uiSourcePinIndex, plVisualScriptDataType::Vector3);
            AddConstantDataInput(makeTransformNode, plQuat::MakeIdentity());
            AddConstantDataInput(makeTransformNode, plVec3(1));
            AddDataOutput(makeTransformNode, plVisualScriptDataType::Transform);

            dataInput.m_pSourceNode = &makeTransformNode;
            dataInput.m_uiSourcePinIndex = 0;

            ExecuteBefore(*pAstNode, makeTransformNode, makeTransformNode);
          }
          else
          {
            InsertBuiltinTypeConversion(*pAstNode, dataInput, inputDataType, outputDataType);
          }
        }
      }

      return VisitorResult::Continue;
    });
}

plResult plVisualScriptCompiler::ReplaceLoop(AstNode* pLoopNode)
{
  AstNode* pLoopInitStart = nullptr;
  AstNode* pLoopInitEnd = nullptr;

  AstNode* pLoopConditionStart = nullptr;
  AstNode* pLoopConditionEnd = nullptr;

  DataInput conditionDataInput;
  conditionDataInput.m_uiSourcePinIndex = 0;
  conditionDataInput.m_DataOffset.m_uiType = plVisualScriptDataType::Bool;

  AstNode* pLoopIncrement = nullptr;

  AstNode* pLoopElement = nullptr;
  AstNode* pLoopIndex = nullptr;

  AstNode* pLoopBody = pLoopNode->m_ExecOutputs[0].m_pTargetNode;
  AstNode* pLoopCompleted = pLoopNode->m_ExecOutputs[1].m_pTargetNode;

  auto loopType = pLoopNode->m_Type;

  if (loopType == plVisualScriptNodeDescription::Type::Builtin_WhileLoop)
  {
    conditionDataInput = pLoopNode->m_DataInputs[0];

    if (conditionDataInput.m_pSourceNode != nullptr)
    {
      pLoopConditionEnd = conditionDataInput.m_pSourceNode;

      pLoopConditionStart = conditionDataInput.m_pSourceNode;
      while (pLoopConditionStart->m_ExecInputs.GetCount() == 1 && pLoopConditionStart->m_ExecInputs[0].m_pSourceNode->m_bImplicitExecution)
      {
        pLoopConditionStart = pLoopConditionStart->m_ExecInputs[0].m_pSourceNode;
      }
    }
  }
  else if (loopType == plVisualScriptNodeDescription::Type::Builtin_ForLoop)
  {
    auto& firstIndexInput = pLoopNode->m_DataInputs[0];
    auto& lastIndexInput = pLoopNode->m_DataInputs[1];

    // Loop Init
    {
      pLoopInitStart = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_ToInt, plVisualScriptDataType::Int);
      pLoopInitStart->m_DataInputs.PushBack(firstIndexInput);
      AddDataOutput(*pLoopInitStart, plVisualScriptDataType::Int);

      pLoopInitEnd = pLoopInitStart;
      pLoopIndex = pLoopInitStart;
    }

    // Loop Condition
    {
      pLoopConditionStart = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Compare, plVisualScriptDataType::Int);
      pLoopConditionStart->m_Value = plInt64(plComparisonOperator::LessEqual);
      AddDataInput(*pLoopConditionStart, pLoopInitStart, 0, plVisualScriptDataType::Int);
      pLoopConditionStart->m_DataInputs.PushBack(lastIndexInput);
      AddDataOutput(*pLoopConditionStart, plVisualScriptDataType::Bool);

      pLoopConditionEnd = pLoopConditionStart;
    }

    // Loop Increment
    {
      pLoopIncrement = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Add, plVisualScriptDataType::Int);
      AddDataInput(*pLoopIncrement, pLoopIndex, 0, plVisualScriptDataType::Int);
      AddConstantDataInput(*pLoopIncrement, 1);

      // Dummy input that is not used at runtime but prevents the lastIndexInput from being re-used across the loop's lifetime
      pLoopIncrement->m_DataInputs.PushBack(lastIndexInput);

      // Ensure to write to the same local variable by re-using the loop index output id.
      auto& dataOutput = pLoopIncrement->m_DataOutputs.ExpandAndGetRef();
      dataOutput.m_DataOffset = pLoopIndex->m_DataOutputs[0].m_DataOffset;
    }
  }
  else if (loopType == plVisualScriptNodeDescription::Type::Builtin_ForEachLoop ||
           loopType == plVisualScriptNodeDescription::Type::Builtin_ReverseForEachLoop)
  {
    const bool isReverse = (loopType == plVisualScriptNodeDescription::Type::Builtin_ReverseForEachLoop);
    auto& arrayInput = pLoopNode->m_DataInputs[0];

    // Loop Init
    if (isReverse)
    {
      pLoopInitStart = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Array_GetCount);
      pLoopInitStart->m_DataInputs.PushBack(arrayInput);
      AddDataOutput(*pLoopInitStart, plVisualScriptDataType::Int);

      pLoopInitEnd = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Subtract, plVisualScriptDataType::Int);
      AddDataInput(*pLoopInitEnd, pLoopInitStart, 0, plVisualScriptDataType::Int);
      AddConstantDataInput(*pLoopInitEnd, 1);
      AddDataOutput(*pLoopInitEnd, plVisualScriptDataType::Int);

      ConnectExecution(*pLoopInitStart, *pLoopInitEnd);

      pLoopIndex = pLoopInitEnd;
    }
    else
    {
      pLoopInitStart = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_ToInt, plVisualScriptDataType::Int);
      AddConstantDataInput(*pLoopInitStart, 0);
      AddDataOutput(*pLoopInitStart, plVisualScriptDataType::Int);

      pLoopInitEnd = pLoopInitStart;

      pLoopIndex = pLoopInitStart;
    }

    // Loop Condition
    if (isReverse)
    {
      pLoopConditionStart = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Compare, plVisualScriptDataType::Int);
      pLoopConditionStart->m_Value = plInt64(plComparisonOperator::GreaterEqual);
      AddDataInput(*pLoopConditionStart, pLoopIndex, 0, plVisualScriptDataType::Int);
      AddConstantDataInput(*pLoopConditionStart, 0);
      AddDataOutput(*pLoopConditionStart, plVisualScriptDataType::Bool);

      pLoopConditionEnd = pLoopConditionStart;
    }
    else
    {
      pLoopConditionStart = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Array_GetCount);
      pLoopConditionStart->m_DataInputs.PushBack(arrayInput);
      AddDataOutput(*pLoopConditionStart, plVisualScriptDataType::Int);

      pLoopConditionEnd = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Compare, plVisualScriptDataType::Int);
      pLoopConditionEnd->m_Value = plInt64(plComparisonOperator::Less);
      AddDataInput(*pLoopConditionEnd, pLoopIndex, 0, plVisualScriptDataType::Int);
      AddDataInput(*pLoopConditionEnd, pLoopConditionStart, 0, plVisualScriptDataType::Int);
      AddDataOutput(*pLoopConditionEnd, plVisualScriptDataType::Bool);

      ConnectExecution(*pLoopConditionStart, *pLoopConditionEnd);
    }

    // Loop Increment
    {
      auto incType = isReverse ? plVisualScriptNodeDescription::Type::Builtin_Subtract : plVisualScriptNodeDescription::Type::Builtin_Add;

      pLoopIncrement = &CreateAstNode(incType, plVisualScriptDataType::Int);
      AddDataInput(*pLoopIncrement, pLoopIndex, 0, plVisualScriptDataType::Int);
      AddConstantDataInput(*pLoopIncrement, 1);

      // Dummy input that is not used at runtime but prevents the array from being re-used across the loop's lifetime
      pLoopIncrement->m_DataInputs.PushBack(arrayInput);

      // Ensure to write to the same local variable by re-using the loop index output id.
      auto& dataOutput = pLoopIncrement->m_DataOutputs.ExpandAndGetRef();
      dataOutput.m_DataOffset = pLoopIndex->m_DataOutputs[0].m_DataOffset;
    }

    pLoopElement = &CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Array_GetElement, plVisualScriptDataType::Invalid, true);
    pLoopElement->m_DataInputs.PushBack(arrayInput);
    AddDataInput(*pLoopElement, pLoopIndex, 0, plVisualScriptDataType::Int);
    AddDataOutput(*pLoopElement, plVisualScriptDataType::Variant);
  }
  else
  {
    PL_ASSERT_NOT_IMPLEMENTED;
  }

  {
    if (conditionDataInput.m_pSourceNode == nullptr && conditionDataInput.m_DataOffset.IsLocal())
    {
      conditionDataInput.m_pSourceNode = pLoopConditionEnd;
    }

    auto& branchNode = CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Branch);
    branchNode.m_DataInputs.PushBack(conditionDataInput);

    ReplaceExecution(*pLoopNode, branchNode);

    if (pLoopConditionStart == nullptr)
    {
      pLoopConditionStart = &branchNode;
    }
    else if (pLoopConditionEnd->m_ExecOutputs.IsEmpty())
    {
      ExecuteBefore(branchNode, *pLoopConditionStart, *pLoopConditionEnd);
    }

    if (pLoopInitStart != nullptr)
    {
      ExecuteBefore(*pLoopConditionStart, *pLoopInitStart, *pLoopInitEnd);
    }

    if (pLoopElement != nullptr && pLoopBody != nullptr)
    {
      ExecuteBefore(*pLoopBody, *pLoopElement, *pLoopElement);
    }
  }

  AstNode* pJumpNode = &CreateJumpNode(pLoopConditionStart);
  if (pLoopIncrement != nullptr)
  {
    ConnectExecution(*pLoopIncrement, *pJumpNode);
    pJumpNode = pLoopIncrement;
  }

  plHybridArray<AstNode*, 8> nodesConnectedToBreak;

  if (TraverseAstDepthFirst(pLoopBody,
        [&](AstNode*& pAstNode)
        {
          PL_ASSERT_DEV(plVisualScriptNodeDescription::Type::IsLoop(pAstNode->m_Type) == false, "Nested Loops should have been resolved already");

          for (plUInt32 i = 0; i < pAstNode->m_ExecOutputs.GetCount(); ++i)
          {
            auto& execOutput = pAstNode->m_ExecOutputs[i];
            if (execOutput.m_pTargetNode == nullptr)
            {
              ConnectExecution(*pAstNode, *pJumpNode, i);
            }
            else if (execOutput.m_pTargetNode->m_Type == plVisualScriptNodeDescription::Type::Builtin_Break)
            {
              // handle breaks after this traversal, otherwise we could end up traversing to nodes outside the loop
              nodesConnectedToBreak.PushBack(pAstNode);
            }
          }

          for (auto& dataInput : pAstNode->m_DataInputs)
          {
            if (loopType == plVisualScriptNodeDescription::Type::Builtin_ForEachLoop ||
                loopType == plVisualScriptNodeDescription::Type::Builtin_ReverseForEachLoop)
            {
              if (dataInput.m_pSourceNode == pLoopNode && dataInput.m_uiSourcePinIndex == 0)
              {
                dataInput.m_pSourceNode = pLoopElement;
              }
              else if (dataInput.m_pSourceNode == pLoopNode && dataInput.m_uiSourcePinIndex == 1)
              {
                dataInput.m_pSourceNode = pLoopIndex;
                dataInput.m_uiSourcePinIndex = 0;
              }
            }
            else
            {
              if (dataInput.m_pSourceNode == pLoopNode && dataInput.m_uiSourcePinIndex == 0)
              {
                dataInput.m_pSourceNode = pLoopIndex;
              }
            }
          }

          return VisitorResult::Continue;
        })
        .Failed())
  {
    return PL_FAILURE;
  }

  // Go through all loop body nodes and check if they have data connections to nodes outside the loop.
  // If so add the data input to the jump node to prevent register re-use inside the loop.
  m_CompilationState.m_VisitedNodes.Insert(pLoopBody);
  for (auto pAstNode : m_CompilationState.m_VisitedNodes)
  {
    if (pAstNode == pLoopIncrement)
      continue;

    for (auto& dataInput : pAstNode->m_DataInputs)
    {
      auto pSourceNode = dataInput.m_pSourceNode;
      if (pSourceNode == nullptr || pSourceNode == pLoopIndex || pSourceNode == pLoopElement)
        continue;

      if (!m_CompilationState.m_VisitedNodes.Contains(dataInput.m_pSourceNode) && pJumpNode->m_DataInputs.Contains(dataInput) == false)
      {
        pJumpNode->m_DataInputs.PushBack(dataInput);
      }
    }
  }

  // Remove break nodes from the execution flow and connect them to the loop completed node
  for (auto pAstNode : nodesConnectedToBreak)
  {
    for (plUInt32 i = 0; i < pAstNode->m_ExecOutputs.GetCount(); ++i)
    {
      auto& execOutput = pAstNode->m_ExecOutputs[i];
      if (execOutput.m_pTargetNode->m_Type == plVisualScriptNodeDescription::Type::Builtin_Break)
      {
        DisconnectExecution(*pAstNode, *execOutput.m_pTargetNode, i);
        if (pLoopCompleted != nullptr)
        {
          ConnectExecution(*pAstNode, *pLoopCompleted, i);
        }
      }
    }
  }

  return PL_SUCCESS;
}

plResult plVisualScriptCompiler::ReplaceUnsupportedNodes(AstNode* pEntryAstNode)
{
  plHybridArray<AstNode*, 64> unsupportedNodes;

  if (TraverseAstDepthFirst(pEntryAstNode,
        [&](AstNode*& pAstNode)
        {
          if (plVisualScriptNodeDescription::Type::IsLoop(pAstNode->m_Type) || pAstNode->m_Type == plVisualScriptNodeDescription::Type::Builtin_CompareExec)
          {
            PL_ASSERT_DEBUG(unsupportedNodes.Contains(pAstNode) == false, "");
            unsupportedNodes.PushBack(pAstNode);
          }

          return VisitorResult::Continue;
        })
        .Failed())
  {
    return PL_FAILURE;
  }

  // Replace unsupported nodes backwards so that we replace inner loops first, order doesn't matter for the other nodes
  for (plUInt32 i = unsupportedNodes.GetCount(); i-- > 0;)
  {
    AstNode* pAstNode = unsupportedNodes[i];

    if (pAstNode->m_Type == plVisualScriptNodeDescription::Type::Builtin_CompareExec)
    {
      auto& compareNode = CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Compare, pAstNode->m_DeductedDataType, true);
      compareNode.m_Value = pAstNode->m_Value;
      compareNode.m_DataInputs = pAstNode->m_DataInputs;
      AddDataOutput(compareNode, plVisualScriptDataType::Bool);

      auto& branchNode = CreateAstNode(plVisualScriptNodeDescription::Type::Builtin_Branch);
      AddDataInput(branchNode, &compareNode, 0, plVisualScriptDataType::Bool);

      ReplaceExecution(*pAstNode, branchNode);
      ExecuteBefore(branchNode, compareNode, compareNode);
    }
    else if (plVisualScriptNodeDescription::Type::IsLoop(pAstNode->m_Type))
    {
      PL_SUCCEED_OR_RETURN(ReplaceLoop(pAstNode));
    }
    else
    {
      PL_ASSERT_NOT_IMPLEMENTED;
    }
  }

  return PL_SUCCESS;
}

plResult plVisualScriptCompiler::AssignInstanceVariables(AstNode* pEntryAstNode)
{
  return TraverseAstDepthFirst(pEntryAstNode,
    [&](AstNode*& pAstNode)
    {
      for (auto& dataInput : pAstNode->m_DataInputs)
      {
        if (dataInput.IsConnectedAndLocal() == false)
          continue;

        auto pSourceNode = dataInput.m_pSourceNode;
        PL_ASSERT_DEBUG(pSourceNode != nullptr, "Data input source node should be always valid for local inputs");

        if (pSourceNode->m_Type == plVisualScriptNodeDescription::Type::Builtin_GetVariable)
        {
          auto& dataOutput = GetDataOutputFromInput(dataInput);

          plHashedString sName;
          sName.Assign(pSourceNode->m_Value.Get<plString>());

          dataInput.m_pSourceNode = nullptr;
          dataInput.m_uiSourcePinIndex = 0;
          dataInput.m_DataOffset = GetInstanceDataOffset(sName, dataOutput.m_DataOffset.GetType());

          // Remove the GetVariable node from the execution flow
          if (pSourceNode->m_ExecOutputs.IsEmpty() == false)
          {
            AstNode* pNodeAfterGetVariable = pSourceNode->m_ExecOutputs[0].m_pTargetNode;
            pNodeAfterGetVariable->m_ExecInputs.Clear();

            for (auto& execInput : pSourceNode->m_ExecInputs)
            {
              ConnectExecution(*execInput.m_pSourceNode, *pNodeAfterGetVariable, execInput.m_uiSourcePinIndex);
            }

            pSourceNode->m_ExecInputs.Clear();
            pSourceNode->m_ExecOutputs.Clear();
          }
        }
      }

      if (pAstNode->m_Type == plVisualScriptNodeDescription::Type::Builtin_SetVariable ||
          pAstNode->m_Type == plVisualScriptNodeDescription::Type::Builtin_IncVariable ||
          pAstNode->m_Type == plVisualScriptNodeDescription::Type::Builtin_DecVariable)
      {
        plHashedString sName;
        sName.Assign(pAstNode->m_Value.Get<plString>());

        DataOffset dataOffset = GetInstanceDataOffset(sName, pAstNode->m_DeductedDataType);

        if (pAstNode->m_Type != plVisualScriptNodeDescription::Type::Builtin_SetVariable)
        {
          if (pAstNode->m_DataInputs.IsEmpty())
          {
            auto& dataInput = pAstNode->m_DataInputs.ExpandAndGetRef();
            dataInput.m_DataOffset = dataOffset;
          }
        }

        PL_ASSERT_DEBUG(pAstNode->m_DataOutputs.GetCount() > 0, "");
        auto& dataOutput = pAstNode->m_DataOutputs[0];
        dataOutput.m_DataOffset = dataOffset;
      }

      return VisitorResult::Continue;
    });
}

plResult plVisualScriptCompiler::AssignLocalVariables(AstNode* pEntryAstNode, plVisualScriptDataDescription& inout_localDataDesc)
{
  m_CompilationState.m_LiveLocalVars.Reserve(m_CompilationState.m_uiNextLocalVarId);
  for (plUInt32 i = 0; i < m_CompilationState.m_uiNextLocalVarId; ++i)
  {
    auto& liveLocalVar = m_CompilationState.m_LiveLocalVars.ExpandAndGetRef();
    liveLocalVar.m_uiId = i;
  }

  plUInt32 uiNodeIndex = 0;

  plResult r = TraverseAstTopologicalOrder(pEntryAstNode,
    [&](const AstNode* pAstNode)
    {
      for (auto& dataInput : pAstNode->m_DataInputs)
      {
        if (dataInput.IsConnectedAndLocal() == false)
          continue;

        const plUInt32 id = dataInput.m_DataOffset.m_uiByteOffset;
        auto& liveLocalVar = m_CompilationState.m_LiveLocalVars[id];

        liveLocalVar.m_uiEnd = plMath::Max(liveLocalVar.m_uiEnd, uiNodeIndex);
      }

      for (auto& dataOutput : pAstNode->m_DataOutputs)
      {
        if (dataOutput.IsValidAndLocal() == false)
          continue;

        const plUInt32 id = dataOutput.m_DataOffset.m_uiByteOffset;
        auto& liveLocalVar = m_CompilationState.m_LiveLocalVars[id];

        liveLocalVar.m_uiId = id;
        liveLocalVar.m_DataOffset = dataOutput.m_DataOffset;
        liveLocalVar.m_uiStart = plMath::Min(liveLocalVar.m_uiStart, uiNodeIndex);
      }

      ++uiNodeIndex;
      return VisitorResult::Continue;
    });
  if (r.Failed())
    return PL_FAILURE;

  // This is an implementation of the linear scan register allocation algorithm without spilling
  // https://www2.seas.gwu.edu/~hchoi/teaching/cs160d/linearscan.pdf

  // Sort lifetime by start index
  m_CompilationState.m_LiveLocalVars.Sort(
    [](const LiveLocalVar& a, const LiveLocalVar& b)
    {
      if (a.m_uiStart != b.m_uiStart)
        return a.m_uiStart < b.m_uiStart;

      return a.m_uiId < b.m_uiId;
    });

  // Assign local vars
  plHybridArray<LiveLocalVar, 64> activeIntervals;
  plHybridArray<DataOffset, 64> freeDataOffsets;

  for (auto& liveInterval : m_CompilationState.m_LiveLocalVars)
  {
    // Expire old intervals with less comparison instead of less equal (as in the original paper) so we don't end up using the same data as input and output
    for (plUInt32 uiActiveIndex = activeIntervals.GetCount(); uiActiveIndex-- > 0;)
    {
      auto& activeInterval = activeIntervals[uiActiveIndex];
      if (activeInterval.m_uiEnd < liveInterval.m_uiStart)
      {
        freeDataOffsets.PushBack(activeInterval.m_DataOffset);

        activeIntervals.RemoveAtAndCopy(uiActiveIndex);
      }
    }

    // Unused var
    if (liveInterval.m_uiEnd <= liveInterval.m_uiStart)
    {
      liveInterval.m_DataOffset = {};
      continue;
    }

    // Allocate local var
    {
      DataOffset dataOffset;
      dataOffset.m_uiType = liveInterval.m_DataOffset.m_uiType;

      for (plUInt32 i = 0; i < freeDataOffsets.GetCount(); ++i)
      {
        auto freeDataOffset = freeDataOffsets[i];
        if (freeDataOffset.m_uiType == dataOffset.m_uiType)
        {
          dataOffset = freeDataOffset;
          freeDataOffsets.RemoveAtAndSwap(i);
          break;
        }
      }

      if (dataOffset.IsValid() == false)
      {
        PL_ASSERT_DEBUG(dataOffset.GetType() < plVisualScriptDataType::Count, "Invalid data type");
        auto& offsetAndCount = inout_localDataDesc.m_PerTypeInfo[dataOffset.m_uiType];
        dataOffset.m_uiByteOffset = offsetAndCount.m_uiCount;
        ++offsetAndCount.m_uiCount;
      }

      liveInterval.m_DataOffset = dataOffset;
    }

    activeIntervals.PushBack(liveInterval);
  }

  // Sort by id and copy back to outputs and inputs
  m_CompilationState.m_LiveLocalVars.Sort([](const LiveLocalVar& a, const LiveLocalVar& b)
    { return a.m_uiId < b.m_uiId; });

  return TraverseAstDepthFirst(pEntryAstNode,
    [&](AstNode*& pAstNode)
    {
      for (auto& dataInput : pAstNode->m_DataInputs)
      {
        if (dataInput.IsConnectedAndLocal() == false)
          continue;

        const plUInt32 id = dataInput.m_DataOffset.m_uiByteOffset;
        PL_ASSERT_DEBUG(m_CompilationState.m_LiveLocalVars[id].m_uiId == id, "");
        dataInput.m_DataOffset = m_CompilationState.m_LiveLocalVars[id].m_DataOffset;
      }

      for (auto& dataOutput : pAstNode->m_DataOutputs)
      {
        if (dataOutput.IsValidAndLocal() == false)
          continue;

        const plUInt32 id = dataOutput.m_DataOffset.m_uiByteOffset;
        PL_ASSERT_DEBUG(m_CompilationState.m_LiveLocalVars[id].m_uiId == id, "");
        dataOutput.m_DataOffset = m_CompilationState.m_LiveLocalVars[id].m_DataOffset;
      }

      return VisitorResult::Continue;
    });
}

plResult plVisualScriptCompiler::CopyOutputsToInputs(AstNode* pEntryAstNode)
{
  return TraverseAstDepthFirst(pEntryAstNode,
    [&](AstNode*& pAstNode)
    {
      for (auto& dataInput : pAstNode->m_DataInputs)
      {
        if (dataInput.IsConnected() == false)
          continue;

        auto& dataOutput = GetDataOutputFromInput(dataInput);
        dataInput.m_DataOffset.m_uiByteOffset = dataOutput.m_DataOffset.m_uiByteOffset;
        dataInput.m_DataOffset.m_uiSource = dataOutput.m_DataOffset.m_uiSource;
        PL_ASSERT_DEBUG(dataInput.m_DataOffset.GetType() == plVisualScriptDataType::Variant || dataInput.m_DataOffset.GetType() == dataOutput.m_DataOffset.GetType(), "");
      }

      return VisitorResult::Continue;
    });
}

plResult plVisualScriptCompiler::BuildNodeDescriptions(AstNode* pEntryAstNode, plDynamicArray<plVisualScriptNodeDescription>& out_NodeDescriptions)
{
  plHashTable<const AstNode*, plUInt32> astNodeToNodeDescIndices;
  out_NodeDescriptions.Clear();

  auto CreateNodeDesc = [&](const AstNode& astNode, plUInt32& out_uiNodeDescIndex) -> plResult
  {
    out_uiNodeDescIndex = out_NodeDescriptions.GetCount();

    auto& nodeDesc = out_NodeDescriptions.ExpandAndGetRef();
    nodeDesc.m_Type = astNode.m_Type;
    nodeDesc.m_DeductedDataType = astNode.m_DeductedDataType;
    nodeDesc.m_sTargetTypeName = astNode.m_sTargetTypeName;
    nodeDesc.m_Value = astNode.m_Value;

    for (auto& dataInput : astNode.m_DataInputs)
    {
      nodeDesc.m_InputDataOffsets.PushBack(dataInput.m_DataOffset);
    }

    for (auto& dataOutput : astNode.m_DataOutputs)
    {
      nodeDesc.m_OutputDataOffsets.PushBack(dataOutput.m_DataOffset);
    }

    nodeDesc.m_ExecutionIndices.SetCount(astNode.m_ExecOutputs.GetCount(), plSmallInvalidIndex);

    PL_VERIFY(astNodeToNodeDescIndices.Insert(&astNode, out_uiNodeDescIndex) == false, "");
    return PL_SUCCESS;
  };

  return TraverseAstTopologicalOrder(pEntryAstNode,
    [&](const AstNode* pAstNode)
    {
      plUInt32 uiTargetIndex = 0;

      // Jump nodes should not end up in the final node descriptions
      if (pAstNode->m_Type == plVisualScriptNodeDescription::Type::Builtin_Jump)
      {
        plUInt64 uiPtr = pAstNode->m_Value.Get<plUInt64>();
        const AstNode* pTargetAstNode = *reinterpret_cast<const AstNode**>(&uiPtr);

        if (astNodeToNodeDescIndices.TryGetValue(pTargetAstNode, uiTargetIndex) == false)
          return VisitorResult::Error;
      }
      else
      {
        if (CreateNodeDesc(*pAstNode, uiTargetIndex).Failed())
          return VisitorResult::Error;
      }

      for (auto& execInput : pAstNode->m_ExecInputs)
      {
        if (execInput.m_pSourceNode == nullptr)
          continue;

        plUInt32 uiSourceIndex = 0;
        PL_VERIFY(astNodeToNodeDescIndices.TryGetValue(execInput.m_pSourceNode, uiSourceIndex), "Topological sort failed");

        auto pSourceNodeDesc = &out_NodeDescriptions[uiSourceIndex];
        pSourceNodeDesc->m_ExecutionIndices[execInput.m_uiSourcePinIndex] = uiTargetIndex;
      }

      return VisitorResult::Continue;
    });
}

plResult plVisualScriptCompiler::FinalizeConstantData()
{
  m_Module.m_ConstantDataDesc.CalculatePerTypeStartOffsets();
  m_Module.m_ConstantDataStorage.AllocateStorage(plFoundation::GetDefaultAllocator());

  for (auto& it : m_Module.m_ConstantDataToIndex)
  {
    const plVariant& value = it.Key();
    plUInt32 uiIndex = it.Value();

    auto scriptDataType = plVisualScriptDataType::FromVariantType(value.GetType());
    if (scriptDataType == plVisualScriptDataType::Invalid)
    {
      scriptDataType = plVisualScriptDataType::Variant;
    }

    auto dataOffset = m_Module.m_ConstantDataDesc.GetOffset(scriptDataType, uiIndex, DataOffset::Source::Constant);

    m_Module.m_ConstantDataStorage.SetDataFromVariant(dataOffset, value, 0);
  }

  return PL_SUCCESS;
}

plResult plVisualScriptCompiler::TraverseAstDepthFirst(AstNode* pEntryAstNode, plDelegate<VisitorResult(AstNode*& pAstNode)> func)
{
  if (pEntryAstNode == nullptr)
    return PL_SUCCESS;

  m_CompilationState.m_VisitedNodes.Clear();

  plHybridArray<AstNode*, 64> nodeStack;
  nodeStack.PushBack(pEntryAstNode);

  while (nodeStack.IsEmpty() == false)
  {
    AstNode* pAstNode = nodeStack.PeekBack();
    nodeStack.PopBack();

    AstNode* pOldAstNode = pAstNode;

    VisitorResult r = func(pAstNode);
    if (r == VisitorResult::Error)
      return PL_FAILURE;

    // Check whether the node has been replaced by a new one and if so mark the new one as visited as well
    if (pAstNode != pOldAstNode)
    {
      PL_VERIFY(m_CompilationState.m_VisitedNodes.Insert(pAstNode) == false, "");
    }

    for (plUInt32 i = 0; i < pAstNode->m_ExecOutputs.GetCount(); ++i)
    {
      auto& execOutput = pAstNode->m_ExecOutputs[i];

      if (execOutput.m_pTargetNode == nullptr)
        continue;
      if (m_CompilationState.m_VisitedNodes.Insert(execOutput.m_pTargetNode))
        continue;

#if PL_ENABLED(PL_COMPILE_FOR_DEBUG)
      bool bExecInputFound = false;
      for (auto& execInput : execOutput.m_pTargetNode->m_ExecInputs)
      {
        if (execInput.m_pSourceNode == pAstNode && execInput.m_uiSourcePinIndex == i)
        {
          bExecInputFound = true;
          break;
        }
      }
      PL_ASSERT_DEBUG(bExecInputFound, "Execution connection corrupted");
#endif

      nodeStack.PushBack(execOutput.m_pTargetNode);
    }
  }

  return PL_SUCCESS;
}

plResult plVisualScriptCompiler::TraverseAstTopologicalOrder(const AstNode* pEntryAstNode, plDelegate<VisitorResult(const AstNode* pAstNode)> func)
{
  if (pEntryAstNode == nullptr)
    return PL_SUCCESS;

  m_CompilationState.m_VisitedNodes.Clear();

  plHybridArray<const AstNode*, 64> nodeStack;
  nodeStack.PushBack(pEntryAstNode);

  while (nodeStack.IsEmpty() == false)
  {
    // Find next node with all execution dependencies visited
    const AstNode* pAstNode = nullptr;
    for (plUInt32 i = nodeStack.GetCount(); i-- > 0;)
    {
      const AstNode* pCandidateAstNode = nodeStack[i];
      bool bAllVisited = true;
      for (auto& execInput : pCandidateAstNode->m_ExecInputs)
      {
        if (execInput.m_pSourceNode != nullptr && m_CompilationState.m_VisitedNodes.Contains(execInput.m_pSourceNode) == false)
        {
          bAllVisited = false;
          break;
        }
      }

      if (bAllVisited)
      {
        pAstNode = pCandidateAstNode;
        nodeStack.RemoveAtAndCopy(i);
        break;
      }
    }

    if (pAstNode == nullptr)
    {
      PL_REPORT_FAILURE("Execution connection corrupted or loop detected");
      return PL_FAILURE;
    }

    PL_VERIFY(m_CompilationState.m_VisitedNodes.Insert(pAstNode) == false, "");

    VisitorResult r = func(pAstNode);
    if (r == VisitorResult::Error)
      return PL_FAILURE;

    // Since we use a stack we need to iterate the execution outputs backwards to ensure that they are processed in order.
    // Strictly speaking this is not necessary but improves data locality especially for loops,
    // which in the end results in shorter variable lifetimes and thus in fewer local variables.
    for (plUInt32 i = pAstNode->m_ExecOutputs.GetCount(); i-- > 0;)
    {
      auto& execOutput = pAstNode->m_ExecOutputs[i];

      if (execOutput.m_pTargetNode == nullptr)
        continue;
      if (m_CompilationState.m_VisitedNodes.Contains(execOutput.m_pTargetNode))
        continue;

      if (nodeStack.Contains(execOutput.m_pTargetNode) == false)
      {
        nodeStack.PushBack(execOutput.m_pTargetNode);
      }
    }
  }

  return PL_SUCCESS;
}

void plVisualScriptCompiler::DumpAST(AstNode* pEntryAstNode, plStringView sOutputPath, plStringView sFunctionName, plStringView sSuffix)
{
  if (sOutputPath.IsEmpty())
    return;

  plDGMLGraph dgmlGraph;
  {
    plHashTable<const AstNode*, plUInt32> nodeCache;
    plHashTable<plUInt64, plString> connectionCache;
    plStringBuilder sb;
    plUInt32 uiNodeIndex = 0;

    // First build all the dgml graph nodes
    TraverseAstTopologicalOrder(pEntryAstNode,
      [&](const AstNode* pAstNode)
      {
        plStringView sTypeName = plVisualScriptNodeDescription::Type::GetName(pAstNode->m_Type);
        sb.SetFormat("{} (id: {})", sTypeName, uiNodeIndex);
        if (pAstNode->m_sTargetTypeName.IsEmpty() == false)
        {
          sb.Append("\n", pAstNode->m_sTargetTypeName);
        }
        if (pAstNode->m_DeductedDataType != plVisualScriptDataType::Invalid)
        {
          sb.Append("\nDataType: ", plVisualScriptDataType::GetName(pAstNode->m_DeductedDataType));
        }
        sb.AppendFormat("\nImplicitExec: {}", pAstNode->m_bImplicitExecution);
        if (pAstNode->m_Value.IsValid())
        {
          sb.AppendFormat("\nValue: {}", pAstNode->m_Value);
        }

        float colorX = plSimdRandom::FloatZeroToOne(plSimdVec4i(plHashingUtils::StringHash(sTypeName))).x();

        plDGMLGraph::NodeDesc nd;
        nd.m_Color = plColorScheme::LightUI(colorX);
        plUInt32 uiGraphNode = dgmlGraph.AddNode(sb, &nd);
        PL_VERIFY(nodeCache.Insert(pAstNode, uiGraphNode) == false, "");

        ++uiNodeIndex;
        return VisitorResult::Continue;
      })
      .IgnoreResult();

    // Then build all the connections
    for (auto it : nodeCache)
    {
      const AstNode* pAstNode = it.Key();
      const plUInt32 uiGraphNode = it.Value();

      for (auto& execInput : pAstNode->m_ExecInputs)
      {
        plUInt32 uiSourceGraphNode = 0;
        PL_VERIFY(nodeCache.TryGetValue(execInput.m_pSourceNode, uiSourceGraphNode), "");

        plUInt64 uiConnectionKey = uiSourceGraphNode | plUInt64(uiGraphNode) << 32;
        plString& sLabel = connectionCache[uiConnectionKey];

        plStringBuilder sb = sLabel;
        if (sb.IsEmpty() == false)
        {
          sb.Append(" + ");
        }
        sb.AppendFormat("Exec{}", execInput.m_uiSourcePinIndex);
        sLabel = sb;
      }

      for (plUInt32 i = 0; i < pAstNode->m_DataInputs.GetCount(); ++i)
      {
        auto& dataInput = pAstNode->m_DataInputs[i];
        if (dataInput.m_pSourceNode == nullptr)
          continue;

        // Exclude GetScriptOwner connections as they create too much noise
        if (dataInput.m_pSourceNode->m_Type == plVisualScriptNodeDescription::Type::GetScriptOwner)
          continue;

        auto& dataOutput = GetDataOutputFromInput(dataInput);

        plUInt32 uiSourceGraphNode = 0;
        PL_VERIFY(nodeCache.TryGetValue(dataInput.m_pSourceNode, uiSourceGraphNode), "");

        plUInt64 uiConnectionKey = uiSourceGraphNode | plUInt64(uiGraphNode) << 32;
        plString& sLabel = connectionCache[uiConnectionKey];

        plStringBuilder sb = sLabel;
        if (sb.IsEmpty() == false)
        {
          sb.Append(" + ");
        }
        const plUInt32 uiOutputId = dataOutput.m_DataOffset.m_uiByteOffset;
        const plUInt32 uiInputId = dataInput.m_DataOffset.m_uiByteOffset;
        sb.AppendFormat("o{}:{} (id: {})->i{}:{} (id: {})", dataInput.m_uiSourcePinIndex, plVisualScriptDataType::GetName(dataOutput.m_DataOffset.GetType()), uiOutputId, i, plVisualScriptDataType::GetName(dataInput.m_DataOffset.GetType()), uiInputId);
        sLabel = sb;
      }
    }

    for (auto& it : connectionCache)
    {
      plUInt32 uiSource = it.Key() & 0xFFFFFFFF;
      plUInt32 uiTarget = it.Key() >> 32;

      dgmlGraph.AddConnection(uiSource, uiTarget, it.Value());
    }
  }

  plStringView sExt = sOutputPath.GetFileExtension();
  plStringBuilder sFullPath;
  sFullPath.Append(sOutputPath.GetFileDirectory(), sOutputPath.GetFileName(), "_", sFunctionName, sSuffix);
  sFullPath.Append(".", sExt);

  plDGMLGraphWriter dgmlGraphWriter;
  if (dgmlGraphWriter.WriteGraphToFile(sFullPath, dgmlGraph).Succeeded())
  {
    plLog::Info("AST was dumped to: {}", sFullPath);
  }
  else
  {
    plLog::Error("Failed to dump AST to: {}", sFullPath);
  }
}

void plVisualScriptCompiler::DumpGraph(plArrayPtr<const plVisualScriptNodeDescription> nodeDescriptions, plStringView sOutputPath, plStringView sFunctionName, plStringView sSuffix)
{
  if (sOutputPath.IsEmpty())
    return;

  plDGMLGraph dgmlGraph;
  {
    plStringBuilder sTmp;
    for (plUInt32 i = 0; i < nodeDescriptions.GetCount(); ++i)
    {
      auto& nodeDesc = nodeDescriptions[i];

      plStringView sTypeName = plVisualScriptNodeDescription::Type::GetName(nodeDesc.m_Type);
      sTmp = sTypeName;

      nodeDesc.AppendUserDataName(sTmp);

      sTmp.AppendFormat(" (id: {})", i);

      for (auto& dataOffset : nodeDesc.m_InputDataOffsets)
      {
        sTmp.AppendFormat("\n Input {} {}[{}]", DataOffset::Source::GetName(dataOffset.GetSource()), plVisualScriptDataType::GetName(dataOffset.GetType()), dataOffset.m_uiByteOffset);

        if (dataOffset.GetSource() == DataOffset::Source::Constant)
        {
          for (auto& it : m_Module.m_ConstantDataToIndex)
          {
            auto scriptDataType = plVisualScriptDataType::FromVariantType(it.Key().GetType());
            if (scriptDataType == dataOffset.GetType() && it.Value() == dataOffset.m_uiByteOffset)
            {
              sTmp.AppendFormat(" ({})", it.Key());
              break;
            }
          }
        }
      }

      for (auto& dataOffset : nodeDesc.m_OutputDataOffsets)
      {
        sTmp.AppendFormat("\n Output {} {}[{}]", DataOffset::Source::GetName(dataOffset.GetSource()), plVisualScriptDataType::GetName(dataOffset.GetType()), dataOffset.m_uiByteOffset);
      }

      float colorX = plSimdRandom::FloatZeroToOne(plSimdVec4i(plHashingUtils::StringHash(sTypeName))).x();

      plDGMLGraph::NodeDesc nd;
      nd.m_Color = plColorScheme::LightUI(colorX);

      dgmlGraph.AddNode(sTmp, &nd);
    }

    for (plUInt32 uiCurrentIndex = 0; uiCurrentIndex < nodeDescriptions.GetCount(); ++uiCurrentIndex)
    {
      auto& executionIndices = nodeDescriptions[uiCurrentIndex].m_ExecutionIndices;
      for (plUInt32 uiExecIndex = 0; uiExecIndex < executionIndices.GetCount(); ++uiExecIndex)
      {
        const plUInt32 uiNextIndex = executionIndices[uiExecIndex];
        if (uiNextIndex == plSmallInvalidIndex)
          continue;

        sTmp.SetFormat("Exec{}", uiExecIndex);
        dgmlGraph.AddConnection(uiCurrentIndex, uiNextIndex, sTmp);
      }
    }
  }

  plStringView sExt = sOutputPath.GetFileExtension();
  plStringBuilder sFullPath;
  sFullPath.Append(sOutputPath.GetFileDirectory(), sOutputPath.GetFileName(), "_", sFunctionName, sSuffix);
  sFullPath.Append(".", sExt);

  plDGMLGraphWriter dgmlGraphWriter;
  if (dgmlGraphWriter.WriteGraphToFile(sFullPath, dgmlGraph).Succeeded())
  {
    plLog::Info("AST was dumped to: {}", sFullPath);
  }
  else
  {
    plLog::Error("Failed to dump AST to: {}", sFullPath);
  }
}
