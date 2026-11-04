/*
 * Open X-Com Companion — update in-game GitHub quota HUD text (M1-25D).
 */
#include "CompanionGithubQuotaHudOverlay.h"
#include "CompanionGithubQuotaHudCore.h"
#include "../Engine/Palette.h"
#include "../Interface/Text.h"
#include <SDL_types.h>

namespace OpenXcom
{

namespace CompanionGithubQuotaHudOverlay
{

namespace
{

using CompanionGithubQuotaHudCore::HudSnapshot;
using CompanionGithubQuotaHudCore::applyRelayStaleness;
using CompanionGithubQuotaHudCore::companionSnapshotPathConfigured;
using CompanionGithubQuotaHudCore::companionWallClockMs;
using CompanionGithubQuotaHudCore::computeHudAnchorY;
using CompanionGithubQuotaHudCore::formatHudMultilineText;
using CompanionGithubQuotaHudCore::readHudSnapshot;

const Uint32 FILE_REFRESH_MS = 1000;

HudSnapshot g_cachedSnapshot;
long long g_lastFileReadWallMs = 0;
std::string g_lastRendered;

void refreshSnapshotIfStale(long long wallNowMs)
{
	if (g_lastFileReadWallMs != 0
	    && wallNowMs >= g_lastFileReadWallMs
	    && wallNowMs - g_lastFileReadWallMs < FILE_REFRESH_MS)
	{
		return;
	}
	g_lastFileReadWallMs = wallNowMs;
	readHudSnapshot(&g_cachedSnapshot);
	applyRelayStaleness(&g_cachedSnapshot, wallNowMs);
}

} // namespace

Text *createHudText(int anchorY)
{
	Text *target = new Text(HUD_TEXT_WIDTH, HUD_TEXT_HEIGHT, HUD_MARGIN_X, anchorY);
	target->setColor(Palette::blockOffset(8));
	target->setHighContrast(true);
	target->setVisible(false);
	return target;
}

void layoutHudText(Text *target, int screenHeight, int bottomReservePx)
{
	if (target == 0)
	{
		return;
	}
	target->setY(computeHudAnchorY(screenHeight, bottomReservePx));
}

void tick(Text *target)
{
	if (target == 0)
	{
		return;
	}
	if (!companionSnapshotPathConfigured())
	{
		target->setVisible(false);
		return;
	}
	const long long wallNowMs = companionWallClockMs();
	refreshSnapshotIfStale(wallNowMs);
	std::string text;
	formatHudMultilineText(g_cachedSnapshot, wallNowMs, &text);
	if (text != g_lastRendered)
	{
		target->setText(text);
		g_lastRendered = text;
	}
	target->setVisible(true);
}

} // namespace CompanionGithubQuotaHudOverlay

} // namespace OpenXcom
