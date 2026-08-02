// #pl-version 1

using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class PlasmaVisualScriptWorkspace : WorkspaceRules
{
    public PlasmaVisualScriptWorkspace(BuildContext context) : base(context)
    {
        TargetNames.Add("VisualScriptPlugin");
        TargetNames.Add("EditorPluginVisualScript");
    }
}
