#pragma once

#include <Foundation/Basics.h>
#include <GuiFoundation/DocumentWindow/DocumentWindow.moc.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

class plQtVisualGraphView;
class plQtVisualScriptNodeScene;
class plQtVisualScriptBreadcrumb;
class plQtVisualScriptFunctionPanel;

class plQtVisualScriptWindow : public plQtDocumentWindow
{
  Q_OBJECT

public:
  plQtVisualScriptWindow(plDocument* pDocument);
  ~plQtVisualScriptWindow();

  plQtVisualScriptFunctionPanel* GetFunctionPanel() const { return m_pFunctionPanel; }

private Q_SLOTS:
  void OnNavigateToScope(const plUuid& scopeGuid);
  void OnScopeChanged(const plUuid& newScope);

private:
  void SelectionEventHandler(const plSelectionManagerEvent& e);

  plQtVisualScriptNodeScene* m_pScene = nullptr;
  plQtVisualGraphView* m_pView = nullptr;
  plQtVisualScriptBreadcrumb* m_pBreadcrumb = nullptr;
  plQtVisualScriptFunctionPanel* m_pFunctionPanel = nullptr;
};
