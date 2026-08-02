using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginVisualScriptModule : ModuleRules
{
    public EditorPluginVisualScriptModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EditorPluginVisualScript", "EditorPluginVisualScriptPCH.h",
            "BUILDSYSTEM_BUILDING_EDITORPLUGINVISUALSCRIPT_LIB");

        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);
        PlasmaPackageSdk.ConfigureAssetsBuiltIn(this, context);
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/VisualScriptPlugin", "plVisualScriptPlugin");

        PlasmaPackageQt.ConfigureQtModule(this, context, "EditorPluginVisualScriptModule",
            "Source/EditorPluginVisualScript", "Core", "Gui", "Widgets");
    }
}
