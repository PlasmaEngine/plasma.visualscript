#pragma once

#include <Core/Scripting/ScriptDiagnostic.h>
#include <Foundation/Logging/Log.h>

class plDocument;

/// Turns the errors and warnings logged while a graph compiles into script diagnostics on the node that raised them.
///
/// Messages still reach the previous log. Report() publishes the result for the asset, which also clears the problems
/// of an earlier compile when there are none.
class plVisualScriptDiagnosticCapture : public plLogInterface
{
public:
  plVisualScriptDiagnosticCapture();
  ~plVisualScriptDiagnosticCapture();

  virtual void HandleLogMessage(const plLoggingEventData& le) override;

  /// Adds a failure that was returned rather than logged, unless an error was already captured.
  void AddFailure(plStringView sMessage);

  /// Stops capturing and logs the diagnostics of the document's asset.
  void Report(const plDocument* pDocument);

private:
  plLogInterface* m_pPrevious = nullptr;
  bool m_bCapturing = true;
  plDynamicArray<plScriptDiagnostic> m_Diagnostics;
};
