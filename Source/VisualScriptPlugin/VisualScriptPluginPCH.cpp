#include <VisualScriptPlugin/VisualScriptPluginPCH.h>

#include <VisualScriptPlugin/VisualScriptPluginDLL.h>

#include <Foundation/Configuration/Plugin.h>

PL_STATICLINK_LIBRARY(VisualScriptPlugin)
{
  if (bReturn)
    return;

  PL_STATICLINK_REFERENCE(VisualScriptPlugin_Resources_VisualScriptClassResource);
  PL_STATICLINK_REFERENCE(VisualScriptPlugin_Resources_VisualScriptLibraryResource);
  PL_STATICLINK_REFERENCE(VisualScriptPlugin_Runtime_VisualScript);
  PL_STATICLINK_REFERENCE(VisualScriptPlugin_Runtime_VisualScriptDataType);
}
