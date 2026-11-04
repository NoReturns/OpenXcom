#pragma once
/*
 * Open X-Com Companion — GitHub quota HUD file reader (M1-25D overlay).
 */
#include <string>

namespace OpenXcom
{

namespace CompanionGithubQuotaHudCore
{

/** Sidecar must refresh companion-github-quota.json more often than this. */
const long long HUD_RELAY_STALE_MS = 45000LL;

struct HudSnapshot
{
	bool configured;
	bool fileReadable;
	bool remainingKnown;
	bool limitKnown;
	bool resetKnown;
	bool gateCooldownKnown;
	bool updatedAtMsKnown;
	int remaining;
	int limit;
	long long resetEpochSec;
	long long gateCooldownUntilMs;
	long long updatedAtMs;
};

long long companionWallClockMs();
bool companionSnapshotPathConfigured();
bool resolveHudFilePath(std::string *outPath);
bool readHudSnapshot(HudSnapshot *out);
bool isHudTelemetryFresh(const HudSnapshot &snap, long long wallNowMs);
void applyRelayStaleness(HudSnapshot *snap, long long wallNowMs);
int computeHudAnchorY(int screenHeight, int bottomReservePx);
void formatRemainingLabel(const HudSnapshot &snap, std::string *out);
void formatLimitLabel(const HudSnapshot &snap, std::string *out);
long long countdownMsUntilReset(const HudSnapshot &snap, long long wallNowMs);
void formatCountdownMmSs(long long countdownMs, std::string *out);
void formatHudMultilineText(const HudSnapshot &snap, long long wallNowMs, std::string *out);

} // namespace CompanionGithubQuotaHudCore

} // namespace OpenXcom
