#include <EditorPluginVisualScript/EditorPluginVisualScriptPCH.h>

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptActions.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptFunctionPanel.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraphView.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptWindow.h>
#include <GuiFoundation/Action/ActionManager.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <GuiFoundation/EngineUi/EngineGraphView.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptAction, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plActionDescriptorHandle plVisualScriptActions::s_hCategory;
plActionDescriptorHandle plVisualScriptActions::s_hCollapseToSubGraph;
plActionDescriptorHandle plVisualScriptActions::s_hExpandSubGraph;
plActionDescriptorHandle plVisualScriptActions::s_hNavigateToParent;
plActionDescriptorHandle plVisualScriptActions::s_hAddFunction;

void plVisualScriptActions::RegisterActions()
{
  s_hCategory = PL_REGISTER_CATEGORY("VisualScriptCategory");
  s_hCollapseToSubGraph = PL_REGISTER_ACTION_1("VisualScript.CollapseToSubGraph", plActionScope::Document, "Visual Script", "Ctrl+Shift+G", plVisualScriptAction, plVisualScriptAction::ButtonType::CollapseToSubGraph);
  s_hExpandSubGraph = PL_REGISTER_ACTION_1("VisualScript.ExpandSubGraph", plActionScope::Document, "Visual Script", "", plVisualScriptAction, plVisualScriptAction::ButtonType::ExpandSubGraph);
  s_hNavigateToParent = PL_REGISTER_ACTION_1("VisualScript.NavigateToParent", plActionScope::Document, "Visual Script", "", plVisualScriptAction, plVisualScriptAction::ButtonType::NavigateToParent);
  s_hAddFunction = PL_REGISTER_ACTION_1("VisualScript.AddFunction", plActionScope::Document, "Visual Script", "Ctrl+Shift+F", plVisualScriptAction, plVisualScriptAction::ButtonType::AddFunction);
}

void plVisualScriptActions::UnregisterActions()
{
  plActionManager::UnregisterAction(s_hCategory);
  plActionManager::UnregisterAction(s_hCollapseToSubGraph);
  plActionManager::UnregisterAction(s_hExpandSubGraph);
  plActionManager::UnregisterAction(s_hNavigateToParent);
  plActionManager::UnregisterAction(s_hAddFunction);
}

void plVisualScriptActions::MapActions(plStringView sMapping, plStringView sTargetMenu)
{
  plActionMap* pMap = plActionMapManager::GetActionMap(sMapping);
  PL_ASSERT_DEV(pMap != nullptr, "The given mapping ('{0}') does not exist, mapping the actions failed!", sMapping);

  pMap->MapAction(s_hCategory, sTargetMenu, 21.0f);
  pMap->MapAction(s_hNavigateToParent, sTargetMenu, "VisualScriptCategory", 1.0f);
  pMap->MapAction(s_hCollapseToSubGraph, sTargetMenu, "VisualScriptCategory", 2.0f);
  pMap->MapAction(s_hExpandSubGraph, sTargetMenu, "VisualScriptCategory", 3.0f);
  pMap->MapAction(s_hAddFunction, sTargetMenu, "VisualScriptCategory", 4.0f);
}

//////////////////////////////////////////////////////////////////////////

plVisualScriptAction::plVisualScriptAction(const plActionContext& context, const char* szName, ButtonType button)
  : plButtonAction(context, szName, false, "")
  , m_ButtonType(button)
{
  switch (m_ButtonType)
  {
    case ButtonType::CollapseToSubGraph:
      SetIconPath(":/EditorPluginVisualScript/Icons/CollapseSubGraph.svg");
      break;
    case ButtonType::ExpandSubGraph:
      SetIconPath(":/EditorPluginVisualScript/Icons/ExpandSubGraph.svg");
      break;
    case ButtonType::NavigateToParent:
      SetIconPath(":/EditorPluginVisualScript/Icons/NavigateUp.svg");
      break;
    case ButtonType::AddFunction:
      SetIconPath(":/EditorPluginVisualScript/Icons/Function.svg");
      break;
  }
}

void plVisualScriptAction::Execute(const plVariant& value)
{
  if (m_ButtonType == ButtonType::AddFunction)
  {
    plVisualScriptWindow* pWindow = dynamic_cast<plVisualScriptWindow*>(m_Context.m_pWindow);
    if (pWindow != nullptr && pWindow->GetFunctionPanel() != nullptr)
      pWindow->GetFunctionPanel()->AddFunction();
    return;
  }

  plVisualScriptGraphHost* pGraph = FindGraph();
  if (pGraph == nullptr)
    return;

  switch (m_ButtonType)
  {
    case ButtonType::CollapseToSubGraph:
      pGraph->CollapseSelectionToSubGraph();
      break;
    case ButtonType::ExpandSubGraph:
      pGraph->ExpandSelectedSubGraph();
      break;
    case ButtonType::NavigateToParent:
      pGraph->NavigateToParentScope();
      break;
    case ButtonType::AddFunction:
      break;
  }
}

plVisualScriptGraphHost* plVisualScriptAction::FindGraph() const
{
  // the controller of the window's engine graph view
  auto* pWindow = dynamic_cast<plDocumentWindow*>(m_Context.m_pWindow);
  if (pWindow == nullptr)
    return nullptr;

  plEditorUiGraphView* pView = pWindow->FindPanelContent<plEditorUiGraphView>();
  return pView ? dynamic_cast<plVisualScriptGraphController*>(pView->GetController()) : nullptr;
}
