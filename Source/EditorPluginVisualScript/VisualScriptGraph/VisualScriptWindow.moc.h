#pragma once

#include <Foundation/Basics.h>
#include <GuiFoundation/DocumentWindow/DocumentWindow.moc.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

class plQtVisualScriptNodeScene;
class plQtVisualGraphView;

class plQtVisualScriptWindow : public plQtDocumentWindow
{
  Q_OBJECT

public:
  plQtVisualScriptWindow(plDocument* pDocument);
  ~plQtVisualScriptWindow();

private Q_SLOTS:

private:
  void SelectionEventHandler(const plSelectionManagerEvent& e);

  plQtVisualScriptNodeScene* m_pScene;
  plQtVisualGraphView* m_pView;
};
