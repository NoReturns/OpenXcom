/*
 * Open X-Com Companion — briefing request core (testable without battle snapshot).
 */
#include "CompanionBriefingTestSupport.h"
#include "CompanionSha256.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdio>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace OpenXcom
{

namespace
{

std::string formatRequestId(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
	char buf[33];
	std::sprintf(buf, "%08x%08x%08x%08x", w0, w1, w2, w3);
	return std::string(buf);
}

uint32_t mixRequestWord(uint32_t a, uint32_t b)
{
	uint64_t x = (static_cast<uint64_t>(a) << 32) ^ static_cast<uint64_t>(b);
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdULL;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ULL;
	x ^= x >> 33;
	return static_cast<uint32_t>(x ^ (x >> 32));
}

bool writeTextAtomically(const std::string &destPath, const std::string &content)
{
	const std::string tmpPath = destPath + ".companion.tmp";

	{
		std::ofstream out(tmpPath.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
		if (!out)
		{
			return false;
		}
		out << content;
		out.flush();
		if (!out)
		{
#ifdef _WIN32
			DeleteFileA(tmpPath.c_str());
#else
			std::remove(tmpPath.c_str());
#endif
			return false;
		}
	}

#ifdef _WIN32
	if (!MoveFileExA(tmpPath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		DeleteFileA(tmpPath.c_str());
		return false;
	}
#else
	if (std::rename(tmpPath.c_str(), destPath.c_str()) != 0)
	{
		std::remove(tmpPath.c_str());
		return false;
	}
#endif
	return true;
}

} // namespace

namespace CompanionBriefingTestSupport
{

std::string parentDirectoryOfSnapshotPath(const std::string &snapshotPath)
{
	const std::string::size_type pos = snapshotPath.find_last_of("/\\");
	if (pos == std::string::npos)
	{
		return std::string();
	}
	return snapshotPath.substr(0, pos);
}

std::string makeRequestId()
{
	static std::atomic<uint32_t> requestNonce(0);
	const uint32_t nonce = ++requestNonce;
	const uint64_t ticks = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	const uint32_t w0 = mixRequestWord(nonce, static_cast<uint32_t>(ticks));
	const uint32_t w1 = mixRequestWord(static_cast<uint32_t>(ticks >> 32), static_cast<uint32_t>(ticks));
	const uint32_t w2 = mixRequestWord(w0, w1 ^ nonce);
	const uint32_t w3 = mixRequestWord(w2, nonce ^ static_cast<uint32_t>(ticks >> 16));
	return formatRequestId(w0, w1, w2, w3);
}

bool isValidRequestIdFormat(const std::string &requestId)
{
	if (requestId.size() != 32)
	{
		return false;
	}
	for (std::string::size_type i = 0; i < requestId.size(); ++i)
	{
		const char c = requestId[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			return false;
		}
	}
	return true;
}

std::string buildBriefingRequestJson(const std::string &requestId, const std::string &sessionId, int turn,
	const std::string &snapshotSha256)
{
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"type\": \"tactical_briefing_request\",\n"
	     << "  \"request_id\": \"" << requestId << "\",\n"
	     << "  \"session_id\": \"" << sessionId << "\",\n"
	     << "  \"turn\": " << turn << ",\n"
	     << "  \"snapshot_sha256\": \"" << snapshotSha256 << "\"\n"
	     << "}\n";
	return json.str();
}

bool shouldWriteBriefingRequestAfterExport(bool exportSucceeded, const std::string &snapshotSha256)
{
	return exportSucceeded && CompanionSha256::isValidHexDigest(snapshotSha256);
}

bool canFulfillBriefing(const BriefingGateInput &input)
{
	if (!input.snapshotPathConfigured || !input.battleInitialized)
	{
		return false;
	}
	if (input.debugMode || !input.playerTurn || input.unitsFalling || input.battleBusy)
	{
		return false;
	}
	return true;
}

BriefingPressOutcome evaluateBriefingKeyPress(const BriefingGateInput &input, BriefingPressState *state)
{
	if (state == 0)
	{
		return BRIEFING_PRESS_IGNORED;
	}
	if (!input.snapshotPathConfigured || !input.battleInitialized)
	{
		return BRIEFING_PRESS_IGNORED;
	}
	if (input.debugMode)
	{
		return BRIEFING_PRESS_REJECTED;
	}
	if (!input.playerTurn)
	{
		return BRIEFING_PRESS_REJECTED;
	}
	if (canFulfillBriefing(input))
	{
		state->pending = false;
		return BRIEFING_PRESS_FULFILL_NOW;
	}
	state->pending = true;
	return BRIEFING_PRESS_DEFERRED;
}

bool shouldFulfillPendingBriefing(const BriefingGateInput &input, bool pending)
{
	return pending && canFulfillBriefing(input);
}

bool writeBriefingRequestFile(const std::string &requestDir, const std::string &jsonContent)
{
	if (requestDir.empty())
	{
		return false;
	}
#ifdef _WIN32
	const char pathSep = '\\';
#else
	const char pathSep = '/';
#endif
	const std::string requestPath = requestDir + pathSep + "briefing-request.json";
	return writeTextAtomically(requestPath, jsonContent);
}

bool writeTacticalSendMarkerFile(const std::string &requestDir, const std::string &briefingRequestId,
	const std::string &sessionId, int turn, const std::string &snapshotSha256, long long exportedAtMs)
{
	if (requestDir.empty() || briefingRequestId.empty() || sessionId.empty()
	    || !CompanionSha256::isValidHexDigest(snapshotSha256))
	{
		return false;
	}
#ifdef _WIN32
	const char pathSep = '\\';
#else
	const char pathSep = '/';
#endif
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"briefing_request_id\": \"" << briefingRequestId << "\",\n"
	     << "  \"session_id\": \"" << sessionId << "\",\n"
	     << "  \"turn\": " << turn << ",\n"
	     << "  \"snapshot_sha256\": \"" << snapshotSha256 << "\",\n"
	     << "  \"exported_at_ms\": " << exportedAtMs << "\n"
	     << "}\n";
	const std::string markerPath = requestDir + pathSep + "companion-tactical-send-marker.json";
	return writeTextAtomically(markerPath, json.str());
}

bool writeStrategicSendMarkerFile(const std::string &requestDir, const std::string &onDemandRequestId,
	const std::string &contentSha256, const std::string &campaignId, long long exportedAtMs)
{
	if (requestDir.empty() || onDemandRequestId.empty() || campaignId.empty()
	    || !CompanionSha256::isValidHexDigest(contentSha256))
	{
		return false;
	}
#ifdef _WIN32
	const char pathSep = '\\';
#else
	const char pathSep = '/';
#endif
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"on_demand_request_id\": \"" << onDemandRequestId << "\",\n"
	     << "  \"content_sha256\": \"" << contentSha256 << "\",\n"
	     << "  \"campaign_id\": \"" << campaignId << "\",\n"
	     << "  \"exported_at_ms\": " << exportedAtMs << "\n"
	     << "}\n";
	const std::string markerPath = requestDir + pathSep + "companion-strategic-send-marker.json";
	return writeTextAtomically(markerPath, json.str());
}

void removeStrategicSendMarkerFile(const std::string &requestDir)
{
	if (requestDir.empty())
	{
		return;
	}
#ifdef _WIN32
	const char pathSep = '\\';
#else
	const char pathSep = '/';
#endif
	const std::string markerPath = requestDir + pathSep + "companion-strategic-send-marker.json";
#ifdef _WIN32
	DeleteFileA(markerPath.c_str());
#else
	std::remove(markerPath.c_str());
#endif
}

} // namespace CompanionBriefingTestSupport

} // namespace OpenXcom
