#pragma once

#include <EditorUi/Runtime.h>
#include <Foundation/Basics.h>
#include <GuiFoundation/DocumentWindow/DocumentWindow.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

class plVisualScriptFunctionPanel;
class plEditorUiGraphView;
class plVisualScriptGraphController;
class plVisualScriptGraphHost;

/// The document window of visual script classes and libraries: the graph ("VisualScriptView", the engine graph view,
/// which draws the scope breadcrumbs itself), its properties and the Functions panel.
class plVisualScriptWindow : public plDocumentWindow
{
public:
  plVisualScriptWindow(plDocument* pDocument);
  ~plVisualScriptWindow();

  plVisualScriptFunctionPanel* GetFunctionPanel() const { return m_pFunctionPanel; }

  /// The shown graph: the engine graph controller.
  plVisualScriptGraphHost* GetGraph() const;

private:
  void CreateGraph();
  void OnScopeChanged(const plUuid& newScope);
  void SelectionEventHandler(const plSelectionManagerEvent& e);

  plEditorUiGraphView* m_pGraph = nullptr;
  plVisualScriptGraphController* m_pController = nullptr; ///< owned by m_pGraph
  plVisualScriptFunctionPanel* m_pFunctionPanel = nullptr;

  plEditorUiTimerGroup m_Timers;
};
