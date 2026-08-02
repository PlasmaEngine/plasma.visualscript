using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class VisualScriptPluginModule : ModuleRules
{
    public VisualScriptPluginModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/VisualScriptPlugin", "VisualScriptPluginPCH.h",
            "BUILDSYSTEM_BUILDING_VISUALSCRIPTPLUGIN_LIB");
    }
}
