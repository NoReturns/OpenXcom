/*
 * Open X-Com Companion — on-demand send/retrieve relay queue (M1-25E).
 */
#include "CompanionOnDemandRelayCore.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionJsonLite.h"
#include <chrono>
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

namespace CompanionOnDemandRelayCore
{

namespace
{

const char *SNAPSHOT_ENV = "OPENXCOM_COMPANION_SNAPSHOT_PATH";
const char *REQUEST_FILENAME = "companion-on-demand-request.json";
const char *STATUS_FILENAME = "companion-on-demand-status.json";
const char *BRIEFING_REQUEST_FILENAME = "briefing-request.json";
const char *DAEMON_READY_FILENAME = ".companion-on-demand-daemon-ready.json";
const long long RELAY_OFFLINE_GRACE_MS = 2500;

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

bool fileExists(const std::string &path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	return static_cast<bool>(in);
}

bool buildInitialStatusJson(const std::string &requestId, const char *action, long long updatedAtMs, std::string *out)
{
	if (out == 0)
	{
		return false;
	}
	const char *phase = (std::string(action) == "retrieve") ? "retrieving" : "sending";
	std::ostringstream ss;
	ss << "{\n"
	   << "  \"request_id\": \"" << requestId << "\",\n"
	   << "  \"action\": \"" << action << "\",\n"
	   << "  \"phase\": \"" << phase << "\",\n"
	   << "  \"message\": \"\",\n"
	   << "  \"updated_at_ms\": " << updatedAtMs << ",\n"
	   << "  \"terminal\": false\n"
	   << "}\n";
	*out = ss.str();
	return true;
}

std::string buildRequestJson(const std::string &requestId, const char *action, long long createdAtMs,
	const char *sendScope = 0, const char *briefingRequestId = 0)
{
	std::ostringstream ss;
	ss << "{\n"
	   << "  \"request_id\": \"" << requestId << "\",\n"
	   << "  \"action\": \"" << action << "\",\n"
	   << "  \"status\": \"pending\",\n"
	   << "  \"created_at_ms\": " << createdAtMs;
	if (sendScope != 0 && sendScope[0] != '\0')
	{
		ss << ",\n  \"send_scope\": \"" << sendScope << "\"";
	}
	if (briefingRequestId != 0 && briefingRequestId[0] != '\0')
	{
		ss << ",\n  \"briefing_request_id\": \"" << briefingRequestId << "\"";
	}
	ss << "\n}\n";
	return ss.str();
}

bool pointInButton(int absX, int absY, int rectX, int rectY, int rectW, int rectH, bool visible)
{
	if (!visible || rectW <= 0 || rectH <= 0)
	{
		return false;
	}
	return absX >= rectX && absX < rectX + rectW
	    && absY >= rectY && absY < rectY + rectH;
}

bool writeTerminalFailureStatus(const std::string &statusPath, const std::string &requestId,
	const char *action, const char *message, long long updatedAtMs)
{
	std::ostringstream ss;
	ss << "{\n"
	   << "  \"request_id\": \"" << requestId << "\",\n"
	   << "  \"action\": \"" << action << "\",\n"
	   << "  \"phase\": \"failed\",\n"
	   << "  \"message\": \"" << message << "\",\n"
	   << "  \"updated_at_ms\": " << updatedAtMs << ",\n"
	   << "  \"terminal\": true\n"
	   << "}\n";
	return writeTextAtomically(statusPath, ss.str());
}

void removePendingRequestFile(const std::string &requestPath)
{
#ifdef _WIN32
	DeleteFileA(requestPath.c_str());
#else
	std::remove(requestPath.c_str());
#endif
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

bool resolveRelayDir(std::string *outDir)
{
	if (outDir == 0)
	{
		return false;
	}
	outDir->clear();
	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	if (snapshotPath == 0 || snapshotPath[0] == '\0')
	{
		return false;
	}
	*outDir = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(std::string(snapshotPath));
	return !outDir->empty();
}

bool resolveRequestPath(std::string *outPath)
{
	std::string dir;
	if (!resolveRelayDir(&dir))
	{
		return false;
	}
	*outPath = dir + "/" + REQUEST_FILENAME;
	return true;
}

bool resolveStatusPath(std::string *outPath)
{
	std::string dir;
	if (!resolveRelayDir(&dir))
	{
		return false;
	}
	*outPath = dir + "/" + STATUS_FILENAME;
	return true;
}

bool readStatusPhase(const std::string &statusJson, std::string *outPhase, bool *outTerminal)
{
	if (outPhase == 0)
	{
		return false;
	}
	outPhase->clear();
	if (!CompanionJsonLite::extractJsonStringField(statusJson, "phase", outPhase))
	{
		return false;
	}
	if (outTerminal != 0)
	{
		int terminalInt = 0;
		if (CompanionJsonLite::extractJsonIntField(statusJson, "terminal", &terminalInt))
		{
			*outTerminal = terminalInt != 0;
		}
		else if (statusJson.find("\"terminal\": true") != std::string::npos
			|| statusJson.find("\"terminal\":true") != std::string::npos)
		{
			*outTerminal = true;
		}
		else
		{
			*outTerminal = false;
		}
	}
	return true;
}

RelayUiPhase mapPhaseString(const std::string &phase, const std::string &action)
{
	if (phase == "sending")
	{
		return RELAY_UI_SENDING;
	}
	if (phase == "sent")
	{
		return RELAY_UI_SENT;
	}
	if (phase == "failed" && action == "send")
	{
		return RELAY_UI_SEND_FAILED;
	}
	if (phase == "not_ready")
	{
		return RELAY_UI_NOT_READY;
	}
	if (phase == "retrieving")
	{
		return RELAY_UI_RETRIEVING;
	}
	if (phase == "completed")
	{
		return RELAY_UI_COMPLETED;
	}
	if (phase == "no_new_orders")
	{
		return RELAY_UI_NO_NEW_ORDERS;
	}
	if (phase == "failed" && action == "retrieve")
	{
		return RELAY_UI_RETRIEVE_FAILED;
	}
	if (phase == "failed")
	{
		return action == "retrieve" ? RELAY_UI_RETRIEVE_FAILED : RELAY_UI_SEND_FAILED;
	}
	return RELAY_UI_IDLE;
}

bool isRelayBusyPhase(RelayUiPhase phase)
{
	return phase == RELAY_UI_SENDING || phase == RELAY_UI_RETRIEVING;
}

bool tryEnqueueAction(const char *action, std::string *outRequestId, std::string *outError)
{
	if (action == 0 || std::string(action) != "retrieve")
	{
		if (outError != 0)
		{
			*outError = "invalid_action";
		}
		return false;
	}
	return tryEnqueueSend("retrieve", 0, outRequestId, outError);
}

bool tryEnqueueSend(const char *sendScope, const char *briefingRequestId,
	std::string *outRequestId, std::string *outError)
{
	const char *action = "send";
	if (sendScope == 0 || sendScope[0] == '\0')
	{
		if (outError != 0)
		{
			*outError = "invalid_send_scope";
		}
		return false;
	}
	const std::string scope(sendScope);
	if (scope == "retrieve")
	{
		action = "retrieve";
	}
	else if (scope != "tactical" && scope != "strategic")
	{
		if (outError != 0)
		{
			*outError = "invalid_send_scope";
		}
		return false;
	}
	if (scope == "tactical" && (briefingRequestId == 0 || briefingRequestId[0] == '\0'))
	{
		if (outError != 0)
		{
			*outError = "missing_briefing_request_id";
		}
		return false;
	}
	if (!companionSnapshotPathConfigured())
	{
		if (outError != 0)
		{
			*outError = "snapshot_not_configured";
		}
		return false;
	}

	std::string requestPath;
	std::string statusPath;
	if (!resolveRequestPath(&requestPath) || !resolveStatusPath(&statusPath))
	{
		if (outError != 0)
		{
			*outError = "path_resolution_failed";
		}
		return false;
	}

	if (fileExists(requestPath))
	{
		if (outError != 0)
		{
			*outError = "request_in_flight";
		}
		return false;
	}

	std::string statusJson;
	if (readTextFile(statusPath, &statusJson))
	{
		std::string phase;
		std::string statusAction;
		bool terminal = true;
		readStatusPhase(statusJson, &phase, &terminal);
		CompanionJsonLite::extractJsonStringField(statusJson, "action", &statusAction);
		const RelayUiPhase uiPhase = mapPhaseString(phase, statusAction);
		if (isRelayBusyPhase(uiPhase) || !terminal)
		{
			if (outError != 0)
			{
				*outError = "relay_busy";
			}
			return false;
		}
	}

	const std::string requestId = CompanionBriefingTestSupport::makeRequestId();
	const long long nowMs = companionWallClockMs();
	const char *scopeForJson = (scope == "retrieve") ? 0 : scope.c_str();
	if (!writeTextAtomically(requestPath, buildRequestJson(
		requestId, action, nowMs, scopeForJson, briefingRequestId)))
	{
		if (outError != 0)
		{
			*outError = "request_write_failed";
		}
		return false;
	}

	std::string initialStatus;
	buildInitialStatusJson(requestId, action, nowMs, &initialStatus);
	writeTextAtomically(statusPath, initialStatus);

	if (outRequestId != 0)
	{
		*outRequestId = requestId;
	}
	return true;
}

void formatStatusLabel(RelayUiPhase phase, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	switch (phase)
	{
	case RELAY_UI_SENDING:
		*out = "Sending";
		break;
	case RELAY_UI_SENT:
		*out = "Sent";
		break;
	case RELAY_UI_SEND_FAILED:
		*out = "Failed";
		break;
	case RELAY_UI_NOT_READY:
		*out = "Not ready";
		break;
	case RELAY_UI_RETRIEVING:
		*out = "Retrieving";
		break;
	case RELAY_UI_COMPLETED:
		*out = "Completed";
		break;
	case RELAY_UI_NO_NEW_ORDERS:
		*out = "No new orders";
		break;
	case RELAY_UI_RETRIEVE_FAILED:
		*out = "Failed";
		break;
	default:
		out->clear();
		break;
	}
}

bool relayControlRectHitTest(int absX, int absY, int rectX, int rectY, int rectW, int rectH, bool visible)
{
	if (!companionSnapshotPathConfigured())
	{
		return false;
	}
	return pointInButton(absX, absY, rectX, rectY, rectW, rectH, visible);
}

RelayControlHit relayControlHitTarget(int absX, int absY,
	int sendX, int sendY, int sendW, int sendH, bool sendVisible,
	int retrieveX, int retrieveY, int retrieveW, int retrieveH, bool retrieveVisible)
{
	if (!companionSnapshotPathConfigured())
	{
		return RELAY_CONTROL_HIT_NONE;
	}
	if (pointInButton(absX, absY, retrieveX, retrieveY, retrieveW, retrieveH, retrieveVisible))
	{
		return RELAY_CONTROL_HIT_RETRIEVE;
	}
	if (pointInButton(absX, absY, sendX, sendY, sendW, sendH, sendVisible))
	{
		return RELAY_CONTROL_HIT_SEND;
	}
	return RELAY_CONTROL_HIT_NONE;
}

bool reconcilePendingOnDemandRelay(long long wallNowMs)
{
	if (!companionSnapshotPathConfigured())
	{
		return false;
	}
	std::string relayDir;
	std::string requestPath;
	std::string statusPath;
	if (!resolveRelayDir(&relayDir) || !resolveRequestPath(&requestPath) || !resolveStatusPath(&statusPath))
	{
		return false;
	}
	std::string requestJson;
	if (!readTextFile(requestPath, &requestJson))
	{
		return false;
	}
	std::string pendingStatus;
	if (!CompanionJsonLite::extractJsonStringField(requestJson, "status", &pendingStatus)
	    || pendingStatus != "pending")
	{
		return false;
	}
	std::string requestId;
	std::string action;
	std::string briefingRequestId;
	CompanionJsonLite::extractJsonStringField(requestJson, "request_id", &requestId);
	CompanionJsonLite::extractJsonStringField(requestJson, "action", &action);
	CompanionJsonLite::extractJsonStringField(requestJson, "briefing_request_id", &briefingRequestId);
	if (requestId.empty() || action.empty())
	{
		return false;
	}

	if (action == "send" && !briefingRequestId.empty())
	{
		const std::string briefingPath = relayDir + "/" + BRIEFING_REQUEST_FILENAME;
		std::string briefingJson;
		if (readTextFile(briefingPath, &briefingJson))
		{
			std::string currentBriefingId;
			if (CompanionJsonLite::extractJsonStringField(briefingJson, "request_id", &currentBriefingId)
			    && !currentBriefingId.empty()
			    && currentBriefingId != briefingRequestId)
			{
				writeTerminalFailureStatus(statusPath, requestId, action.c_str(),
					"Briefing superseded; retry SEND.", wallNowMs);
				removePendingRequestFile(requestPath);
				return true;
			}
		}
	}

	int createdAtMsInt = 0;
	long long createdAtMs = 0;
	if (CompanionJsonLite::extractJsonIntField(requestJson, "created_at_ms", &createdAtMsInt))
	{
		createdAtMs = createdAtMsInt;
	}
	const std::string readyPath = relayDir + "/" + DAEMON_READY_FILENAME;
	if (!fileExists(readyPath)
	    && createdAtMs > 0
	    && wallNowMs >= createdAtMs
	    && wallNowMs - createdAtMs >= RELAY_OFFLINE_GRACE_MS)
	{
		writeTerminalFailureStatus(statusPath, requestId, action.c_str(),
			"Relay offline; start companion launcher.", wallNowMs);
		removePendingRequestFile(requestPath);
		return true;
	}
	return false;
}

} // namespace CompanionOnDemandRelayCore

} // namespace OpenXcom
