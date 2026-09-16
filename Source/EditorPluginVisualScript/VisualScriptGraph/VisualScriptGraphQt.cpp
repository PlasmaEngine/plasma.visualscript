#include <EditorPluginAssets/EditorPluginAssetsPCH.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptCommands.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraph.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptGraphQt.moc.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptNodeRegistry.h>
#include <EditorPluginVisualScript/VisualScriptGraph/VisualScriptVariable.moc.h>
#include <GuiFoundation/UIServices/UIServices.moc.h>
#include <GuiFoundation/VisualGraph/View.moc.h>
#include <ToolsFoundation/Command/TreeCommands.h>
#include <ToolsFoundation/Command/VisualGraphCommands.h>

#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QInputDialog>
#include <QPainter>
#include <QTimer>

// clang-format off
PL_BEGIN_SUBSYSTEM_DECLARATION(EditorPluginVisualScript, Factories)

  BEGIN_SUBSYSTEM_DEPENDENCIES
    "ReflectedTypeManager"
  END_SUBSYSTEM_DEPENDENCIES

  ON_CORESYSTEMS_STARTUP
  {
    PL_DEFAULT_NEW(plVisualScriptNodeRegistry);
    auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
    const plRTTI* pBaseType = pRegistry->GetNodeBaseType();

    plQtVisualGraphScene::GetPinFactory().RegisterCreator(plGetStaticRTTI<plVisualScriptPin>(), [](const plRTTI* pRtti)->plQtVisualGraphPin* { return new plQtVisualScriptPin(); });
    /*plQtVisualGraphScene::GetConnectionFactory().RegisterCreator(plGetStaticRTTI<plVisualScriptConnection>(), [](const plRTTI* pRtti)->plQtVisualGraphConnection* { return new plQtVisualScriptConnection(); });    */
    plQtVisualGraphScene::GetNodeFactory().RegisterCreator(pBaseType, [](const plRTTI* pRtti)->plQtVisualGraphNode* { return new plQtVisualScriptNode(); });

    // the factory walks the parent types, so these more specific registrations win over the base node
    plQtVisualGraphScene::GetNodeFactory().RegisterCreator(pRegistry->GetRerouteType(), [](const plRTTI* pRtti)->plQtVisualGraphNode* { return new plQtVisualScriptRerouteNode(); });
    plQtVisualGraphScene::GetNodeFactory().RegisterCreator(pRegistry->GetSubGraphType(), [](const plRTTI* pRtti)->plQtVisualGraphNode* { return new plQtVisualScriptSubGraphNode(); });
    for (plUInt32 uiIn = 0; uiIn < 2; ++uiIn)
    {
      for (plUInt32 uiExec = 0; uiExec < 2; ++uiExec)
      {
        plQtVisualGraphScene::GetNodeFactory().RegisterCreator(pRegistry->GetPortalType(uiIn == 1, uiExec == 1), [](const plRTTI* pRtti)->plQtVisualGraphNode* { return new plQtVisualScriptPortalNode(); });
      }
    }
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
    auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
    const plRTTI* pBaseType = pRegistry->GetNodeBaseType();

    plQtVisualGraphScene::GetPinFactory().UnregisterCreator(plGetStaticRTTI<plVisualScriptPin>());
    //plQtVisualGraphScene::GetConnectionFactory().UnregisterCreator(plGetStaticRTTI<plVisualScriptConnection>());
    plQtVisualGraphScene::GetNodeFactory().UnregisterCreator(pBaseType);
    plQtVisualGraphScene::GetNodeFactory().UnregisterCreator(pRegistry->GetRerouteType());
    plQtVisualGraphScene::GetNodeFactory().UnregisterCreator(pRegistry->GetSubGraphType());
    for (plUInt32 uiIn = 0; uiIn < 2; ++uiIn)
    {
      for (plUInt32 uiExec = 0; uiExec < 2; ++uiExec)
      {
        plQtVisualGraphScene::GetNodeFactory().UnregisterCreator(pRegistry->GetPortalType(uiIn == 1, uiExec == 1));
      }
    }

    plVisualScriptNodeRegistry* pDummy = plVisualScriptNodeRegistry::GetSingleton();
    PL_DEFAULT_DELETE(pDummy);
  }

PL_END_SUBSYSTEM_DECLARATION;
// clang-format on

namespace
{
  const plVisualScriptNodeManager* GetManagerOf(const plDocumentObject* pObject)
  {
    return static_cast<const plVisualScriptNodeManager*>(pObject->GetDocumentObjectManager());
  }

  plString GetNodeName(const plDocumentObject* pObject)
  {
    plVariant name = pObject->GetTypeAccessor().GetValue("Name");
    return name.IsA<plString>() ? name.Get<plString>() : plString();
  }
} // namespace

//////////////////////////////////////////////////////////////////////////

plQtVisualScriptPin::plQtVisualScriptPin() = default;

void plQtVisualScriptPin::SetPin(const plVisualGraphPin& pin)
{
  m_bTranslatePinName = false;

  plQtVisualGraphPin::SetPin(pin);

  UpdateTooltip();
}

void plQtVisualScriptPin::SetCompact(bool bCompact)
{
  m_pLabel->setVisible(!bCompact);
  if (bCompact == false)
    return;

  // only the pin shape remains: a circle the size of the label height, centred on the origin
  const qreal fSize = m_pLabel->boundingRect().height() - 2;
  QRectF bounds(-fSize * 0.5, -fSize * 0.5, fSize, fSize);
  m_PinCenter = bounds.center();

  QPainterPath p;
  p.addEllipse(bounds);
  setPath(p);
}

bool plQtVisualScriptPin::UpdatePinColors(const plColorGammaUB* pOverwriteColor)
{
  plColorGammaUB overwriteColor;
  const plVisualScriptPin& vsPin = plStaticCast<const plVisualScriptPin&>(*GetPin());
  auto pManager = GetManagerOf(vsPin.GetParent());

  plVisualScriptDataType::Enum type = vsPin.GetResolvedScriptDataType();
  if (vsPin.NeedsTypeDeduction())
  {
    overwriteColor = plVisualScriptNodeRegistry::PinDesc::GetColorForScriptDataType(type);
    pOverwriteColor = &overwriteColor;
  }

  bool res = plQtVisualGraphPin::UpdatePinColors(pOverwriteColor);

  // an unconnected required pin is drawn red; game object targets default to the script owner unless the document is a library
  const bool bOwnerFallback = type == plVisualScriptDataType::GameObject && pManager->m_bIsLibrary == false;
  if (pManager->IsPinRequired(vsPin) && bOwnerFallback == false && HasAnyConnections() == false)
  {
    QColor requiredColor = plToQtColor(plColorScheme::LightUI(plColorScheme::Red));

    QPen p = pen();
    p.setColor(requiredColor);
    setPen(p);

    m_pLabel->setDefaultTextColor(requiredColor);

    return true;
  }

  UpdateTooltip();

  return res;
}

void plQtVisualScriptPin::UpdateTooltip()
{
  const plVisualScriptPin& vsPin = plStaticCast<const plVisualScriptPin&>(*GetPin());

  plStringBuilder sTooltip;
  sTooltip = vsPin.GetName();

  if (vsPin.IsDataPin())
  {
    sTooltip.Append(": ", vsPin.GetDataTypeName());

    if (GetManagerOf(vsPin.GetParent())->IsPinRequired(vsPin))
    {
      sTooltip.Append(" (Required)");
    }
  }

  setToolTip(sTooltip.GetData());
}

//////////////////////////////////////////////////////////////////////////

plQtVisualScriptConnection::plQtVisualScriptConnection() = default;

//////////////////////////////////////////////////////////////////////////

plQtVisualScriptNode::plQtVisualScriptNode() = default;

void plQtVisualScriptNode::UpdateState()
{
  plStringBuilder sTitle;

  auto pManager = GetManagerOf(GetObject());
  auto pType = GetObject()->GetType();

  if (auto pTitleAttribute = pType->GetAttributeByType<plTitleAttribute>())
  {
    sTitle = pTitleAttribute->GetTitle();

    plHybridArray<const plAbstractProperty*, 32> properties;
    GetObject()->GetType()->GetAllProperties(properties);

    plStringBuilder temp;
    for (const auto& pin : GetInputPins())
    {
      if (pin->HasAnyConnections())
      {
        temp.Set("{", pin->GetPin()->GetName(), "}");
        if (static_cast<const plVisualScriptPin*>(pin->GetPin())->GetScriptDataType() == plVisualScriptDataType::String)
        {
          sTitle.ReplaceAll(temp, "");
        }
        else
        {
          sTitle.ReplaceAll(temp, pin->GetPin()->GetName());
        }

        temp.Set("{?", pin->GetPin()->GetName(), "}");
        sTitle.ReplaceAll(temp, "");
      }
    }

    plVariant val;
    plStringBuilder sVal;
    for (const auto& prop : properties)
    {
      val = GetObject()->GetTypeAccessor().GetValue(prop->GetPropertyName());

      if (prop->GetSpecificType()->IsDerivedFrom<plEnumBase>() || prop->GetSpecificType()->IsDerivedFrom<plBitflagsBase>())
      {
        plReflectionUtils::EnumerationToString(prop->GetSpecificType(), val.ConvertTo<plInt64>(), sVal);
        sVal = plTranslate(sVal);
      }
      else if (val.IsA<plString>() || val.IsA<plHashedString>())
      {
        sVal = val.ConvertTo<plString>();

        if (prop->GetAttributeByType<plAssetBrowserAttribute>())
        {
          if (plConversionUtils::IsStringUuid(sVal))
          {
            const plUuid AssetGuid = plConversionUtils::ConvertStringToUuid(sVal);

            auto pAsset = plAssetCurator::GetSingleton()->GetSubAsset(AssetGuid);

            if (pAsset)
              sVal = pAsset->m_pAssetInfo->m_Path.GetDataDirRelativePath().GetFileName();
            else
              sVal = "<unknown>";
          }
        }

        sVal.ReplaceAll("\n", " ");
        sVal.ReplaceAll("\t", " ");

        if (sVal.GetCharacterCount() > 23)
        {
          sVal.Shrink(0, sVal.GetCharacterCount() - 21);
          sVal.Append("...");
        }

        // names of graph elements read better without quotes
        const bool bIsGraphName = pManager->IsFunctionEntry(GetObject()) || pManager->IsCall(GetObject()) || pManager->IsCallLibrary(GetObject()) || pManager->IsSubGraph(GetObject()) || pManager->IsPortalIn(GetObject()) || pManager->IsPortalOut(GetObject());
        if (bIsGraphName == false)
        {
          sVal.Prepend("\"");
          sVal.Append("\"");
        }
      }
      else if (val.CanConvertTo<plString>())
      {
        sVal = val.ConvertTo<plString>();
      }
      else
      {
        sVal = "<Invalid>";
      }

      temp.Set("{", prop->GetPropertyName(), "}");
      sTitle.ReplaceAll(temp, sVal);

      temp.Set("{?", prop->GetPropertyName(), "}");
      if (val == plVariant(0))
      {
        sTitle.ReplaceAll(temp, "");
      }
      else
      {
        sTitle.ReplaceAll(temp, sVal);
      }
    }
  }
  else
  {
    sTitle = plVisualScriptNodeManager::GetNiceTypeName(GetObject());
  }

  if (const char* szSeparator = sTitle.FindSubString("::"))
  {
    m_pTitleLabel->setPlainText(szSeparator + 2);

    plStringBuilder sSubTitle = plStringView(sTitle.GetData(), szSeparator);
    sSubTitle.Trim("\"");
    m_pSubtitleLabel->setPlainText(sSubTitle.GetData());
  }
  else
  {
    m_pTitleLabel->setPlainText(sTitle.GetData());

    auto pNodeDesc = plVisualScriptNodeRegistry::GetSingleton()->GetNodeDescForType(pType);
    if (pNodeDesc != nullptr && pNodeDesc->NeedsTypeDeduction())
    {
      plVisualScriptDataType::Enum deductedType = pManager->GetDeductedType(GetObject());
      const char* sSubTitle = deductedType != plVisualScriptDataType::Invalid ? plVisualScriptDataType::GetName(deductedType) : "Unknown";
      m_pSubtitleLabel->setPlainText(sSubTitle);
    }
  }

  auto pScene = static_cast<plQtVisualScriptNodeScene*>(scene());

  if (pManager->IsCoroutine(GetObject()))
  {
    m_pIcon->setPixmap(pScene->GetCoroutineIcon());
    m_pIcon->setScale(0.5);
  }
  else if (pManager->IsLoop(GetObject()))
  {
    m_pIcon->setPixmap(pScene->GetLoopIcon());
    m_pIcon->setScale(0.5);
  }
  else
  {
    m_pIcon->setPixmap(QPixmap());
  }

  UpdateErrorState();
}

void plQtVisualScriptNode::UpdateErrorState()
{
  plStringBuilder sError;
  m_bHasError = GetManagerOf(GetObject())->GetNodeError(GetObject(), sError);
  setToolTip(m_bHasError ? QString::fromUtf8(sError.GetData()) : QString());
}

QColor plQtVisualScriptNode::GetErrorColor() const
{
  return plToQtColor(plColorScheme::LightUI(plColorScheme::Red));
}

void plQtVisualScriptNode::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  plQtVisualGraphNode::paint(painter, option, widget);

  if (m_bHasError)
  {
    painter->setPen(QPen(GetErrorColor(), 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());
  }
}

void plQtVisualScriptNode::ExtendContextMenu(QMenu& ref_menu)
{
  auto pScene = static_cast<plQtVisualScriptNodeScene*>(scene());
  auto pManager = GetManagerOf(GetObject());

  if (pManager->IsEntryNode(GetObject()) || pManager->IsSubGraphBoundary(GetObject()) || pManager->IsReturn(GetObject()))
    return;

  ref_menu.addSeparator();
  QAction* pAction = ref_menu.addAction("Collapse to Sub-graph");
  QObject::connect(pAction, &QAction::triggered, pScene, [pScene]()
    { pScene->CollapseSelectionToSubGraph(); });
}

//////////////////////////////////////////////////////////////////////////

plQtVisualScriptRerouteNode::plQtVisualScriptRerouteNode() = default;

void plQtVisualScriptRerouteNode::InitNode(const plVisualGraphObjectManager* pManager, const plDocumentObject* pObject)
{
  plQtVisualScriptNode::InitNode(pManager, pObject);

  m_pTitleLabel->hide();
  m_pSubtitleLabel->hide();
  m_pIcon->hide();
  EnableDropShadow(false);
}

void plQtVisualScriptRerouteNode::UpdateGeometry()
{
  prepareGeometryChange();

  // both pins sit on top of the dot; they are only hit-testable while shown (see SetPinsVisible)
  for (plQtVisualGraphPin* pPin : GetInputPins())
  {
    auto pVsPin = static_cast<plQtVisualScriptPin*>(pPin);
    pVsPin->SetCompact(true);
    pVsPin->setPos(-pVsPin->GetPinCenterLocal());
    pVsPin->setVisible(m_bPinsVisible);
  }
  for (plQtVisualGraphPin* pPin : GetOutputPins())
  {
    auto pVsPin = static_cast<plQtVisualScriptPin*>(pPin);
    pVsPin->SetCompact(true);
    pVsPin->setPos(-pVsPin->GetPinCenterLocal());
    pVsPin->setVisible(m_bPinsVisible);
  }

  m_HeaderRect = QRectF();

  QPainterPath p;
  p.addEllipse(QRectF(-s_fRadius, -s_fRadius, 2 * s_fRadius, 2 * s_fRadius));
  setPath(p);
}

void plQtVisualScriptRerouteNode::SetPinsVisible(bool bVisible)
{
  m_bPinsVisible = bVisible;
  for (plQtVisualGraphPin* pPin : GetInputPins())
    pPin->setVisible(bVisible);
  for (plQtVisualGraphPin* pPin : GetOutputPins())
    pPin->setVisible(bVisible);
}

void plQtVisualScriptRerouteNode::UpdateState()
{
  plQtVisualScriptNode::UpdateState();

  m_pTitleLabel->setPlainText("");
  m_pSubtitleLabel->setPlainText("");
  m_pIcon->setPixmap(QPixmap());

  auto pManager = GetManagerOf(GetObject());
  plVisualScriptDataType::Enum type = pManager->GetDeductedType(GetObject());
  plStringBuilder sTooltip;
  sTooltip.Set("Reroute: ", type != plVisualScriptDataType::Invalid ? plVisualScriptDataType::GetName(type) : "Unknown");
  setToolTip(sTooltip.GetData());
}

void plQtVisualScriptRerouteNode::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  // the base handles the deferred layout and draws the plain body; the coloured dot goes on top
  plQtVisualScriptNode::paint(painter, option, widget);

  auto pManager = GetManagerOf(GetObject());

  QColor color;
  if (GetInputPins().IsEmpty() == false && static_cast<const plVisualScriptPin*>(GetInputPins()[0]->GetPin())->IsExecutionPin())
  {
    color = plToQtColor(plColorScheme::DarkUI(plColorScheme::Gray));
  }
  else
  {
    plVisualScriptDataType::Enum type = pManager->GetDeductedType(GetObject());
    color = plToQtColor(plVisualScriptNodeRegistry::PinDesc::GetColorForScriptDataType(type));
  }

  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setBrush(color);
  painter->setPen(QPen(isSelected() ? QColor(255, 255, 255) : color.darker(140), isSelected() ? 2 : 1));
  painter->drawPath(path());
}

//////////////////////////////////////////////////////////////////////////

plQtVisualScriptPortalNode::plQtVisualScriptPortalNode() = default;

void plQtVisualScriptPortalNode::UpdateGeometry()
{
  prepareGeometryChange();

  auto pManager = GetManagerOf(GetObject());
  const bool bIn = pManager->IsPortalIn(GetObject());

  m_pSubtitleLabel->setPlainText("");
  m_pIcon->setPixmap(QPixmap());

  const QRectF titleRect = m_pTitleLabel->boundingRect();
  constexpr qreal fPad = 6;
  constexpr qreal fGlyph = 14;

  // the single pin is centred vertically on the left (in) or the right (out); the arrow glyph sits on the other side
  qreal fPinWidth = 0;
  qreal fHeight = titleRect.height() + 2 * fPad;

  auto& pins = bIn ? GetInputPins() : GetOutputPins();
  for (plQtVisualGraphPin* pPin : pins)
  {
    auto pVsPin = static_cast<plQtVisualScriptPin*>(pPin);
    pVsPin->SetCompact(true);
    const QRectF pinRect = pVsPin->GetPinRect();
    fPinWidth = pinRect.width();
  }

  const qreal fWidth = fPinWidth + fPad + titleRect.width() + fPad + fGlyph + fPad;

  for (plQtVisualGraphPin* pPin : pins)
  {
    auto pVsPin = static_cast<plQtVisualScriptPin*>(pPin);
    const QPointF center = pVsPin->GetPinCenterLocal();
    if (bIn)
      pVsPin->setPos(QPointF(fPinWidth * 0.5, fHeight * 0.5) - center);
    else
      pVsPin->setPos(QPointF(fWidth - fPinWidth * 0.5, fHeight * 0.5) - center);
  }

  const qreal fTitleX = bIn ? fPinWidth + fPad : fPad + fGlyph + fPad;
  m_pTitleLabel->setPos(fTitleX, fPad);

  m_HeaderRect = QRectF();

  QPainterPath p;
  p.addRoundedRect(QRectF(0, 0, fWidth, fHeight), fHeight * 0.5, fHeight * 0.5);
  setPath(p);
}

void plQtVisualScriptPortalNode::UpdateState()
{
  plQtVisualScriptNode::UpdateState();

  auto pManager = GetManagerOf(GetObject());
  plString sName = pManager->GetPortalName(GetObject());
  m_pTitleLabel->setPlainText(sName.IsEmpty() ? "<unnamed>" : sName.GetData());
  m_pSubtitleLabel->setPlainText("");
  m_pIcon->setPixmap(QPixmap());
}

void plQtVisualScriptPortalNode::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  // the base handles the deferred layout and draws the plain body; the pill is painted over it
  plQtVisualScriptNode::paint(painter, option, widget);

  auto pManager = GetManagerOf(GetObject());
  const bool bIn = pManager->IsPortalIn(GetObject());

  QColor color;
  if (pManager->IsExecPortal(GetObject()))
  {
    color = plToQtColor(plColorScheme::DarkUI(plColorScheme::Gray));
  }
  else
  {
    plVisualScriptDataType::Enum type = pManager->GetDeductedType(GetObject());
    color = plToQtColor(plVisualScriptNodeRegistry::PinDesc::GetColorForScriptDataType(type));
  }

  QColor fill = color;
  fill.setAlpha(150);

  painter->setRenderHint(QPainter::Antialiasing, true);
  painter->setBrush(fill);
  painter->setPen(QPen(isSelected() ? QColor(255, 255, 255) : color, isSelected() ? 2 : 1));
  painter->drawPath(path());

  // arrow glyph: into the body for 'in', out of the body for 'out'
  {
    const QRectF bounds = path().boundingRect();
    const qreal fCenterY = bounds.center().y();
    const qreal fGlyphX = bIn ? bounds.right() - 20 : 6;

    QPolygonF arrow;
    arrow << QPointF(fGlyphX, fCenterY - 5) << QPointF(fGlyphX + 12, fCenterY) << QPointF(fGlyphX, fCenterY + 5);

    painter->setBrush(QColor(255, 255, 255, 200));
    painter->setPen(Qt::NoPen);
    painter->drawPolygon(arrow);
  }

  // the title is drawn by the label item, which sits above the body fill painted here
  m_pTitleLabel->setZValue(1);

  if (m_bHasError)
  {
    painter->setPen(QPen(GetErrorColor(), 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());
  }
}

//////////////////////////////////////////////////////////////////////////

plQtVisualScriptSubGraphNode::plQtVisualScriptSubGraphNode() = default;

void plQtVisualScriptSubGraphNode::ExtendContextMenu(QMenu& ref_menu)
{
  auto pScene = static_cast<plQtVisualScriptNodeScene*>(scene());
  const plDocumentObject* pObject = GetObject();

  ref_menu.addSeparator();

  QAction* pEnter = ref_menu.addAction("Enter Sub-graph");
  QObject::connect(pEnter, &QAction::triggered, pScene, [pScene, pObject]()
    { pScene->NavigateToScope(pObject->GetGuid()); });

  QAction* pExpand = ref_menu.addAction("Expand Sub-graph");
  QObject::connect(pExpand, &QAction::triggered, pScene, [pScene, pObject]()
    { pScene->ExpandSubGraph(pObject); });

  plQtVisualScriptNode::ExtendContextMenu(ref_menu);
}

//////////////////////////////////////////////////////////////////////////

plQtVisualScriptNodeScene::plQtVisualScriptNodeScene(QObject* pParent /*= nullptr*/)
  : plQtVisualGraphScene(pParent)
{
  constexpr int iconSize = 32;
  m_CoroutineIcon = QIcon(":/EditorPluginVisualScript/Icons/Coroutine.svg").pixmap(QSize(iconSize, iconSize));
  m_LoopIcon = QIcon(":/EditorPluginVisualScript/Icons/Loop.svg").pixmap(QSize(iconSize, iconSize));
}

plQtVisualScriptNodeScene::~plQtVisualScriptNodeScene()
{
  if (m_pManager != nullptr)
  {
    auto pManager = const_cast<plVisualScriptNodeManager*>(GetScriptManager());
    pManager->m_NodeChangedEvent.RemoveEventHandler(plMakeDelegate(&plQtVisualScriptNodeScene::NodeChangedHandler, this));
    pManager->m_NodeEvents.RemoveEventHandler(plMakeDelegate(&plQtVisualScriptNodeScene::ScopeNodeEventsHandler, this));
    pManager->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plQtVisualScriptNodeScene::ScopePropertyEventsHandler, this));
  }
}

void plQtVisualScriptNodeScene::InitScene(const plVisualGraphObjectManager* pManager)
{
  plQtVisualGraphScene::InitScene(pManager);

  // registered after the base handlers on purpose: the Qt items exist when these run
  auto pScriptManager = const_cast<plVisualScriptNodeManager*>(GetScriptManager());
  pScriptManager->m_NodeChangedEvent.AddEventHandler(plMakeDelegate(&plQtVisualScriptNodeScene::NodeChangedHandler, this));
  pScriptManager->m_NodeEvents.AddEventHandler(plMakeDelegate(&plQtVisualScriptNodeScene::ScopeNodeEventsHandler, this));
  pScriptManager->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plQtVisualScriptNodeScene::ScopePropertyEventsHandler, this));

  m_CurrentScope = plUuid();
  pScriptManager->SetCurrentViewScope(m_CurrentScope);
  ApplyScopeFilter();
}

const plVisualScriptNodeManager* plQtVisualScriptNodeScene::GetScriptManager() const
{
  return static_cast<const plVisualScriptNodeManager*>(m_pManager);
}

bool plQtVisualScriptNodeScene::IsLayoutFlowPin(const plVisualGraphPin& pin) const
{
  return plStaticCast<const plVisualScriptPin&>(pin).IsExecutionPin();
}

void plQtVisualScriptNodeScene::NodeChangedHandler(const plDocumentObject* pObject)
{
  auto it = m_Nodes.Find(pObject);
  if (it.IsValid() == false)
    return;

  plQtVisualGraphNode* pNode = it.Value();

  pNode->ResetFlags();
  pNode->update();

  auto& inputPins = pNode->GetInputPins();
  for (plQtVisualGraphPin* pPin : inputPins)
  {
    if (static_cast<plQtVisualScriptPin*>(pPin)->UpdatePinColors())
    {
      pPin->update();
    }
  }

  auto& outputPins = pNode->GetOutputPins();
  for (plQtVisualGraphPin* pPin : outputPins)
  {
    if (static_cast<plQtVisualScriptPin*>(pPin)->UpdatePinColors())
    {
      pPin->update();
    }
  }
}

// Sub-graph scopes
//////////////////////////////////////////////////////////////////////////

void plQtVisualScriptNodeScene::ScopeNodeEventsHandler(const plVisualGraphObjectManagerEvent& e)
{
  switch (e.m_EventType)
  {
    case plVisualGraphObjectManagerEvent::Type::AfterNodeAdded:
    {
      auto it = m_Nodes.Find(e.m_pObject);
      if (it.IsValid())
      {
        it.Value()->setVisible(GetScriptManager()->GetDisplayScope(e.m_pObject) == m_CurrentScope);
      }
    }
    break;

    case plVisualGraphObjectManagerEvent::Type::AfterPinsConnected:
    {
      auto it = m_Connections.Find(e.m_pObject);
      if (it.IsValid())
      {
        auto& connection = m_pManager->GetConnection(e.m_pObject);
        const bool bVisible = GetScriptManager()->GetDisplayScope(connection.GetSourcePin().GetParent()) == m_CurrentScope &&
                              GetScriptManager()->GetDisplayScope(connection.GetTargetPin().GetParent()) == m_CurrentScope;
        it.Value()->setVisible(bVisible);
      }
    }
    break;

    default:
      break;
  }
}

void plQtVisualScriptNodeScene::ScopePropertyEventsHandler(const plDocumentObjectPropertyEvent& e)
{
  if (e.m_sProperty == plVisualScriptNodeRegistry::s_szScopeProperty && m_bScopeFilterPending == false)
  {
    // many nodes change scope in one transaction (collapse / expand), filter once afterwards
    m_bScopeFilterPending = true;
    QTimer::singleShot(0, this, [this]()
      {
        m_bScopeFilterPending = false;
        ApplyScopeFilter();
      });
  }
}

void plQtVisualScriptNodeScene::ApplyScopeFilter()
{
  auto pManager = GetScriptManager();

  for (auto it = m_Nodes.GetIterator(); it.IsValid(); ++it)
  {
    it.Value()->setVisible(pManager->GetDisplayScope(it.Key()) == m_CurrentScope);
  }

  for (auto it = m_Connections.GetIterator(); it.IsValid(); ++it)
  {
    const plVisualGraphConnection* pConnection = m_pManager->GetConnectionIfExists(it.Key());
    if (pConnection == nullptr)
    {
      it.Value()->setVisible(false);
      continue;
    }

    const bool bVisible = pManager->GetDisplayScope(pConnection->GetSourcePin().GetParent()) == m_CurrentScope &&
                          pManager->GetDisplayScope(pConnection->GetTargetPin().GetParent()) == m_CurrentScope;
    it.Value()->setVisible(bVisible);
  }

  // Nodes lay out their pins lazily in paint(); force that now and refresh the connections once it has happened,
  // otherwise connections of nodes that just became visible attach to stale pin positions.
  for (auto it = m_Nodes.GetIterator(); it.IsValid(); ++it)
  {
    if (it.Value()->isVisible())
    {
      it.Value()->ResetFlags();
      it.Value()->update();
    }
  }

  QTimer::singleShot(0, this, [this]()
    {
      for (auto it = m_Nodes.GetIterator(); it.IsValid(); ++it)
      {
        if (!it.Value()->isVisible())
          continue;

        for (plQtVisualGraphPin* pPin : it.Value()->GetInputPins())
          pPin->UpdateConnections();
        for (plQtVisualGraphPin* pPin : it.Value()->GetOutputPins())
          pPin->UpdateConnections();
      }
    });
}

plStatus plQtVisualScriptNodeScene::EnsureSubGraphBoundaries(const plUuid& scope)
{
  auto pManager = GetScriptManager();
  const plDocumentObject* pSubGraph = pManager->GetObject(scope);
  if (pSubGraph == nullptr || pManager->IsSubGraph(pSubGraph) == false)
    return plStatus(PL_SUCCESS);

  const bool bHasInputs = pManager->GetSubGraphBoundary(pSubGraph, true) != nullptr;
  const bool bHasOutputs = pManager->GetSubGraphBoundary(pSubGraph, false) != nullptr;
  if (bHasInputs && bHasOutputs)
    return plStatus(PL_SUCCESS);

  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Create Sub-graph Boundary");

  plStatus res(PL_SUCCESS);
  auto AddBoundary = [&](const plRTTI* pType, const plVec2& vPos)
  {
    plAddObjectCommand cmd;
    cmd.m_pType = pType;
    cmd.m_NewObjectGuid = plUuid::MakeUuid();
    cmd.m_Index = -1;
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      return;

    plSetObjectPropertyCommand setScope;
    setScope.m_Object = cmd.m_NewObjectGuid;
    setScope.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
    setScope.m_NewValue = scope;
    res = pHistory->AddCommand(setScope);
    if (res.Failed())
      return;

    plMoveNodeCommand move;
    move.m_Object = cmd.m_NewObjectGuid;
    move.m_NewPos = vPos;
    res = pHistory->AddCommand(move);
  };

  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
  if (bHasInputs == false)
    AddBoundary(pRegistry->GetSubGraphInputsType(), plVec2(-400, 0));
  if (res.Succeeded() && bHasOutputs == false)
    AddBoundary(pRegistry->GetSubGraphOutputsType(), plVec2(400, 0));

  if (res.Failed())
    pHistory->CancelTransaction();
  else
    pHistory->FinishTransaction();

  return res;
}

void plQtVisualScriptNodeScene::NavigateToScope(const plUuid& scope)
{
  auto pManager = const_cast<plVisualScriptNodeManager*>(GetScriptManager());

  plUuid newScope = scope;
  if (newScope.IsValid())
  {
    const plDocumentObject* pOwner = pManager->GetObject(newScope);
    if (pOwner == nullptr || pManager->IsNode(pOwner) == false || pManager->IsScopeOwner(pOwner) == false)
      newScope = plUuid();
  }

  clearSelection();

  m_CurrentScope = newScope;
  pManager->SetCurrentViewScope(m_CurrentScope);

  if (m_CurrentScope.IsValid())
  {
    EnsureSubGraphBoundaries(m_CurrentScope).IgnoreResult();
  }

  ApplyScopeFilter();

  Q_EMIT ScopeChanged(m_CurrentScope);
}

void plQtVisualScriptNodeScene::NavigateToParentScope()
{
  plUuid parent;
  if (GetScriptManager()->GetParentScope(m_CurrentScope, parent))
    NavigateToScope(parent);
}

// Functions
//////////////////////////////////////////////////////////////////////////

plUuid plQtVisualScriptNodeScene::AddFunction(plStringView sName)
{
  auto pManager = GetScriptManager();
  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();

  plStringBuilder sUniqueName = sName.IsEmpty() ? plStringView("NewFunction") : sName;
  for (plUInt32 i = 2; pManager->FindFunctionEntry(sUniqueName) != nullptr; ++i)
  {
    sUniqueName.SetFormat("{}{}", sName.IsEmpty() ? plStringView("NewFunction") : sName, i);
  }

  pHistory->StartTransaction("Add Function");

  plAddObjectCommand add;
  add.m_pType = plVisualScriptNodeRegistry::GetSingleton()->GetFunctionEntryType();
  add.m_NewObjectGuid = plUuid::MakeUuid();
  add.m_Index = -1;
  plStatus res = pHistory->AddCommand(add);

  if (res.Succeeded())
  {
    plSetObjectPropertyCommand setName;
    setName.m_Object = add.m_NewObjectGuid;
    setName.m_sProperty = "Name";
    setName.m_NewValue = plString(sUniqueName);
    res = pHistory->AddCommand(setName);
  }

  if (res.Succeeded())
  {
    plMoveNodeCommand move;
    move.m_Object = add.m_NewObjectGuid;
    move.m_NewPos = plVec2(-300, 0);
    res = pHistory->AddCommand(move);
  }

  if (res.Failed())
  {
    pHistory->CancelTransaction();
    plQtUiServices::GetSingleton()->MessageBoxStatus(res, "Failed to add the function");
    return plUuid();
  }

  pHistory->FinishTransaction();
  return add.m_NewObjectGuid;
}

void plQtVisualScriptNodeScene::RemoveFunction(const plUuid& functionGuid)
{
  auto pManager = GetScriptManager();
  const plDocumentObject* pEntry = pManager->GetObject(functionGuid);
  if (pEntry == nullptr || pManager->IsNode(pEntry) == false || pManager->IsFunctionEntry(pEntry) == false)
    return;

  // leave the graph that is about to disappear first
  if (pManager->GetScopeFunction(m_CurrentScope) == pEntry)
  {
    NavigateToScope(plUuid());
  }

  plHybridArray<const plDocumentObject*, 64> toRemove;
  pManager->GetNodesInScope(functionGuid, toRemove, true);
  toRemove.PushBack(pEntry);

  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Remove Function");

  plStatus res(PL_SUCCESS);
  for (const plDocumentObject* pObject : toRemove)
  {
    plRemoveNodeCommand cmd;
    cmd.m_Object = pObject->GetGuid();
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      break;
  }

  if (res.Succeeded())
  {
    RefreshReturnOwners();
    pHistory->FinishTransaction();
  }
  else
  {
    pHistory->CancelTransaction();
    plQtUiServices::GetSingleton()->MessageBoxStatus(res, "Failed to remove the function");
  }
}

void plQtVisualScriptNodeScene::RenameFunction(const plUuid& functionGuid, plStringView sNewName)
{
  auto pManager = GetScriptManager();
  const plDocumentObject* pEntry = pManager->GetObject(functionGuid);
  if (pEntry == nullptr || pManager->IsNode(pEntry) == false || pManager->IsFunctionEntry(pEntry) == false || sNewName.IsEmpty())
    return;

  if (GetNodeName(pEntry) == sNewName)
    return;

  if (const plDocumentObject* pOther = pManager->FindFunctionEntry(sNewName))
  {
    if (pOther != pEntry)
    {
      plQtUiServices::GetSingleton()->MessageBoxInformation(plFmt("There is already a function named '{}'.", sNewName));
      return;
    }
  }

  // the document's accessor renames the calls and returns of the function along with it
  plObjectAccessorBase* pAccessor = m_pManager->GetDocument()->GetObjectAccessor();
  pAccessor->StartTransaction("Rename Function");
  plStatus res = pAccessor->SetValueByName(pEntry, "Name", plString(sNewName));
  if (res.Failed())
  {
    pAccessor->CancelTransaction();
    plQtUiServices::GetSingleton()->MessageBoxStatus(res, "Failed to rename the function");
    return;
  }
  pAccessor->FinishTransaction();
}

// Mouse handling
//////////////////////////////////////////////////////////////////////////

QGraphicsItem* plQtVisualScriptNodeScene::FindGraphItemAt(const QPointF& scenePos, int& out_iType) const
{
  QGraphicsItem* pItem = itemAt(scenePos, QTransform());
  out_iType = pItem != nullptr ? pItem->type() : -1;
  while (pItem && !(out_iType >= Type::Node && out_iType <= Type::Connection))
  {
    pItem = pItem->parentItem();
    out_iType = pItem != nullptr ? pItem->type() : -1;
  }
  return pItem;
}

void plQtVisualScriptNodeScene::ShowReroutePins(bool bVisible)
{
  auto pManager = GetScriptManager();
  for (auto it = m_Nodes.GetIterator(); it.IsValid(); ++it)
  {
    if (pManager->IsReroute(it.Key()))
      static_cast<plQtVisualScriptRerouteNode*>(it.Value())->SetPinsVisible(bVisible);
  }
}

void plQtVisualScriptNodeScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
  m_pDragStartPin = nullptr;

  if (event->button() == Qt::LeftButton)
  {
    int iType = -1;
    QGraphicsItem* pItem = FindGraphItemAt(event->scenePos(), iType);

    if (iType == Type::Node && GetScriptManager()->IsReroute(static_cast<plQtVisualGraphNode*>(pItem)->GetObject()))
    {
      // a plain click selects and drags the dot; Ctrl+drag starts a connection from its output pin
      if (event->modifiers().testFlag(Qt::ControlModifier))
        static_cast<plQtVisualScriptRerouteNode*>(pItem)->SetPinsVisible(true);
    }
    else if (iType == Type::Pin)
    {
      // while a connection is dragged the reroute dots must accept it, so their pins become hit-testable
      ShowReroutePins(true);
    }

    QList<QGraphicsItem*> itemList = items(event->scenePos(), Qt::IntersectsItemBoundingRect);
    for (QGraphicsItem* item : itemList)
    {
      if (item->type() == Type::Pin)
      {
        m_pDragStartPin = static_cast<plQtVisualGraphPin*>(item);
        break;
      }
    }
  }

  plQtVisualGraphScene::mousePressEvent(event);
}

void plQtVisualScriptNodeScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
  auto pManager = const_cast<plVisualScriptNodeManager*>(GetScriptManager());

  // a reroute created by dragging a connection onto empty space takes the kind of the pin it came from
  if (m_pDragStartPin != nullptr && m_pDragStartPin->GetPin() != nullptr)
  {
    pManager->m_CreationHint.m_bValid = true;
    pManager->m_CreationHint.m_bExec = static_cast<const plVisualScriptPin*>(m_pDragStartPin->GetPin())->IsExecutionPin();
  }

  plQtVisualGraphScene::mouseReleaseEvent(event);

  pManager->m_CreationHint = plVisualScriptNodeManager::CreationHint();
  m_pDragStartPin = nullptr;

  ShowReroutePins(false);
}

void plQtVisualScriptNodeScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* contextMenuEvent)
{
  int iType = -1;
  QGraphicsItem* pItem = FindGraphItemAt(contextMenuEvent->scenePos(), iType);

  if (iType != Type::Connection)
  {
    plQtVisualGraphScene::contextMenuEvent(contextMenuEvent);
    return;
  }

  auto pQtConnection = static_cast<plQtVisualGraphConnection*>(pItem);
  const plDocumentObject* pConnectionObject = pQtConnection->GetObject();
  const plVec2 vPos(static_cast<float>(contextMenuEvent->scenePos().x()), static_cast<float>(contextMenuEvent->scenePos().y()));

  QMenu menu;
  QAction* pReroute = menu.addAction("Insert Reroute");
  QAction* pPortal = menu.addAction("Insert Portal...");
  menu.addSeparator();
  QAction* pDelete = menu.addAction("Delete Connection");

  QAction* pChosen = menu.exec(contextMenuEvent->screenPos());
  if (pChosen == pReroute)
    InsertRerouteOnConnection(pConnectionObject, vPos);
  else if (pChosen == pPortal)
    InsertPortalOnConnection(pConnectionObject, vPos);
  else if (pChosen == pDelete)
    DisconnectPinsAction(pQtConnection);
}

void plQtVisualScriptNodeScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
  if (event->button() != Qt::LeftButton)
  {
    plQtVisualGraphScene::mouseDoubleClickEvent(event);
    return;
  }

  auto pManager = GetScriptManager();

  QGraphicsItem* pItem = itemAt(event->scenePos(), QTransform());
  int iType = pItem != nullptr ? pItem->type() : -1;
  while (pItem && !(iType >= Type::Node && iType <= Type::Connection))
  {
    pItem = pItem->parentItem();
    iType = pItem != nullptr ? pItem->type() : -1;
  }

  if (iType == Type::Node)
  {
    const plDocumentObject* pObject = static_cast<plQtVisualGraphNode*>(pItem)->GetObject();
    if (pManager->IsSubGraph(pObject))
    {
      NavigateToScope(pObject->GetGuid());
      event->accept();
      return;
    }
    if (pManager->IsSubGraphBoundary(pObject))
    {
      NavigateToParentScope();
      event->accept();
      return;
    }
  }
  else if (iType == Type::Connection)
  {
    const plDocumentObject* pConnectionObject = static_cast<plQtVisualGraphConnection*>(pItem)->GetObject();
    if (pConnectionObject != nullptr)
    {
      InsertRerouteOnConnection(pConnectionObject, plVec2(static_cast<float>(event->scenePos().x()), static_cast<float>(event->scenePos().y())));
      event->accept();
      return;
    }
  }

  plQtVisualGraphScene::mouseDoubleClickEvent(event);
}

void plQtVisualScriptNodeScene::keyPressEvent(QKeyEvent* event)
{
  plUuid parent;
  if (event->key() == Qt::Key_Backspace && event->modifiers() == Qt::NoModifier && GetScriptManager()->GetParentScope(m_CurrentScope, parent))
  {
    NavigateToScope(parent);
    event->accept();
    return;
  }

  plQtVisualGraphScene::keyPressEvent(event);
}

// Return owners
//////////////////////////////////////////////////////////////////////////

void plQtVisualScriptNodeScene::RefreshReturnOwners()
{
  auto pManager = GetScriptManager();
  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();

  plHybridArray<const plDocumentObject*, 8> returns;
  for (auto pObject : m_pManager->GetRootObject()->GetChildren())
  {
    if (pManager->IsNode(pObject) && pManager->IsReturn(pObject))
      returns.PushBack(pObject);
  }

  for (const plDocumentObject* pReturn : returns)
  {
    const plDocumentObject* pOwner = pManager->FindReturnOwner(pReturn);
    const plString sOwnerName = pOwner != nullptr ? GetNodeName(pOwner) : plString();

    plVariant currentFunction = pReturn->GetTypeAccessor().GetValue("Function");
    if (currentFunction.IsA<plString>() && currentFunction.Get<plString>() == sOwnerName)
      continue;

    // the owner decides the pins, so this goes through the mirrored-pin choreography
    const plDocumentObject* dependents[] = {pReturn};
    plVisualScriptMirroredPins::ApplyWithDependents(pHistory, pManager, plMakeArrayPtr(dependents), [&]() -> plStatus
      {
        plSetObjectPropertyCommand cmd;
        cmd.m_Object = pReturn->GetGuid();
        cmd.m_sProperty = "Function";
        cmd.m_NewValue = sOwnerName;
        return pHistory->AddCommand(cmd);
      })
      .IgnoreResult();
  }
}

void plQtVisualScriptNodeScene::ConnectPinsAction(const plVisualGraphPin& sourcePin, const plVisualGraphPin& targetPin)
{
  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Connect Pins");

  plQtVisualGraphScene::ConnectPinsAction(sourcePin, targetPin);
  RefreshReturnOwners();

  pHistory->FinishTransaction();
}

void plQtVisualScriptNodeScene::DisconnectPinsAction(plQtVisualGraphConnection* pConnection)
{
  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Disconnect Pins");

  plQtVisualGraphScene::DisconnectPinsAction(pConnection);
  RefreshReturnOwners();

  pHistory->FinishTransaction();
}

void plQtVisualScriptNodeScene::DisconnectPinsAction(plQtVisualGraphPin* pPin)
{
  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Disconnect Pins");

  plQtVisualGraphScene::DisconnectPinsAction(pPin);
  RefreshReturnOwners();

  pHistory->FinishTransaction();
}

// Removal
//////////////////////////////////////////////////////////////////////////

void plQtVisualScriptNodeScene::GetSelectedNodeObjectsInScope(plDynamicArray<const plDocumentObject*>& out_nodes) const
{
  out_nodes.Clear();

  for (QGraphicsItem* pItem : selectedItems())
  {
    if (pItem->type() != plQtVisualGraphScene::Node)
      continue;

    const plDocumentObject* pObject = static_cast<plQtVisualGraphNode*>(pItem)->GetObject();
    if (m_pManager->IsComment(pObject))
      continue;

    if (GetScriptManager()->GetNodeScope(pObject) != m_CurrentScope)
      continue;

    out_nodes.PushBack(pObject);
  }
}

void plQtVisualScriptNodeScene::RemoveSelectedNodesAction()
{
  auto pManager = GetScriptManager();

  plHybridArray<const plDocumentObject*, 32> selection;
  for (QGraphicsItem* pItem : selectedItems())
  {
    if (pItem->type() == plQtVisualGraphScene::Node)
      selection.PushBack(static_cast<plQtVisualGraphNode*>(pItem)->GetObject());
  }

  if (selection.IsEmpty())
    return;

  // a sub-graph node takes its contents with it; the boundary nodes of the shown sub-graph and the entry of a function graph stay
  plHybridArray<const plDocumentObject*, 64> toRemove;
  plHybridArray<const plDocumentObject*, 32> contents;
  for (const plDocumentObject* pObject : selection)
  {
    if (pManager->IsSubGraphBoundary(pObject) || pManager->IsFunctionEntry(pObject))
      continue;

    if (toRemove.Contains(pObject) == false)
      toRemove.PushBack(pObject);

    if (pManager->IsSubGraph(pObject))
    {
      pManager->GetNodesInScope(pObject->GetGuid(), contents, true);
      for (const plDocumentObject* pContent : contents)
      {
        if (toRemove.Contains(pContent) == false)
          toRemove.PushBack(pContent);
      }
    }
  }

  if (toRemove.IsEmpty())
  {
    plQtUiServices::GetSingleton()->MessageBoxInformation("The inputs and outputs of a sub-graph and the entry of a function cannot be removed. Expand the sub-graph or remove the function from the Functions panel instead.");
    return;
  }

  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Remove Nodes");

  plStatus res(PL_SUCCESS);
  for (const plDocumentObject* pObject : toRemove)
  {
    res = m_pManager->CanRemove(pObject);
    if (res.Failed())
      break;

    plRemoveNodeCommand cmd;
    cmd.m_Object = pObject->GetGuid();
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      break;
  }

  if (res.Succeeded())
  {
    RefreshReturnOwners();
    pHistory->FinishTransaction();
  }
  else
  {
    pHistory->CancelTransaction();
    plQtUiServices::GetSingleton()->MessageBoxStatus(res, "Failed to remove node");
  }
}

// Reroutes
//////////////////////////////////////////////////////////////////////////

void plQtVisualScriptNodeScene::InsertRerouteOnConnection(const plDocumentObject* pConnectionObject, const plVec2& vPos)
{
  const plVisualGraphConnection* pConnection = m_pManager->GetConnectionIfExists(pConnectionObject);
  if (pConnection == nullptr)
    return;

  auto pManager = GetScriptManager();
  auto& sourcePin = static_cast<const plVisualScriptPin&>(pConnection->GetSourcePin());
  auto& targetPin = static_cast<const plVisualScriptPin&>(pConnection->GetTargetPin());

  const plUuid sourceGuid = sourcePin.GetParent()->GetGuid();
  const plUuid targetGuid = targetPin.GetParent()->GetGuid();
  const plString sSourcePin = sourcePin.GetName();
  const plString sTargetPin = targetPin.GetName();
  const bool bExec = sourcePin.IsExecutionPin();
  const plUuid scope = pManager->GetNodeScope(sourcePin.GetParent());

  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Insert Reroute");

  plStatus res = plNodeCommands::DisconnectAndRemoveCommand(pHistory, pConnectionObject->GetGuid());

  plUuid rerouteGuid = plUuid::MakeUuid();
  if (res.Succeeded())
  {
    plAddObjectCommand cmd;
    cmd.m_pType = plVisualScriptNodeRegistry::GetSingleton()->GetRerouteType();
    cmd.m_NewObjectGuid = rerouteGuid;
    cmd.m_Index = -1;
    res = pHistory->AddCommand(cmd);
  }

  if (res.Succeeded())
  {
    plSetObjectPropertyCommand cmd;
    cmd.m_Object = rerouteGuid;
    cmd.m_sProperty = "Kind";
    cmd.m_NewValue = plInt64(bExec ? plVisualScriptPinKind::Exec : plVisualScriptPinKind::Data);
    res = pHistory->AddCommand(cmd);
  }

  if (res.Succeeded() && scope.IsValid())
  {
    plSetObjectPropertyCommand cmd;
    cmd.m_Object = rerouteGuid;
    cmd.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
    cmd.m_NewValue = scope;
    res = pHistory->AddCommand(cmd);
  }

  if (res.Succeeded())
  {
    plMoveNodeCommand move;
    move.m_Object = rerouteGuid;
    move.m_NewPos = vPos;
    res = pHistory->AddCommand(move);
  }

  if (res.Succeeded())
  {
    const plDocumentObject* pReroute = m_pManager->GetObject(rerouteGuid);
    const plDocumentObject* pSource = m_pManager->GetObject(sourceGuid);
    const plDocumentObject* pTarget = m_pManager->GetObject(targetGuid);

    const plVisualGraphPin* pSourcePin = pSource ? m_pManager->GetOutputPinByName(pSource, sSourcePin) : nullptr;
    const plVisualGraphPin* pTargetPin = pTarget ? m_pManager->GetInputPinByName(pTarget, sTargetPin) : nullptr;
    const plVisualGraphPin* pRerouteIn = pReroute ? m_pManager->GetInputPinByName(pReroute, "In") : nullptr;
    const plVisualGraphPin* pRerouteOut = pReroute ? m_pManager->GetOutputPinByName(pReroute, "Out") : nullptr;

    if (pSourcePin && pTargetPin && pRerouteIn && pRerouteOut)
    {
      res = plNodeCommands::AddAndConnectCommand(pHistory, m_pManager->GetConnectionType(), *pSourcePin, *pRerouteIn);
      if (res.Succeeded())
        res = plNodeCommands::AddAndConnectCommand(pHistory, m_pManager->GetConnectionType(), *pRerouteOut, *pTargetPin);
    }
    else
    {
      res = plStatus("The reroute pins could not be created");
    }
  }

  if (res.Failed())
    pHistory->CancelTransaction();
  else
    pHistory->FinishTransaction();

  plQtUiServices::GetSingleton()->MessageBoxStatus(res, "Inserting the reroute failed");
}

void plQtVisualScriptNodeScene::InsertPortalOnConnection(const plDocumentObject* pConnectionObject, const plVec2& vPos)
{
  const plVisualGraphConnection* pConnection = m_pManager->GetConnectionIfExists(pConnectionObject);
  if (pConnection == nullptr)
    return;

  auto pManager = GetScriptManager();
  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
  auto& sourcePin = static_cast<const plVisualScriptPin&>(pConnection->GetSourcePin());
  auto& targetPin = static_cast<const plVisualScriptPin&>(pConnection->GetTargetPin());

  const plUuid sourceGuid = sourcePin.GetParent()->GetGuid();
  const plUuid targetGuid = targetPin.GetParent()->GetGuid();
  const plString sSourcePin = sourcePin.GetName();
  const plString sTargetPin = targetPin.GetName();
  const bool bExec = sourcePin.IsExecutionPin();
  const plUuid scope = pManager->GetNodeScope(sourcePin.GetParent());

  // a default name that is not taken yet
  plStringBuilder sDefault;
  {
    plStringView sBase = sSourcePin.IsEmpty() ? (bExec ? "Flow" : "Value") : sSourcePin.GetView();
    sDefault = sBase;
    for (plUInt32 i = 2; pManager->FindPortalIn(sDefault, bExec) != nullptr; ++i)
      sDefault.SetFormat("{}{}", sBase, i);
  }

  bool bOk = false;
  const QString sName = QInputDialog::getText(nullptr, "Insert Portal", bExec ? "Name of the execution portal:" : "Name of the data portal:", QLineEdit::Normal, sDefault.GetData(), &bOk).trimmed();
  if (bOk == false || sName.isEmpty())
    return;

  const plString sPortalName = sName.toUtf8().data();

  // data portals have exactly one source; execution portals may have several ins
  const bool bReuseExistingIn = pManager->FindPortalIn(sPortalName, bExec) != nullptr;
  if (bReuseExistingIn && bExec == false)
  {
    plQtUiServices::GetSingleton()->MessageBoxInformation("A data portal with this name already exists. Create a 'Portal Out' for it instead, or choose another name.");
    return;
  }

  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Insert Portal");

  plStatus res = plNodeCommands::DisconnectAndRemoveCommand(pHistory, pConnectionObject->GetGuid());

  auto AddPortal = [&](bool bIn, plUuid& out_guid, const plVec2& vNodePos)
  {
    if (res.Failed())
      return;

    plAddObjectCommand cmd;
    cmd.m_pType = pRegistry->GetPortalType(bIn, bExec);
    cmd.m_NewObjectGuid = plUuid::MakeUuid();
    cmd.m_Index = -1;
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      return;
    out_guid = cmd.m_NewObjectGuid;

    plSetObjectPropertyCommand setName;
    setName.m_Object = out_guid;
    setName.m_sProperty = "Name";
    setName.m_NewValue = sPortalName;
    res = pHistory->AddCommand(setName);
    if (res.Failed())
      return;

    if (scope.IsValid())
    {
      plSetObjectPropertyCommand setScope;
      setScope.m_Object = out_guid;
      setScope.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
      setScope.m_NewValue = scope;
      res = pHistory->AddCommand(setScope);
      if (res.Failed())
        return;
    }

    plMoveNodeCommand move;
    move.m_Object = out_guid;
    move.m_NewPos = vNodePos;
    res = pHistory->AddCommand(move);
  };

  plUuid inGuid, outGuid;
  AddPortal(true, inGuid, vPos + plVec2(-160.0f, -12.0f));
  AddPortal(false, outGuid, vPos + plVec2(40.0f, -12.0f));

  if (res.Succeeded())
  {
    const plDocumentObject* pSource = m_pManager->GetObject(sourceGuid);
    const plDocumentObject* pTarget = m_pManager->GetObject(targetGuid);
    const plDocumentObject* pIn = m_pManager->GetObject(inGuid);
    const plDocumentObject* pOut = m_pManager->GetObject(outGuid);

    const plVisualGraphPin* pSourcePin = pSource ? m_pManager->GetOutputPinByName(pSource, sSourcePin) : nullptr;
    const plVisualGraphPin* pTargetPin = pTarget ? m_pManager->GetInputPinByName(pTarget, sTargetPin) : nullptr;
    const plVisualGraphPin* pInPin = pIn ? m_pManager->GetInputPinByName(pIn, "In") : nullptr;
    const plVisualGraphPin* pOutPin = pOut ? m_pManager->GetOutputPinByName(pOut, "Out") : nullptr;

    if (pSourcePin && pTargetPin && pInPin && pOutPin)
    {
      res = plNodeCommands::AddAndConnectCommand(pHistory, m_pManager->GetConnectionType(), *pSourcePin, *pInPin);
      if (res.Succeeded())
        res = plNodeCommands::AddAndConnectCommand(pHistory, m_pManager->GetConnectionType(), *pOutPin, *pTargetPin);
    }
    else
    {
      res = plStatus("The portal pins could not be created");
    }
  }

  if (res.Failed())
    pHistory->CancelTransaction();
  else
    pHistory->FinishTransaction();

  plQtUiServices::GetSingleton()->MessageBoxStatus(res, "Inserting the portal failed");
}

// Collapse / Expand
//////////////////////////////////////////////////////////////////////////

void plQtVisualScriptNodeScene::CollapseSelectionToSubGraph()
{
  auto pManager = GetScriptManager();

  plHybridArray<const plDocumentObject*, 32> selection;
  GetSelectedNodeObjectsInScope(selection);

  if (selection.IsEmpty())
  {
    plQtUiServices::GetSingleton()->MessageBoxInformation("Select the nodes to collapse first.");
    return;
  }

  for (const plDocumentObject* pObject : selection)
  {
    if (pManager->IsEntryNode(pObject) || pManager->IsReturn(pObject) || pManager->IsSubGraphBoundary(pObject))
    {
      plQtUiServices::GetSingleton()->MessageBoxInformation("Entry nodes, Return nodes and sub-graph boundaries cannot be collapsed into a sub-graph.");
      return;
    }
  }

  plHashSet<const plDocumentObject*> selected;
  QRectF bounds;
  for (const plDocumentObject* pObject : selection)
  {
    selected.Insert(pObject);
    auto it = m_Nodes.Find(pObject);
    if (it.IsValid())
      bounds = bounds.isNull() ? it.Value()->sceneBoundingRect() : bounds.united(it.Value()->sceneBoundingRect());
  }

  // crossing connections become the pins of the sub-graph
  struct Crossing
  {
    plUuid m_Source;
    plString m_sSourcePin;
    plUuid m_Target;
    plString m_sTargetPin;
    plUuid m_ConnectionObject;
    plString m_sDeclName;
    plVisualScriptPinType::Enum m_Type;
    bool m_bInbound;
  };

  plHybridArray<Crossing, 16> crossings;
  plHashSet<plString> usedInputNames, usedOutputNames;

  auto MakeUniqueName = [](plHashSet<plString>& ref_used, plStringView sWanted, plStringBuilder& out_sName)
  {
    out_sName = sWanted.IsEmpty() ? "Pin" : sWanted;
    plStringBuilder sBase = out_sName;
    for (plUInt32 i = 2; ref_used.Contains(out_sName); ++i)
      out_sName.SetFormat("{}{}", sBase, i);
    ref_used.Insert(out_sName);
  };

  // inbound: keyed by source pin so one outside value feeds one sub-graph input
  plMap<plString, plString> inboundBySource;

  for (auto it = m_Connections.GetIterator(); it.IsValid(); ++it)
  {
    const plVisualGraphConnection* pConnection = m_pManager->GetConnectionIfExists(it.Key());
    if (pConnection == nullptr)
      continue;

    auto& sourcePin = static_cast<const plVisualScriptPin&>(pConnection->GetSourcePin());
    auto& targetPin = static_cast<const plVisualScriptPin&>(pConnection->GetTargetPin());
    const bool bSourceIn = selected.Contains(sourcePin.GetParent());
    const bool bTargetIn = selected.Contains(targetPin.GetParent());
    if (bSourceIn == bTargetIn)
      continue;

    auto& crossing = crossings.ExpandAndGetRef();
    crossing.m_Source = sourcePin.GetParent()->GetGuid();
    crossing.m_sSourcePin = sourcePin.GetName();
    crossing.m_Target = targetPin.GetParent()->GetGuid();
    crossing.m_sTargetPin = targetPin.GetName();
    crossing.m_ConnectionObject = it.Key()->GetGuid();
    crossing.m_bInbound = bTargetIn;

    // Invalid doubles as "execution" for pin declarations, so an undeduced data pin must never end up there
    auto IsUsable = [](plVisualScriptDataType::Enum type)
    { return type > plVisualScriptDataType::Invalid && type < plVisualScriptDataType::Count; };

    plVisualScriptDataType::Enum dataType = plVisualScriptDataType::Invalid;
    if (sourcePin.IsExecutionPin() == false)
    {
      dataType = sourcePin.GetResolvedScriptDataType();
      if (IsUsable(dataType) == false)
        dataType = targetPin.GetResolvedScriptDataType();
      if (dataType == plVisualScriptDataType::EnumValue || dataType == plVisualScriptDataType::BitflagValue)
        dataType = plVisualScriptDataType::Int64;
      if (IsUsable(dataType) == false)
        dataType = plVisualScriptDataType::Variant;
    }
    crossing.m_Type = static_cast<plVisualScriptPinType::Enum>(dataType);

    plStringBuilder sName;
    if (crossing.m_bInbound)
    {
      plStringBuilder sKey;
      sKey.Set(plConversionUtils::ToString(crossing.m_Source, sKey).GetView(), ".", crossing.m_sSourcePin);
      auto itExisting = inboundBySource.Find(sKey);
      if (itExisting.IsValid())
      {
        crossing.m_sDeclName = itExisting.Value();
      }
      else
      {
        plStringView sWanted = crossing.m_sTargetPin;
        if (sWanted.IsEmpty())
          sWanted = sourcePin.IsExecutionPin() ? "Exec" : "In";
        MakeUniqueName(usedInputNames, sWanted, sName);
        crossing.m_sDeclName = sName;
        inboundBySource.Insert(sKey, sName);
      }
    }
    else
    {
      plStringView sWanted = crossing.m_sSourcePin;
      if (sWanted.IsEmpty())
        sWanted = sourcePin.IsExecutionPin() ? "Exec" : "Out";
      MakeUniqueName(usedOutputNames, sWanted, sName);
      crossing.m_sDeclName = sName;
    }
  }

  // outbound data pins shared by several outside targets get one declaration
  {
    plMap<plString, plString> outboundBySource;
    for (auto& crossing : crossings)
    {
      if (crossing.m_bInbound)
        continue;

      plStringBuilder sKey;
      sKey.Set(plConversionUtils::ToString(crossing.m_Source, sKey).GetView(), ".", crossing.m_sSourcePin);
      auto itExisting = outboundBySource.Find(sKey);
      if (itExisting.IsValid())
        crossing.m_sDeclName = itExisting.Value();
      else
        outboundBySource.Insert(sKey, crossing.m_sDeclName);
    }
  }

  auto pRegistry = plVisualScriptNodeRegistry::GetSingleton();
  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Collapse to Sub-graph");

  plStatus res(PL_SUCCESS);
  const plUuid subGraphGuid = plUuid::MakeUuid();
  const plUuid inputsGuid = plUuid::MakeUuid();
  const plUuid outputsGuid = plUuid::MakeUuid();

  auto AddNode = [&](const plRTTI* pType, const plUuid& guid, const plUuid& scope, const plVec2& vPos)
  {
    if (res.Failed())
      return;

    plAddObjectCommand cmd;
    cmd.m_pType = pType;
    cmd.m_NewObjectGuid = guid;
    cmd.m_Index = -1;
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      return;

    if (scope.IsValid())
    {
      plSetObjectPropertyCommand setScope;
      setScope.m_Object = guid;
      setScope.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
      setScope.m_NewValue = scope;
      res = pHistory->AddCommand(setScope);
      if (res.Failed())
        return;
    }

    plMoveNodeCommand move;
    move.m_Object = guid;
    move.m_NewPos = vPos;
    res = pHistory->AddCommand(move);
  };

  const plVec2 vCenter(static_cast<float>(bounds.center().x()), static_cast<float>(bounds.center().y()));
  AddNode(pRegistry->GetSubGraphType(), subGraphGuid, m_CurrentScope, vCenter);
  AddNode(pRegistry->GetSubGraphInputsType(), inputsGuid, subGraphGuid, plVec2(static_cast<float>(bounds.left()) - 350.0f, vCenter.y));
  AddNode(pRegistry->GetSubGraphOutputsType(), outputsGuid, subGraphGuid, plVec2(static_cast<float>(bounds.right()) + 350.0f, vCenter.y));

  // 1. cut the crossing connections
  for (auto& crossing : crossings)
  {
    if (res.Failed())
      break;
    res = plNodeCommands::DisconnectAndRemoveCommand(pHistory, crossing.m_ConnectionObject);
  }

  // 2. move the selection inside
  for (const plDocumentObject* pObject : selection)
  {
    if (res.Failed())
      break;

    plSetObjectPropertyCommand cmd;
    cmd.m_Object = pObject->GetGuid();
    cmd.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
    cmd.m_NewValue = subGraphGuid;
    res = pHistory->AddCommand(cmd);
  }

  // 3. declare the boundary pins (one per unique declaration name)
  auto AddDecl = [&](const plUuid& boundaryGuid, plStringView sName, plVisualScriptPinType::Enum type)
  {
    if (res.Failed())
      return;

    plAddObjectCommand cmd;
    cmd.m_pType = plGetStaticRTTI<plVisualScriptPinDecl>();
    cmd.m_Parent = boundaryGuid;
    cmd.m_sParentProperty = "Pins";
    cmd.m_Index = -1;
    cmd.m_NewObjectGuid = plUuid::MakeUuid();
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      return;

    plSetObjectPropertyCommand setName;
    setName.m_Object = cmd.m_NewObjectGuid;
    setName.m_sProperty = "Name";
    plHashedString sHashedName;
    sHashedName.Assign(sName);
    setName.m_NewValue = sHashedName;
    res = pHistory->AddCommand(setName);
    if (res.Failed())
      return;

    plSetObjectPropertyCommand setType;
    setType.m_Object = cmd.m_NewObjectGuid;
    setType.m_sProperty = "Type";
    setType.m_NewValue = plInt64(type);
    res = pHistory->AddCommand(setType);
  };

  {
    plHashSet<plString> declared;
    for (auto& crossing : crossings)
    {
      plStringBuilder sKey;
      sKey.Set(crossing.m_bInbound ? "in:" : "out:", crossing.m_sDeclName);
      if (declared.Insert(sKey))
        continue;

      AddDecl(crossing.m_bInbound ? inputsGuid : outputsGuid, crossing.m_sDeclName, crossing.m_Type);
    }
  }

  // 4. the sub-graph node mirrors the boundary declarations
  if (res.Succeeded())
  {
    if (const plDocumentObject* pSubGraph = m_pManager->GetObject(subGraphGuid))
      res = plVisualScriptMirroredPins::RecreatePins(pHistory, pSubGraph);
  }

  // 5. rewire through the boundary
  for (auto& crossing : crossings)
  {
    if (res.Failed())
      break;

    const plDocumentObject* pSubGraph = m_pManager->GetObject(subGraphGuid);
    const plDocumentObject* pInputs = m_pManager->GetObject(inputsGuid);
    const plDocumentObject* pOutputs = m_pManager->GetObject(outputsGuid);
    const plDocumentObject* pSource = m_pManager->GetObject(crossing.m_Source);
    const plDocumentObject* pTarget = m_pManager->GetObject(crossing.m_Target);
    if (pSubGraph == nullptr || pInputs == nullptr || pOutputs == nullptr || pSource == nullptr || pTarget == nullptr)
    {
      res = plStatus("A node of the collapsed selection could not be found");
      break;
    }

    const plVisualGraphPin* pSourcePin = m_pManager->GetOutputPinByName(pSource, crossing.m_sSourcePin);
    const plVisualGraphPin* pTargetPin = m_pManager->GetInputPinByName(pTarget, crossing.m_sTargetPin);

    if (crossing.m_bInbound)
    {
      const plVisualGraphPin* pSubGraphIn = m_pManager->GetInputPinByName(pSubGraph, crossing.m_sDeclName);
      const plVisualGraphPin* pBoundaryOut = m_pManager->GetOutputPinByName(pInputs, crossing.m_sDeclName);
      if (pSourcePin && pSubGraphIn && m_pManager->HasConnections(*pSubGraphIn) == false)
        res = plNodeCommands::AddAndConnectCommand(pHistory, m_pManager->GetConnectionType(), *pSourcePin, *pSubGraphIn);
      if (res.Succeeded() && pBoundaryOut && pTargetPin)
        res = plNodeCommands::AddAndConnectCommand(pHistory, m_pManager->GetConnectionType(), *pBoundaryOut, *pTargetPin);
    }
    else
    {
      const plVisualGraphPin* pBoundaryIn = m_pManager->GetInputPinByName(pOutputs, crossing.m_sDeclName);
      const plVisualGraphPin* pSubGraphOut = m_pManager->GetOutputPinByName(pSubGraph, crossing.m_sDeclName);
      if (pSourcePin && pBoundaryIn && m_pManager->HasConnections(*pBoundaryIn) == false)
        res = plNodeCommands::AddAndConnectCommand(pHistory, m_pManager->GetConnectionType(), *pSourcePin, *pBoundaryIn);
      if (res.Succeeded() && pSubGraphOut && pTargetPin)
        res = plNodeCommands::AddAndConnectCommand(pHistory, m_pManager->GetConnectionType(), *pSubGraphOut, *pTargetPin);
    }
  }

  if (res.Succeeded())
  {
    RefreshReturnOwners();
    pHistory->FinishTransaction();

    clearSelection();
    if (const plDocumentObject* pSubGraph = m_pManager->GetObject(subGraphGuid))
    {
      m_pManager->GetDocument()->GetSelectionManager()->SetSelection(pSubGraph);
    }
  }
  else
  {
    pHistory->CancelTransaction();
    plQtUiServices::GetSingleton()->MessageBoxStatus(res, "Collapsing the selection failed");
  }
}

void plQtVisualScriptNodeScene::ExpandSelectedSubGraph()
{
  auto pManager = GetScriptManager();

  plHybridArray<const plDocumentObject*, 32> selection;
  GetSelectedNodeObjectsInScope(selection);

  const plDocumentObject* pSubGraph = nullptr;
  for (const plDocumentObject* pObject : selection)
  {
    if (pManager->IsSubGraph(pObject))
    {
      if (pSubGraph != nullptr)
      {
        plQtUiServices::GetSingleton()->MessageBoxInformation("Select a single sub-graph to expand.");
        return;
      }
      pSubGraph = pObject;
    }
  }

  if (pSubGraph == nullptr)
  {
    plQtUiServices::GetSingleton()->MessageBoxInformation("Select the sub-graph to expand first.");
    return;
  }

  ExpandSubGraph(pSubGraph);
}

void plQtVisualScriptNodeScene::ExpandSubGraph(const plDocumentObject* pSubGraph)
{
  auto pManager = GetScriptManager();
  if (pSubGraph == nullptr || pManager->IsSubGraph(pSubGraph) == false)
    return;

  const plUuid subGraphGuid = pSubGraph->GetGuid();
  const plUuid parentScope = pManager->GetNodeScope(pSubGraph);
  const plDocumentObject* pInputs = pManager->GetSubGraphBoundary(pSubGraph, true);
  const plDocumentObject* pOutputs = pManager->GetSubGraphBoundary(pSubGraph, false);

  struct Rewire
  {
    plUuid m_Source;
    plString m_sSourcePin;
    plUuid m_Target;
    plString m_sTargetPin;
  };
  plHybridArray<Rewire, 16> rewires;
  plHybridArray<plUuid, 32> connectionsToRemove;

  auto CollectConnections = [&](const plDocumentObject* pNode)
  {
    if (pNode == nullptr)
      return;
    for (auto& pPin : m_pManager->GetInputPins(pNode))
      for (auto pConnection : m_pManager->GetConnections(*pPin))
        if (connectionsToRemove.Contains(pConnection->GetParent()->GetGuid()) == false)
          connectionsToRemove.PushBack(pConnection->GetParent()->GetGuid());
    for (auto& pPin : m_pManager->GetOutputPins(pNode))
      for (auto pConnection : m_pManager->GetConnections(*pPin))
        if (connectionsToRemove.Contains(pConnection->GetParent()->GetGuid()) == false)
          connectionsToRemove.PushBack(pConnection->GetParent()->GetGuid());
  };

  // outside source -> sub-graph input X, inputs.X -> inside target  ==>  outside source -> inside target
  for (auto& pPin : m_pManager->GetInputPins(pSubGraph))
  {
    const plVisualGraphPin* pBoundaryOut = pInputs ? m_pManager->GetOutputPinByName(pInputs, pPin->GetName()) : nullptr;
    if (pBoundaryOut == nullptr)
      continue;

    for (auto pOuter : m_pManager->GetConnections(*pPin))
    {
      for (auto pInner : m_pManager->GetConnections(*pBoundaryOut))
      {
        auto& rewire = rewires.ExpandAndGetRef();
        rewire.m_Source = pOuter->GetSourcePin().GetParent()->GetGuid();
        rewire.m_sSourcePin = pOuter->GetSourcePin().GetName();
        rewire.m_Target = pInner->GetTargetPin().GetParent()->GetGuid();
        rewire.m_sTargetPin = pInner->GetTargetPin().GetName();
      }
    }
  }

  // inside source -> outputs.Y, sub-graph output Y -> outside target  ==>  inside source -> outside target
  for (auto& pPin : m_pManager->GetOutputPins(pSubGraph))
  {
    const plVisualGraphPin* pBoundaryIn = pOutputs ? m_pManager->GetInputPinByName(pOutputs, pPin->GetName()) : nullptr;
    if (pBoundaryIn == nullptr)
      continue;

    for (auto pInner : m_pManager->GetConnections(*pBoundaryIn))
    {
      for (auto pOuter : m_pManager->GetConnections(*pPin))
      {
        auto& rewire = rewires.ExpandAndGetRef();
        rewire.m_Source = pInner->GetSourcePin().GetParent()->GetGuid();
        rewire.m_sSourcePin = pInner->GetSourcePin().GetName();
        rewire.m_Target = pOuter->GetTargetPin().GetParent()->GetGuid();
        rewire.m_sTargetPin = pOuter->GetTargetPin().GetName();
      }
    }
  }

  CollectConnections(pSubGraph);
  CollectConnections(pInputs);
  CollectConnections(pOutputs);

  plHybridArray<const plDocumentObject*, 32> contents;
  pManager->GetNodesInScope(subGraphGuid, contents, false);

  // keep the relative layout, centred on where the sub-graph node was
  plVec2 vContentCenter = plVec2::MakeZero();
  plUInt32 uiCounted = 0;
  for (const plDocumentObject* pContent : contents)
  {
    if (pContent == pInputs || pContent == pOutputs)
      continue;
    vContentCenter += m_pManager->GetNodePos(pContent);
    ++uiCounted;
  }
  if (uiCounted > 0)
    vContentCenter /= static_cast<float>(uiCounted);
  const plVec2 vOffset = m_pManager->GetNodePos(pSubGraph) - vContentCenter;

  plCommandHistory* pHistory = m_pManager->GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Expand Sub-graph");

  plStatus res(PL_SUCCESS);

  for (const plUuid& connection : connectionsToRemove)
  {
    if (res.Failed())
      break;
    res = plNodeCommands::DisconnectAndRemoveCommand(pHistory, connection);
  }

  for (const plDocumentObject* pContent : contents)
  {
    if (res.Failed())
      break;
    if (pContent == pInputs || pContent == pOutputs)
      continue;

    plSetObjectPropertyCommand cmd;
    cmd.m_Object = pContent->GetGuid();
    cmd.m_sProperty = plVisualScriptNodeRegistry::s_szScopeProperty;
    cmd.m_NewValue = parentScope;
    res = pHistory->AddCommand(cmd);
    if (res.Failed())
      break;

    plMoveNodeCommand move;
    move.m_Object = pContent->GetGuid();
    move.m_NewPos = m_pManager->GetNodePos(pContent) + vOffset;
    res = pHistory->AddCommand(move);
  }

  for (auto& rewire : rewires)
  {
    if (res.Failed())
      break;

    const plDocumentObject* pSource = m_pManager->GetObject(rewire.m_Source);
    const plDocumentObject* pTarget = m_pManager->GetObject(rewire.m_Target);
    const plVisualGraphPin* pSourcePin = pSource ? m_pManager->GetOutputPinByName(pSource, rewire.m_sSourcePin) : nullptr;
    const plVisualGraphPin* pTargetPin = pTarget ? m_pManager->GetInputPinByName(pTarget, rewire.m_sTargetPin) : nullptr;
    if (pSourcePin == nullptr || pTargetPin == nullptr)
      continue;

    plVisualGraphObjectManager::CanConnectResult canConnect;
    if (m_pManager->CanConnect(m_pManager->GetConnectionType(), *pSourcePin, *pTargetPin, canConnect).Failed())
      continue;

    res = plNodeCommands::AddAndConnectCommand(pHistory, m_pManager->GetConnectionType(), *pSourcePin, *pTargetPin);
  }

  auto RemoveNode = [&](const plDocumentObject* pNode)
  {
    if (res.Failed() || pNode == nullptr)
      return;
    plRemoveNodeCommand cmd;
    cmd.m_Object = pNode->GetGuid();
    res = pHistory->AddCommand(cmd);
  };
  RemoveNode(pInputs);
  RemoveNode(pOutputs);
  RemoveNode(pSubGraph);

  if (res.Succeeded())
  {
    RefreshReturnOwners();
    pHistory->FinishTransaction();
  }
  else
  {
    pHistory->CancelTransaction();
    plQtUiServices::GetSingleton()->MessageBoxStatus(res, "Expanding the sub-graph failed");
  }
}
