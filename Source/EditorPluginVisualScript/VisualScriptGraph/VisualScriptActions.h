#pragma once

#include <EditorPluginVisualScript/EditorPluginVisualScriptDLL.h>
#include <GuiFoundation/Action/BaseActions.h>

class plVisualScriptGraphHost;

/// Toolbar and menu actions of the visual script editor: collapse to sub-graph, expand sub-graph, go to parent graph.
class plVisualScriptActions
{
public:
  static void RegisterActions();
  static void UnregisterActions();

  /// \brief Maps the actions into \a sMapping. An empty \a sTargetMenu is the toolbar; "G.Edit" is the Edit menu.
  static void MapActions(plStringView sMapping, plStringView sTargetMenu = plStringView());

  static plActionDescriptorHandle s_hCategory;
  static plActionDescriptorHandle s_hCollapseToSubGraph;
  static plActionDescriptorHandle s_hExpandSubGraph;
  static plActionDescriptorHandle s_hNavigateToParent;
  static plActionDescriptorHandle s_hAddFunction;
};

class plVisualScriptAction : public plButtonAction
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptAction, plButtonAction);

public:
  enum class ButtonType
  {
    CollapseToSubGraph,
    ExpandSubGraph,
    NavigateToParent,
    AddFunction,
  };

  plVisualScriptAction(const plActionContext& context, const char* szName, ButtonType button);

  virtual void Execute(const plVariant& value) override;

private:
  /// The graph shown by the action's window: the controller of its engine graph view.
  plVisualScriptGraphHost* FindGraph() const;

  ButtonType m_ButtonType;
};
