#pragma once

#include <Foundation/Basics.h>
#include <Foundation/Types/Uuid.h>

#include <QHBoxLayout>
#include <QWidget>

class plVisualScriptNodeManager;

/// Clickable path above the graph view showing where in the sub-graph hierarchy the user is, e.g. "Root > Movement > Jump".
class plQtVisualScriptBreadcrumb : public QWidget
{
  Q_OBJECT

public:
  explicit plQtVisualScriptBreadcrumb(QWidget* pParent = nullptr);
  ~plQtVisualScriptBreadcrumb();

  void UpdateFromNodeManager(const plVisualScriptNodeManager* pManager);

Q_SIGNALS:
  void NavigateToScope(const plUuid& scopeGuid);

private:
  void OnButtonClicked();

  QHBoxLayout* m_pLayout = nullptr;
};
