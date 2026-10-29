#pragma once

#include <Core/Scripting/ScriptDebugger.h>
#include <VisualScriptPlugin/VisualScriptPluginDLL.h>

class plVisualScriptExecutionContext;
class plVisualScriptGraphDescription;

/// The Visual Script part of the script debugger: node breakpoints, stepping from node to node, the graph call stack and
/// the instance variables. Execution contexts report each node they run; without an attached session that is one check.
class PL_VISUALSCRIPTPLUGIN_DLL plVisualScriptDebugBackend : public plScriptDebugBackend
{
public:
  static plVisualScriptDebugBackend* GetSingleton();

  /// Register and unregister with the script debugger.
  static void Startup();
  static void Shutdown();

  /// An execution context starts or ends running on this thread.
  static void PushContext(const plVisualScriptExecutionContext* pContext);
  static void PopContext();

  /// Called before a node runs while a session is attached.
  void OnNode(const plVisualScriptExecutionContext& context);

  /// Called when a node stops the execution with an error. Reports a runtime diagnostic on the node.
  static void OnError(const plVisualScriptExecutionContext& context);

  virtual plStringView GetLanguage() const override { return "Visual Script"; }
  virtual void OnDebugStateChanged() override {}
  virtual void CollectFrames(const plScriptCallFrame& entry, plDynamicArray<plScriptDebugFrame>& out_frames) override;
  virtual void GetVariables(const plScriptCallFrame& entry, plUInt32 uiLocalFrame, plDynamicArray<plScriptDebugVariable>& out_variables) override;
  virtual plResult SetVariable(const plScriptCallFrame& entry, plUInt32 uiLocalFrame, plStringView sName, plStringView sValue) override;
};
