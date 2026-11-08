#include <EditorPluginAssets/EditorPluginAssetsPCH.h>

#include <Core/World/World.h>
#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorPluginVisualScript/VisualScriptClassAsset/VisualScriptClassAsset.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraph.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptVariable.h>
#include <EditorPluginVisualScript/VisualScriptLibraryAsset/VisualScriptLibraryMetaData.h>
#include <Foundation/SimdMath/SimdRandom.h>
#include <Foundation/Utilities/DGMLWriter.h>
#include <GuiFoundation/UIServices/DynamicStringEnum.h>

namespace
{
  constexpr plUInt32 s_uiMaxTransparentDepth = 256;

  plString GetStringProperty(const plDocumentObject* pObject, plStringView sProperty)
  {
    if (pObject == nullptr)
      return {};

    plVariant value = pObject->GetTypeAccessor().GetValue(sProperty);
    if (value.IsA<plString>())
      return value.Get<plString>();
    if (value.IsA<plHashedString>())
      return plString(value.Get<plHashedString>().GetView());
    return {};
  }
} // namespace

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptPin, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plVisualScriptPin::plVisualScriptPin(Type type, plStringView sName, const plVisualScriptNodeRegistry::PinDesc& pinDesc, const plDocumentObject* pObject, plUInt32 uiDataPinIndex, plUInt32 uiElementIndex)
  : plVisualGraphPin(type, sName, pinDesc.GetColor(), pObject)
  , m_pDesc(&pinDesc)
  , m_uiDataPinIndex(uiDataPinIndex)
  , m_uiElementIndex(uiElementIndex)
{
  if (pinDesc.IsExecutionPin())
  {
    m_Shape = Shape::Arrow;
  }
  else
  {
    m_Shape = (pinDesc.m_ScriptDataType == plVisualScriptDataType::Array || pinDesc.m_ScriptDataType == plVisualScriptDataType::Map) ? Shape::Rect : Shape::Circle;
  }
}

plVisualScriptPin::~plVisualScriptPin()
{
  auto pManager = static_cast<plVisualScriptNodeManager*>(const_cast<plDocumentObjectManager*>(GetParent()->GetDocumentObjectManager()));
  pManager->RemoveDeductedPinType(*this);
}

plVisualScriptDataType::Enum plVisualScriptPin::GetResolvedScriptDataType() const
{
  auto scriptDataType = GetScriptDataType();
  if (scriptDataType == plVisualScriptDataType::AnyPointer || scriptDataType == plVisualScriptDataType::Any)
  {
    auto pManager = static_cast<const plVisualScriptNodeManager*>(GetParent()->GetDocumentObjectManager());
    return pManager->GetDeductedType(*this);
  }

  return scriptDataType;
}

plStringView plVisualScriptPin::GetDataTypeName() const
{
  plVisualScriptDataType::Enum resolvedDataType = GetResolvedScriptDataType();
  if (resolvedDataType == plVisualScriptDataType::Invalid)
  {
    return plVisualScriptDataType::GetName(GetScriptDataType());
  }

  if ((resolvedDataType == plVisualScriptDataType::TypedPointer ||
        resolvedDataType == plVisualScriptDataType::EnumValue || resolvedDataType == plVisualScriptDataType::BitflagValue) &&
      GetDataType() != nullptr)
  {
    return GetDataType()->GetTypeName();
  }

  return plVisualScriptDataType::GetName(resolvedDataType);
}

bool plVisualScriptPin::CanConvertTo(const plVisualScriptPin& targetPin, bool bUseResolvedDataTypes /*= true*/) const
{
  plVisualScriptDataType::Enum sourceScriptDataType = bUseResolvedDataTypes ? GetResolvedScriptDataType() : GetScriptDataType();
  plVisualScriptDataType::Enum targetScriptDataType = bUseResolvedDataTypes ? targetPin.GetResolvedScriptDataType() : targetPin.GetScriptDataType();

  const plRTTI* pSourceDataType = GetDataType();
  const plRTTI* pTargetDataType = targetPin.GetDataType();

  if (sourceScriptDataType == plVisualScriptDataType::TypedPointer && pSourceDataType != nullptr &&
      targetScriptDataType == plVisualScriptDataType::TypedPointer && pTargetDataType != nullptr)
    return pSourceDataType->IsDerivedFrom(pTargetDataType);

  if (sourceScriptDataType == plVisualScriptDataType::EnumValue && pSourceDataType != nullptr &&
      targetScriptDataType == plVisualScriptDataType::EnumValue && pTargetDataType != nullptr)
    return pSourceDataType == pTargetDataType;

  if (sourceScriptDataType == plVisualScriptDataType::BitflagValue && pSourceDataType != nullptr &&
      targetScriptDataType == plVisualScriptDataType::BitflagValue && pTargetDataType != nullptr)
    return pSourceDataType == pTargetDataType;

  return plVisualScriptDataType::CanConvertTo(sourceScriptDataType, targetScriptDataType);
}

//////////////////////////////////////////////////////////////////////////

plVisualScriptNodeManager::plVisualScriptNodeManager()
{
  m_NodeEvents.AddEventHandler(plMakeDelegate(&plVisualScriptNodeManager::NodeEventsHandler, this));
  m_PropertyEvents.AddEventHandler(plMakeDelegate(&plVisualScriptNodeManager::PropertyEventsHandler, this));
  // registered after the base handler, so it runs once the base has finished broadcasting the node events
  m_StructureEvents.AddEventHandler(plMakeDelegate(&plVisualScriptNodeManager::StructureEventsHandler, this));

  if (plAssetCurator::GetSingleton() != nullptr)
  {
    plAssetCurator::GetSingleton()->m_Events.AddEventHandler(plMakeDelegate(&plVisualScriptNodeManager::AssetCuratorEventHandler, this));
  }
}

plVisualScriptNodeManager::~plVisualScriptNodeManager()
{
  m_StructureEvents.RemoveEventHandler(plMakeDelegate(&plVisualScriptNodeManager::StructureEventsHandler, this));

  if (plAssetCurator::GetSingleton() != nullptr)
  {
    plAssetCurator::GetSingleton()->m_Events.RemoveEventHandler(plMakeDelegate(&plVisualScriptNodeManager::AssetCuratorEventHandler, this));
  }
}

plHashedString plVisualScriptNodeManager::GetScriptBaseClass() const
{
  plHashedString sBaseClass;
  if (GetRootObject()->GetChildren().IsEmpty() == false)
  {
    plVariant baseClass = GetRootObject()->GetChildren()[0]->GetTypeAccessor().GetValue("BaseClass");
    if (baseClass.IsA<plString>())
    {
      sBaseClass.Assign(baseClass.Get<plString>());
    }
  }
  return sBaseClass;
}

bool plVisualScriptNodeManager::IsFilteredByBaseClass(const plRTTI* pNodeType, const plVisualScriptNodeRegistry::NodeDesc& nodeDesc, const plHashedString& sBaseClass, bool bLogWarning /*= false*/) const
{
  if (nodeDesc.m_sFilterByBaseClass.IsEmpty() == false && nodeDesc.m_sFilterByBaseClass != sBaseClass)
  {
    if (bLogWarning)
    {
      plStringView sTypeName = pNodeType->GetTypeName();
      sTypeName.TrimWordStart(plVisualScriptNodeRegistry::s_szTypeNamePrefix);

      plLog::Warning("The base class function '{}' is not a function of the currently selected base class '{}' and will be skipped", sTypeName, sBaseClass);
    }

    return true;
  }

  return false;
}


plVisualScriptDataType::Enum plVisualScriptNodeManager::GetVariableType(plTempHashedString sName) const
{
  plVisualScriptVariable variable;
  if (GetVariable(sName, variable).Succeeded())
  {
    return variable.m_TypeDecl.GetDataType();
  }

  return plVisualScriptDataType::Invalid;
}

plResult plVisualScriptNodeManager::GetVariable(plTempHashedString sName, plVisualScriptVariable& out_variable) const
{
  // libraries have no variables (their properties object lacks the array, GetCount then reports -1)
  if (m_bIsLibrary == false && GetRootObject()->GetChildren().IsEmpty() == false)
  {
    auto& typeAccessor = GetRootObject()->GetChildren()[0]->GetTypeAccessor();
    const plInt32 iNumVariables = typeAccessor.GetCount("Variables");
    for (plInt32 i = 0; i < iNumVariables; ++i)
    {
      plVariant variableUuid = typeAccessor.GetValue("Variables", i);
      if (variableUuid.IsA<plUuid>() == false)
        continue;

      auto pVariableObject = GetObject(variableUuid.Get<plUuid>());
      if (pVariableObject == nullptr)
        continue;

      plVariant nameVar = pVariableObject->GetTypeAccessor().GetValue("Name");
      if (nameVar.IsA<plHashedString>() == false || nameVar.Get<plHashedString>() != sName)
        continue;

      out_variable.m_sName = nameVar.Get<plHashedString>();
      out_variable.m_TypeDecl = pVariableObject->GetTypeAccessor().GetValue("Type").Get<plVisualScriptVariableTypeDeclaration>();
      out_variable.m_DefaultValue = pVariableObject->GetTypeAccessor().GetValue("DefaultValue");
      return PL_SUCCESS;
    }
  }

  return PL_FAILURE;
}

void plVisualScriptNodeManager::GetAllVariables(plDynamicArray<plVisualScriptVariable>& out_variables) const
{
  out_variables.Clear();

  if (m_bIsLibrary == false && GetRootObject()->GetChildren().IsEmpty() == false)
  {
    auto& typeAccessor = GetRootObject()->GetChildren()[0]->GetTypeAccessor();
    const plInt32 iNumVariables = typeAccessor.GetCount("Variables");
    for (plInt32 i = 0; i < iNumVariables; ++i)
    {
      plVariant variableUuid = typeAccessor.GetValue("Variables", i);
      if (variableUuid.IsA<plUuid>() == false)
        continue;

      auto pVariableObject = GetObject(variableUuid.Get<plUuid>());
      if (pVariableObject == nullptr)
        continue;

      auto& variable = out_variables.ExpandAndGetRef();
      variable.m_sName = pVariableObject->GetTypeAccessor().GetValue("Name").ConvertTo<plHashedString>();
      variable.m_TypeDecl = pVariableObject->GetTypeAccessor().GetValue("Type").Get<plVisualScriptVariableTypeDeclaration>();
      variable.m_DefaultValue = pVariableObject->GetTypeAccessor().GetValue("DefaultValue");
    }
  }
}

void plVisualScriptNodeManager::GetInputExecutionPins(const plDocumentObject* pObject, plDynamicArray<const plVisualScriptPin*>& out_pins) const
{
  out_pins.Clear();

  auto pins = GetInputPins(pObject);
  for (auto& pPin : pins)
  {
    auto& vsPin = plStaticCast<const plVisualScriptPin&>(*pPin);
    if (vsPin.IsExecutionPin())
    {
      out_pins.PushBack(&vsPin);
    }
  }
}

void plVisualScriptNodeManager::GetOutputExecutionPins(const plDocumentObject* pObject, plDynamicArray<const plVisualScriptPin*>& out_pins) const
{
  out_pins.Clear();

  auto pins = GetOutputPins(pObject);
  for (auto& pPin : pins)
  {
    auto& vsPin = plStaticCast<const plVisualScriptPin&>(*pPin);
    if (vsPin.IsExecutionPin())
    {
      out_pins.PushBack(&vsPin);
    }
  }
}

void plVisualScriptNodeManager::GetInputDataPins(const plDocumentObject* pObject, plDynamicArray<const plVisualScriptPin*>& out_pins) const
{
  out_pins.Clear();

  auto pins = GetInputPins(pObject);
  for (auto& pPin : pins)
  {
    auto& vsPin = plStaticCast<const plVisualScriptPin&>(*pPin);
    if (vsPin.IsDataPin())
    {
      out_pins.PushBack(&vsPin);
    }
  }
}

void plVisualScriptNodeManager::GetOutputDataPins(const plDocumentObject* pObject, plDynamicArray<const plVisualScriptPin*>& out_pins) const
{
  out_pins.Clear();

  auto pins = GetOutputPins(pObject);
  for (auto& pPin : pins)
  {
    auto& vsPin = plStaticCast<const plVisualScriptPin&>(*pPin);
    if (vsPin.IsDataPin())
    {
      out_pins.PushBack(&vsPin);
    }
  }
}

void plVisualScriptNodeManager::GetEntryNodes(const plDocumentObject* pObject, plDynamicArray<const plDocumentObject*>& out_entryNodes) const
{
  plHybridArray<const plDocumentObject*, 64> nodeStack;
  nodeStack.PushBack(pObject);

  plHashSet<const plDocumentObject*> visitedNodes;
  plHybridArray<const plVisualScriptPin*, 16> pins;

  while (nodeStack.IsEmpty() == false)
  {
    const plDocumentObject* pCurrentNode = nodeStack.PeekBack();
    nodeStack.PopBack();

    auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pCurrentNode->GetType());
    if (pNodeDesc == nullptr)
      continue;

    if (plVisualScriptNodeDescription::Type::IsEntry(pNodeDesc->m_Type))
    {
      out_entryNodes.PushBack(pCurrentNode);
      continue;
    }

    GetInputExecutionPins(pCurrentNode, pins);
    plHybridArray<const plVisualScriptPin*, 8> sourcePins;
    for (auto pPin : pins)
    {
      // walks through reroutes, portals and sub-graph boundaries to the real predecessors
      GetLogicalSources(*pPin, sourcePins);
      for (auto pSourcePin : sourcePins)
      {
        const plDocumentObject* pSourceNode = pSourcePin->GetParent();
        if (visitedNodes.Insert(pSourceNode))
          continue;

        nodeStack.PushBack(pSourceNode);
      }
    }
  }
}

// static
plStringView plVisualScriptNodeManager::GetNiceTypeName(const plDocumentObject* pObject)
{
  plStringView sTypeName = pObject->GetType()->GetTypeName();

  while (sTypeName.TrimWordStart(plVisualScriptNodeRegistry::s_szTypeNamePrefix) ||
         sTypeName.TrimWordStart("Builtin_"))
  {
  }

  if (const char* szAngleBracket = sTypeName.FindSubString("<"))
  {
    sTypeName = plStringView(sTypeName.GetStartPointer(), szAngleBracket);
  }

  return sTypeName;
}

plStringView plVisualScriptNodeManager::GetNiceFunctionName(const plDocumentObject* pObject)
{
  plStringView sFunctionName = pObject->GetType()->GetTypeName();

  if (const char* szSeparator = sFunctionName.FindLastSubString("::"))
  {
    sFunctionName = plStringView(szSeparator + 2, sFunctionName.GetEndPointer());
  }

  return sFunctionName;
}

plVisualScriptDataType::Enum plVisualScriptNodeManager::GetDeductedType(const plVisualScriptPin& pin) const
{
  plEnum<plVisualScriptDataType> dataType = plVisualScriptDataType::Invalid;
  m_PinToDeductedType.TryGetValue(&pin, dataType);
  return dataType;
}

plVisualScriptDataType::Enum plVisualScriptNodeManager::GetDeductedType(const plDocumentObject* pObject) const
{
  plEnum<plVisualScriptDataType> dataType = plVisualScriptDataType::Invalid;
  m_ObjectToDeductedType.TryGetValue(pObject, dataType);
  return dataType;
}

bool plVisualScriptNodeManager::IsCoroutine(const plDocumentObject* pObject) const
{
  auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pObject->GetType());
  if (pNodeDesc != nullptr && plVisualScriptNodeDescription::Type::MakesOuterCoroutine(pNodeDesc->m_Type))
  {
    return true;
  }

  if (IsCall(pObject))
  {
    // a call is a coroutine when the called function is one
    const plDocumentObject* pEntry = FindFunctionEntry(GetStringProperty(pObject, "Function"));
    return pEntry != nullptr && m_CoroutineObjects.Contains(pEntry);
  }

  if (IsCallLibrary(pObject))
  {
    if (auto pMeta = GetLibraryMetaData(GetStringProperty(pObject, "Library")))
    {
      if (auto pFunction = pMeta->FindFunction(GetStringProperty(pObject, "Function")))
        return pFunction->m_bCoroutine;
    }
    return false;
  }

  return m_CoroutineObjects.Contains(pObject);
}

bool plVisualScriptNodeManager::IsLoop(const plDocumentObject* pObject) const
{
  auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pObject->GetType());
  if (pNodeDesc != nullptr && plVisualScriptNodeDescription::Type::IsLoop(pNodeDesc->m_Type))
  {
    return true;
  }

  return false;
}

bool plVisualScriptNodeManager::InternalIsNode(const plDocumentObject* pObject) const
{
  return pObject->GetType()->IsDerivedFrom(plVisualScriptNodeRegistry::GetSingleton()->GetNodeBaseType());
}

bool plVisualScriptNodeManager::InternalIsDynamicPinProperty(const plDocumentObject* pObject, const plAbstractProperty* pProp) const
{
  auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pObject->GetType());

  if (pNodeDesc != nullptr && pNodeDesc->m_bHasDynamicPins)
  {
    plTempHashedString sPropNameHashed = plTempHashedString(pProp->GetPropertyName());

    // properties whose pins are mirrored from declarations rather than listed in the node description
    switch (pNodeDesc->m_Type)
    {
      case plVisualScriptNodeDescription::Type::Builtin_CallLibrary:
        return sPropNameHashed == plTempHashedString("Library") || sPropNameHashed == plTempHashedString("Function");
      case plVisualScriptNodeDescription::Type::Builtin_Call:
      case plVisualScriptNodeDescription::Type::Builtin_Return:
        return sPropNameHashed == plTempHashedString("Function");
      case plVisualScriptNodeDescription::Type::Builtin_SubGraphInputs:
      case plVisualScriptNodeDescription::Type::Builtin_SubGraphOutputs:
        return sPropNameHashed == plTempHashedString("Pins");
      case plVisualScriptNodeDescription::Type::Builtin_Reroute:
        return sPropNameHashed == plTempHashedString("Kind");
      default:
        break;
    }
    for (auto& pinDesc : pNodeDesc->m_InputPins)
    {
      if (pinDesc.m_sDynamicPinProperty == sPropNameHashed)
      {
        return true;
      }
    }

    for (auto& pinDesc : pNodeDesc->m_OutputPins)
    {
      if (pinDesc.m_sDynamicPinProperty == sPropNameHashed)
      {
        return true;
      }
    }
  }

  return false;
}

plStatus plVisualScriptNodeManager::InternalCanConnect(const plVisualGraphPin& source, const plVisualGraphPin& target, CanConnectResult& out_result) const
{
  const plVisualScriptPin& pinSource = plStaticCast<const plVisualScriptPin&>(source);
  const plVisualScriptPin& pinTarget = plStaticCast<const plVisualScriptPin&>(target);

  if (pinSource.IsExecutionPin() != pinTarget.IsExecutionPin())
  {
    out_result = CanConnectResult::ConnectNever;
    return plStatus("Cannot connect data pins with execution pins.");
  }

  if (pinSource.IsDataPin() && pinSource.CanConvertTo(pinTarget, false) == false)
  {
    out_result = CanConnectResult::ConnectNever;
    return plStatus(plFmt("The pin data types are incompatible."));
  }

  if (WouldConnectionCreateCircle(source, target) || CanReachNodeLogically(target.GetParent(), source.GetParent()))
  {
    out_result = CanConnectResult::ConnectNever;
    return plStatus("Connecting these pins would create a circle in the graph.");
  }

  // a sub-graph boundary only forwards data within its own sub-graph
  if (pinSource.IsDataPin())
  {
    const plDocumentObject* pSourceNode = source.GetParent();
    const plDocumentObject* pTargetNode = target.GetParent();
    if (IsSubGraphInputs(pSourceNode) && IsSubGraphOutputs(pTargetNode))
    {
      out_result = CanConnectResult::ConnectNever;
      return plStatus("Sub-graph inputs cannot be connected directly to sub-graph outputs.");
    }
  }

  // only one connection is allowed on DATA input pins, execution input pins may have multiple incoming connections
  if (pinTarget.IsDataPin() && HasConnections(pinTarget))
  {
    out_result = CanConnectResult::ConnectNto1;
    return plStatus(PL_FAILURE);
  }

  // only one outgoing connection is allowed on EXECUTION pins, data pins may have multiple outgoing connections
  if (pinSource.IsExecutionPin() && HasConnections(pinSource))
  {
    out_result = CanConnectResult::Connect1toN;
    return plStatus(PL_FAILURE);
  }

  out_result = CanConnectResult::ConnectNtoN;
  return plStatus(PL_SUCCESS);
}

plUInt8 plVisualScriptNodeManager::ScorePinMatch(const plVisualGraphPin& fromPin, const plVisualGraphPin& candidate) const
{
  const plVisualScriptPin& from = plStaticCast<const plVisualScriptPin&>(fromPin);
  const plVisualScriptPin& other = plStaticCast<const plVisualScriptPin&>(candidate);

  if (from.IsExecutionPin())
    return plVisualGraphTemplateMatch::Exact;

  // the dragged pin may already have a deduced type; the new node's pins only have their declared one
  plVisualScriptDataType::Enum fromType = from.GetResolvedScriptDataType();
  if (fromType == plVisualScriptDataType::Invalid)
    fromType = from.GetScriptDataType();

  const bool bFromOutput = from.GetType() == plVisualGraphPin::Type::Output;
  const plVisualScriptDataType::Enum sourceType = bFromOutput ? fromType : other.GetScriptDataType();
  const plVisualScriptDataType::Enum targetType = bFromOutput ? other.GetScriptDataType() : fromType;

  if (sourceType == plVisualScriptDataType::Any || targetType == plVisualScriptDataType::Any || targetType == plVisualScriptDataType::AnyPointer)
    return plVisualGraphTemplateMatch::Converts;

  // CanConnect already checked typed pointers (derived classes) and enums (the same type)
  if (sourceType == targetType)
    return plVisualGraphTemplateMatch::Exact;

  if (targetType == plVisualScriptDataType::String || targetType == plVisualScriptDataType::HashedString || targetType == plVisualScriptDataType::Variant)
    return plVisualGraphTemplateMatch::AcceptsAnything;

  if (plVisualScriptDataType::IsPointer(sourceType) && targetType == plVisualScriptDataType::Bool)
    return plVisualGraphTemplateMatch::AcceptsAnything;

  return plVisualGraphTemplateMatch::Converts;
}

plString plVisualScriptNodeManager::GetPinTypeName(const plVisualGraphPin& pin) const
{
  const plVisualScriptPin& scriptPin = plStaticCast<const plVisualScriptPin&>(pin);
  if (scriptPin.IsExecutionPin())
    return "Exec";

  // reflected class names without the engine prefix: "plRigidBodyComponent" reads as "RigidBodyComponent"
  plStringView sName = scriptPin.GetDataTypeName();
  if (sName.StartsWith("pl") && sName.GetElementCount() > 2 && sName.GetStartPointer()[2] >= 'A' && sName.GetStartPointer()[2] <= 'Z')
    sName.Shrink(2, 0);

  return sName;
}

void plVisualScriptNodeManager::InternalCreatePins(const plDocumentObject* pObject, NodeInternal& ref_node)
{
  auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pObject->GetType());

  if (pNodeDesc == nullptr)
    return;

  if (CreateMirroredPins(pObject, *pNodeDesc, ref_node))
    return;

  plHybridArray<plString, 16> dynamicPinNames;
  auto CreatePins = [&](const plVisualScriptNodeRegistry::PinDesc& pinDesc, plVisualGraphPin::Type type, plDynamicArray<plUniquePtr<plVisualGraphPin>>& out_pins, plUInt32& inout_dataPinIndex)
  {
    if (pinDesc.m_sDynamicPinProperty.IsEmpty() == false)
    {
      GetDynamicPinNames(pObject, pinDesc.m_sDynamicPinProperty, pinDesc.m_sName, dynamicPinNames);
    }
    else
    {
      dynamicPinNames.Clear();
      dynamicPinNames.PushBack(pinDesc.m_sName.GetView());
    }

    for (plUInt32 i = 0; i < dynamicPinNames.GetCount(); ++i)
    {
      plUInt32 uiDataPinIndex = plInvalidIndex;
      if (pinDesc.IsDataPin())
      {
        uiDataPinIndex = inout_dataPinIndex;
        ++inout_dataPinIndex;
      }

      auto pPin = PL_DEFAULT_NEW(plVisualScriptPin, type, dynamicPinNames[i], pinDesc, pObject, uiDataPinIndex, i);
      out_pins.PushBack(pPin);
    }
  };

  plUInt32 uiDataPinIndex = 0;
  for (const auto& pinDesc : pNodeDesc->m_InputPins)
  {
    CreatePins(pinDesc, plVisualGraphPin::Type::Input, ref_node.m_Inputs, uiDataPinIndex);
  }

  uiDataPinIndex = 0;
  for (const auto& pinDesc : pNodeDesc->m_OutputPins)
  {
    CreatePins(pinDesc, plVisualGraphPin::Type::Output, ref_node.m_Outputs, uiDataPinIndex);
  }
}

void plVisualScriptNodeManager::GetNodeCreationTemplates(plDynamicArray<plVisualGraphNodeDesc>& out_templates) const
{
  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
  auto registryPropertyValues = pRegistry->GetPropertyValues();
  plHashedString sBaseClass = GetScriptBaseClass();

  static plHashedString sVariables = plMakeHashedString("Variables");
  static plHashedString sName = plMakeHashedString("Name");
  static plHashedString sFunction = plMakeHashedString("Function");
  static plHashedString sLibrary = plMakeHashedString("Library");
  static plHashedString sKind = plMakeHashedString("Kind");
  static plHashedString sScope = plMakeHashedString("Scope"); // == plVisualScriptNodeRegistry::s_szScopeProperty
  static plHashedString sPortalsCategory = plMakeHashedString("Portals");
  static plHashedString sFunctionsCategory = plMakeHashedString("Functions");
  static plHashedString sEventHandlerCategory = plMakeHashedString("Add Event Handler/");

  // Every template gets its property values from m_PropertyValues, which is filled in two passes so the
  // array pointers handed out stay valid: first everything is collected, then the storage is allocated once.
  struct PendingTemplate
  {
    const plRTTI* m_pType = nullptr;
    plStringView m_sTypeName;
    plHashedString m_sCategory;
    plUInt32 m_uiPropertyStart = 0;
    plUInt32 m_uiPropertyCount = 0;
  };

  plHybridArray<PendingTemplate, 256> pending;
  plHybridArray<plVisualGraphNodeProperty, 256> properties;

  m_VariableNodeTypeNames.Clear();
  m_TemplateCategories.Clear();

  const bool bInSubGraph = m_CurrentViewScope.IsValid();
  const plDocumentObject* pCurrentFunction = GetScopeFunction(m_CurrentViewScope);

  auto AddTemplate = [&](const plRTTI* pType, plStringView sTypeName, const plHashedString& sCategory, plArrayPtr<const plVisualGraphNodeProperty> extraProperties) -> PendingTemplate&
  {
    auto& temp = pending.ExpandAndGetRef();
    temp.m_pType = pType;
    temp.m_sTypeName = sTypeName;
    temp.m_sCategory = sCategory;
    temp.m_uiPropertyStart = properties.GetCount();

    for (auto& prop : extraProperties)
    {
      properties.PushBack(prop);
    }

    if (bInSubGraph)
    {
      properties.PushBack({sScope, m_CurrentViewScope});
    }

    temp.m_uiPropertyCount = properties.GetCount() - temp.m_uiPropertyStart;
    return temp;
  };

  auto GetDisplayName = [&](const plRTTI* pType, plStringView sFallback) -> plStringView
  {
    if (pType == pRegistry->GetRerouteType())
      return "Reroute";
    if (pType == pRegistry->GetPortalType(true, true))
      return "Portal In (Exec)";
    if (pType == pRegistry->GetPortalType(true, false))
      return "Portal In (Data)";
    if (pType == pRegistry->GetPortalType(false, true))
      return "Portal Out (Exec)";
    if (pType == pRegistry->GetPortalType(false, false))
      return "Portal Out (Data)";
    if (pType == pRegistry->GetSubGraphType())
      return "Sub-graph";
    if (pType == pRegistry->GetFunctionEntryType())
      return "Function Entry";
    if (pType == pRegistry->GetReturnType())
      return "Return";
    if (pType == pRegistry->GetCallType())
      return "Call Function";
    if (pType == pRegistry->GetCallLibraryType())
      return "Call Library Function";
    return sFallback;
  };

  for (auto& nodeTemplate : pRegistry->GetNodeCreationTemplates())
  {
    const plRTTI* pNodeType = nodeTemplate.m_pType;
    const auto* pNodeDesc = pRegistry->GetNodeDescForType(pNodeType);
    if (pNodeDesc == nullptr)
      continue;

    if (IsFilteredByBaseClass(pNodeType, *pNodeDesc, sBaseClass))
      continue;

    if (pNodeType->GetTypeFlags().IsSet(plTypeFlags::Abstract))
      continue;

    // entry points only live in the event graph; functions are created from the Functions panel
    if (plVisualScriptNodeDescription::Type::IsFunctionEntry(pNodeDesc->m_Type))
      continue;
    if (bInSubGraph && plVisualScriptNodeDescription::Type::IsEntry(pNodeDesc->m_Type))
      continue;

    // a return belongs to the function whose graph is shown
    if (pNodeType == pRegistry->GetReturnType())
    {
      if (pCurrentFunction == nullptr)
        continue;

      plVisualGraphNodeProperty functionProp = {sFunction, GetStringProperty(pCurrentFunction, "Name")};
      AddTemplate(pNodeType, GetDisplayName(pNodeType, nodeTemplate.m_sTypeName), nodeTemplate.m_sCategory, plMakeArrayPtr(&functionProp, 1));
      continue;
    }

    if (m_bIsLibrary)
    {
      // libraries have no owner, no variables and no events
      if (pNodeDesc->m_Type == plVisualScriptNodeDescription::Type::MessageHandler ||
          pNodeDesc->m_Type == plVisualScriptNodeDescription::Type::MessageHandler_Coroutine ||
          pNodeDesc->m_Type == plVisualScriptNodeDescription::Type::GetScriptOwner ||
          pNodeDesc->m_Type == plVisualScriptNodeDescription::Type::EntryCall ||
          pNodeDesc->m_Type == plVisualScriptNodeDescription::Type::Builtin_GetVariable ||
          pNodeDesc->m_Type == plVisualScriptNodeDescription::Type::Builtin_SetVariable ||
          pNodeDesc->m_Type == plVisualScriptNodeDescription::Type::Builtin_IncVariable ||
          pNodeDesc->m_Type == plVisualScriptNodeDescription::Type::Builtin_DecVariable)
        continue;

      if (nodeTemplate.m_sCategory == sEventHandlerCategory)
        continue;
    }

    // the reroute kind follows the pin the user dragged from
    if (pNodeType == pRegistry->GetRerouteType() && m_CreationHint.m_bValid)
    {
      plVisualGraphNodeProperty kind = {sKind, plInt64(m_CreationHint.m_bExec ? plVisualScriptPinKind::Exec : plVisualScriptPinKind::Data)};
      AddTemplate(pNodeType, GetDisplayName(pNodeType, nodeTemplate.m_sTypeName), nodeTemplate.m_sCategory, plMakeArrayPtr(&kind, 1));
      continue;
    }

    AddTemplate(pNodeType, GetDisplayName(pNodeType, nodeTemplate.m_sTypeName), nodeTemplate.m_sCategory, registryPropertyValues.GetSubArray(nodeTemplate.m_uiPropertyValuesStart, nodeTemplate.m_uiPropertyValuesCount));
  }

  plStringBuilder sNodeTypeName;

  // Getter and setter templates for variables
  if (m_bIsLibrary == false && GetRootObject()->GetChildren().IsEmpty() == false)
  {
    auto& typeAccessor = GetRootObject()->GetChildren()[0]->GetTypeAccessor();
    const plInt32 iNumVariables = typeAccessor.GetCount(sVariables.GetView());
    for (plInt32 i = 0; i < iNumVariables; ++i)
    {
      plVariant variableUuid = typeAccessor.GetValue(sVariables.GetView(), i);
      if (variableUuid.IsA<plUuid>() == false)
        continue;

      auto pVariableObject = GetObject(variableUuid.Get<plUuid>());
      if (pVariableObject == nullptr)
        continue;

      plVariant nameVar = pVariableObject->GetTypeAccessor().GetValue(sName.GetView());
      if (nameVar.IsA<plHashedString>() == false)
        continue;

      plHashedString sVariableName = nameVar.Get<plHashedString>();
      plVisualGraphNodeProperty nameProp = {sName, nameVar};

      sNodeTypeName.Set("Set", sVariableName);
      m_VariableNodeTypeNames.PushBack(sNodeTypeName);
      AddTemplate(pRegistry->GetVariableSetterType(), m_VariableNodeTypeNames.PeekBack(), sVariables, plMakeArrayPtr(&nameProp, 1));

      sNodeTypeName.Set("Get", sVariableName);
      m_VariableNodeTypeNames.PushBack(sNodeTypeName);
      AddTemplate(pRegistry->GetVariableGetterType(), m_VariableNodeTypeNames.PeekBack(), sVariables, plMakeArrayPtr(&nameProp, 1));
    }
  }

  // One 'Portal Out' template per 'Portal In' and one 'Call' template per function
  for (auto pObject : GetRootObject()->GetChildren())
  {
    if (IsNode(pObject) == false)
      continue;

    if (IsPortalIn(pObject))
    {
      plString sPortalName = GetPortalName(pObject);
      if (sPortalName.IsEmpty())
        continue;

      const bool bExec = IsExecPortal(pObject);
      plVisualGraphNodeProperty nameProp = {sName, sPortalName};

      sNodeTypeName.Set(sPortalName, bExec ? " (Exec Out)" : " (Data Out)");
      m_VariableNodeTypeNames.PushBack(sNodeTypeName);
      AddTemplate(pRegistry->GetPortalType(false, bExec), m_VariableNodeTypeNames.PeekBack(), sPortalsCategory, plMakeArrayPtr(&nameProp, 1));
    }
    else if (IsFunctionEntry(pObject))
    {
      plString sFunctionName = GetStringProperty(pObject, "Name");
      if (sFunctionName.IsEmpty())
        continue;

      plVisualGraphNodeProperty functionProp = {sFunction, sFunctionName};

      sNodeTypeName.Set("Call ", sFunctionName);
      m_VariableNodeTypeNames.PushBack(sNodeTypeName);
      AddTemplate(pRegistry->GetCallType(), m_VariableNodeTypeNames.PeekBack(), sFunctionsCategory, plMakeArrayPtr(&functionProp, 1));
    }
  }

  // One template per function of every visual script library in the project
  if (plAssetCurator::GetSingleton() != nullptr)
  {
    plStringBuilder sCategory, sGuid;

    auto subAssets = plAssetCurator::GetSingleton()->GetKnownSubAssets();
    for (auto it = subAssets->GetIterator(); it.IsValid(); ++it)
    {
      const plSubAsset& subAsset = it.Value();
      if (subAsset.m_bMainAsset == false || subAsset.m_pAssetInfo == nullptr || subAsset.m_pAssetInfo->m_Info == nullptr)
        continue;

      if (subAsset.m_pAssetInfo->m_Info->m_sAssetsDocumentTypeName != plTempHashedString("VisualScriptLibrary"))
        continue;

      const plVisualScriptLibraryMetaData* pMeta = subAsset.m_pAssetInfo->m_Info->GetMetaInfo<plVisualScriptLibraryMetaData>();
      if (pMeta == nullptr || pMeta->m_Functions.IsEmpty())
        continue;

      // don't offer a library its own functions
      if (m_bIsLibrary && GetDocument() != nullptr && subAsset.m_Data.m_Guid == GetDocument()->GetGuid())
        continue;

      sCategory.Set("Libraries/", subAsset.m_pAssetInfo->m_Path.GetDataDirRelativePath().GetFileName());
      m_TemplateCategories.PushBack(plHashedString());
      m_TemplateCategories.PeekBack().Assign(sCategory);

      plConversionUtils::ToString(subAsset.m_Data.m_Guid, sGuid);

      for (const auto& function : pMeta->m_Functions)
      {
        plVisualGraphNodeProperty props[2] = {{sLibrary, plString(sGuid)}, {sFunction, function.m_sName}};

        m_VariableNodeTypeNames.PushBack(function.m_sName);
        AddTemplate(pRegistry->GetCallLibraryType(), m_VariableNodeTypeNames.PeekBack(), m_TemplateCategories.PeekBack(), plMakeArrayPtr(props, 2));
      }
    }
  }

  // Second pass: hand out stable array pointers
  m_PropertyValues.Clear();
  m_PropertyValues.Reserve(properties.GetCount());
  for (auto& prop : properties)
  {
    m_PropertyValues.PushBack(prop);
  }

  for (auto& temp : pending)
  {
    auto& desc = out_templates.ExpandAndGetRef();
    desc.m_pType = temp.m_pType;
    desc.m_sTypeName = temp.m_sTypeName;
    desc.m_sCategory = temp.m_sCategory;
    desc.m_PropertyValues = m_PropertyValues.GetArrayPtr().GetSubArray(temp.m_uiPropertyStart, temp.m_uiPropertyCount);
  }
}

void plVisualScriptNodeManager::NodeEventsHandler(const plVisualGraphObjectManagerEvent& e)
{
  switch (e.m_EventType)
  {
    case plVisualGraphObjectManagerEvent::Type::AfterPinsConnected:
    {
      auto& connection = GetConnection(e.m_pObject);
      auto& targetPin = connection.GetTargetPin();
      DeductNodeTypeAndAllPinTypes(targetPin.GetParent());

      // a data reroute also learns its type from what it is connected to on its output side
      const plDocumentObject* pSourceNode = connection.GetSourcePin().GetParent();
      if (IsReroute(pSourceNode))
      {
        DeductNodeTypeAndAllPinTypes(pSourceNode);
      }

      UpdateCoroutine(targetPin.GetParent(), connection);
    }
    break;

    case plVisualGraphObjectManagerEvent::Type::BeforePinsDisonnected:
    {
      auto& connection = GetConnection(e.m_pObject);
      auto& targetPin = connection.GetTargetPin();
      DeductNodeTypeAndAllPinTypes(targetPin.GetParent(), &targetPin);

      const plDocumentObject* pSourceNode = connection.GetSourcePin().GetParent();
      if (IsReroute(pSourceNode))
      {
        DeductNodeTypeAndAllPinTypes(pSourceNode, &targetPin);
      }

      UpdateCoroutine(targetPin.GetParent(), connection, true);
    }
    break;

    case plVisualGraphObjectManagerEvent::Type::AfterNodeAdded:
    {
      DeductNodeTypeAndAllPinTypes(e.m_pObject);

      if (IsPortalIn(e.m_pObject) || IsPortalOut(e.m_pObject) || IsFunctionEntry(e.m_pObject) || IsCall(e.m_pObject))
      {
        RefreshDynamicEnums();
      }
    }
    break;

    case plVisualGraphObjectManagerEvent::Type::BeforeNodeRemoved:
    {
      m_ObjectToDeductedType.Remove(e.m_pObject);
      m_CoroutineObjects.Remove(e.m_pObject);
    }
    break;

    case plVisualGraphObjectManagerEvent::Type::AfterNodeRemoved:
    {
      if (IsPortalIn(e.m_pObject))
      {
        // the outs lost their source
        for (auto pObject : GetRootObject()->GetChildren())
        {
          if (IsPortalOut(pObject))
            DeductNodeTypeAndAllPinTypes(pObject);
        }
      }

      if (IsFunctionEntry(e.m_pObject) || IsCall(e.m_pObject))
      {
        RecomputeAllCoroutines();
      }

      RefreshDynamicEnums();
    }
    break;

    default:
      break;
  }
}

void plVisualScriptNodeManager::StructureEventsHandler(const plDocumentObjectStructureEvent& e)
{
  if (e.m_EventType != plDocumentObjectStructureEvent::Type::AfterObjectAdded || IsNode(e.m_pObject) == false)
    return;

  // While a document loads, a node's pin declaration children are added before the node itself is part of the
  // document, so the pins built at that point cannot resolve them. Now everything is in place: build them again.
  // This runs outside the node event broadcast (which must not nest).
  if (IsFunctionEntry(e.m_pObject) || IsSubGraphBoundary(e.m_pObject))
  {
    TryRecreatePins(e.m_pObject);
  }
}

void plVisualScriptNodeManager::PropertyEventsHandler(const plDocumentObjectPropertyEvent& e)
{
  if (IsNode(e.m_pObject))
  {
    DeductNodeTypeAndAllPinTypes(e.m_pObject);

    if ((IsPortalIn(e.m_pObject) || IsPortalOut(e.m_pObject)) && e.m_sProperty == "Name")
    {
      // outs look up their in by name
      for (auto pObject : GetRootObject()->GetChildren())
      {
        if (IsPortalOut(pObject))
          DeductNodeTypeAndAllPinTypes(pObject);
      }
      RefreshDynamicEnums();
    }
    else if (IsFunctionEntry(e.m_pObject) && e.m_sProperty == "Name")
    {
      RecomputeAllCoroutines();
      RefreshDynamicEnums();
    }
    else if ((IsCall(e.m_pObject) || IsCallLibrary(e.m_pObject)) && (e.m_sProperty == "Function" || e.m_sProperty == "Library"))
    {
      RecomputeAllCoroutines();
      m_NodeChangedEvent.Broadcast(e.m_pObject);
    }
    else if (IsSubGraph(e.m_pObject) && e.m_sProperty == "Name")
    {
      m_NodeChangedEvent.Broadcast(e.m_pObject);
    }
  }
  else if (e.m_pObject->GetType() == plGetStaticRTTI<plVisualScriptVariable>() && (e.m_sProperty == "Name" || e.m_sProperty == "Type"))
  {
    // a variable's name or type has changed, re-run type deduction
    for (auto pObject : GetRootObject()->GetChildren())
    {
      if (IsNode(pObject) == false)
        continue;

      DeductNodeTypeAndAllPinTypes(pObject);
    }

    if (e.m_sProperty == "Type")
    {
      auto typeDecl = e.m_NewValue.Get<plVisualScriptVariableTypeDeclaration>();
      plVariant defaultValue = e.m_pObject->GetTypeAccessor().GetValue("DefaultValue");
      plVisualScriptVariable::ConvertDefaultValue(defaultValue, typeDecl);

      GetDocument()->GetObjectAccessor()->SetValueByName(e.m_pObject, "DefaultValue", defaultValue).AssertSuccess();
    }
  }
}

void plVisualScriptNodeManager::RemoveDeductedPinType(const plVisualScriptPin& pin)
{
  m_PinToDeductedType.Remove(&pin);
}

void plVisualScriptNodeManager::DeductNodeTypeAndAllPinTypes(const plDocumentObject* pObject, const plVisualGraphPin* pDisconnectedPin /*= nullptr*/)
{
  auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pObject->GetType());
  if (pNodeDesc == nullptr || pNodeDesc->NeedsTypeDeduction() == false)
    return;

  if (pDisconnectedPin != nullptr && static_cast<const plVisualScriptPin*>(pDisconnectedPin)->NeedsTypeDeduction() == false)
    return;

  bool bNodeTypeChanged = false;
  {
    plEnum<plVisualScriptDataType> newDeductedType = pNodeDesc->m_DeductTypeFunc(pObject, static_cast<const plVisualScriptPin*>(pDisconnectedPin));
    plEnum<plVisualScriptDataType> oldDeductedType = plVisualScriptDataType::Invalid;
    m_ObjectToDeductedType.Insert(pObject, newDeductedType, &oldDeductedType);

    bNodeTypeChanged = (newDeductedType != oldDeductedType);
  }

  bool bAnyInputPinChanged = false;
  plHybridArray<const plVisualScriptPin*, 16> pins;
  GetInputDataPins(pObject, pins);
  for (auto pPin : pins)
  {
    if (auto pFunc = pPin->GetDeductTypeFunc())
    {
      plEnum<plVisualScriptDataType> newDeductedType = pFunc(*pPin);
      plEnum<plVisualScriptDataType> oldDeductedType = plVisualScriptDataType::Invalid;
      m_PinToDeductedType.Insert(pPin, newDeductedType, &oldDeductedType);

      bAnyInputPinChanged |= (newDeductedType != oldDeductedType);
    }
  }

  bool bAnyOutputPinChanged = false;
  GetOutputDataPins(pObject, pins);
  for (auto pPin : pins)
  {
    if (auto pFunc = pPin->GetDeductTypeFunc())
    {
      plEnum<plVisualScriptDataType> newDeductedType = pFunc(*pPin);
      plEnum<plVisualScriptDataType> oldDeductedType = plVisualScriptDataType::Invalid;
      m_PinToDeductedType.Insert(pPin, newDeductedType, &oldDeductedType);

      bAnyOutputPinChanged |= (newDeductedType != oldDeductedType);
    }
  }

  if (bNodeTypeChanged || bAnyInputPinChanged || bAnyOutputPinChanged)
  {
    m_NodeChangedEvent.Broadcast(pObject);
  }

  // propagate to connected nodes
  if (bAnyOutputPinChanged)
  {
    for (auto pPin : pins)
    {
      if (pPin->NeedsTypeDeduction() == false)
        continue;

      auto connections = GetConnections(*pPin);
      for (auto& connection : connections)
      {
        DeductNodeTypeAndAllPinTypes(connection->GetTargetPin().GetParent());
      }
    }
  }

  // a data portal in feeds every portal out with the same name
  if (bNodeTypeChanged && IsPortalIn(pObject) && IsExecPortal(pObject) == false)
  {
    plHybridArray<const plDocumentObject*, 8> portalOuts;
    GetPortalOuts(GetPortalName(pObject), false, portalOuts);
    for (auto pPortalOut : portalOuts)
    {
      DeductNodeTypeAndAllPinTypes(pPortalOut);
    }
  }
}

void plVisualScriptNodeManager::UpdateCoroutine(const plDocumentObject* pTargetNode, const plVisualGraphConnection& changedConnection, bool bIsAboutToDisconnect)
{
  auto vsPin = static_cast<const plVisualScriptPin&>(changedConnection.GetTargetPin());
  if (vsPin.IsExecutionPin() == false)
    return;

  const plVisualGraphConnection* pIgnoredConnection = bIsAboutToDisconnect ? &changedConnection : nullptr;

  plHybridArray<const plDocumentObject*, 16> entryNodes;
  GetEntryNodes(pTargetNode, entryNodes);

  bool bAnyFunctionChanged = false;
  for (auto pEntryNode : entryNodes)
  {
    const bool bWasCoroutine = m_CoroutineObjects.Contains(pEntryNode);
    const bool bIsCoroutine = IsConnectedToCoroutine(pEntryNode, pIgnoredConnection);

    if (bWasCoroutine != bIsCoroutine)
    {
      if (bIsCoroutine)
      {
        m_CoroutineObjects.Insert(pEntryNode);
      }
      else
      {
        m_CoroutineObjects.Remove(pEntryNode);
      }

      m_NodeChangedEvent.Broadcast(pEntryNode);

      bAnyFunctionChanged |= IsFunctionEntry(pEntryNode);
    }
  }

  // callers of a function follow the function's coroutine state
  if (bAnyFunctionChanged)
  {
    RecomputeAllCoroutines();
  }
}

void plVisualScriptNodeManager::RecomputeAllCoroutines()
{
  // Iterate until stable: functions can call each other in any order, including recursively.
  for (plUInt32 uiIteration = 0; uiIteration < 16; ++uiIteration)
  {
    bool bChanged = false;

    for (auto pObject : GetRootObject()->GetChildren())
    {
      if (IsNode(pObject) == false || IsComment(pObject) || IsEntryNode(pObject) == false)
        continue;

      const bool bWasCoroutine = m_CoroutineObjects.Contains(pObject);
      const bool bIsCoroutine = IsConnectedToCoroutine(pObject, nullptr);

      if (bWasCoroutine != bIsCoroutine)
      {
        if (bIsCoroutine)
          m_CoroutineObjects.Insert(pObject);
        else
          m_CoroutineObjects.Remove(pObject);

        m_NodeChangedEvent.Broadcast(pObject);
        bChanged = true;
      }
    }

    if (bChanged == false)
      break;
  }

  // call nodes show the coroutine icon of their function
  for (auto pObject : GetRootObject()->GetChildren())
  {
    if (IsNode(pObject) && IsCall(pObject))
    {
      m_NodeChangedEvent.Broadcast(pObject);
    }
  }
}

bool plVisualScriptNodeManager::IsConnectedToCoroutine(const plDocumentObject* pEntryNode, const plVisualGraphConnection* pIgnoredConnection) const
{
  plHybridArray<const plDocumentObject*, 64> nodeStack;
  nodeStack.PushBack(pEntryNode);

  plHashSet<const plDocumentObject*> visitedNodes;
  plHybridArray<const plVisualScriptPin*, 16> pins;
  plHybridArray<const plVisualScriptPin*, 8> targetPins;

  while (nodeStack.IsEmpty() == false)
  {
    const plDocumentObject* pCurrentNode = nodeStack.PeekBack();
    nodeStack.PopBack();

    auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pCurrentNode->GetType());
    if (pNodeDesc == nullptr)
      continue;

    if (plVisualScriptNodeDescription::Type::MakesOuterCoroutine(pNodeDesc->m_Type))
    {
      return true;
    }

    if (IsCallLibrary(pCurrentNode) && IsCoroutine(pCurrentNode))
    {
      return true;
    }

    if (IsCall(pCurrentNode))
    {
      // follow the call into the function body
      if (const plDocumentObject* pFunction = FindFunctionEntry(GetStringProperty(pCurrentNode, "Function")))
      {
        if (visitedNodes.Insert(pFunction) == false)
          nodeStack.PushBack(pFunction);
      }
    }

    GetOutputExecutionPins(pCurrentNode, pins);
    for (auto pPin : pins)
    {
      if (pPin->SplitExecution())
        continue;

      GetLogicalTargets(*pPin, targetPins, pIgnoredConnection);
      for (auto pTargetPin : targetPins)
      {
        const plDocumentObject* pTargetNode = pTargetPin->GetParent();
        if (visitedNodes.Insert(pTargetNode))
          continue;

        nodeStack.PushBack(pTargetNode);
      }
    }
  }

  return false;
}

// Node kinds
//////////////////////////////////////////////////////////////////////////

plVisualScriptNodeDescription::Type::Enum plVisualScriptNodeManager::GetNodeType(const plDocumentObject* pObject) const
{
  if (pObject == nullptr)
    return plVisualScriptNodeDescription::Type::Invalid;

  auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pObject->GetType());
  return pNodeDesc != nullptr ? static_cast<plVisualScriptNodeDescription::Type::Enum>(pNodeDesc->m_Type.GetValue()) : plVisualScriptNodeDescription::Type::Invalid;
}

bool plVisualScriptNodeManager::IsTransparentNode(const plDocumentObject* pObject) const
{
  return plVisualScriptNodeDescription::Type::IsTransparent(GetNodeType(pObject));
}

bool plVisualScriptNodeManager::IsReroute(const plDocumentObject* pObject) const
{
  return GetNodeType(pObject) == plVisualScriptNodeDescription::Type::Builtin_Reroute;
}

bool plVisualScriptNodeManager::IsPortalIn(const plDocumentObject* pObject) const
{
  return GetNodeType(pObject) == plVisualScriptNodeDescription::Type::Builtin_PortalIn;
}

bool plVisualScriptNodeManager::IsPortalOut(const plDocumentObject* pObject) const
{
  return GetNodeType(pObject) == plVisualScriptNodeDescription::Type::Builtin_PortalOut;
}

bool plVisualScriptNodeManager::IsExecPortal(const plDocumentObject* pObject) const
{
  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
  const plRTTI* pType = pObject->GetType();
  return pType == pRegistry->GetPortalType(true, true) || pType == pRegistry->GetPortalType(false, true);
}

bool plVisualScriptNodeManager::IsSubGraph(const plDocumentObject* pObject) const
{
  return GetNodeType(pObject) == plVisualScriptNodeDescription::Type::Builtin_SubGraph;
}

bool plVisualScriptNodeManager::IsSubGraphInputs(const plDocumentObject* pObject) const
{
  return GetNodeType(pObject) == plVisualScriptNodeDescription::Type::Builtin_SubGraphInputs;
}

bool plVisualScriptNodeManager::IsSubGraphOutputs(const plDocumentObject* pObject) const
{
  return GetNodeType(pObject) == plVisualScriptNodeDescription::Type::Builtin_SubGraphOutputs;
}

bool plVisualScriptNodeManager::IsFunctionEntry(const plDocumentObject* pObject) const
{
  return plVisualScriptNodeDescription::Type::IsFunctionEntry(GetNodeType(pObject));
}

bool plVisualScriptNodeManager::IsReturn(const plDocumentObject* pObject) const
{
  return GetNodeType(pObject) == plVisualScriptNodeDescription::Type::Builtin_Return;
}

bool plVisualScriptNodeManager::IsCall(const plDocumentObject* pObject) const
{
  return GetNodeType(pObject) == plVisualScriptNodeDescription::Type::Builtin_Call;
}

bool plVisualScriptNodeManager::IsCallLibrary(const plDocumentObject* pObject) const
{
  return GetNodeType(pObject) == plVisualScriptNodeDescription::Type::Builtin_CallLibrary;
}

bool plVisualScriptNodeManager::IsEntryNode(const plDocumentObject* pObject) const
{
  return plVisualScriptNodeDescription::Type::IsEntry(GetNodeType(pObject));
}

// Sub-graph scopes
//////////////////////////////////////////////////////////////////////////

plUuid plVisualScriptNodeManager::GetNodeScope(const plDocumentObject* pObject) const
{
  if (pObject == nullptr || IsNode(pObject) == false || IsComment(pObject))
    return plUuid();

  plVariant scope = pObject->GetTypeAccessor().GetValue(plVisualScriptNodeRegistry::s_szScopeProperty);
  if (scope.IsA<plUuid>() == false)
    return plUuid();

  // a scope whose sub-graph node is gone counts as root, so nothing can get lost in an unreachable scope
  const plUuid scopeGuid = scope.Get<plUuid>();
  if (scopeGuid.IsValid() && GetObject(scopeGuid) == nullptr)
    return plUuid();

  return scopeGuid;
}

void plVisualScriptNodeManager::GetNodesInScope(const plUuid& scope, plDynamicArray<const plDocumentObject*>& out_nodes, bool bRecursive) const
{
  out_nodes.Clear();

  plHybridArray<plUuid, 8> scopes;
  scopes.PushBack(scope);

  for (plUInt32 uiScopeIndex = 0; uiScopeIndex < scopes.GetCount(); ++uiScopeIndex)
  {
    const plUuid currentScope = scopes[uiScopeIndex];

    for (auto pObject : GetRootObject()->GetChildren())
    {
      if (IsNode(pObject) == false || IsComment(pObject))
        continue;

      if (GetNodeScope(pObject) != currentScope)
        continue;

      out_nodes.PushBack(pObject);

      if (bRecursive && IsSubGraph(pObject))
      {
        scopes.PushBack(pObject->GetGuid());
      }
    }
  }
}

const plDocumentObject* plVisualScriptNodeManager::GetSubGraphBoundary(const plDocumentObject* pSubGraph, bool bInputs) const
{
  if (pSubGraph == nullptr)
    return nullptr;

  const plUuid scope = pSubGraph->GetGuid();
  for (auto pObject : GetRootObject()->GetChildren())
  {
    if (IsNode(pObject) == false)
      continue;

    if ((bInputs ? IsSubGraphInputs(pObject) : IsSubGraphOutputs(pObject)) && GetNodeScope(pObject) == scope)
      return pObject;
  }

  return nullptr;
}

const plDocumentObject* plVisualScriptNodeManager::GetSubGraphOwner(const plDocumentObject* pBoundary) const
{
  const plUuid scope = GetNodeScope(pBoundary);
  if (scope.IsValid() == false)
    return nullptr;

  const plDocumentObject* pOwner = GetObject(scope);
  return (pOwner != nullptr && IsSubGraph(pOwner)) ? pOwner : nullptr;
}

plUuid plVisualScriptNodeManager::GetDisplayScope(const plDocumentObject* pObject) const
{
  if (pObject != nullptr && IsNode(pObject) && IsFunctionEntry(pObject))
    return pObject->GetGuid();

  return GetNodeScope(pObject);
}

bool plVisualScriptNodeManager::GetParentScope(const plUuid& scope, plUuid& out_parent) const
{
  out_parent = plUuid();

  if (scope.IsValid() == false)
    return false;

  const plDocumentObject* pOwner = GetObject(scope);
  if (pOwner == nullptr || IsNode(pOwner) == false || IsSubGraph(pOwner) == false)
    return false;

  out_parent = GetNodeScope(pOwner);
  return true;
}

const plDocumentObject* plVisualScriptNodeManager::GetScopeFunction(const plUuid& scope) const
{
  plHybridArray<plUuid, 8> visited;
  plUuid current = scope;
  while (current.IsValid() && visited.Contains(current) == false)
  {
    visited.PushBack(current);

    const plDocumentObject* pOwner = GetObject(current);
    if (pOwner == nullptr || IsNode(pOwner) == false)
      return nullptr;

    if (IsFunctionEntry(pOwner))
      return pOwner;

    current = GetNodeScope(pOwner);
  }

  return nullptr;
}

plString plVisualScriptNodeManager::GetRootGraphName() const
{
  if (m_bIsLibrary)
    return "Library";

  if (GetRootObject()->GetChildren().IsEmpty() == false)
  {
    plVariant name = GetRootObject()->GetChildren()[0]->GetTypeAccessor().GetValue("GraphName");
    if (name.IsA<plString>() && name.Get<plString>().IsEmpty() == false)
      return name.Get<plString>();
  }

  return "Event Graph";
}

void plVisualScriptNodeManager::GetBreadcrumbPath(plDynamicArray<plVisualScriptBreadcrumbEntry>& out_path) const
{
  out_path.Clear();

  // walk up to the root of the current graph: the event graph or a function entry
  plHybridArray<const plDocumentObject*, 8> chain;
  const plDocumentObject* pFunction = nullptr;
  plUuid scope = m_CurrentViewScope;
  while (scope.IsValid())
  {
    const plDocumentObject* pOwner = GetObject(scope);
    if (pOwner == nullptr || IsNode(pOwner) == false || chain.Contains(pOwner))
      break;

    if (IsFunctionEntry(pOwner))
    {
      pFunction = pOwner;
      break;
    }

    chain.PushBack(pOwner);
    scope = GetNodeScope(pOwner);
  }

  auto& root = out_path.ExpandAndGetRef();
  if (pFunction != nullptr)
  {
    root.m_Guid = pFunction->GetGuid();
    root.m_sDisplayName = GetStringProperty(pFunction, "Name");
    if (root.m_sDisplayName.IsEmpty())
      root.m_sDisplayName = "Function";
  }
  else
  {
    root.m_sDisplayName = GetRootGraphName();
  }

  for (plUInt32 i = chain.GetCount(); i-- > 0;)
  {
    auto& entry = out_path.ExpandAndGetRef();
    entry.m_Guid = chain[i]->GetGuid();
    entry.m_sDisplayName = GetStringProperty(chain[i], "Name");
    if (entry.m_sDisplayName.IsEmpty())
      entry.m_sDisplayName = "Sub-graph";
  }
}

void plVisualScriptNodeManager::MigrateFunctionScopes()
{
  // Runs once after loading, outside the command history: the objects are written directly.
  plHybridArray<const plDocumentObject*, 8> entries;
  GetFunctionEntries(entries);
  if (entries.IsEmpty())
    return;

  plHybridArray<const plDocumentObject*, 64> body;
  for (const plDocumentObject* pEntry : entries)
  {
    if (GetNodeScope(pEntry).IsValid())
      continue;

    const plString sName = GetStringProperty(pEntry, "Name");

    // everything reachable from the entry through any connection that still sits in the event graph belongs to the function
    body.Clear();
    body.PushBack(pEntry);
    for (plUInt32 i = 0; i < body.GetCount(); ++i)
    {
      auto Visit = [&](const plDocumentObject* pOther)
      {
        if (pOther == nullptr || IsNode(pOther) == false || IsComment(pOther) || body.Contains(pOther))
          return;
        if (IsEntryNode(pOther) || GetNodeScope(pOther).IsValid())
          return;
        body.PushBack(pOther);
      };

      for (auto& pPin : GetOutputPins(body[i]))
      {
        for (const plVisualGraphConnection* pConnection : GetConnections(*pPin))
          Visit(pConnection->GetTargetPin().GetParent());
      }
      for (auto& pPin : GetInputPins(body[i]))
      {
        for (const plVisualGraphConnection* pConnection : GetConnections(*pPin))
          Visit(pConnection->GetSourcePin().GetParent());
      }
    }

    // unconnected returns of this function come along too
    for (auto pObject : GetRootObject()->GetChildren())
    {
      if (IsNode(pObject) && IsReturn(pObject) && GetNodeScope(pObject).IsValid() == false && body.Contains(pObject) == false && GetStringProperty(pObject, "Function") == sName)
        body.PushBack(pObject);
    }

    for (plUInt32 i = 1; i < body.GetCount(); ++i)
    {
      const_cast<plDocumentObject*>(body[i])->GetTypeAccessor().SetValue(plVisualScriptNodeRegistry::s_szScopeProperty, pEntry->GetGuid());
    }
  }
}

// Logical graph walking
//////////////////////////////////////////////////////////////////////////

void plVisualScriptNodeManager::ForwardThroughTransparent(const plVisualScriptPin& inputPin, plDynamicArray<const plVisualScriptPin*>& out_targets, plHashSet<const plVisualScriptPin*>& inout_visited, const plVisualGraphConnection* pIgnoredConnection) const
{
  if (inout_visited.Insert(&inputPin) || inout_visited.GetCount() > s_uiMaxTransparentDepth)
    return;

  const plDocumentObject* pNode = inputPin.GetParent();
  plHybridArray<const plVisualScriptPin*, 4> outputPins;

  auto FollowOutput = [&](const plVisualScriptPin* pOutputPin)
  {
    if (pOutputPin == nullptr)
      return;

    for (auto pConnection : GetConnections(*pOutputPin))
    {
      if (pConnection == pIgnoredConnection)
        continue;

      auto& targetPin = static_cast<const plVisualScriptPin&>(pConnection->GetTargetPin());
      if (IsTransparentNode(targetPin.GetParent()))
        ForwardThroughTransparent(targetPin, out_targets, inout_visited, pIgnoredConnection);
      else
        out_targets.PushBack(&targetPin);
    }
  };

  switch (GetNodeType(pNode))
  {
    case plVisualScriptNodeDescription::Type::Builtin_Reroute:
    {
      auto outputs = GetOutputPins(pNode);
      FollowOutput(outputs.IsEmpty() ? nullptr : static_cast<const plVisualScriptPin*>(outputs[0].Borrow()));
    }
    break;

    case plVisualScriptNodeDescription::Type::Builtin_PortalIn:
    {
      plHybridArray<const plDocumentObject*, 8> portalOuts;
      GetPortalOuts(GetPortalName(pNode), IsExecPortal(pNode), portalOuts);
      for (auto pPortalOut : portalOuts)
      {
        auto outputs = GetOutputPins(pPortalOut);
        FollowOutput(outputs.IsEmpty() ? nullptr : static_cast<const plVisualScriptPin*>(outputs[0].Borrow()));
      }
    }
    break;

    case plVisualScriptNodeDescription::Type::Builtin_SubGraph:
    {
      // into the sub-graph: the boundary 'Inputs' node has an output pin with the same name
      if (const plDocumentObject* pBoundary = GetSubGraphBoundary(pNode, true))
        FollowOutput(static_cast<const plVisualScriptPin*>(GetOutputPinByName(pBoundary, inputPin.GetName())));
    }
    break;

    case plVisualScriptNodeDescription::Type::Builtin_SubGraphOutputs:
    {
      // out of the sub-graph: the owning sub-graph node has an output pin with the same name
      if (const plDocumentObject* pOwner = GetSubGraphOwner(pNode))
        FollowOutput(static_cast<const plVisualScriptPin*>(GetOutputPinByName(pOwner, inputPin.GetName())));
    }
    break;

    default:
      break;
  }
}

void plVisualScriptNodeManager::BackwardThroughTransparent(const plVisualScriptPin& outputPin, plDynamicArray<const plVisualScriptPin*>& out_sources, plHashSet<const plVisualScriptPin*>& inout_visited) const
{
  if (inout_visited.Insert(&outputPin) || inout_visited.GetCount() > s_uiMaxTransparentDepth)
    return;

  const plDocumentObject* pNode = outputPin.GetParent();

  auto FollowInput = [&](const plVisualScriptPin* pInputPin)
  {
    if (pInputPin == nullptr)
      return;

    for (auto pConnection : GetConnections(*pInputPin))
    {
      auto& sourcePin = static_cast<const plVisualScriptPin&>(pConnection->GetSourcePin());
      if (IsTransparentNode(sourcePin.GetParent()))
        BackwardThroughTransparent(sourcePin, out_sources, inout_visited);
      else
        out_sources.PushBack(&sourcePin);
    }
  };

  switch (GetNodeType(pNode))
  {
    case plVisualScriptNodeDescription::Type::Builtin_Reroute:
    {
      auto inputs = GetInputPins(pNode);
      FollowInput(inputs.IsEmpty() ? nullptr : static_cast<const plVisualScriptPin*>(inputs[0].Borrow()));
    }
    break;

    case plVisualScriptNodeDescription::Type::Builtin_PortalOut:
    {
      const bool bExec = IsExecPortal(pNode);
      const plString sName = GetPortalName(pNode);
      for (auto pObject : GetRootObject()->GetChildren())
      {
        if (IsNode(pObject) && IsPortalIn(pObject) && IsExecPortal(pObject) == bExec && GetPortalName(pObject) == sName)
        {
          auto inputs = GetInputPins(pObject);
          FollowInput(inputs.IsEmpty() ? nullptr : static_cast<const plVisualScriptPin*>(inputs[0].Borrow()));
        }
      }
    }
    break;

    case plVisualScriptNodeDescription::Type::Builtin_SubGraphInputs:
    {
      if (const plDocumentObject* pOwner = GetSubGraphOwner(pNode))
        FollowInput(static_cast<const plVisualScriptPin*>(GetInputPinByName(pOwner, outputPin.GetName())));
    }
    break;

    case plVisualScriptNodeDescription::Type::Builtin_SubGraph:
    {
      if (const plDocumentObject* pBoundary = GetSubGraphBoundary(pNode, false))
        FollowInput(static_cast<const plVisualScriptPin*>(GetInputPinByName(pBoundary, outputPin.GetName())));
    }
    break;

    default:
      break;
  }
}

void plVisualScriptNodeManager::GetLogicalTargets(const plVisualScriptPin& outputPin, plDynamicArray<const plVisualScriptPin*>& out_targets, const plVisualGraphConnection* pIgnoredConnection /*= nullptr*/) const
{
  out_targets.Clear();

  plHashSet<const plVisualScriptPin*> visited;
  for (auto pConnection : GetConnections(outputPin))
  {
    if (pConnection == pIgnoredConnection)
      continue;

    auto& targetPin = static_cast<const plVisualScriptPin&>(pConnection->GetTargetPin());
    if (IsTransparentNode(targetPin.GetParent()))
      ForwardThroughTransparent(targetPin, out_targets, visited, pIgnoredConnection);
    else
      out_targets.PushBack(&targetPin);
  }
}

void plVisualScriptNodeManager::GetLogicalSources(const plVisualScriptPin& inputPin, plDynamicArray<const plVisualScriptPin*>& out_sources) const
{
  out_sources.Clear();

  plHashSet<const plVisualScriptPin*> visited;
  for (auto pConnection : GetConnections(inputPin))
  {
    auto& sourcePin = static_cast<const plVisualScriptPin&>(pConnection->GetSourcePin());
    if (IsTransparentNode(sourcePin.GetParent()))
      BackwardThroughTransparent(sourcePin, out_sources, visited);
    else
      out_sources.PushBack(&sourcePin);
  }
}

plResult plVisualScriptNodeManager::ResolveExecTarget(const plVisualScriptPin& outputPin, const plVisualScriptPin*& out_pTargetPin) const
{
  out_pTargetPin = nullptr;

  plHybridArray<const plVisualScriptPin*, 4> targets;
  GetLogicalTargets(outputPin, targets);

  if (targets.IsEmpty())
    return PL_SUCCESS;

  if (targets.GetCount() > 1)
  {
    plLog::Error("The execution pin '{}.{}' reaches more than one node, probably through a portal with several outs", GetNiceTypeName(outputPin.GetParent()), outputPin.GetName());
    return PL_FAILURE;
  }

  out_pTargetPin = targets[0];
  return PL_SUCCESS;
}

plResult plVisualScriptNodeManager::ResolveDataSource(const plVisualScriptPin& inputPin, const plVisualScriptPin*& out_pSourcePin) const
{
  out_pSourcePin = nullptr;

  plHybridArray<const plVisualScriptPin*, 4> sources;
  GetLogicalSources(inputPin, sources);

  if (sources.IsEmpty())
    return PL_SUCCESS;

  if (sources.GetCount() > 1)
  {
    plLog::Error("The data pin '{}.{}' is fed by more than one node, probably through a portal with several ins", GetNiceTypeName(inputPin.GetParent()), inputPin.GetName());
    return PL_FAILURE;
  }

  out_pSourcePin = sources[0];
  return PL_SUCCESS;
}

bool plVisualScriptNodeManager::CanReachNodeLogically(const plDocumentObject* pSource, const plDocumentObject* pTarget) const
{
  if (pSource == nullptr || pTarget == nullptr)
    return false;

  plHybridArray<const plDocumentObject*, 64> nodeStack;
  nodeStack.PushBack(pSource);

  plHashSet<const plDocumentObject*> visited;
  plHybridArray<const plVisualScriptPin*, 8> targets;

  while (nodeStack.IsEmpty() == false)
  {
    const plDocumentObject* pNode = nodeStack.PeekBack();
    nodeStack.PopBack();

    if (pNode == pTarget)
      return true;

    if (visited.Insert(pNode))
      continue;

    if (IsTransparentNode(pNode))
    {
      // a transparent node forwards each of its inputs
      plHashSet<const plVisualScriptPin*> visitedPins;
      for (auto& pInput : GetInputPins(pNode))
      {
        targets.Clear();
        ForwardThroughTransparent(static_cast<const plVisualScriptPin&>(*pInput), targets, visitedPins, nullptr);
        for (auto pTargetPin : targets)
          nodeStack.PushBack(pTargetPin->GetParent());
      }

      // a transparent node with unconnected inputs still leads somewhere through its outputs
      for (auto& pOutput : GetOutputPins(pNode))
      {
        GetLogicalTargets(static_cast<const plVisualScriptPin&>(*pOutput), targets);
        for (auto pTargetPin : targets)
          nodeStack.PushBack(pTargetPin->GetParent());
      }
      continue;
    }

    for (auto& pOutput : GetOutputPins(pNode))
    {
      GetLogicalTargets(static_cast<const plVisualScriptPin&>(*pOutput), targets);
      for (auto pTargetPin : targets)
        nodeStack.PushBack(pTargetPin->GetParent());
    }
  }

  return false;
}

// Functions, portals, libraries
//////////////////////////////////////////////////////////////////////////

plString plVisualScriptNodeManager::GetPortalName(const plDocumentObject* pPortal) const
{
  return plString(GetStringProperty(pPortal, "Name"));
}

const plDocumentObject* plVisualScriptNodeManager::FindPortalIn(plStringView sName, bool bExec) const
{
  if (sName.IsEmpty())
    return nullptr;

  for (auto pObject : GetRootObject()->GetChildren())
  {
    if (IsNode(pObject) && IsPortalIn(pObject) && IsExecPortal(pObject) == bExec && GetStringProperty(pObject, "Name") == sName)
      return pObject;
  }

  return nullptr;
}

void plVisualScriptNodeManager::GetPortalOuts(plStringView sName, bool bExec, plDynamicArray<const plDocumentObject*>& out_portals) const
{
  out_portals.Clear();

  if (sName.IsEmpty())
    return;

  for (auto pObject : GetRootObject()->GetChildren())
  {
    if (IsNode(pObject) && IsPortalOut(pObject) && IsExecPortal(pObject) == bExec && GetStringProperty(pObject, "Name") == sName)
      out_portals.PushBack(pObject);
  }
}

const plDocumentObject* plVisualScriptNodeManager::FindFunctionEntry(plStringView sName) const
{
  if (sName.IsEmpty())
    return nullptr;

  for (auto pObject : GetRootObject()->GetChildren())
  {
    if (IsNode(pObject) && IsFunctionEntry(pObject) && GetStringProperty(pObject, "Name") == sName)
      return pObject;
  }

  return nullptr;
}

void plVisualScriptNodeManager::GetFunctionEntries(plDynamicArray<const plDocumentObject*>& out_entries) const
{
  out_entries.Clear();

  for (auto pObject : GetRootObject()->GetChildren())
  {
    if (IsNode(pObject) && IsFunctionEntry(pObject))
      out_entries.PushBack(pObject);
  }
}

plString plVisualScriptNodeManager::GetFunctionName(const plDocumentObject* pEntry) const
{
  return GetStringProperty(pEntry, "Name");
}

const plDocumentObject* plVisualScriptNodeManager::FindReturnOwner(const plDocumentObject* pReturn) const
{
  // the graph the return sits in decides; the execution flow is only consulted for returns that are still in the event graph
  if (const plDocumentObject* pFunction = GetScopeFunction(GetNodeScope(pReturn)))
    return pFunction;

  plHybridArray<const plDocumentObject*, 4> entries;
  GetEntryNodes(pReturn, entries);

  for (auto pEntry : entries)
  {
    if (IsFunctionEntry(pEntry))
      return pEntry;
  }

  return nullptr;
}

const plVisualScriptLibraryMetaData* plVisualScriptNodeManager::GetLibraryMetaData(plStringView sLibrary) const
{
  if (sLibrary.IsEmpty() || plAssetCurator::GetSingleton() == nullptr)
    return nullptr;

  auto pSubAsset = plAssetCurator::GetSingleton()->FindSubAsset(sLibrary);
  if (pSubAsset.isValid() == false || pSubAsset->m_pAssetInfo == nullptr || pSubAsset->m_pAssetInfo->m_Info == nullptr)
    return nullptr;

  return pSubAsset->m_pAssetInfo->m_Info->GetMetaInfo<plVisualScriptLibraryMetaData>();
}

void plVisualScriptNodeManager::GetMirroredPinDependents(const plDocumentObject* pObject, plStringView sProperty, plDynamicArray<const plDocumentObject*>& out_dependents) const
{
  out_dependents.Clear();

  if (pObject == nullptr)
    return;

  // a pin declaration is a child of the node that owns the array
  if (IsNode(pObject) == false)
  {
    const plDocumentObject* pParent = pObject->GetParent();
    if (pParent == nullptr || IsNode(pParent) == false)
      return;

    GetMirroredPinDependents(pParent, pObject->GetParentProperty(), out_dependents);
    return;
  }

  if (IsFunctionEntry(pObject))
  {
    const plString sFunctionName = GetStringProperty(pObject, "Name");
    const bool bInputs = sProperty == "Inputs";
    const bool bOutputs = sProperty == "Outputs";
    const bool bName = sProperty == "Name";
    if (bInputs == false && bOutputs == false && bName == false)
      return;

    for (auto pNode : GetRootObject()->GetChildren())
    {
      if (IsNode(pNode) == false)
        continue;

      if ((bInputs || bOutputs || bName) && IsCall(pNode) && GetStringProperty(pNode, "Function") == sFunctionName)
        out_dependents.PushBack(pNode);
      else if ((bOutputs || bName) && IsReturn(pNode) && GetStringProperty(pNode, "Function") == sFunctionName)
        out_dependents.PushBack(pNode);
    }
  }
  else if (IsSubGraphBoundary(pObject) && sProperty == "Pins")
  {
    if (const plDocumentObject* pOwner = GetSubGraphOwner(pObject))
      out_dependents.PushBack(pOwner);
  }
}

void plVisualScriptNodeManager::RefreshDynamicEnums(const plDocumentObject* pSelectedNode /*= nullptr*/) const
{
  auto& execPortals = plDynamicStringEnum::CreateDynamicEnum("VisualScriptPortalNames_Exec");
  auto& dataPortals = plDynamicStringEnum::CreateDynamicEnum("VisualScriptPortalNames_Data");
  auto& functions = plDynamicStringEnum::CreateDynamicEnum("VisualScriptFunctions");
  execPortals.Clear();
  dataPortals.Clear();
  functions.Clear();

  for (auto pObject : GetRootObject()->GetChildren())
  {
    if (IsNode(pObject) == false)
      continue;

    if (IsPortalIn(pObject))
    {
      const plString sName = GetPortalName(pObject);
      if (sName.IsEmpty() == false)
        (IsExecPortal(pObject) ? execPortals : dataPortals).AddValidValue(sName, true);
    }
    else if (IsFunctionEntry(pObject))
    {
      const plString sName = GetStringProperty(pObject, "Name");
      if (sName.IsEmpty() == false)
        functions.AddValidValue(sName, true);
    }
  }

  // the library functions depend on the library assigned to the selected call node
  auto& libraryFunctions = plDynamicStringEnum::CreateDynamicEnum("VisualScriptLibraryFunctions");
  libraryFunctions.Clear();

  if (pSelectedNode != nullptr && IsNode(pSelectedNode) && IsCallLibrary(pSelectedNode))
  {
    if (auto pMeta = GetLibraryMetaData(GetStringProperty(pSelectedNode, "Library")))
    {
      for (const auto& function : pMeta->m_Functions)
        libraryFunctions.AddValidValue(function.m_sName, true);
    }
  }
}

void plVisualScriptNodeManager::AssetCuratorEventHandler(const plAssetCuratorEvent& e)
{
  if (e.m_Type != plAssetCuratorEvent::Type::AssetUpdated)
    return;

  // TryRecreatePins adds and removes connection objects, so collect the affected nodes first
  plHybridArray<const plDocumentObject*, 16> affectedNodes;
  for (auto pObject : GetRootObject()->GetChildren())
  {
    if (IsNode(pObject) == false || IsCallLibrary(pObject) == false)
      continue;

    const plString sLibrary = GetStringProperty(pObject, "Library");
    if (plConversionUtils::IsStringUuid(sLibrary) && plConversionUtils::ConvertStringToUuid(sLibrary) == e.m_AssetGuid)
    {
      affectedNodes.PushBack(pObject);
    }
  }

  for (auto pObject : affectedNodes)
  {
    TryRecreatePins(pObject);
    m_NodeChangedEvent.Broadcast(pObject);
  }

  if (affectedNodes.IsEmpty() == false)
  {
    RecomputeAllCoroutines();
  }
}

// Mirrored pins
//////////////////////////////////////////////////////////////////////////

void plVisualScriptNodeManager::AddDeclaredPins(const plDocumentObject* pDeclOwner, plStringView sArrayProperty, plVisualGraphPin::Type type, const plDocumentObject* pPinOwner, plDynamicArray<plUniquePtr<plVisualGraphPin>>& out_pins, plUInt32& inout_uiDataPinIndex)
{
  if (pDeclOwner == nullptr)
    return;

  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
  auto& typeAccessor = pDeclOwner->GetTypeAccessor();

  plHashSet<plString> usedNames;
  plStringBuilder sPinName;

  const plInt32 iCount = typeAccessor.GetCount(sArrayProperty);
  for (plInt32 i = 0; i < iCount; ++i)
  {
    plVariant declUuid = typeAccessor.GetValue(sArrayProperty, i);
    if (declUuid.IsA<plUuid>() == false)
      continue;

    const plDocumentObject* pDecl = GetObject(declUuid.Get<plUuid>());
    if (pDecl == nullptr)
      continue;

    sPinName = GetStringProperty(pDecl, "Name");
    if (sPinName.IsEmpty())
      sPinName.SetFormat("Pin{}", i);

    // pins are addressed by name, so keep them unique
    plStringBuilder sUniqueName = sPinName;
    for (plUInt32 uiSuffix = 2; usedNames.Contains(sUniqueName); ++uiSuffix)
    {
      sUniqueName.SetFormat("{}{}", sPinName, uiSuffix);
    }
    usedNames.Insert(sUniqueName);

    plVariant typeVar = pDecl->GetTypeAccessor().GetValue("Type");
    const auto pinType = typeVar.IsA<plInt64>() ? static_cast<plVisualScriptPinType::Enum>(typeVar.Get<plInt64>()) : plVisualScriptPinType::Default;
    const auto dataType = plVisualScriptPinType::GetDataType(pinType);

    if (dataType < 0 || dataType >= plVisualScriptDataType::Count)
      continue;

    const auto& pinDesc = pRegistry->GetGenericPinDesc(dataType);

    plUInt32 uiDataPinIndex = plInvalidIndex;
    if (pinDesc.IsDataPin())
    {
      uiDataPinIndex = inout_uiDataPinIndex;
      ++inout_uiDataPinIndex;
    }

    auto pPin = PL_DEFAULT_NEW(plVisualScriptPin, type, sUniqueName, pinDesc, pPinOwner, uiDataPinIndex, static_cast<plUInt32>(i));
    out_pins.PushBack(pPin);
  }
}

void plVisualScriptNodeManager::AddLibraryPins(const plDocumentObject* pObject, NodeInternal& ref_node)
{
  auto pMeta = GetLibraryMetaData(GetStringProperty(pObject, "Library"));
  if (pMeta == nullptr)
    return;

  auto pFunction = pMeta->FindFunction(GetStringProperty(pObject, "Function"));
  if (pFunction == nullptr)
    return;

  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();

  auto AddPins = [&](const plDynamicArray<plVisualScriptLibraryPinInfo>& infos, plVisualGraphPin::Type type, plDynamicArray<plUniquePtr<plVisualGraphPin>>& out_pins)
  {
    plUInt32 uiDataPinIndex = 0;
    for (plUInt32 i = 0; i < infos.GetCount(); ++i)
    {
      const auto dataType = plVisualScriptPinType::GetDataType(infos[i].m_Type);
      if (dataType < 0 || dataType >= plVisualScriptDataType::Count || dataType == plVisualScriptDataType::Invalid)
        continue;

      auto pPin = PL_DEFAULT_NEW(plVisualScriptPin, type, infos[i].m_sName, pRegistry->GetGenericPinDesc(dataType), pObject, uiDataPinIndex, i);
      out_pins.PushBack(pPin);
      ++uiDataPinIndex;
    }
  };

  AddPins(pFunction->m_Inputs, plVisualGraphPin::Type::Input, ref_node.m_Inputs);
  AddPins(pFunction->m_Outputs, plVisualGraphPin::Type::Output, ref_node.m_Outputs);
}

bool plVisualScriptNodeManager::CreateMirroredPins(const plDocumentObject* pObject, const plVisualScriptNodeRegistry::NodeDesc& nodeDesc, NodeInternal& ref_node)
{
  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
  const auto& execPinDesc = pRegistry->GetGenericPinDesc(plVisualScriptDataType::Invalid);

  auto AddExecPin = [&](plVisualGraphPin::Type type, plStringView sName, plDynamicArray<plUniquePtr<plVisualGraphPin>>& out_pins)
  {
    out_pins.PushBack(PL_DEFAULT_NEW(plVisualScriptPin, type, sName, execPinDesc, pObject, plInvalidIndex, 0));
  };

  plUInt32 uiDataPinIndex = 0;

  switch (nodeDesc.m_Type)
  {
    case plVisualScriptNodeDescription::Type::Builtin_Reroute:
    {
      plVariant kind = pObject->GetTypeAccessor().GetValue("Kind");
      if (kind.IsA<plInt64>() && kind.Get<plInt64>() == plVisualScriptPinKind::Exec)
      {
        AddExecPin(plVisualGraphPin::Type::Input, "In", ref_node.m_Inputs);
        AddExecPin(plVisualGraphPin::Type::Output, "Out", ref_node.m_Outputs);
      }
      else
      {
        // the Any pins of the node description carry the type deduction
        ref_node.m_Inputs.PushBack(PL_DEFAULT_NEW(plVisualScriptPin, plVisualGraphPin::Type::Input, "In", nodeDesc.m_InputPins[0], pObject, 0, 0));
        ref_node.m_Outputs.PushBack(PL_DEFAULT_NEW(plVisualScriptPin, plVisualGraphPin::Type::Output, "Out", nodeDesc.m_OutputPins[0], pObject, 0, 0));
      }
      return true;
    }

    case plVisualScriptNodeDescription::Type::Builtin_SubGraphInputs:
      AddDeclaredPins(pObject, "Pins", plVisualGraphPin::Type::Output, pObject, ref_node.m_Outputs, uiDataPinIndex);
      return true;

    case plVisualScriptNodeDescription::Type::Builtin_SubGraphOutputs:
      AddDeclaredPins(pObject, "Pins", plVisualGraphPin::Type::Input, pObject, ref_node.m_Inputs, uiDataPinIndex);
      return true;

    case plVisualScriptNodeDescription::Type::Builtin_SubGraph:
    {
      AddDeclaredPins(GetSubGraphBoundary(pObject, true), "Pins", plVisualGraphPin::Type::Input, pObject, ref_node.m_Inputs, uiDataPinIndex);
      uiDataPinIndex = 0;
      AddDeclaredPins(GetSubGraphBoundary(pObject, false), "Pins", plVisualGraphPin::Type::Output, pObject, ref_node.m_Outputs, uiDataPinIndex);
      return true;
    }

    case plVisualScriptNodeDescription::Type::FunctionEntry:
    case plVisualScriptNodeDescription::Type::FunctionEntry_Coroutine:
    {
      AddExecPin(plVisualGraphPin::Type::Output, "", ref_node.m_Outputs);
      AddDeclaredPins(pObject, "Inputs", plVisualGraphPin::Type::Output, pObject, ref_node.m_Outputs, uiDataPinIndex);
      return true;
    }

    case plVisualScriptNodeDescription::Type::Builtin_Return:
    {
      AddExecPin(plVisualGraphPin::Type::Input, "", ref_node.m_Inputs);
      AddDeclaredPins(FindFunctionEntry(GetStringProperty(pObject, "Function")), "Outputs", plVisualGraphPin::Type::Input, pObject, ref_node.m_Inputs, uiDataPinIndex);
      return true;
    }

    case plVisualScriptNodeDescription::Type::Builtin_Call:
    {
      const plDocumentObject* pEntry = FindFunctionEntry(GetStringProperty(pObject, "Function"));
      AddExecPin(plVisualGraphPin::Type::Input, "", ref_node.m_Inputs);
      AddExecPin(plVisualGraphPin::Type::Output, "", ref_node.m_Outputs);
      AddDeclaredPins(pEntry, "Inputs", plVisualGraphPin::Type::Input, pObject, ref_node.m_Inputs, uiDataPinIndex);
      uiDataPinIndex = 0;
      AddDeclaredPins(pEntry, "Outputs", plVisualGraphPin::Type::Output, pObject, ref_node.m_Outputs, uiDataPinIndex);
      return true;
    }

    case plVisualScriptNodeDescription::Type::Builtin_CallLibrary:
    {
      AddExecPin(plVisualGraphPin::Type::Input, "", ref_node.m_Inputs);
      AddExecPin(plVisualGraphPin::Type::Output, "", ref_node.m_Outputs);
      AddLibraryPins(pObject, ref_node);
      return true;
    }

    default:
      return false;
  }
}

// Validation
//////////////////////////////////////////////////////////////////////////

bool plVisualScriptNodeManager::IsPinRequired(const plVisualScriptPin& pin) const
{
  if (pin.IsRequired())
    return true;

  // libraries have no script owner, so game object and world targets have to be connected explicitly
  if (m_bIsLibrary && pin.GetType() == plVisualGraphPin::Type::Input && pin.IsDataPin())
  {
    const plDocumentObject* pNode = pin.GetParent();
    const auto type = GetNodeType(pNode);
    if (type == plVisualScriptNodeDescription::Type::ReflectedFunction || type == plVisualScriptNodeDescription::Type::GetReflectedProperty || type == plVisualScriptNodeDescription::Type::SetReflectedProperty)
    {
      if (pin.GetDataPinIndex() == 0)
      {
        const plRTTI* pDataType = pin.GetDataType();
        if (pDataType != nullptr && (pDataType->IsDerivedFrom<plGameObject>() || pDataType->IsDerivedFrom<plWorld>()))
          return true;
      }
    }
  }

  return false;
}

bool plVisualScriptNodeManager::GetNodeError(const plDocumentObject* pObject, plStringBuilder& out_sError) const
{
  out_sError.Clear();

  if (pObject == nullptr || IsNode(pObject) == false || IsComment(pObject))
    return false;

  const auto type = GetNodeType(pObject);

  if (plVisualScriptNodeDescription::Type::IsEntry(type) && GetNodeScope(pObject).IsValid())
  {
    out_sError = "Entry nodes must be in the root graph, not inside a sub-graph";
    return true;
  }

  if (IsPortalOut(pObject))
  {
    const plString sName = GetPortalName(pObject);
    if (sName.IsEmpty())
    {
      out_sError = "Select the name of a Portal In";
      return true;
    }
    if (FindPortalIn(sName, IsExecPortal(pObject)) == nullptr)
    {
      out_sError.SetFormat("There is no Portal In named '{}'", sName);
      return true;
    }
  }
  else if (IsPortalIn(pObject))
  {
    const plString sName = GetPortalName(pObject);
    if (sName.IsEmpty())
    {
      out_sError = "The portal needs a name";
      return true;
    }

    const bool bExec = IsExecPortal(pObject);
    for (auto pOther : GetRootObject()->GetChildren())
    {
      if (pOther != pObject && IsNode(pOther) && IsPortalIn(pOther) && IsExecPortal(pOther) == bExec && GetPortalName(pOther) == sName)
      {
        out_sError.SetFormat("There is more than one Portal In named '{}'", sName);
        return true;
      }
    }

    if (bExec)
    {
      plHybridArray<const plDocumentObject*, 4> outs;
      GetPortalOuts(sName, true, outs);
      if (outs.GetCount() > 1)
      {
        out_sError.SetFormat("An execution portal can only have one Portal Out, '{}' has {}", sName, outs.GetCount());
        return true;
      }
    }
  }
  else if (IsCall(pObject))
  {
    const plString sFunction = GetStringProperty(pObject, "Function");
    if (sFunction.IsEmpty())
    {
      out_sError = "Select a function to call";
      return true;
    }
    if (FindFunctionEntry(sFunction) == nullptr)
    {
      out_sError.SetFormat("There is no function named '{}'", sFunction);
      return true;
    }
  }
  else if (IsCallLibrary(pObject))
  {
    const plString sLibrary = GetStringProperty(pObject, "Library");
    const plString sFunction = GetStringProperty(pObject, "Function");
    if (sLibrary.IsEmpty())
    {
      out_sError = "Select a library";
      return true;
    }
    auto pMeta = GetLibraryMetaData(sLibrary);
    if (pMeta == nullptr)
    {
      out_sError = "The library asset could not be found or has not been saved yet";
      return true;
    }
    if (sFunction.IsEmpty() || pMeta->FindFunction(sFunction) == nullptr)
    {
      out_sError.SetFormat("The library has no function named '{}'", sFunction);
      return true;
    }
  }
  else if (IsReturn(pObject))
  {
    if (FindReturnOwner(pObject) == nullptr)
    {
      out_sError = "Return only works inside a function graph";
      return true;
    }
  }
  else if (IsFunctionEntry(pObject))
  {
    const plString sName = GetStringProperty(pObject, "Name");
    if (sName.IsEmpty())
    {
      out_sError = "The function needs a name";
      return true;
    }
    for (auto pOther : GetRootObject()->GetChildren())
    {
      if (pOther != pObject && IsNode(pOther) && IsFunctionEntry(pOther) && GetStringProperty(pOther, "Name") == sName)
      {
        out_sError.SetFormat("There is more than one function named '{}'", sName);
        return true;
      }
    }
  }
  else if (IsSubGraphBoundary(pObject))
  {
    if (GetSubGraphOwner(pObject) == nullptr)
    {
      out_sError = "Sub-graph boundary without a sub-graph";
      return true;
    }
  }

  if (m_bIsLibrary)
  {
    for (auto& pPin : GetInputPins(pObject))
    {
      auto& vsPin = static_cast<const plVisualScriptPin&>(*pPin);
      if (IsPinRequired(vsPin) && vsPin.IsRequired() == false && HasConnections(vsPin) == false)
      {
        out_sError.SetFormat("'{}' must be connected: a library has no script owner to fall back to", vsPin.GetName());
        return true;
      }
    }
  }

  return false;
}

plStatus plVisualScriptNodeManager::ValidateGraph() const
{
  plStringBuilder sErrors, sError;

  for (auto pObject : GetRootObject()->GetChildren())
  {
    if (GetNodeError(pObject, sError))
    {
      sErrors.AppendFormat("{}: {}\n", GetNiceTypeName(pObject), sError);
    }
  }

  if (sErrors.IsEmpty() == false)
  {
    sErrors.Trim("\n");
    return plStatus(sErrors.GetView());
  }

  return plStatus(PL_SUCCESS);
}