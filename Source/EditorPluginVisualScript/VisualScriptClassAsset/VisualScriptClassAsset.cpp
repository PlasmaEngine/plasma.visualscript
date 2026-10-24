#include <EditorPluginAssets/EditorPluginAssetsPCH.h>

#include <EditorFramework/GUI/ExposedParameters.h>
#include <EditorPluginVisualScript/VisualScriptClassAsset/VisualScriptClassAsset.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptCommands.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptCompiler.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptDiagnostics.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraphView.h>
#include <GuiFoundation/EngineUi/EngineGraphView.h>
#include <ToolsFoundation/Command/TreeCommands.h>
#include <ToolsFoundation/Serialization/DocumentObjectConverter.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptClassAssetProperties, 1, plRTTIDefaultAllocator<plVisualScriptClassAssetProperties>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("BaseClass", m_sBaseClass)->AddAttributes(new plDefaultValueAttribute(plStringView("Component")), new plDynamicStringEnumAttribute("ScriptBaseClasses")),
    PL_MEMBER_PROPERTY("GraphName", m_sGraphName)->AddAttributes(new plDefaultValueAttribute(plStringView("Event Graph"))),
    PL_ARRAY_MEMBER_PROPERTY("Variables", m_Variables),
    PL_MEMBER_PROPERTY("DumpAST", m_bDumpAST),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plVisualScriptClassAssetDocument, 12, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plVisualScriptClassAssetDocument::plVisualScriptClassAssetDocument(plStringView sDocumentPath)
  : plSimpleAssetDocument<plVisualScriptClassAssetProperties>(PL_DEFAULT_NEW(plVisualScriptNodeManager), sDocumentPath, plAssetDocEngineConnection::None)
{
  m_pObjectAccessor = PL_DEFAULT_NEW(plVisualScriptCommandAccessor, GetCommandHistory());
}

// static
plStatus plVisualScriptClassAssetDocument::AddEntryFunctions(plVisualScriptNodeManager& ref_manager, plVisualScriptCompiler& ref_compiler, const plHashedString& sBaseClass)
{
  plHybridArray<const plVisualScriptPin*, 16> pins;
  for (const plDocumentObject* pObject : ref_manager.GetRootObject()->GetChildren())
  {
    // comments are nodes to the graph framework but have no node description
    if (ref_manager.IsNode(pObject) == false || ref_manager.IsComment(pObject))
      continue;

    auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pObject->GetType());
    if (pNodeDesc == nullptr)
      return plStatus(plFmt("Unknown node type '{}'", pObject->GetType()->GetTypeName()));

    if (ref_manager.IsFilteredByBaseClass(pObject->GetType(), *pNodeDesc, sBaseClass, true))
      continue;

    if (plVisualScriptNodeDescription::Type::IsEntry(pNodeDesc->m_Type) == false)
      continue;

    if (ref_manager.IsFunctionEntry(pObject))
    {
      // a function is compiled even without a body so that calls to it resolve; it then returns immediately
      plString sFunctionName = pObject->GetTypeAccessor().GetValue("Name").ConvertTo<plString>();
      if (sFunctionName.IsEmpty())
        return plStatus("A function entry has no name");

      PL_SUCCEED_OR_RETURN(ref_compiler.AddFunction(sFunctionName, pObject));
      continue;
    }

    ref_manager.GetOutputExecutionPins(pObject, pins);
    if (pins.IsEmpty())
      continue;

    if (ref_manager.GetConnections(*pins[0]).IsEmpty())
      continue;

    plStringView sFunctionName = plVisualScriptNodeManager::GetNiceFunctionName(pObject);
    PL_SUCCEED_OR_RETURN(ref_compiler.AddFunction(sFunctionName, pObject));
  }

  return plStatus(PL_SUCCESS);
}

plTransformStatus plVisualScriptClassAssetDocument::InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  plVisualScriptDiagnosticCapture capture;
  plTransformStatus status = CompileGraph(stream);
  if (status.Failed())
    capture.AddFailure(status.m_sMessage);
  capture.Report(this);
  return status;
}

plTransformStatus plVisualScriptClassAssetDocument::CompileGraph(plStreamWriter& stream)
{
  auto pManager = static_cast<plVisualScriptNodeManager*>(GetObjectManager());

  plHashedString sBaseClass = pManager->GetScriptBaseClass();

  plStringBuilder sBaseClassName = sBaseClass.GetView();
  if (plRTTI::FindTypeByName(sBaseClassName) == nullptr)
  {
    sBaseClassName.Prepend("pl");
    if (plRTTI::FindTypeByName(sBaseClassName) == nullptr)
    {
      return plStatus(plFmt("Invalid base class '{}'", sBaseClassName));
    }
  }

  PL_SUCCEED_OR_RETURN(pManager->ValidateGraph());

  plStringView sScriptClassName = plPathUtils::GetFileName(GetDocumentPath());

  plVisualScriptCompiler compiler(*pManager);
  plStringBuilder sAssetGuid;
  compiler.SetDebugAsset(plConversionUtils::ToString(GetGuid(), sAssetGuid));
  compiler.InitModule(sBaseClassName, sScriptClassName);

  PL_SUCCEED_OR_RETURN(AddEntryFunctions(*pManager, compiler, sBaseClass));

  plStringBuilder sDumpPath;
  if (GetProperties()->m_bDumpAST)
  {
    sDumpPath.SetFormat(":appdata/{}_AST.dgml", sScriptClassName);
  }
  PL_SUCCEED_OR_RETURN(compiler.Compile(sDumpPath));

  auto& compiledModule = compiler.GetCompiledModule();
  PL_SUCCEED_OR_RETURN(compiledModule.Serialize(stream));

  return plStatus(PL_SUCCESS);
}

void plVisualScriptClassAssetDocument::UpdateAssetDocumentInfo(plAssetDocumentInfo* pInfo) const
{
  SUPER::UpdateAssetDocumentInfo(pInfo);

  plExposedParameters* pExposedParams = PL_DEFAULT_NEW(plExposedParameters);

  for (const auto& v : GetProperties()->m_Variables)
  {
    if (v.m_TypeDecl.m_Type == plVisualScriptVariableType::Invalid || v.m_TypeDecl.m_bPublic == false)
      continue;

    if (v.m_TypeDecl.m_Type == plVisualScriptVariableType::GameObject || v.m_TypeDecl.m_Type == plVisualScriptVariableType::Component)
    {
      plLog::Error("Variables of type 'GameObject' or 'Component' are currently not supported as exposed parameters.");
      continue;
    }

    plExposedParameter* param = PL_DEFAULT_NEW(plExposedParameter);
    param->m_sName = v.m_sName.GetString();
    param->m_sType = plVisualScriptDataType::GetRtti(static_cast<plVisualScriptDataType::Enum>(v.m_TypeDecl.m_Type.GetValue()))->GetTypeName();
    param->m_DefaultValue = v.m_DefaultValue;
    param->m_Category = plVisualScriptVariableCategory::GetPropertyCategory(v.m_TypeDecl.m_Category);

    if (v.m_bClampRange && v.m_TypeDecl.m_Type >= plVisualScriptVariableType::Byte && v.m_TypeDecl.m_Type <= plVisualScriptVariableType::Double)
    {
      auto pClampValueAttribute = PL_DEFAULT_NEW(plClampValueAttribute, v.m_fMinValue, v.m_fMaxValue);
      param->m_Attributes.PushBack(pClampValueAttribute);
    }

    pExposedParams->m_Parameters.PushBack(param);
  }

  // Info takes ownership of meta data.
  pInfo->m_MetaInfo.PushBack(pExposedParams);
}

void plVisualScriptClassAssetDocument::InternalGetMetaDataHash(const plDocumentObject* pObject, plUInt64& inout_uiHash) const
{
  auto pManager = static_cast<const plVisualGraphObjectManager*>(GetObjectManager());
  pManager->GetMetaDataHash(pObject, inout_uiHash);
}

void plVisualScriptClassAssetDocument::AttachMetaDataBeforeSaving(plAbstractObjectGraph& graph) const
{
  SUPER::AttachMetaDataBeforeSaving(graph);
  const auto pManager = static_cast<const plVisualGraphObjectManager*>(GetObjectManager());
  pManager->AttachMetaDataBeforeSaving(graph);
}

void plVisualScriptClassAssetDocument::RestoreMetaDataAfterLoading(const plAbstractObjectGraph& graph, bool bUndoable)
{
  SUPER::RestoreMetaDataAfterLoading(graph, bUndoable);
  auto pManager = static_cast<plVisualScriptNodeManager*>(GetObjectManager());
  pManager->RestoreMetaDataAfterLoading(graph, bUndoable);

  // document load (not paste): function bodies saved in the event graph move into their function's graph
  if (bUndoable == false)
  {
    pManager->MigrateFunctionScopes();
  }
}

void plVisualScriptClassAssetDocument::GetSupportedMimeTypesForPasting(plHybridArray<plString, 4>& out_MimeTypes) const
{
  out_MimeTypes.PushBack("application/plEditor.VisualScriptClassGraph");
}

bool plVisualScriptClassAssetDocument::CopySelectedObjects(plAbstractObjectGraph& out_objectGraph, plStringBuilder& out_MimeType) const
{
  out_MimeType = "application/plEditor.VisualScriptClassGraph";

  const plVisualScriptNodeManager* pManager = static_cast<const plVisualScriptNodeManager*>(GetObjectManager());
  return plVisualScriptGraphClipboard::CopySelectedObjects(*pManager, out_objectGraph);
}

bool plVisualScriptClassAssetDocument::Paste(const plArrayPtr<PasteInfo>& info, const plAbstractObjectGraph& objectGraph, bool bAllowPickedPosition, plStringView sMimeType)
{
  plVisualScriptNodeManager* pManager = static_cast<plVisualScriptNodeManager*>(GetObjectManager());
  return plVisualScriptGraphClipboard::PasteObjects(*pManager, info, objectGraph, plEditorUiGraphView::GetLastPointerGraphPos(), bAllowPickedPosition);
}

//////////////////////////////////////////////////////////////////////////

// static
bool plVisualScriptGraphClipboard::CopySelectedObjects(const plVisualScriptNodeManager& manager, plAbstractObjectGraph& out_objectGraph)
{
  const auto& selection = manager.GetDocument()->GetSelectionManager()->GetSelection();
  if (selection.IsEmpty())
    return false;

  plDocumentObjectConverterWriter writer(&out_objectGraph, &manager);

  // A sub-graph node or function entry takes everything inside its graph along, including nested sub-graphs
  plHashSet<const plDocumentObject*> copiedNodes;
  plHybridArray<const plDocumentObject*, 32> contents;
  for (const plDocumentObject* pObject : selection)
  {
    if (manager.IsNode(pObject) == false)
      continue;

    if (copiedNodes.Insert(pObject) == false)
    {
      writer.AddObjectToGraph(pObject, "root");
    }

    if (manager.IsScopeOwner(pObject))
    {
      manager.GetNodesInScope(pObject->GetGuid(), contents, true);
      for (const plDocumentObject* pContent : contents)
      {
        if (copiedNodes.Insert(pContent) == false)
        {
          writer.AddObjectToGraph(pContent, "root");
        }
      }
    }
  }

  // Only connections with both ends inside the copied set
  plHashSet<const plDocumentObject*> copiedConnections;
  for (const plDocumentObject* pNodeObject : copiedNodes)
  {
    for (auto& pSourcePin : manager.GetOutputPins(pNodeObject))
    {
      for (const plVisualGraphConnection* pConnection : manager.GetConnections(*pSourcePin))
      {
        const plDocumentObject* pConnectionObject = pConnection->GetParent();
        if (copiedConnections.Contains(pConnectionObject) == false && copiedNodes.Contains(pConnection->GetTargetPin().GetParent()))
        {
          writer.AddObjectToGraph(pConnectionObject, "root");
          copiedConnections.Insert(pConnectionObject);
        }
      }
    }
  }

  manager.AttachMetaDataBeforeSaving(out_objectGraph);
  return true;
}

// static
bool plVisualScriptGraphClipboard::PasteObjects(plVisualScriptNodeManager& ref_manager, const plArrayPtr<plDocument::PasteInfo>& info, const plAbstractObjectGraph& objectGraph, const plVec2& vPickedPosition, bool bAllowPickedPosition)
{
  if (ref_manager.PasteObjects(info, objectGraph, vPickedPosition, bAllowPickedPosition) == false)
    return false;

  // Pasted nodes whose scope is not part of the paste land in the graph that is currently shown.
  // The paste command runs inside a transaction, so these commands undo together with it.
  plHashSet<plUuid> pastedGuids;
  for (const auto& pi : info)
  {
    pastedGuids.Insert(pi.m_pObject->GetGuid());
  }

  plCommandHistory* pHistory = ref_manager.GetDocument()->GetCommandHistory();
  const plUuid currentScope = ref_manager.GetCurrentViewScope();

  for (const auto& pi : info)
  {
    const plDocumentObject* pObject = pi.m_pObject;
    if (ref_manager.IsNode(pObject) == false || ref_manager.IsComment(pObject))
      continue;

    // a function entry is the root of its own graph and always stored in the event graph
    if (ref_manager.IsFunctionEntry(pObject))
      continue;

    plVariant scope = pObject->GetTypeAccessor().GetValue(plVisualScriptNodeRegistry::s_szScopeProperty);
    const plUuid scopeGuid = scope.IsA<plUuid>() ? scope.Get<plUuid>() : plUuid();
    if (scopeGuid.IsValid() && pastedGuids.Contains(scopeGuid))
      continue;

    if (scopeGuid == currentScope)
      continue;

    plSetObjectPropertyCommand cmd;
    cmd.m_Object = pObject->GetGuid();
    cmd.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
    cmd.m_NewValue = currentScope;
    pHistory->AddCommand(cmd).IgnoreResult();
  }

  return true;
}
