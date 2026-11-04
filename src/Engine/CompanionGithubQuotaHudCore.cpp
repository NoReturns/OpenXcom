/*
 * Open X-Com Companion — GitHub quota HUD file reader (M1-25D overlay).
 */
#include "CompanionGithubQuotaHudCore.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionJsonLite.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace OpenXcom
{

namespace CompanionGithubQuotaHudCore
{

namespace
{

const char *SNAPSHOT_ENV = "OPENXCOM_COMPANION_SNAPSHOT_PATH";
const char *HUD_FILENAME = "companion-github-quota.json";

bool readTextFile(const std::string &path, std::string *out)
{
	if (out == 0)
	{
		return false;
	}
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		return false;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	*out = ss.str();
	return true;
}

void clearSnapshot(HudSnapshot *out)
{
	out->configured = false;
	out->fileReadable = false;
	out->remainingKnown = false;
	out->limitKnown = false;
	out->resetKnown = false;
	out->gateCooldownKnown = false;
	out->updatedAtMsKnown = false;
	out->observationAtMsKnown = false;
	out->observationStale = false;
	out->remaining = 0;
	out->limit = 0;
	out->resetEpochSec = 0;
	out->gateCooldownUntilMs = 0;
	out->updatedAtMs = 0;
	out->observationAtMs = 0;
}

bool parseOptionalIntField(const std::string &json, const std::string &key, int *out, bool *known)
{
	if (out == 0 || known == 0)
	{
		return false;
	}
	*known = CompanionJsonLite::extractJsonIntField(json, key, out);
	return *known;
}

bool parseOptionalLongLongField(const std::string &json, const std::string &key, long long *out, bool *known)
{
	if (out == 0 || known == 0)
	{
		return false;
	}
	std::string::size_type valueStart = 0;
	if (!CompanionJsonLite::findJsonKeyValueStart(json, key, &valueStart))
	{
		*known = false;
		return false;
	}
	std::string::size_type pos = valueStart;
	if (!CompanionJsonLite::skipWhitespace(json, &pos))
	{
		*known = false;
		return false;
	}
	char *endPtr = 0;
	const long long value = std::strtoll(json.c_str() + pos, &endPtr, 10);
	if (endPtr == json.c_str() + pos)
	{
		*known = false;
		return false;
	}
	*out = value;
	*known = true;
	return true;
}

bool parseOptionalBoolField(const std::string &json, const std::string &key, bool *out, bool *known)
{
	if (out == 0 || known == 0)
	{
		return false;
	}
	std::string::size_type valueStart = 0;
	if (!CompanionJsonLite::findJsonKeyValueStart(json, key, &valueStart))
	{
		*known = false;
		return false;
	}
	std::string::size_type pos = valueStart;
	if (!CompanionJsonLite::skipWhitespace(json, &pos))
	{
		*known = false;
		return false;
	}
	if (json.compare(pos, 4, "true") == 0)
	{
		*out = true;
		*known = true;
		return true;
	}
	if (json.compare(pos, 5, "false") == 0)
	{
		*out = false;
		*known = true;
		return true;
	}
	*known = false;
	return false;
}

} // namespace

long long companionWallClockMs()
{
	return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
}

bool companionSnapshotPathConfigured()
{
	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	return snapshotPath != 0 && snapshotPath[0] != '\0';
}

bool resolveHudFilePath(std::string *outPath)
{
	if (outPath == 0)
	{
		return false;
	}
	outPath->clear();
	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	if (snapshotPath == 0 || snapshotPath[0] == '\0')
	{
		return false;
	}
	const std::string dir = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(snapshotPath);
#ifdef _WIN32
	*outPath = dir + "\\" + HUD_FILENAME;
#else
	*outPath = dir + "/" + HUD_FILENAME;
#endif
	return true;
}

bool readHudSnapshot(HudSnapshot *out)
{
	if (out == 0)
	{
		return false;
	}
	clearSnapshot(out);
	out->configured = companionSnapshotPathConfigured();
	if (!out->configured)
	{
		return false;
	}
	std::string path;
	if (!resolveHudFilePath(&path))
	{
		return false;
	}
	std::string json;
	if (!readTextFile(path, &json))
	{
		return true;
	}
	out->fileReadable = true;
	int schema = 0;
	if (!CompanionJsonLite::extractJsonIntField(json, "schema", &schema) || schema != 1)
	{
		return true;
	}
	parseOptionalIntField(json, "remaining", &out->remaining, &out->remainingKnown);
	parseOptionalIntField(json, "limit", &out->limit, &out->limitKnown);
	parseOptionalLongLongField(json, "reset_epoch_sec", &out->resetEpochSec, &out->resetKnown);
	parseOptionalLongLongField(json, "gate_cooldown_until_ms", &out->gateCooldownUntilMs, &out->gateCooldownKnown);
	parseOptionalLongLongField(json, "updated_at_ms", &out->updatedAtMs, &out->updatedAtMsKnown);
	parseOptionalLongLongField(json, "observation_at_ms", &out->observationAtMs, &out->observationAtMsKnown);
	bool staleFromFile = false;
	bool staleFromFileKnown = false;
	parseOptionalBoolField(json, "stale_observation", &staleFromFile, &staleFromFileKnown);
	if (staleFromFileKnown)
	{
		out->observationStale = staleFromFile;
	}
	return true;
}

bool isHudTelemetryFresh(const HudSnapshot &snap, long long wallNowMs)
{
	if (!snap.observationAtMsKnown)
	{
		return false;
	}
	if (wallNowMs < snap.observationAtMs)
	{
		return false;
	}
	return (wallNowMs - snap.observationAtMs) <= HUD_RELAY_STALE_MS;
}

void applyRelayStaleness(HudSnapshot *snap, long long wallNowMs)
{
	if (snap == 0 || !snap->fileReadable)
	{
		return;
	}
	if (snap->observationAtMsKnown)
	{
		snap->observationStale = !isHudTelemetryFresh(*snap, wallNowMs);
	}
	else if (!snap->remainingKnown && !snap->limitKnown)
	{
		snap->observationStale = true;
	}
}

int computeHudAnchorY(int screenHeight, int bottomReservePx)
{
	const int hudHeight = 30;
	const int margin = 4;
	const int y = screenHeight - bottomReservePx - hudHeight - margin;
	return y > 0 ? y : 0;
}

void formatRemainingLabel(const HudSnapshot &snap, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	if (snap.remainingKnown)
	{
		std::ostringstream ss;
		ss << snap.remaining;
		*out = ss.str();
	}
	else
	{
		*out = "UNKNOWN";
	}
}

void formatLimitLabel(const HudSnapshot &snap, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	if (snap.limitKnown)
	{
		std::ostringstream ss;
		ss << snap.limit;
		*out = ss.str();
	}
	else
	{
		*out = "UNKNOWN";
	}
}

long long countdownMsUntilReset(const HudSnapshot &snap, long long nowMs)
{
	long long best = -1;
	if (snap.resetKnown)
	{
		const long long resetMs = snap.resetEpochSec * 1000LL - nowMs;
		best = resetMs > 0 ? resetMs : 0;
	}
	if (snap.gateCooldownKnown)
	{
		const long long gateMs = snap.gateCooldownUntilMs - nowMs;
		const long long clamped = gateMs > 0 ? gateMs : 0;
		if (best < 0 || clamped > best)
		{
			best = clamped;
		}
	}
	return best;
}

void formatCountdownMmSs(long long countdownMs, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	if (countdownMs < 0)
	{
		*out = "--:--";
		return;
	}
	const long long totalSec = (countdownMs + 999) / 1000;
	const long long minutes = totalSec / 60;
	const long long seconds = totalSec % 60;
	std::ostringstream ss;
	ss << std::setw(2) << std::setfill('0') << minutes
	   << ':'
	   << std::setw(2) << std::setfill('0') << seconds;
	*out = ss.str();
}

void formatHudMultilineText(const HudSnapshot &snap, long long nowMs, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	std::string remainingLabel;
	std::string limitLabel;
	std::string countdownLabel;
	formatRemainingLabel(snap, &remainingLabel);
	formatLimitLabel(snap, &limitLabel);
	formatCountdownMmSs(countdownMsUntilReset(snap, nowMs), &countdownLabel);
	std::ostringstream ss;
	if (snap.observationStale && snap.remainingKnown)
	{
		remainingLabel = std::string("~") + remainingLabel;
	}
	if (snap.observationStale && snap.limitKnown)
	{
		limitLabel = std::string("~") + limitLabel;
	}
	const char *title = (snap.observationStale && snap.remainingKnown)
		? "Github API Requests (cached):"
		: "Github API Requests:";
	ss << title << '\n'
	   << remainingLabel << " / " << limitLabel << '\n'
	   << countdownLabel;
	*out = ss.str();
}

} // namespace CompanionGithubQuotaHudCore

} // namespace OpenXcom
