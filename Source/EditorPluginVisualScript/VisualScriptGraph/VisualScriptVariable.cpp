#include <EditorPluginVisualScript/EditorPluginVisualScriptPCH.h>

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptVariable.h>

#include <Foundation/Serialization/AbstractObjectGraph.h>
#include <Foundation/Serialization/GraphPatch.h>
#include <Foundation/Types/VariantTypeRegistry.h>
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

// The editor's property grid shows plVisualScriptVariableTypeDeclaration as a type list with
// category and visibility buttons, and limits a variable's default value to the variant types
// visual scripts support (plVisualScriptVariableAttribute).

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
    plPropertyMetaState::GetSingleton()->m_Events.AddEventHandler(plVisualScriptVariable_PropertyMetaStateEventHandler);
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
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

PL_BEGIN_STATIC_REFLECTED_ENUM(plVisualScriptPinType, 1)
  PL_ENUM_CONSTANT(plVisualScriptPinType::Exec),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Bool),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Byte),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Int),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Int64),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Float),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Double),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Color),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Vector3),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Quaternion),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Transform),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Time),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Angle),
  PL_ENUM_CONSTANT(plVisualScriptPinType::String),
  PL_ENUM_CONSTANT(plVisualScriptPinType::HashedString),
  PL_ENUM_CONSTANT(plVisualScriptPinType::GameObject),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Component),
  PL_ENUM_CONSTANT(plVisualScriptPinType::TypedPointer),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Variant),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Array),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Map),
  PL_ENUM_CONSTANT(plVisualScriptPinType::Coroutine),
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_ENUM(plVisualScriptPinKind, 1)
  PL_ENUM_CONSTANT(plVisualScriptPinKind::Data),
  PL_ENUM_CONSTANT(plVisualScriptPinKind::Exec),
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_STATIC_REFLECTED_TYPE(plVisualScriptPinDecl, plNoBase, 1, plRTTIDefaultAllocator<plVisualScriptPinDecl>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName)->AddAttributes(new plDefaultValueAttribute(plStringView("Pin"))),
    PL_ENUM_MEMBER_PROPERTY("Type", plVisualScriptPinType, m_Type),
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
