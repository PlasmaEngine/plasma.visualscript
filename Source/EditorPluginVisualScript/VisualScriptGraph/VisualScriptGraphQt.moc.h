#pragma once

#include <GuiFoundation/VisualGraph/Connection.h>
#include <GuiFoundation/VisualGraph/Node.h>
#include <GuiFoundation/VisualGraph/Pin.h>
#include <GuiFoundation/VisualGraph/Scene.moc.h>
#include <VisualScriptPlugin/Runtime/VisualScriptDataType.h>

/// Qt graphics item for visual script pins.
///
/// Displays pins with color coding based on data type and provides tooltips with detailed type information.
/// Handles both execution pins and data pins with appropriate visual styling.
class plQtVisualScriptPin : public plQtVisualGraphPin
{
public:
  plQtVisualScriptPin();

  virtual void SetPin(const plVisualGraphPin& pin) override;
  virtual bool UpdatePinColors(const plColorGammaUB* pOverwriteColor = nullptr) override;

private:
  void UpdateTooltip();
};

/// Qt graphics item for visual script connections.
///
/// Renders connections between visual script pins, representing both execution flow and data flow.
class plQtVisualScriptConnection : public plQtVisualGraphConnection
{
public:
  plQtVisualScriptConnection();
};

/// Qt graphics item for visual script nodes.
///
/// Visual representation of nodes in a visual script, such as function calls, variables, or control flow nodes.
/// Displays special icons for coroutine and loop nodes.
class plQtVisualScriptNode : public plQtVisualGraphNode
{
public:
  plQtVisualScriptNode();

  virtual void UpdateState() override;
};

/// Qt scene for visual script graphs.
///
/// Manages the visual scene for visual script editing. Provides icons for coroutine and loop nodes
/// and updates node visuals when their properties change.
class plQtVisualScriptNodeScene : public plQtVisualGraphScene
{
  Q_OBJECT

public:
  plQtVisualScriptNodeScene(QObject* pParent = nullptr);
  ~plQtVisualScriptNodeScene();

  virtual void InitScene(const plVisualGraphObjectManager* pManager);

  const QPixmap& GetCoroutineIcon() const { return m_CoroutineIcon; }
  const QPixmap& GetLoopIcon() const { return m_LoopIcon; }

private:
  void NodeChangedHandler(const plDocumentObject* pObject);

  QPixmap m_CoroutineIcon;
  QPixmap m_LoopIcon;
};
