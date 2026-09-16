#pragma once

#include <EditorPluginVisualScript/EditorPluginVisualScriptDLL.h>
#include <ToolsFoundation/Command/Command.h>
#include <ToolsFoundation/VisualGraph/VisualGraphCommandAccessor.h>

class plVisualScriptNodeManager;
class plCommandHistory;

/// Rebuilds the pins of a node whose pins mirror a declaration elsewhere (calls, returns, sub-graph nodes).
///
/// Do and Undo both rebuild, so the pins always match the document state on either side of the history step.
/// Only ever executed while the node has no connections; connections are handled by separate commands.
class plVisualScriptRecreatePinsCommand : public plCommand
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptRecreatePinsCommand, plCommand);

public:
  plVisualScriptRecreatePinsCommand();

  plUuid m_Object;

private:
  virtual plStatus DoInternal(bool bRedo) override;
  virtual plStatus UndoInternal(bool bFireEvents) override;
  virtual void CleanupInternal(CommandState state) override {}
};

/// Command choreography for changes that affect the pins of other nodes.
struct plVisualScriptMirroredPins
{
  struct SavedConnection
  {
    plUuid m_Source;
    plUuid m_Target;
    plString m_sSourcePin;
    plString m_sTargetPin;
  };

  /// Disconnects all pins of \a pObject through commands, remembering the connections by pin name.
  static plStatus Disconnect(plCommandHistory* pHistory, const plVisualScriptNodeManager* pManager, const plDocumentObject* pObject, plDynamicArray<SavedConnection>& inout_saved);

  /// Restores the connections that still resolve by pin name and are still valid.
  static plStatus Reconnect(plCommandHistory* pHistory, const plVisualScriptNodeManager* pManager, const plDynamicArray<SavedConnection>& saved);

  static plStatus RecreatePins(plCommandHistory* pHistory, const plDocumentObject* pObject);

  /// Runs \a change (which must only add commands to \a pHistory) with every node in \a dependents disconnected before and
  /// its pins rebuilt and reconnected afterwards, so the whole thing undoes and redoes cleanly.
  static plStatus ApplyWithDependents(plCommandHistory* pHistory, const plVisualScriptNodeManager* pManager, plArrayPtr<const plDocumentObject* const> dependents, plDelegate<plStatus()> change);
};

/// Command accessor for visual script documents.
///
/// On top of the base behaviour (disconnect / reconnect the edited node when one of its own dynamic pin properties changes)
/// it keeps the nodes in sync whose pins mirror the edited declaration: calls and returns of a function, the sub-graph
/// node of a boundary. Renaming a function also updates the calls and returns that reference it.
class plVisualScriptCommandAccessor : public plVisualGraphCommandAccessor
{
  PL_ADD_DYNAMIC_REFLECTION(plVisualScriptCommandAccessor, plVisualGraphCommandAccessor);

public:
  plVisualScriptCommandAccessor(plCommandHistory* pHistory);
  ~plVisualScriptCommandAccessor();

  virtual plStatus SetValue(const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& newValue, plVariant index = plVariant()) override;
  virtual plStatus InsertValue(const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& newValue, plVariant index = plVariant()) override;
  virtual plStatus RemoveValue(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index = plVariant()) override;
  virtual plStatus MoveValue(const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& oldIndex, const plVariant& newIndex) override;
  virtual plStatus AddObject(const plDocumentObject* pParent, const plAbstractProperty* pParentProp, const plVariant& index, const plRTTI* pType, plUuid& inout_objectGuid) override;
  virtual plStatus RemoveObject(const plDocumentObject* pObject) override;

private:
  const plVisualScriptNodeManager* GetManager(const plDocumentObject* pObject) const;
  void GetDependents(const plDocumentObject* pObject, const plAbstractProperty* pProp, plDynamicArray<const plDocumentObject*>& out_dependents) const;
};
