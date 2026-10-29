#pragma once

#include <VisualScriptPlugin/Runtime/VisualScriptDataType.h>

class plVisualScriptPin;

class plVisualScriptTypeDeduction
{
public:
  static plVisualScriptDataType::Enum DeductFromNodeDataType(const plVisualScriptPin& pin);
  static plVisualScriptDataType::Enum DeductFromTypeProperty(const plVisualScriptPin& pin);
  static plVisualScriptDataType::Enum DeductFromExpressionInput(const plVisualScriptPin& pin);
  static plVisualScriptDataType::Enum DeductFromExpressionOutput(const plVisualScriptPin& pin);

  static plVisualScriptDataType::Enum DeductFromAllInputPins(const plDocumentObject* pObject, const plVisualScriptPin* pDisconnectedPin);
  static plVisualScriptDataType::Enum DeductFromVariableNameProperty(const plDocumentObject* pObject, const plVisualScriptPin* pDisconnectedPin);
  static plVisualScriptDataType::Enum DeductFromScriptDataTypeProperty(const plDocumentObject* pObject, const plVisualScriptPin* pDisconnectedPin);
  static plVisualScriptDataType::Enum DeductFromPropertyProperty(const plDocumentObject* pObject, const plVisualScriptPin* pDisconnectedPin);
  static plVisualScriptDataType::Enum DeductDummy(const plDocumentObject* pObject, const plVisualScriptPin* pDisconnectedPin);

  /// \brief A data reroute takes the type of whatever is connected: the source of its input first, otherwise the target of its output.
  static plVisualScriptDataType::Enum DeductFromRerouteConnections(const plDocumentObject* pObject, const plVisualScriptPin* pDisconnectedPin);

  /// \brief A data 'Portal Out' takes the type of the 'Portal In' with the same name.
  static plVisualScriptDataType::Enum DeductFromPortalIn(const plDocumentObject* pObject, const plVisualScriptPin* pDisconnectedPin);

  static const plRTTI* GetReflectedType(const plDocumentObject* pObject);
  static const plAbstractProperty* GetReflectedProperty(const plDocumentObject* pObject);

private:
  static plVisualScriptDataType::Enum DeductFromExpressionVariable(const plVisualScriptPin& pin, plStringView sPropertyName);
};
