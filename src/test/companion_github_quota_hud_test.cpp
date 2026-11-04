/*
 * Open X-Com Companion — GitHub quota HUD overlay formatting (M1-25D).
 */
#include "Engine/CompanionGithubQuotaHudCore.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#define COMPANION_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define COMPANION_MKDIR(path) mkdir(path, 0755)
#endif

using namespace OpenXcom;
using namespace OpenXcom::CompanionGithubQuotaHudCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static void testUnknownLabels()
{
	HudSnapshot snap = {};
	snap.configured = true;
	snap.fileReadable = false;
	std::string text;
	formatHudMultilineText(snap, 1'700'000'000'000LL, &text);
	expect(text.find("Github API Requests:") != std::string::npos, "HUD title line present");
	expect(text.find("UNKNOWN / UNKNOWN") != std::string::npos, "missing telemetry uses UNKNOWN");
	expect(text.find("--:--") != std::string::npos, "missing reset uses --:--");
}

static void testCountdownFromResetEpoch()
{
	HudSnapshot snap = {};
	snap.configured = true;
	snap.fileReadable = true;
	snap.updatedAtMsKnown = true;
	snap.updatedAtMs = 1'700'000'000'000LL;
	snap.remainingKnown = true;
	snap.limitKnown = true;
	snap.resetKnown = true;
	snap.remaining = 99;
	snap.limit = 5000;
	snap.resetEpochSec = 1'700'000'090LL;
	const long long nowMs = 1'700'000'000'000LL;
	std::string text;
	formatHudMultilineText(snap, nowMs, &text);
	expect(text.find("99 / 5000") != std::string::npos, "remaining/limit rendered");
	expect(text.find("01:30") != std::string::npos, "countdown rendered as MM:SS");
}

static void testStaleRelayClearsTelemetry()
{
	HudSnapshot snap = {};
	snap.fileReadable = true;
	snap.updatedAtMsKnown = true;
	snap.updatedAtMs = 1'000'000LL;
	snap.remainingKnown = true;
	snap.limitKnown = true;
	snap.remaining = 123;
	snap.limit = 5000;
	snap.resetKnown = true;
	snap.resetEpochSec = 1'700'000'090LL;
	applyRelayStaleness(&snap, 1'000'000LL + HUD_RELAY_STALE_MS + 1);
	expect(!snap.remainingKnown, "stale sidecar clears remaining");
	expect(!snap.limitKnown, "stale sidecar clears limit");
	expect(!snap.resetKnown, "stale sidecar clears reset epoch");
	std::string text;
	formatHudMultilineText(snap, 1'000'000LL + HUD_RELAY_STALE_MS + 1, &text);
	expect(text.find("UNKNOWN / UNKNOWN") != std::string::npos, "stale HUD shows UNKNOWN");
}

static void testBattlescapeHudAnchorAboveIconBar()
{
	const int screenHeight = 360;
	const int iconsHeight = 64;
	const int anchorY = computeHudAnchorY(screenHeight, iconsHeight + 4);
	expect(anchorY + 30 <= screenHeight - iconsHeight - 4, "720p-class anchor sits above icon bar");
}

static void testHudFilePathResolution()
{
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", "E:\\Open X-Com Companion\\local\\m0\\tactical-live.yaml");
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", "/tmp/local/m0/tactical-live.yaml", 1);
#endif
	std::string path;
	expect(resolveHudFilePath(&path), "HUD path resolves when snapshot env set");
#ifdef _WIN32
	expect(path.find("companion-github-quota.json") != std::string::npos, "HUD filename included");
#endif
}

int main()
{
	testUnknownLabels();
	testCountdownFromResetEpoch();
	testStaleRelayClearsTelemetry();
	testBattlescapeHudAnchorAboveIconBar();
	testHudFilePathResolution();
	if (g_failures != 0)
	{
		std::cerr << g_failures << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "companion_github_quota_hud_test: OK" << std::endl;
	return 0;
}
