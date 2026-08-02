#include <EditorPluginVisualScript/EditorPluginVisualScriptPCH.h>

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptVariable.moc.h>

#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <Foundation/Serialization/GraphPatch.h>
#include <Foundation/Types/VariantTypeRegistry.h>
#include <GuiFoundation/PropertyGrid/PropertyGridWidget.moc.h>
#include <GuiFoundation/PropertyGrid/PropertyMetaState.h>
#include <ToolsFoundation/Object/ObjectAccessorBase.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plVisualScriptVariableType, 1)
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Bool),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Byte),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Int),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Int64),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Float),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Double),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Color),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Vector3),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Quaternion),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Transform),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Time),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Angle),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::String),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::HashedString),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::GameObject),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Component),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::TypedPointer),
  PL_ENUM_CONSTANT(plVisualScriptVariableType::Variant),
  // PL_ENUM_CONSTANT(plVisualScriptVariableType::Resource), // Not yet supported in the editor
PL_END_STATIC_REFLECTED_ENUM;
// clang-format on

static_assert(plVisualScriptVariableType::Variant == plVisualScriptDataType::Variant);
static_assert(plVisualScriptVariableType::Resource == plVisualScriptDataType::Resource);

///////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plVisualScriptVariableCategory, 1)
  PL_ENUM_CONSTANT(plVisualScriptVariableCategory::Member),
  PL_ENUM_CONSTANT(plVisualScriptVariableCategory::Array),
  PL_ENUM_CONSTANT(plVisualScriptVariableCategory::Map), // Maps are not supported yet
PL_END_STATIC_REFLECTED_ENUM;
// clang-format on

// static
plPropertyCategory::Enum plVisualScriptVariableCategory::GetPropertyCategory(Enum category)
{
  switch (category)
  {
    case Member:
      return plPropertyCategory::Member;
    case Array:
      return plPropertyCategory::Array;
    case Map:
      return plPropertyCategory::Map;
    default:
      PL_ASSERT_NOT_IMPLEMENTED;
      return plPropertyCategory::Member;
  }
}

///////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_STATIC_REFLECTED_TYPE(plVisualScriptVariableTypeDeclaration, plNoBase, 1, plRTTIDefaultAllocator<plVisualScriptVariableTypeDeclaration>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ENUM_MEMBER_PROPERTY("Type", plVisualScriptVariableType, m_Type),
    PL_ENUM_MEMBER_PROPERTY("Category", plVisualScriptVariableCategory, m_Category),
    PL_MEMBER_PROPERTY("Public", m_bPublic),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;
PL_DEFINE_CUSTOM_VARIANT_TYPE(plVisualScriptVariableTypeDeclaration);
// clang-format on

plVisualScriptDataType::Enum plVisualScriptVariableTypeDeclaration::GetDataType() const
{
  if (m_Category == plVisualScriptVariableCategory::Array)
  {
    return plVisualScriptDataType::Array;
  }
  else if (m_Category == plVisualScriptVariableCategory::Map)
  {
    return plVisualScriptDataType::Map;
  }

  return static_cast<plVisualScriptDataType::Enum>(m_Type.GetValue());
}

void operator<<(plStreamWriter& inout_stream, const plVisualScriptVariableTypeDeclaration& value)
{
  inout_stream << value.m_Type;
  inout_stream << value.m_Category;
  inout_stream << value.m_bPublic;
}

void operator>>(plStreamReader& inout_stream, plVisualScriptVariableTypeDeclaration& value)
{
  inout_stream >> value.m_Type;
  inout_stream >> value.m_Category;
  inout_stream >> value.m_bPublic;
}

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptVariableAttribute, 1, plRTTIDefaultAllocator<plVisualScriptVariableAttribute>)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

//////////////////////////////////////////////////////////////////////////

plQtVisualScriptVariableWidget::plQtVisualScriptVariableWidget() = default;
plQtVisualScriptVariableWidget::~plQtVisualScriptVariableWidget() = default;

void plQtVisualScriptVariableWidget::InternalSetValue(const plVariant& value)
{
  plQtVariantPropertyWidget::InternalSetValue(value);

  bool bEnableTypeSelection = true;
  for (const auto& item : m_Items)
  {
    plVariant typeDeclVar;
    if (m_pObjectAccessor->GetValueByName(item.m_pObject, "Type", typeDeclVar, item.m_Index).Failed())
      break;

    if (typeDeclVar.GetReflectedType() != plGetStaticRTTI<plVisualScriptVariableTypeDeclaration>())
      break;

    auto typeDecl = typeDeclVar.Get<plVisualScriptVariableTypeDeclaration>();
    if (typeDecl.m_Type != plVisualScriptVariableType::Variant || typeDecl.m_Category != plVisualScriptVariableCategory::Member)
    {
      bEnableTypeSelection = false;
      break;
    }
  }

  EnableTypeSelection(bEnableTypeSelection);
}

plResult plQtVisualScriptVariableWidget::GetVariantTypeDisplayName(plVariantType::Enum type, plStringBuilder& out_sName) const
{
  if (type == plVariantType::Int8 ||
      type == plVariantType::Int16 ||
      type == plVariantType::UInt16 ||
      type == plVariantType::UInt32 ||
      type == plVariantType::UInt64 ||
      type == plVariantType::StringView ||
      type == plVariantType::TempHashedString)
    return PL_FAILURE;

  plVisualScriptDataType::Enum dataType = plVisualScriptDataType::FromVariantType(type);
  if (type != plVariantType::Invalid && dataType == plVisualScriptDataType::Invalid)
    return PL_FAILURE;

  const plRTTI* pVisualScriptDataType = plGetStaticRTTI<plVisualScriptDataType>();
  if (plReflectionUtils::EnumerationToString(pVisualScriptDataType, dataType, out_sName) == false)
    return PL_FAILURE;

  return PL_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////

plQtVisualScriptVariableTypeDeclarationWidget::plQtVisualScriptVariableTypeDeclarationWidget()
{
  m_pLayout = new QHBoxLayout(this);
  m_pLayout->setContentsMargins(0, 0, 0, 4);
  m_pLayout->setSpacing(1);
  setLayout(m_pLayout);

  m_pTypeList = new QComboBox(this);
  m_pTypeList->installEventFilter(this);
  m_pLayout->addWidget(m_pTypeList);

  m_pCategoryList = new QMenu(this);

  m_pCategoryButton = new QPushButton(this);
  m_pCategoryButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  m_pCategoryButton->installEventFilter(this);
  m_pCategoryButton->setMenu(m_pCategoryList);
  m_pLayout->addWidget(m_pCategoryButton);

  m_pVisibilityButton = new QPushButton(this);
  m_pVisibilityButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  QIcon icon;
  icon.addFile(QString::fromUtf8(":/EditorFramework/Icons/ObjectsHidden.svg"), QSize(), QIcon::Normal, QIcon::Off);
  icon.addFile(QString::fromUtf8(":/EditorFramework/Icons/ObjectsVisible.svg"), QSize(), QIcon::Normal, QIcon::On);
  m_pVisibilityButton->setIcon(icon);
  m_pVisibilityButton->setCheckable(true);
  m_pVisibilityButton->setToolTip(QCoreApplication::translate("VisualScriptVariable", "Make variable public or private", nullptr));
  m_pLayout->addWidget(m_pVisibilityButton);
}

plQtVisualScriptVariableTypeDeclarationWidget::~plQtVisualScriptVariableTypeDeclarationWidget() = default;

void plQtVisualScriptVariableTypeDeclarationWidget::OnInit()
{
  plHybridArray<plReflectionUtils::EnumKeyValuePair, 16> enumValues;

  {
    plReflectionUtils::GetEnumKeysAndValues(plGetStaticRTTI<plVisualScriptVariableType>(), enumValues);
    for (auto& val : enumValues)
    {
      m_pTypeList->addItem(plMakeQString(plTranslate(val.m_sKey)), val.m_iValue);
    }

    connect(m_pTypeList, &QComboBox::currentIndexChanged, [this](int iIndex)
      { ChangeType(); });
  }

  {
    QActionGroup* pActionGroup = new QActionGroup(this);
    plReflectionUtils::GetEnumKeysAndValues(plGetStaticRTTI<plVisualScriptVariableCategory>(), enumValues);

    QString sIcons[] = {
      ":/EditorPluginVisualScript/Icons/VariableCategoryMember.svg",
      ":/EditorPluginVisualScript/Icons/VariableCategoryArray.svg",
      ":/EditorPluginVisualScript/Icons/VariableCategoryMap.svg"};

    PL_ASSERT_DEV(PL_ARRAY_SIZE(sIcons) >= enumValues.GetCount(), "Need exactly one icon per category");

    for (plUInt32 i = 0; i < enumValues.GetCount(); ++i)
    {
      QIcon icon;
      icon.addFile(sIcons[i]);

      QAction* pAction = new QAction(icon, plMakeQString(plTranslate(enumValues[i].m_sKey)), this);
      pAction->setCheckable(true);

      connect(pAction, &QAction::triggered, [this]()
        { ChangeType(); });

      pActionGroup->addAction(pAction);
      m_pCategoryList->addAction(pAction);
    }
  }

  connect(m_pVisibilityButton, &QPushButton::toggled, [this](bool)
    { ChangeType(); });
}

void plQtVisualScriptVariableTypeDeclarationWidget::InternalSetValue(const plVariant& value)
{
  auto typeDecl = value.Get<plVisualScriptVariableTypeDeclaration>();

  {
    plQtScopedBlockSignals bs(m_pTypeList);
    for (int i = 0; i < m_pTypeList->count(); ++i)
    {
      if (m_pTypeList->itemData(i).toInt() == typeDecl.m_Type)
      {
        m_pTypeList->setCurrentIndex(i);
        break;
      }
    }
  }

  {
    plQtScopedBlockSignals bs(m_pCategoryList);
    QAction* pAction = m_pCategoryList->actions()[typeDecl.m_Category];
    pAction->setChecked(true);

    m_pCategoryButton->setIcon(pAction->icon());
  }

  {
    plQtScopedBlockSignals bs(m_pVisibilityButton);
    m_pVisibilityButton->setChecked(typeDecl.m_bPublic);
  }
}

void plQtVisualScriptVariableTypeDeclarationWidget::ChangeType()
{
  QAction* pAction = nullptr;
  plUInt32 uiCategory = 0;
  for (auto action : m_pCategoryList->actions())
  {
    if (action->isChecked())
    {
      pAction = action;
      break;
    }
    ++uiCategory;
  }
  PL_ASSERT_DEV(pAction != nullptr, "No category selected");

  m_pObjectAccessor->StartTransaction("Change variable type");

  for (const auto& item : m_Items)
  {
    plVisualScriptVariableTypeDeclaration typeDecl;

    typeDecl.m_Type = static_cast<plVisualScriptVariableType::Enum>(m_pTypeList->currentData().toInt());
    typeDecl.m_Category = static_cast<plVisualScriptVariableCategory::Enum>(uiCategory);
    typeDecl.m_bPublic = m_pVisibilityButton->isChecked();

    m_pObjectAccessor->SetValue(item.m_pObject, m_pProp, typeDecl, item.m_Index).AssertSuccess();
  }

  m_pObjectAccessor->FinishTransaction();

  m_pCategoryButton->setIcon(pAction->icon());
}

///////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_STATIC_REFLECTED_TYPE(plVisualScriptVariable, plNoBase, 2, plRTTIDefaultAllocator<plVisualScriptVariable>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_MEMBER_PROPERTY("Type", m_TypeDecl),
    PL_MEMBER_PROPERTY("DefaultValue", m_DefaultValue)->AddAttributes(new plDefaultValueAttribute(0), new plVisualScriptVariableAttribute()),
    PL_MEMBER_PROPERTY("ClampRange", m_bClampRange),
    PL_MEMBER_PROPERTY("MinValue", m_fMinValue),
    PL_MEMBER_PROPERTY("MaxValue", m_fMaxValue)->AddAttributes(new plDefaultValueAttribute(1)),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;
// clang-format on

// static
void plVisualScriptVariable::ConvertDefaultValue(plVariant& inout_defaultValue, plVisualScriptVariableTypeDeclaration targetTypeDecl)
{
  auto ConvertOrSetToDefault = [](plVariant& v, plVisualScriptDataType::Enum targetType)
  {
    if (targetType == plVisualScriptDataType::Variant)
      return;

    auto variantTargetType = plVisualScriptDataType::GetVariantType(targetType);
    if (variantTargetType == plVariantType::Invalid || variantTargetType == plVariantType::TypedObject || variantTargetType == plVariantType::TypedPointer)
    {
      v = plVariant();
      return;
    }

    plResult res = PL_SUCCESS;
    v = v.ConvertTo(variantTargetType, &res);
    if (res.Failed())
    {
      v = plReflectionUtils::GetDefaultVariantFromType(plVisualScriptDataType::GetRtti(targetType));
    }
  };

  auto targetType = static_cast<plVisualScriptDataType::Enum>(targetTypeDecl.m_Type.GetValue());
  if (targetTypeDecl.m_Category == plVisualScriptVariableCategory::Array)
  {
    if (inout_defaultValue.IsA<plVariantArray>())
    {
      plVariantArray a = inout_defaultValue.Get<plVariantArray>();
      for (auto& v : a)
      {
        ConvertOrSetToDefault(v, targetType);
      }
      inout_defaultValue = a;
    }
    else
    {
      plVariantArray a;
      ConvertOrSetToDefault(inout_defaultValue, targetType);
      a.PushBack(inout_defaultValue);
      inout_defaultValue = a;
    }
  }
  else if (targetTypeDecl.m_Category == plVisualScriptVariableCategory::Map)
  {
    if (inout_defaultValue.IsA<plVariantDictionary>())
    {
      plVariantDictionary d = inout_defaultValue.Get<plVariantDictionary>();
      for (auto it = d.GetIterator(); it.IsValid(); it.Next())
      {
        plVariant v = it.Value();
        ConvertOrSetToDefault(v, targetType);
        d[it.Key()] = v;
      }
      inout_defaultValue = d;
    }
    else
    {
      plVariantDictionary d;
      ConvertOrSetToDefault(inout_defaultValue, targetType);
      d["Key"] = inout_defaultValue;
      inout_defaultValue = d;
    }
  }
  else
  {
    ConvertOrSetToDefault(inout_defaultValue, targetType);
  }
}

/////////////////////////////////////////////////////////////////////////////

static plQtPropertyWidget* VisualScriptVariableTypeCreator(const plRTTI* pRtti)
{
  return new plQtVisualScriptVariableWidget();
}

static plQtPropertyWidget* VisualScriptVariableTypeDeclarationCreator(const plRTTI* pRtti)
{
  return new plQtVisualScriptVariableTypeDeclarationWidget();
}

void plVisualScriptVariable_PropertyMetaStateEventHandler(plPropertyMetaStateEvent& e)
{
  const plRTTI* pRtti = plGetStaticRTTI<plVisualScriptVariable>();

  auto& typeAccessor = e.m_pObject->GetTypeAccessor();

  if (typeAccessor.GetType() != pRtti)
    return;

  auto typeDecl = typeAccessor.GetValue("Type").Get<plVisualScriptVariableTypeDeclaration>();
  const bool bIsPublicNumberType = typeDecl.m_bPublic && plVisualScriptDataType::IsNumber(static_cast<plVisualScriptDataType::Enum>(typeDecl.m_Type.GetValue()));

  auto& props = *e.m_pPropertyStates;

  auto clampRangeVisibility = bIsPublicNumberType ? plPropertyUiState::Default : plPropertyUiState::Invisible;
  props["ClampRange"].m_Visibility = clampRangeVisibility;
  props["MinValue"].m_Visibility = clampRangeVisibility;
  props["MaxValue"].m_Visibility = clampRangeVisibility;
}

// clang-format off
PL_BEGIN_SUBSYSTEM_DECLARATION(EditorPluginVisualScript, VisualScriptVariable)

  BEGIN_SUBSYSTEM_DEPENDENCIES
  "ToolsFoundation", "PropertyMetaState"
  END_SUBSYSTEM_DEPENDENCIES

  ON_CORESYSTEMS_STARTUP
  {
    plQtPropertyGridWidget::GetFactory().RegisterCreator(plGetStaticRTTI<plVisualScriptVariableAttribute>(), VisualScriptVariableTypeCreator);
    plQtPropertyGridWidget::GetFactory().RegisterCreator(plGetStaticRTTI<plVisualScriptVariableTypeDeclaration>(), VisualScriptVariableTypeDeclarationCreator);

    plPropertyMetaState::GetSingleton()->m_Events.AddEventHandler(plVisualScriptVariable_PropertyMetaStateEventHandler);
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
    plQtPropertyGridWidget::GetFactory().UnregisterCreator(plGetStaticRTTI<plVisualScriptVariableAttribute>());
    plQtPropertyGridWidget::GetFactory().UnregisterCreator(plGetStaticRTTI<plVisualScriptVariableTypeDeclaration>());

    plPropertyMetaState::GetSingleton()->m_Events.RemoveEventHandler(plVisualScriptVariable_PropertyMetaStateEventHandler);
  }

PL_END_SUBSYSTEM_DECLARATION;
// clang-format on

///////////////////////////////////////////////////////////////////////////

class plVisualScriptVariablePatch_1_2 : public plGraphPatch
{
public:
  plVisualScriptVariablePatch_1_2()
    : plGraphPatch("plVisualScriptVariable", 2)
  {
  }

  virtual void Patch(plGraphPatchContext& ref_context, plAbstractObjectGraph* pGraph, plAbstractObjectNode* pNode) const override
  {
    auto* pDefaultValue = pNode->FindProperty("DefaultValue");
    auto* pExpose = pNode->FindProperty("Expose");

    if (pDefaultValue && pExpose)
    {
      plVisualScriptVariableTypeDeclaration typeDecl;

      plVariantType::Enum variantType = pDefaultValue->m_Value.GetType();
      typeDecl.m_Type = static_cast<plVisualScriptVariableType::Enum>(plVisualScriptDataType::FromVariantType(variantType));
      typeDecl.m_Category = plVisualScriptVariableCategory::Member;

      if (variantType == plVariantType::VariantArray)
      {
        typeDecl.m_Type = plVisualScriptVariableType::Variant;
        typeDecl.m_Category = plVisualScriptVariableCategory::Array;
      }
      else if (variantType == plVariantType::VariantDictionary)
      {
        typeDecl.m_Type = plVisualScriptVariableType::Variant;
        typeDecl.m_Category = plVisualScriptVariableCategory::Map;
      }

      typeDecl.m_bPublic = pExpose->m_Value.ConvertTo<bool>();

      pNode->AddProperty("Type", typeDecl);
    }
  }
};

plVisualScriptVariablePatch_1_2 g_plVisualScriptVariablePatch_1_2;

//////////////////////////////////////////////////////////////////////////

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plVisualScriptExpressionDataType, 1)
  PL_ENUM_CONSTANT(plVisualScriptExpressionDataType::Int),
  PL_ENUM_CONSTANT(plVisualScriptExpressionDataType::Float),
  PL_ENUM_CONSTANT(plVisualScriptExpressionDataType::Vector3),
  PL_ENUM_CONSTANT(plVisualScriptExpressionDataType::Color),
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_TYPE(plVisualScriptExpressionVariable, plNoBase, 1, plRTTIDefaultAllocator<plVisualScriptExpressionVariable>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_ENUM_MEMBER_PROPERTY("Type", plVisualScriptExpressionDataType, m_Type),
  }
  PL_END_PROPERTIES;
}
PL_END_STATIC_REFLECTED_TYPE;
// clang-format on

plVisualScriptDataType::Enum plVisualScriptExpressionDataType::GetVisualScriptDataType(Enum dataType)
{
  switch (dataType)
  {
    case Int:
      return plVisualScriptDataType::Int;
    case Float:
      return plVisualScriptDataType::Float;
    case Vector3:
      return plVisualScriptDataType::Vector3;
    case Color:
      return plVisualScriptDataType::Color;
    default:
      PL_ASSERT_NOT_IMPLEMENTED;
  }

  return plVisualScriptDataType::Invalid;
}
