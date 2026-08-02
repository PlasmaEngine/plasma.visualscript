using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class VisualScriptPluginTarget : TargetRules
{
    public VisualScriptPluginTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plVisualScriptPlugin";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);
        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;
        ExtraModules.Add("VisualScriptPluginModule");
    }
}
