/*
 * Open X-Com Companion — send/retrieve relay controls overlay (M1-25E / M1-26A).
 */
#include "CompanionOnDemandRelayOverlay.h"
#include "CompanionAutoRetrieveCore.h"
#include "CompanionOnDemandRelayCore.h"
#include "CompanionOnDemandRelayLayout.h"
#include "CompanionManualUploadHudCore.h"
#include "CompanionRelayPublicationFreshnessCore.h"
#include "../Savegame/SavedBattleGame.h"
#include "CompanionBriefingTestSupport.h"
#include <cstdlib>
#include "../Engine/Action.h"
#include "../Engine/InteractiveSurface.h"
#include "../Engine/Palette.h"
#include "../Engine/State.h"
#include "../Interface/Text.h"
#include "../Interface/TextButton.h"
#include "CompanionJsonLite.h"
#include <fstream>
#include <sstream>
#include <string>
#include <SDL.h>

namespace OpenXcom
{

namespace CompanionOnDemandRelayOverlay
{

namespace
{

using CompanionOnDemandRelayCore::RelayUiPhase;
using CompanionOnDemandRelayCore::RELAY_UI_IDLE;
using CompanionOnDemandRelayCore::RELAY_UI_SENDING;
using CompanionOnDemandRelayCore::RELAY_UI_RETRIEVING;
using CompanionOnDemandRelayCore::companionSnapshotPathConfigured;
using CompanionOnDemandRelayCore::companionWallClockMs;
using CompanionOnDemandRelayCore::formatRelayStatusLabel;
using CompanionOnDemandRelayCore::isRelayBusyPhase;
using CompanionOnDemandRelayCore::mapStatusViewToUiPhase;
using CompanionOnDemandRelayCore::parseRelayStatusJson;
using CompanionOnDemandRelayCore::RelayStatusView;
using CompanionOnDemandRelayCore::shouldAcceptRelayStatusUpdate;
using CompanionOnDemandRelayCore::resolveStatusPath;
using CompanionOnDemandRelayCore::tryEnqueueAction;
using CompanionOnDemandRelayCore::tryEnqueueSend;
using CompanionOnDemandRelayCore::reconcilePendingOnDemandRelay;
using CompanionOnDemandRelayCore::relayControlHitTarget;
using CompanionOnDemandRelayCore::relayControlRectHitTest;
using CompanionOnDemandRelayCore::RelayControlHit;
using CompanionOnDemandRelayCore::RELAY_CONTROL_HIT_NONE;
using CompanionOnDemandRelayCore::RELAY_CONTROL_HIT_RETRIEVE;
using CompanionOnDemandRelayCore::RELAY_CONTROL_HIT_SEND;
using CompanionOnDemandRelayLayout::CompactBarLayout;
using CompanionOnDemandRelayLayout::COMPACT_BUTTON_HEIGHT;
using CompanionOnDemandRelayLayout::COMPACT_BUTTON_WIDTH;
using CompanionOnDemandRelayLayout::AUTO_RETRIEVE_INTERVAL_TEXT_WIDTH;
using CompanionOnDemandRelayLayout::AUTO_RETRIEVE_ROW_GAP;
using CompanionOnDemandRelayLayout::AUTO_RETRIEVE_STEP_BUTTON_WIDTH;
using CompanionOnDemandRelayLayout::AUTO_RETRIEVE_TOGGLE_WIDTH;

const Uint32 FILE_REFRESH_MS = 400;

RelayUiPhase g_cachedPhase = RELAY_UI_IDLE;
RelayStatusView g_cachedStatus;
bool g_hasCachedStatus = false;
long long g_lastStatusReadWallMs = 0;
std::string g_lastStatusLabel;
std::string g_lastManualNoticeLabel;
bool g_showStatusOverlay = true;
bool g_hasAutoRetrieveUi = false;

void applyAutoRetrieveLayout(Ui *ui, const CompactBarLayout &layout)
{
	if (ui == 0)
	{
		return;
	}
	if (ui->btnAutoRetrieve != 0)
	{
		ui->btnAutoRetrieve->setWidth(AUTO_RETRIEVE_TOGGLE_WIDTH);
		ui->btnAutoRetrieve->setHeight(layout.autoRowHeight);
		ui->btnAutoRetrieve->setX(layout.autoToggleX);
		ui->btnAutoRetrieve->setY(layout.autoRowY);
	}
	if (ui->btnAutoIntervalLess != 0)
	{
		ui->btnAutoIntervalLess->setWidth(AUTO_RETRIEVE_STEP_BUTTON_WIDTH);
		ui->btnAutoIntervalLess->setHeight(layout.autoRowHeight);
		ui->btnAutoIntervalLess->setX(layout.autoIntervalMinusX);
		ui->btnAutoIntervalLess->setY(layout.autoRowY);
	}
	if (ui->btnAutoIntervalMore != 0)
	{
		ui->btnAutoIntervalMore->setWidth(AUTO_RETRIEVE_STEP_BUTTON_WIDTH);
		ui->btnAutoIntervalMore->setHeight(layout.autoRowHeight);
		ui->btnAutoIntervalMore->setX(layout.autoIntervalPlusX);
		ui->btnAutoIntervalMore->setY(layout.autoRowY);
	}
	if (ui->txtAutoInterval != 0)
	{
		ui->txtAutoInterval->setWidth(AUTO_RETRIEVE_INTERVAL_TEXT_WIDTH);
		ui->txtAutoInterval->setHeight(CompanionOnDemandRelayCore::RELAY_STATUS_HEIGHT);
		ui->txtAutoInterval->setX(layout.autoIntervalTextX);
		ui->txtAutoInterval->setY(layout.autoRowY + 1);
	}
}

void refreshAutoRetrieveLabels(Ui *ui)
{
	if (ui == 0 || !g_hasAutoRetrieveUi)
	{
		return;
	}
	std::string toggle;
	CompanionAutoRetrieveCore::formatToggleButtonText(
		CompanionAutoRetrieveCore::isEnabled(), &toggle);
	if (ui->btnAutoRetrieve != 0 && ui->btnAutoRetrieve->getText() != toggle)
	{
		ui->btnAutoRetrieve->setText(toggle);
	}
	std::string intervalLabel;
	CompanionAutoRetrieveCore::formatIntervalLabel(
		CompanionAutoRetrieveCore::intervalSec(), &intervalLabel);
	if (ui->txtAutoInterval != 0 && ui->txtAutoInterval->getText() != intervalLabel)
	{
		ui->txtAutoInterval->setText(intervalLabel);
	}
}

void setAutoRetrieveControlsVisible(Ui *ui, bool visible)
{
	if (ui == 0 || !g_hasAutoRetrieveUi)
	{
		return;
	}
	const bool show = visible && companionSnapshotPathConfigured()
	    && CompanionAutoRetrieveCore::isBattlescapeActive();
	if (ui->btnAutoRetrieve != 0)
	{
		ui->btnAutoRetrieve->setVisible(show);
	}
	if (ui->btnAutoIntervalLess != 0)
	{
		ui->btnAutoIntervalLess->setVisible(show);
	}
	if (ui->btnAutoIntervalMore != 0)
	{
		ui->btnAutoIntervalMore->setVisible(show);
	}
	if (ui->txtAutoInterval != 0)
	{
		ui->txtAutoInterval->setVisible(show);
	}
}

bool readStatusFile(RelayStatusView *outStatus, RelayUiPhase *outPhase)
{
	if (outStatus == 0 || outPhase == 0)
	{
		return false;
	}
	std::string statusPath;
	if (!resolveStatusPath(&statusPath))
	{
		if (g_hasCachedStatus && g_cachedStatus.terminal)
		{
			*outStatus = g_cachedStatus;
			*outPhase = mapStatusViewToUiPhase(g_cachedStatus);
			return true;
		}
		*outStatus = RelayStatusView();
		*outPhase = RELAY_UI_IDLE;
		return false;
	}
	std::ifstream in(statusPath.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		if (g_hasCachedStatus && g_cachedStatus.terminal)
		{
			*outStatus = g_cachedStatus;
			*outPhase = mapStatusViewToUiPhase(g_cachedStatus);
			return true;
		}
		*outStatus = RelayStatusView();
		*outPhase = RELAY_UI_IDLE;
		return false;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	const std::string json = ss.str();
	RelayStatusView incoming;
	if (!parseRelayStatusJson(json, &incoming))
	{
		if (g_hasCachedStatus && g_cachedStatus.terminal)
		{
			*outStatus = g_cachedStatus;
			*outPhase = mapStatusViewToUiPhase(g_cachedStatus);
			return true;
		}
		*outStatus = RelayStatusView();
		*outPhase = RELAY_UI_IDLE;
		return false;
	}
	if (g_hasCachedStatus && !shouldAcceptRelayStatusUpdate(g_cachedStatus, incoming, true))
	{
		*outStatus = g_cachedStatus;
		*outPhase = mapStatusViewToUiPhase(g_cachedStatus);
		return true;
	}
	g_cachedStatus = incoming;
	g_hasCachedStatus = true;
	g_cachedPhase = mapStatusViewToUiPhase(incoming);
	*outStatus = incoming;
	*outPhase = g_cachedPhase;
	return true;
}

void refreshStatusIfStale(long long wallNowMs, RelayStatusView *outStatus, RelayUiPhase *outPhase)
{
	if (g_lastStatusReadWallMs != 0
	    && wallNowMs >= g_lastStatusReadWallMs
	    && wallNowMs - g_lastStatusReadWallMs < FILE_REFRESH_MS)
	{
		if (g_hasCachedStatus)
		{
			*outStatus = g_cachedStatus;
			*outPhase = g_cachedPhase;
		}
		else
		{
			*outStatus = RelayStatusView();
			*outPhase = RELAY_UI_IDLE;
		}
		return;
	}
	g_lastStatusReadWallMs = wallNowMs;
	readStatusFile(outStatus, outPhase);
}

void applyCompactBarLayout(Ui *ui, const CompactBarLayout &layout)
{
	if (ui == 0)
	{
		return;
	}
	if (ui->btnSend != 0)
	{
		ui->btnSend->setWidth(layout.buttonWidth);
		ui->btnSend->setHeight(layout.buttonHeight);
		ui->btnSend->setX(layout.sendX);
		ui->btnSend->setY(layout.sendY);
	}
	if (ui->btnRetrieve != 0)
	{
		ui->btnRetrieve->setWidth(layout.buttonWidth);
		ui->btnRetrieve->setHeight(layout.buttonHeight);
		ui->btnRetrieve->setX(layout.retrieveX);
		ui->btnRetrieve->setY(layout.retrieveY);
	}
	if (ui->txtStatus != 0)
	{
		ui->txtStatus->setWidth(layout.statusWidth);
		ui->txtStatus->setX(layout.statusX);
		ui->txtStatus->setY(layout.statusY);
	}
	if (ui->txtManualNotice != 0)
	{
		ui->txtManualNotice->setWidth(layout.manualNoticeWidth);
		ui->txtManualNotice->setHeight(layout.manualNoticeHeight);
		ui->txtManualNotice->setX(layout.manualNoticeX);
		ui->txtManualNotice->setY(layout.manualNoticeY);
	}
}

const Uint8 SEND_COLOR_NORMAL = 239;
const Uint8 SEND_COLOR_NEEDS_PUBLICATION = Palette::blockOffset(1);

using CompanionRelayPublicationFreshnessCore::ConfirmedPublicationView;
using CompanionRelayPublicationFreshnessCore::SendFreshnessEvaluateInput;
using CompanionRelayPublicationFreshnessCore::TacticalBindingSnapshot;

static SendFreshnessEvaluateInput buildSendFreshnessInput(
	SavedBattleGame *battle,
	RelayUiPhase phase)
{
	SendFreshnessEvaluateInput input = {};
	input.companionConfigured = companionSnapshotPathConfigured();
	input.relayBusy = isRelayBusyPhase(phase)
		|| phase == CompanionOnDemandRelayCore::RELAY_UI_DEFERRED
		|| phase == CompanionOnDemandRelayCore::RELAY_UI_SENDING
		|| phase == CompanionOnDemandRelayCore::RELAY_UI_SENT;
	if (battle != 0)
	{
		input.live.sessionId = battle->getCompanionEventLog().getSessionId();
		input.live.turn = battle->getTurn();
		input.live.stateVersion = battle->getCompanionEventLog().getLatestSeq();
		input.live.mutationGeneration = CompanionRelayPublicationFreshnessCore::getTacticalMutationGeneration();
	}
	input.lastExport = CompanionRelayPublicationFreshnessCore::getLastExportBinding();
	if (!input.lastExport.sessionId.empty() && battle != 0
		&& input.lastExport.sessionId != input.live.sessionId)
	{
		input.lastExport.exportSha256.clear();
		input.lastExport.stateVersion = 0;
	}
	if (input.companionConfigured)
	{
		const char *snapshotPath = std::getenv("OPENXCOM_COMPANION_SNAPSHOT_PATH");
		if (snapshotPath != 0 && snapshotPath[0] != '\0')
		{
			const std::string ackDir =
				CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(snapshotPath);
			ConfirmedPublicationView confirmed;
			if (CompanionRelayPublicationFreshnessCore::loadConfirmedPublicationForLiveScope(
				ackDir, input.live, input.lastExport, &confirmed))
			{
				input.confirmed = confirmed;
			}
		}
	}
	return input;
}

void applyButtonState(Ui *ui, RelayUiPhase phase, SavedBattleGame *battle)
{
	if (ui == 0 || ui->btnSend == 0)
	{
		return;
	}
	using CompanionRelayPublicationFreshnessCore::evaluateSendButtonAppearance;
	using CompanionRelayPublicationFreshnessCore::SEND_APPEARANCE_NEEDS_PUBLICATION;

	const SendFreshnessEvaluateInput input = buildSendFreshnessInput(battle, phase);
	const bool needsPublication = evaluateSendButtonAppearance(input) == SEND_APPEARANCE_NEEDS_PUBLICATION;
	ui->btnSend->setColor(needsPublication ? SEND_COLOR_NEEDS_PUBLICATION : SEND_COLOR_NORMAL);
}

} // namespace

void createUi(State *parent, Ui *out, const char *interfaceId)
{
	if (out == 0 || parent == 0)
	{
		return;
	}
	out->btnSend = new TextButton(COMPACT_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, 0, 0);
	out->btnRetrieve = new TextButton(COMPACT_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, 0, 0);
	out->txtStatus = new Text(CompanionOnDemandRelayCore::RELAY_STATUS_WIDTH,
		CompanionOnDemandRelayCore::RELAY_STATUS_HEIGHT, 0, -100);
	out->btnSend->setText("SEND");
	out->btnRetrieve->setText("RETRIEVE");
	out->txtStatus->setColor(Palette::blockOffset(8));
	out->txtStatus->setHighContrast(true);
	out->txtManualNotice = new Text(CompanionOnDemandRelayCore::RELAY_STATUS_WIDTH,
		CompanionManualUploadHudCore::MANUAL_NOTICE_TEXT_HEIGHT, 0, -100);
	out->txtManualNotice->setColor(Palette::blockOffset(8));
	out->txtManualNotice->setHighContrast(true);
	out->btnAutoRetrieve = 0;
	out->btnAutoIntervalLess = 0;
	out->btnAutoIntervalMore = 0;
	out->txtAutoInterval = 0;
	g_hasAutoRetrieveUi = interfaceId != 0 && std::string(interfaceId) == "battlescape";
	if (g_hasAutoRetrieveUi)
	{
		out->btnAutoRetrieve = new TextButton(AUTO_RETRIEVE_TOGGLE_WIDTH, COMPACT_BUTTON_HEIGHT, 0, 0);
		out->btnAutoIntervalLess = new TextButton(AUTO_RETRIEVE_STEP_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, 0, 0);
		out->btnAutoIntervalMore = new TextButton(AUTO_RETRIEVE_STEP_BUTTON_WIDTH, COMPACT_BUTTON_HEIGHT, 0, 0);
		out->txtAutoInterval = new Text(AUTO_RETRIEVE_INTERVAL_TEXT_WIDTH,
			CompanionOnDemandRelayCore::RELAY_STATUS_HEIGHT, 0, -100);
		out->btnAutoIntervalLess->setText("-");
		out->btnAutoIntervalMore->setText("+");
		out->txtAutoInterval->setColor(Palette::blockOffset(8));
		out->txtAutoInterval->setHighContrast(true);
		parent->add(out->btnAutoRetrieve, "button", interfaceId);
		parent->add(out->btnAutoIntervalLess, "button", interfaceId);
		parent->add(out->btnAutoIntervalMore, "button", interfaceId);
		parent->add(out->txtAutoInterval, "text", interfaceId);
	}
	parent->add(out->btnSend, "button", interfaceId);
	parent->add(out->btnRetrieve, "button", interfaceId);
	parent->add(out->txtStatus, "text", interfaceId);
	parent->add(out->txtManualNotice, "text", interfaceId);
	g_showStatusOverlay = true;
	setControlsVisible(out, false);
}

void layoutForBaseViewport(Ui *ui, int viewportX, int viewportY, int viewportWidth)
{
	CompactBarLayout layout;
	if (ui == 0 || !CompanionOnDemandRelayLayout::computeBaseViewportBarLayout(
		viewportX, viewportY, viewportWidth, &layout))
	{
		return;
	}
	applyCompactBarLayout(ui, layout);
}

void layoutForBattlescapeTop(Ui *ui, int screenWidth, int screenHeight)
{
	CompactBarLayout layout;
	if (ui == 0 || !CompanionOnDemandRelayLayout::computeBattlescapeTopBarLayout(
		screenWidth, screenHeight, &layout))
	{
		return;
	}
	applyCompactBarLayout(ui, layout);
	if (g_hasAutoRetrieveUi)
	{
		applyAutoRetrieveLayout(ui, layout);
		refreshAutoRetrieveLabels(ui);
	}
}

void enterBattlescapeSession()
{
	CompanionAutoRetrieveCore::enterBattlescape();
}

void leaveBattlescapeSession()
{
	CompanionAutoRetrieveCore::leaveBattlescape();
}

void setControlsVisible(Ui *ui, bool visible)
{
	if (ui == 0)
	{
		return;
	}
	if (ui->btnSend != 0)
	{
		ui->btnSend->setVisible(visible);
	}
	if (ui->btnRetrieve != 0)
	{
		ui->btnRetrieve->setVisible(visible);
	}
	if (ui->txtStatus != 0)
	{
		const bool hasLabel = !g_lastStatusLabel.empty();
		ui->txtStatus->setVisible(visible && g_showStatusOverlay && hasLabel);
	}
	if (ui->txtManualNotice != 0)
	{
		const bool hasManual = !g_lastManualNoticeLabel.empty();
		ui->txtManualNotice->setVisible(visible && hasManual);
	}
	setAutoRetrieveControlsVisible(ui, visible);
}

void tick(Ui *ui)
{
	if (ui == 0)
	{
		return;
	}
	const bool configured = companionSnapshotPathConfigured();
	setControlsVisible(ui, configured);
	if (!configured || ui->txtStatus == 0)
	{
		return;
	}
	const long long wallNowMs = companionWallClockMs();
	if (reconcilePendingOnDemandRelay(wallNowMs))
	{
		g_lastStatusReadWallMs = 0;
	}
	RelayStatusView status;
	RelayUiPhase phase = RELAY_UI_IDLE;
	refreshStatusIfStale(wallNowMs, &status, &phase);
	applyButtonState(ui, phase, 0);
	std::string label;
	if (status.parsed)
	{
		formatRelayStatusLabel(status, &label);
	}
	else
	{
		label.clear();
	}
	if (label != g_lastStatusLabel)
	{
		ui->txtStatus->setText(label);
		g_lastStatusLabel = label;
		if (label.empty())
		{
			ui->txtStatus->setVisible(false);
		}
		else
		{
			ui->txtStatus->setVisible(true);
		}
	}
	if (ui->txtManualNotice != 0)
	{
		std::string manualLabel;
		CompanionManualUploadHudCore::ManualUploadHudView manualView;
		if (CompanionManualUploadHudCore::readManualUploadHudView(&manualView))
		{
			CompanionManualUploadHudCore::formatManualUploadHudText(manualView, &manualLabel);
		}
		else
		{
			manualLabel.clear();
		}
		if (manualLabel != g_lastManualNoticeLabel)
		{
			ui->txtManualNotice->setText(manualLabel);
			g_lastManualNoticeLabel = manualLabel;
		}
	}
	setControlsVisible(ui, configured);
	refreshAutoRetrieveLabels(ui);
}

void tickBattlescape(Ui *ui, SavedBattleGame *battle)
{
	tick(ui);
	if (ui != 0)
	{
		RelayStatusView status;
		RelayUiPhase phase = g_cachedPhase;
		if (g_hasCachedStatus)
		{
			status = g_cachedStatus;
			phase = mapStatusViewToUiPhase(status);
		}
		applyButtonState(ui, phase, battle);
	}
	if (ui == 0 || !CompanionAutoRetrieveCore::isBattlescapeActive())
	{
		return;
	}
	refreshAutoRetrieveLabels(ui);
	if (!CompanionAutoRetrieveCore::isEnabled() || !companionSnapshotPathConfigured())
	{
		return;
	}
	const long long wallNowMs = companionWallClockMs();
	if (!CompanionAutoRetrieveCore::shouldAttemptAutoRetrieve(g_cachedPhase, wallNowMs))
	{
		return;
	}
	CompanionAutoRetrieveCore::noteAutoRetrieveAttempt(wallNowMs);
	onRetrievePressed();
}

bool onRetrievePressed()
{
	std::string requestId;
	std::string error;
	if (!tryEnqueueAction("retrieve", &requestId, &error))
	{
		return false;
	}
	g_cachedPhase = RELAY_UI_RETRIEVING;
	g_cachedStatus = RelayStatusView();
	g_cachedStatus.parsed = true;
	g_cachedStatus.phase = "retrieving";
	g_cachedStatus.action = "retrieve";
	g_cachedStatus.terminal = false;
	g_hasCachedStatus = true;
	formatRelayStatusLabel(g_cachedStatus, &g_lastStatusLabel);
	return true;
}

bool onAutoRetrieveTogglePressed()
{
	CompanionAutoRetrieveCore::setEnabled(!CompanionAutoRetrieveCore::isEnabled());
	return true;
}

bool onAutoRetrieveIntervalLessPressed()
{
	CompanionAutoRetrieveCore::adjustIntervalSec(-1);
	return true;
}

bool onAutoRetrieveIntervalMorePressed()
{
	CompanionAutoRetrieveCore::adjustIntervalSec(1);
	return true;
}

bool onStrategicSendPressed()
{
	std::string requestId;
	std::string error;
	if (!tryEnqueueSend("strategic", 0, &requestId, &error))
	{
		return false;
	}
	g_cachedPhase = RELAY_UI_SENDING;
	g_cachedStatus = RelayStatusView();
	g_cachedStatus.parsed = true;
	g_cachedStatus.phase = "sending";
	g_cachedStatus.action = "send";
	g_cachedStatus.terminal = false;
	g_hasCachedStatus = true;
	formatRelayStatusLabel(g_cachedStatus, &g_lastStatusLabel);
	return true;
}

bool tryConsumeRelayMouseAction(State *state, Action *action, Ui *ui)
{
	if (state == 0 || action == 0 || ui == 0)
	{
		return false;
	}
	SDL_Event *ev = action->getDetails();
	if (ev == 0
	    || (ev->type != SDL_MOUSEBUTTONDOWN && ev->type != SDL_MOUSEBUTTONUP))
	{
		return false;
	}
	const int absX = static_cast<int>(action->getAbsoluteXMouse());
	const int absY = static_cast<int>(action->getAbsoluteYMouse());
	if (absX < 0 || absY < 0)
	{
		return false;
	}
	if (ui->btnSend == 0 || ui->btnRetrieve == 0)
	{
		return false;
	}
	if (g_hasAutoRetrieveUi && ui->btnAutoRetrieve != 0 && ui->btnAutoRetrieve->getVisible()
	    && ui->btnAutoIntervalLess != 0 && ui->btnAutoIntervalMore != 0)
	{
		if (relayControlRectHitTest(absX, absY,
		        ui->btnAutoIntervalMore->getX(), ui->btnAutoIntervalMore->getY(),
		        ui->btnAutoIntervalMore->getWidth(), ui->btnAutoIntervalMore->getHeight(), true))
		{
			ui->btnAutoIntervalMore->handle(action, state);
			ev->type = SDL_NOEVENT;
			return true;
		}
		if (relayControlRectHitTest(absX, absY,
		        ui->btnAutoIntervalLess->getX(), ui->btnAutoIntervalLess->getY(),
		        ui->btnAutoIntervalLess->getWidth(), ui->btnAutoIntervalLess->getHeight(), true))
		{
			ui->btnAutoIntervalLess->handle(action, state);
			ev->type = SDL_NOEVENT;
			return true;
		}
		if (relayControlRectHitTest(absX, absY,
		        ui->btnAutoRetrieve->getX(), ui->btnAutoRetrieve->getY(),
		        ui->btnAutoRetrieve->getWidth(), ui->btnAutoRetrieve->getHeight(), true))
		{
			ui->btnAutoRetrieve->handle(action, state);
			ev->type = SDL_NOEVENT;
			return true;
		}
	}
	const RelayControlHit hit = relayControlHitTarget(
		absX,
		absY,
		ui->btnSend->getX(),
		ui->btnSend->getY(),
		ui->btnSend->getWidth(),
		ui->btnSend->getHeight(),
		ui->btnSend->getVisible(),
		ui->btnRetrieve->getX(),
		ui->btnRetrieve->getY(),
		ui->btnRetrieve->getWidth(),
		ui->btnRetrieve->getHeight(),
		ui->btnRetrieve->getVisible());
	if (hit == RELAY_CONTROL_HIT_NONE)
	{
		return false;
	}
	TextButton *target = hit == RELAY_CONTROL_HIT_SEND ? ui->btnSend : ui->btnRetrieve;
	if (target != 0)
	{
		target->handle(action, state);
	}
	ev->type = SDL_NOEVENT;
	return true;
}

bool absPointHitsVisibleRelayControl(const Ui *ui, int absX, int absY)
{
	if (ui == 0 || ui->btnSend == 0 || ui->btnRetrieve == 0)
	{
		return false;
	}
	if (g_hasAutoRetrieveUi && ui->btnAutoRetrieve != 0 && ui->btnAutoRetrieve->getVisible()
	    && ui->btnAutoIntervalLess != 0 && ui->btnAutoIntervalMore != 0)
	{
		if (relayControlRectHitTest(absX, absY,
		        ui->btnAutoRetrieve->getX(), ui->btnAutoRetrieve->getY(),
		        ui->btnAutoRetrieve->getWidth(), ui->btnAutoRetrieve->getHeight(), true))
		{
			return true;
		}
		if (relayControlRectHitTest(absX, absY,
		        ui->btnAutoIntervalLess->getX(), ui->btnAutoIntervalLess->getY(),
		        ui->btnAutoIntervalLess->getWidth(), ui->btnAutoIntervalLess->getHeight(), true))
		{
			return true;
		}
		if (relayControlRectHitTest(absX, absY,
		        ui->btnAutoIntervalMore->getX(), ui->btnAutoIntervalMore->getY(),
		        ui->btnAutoIntervalMore->getWidth(), ui->btnAutoIntervalMore->getHeight(), true))
		{
			return true;
		}
	}
	return relayControlHitTarget(
	           absX,
	           absY,
	           ui->btnSend->getX(),
	           ui->btnSend->getY(),
	           ui->btnSend->getWidth(),
	           ui->btnSend->getHeight(),
	           ui->btnSend->getVisible(),
	           ui->btnRetrieve->getX(),
	           ui->btnRetrieve->getY(),
	           ui->btnRetrieve->getWidth(),
	           ui->btnRetrieve->getHeight(),
	           ui->btnRetrieve->getVisible())
	    != RELAY_CONTROL_HIT_NONE;
}

} // namespace CompanionOnDemandRelayOverlay

} // namespace OpenXcom
