/*
 * Open X-Com Companion — battlescape AUTO RETRIEVE scheduling (M1).
 */
#include "CompanionAutoRetrieveCore.h"
#include <algorithm>
#include <cstdio>

namespace OpenXcom
{

namespace CompanionAutoRetrieveCore
{

namespace
{

bool g_enabled = false;
int g_intervalSec = AUTO_RETRIEVE_INTERVAL_DEFAULT_SEC;
bool g_battlescapeActive = false;
long long g_lastAttemptWallMs = 0;

} // namespace

void resetForNewGameSession()
{
	g_enabled = false;
	g_intervalSec = AUTO_RETRIEVE_INTERVAL_DEFAULT_SEC;
	g_battlescapeActive = false;
	g_lastAttemptWallMs = 0;
}

void enterBattlescape()
{
	g_enabled = false;
	g_intervalSec = AUTO_RETRIEVE_INTERVAL_DEFAULT_SEC;
	g_battlescapeActive = true;
	g_lastAttemptWallMs = 0;
}

void leaveBattlescape()
{
	g_enabled = false;
	g_battlescapeActive = false;
}

bool isEnabled()
{
	return g_enabled;
}

int intervalSec()
{
	return g_intervalSec;
}

long long lastAttemptWallMs()
{
	return g_lastAttemptWallMs;
}

bool isBattlescapeActive()
{
	return g_battlescapeActive;
}

void setEnabled(bool enabled)
{
	g_enabled = enabled && g_battlescapeActive;
}

int clampIntervalSec(int sec)
{
	if (sec < AUTO_RETRIEVE_INTERVAL_MIN_SEC)
	{
		return AUTO_RETRIEVE_INTERVAL_MIN_SEC;
	}
	if (sec > AUTO_RETRIEVE_INTERVAL_MAX_SEC)
	{
		return AUTO_RETRIEVE_INTERVAL_MAX_SEC;
	}
	return sec;
}

void adjustIntervalSec(int deltaSteps)
{
	g_intervalSec = clampIntervalSec(g_intervalSec + deltaSteps * AUTO_RETRIEVE_INTERVAL_STEP_SEC);
}

bool isRelayPhaseBlockingAutoRetrieve(CompanionOnDemandRelayCore::RelayUiPhase phase)
{
	using CompanionOnDemandRelayCore::RELAY_UI_DEFERRED;
	using CompanionOnDemandRelayCore::RELAY_UI_RETRIEVING;
	using CompanionOnDemandRelayCore::RELAY_UI_SENDING;
	return phase == RELAY_UI_SENDING
	    || phase == RELAY_UI_RETRIEVING
	    || phase == RELAY_UI_DEFERRED;
}

bool shouldAttemptAutoRetrieve(CompanionOnDemandRelayCore::RelayUiPhase phase, long long wallNowMs)
{
	if (!g_enabled || !g_battlescapeActive)
	{
		return false;
	}
	if (isRelayPhaseBlockingAutoRetrieve(phase))
	{
		return false;
	}
	const long long intervalMs = static_cast<long long>(g_intervalSec) * 1000;
	if (g_lastAttemptWallMs > 0
	    && wallNowMs >= g_lastAttemptWallMs
	    && wallNowMs - g_lastAttemptWallMs < intervalMs)
	{
		return false;
	}
	return true;
}

void noteAutoRetrieveAttempt(long long wallNowMs)
{
	g_lastAttemptWallMs = wallNowMs;
}

void formatToggleButtonText(bool enabled, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	*out = enabled ? "AUTO:ON" : "AUTO:OFF";
}

void formatIntervalLabel(int intervalSec, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%ds", clampIntervalSec(intervalSec));
	*out = buf;
}

} // namespace CompanionAutoRetrieveCore

} // namespace OpenXcom
