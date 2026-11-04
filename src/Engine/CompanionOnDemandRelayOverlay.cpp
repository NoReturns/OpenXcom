/*
 * Open X-Com Companion — send/retrieve relay controls overlay (M1-25E / M1-26A).
 */
#include "CompanionOnDemandRelayOverlay.h"
#include "CompanionOnDemandRelayCore.h"
#include "CompanionOnDemandRelayLayout.h"
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
using CompanionOnDemandRelayCore::formatStatusLabel;
using CompanionOnDemandRelayCore::isRelayBusyPhase;
using CompanionOnDemandRelayCore::mapPhaseString;
using CompanionOnDemandRelayCore::readStatusPhase;
using CompanionOnDemandRelayCore::resolveStatusPath;
using CompanionOnDemandRelayCore::tryEnqueueAction;
using CompanionOnDemandRelayCore::tryEnqueueSend;
using CompanionOnDemandRelayCore::reconcilePendingOnDemandRelay;
using CompanionOnDemandRelayCore::relayControlHitTarget;
using CompanionOnDemandRelayCore::RelayControlHit;
using CompanionOnDemandRelayCore::RELAY_CONTROL_HIT_NONE;
using CompanionOnDemandRelayCore::RELAY_CONTROL_HIT_RETRIEVE;
using CompanionOnDemandRelayCore::RELAY_CONTROL_HIT_SEND;
using CompanionOnDemandRelayLayout::CompactBarLayout;
using CompanionOnDemandRelayLayout::COMPACT_BUTTON_HEIGHT;
using CompanionOnDemandRelayLayout::COMPACT_BUTTON_WIDTH;

const Uint32 FILE_REFRESH_MS = 400;

RelayUiPhase g_cachedPhase = RELAY_UI_IDLE;
long long g_lastStatusReadWallMs = 0;
std::string g_lastStatusLabel;
bool g_showStatusOverlay = false;

bool readStatusFile(RelayUiPhase *outPhase)
{
	if (outPhase == 0)
	{
		return false;
	}
	std::string statusPath;
	if (!resolveStatusPath(&statusPath))
	{
		*outPhase = RELAY_UI_IDLE;
		return false;
	}
	std::ifstream in(statusPath.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		*outPhase = RELAY_UI_IDLE;
		return false;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	const std::string json = ss.str();
	std::string phase;
	std::string action;
	readStatusPhase(json, &phase, 0);
	CompanionJsonLite::extractJsonStringField(json, "action", &action);
	*outPhase = mapPhaseString(phase, action);
	return true;
}

void refreshPhaseIfStale(long long wallNowMs, RelayUiPhase *phase)
{
	if (g_lastStatusReadWallMs != 0
	    && wallNowMs >= g_lastStatusReadWallMs
	    && wallNowMs - g_lastStatusReadWallMs < FILE_REFRESH_MS)
	{
		*phase = g_cachedPhase;
		return;
	}
	g_lastStatusReadWallMs = wallNowMs;
	readStatusFile(&g_cachedPhase);
	*phase = g_cachedPhase;
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
}

void applyButtonState(Ui * /*ui*/, RelayUiPhase /*phase*/)
{
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
	parent->add(out->btnSend, "button", interfaceId);
	parent->add(out->btnRetrieve, "button", interfaceId);
	parent->add(out->txtStatus, "text", interfaceId);
	g_showStatusOverlay = false;
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

void layoutForBattlescapeTop(Ui *ui, int screenWidth)
{
	CompactBarLayout layout;
	if (ui == 0 || !CompanionOnDemandRelayLayout::computeBattlescapeTopBarLayout(screenWidth, &layout))
	{
		return;
	}
	applyCompactBarLayout(ui, layout);
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
		ui->txtStatus->setVisible(visible && g_showStatusOverlay);
	}
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
		g_lastStatusLabel.clear();
	}
	RelayUiPhase phase = RELAY_UI_IDLE;
	refreshPhaseIfStale(wallNowMs, &phase);
	applyButtonState(ui, phase);
	std::string label;
	formatStatusLabel(phase, &label);
	if (label != g_lastStatusLabel)
	{
		ui->txtStatus->setText(label);
		g_lastStatusLabel = label;
	}
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
	g_lastStatusLabel.clear();
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
	g_lastStatusLabel.clear();
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
