#include <VisualScriptPlugin/VisualScriptPluginPCH.h>

#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/IO/ChunkStream.h>
#include <Foundation/IO/StringDeduplicationContext.h>
#include <VisualScriptPlugin/Resources/VisualScriptLibraryResource.h>
#include <VisualScriptPlugin/Resources/VisualScriptModuleReader.h>
#include <VisualScriptPlugin/Runtime/VisualScriptNodeUserData.h>

void plVisualScriptModuleData::Clear()
{
  m_sBaseClassName.Clear();
  m_sScriptClassName.Clear();
  m_pConstantDataStorage.Clear();
  m_pInstanceDataDesc.Clear();
  m_pInstanceDataMapping.Clear();
  m_Functions.Clear();
  m_FunctionsByName.Clear();
  m_hLibraries.Clear();
  m_LibraryDescs.Clear();
  m_LibraryConstants.Clear();
}

const plSharedPtr<const plVisualScriptGraphDescription>* plVisualScriptModuleData::GetFunctionDesc(plTempHashedString sName) const
{
  return m_FunctionsByName.GetValue(sName);
}

// static
plResult plVisualScriptModuleReader::Read(plStreamReader& inout_stream, plVisualScriptModuleData& out_module, bool bRequireBaseClass, const plRTTI*& out_pBaseClassType)
{
  out_module.Clear();
  out_pBaseClassType = nullptr;

  plStringDeduplicationReadContext stringDedup(inout_stream);

  plChunkStreamReader chunk(inout_stream);
  chunk.SetEndChunkFileMode(plChunkStreamReader::EndChunkFileMode::JustClose);

  chunk.BeginStream();

  // skip all chunks that we don't know
  while (chunk.GetCurrentChunk().m_bValid)
  {
    if (chunk.GetCurrentChunk().m_sChunkName == "Header")
    {
      chunk >> out_module.m_sBaseClassName;
      chunk >> out_module.m_sScriptClassName;

      if (out_module.m_sBaseClassName.IsEmpty() == false)
      {
        out_pBaseClassType = plRTTI::FindTypeByName(out_module.m_sBaseClassName);
      }

      if (bRequireBaseClass && out_pBaseClassType == nullptr)
      {
        plLog::Error("Invalid base class '{}' for Visual Script Class '{}'", out_module.m_sBaseClassName, out_module.m_sScriptClassName);
        return PL_FAILURE;
      }
    }
    else if (chunk.GetCurrentChunk().m_sChunkName == "ConstantData")
    {
      plSharedPtr<plVisualScriptDataDescription> pConstantDataDesc = PL_SCRIPT_NEW(plVisualScriptDataDescription);
      if (pConstantDataDesc->Deserialize(chunk).Failed())
      {
        return PL_FAILURE;
      }

      plSharedPtr<plVisualScriptDataStorage> pConstantDataStorage = PL_SCRIPT_NEW(plVisualScriptDataStorage, pConstantDataDesc);
      if (pConstantDataStorage->Deserialize(chunk, plScriptAllocator::GetAllocator()).Succeeded())
      {
        out_module.m_pConstantDataStorage = pConstantDataStorage;
      }
    }
    else if (chunk.GetCurrentChunk().m_sChunkName == "InstanceData")
    {
      plSharedPtr<plVisualScriptDataDescription> pInstanceDataDesc = PL_SCRIPT_NEW(plVisualScriptDataDescription);
      if (pInstanceDataDesc->Deserialize(chunk).Succeeded())
      {
        out_module.m_pInstanceDataDesc = pInstanceDataDesc;
      }

      plSharedPtr<plVisualScriptInstanceDataMapping> pInstanceDataMapping = PL_SCRIPT_NEW(plVisualScriptInstanceDataMapping);
      if (chunk.ReadHashTable(pInstanceDataMapping->m_Content).Succeeded())
      {
        out_module.m_pInstanceDataMapping = pInstanceDataMapping;

        // calculate byte offsets from indices
        for (auto& it : pInstanceDataMapping->m_Content)
        {
          auto& dataOffset = it.Value().m_DataOffset;
          dataOffset = pInstanceDataDesc->GetOffset(dataOffset.GetType(), dataOffset.m_uiByteOffset, dataOffset.GetSource());
        }
      }
    }
    else if (chunk.GetCurrentChunk().m_sChunkName == "FunctionGraphs")
    {
      plUInt32 uiNumFunctions;
      chunk >> uiNumFunctions;

      if (out_module.m_pInstanceDataDesc == nullptr || out_module.m_pConstantDataStorage == nullptr)
      {
        plLog::Error("Old visual script, needs re-export");
        return PL_FAILURE;
      }

      for (plUInt32 i = 0; i < uiNumFunctions; ++i)
      {
        auto& function = out_module.m_Functions.ExpandAndGetRef();
        chunk >> function.m_sName;
        chunk >> function.m_Type;
        chunk >> function.m_CoroutineCreationMode;

        function.m_pDesc = PL_SCRIPT_NEW(plVisualScriptGraphDescription);
        if (function.m_pDesc->Deserialize(chunk, *out_module.m_pInstanceDataDesc, out_module.m_pConstantDataStorage->GetDesc()).Failed())
        {
          plLog::Error("Invalid visual script desc for function '{}'", function.m_sName);
          return PL_FAILURE;
        }

        function.m_pDesc->GetWritableDebugInfo().m_sFunction = function.m_sName;

        if (plVisualScriptNodeDescription::Type::IsFunctionEntry(function.m_Type))
        {
          plHashedString sName;
          sName.Assign(function.m_sName);
          out_module.m_FunctionsByName.Insert(sName, function.m_pDesc);
        }
      }
    }

    else if (chunk.GetCurrentChunk().m_sChunkName == "DebugTable")
    {
      plString sAsset;
      plUInt32 uiNumFunctions = 0;
      chunk >> sAsset;
      chunk >> uiNumFunctions;

      for (plUInt32 i = 0; i < uiNumFunctions; ++i)
      {
        plDynamicArray<plUuid> nodeGuids;
        PL_SUCCEED_OR_RETURN(chunk.ReadArray(nodeGuids));

        if (i < out_module.m_Functions.GetCount())
        {
          auto& debugInfo = out_module.m_Functions[i].m_pDesc->GetWritableDebugInfo();
          debugInfo.m_sAsset = sAsset;
          debugInfo.m_NodeGuids = std::move(nodeGuids);
        }
      }
    }

    chunk.NextChunk();
  }

  return PL_SUCCESS;
}

// static
plResult plVisualScriptModuleReader::ResolveCallSites(plVisualScriptModuleData& inout_module)
{
  for (auto& function : inout_module.m_Functions)
  {
    plStringView sCallerName = function.m_sName;
    const bool bCallerIsCoroutine = function.m_pDesc->IsCoroutine();

    plResult res = function.m_pDesc->PatchCallSites([&](plVisualScriptGraphDescription::Node& ref_node) -> plResult
      {
        auto& userData = ref_node.GetWritableUserData<NodeUserData_Call>();
        userData.m_pFunctionDesc = nullptr;
        userData.m_pConstantDataStorage = nullptr;

        plSharedPtr<const plVisualScriptGraphDescription> pCalleeDesc;

        if (ref_node.m_Type == plVisualScriptNodeDescription::Type::Builtin_Call)
        {
          auto pDesc = inout_module.GetFunctionDesc(plTempHashedString(userData.m_szFunctionName));
          if (pDesc == nullptr)
          {
            plLog::Error("Visual script '{}': function '{}' called from '{}' does not exist", inout_module.m_sScriptClassName, userData.m_szFunctionName, sCallerName);
            return PL_FAILURE;
          }

          pCalleeDesc = *pDesc;
        }
        else
        {
          plVisualScriptLibraryResourceHandle hLibrary = plResourceManager::LoadResource<plVisualScriptLibraryResource>(userData.m_szLibraryId);

          // Resource loading happens on loader threads and may nest, so a blocking acquire is fine here (never do this from a world update).
          plResourceLock<plVisualScriptLibraryResource> pLibrary(hLibrary, plResourceAcquireMode::BlockTillLoaded_NeverFail);
          if (pLibrary.GetAcquireResult() != plResourceAcquireResult::Final)
          {
            plLog::Error("Visual script '{}': library '{}' referenced from '{}' could not be loaded", inout_module.m_sScriptClassName, userData.m_szLibraryId, sCallerName);
            return PL_FAILURE;
          }

          auto pDesc = pLibrary->GetFunctionDesc(plTempHashedString(userData.m_szFunctionName));
          if (pDesc == nullptr)
          {
            plLog::Error("Visual script '{}': function '{}' does not exist in library '{}'", inout_module.m_sScriptClassName, userData.m_szFunctionName, pLibrary->GetResourceIdOrDescription());
            return PL_FAILURE;
          }

          pCalleeDesc = *pDesc;

          const plSharedPtr<plVisualScriptDataStorage>& pConstants = pLibrary->GetConstantDataStorage();
          userData.m_pConstantDataStorage = pConstants.Borrow();

          inout_module.m_hLibraries.PushBack(hLibrary);
          inout_module.m_LibraryDescs.PushBack(pCalleeDesc);
          inout_module.m_LibraryConstants.PushBack(pConstants);
        }

        if (pCalleeDesc->IsFunction() == false)
        {
          plLog::Error("Visual script '{}': '{}' is not a callable function", inout_module.m_sScriptClassName, userData.m_szFunctionName);
          return PL_FAILURE;
        }

        if (ref_node.m_NumInputDataOffsets > pCalleeDesc->GetNumEntryArguments())
        {
          plLog::Error("Visual script '{}': call of '{}' passes {} arguments but the function takes {}. The caller needs to be re-exported.", inout_module.m_sScriptClassName, userData.m_szFunctionName, ref_node.m_NumInputDataOffsets, pCalleeDesc->GetNumEntryArguments());
          return PL_FAILURE;
        }

        if (pCalleeDesc->IsCoroutine() && bCallerIsCoroutine == false)
        {
          plLog::Error("Visual script '{}': '{}' calls the coroutine function '{}' but was not compiled as a coroutine. The caller needs to be re-exported.", inout_module.m_sScriptClassName, sCallerName, userData.m_szFunctionName);
          return PL_FAILURE;
        }

        userData.m_pFunctionDesc = pCalleeDesc.Borrow();
        return PL_SUCCESS;
      });

    PL_SUCCEED_OR_RETURN(res);
  }

  return PL_SUCCESS;
}
