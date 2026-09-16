#pragma once

#include <GuiFoundation/PropertyGrid/Implementation/PropertyWidget.moc.h>
#include <VisualScriptPlugin/Runtime/VisualScriptDataType.h>

struct plVisualScriptVariableType
{
  using StorageType = plUInt8;

  enum Enum
  {
    Invalid = plVisualScriptDataType::Invalid,
    Bool = plVisualScriptDataType::Bool,
    Byte,
    Int,
    Int64,
    Float,
    Double,
    Color,
    Vector3,
    Quaternion,
    Transform,
    Time,
    Angle,
    String,
    HashedString,
    GameObject,
    Component,
    TypedPointer,
    Variant,

    Resource = plVisualScriptDataType::Resource,

    Default = Int,
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptVariableType);

//////////////////////////////////////////////////////////////////////////////

struct plVisualScriptVariableCategory
{
  using StorageType = plUInt8;

  enum Enum
  {
    Member,
    Array,
    Map,
    Default = Member,
  };

  static plPropertyCategory::Enum GetPropertyCategory(Enum category);
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptVariableCategory);

//////////////////////////////////////////////////////////////////////////////

struct plVisualScriptVariableTypeDeclaration
{
  PL_DECLARE_POD_TYPE();

  plEnum<plVisualScriptVariableType> m_Type;
  plEnum<plVisualScriptVariableCategory> m_Category;
  bool m_bPublic = false;
  plUInt8 m_uiPadding = 0;

  bool operator==(const plVisualScriptVariableTypeDeclaration& other) const
  {
    return m_Type == other.m_Type && m_Category == other.m_Category && m_bPublic == other.m_bPublic;
  }

  plVisualScriptDataType::Enum GetDataType() const;
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptVariableTypeDeclaration);
PL_DECLARE_CUSTOM_VARIANT_TYPE(plVisualScriptVariableTypeDeclaration);

class plStreamWriter;
class plStreamReader;

void operator<<(plStreamWriter& inout_stream, const plVisualScriptVariableTypeDeclaration& value);
void operator>>(plStreamReader& inout_stream, plVisualScriptVariableTypeDeclaration& value);

template <>
struct plHashHelper<plVisualScriptVariableTypeDeclaration>
{
  PL_ALWAYS_INLINE static plUInt32 Hash(const plVisualScriptVariableTypeDeclaration& value) { return plHashHelper<plUInt32>::Hash(*(const plUInt32*)&value); }

  PL_ALWAYS_INLINE static bool Equal(const plVisualScriptVariableTypeDeclaration& a, const plVisualScriptVariableTypeDeclaration& b) { return a == b; }
};

//////////////////////////////////////////////////////////////////////////

class plVisualScriptVariableAttribute : public plTypeWidgetAttribute
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptVariableAttribute, plTypeWidgetAttribute);
};

//////////////////////////////////////////////////////////////////////////

class plQtVisualScriptVariableWidget : public plQtVariantPropertyWidget
{
  Q_OBJECT;

public:
  plQtVisualScriptVariableWidget();
  virtual ~plQtVisualScriptVariableWidget();

  virtual void InternalSetValue(const plVariant& value) override;
  virtual plResult GetVariantTypeDisplayName(plVariantType::Enum type, plStringBuilder& out_sName) const override;
};

//////////////////////////////////////////////////////////////////////////

class plQtVisualScriptVariableTypeDeclarationWidget : public plQtStandardPropertyWidget
{
  Q_OBJECT;

public:
  plQtVisualScriptVariableTypeDeclarationWidget();
  virtual ~plQtVisualScriptVariableTypeDeclarationWidget();

  virtual void OnInit() override;
  virtual void InternalSetValue(const plVariant& value) override;

private:
  void ChangeType();

  QHBoxLayout* m_pLayout = nullptr;
  QComboBox* m_pTypeList = nullptr;
  QMenu* m_pCategoryList = nullptr;
  QPushButton* m_pCategoryButton = nullptr;
  QPushButton* m_pVisibilityButton = nullptr;
};

//////////////////////////////////////////////////////////////////////////////

struct plVisualScriptVariable
{
  plHashedString m_sName;
  plVisualScriptVariableTypeDeclaration m_TypeDecl;
  plVariant m_DefaultValue;

  bool m_bClampRange = false;
  double m_fMinValue = 0.0;
  double m_fMaxValue = 1.0;

  static void ConvertDefaultValue(plVariant& inout_defaultValue, plVisualScriptVariableTypeDeclaration targetTypeDecl);
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptVariable);

//////////////////////////////////////////////////////////////////////////

struct plVisualScriptExpressionDataType
{
  using StorageType = plUInt8;

  enum Enum
  {
    Int = static_cast<plUInt8>(plProcessingStream::DataType::Int),
    Float = static_cast<plUInt8>(plProcessingStream::DataType::Float),
    Vector3 = static_cast<plUInt8>(plProcessingStream::DataType::Float3),
    Color = static_cast<plUInt8>(plProcessingStream::DataType::Float4),

    Default = Float
  };

  static plVisualScriptDataType::Enum GetVisualScriptDataType(Enum dataType);
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptExpressionDataType);

struct plVisualScriptExpressionVariable
{
  plHashedString m_sName;
  plEnum<plVisualScriptExpressionDataType> m_Type;
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptExpressionVariable);

//////////////////////////////////////////////////////////////////////////

/// \brief The type of a user-declared pin (sub-graph boundaries, function entries). Values match plVisualScriptDataType, Exec is the execution flow.
struct plVisualScriptPinType
{
  using StorageType = plUInt8;

  enum Enum
  {
    Exec = plVisualScriptDataType::Invalid,
    Bool = plVisualScriptDataType::Bool,
    Byte = plVisualScriptDataType::Byte,
    Int = plVisualScriptDataType::Int,
    Int64 = plVisualScriptDataType::Int64,
    Float = plVisualScriptDataType::Float,
    Double = plVisualScriptDataType::Double,
    Color = plVisualScriptDataType::Color,
    Vector3 = plVisualScriptDataType::Vector3,
    Quaternion = plVisualScriptDataType::Quaternion,
    Transform = plVisualScriptDataType::Transform,
    Time = plVisualScriptDataType::Time,
    Angle = plVisualScriptDataType::Angle,
    String = plVisualScriptDataType::String,
    HashedString = plVisualScriptDataType::HashedString,
    GameObject = plVisualScriptDataType::GameObject,
    Component = plVisualScriptDataType::Component,
    TypedPointer = plVisualScriptDataType::TypedPointer,
    Variant = plVisualScriptDataType::Variant,
    Array = plVisualScriptDataType::Array,
    Map = plVisualScriptDataType::Map,
    Coroutine = plVisualScriptDataType::Coroutine,

    Default = Float
  };

  static plVisualScriptDataType::Enum GetDataType(Enum type) { return static_cast<plVisualScriptDataType::Enum>(type); }
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptPinType);

/// \brief Whether a reroute node forwards data or execution flow.
struct plVisualScriptPinKind
{
  using StorageType = plUInt8;

  enum Enum
  {
    Data,
    Exec,

    Default = Data
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptPinKind);

/// \brief One user-declared pin: used by 'Sub-graph Inputs/Outputs' and by the Inputs/Outputs of a 'Function Entry'.
struct plVisualScriptPinDecl
{
  plHashedString m_sName;
  plEnum<plVisualScriptPinType> m_Type;
};

PL_DECLARE_REFLECTABLE_TYPE(PL_EDITORPLUGINVISUALSCRIPT_DLL, plVisualScriptPinDecl);
