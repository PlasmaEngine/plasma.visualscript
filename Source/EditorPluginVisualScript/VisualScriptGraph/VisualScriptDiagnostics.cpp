#include <EditorPluginAssets/EditorPluginAssetsPCH.h>

#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptCompiler.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptDiagnostics.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <ToolsFoundation/Document/Document.h>

plVisualScriptDiagnosticCapture::plVisualScriptDiagnosticCapture()
{
  m_pPrevious = plLog::GetThreadLocalLogSystem();
  plLog::SetThreadLocalLogSystem(this);
}

plVisualScriptDiagnosticCapture::~plVisualScriptDiagnosticCapture()
{
  if (m_bCapturing)
    plLog::SetThreadLocalLogSystem(m_pPrevious);
}

void plVisualScriptDiagnosticCapture::HandleLogMessage(const plLoggingEventData& le)
{
  if (m_pPrevious != nullptr)
    m_pPrevious->HandleLogMessage(le);

  if (le.m_EventType != plLogMsgType::ErrorMsg && le.m_EventType != plLogMsgType::SeriousWarningMsg && le.m_EventType != plLogMsgType::WarningMsg)
    return;

  if (le.m_sTag == plScriptDiagnostics::GetLogTag())
    return;

  plScriptDiagnostic& diag = m_Diagnostics.ExpandAndGetRef();
  diag.m_sLanguage = "Visual Script";
  diag.m_Severity = le.m_EventType == plLogMsgType::WarningMsg ? plScriptDiagnosticSeverity::Warning : plScriptDiagnosticSeverity::Error;
  diag.m_Origin = plScriptDiagnosticOrigin::Compile;
  diag.m_sMessage = le.m_sText;

  if (const plDocumentObject* pNode = plVisualScriptCompiler::GetCurrentNode())
    diag.m_NodeGuid = pNode->GetGuid();
}

void plVisualScriptDiagnosticCapture::AddFailure(plStringView sMessage)
{
  for (const plScriptDiagnostic& diag : m_Diagnostics)
  {
    if (diag.m_Severity == plScriptDiagnosticSeverity::Error)
      return;
  }

  plScriptDiagnostic& diag = m_Diagnostics.ExpandAndGetRef();
  diag.m_sLanguage = "Visual Script";
  diag.m_Origin = plScriptDiagnosticOrigin::Compile;
  diag.m_sMessage = sMessage.IsEmpty() ? plStringView("The graph failed to compile.") : sMessage;
}

void plVisualScriptDiagnosticCapture::Report(const plDocument* pDocument)
{
  if (m_bCapturing)
  {
    plLog::SetThreadLocalLogSystem(m_pPrevious);
    m_bCapturing = false;
  }

  plStringBuilder sAsset;
  plConversionUtils::ToString(pDocument->GetGuid(), sAsset);

  for (plScriptDiagnostic& diag : m_Diagnostics)
  {
    diag.m_sAsset = sAsset;
    diag.m_sFile = pDocument->GetDocumentPath();
  }

  plScriptDiagnostics::LogAssetResult(sAsset, "Visual Script", m_Diagnostics);
}
