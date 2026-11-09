/*
 * Open X-Com Companion — on-demand send/retrieve relay queue (M1-25E).
 */
#include "CompanionOnDemandRelayCore.h"
#include "CompanionAutoRetrieveCore.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionStrategicSendEnqueueSupport.h"
#include "CompanionJsonLite.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
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
const char *DAEMON_HEALTH_FILENAME = ".companion-on-demand-daemon-health.json";
const char *TRANSACTIONS_FILENAME = "companion-on-demand-relay-transactions.jsonl";
const long long RELAY_OFFLINE_GRACE_MS = 2500;
const long long DAEMON_HEALTH_STALE_MS = 4000;

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

void appendRelayButtonEnqueueDiagnostic(const std::string &relayDir, const char *action, bool accepted,
	const char *rejectCode, const std::string *requestId)
{
	if (relayDir.empty() || action == 0)
	{
		return;
	}
	const long long atMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	std::ostringstream json;
	json << "{\"kind\":\"relay_button_enqueue\",\"at_ms\":" << atMs
	     << ",\"action\":\"" << action << "\""
	     << ",\"accepted\":" << (accepted ? "true" : "false");
	if (rejectCode != 0 && rejectCode[0] != '\0')
	{
		json << ",\"reject_code\":\"" << rejectCode << "\"";
	}
	if (requestId != 0 && !requestId->empty())
	{
		json << ",\"request_id\":\"" << *requestId << "\"";
	}
	json << "}\n";
	const std::string path = relayDir + "/" + TRANSACTIONS_FILENAME;
	std::ofstream out(path.c_str(), std::ios::out | std::ios::app | std::ios::binary);
	if (out)
	{
		out << json.str();
	}
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
	const char *action, const char *message, long long updatedAtMs, const char *errorCode)
{
	std::ostringstream ss;
	ss << "{\n"
	   << "  \"request_id\": \"" << requestId << "\",\n"
	   << "  \"action\": \"" << action << "\",\n"
	   << "  \"phase\": \"failed\",\n"
	   << "  \"message\": \"" << message << "\",\n"
	   << "  \"updated_at_ms\": " << updatedAtMs << ",\n";
	if (errorCode != 0 && errorCode[0] != '\0')
	{
		ss << "  \"error_code\": \"" << errorCode << "\",\n";
	}
	ss << "  \"terminal\": true\n"
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

bool requestFileHasPendingWork(const std::string &requestPath)
{
	std::string requestJson;
	if (!readTextFile(requestPath, &requestJson))
	{
		return true;
	}
	std::string status;
	if (!CompanionJsonLite::extractJsonStringField(requestJson, "status", &status))
	{
		return true;
	}
	return status == "pending";
}

bool readDaemonHealthFreshness(const std::string &relayDir, long long wallNowMs, long long *outUpdatedMs, std::string *outState)
{
	const std::string healthPath = relayDir + "/" + DAEMON_HEALTH_FILENAME;
	std::string healthJson;
	if (!readTextFile(healthPath, &healthJson))
	{
		return false;
	}
	long long updatedMs = 0;
	if (!CompanionJsonLite::extractJsonInt64Field(healthJson, "updated_at_ms", &updatedMs))
	{
		return false;
	}
	if (outUpdatedMs != 0)
	{
		*outUpdatedMs = updatedMs;
	}
	if (outState != 0)
	{
		outState->clear();
		CompanionJsonLite::extractJsonStringField(healthJson, "state", outState);
	}
	if (wallNowMs < updatedMs)
	{
		return false;
	}
	if (wallNowMs - updatedMs > DAEMON_HEALTH_STALE_MS)
	{
		return false;
	}
	if (outState != 0)
	{
		if (*outState != "ready" && *outState != "busy")
		{
			return false;
		}
	}
	return true;
}

bool isDaemonLocallyServing(const std::string &relayDir, long long wallNowMs)
{
	std::string state;
	long long updatedMs = 0;
	return readDaemonHealthFreshness(relayDir, wallNowMs, &updatedMs, &state);
}

#ifdef _WIN32
bool tryKickEnsureOnDemandDaemonHelper()
{
	const char *script = std::getenv("OPENXCOM_COMPANION_ENSURE_DAEMON_SCRIPT");
	if (script == 0 || script[0] == '\0')
	{
		return false;
	}
	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	if (snapshotPath == 0 || snapshotPath[0] == '\0')
	{
		return false;
	}
	const char *nodeExe = std::getenv("OPENXCOM_COMPANION_NODE_EXE");
	const std::string node = (nodeExe != 0 && nodeExe[0] != '\0') ? std::string(nodeExe) : std::string("node");
	std::string cmd = "\"" + node + "\" \"" + std::string(script) + "\" \"" + std::string(snapshotPath) + "\"";
	std::vector<char> cmdLine(cmd.begin(), cmd.end());
	cmdLine.push_back('\0');
	STARTUPINFOA si = {};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessA(0, cmdLine.data(), 0, 0, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, 0, 0, &si, &pi))
	{
		return false;
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return true;
}
#else
bool tryKickEnsureOnDemandDaemonHelper()
{
	return false;
}
#endif

} // namespace

RelayStatusView::RelayStatusView() : terminal(false), parsed(false), updated_at_ms(0)
{
}

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

bool parseRelayStatusJson(const std::string &statusJson, RelayStatusView *out)
{
	if (out == 0)
	{
		return false;
	}
	*out = RelayStatusView();
	if (statusJson.empty())
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(statusJson, "phase", &out->phase))
	{
		return false;
	}
	out->parsed = true;
	CompanionJsonLite::extractJsonStringField(statusJson, "request_id", &out->request_id);
	CompanionJsonLite::extractJsonStringField(statusJson, "action", &out->action);
	CompanionJsonLite::extractJsonStringField(statusJson, "outcome", &out->outcome);
	CompanionJsonLite::extractJsonStringField(statusJson, "error_code", &out->error_code);
	CompanionJsonLite::extractJsonStringField(statusJson, "last_command_id", &out->last_command_id);
	CompanionJsonLite::extractJsonStringField(statusJson, "last_command_status", &out->last_command_status);
	CompanionJsonLite::extractJsonStringField(statusJson, "retrieve_outcome", &out->retrieve_outcome);
	CompanionJsonLite::extractJsonInt64Field(statusJson, "updated_at_ms", &out->updated_at_ms);
	int terminalInt = 0;
	if (CompanionJsonLite::extractJsonIntField(statusJson, "terminal", &terminalInt))
	{
		out->terminal = terminalInt != 0;
	}
	else if (statusJson.find("\"terminal\": true") != std::string::npos
	    || statusJson.find("\"terminal\":true") != std::string::npos)
	{
		out->terminal = true;
	}
	return true;
}

bool isTerminalRetrievePhase(const std::string &phase)
{
	return phase == "completed" || phase == "failed";
}

bool isStickyTerminalRetrieveOutcome(const RelayStatusView &existing)
{
	if (existing.action != "retrieve" || !existing.terminal)
	{
		return false;
	}
	if (!existing.last_command_id.empty() && !existing.last_command_status.empty())
	{
		return true;
	}
	return isTerminalRetrievePhase(existing.phase);
}

bool incomingRetrievePollWouldEraseTerminalOutcome(const RelayStatusView &existing, const RelayStatusView &incoming)
{
	if (incoming.action != "retrieve")
	{
		return false;
	}
	if (!isStickyTerminalRetrieveOutcome(existing))
	{
		return false;
	}
	if (incoming.phase == "no_new_orders")
	{
		return true;
	}
	if (incoming.phase == "retrieving" && !incoming.terminal)
	{
		return true;
	}
	return false;
}

bool isEngineRelayOfflineFailure(const RelayStatusView &incoming)
{
	return incoming.action == "retrieve"
	    && incoming.phase == "failed"
	    && incoming.last_command_id.empty()
	    && (incoming.error_code.empty() || incoming.error_code == "RELAY_OFFLINE");
}

bool incomingRelayOfflineFailureWouldEraseStickyCompleted(const RelayStatusView &existing,
	const RelayStatusView &incoming)
{
	if (existing.action != "retrieve" || existing.phase != "completed" || !existing.terminal)
	{
		return false;
	}
	if (!isStickyTerminalRetrieveOutcome(existing))
	{
		return false;
	}
	if (!isEngineRelayOfflineFailure(incoming))
	{
		return false;
	}
	return incoming.request_id != existing.request_id;
}

bool shouldAcceptRelayStatusUpdate(const RelayStatusView &existing, const RelayStatusView &incoming, bool hasExisting)
{
	if (!incoming.parsed)
	{
		return false;
	}
	if (!hasExisting || !existing.parsed)
	{
		return true;
	}
	if (incomingRetrievePollWouldEraseTerminalOutcome(existing, incoming))
	{
		return false;
	}
	if (incomingRelayOfflineFailureWouldEraseStickyCompleted(existing, incoming))
	{
		return false;
	}
	if (!incoming.request_id.empty() && !existing.request_id.empty()
	    && incoming.request_id == existing.request_id)
	{
		return true;
	}
	if (!incoming.request_id.empty() && !existing.request_id.empty()
	    && incoming.request_id != existing.request_id)
	{
		if (incoming.updated_at_ms >= existing.updated_at_ms)
		{
			return true;
		}
		if (existing.terminal)
		{
			return false;
		}
		return incoming.updated_at_ms >= existing.updated_at_ms;
	}
	if (existing.terminal && incoming.updated_at_ms < existing.updated_at_ms)
	{
		return false;
	}
	if (existing.terminal && incoming.terminal == false && incoming.request_id == existing.request_id)
	{
		return true;
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
	if (phase == "manual_upload_required")
	{
		return RELAY_UI_MANUAL_UPLOAD_REQUIRED;
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
	return RELAY_UI_UNKNOWN;
}

RelayUiPhase mapStatusViewToUiPhase(const RelayStatusView &status)
{
	if (!status.parsed)
	{
		return RELAY_UI_IDLE;
	}
	if (status.outcome == "deferred"
	    || (status.phase == "sending" && status.error_code == "RATE_LIMIT" && !status.terminal))
	{
		return RELAY_UI_DEFERRED;
	}
	const RelayUiPhase mapped = mapPhaseString(status.phase, status.action);
	if (mapped == RELAY_UI_UNKNOWN)
	{
		return RELAY_UI_UNKNOWN;
	}
	return mapped;
}

bool isRelayBusyPhase(RelayUiPhase phase)
{
	return phase == RELAY_UI_SENDING || phase == RELAY_UI_RETRIEVING;
}

bool suppressSendPublicationHighlight(RelayUiPhase phase)
{
	return isRelayBusyPhase(phase) || phase == RELAY_UI_DEFERRED;
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
	if (!CompanionAutoRetrieveCore::isBattlescapeActive())
	{
		if (outError != 0)
		{
			*outError = "retrieve_not_available";
		}
		return false;
	}
	return tryEnqueueSend("retrieve", 0, outRequestId, outError);
}

bool reportTerminalSendFailure(const char *message, const char *errorCode)
{
	if (message == 0 || message[0] == '\0')
	{
		return false;
	}
	std::string statusPath;
	if (!resolveStatusPath(&statusPath))
	{
		return false;
	}
	const std::string requestId = CompanionBriefingTestSupport::makeRequestId();
	const long long nowMs = companionWallClockMs();
	return writeTerminalFailureStatus(statusPath, requestId, "send", message, nowMs, errorCode);
}

bool tryEnqueueSend(const char *sendScope, const char *briefingRequestId,
	std::string *outRequestId, std::string *outError,
	const std::string *predeterminedRequestId,
	const StrategicSendEnqueueContext *strategicEnqueue)
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
		if (!CompanionAutoRetrieveCore::isBattlescapeActive())
		{
			if (outError != 0)
			{
				*outError = "retrieve_not_available";
			}
			return false;
		}
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
	if (predeterminedRequestId != 0 && !predeterminedRequestId->empty() && scope != "strategic")
	{
		if (outError != 0)
		{
			*outError = "predetermined_request_id_not_allowed";
		}
		return false;
	}
	if (strategicEnqueue != 0 && scope != "strategic")
	{
		if (outError != 0)
		{
			*outError = "strategic_enqueue_not_allowed";
		}
		return false;
	}
	if (scope == "strategic")
	{
		const bool hasLive = strategicEnqueue != 0 && strategicEnqueue->save != 0 && strategicEnqueue->mod != 0;
		const bool hasFixture = strategicEnqueue != 0 && strategicEnqueue->fixture_marker_prep != 0;
		if (!hasLive && !hasFixture)
		{
			if (outError != 0)
			{
				*outError = "strategic_enqueue_required";
			}
			return false;
		}
	}
	if (scope == "tactical" && (briefingRequestId == 0 || briefingRequestId[0] == '\0'))
	{
		if (outError != 0)
		{
			*outError = "missing_briefing_request_id";
		}
		std::string relayDir;
		if (resolveRelayDir(&relayDir))
		{
			appendRelayButtonEnqueueDiagnostic(relayDir, action, false, "missing_briefing_request_id", 0);
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
		std::string relayDir;
		if (resolveRelayDir(&relayDir))
		{
			appendRelayButtonEnqueueDiagnostic(relayDir, action, false, "path_resolution_failed", 0);
		}
		return false;
	}

	if (fileExists(requestPath) && requestFileHasPendingWork(requestPath))
	{
		if (outError != 0)
		{
			*outError = "request_in_flight";
		}
		std::string relayDir;
		if (resolveRelayDir(&relayDir))
		{
			appendRelayButtonEnqueueDiagnostic(relayDir, action, false, "request_in_flight", 0);
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
		RelayStatusView statusView;
		if (parseRelayStatusJson(statusJson, &statusView))
		{
			const RelayUiPhase uiPhase = mapStatusViewToUiPhase(statusView);
			if (isRelayBusyPhase(uiPhase) || uiPhase == RELAY_UI_DEFERRED || !terminal)
			{
				if (outError != 0)
				{
					*outError = "relay_busy";
				}
				std::string relayDir;
				if (resolveRelayDir(&relayDir))
				{
					appendRelayButtonEnqueueDiagnostic(relayDir, action, false, "relay_busy", 0);
				}
				return false;
			}
		}
		const RelayUiPhase uiPhase = mapPhaseString(phase, statusAction);
		if (isRelayBusyPhase(uiPhase) || !terminal)
		{
			if (outError != 0)
			{
				*outError = "relay_busy";
			}
			std::string relayDir;
			if (resolveRelayDir(&relayDir))
			{
				appendRelayButtonEnqueueDiagnostic(relayDir, action, false, "relay_busy", 0);
			}
			return false;
		}
	}

	std::string requestId;
	if (predeterminedRequestId != 0 && !predeterminedRequestId->empty())
	{
		if (!CompanionBriefingTestSupport::isValidRequestIdFormat(*predeterminedRequestId))
		{
			if (outError != 0)
			{
				*outError = "invalid_request_id";
			}
			return false;
		}
		requestId = *predeterminedRequestId;
	}
	else
	{
		requestId = CompanionBriefingTestSupport::makeRequestId();
	}
	const long long nowMs = companionWallClockMs();
	bool wroteStrategicMarker = false;
	if (scope == "strategic")
	{
		StrategicSendMarkerPrep markerPrep = {};
		if (strategicEnqueue->save != 0 && strategicEnqueue->mod != 0)
		{
			std::string exportError;
			if (!CompanionStrategicSendEnqueueSupport::buildStrategicMarkerPrepFromLiveExport(
				strategicEnqueue->save, strategicEnqueue->mod, &markerPrep, &exportError))
			{
				if (outError != 0)
				{
					*outError = exportError.empty() ? "strategic_export_failed" : exportError;
				}
				return false;
			}
		}
		else
		{
			markerPrep = *strategicEnqueue->fixture_marker_prep;
		}
		std::string relayDir;
		if (!resolveRelayDir(&relayDir))
		{
			if (outError != 0)
			{
				*outError = "path_resolution_failed";
			}
			return false;
		}
		if (!CompanionBriefingTestSupport::writeStrategicSendMarkerFile(
			relayDir,
			requestId,
			markerPrep.content_sha256,
			markerPrep.campaign_id,
			markerPrep.exported_at_ms))
		{
			if (outError != 0)
			{
				*outError = "strategic_marker_write_failed";
			}
			return false;
		}
		wroteStrategicMarker = true;
	}
	const char *scopeForJson = (scope == "retrieve") ? 0 : scope.c_str();
	if (!writeTextAtomically(requestPath, buildRequestJson(
		requestId, action, nowMs, scopeForJson, briefingRequestId)))
	{
		if (outError != 0)
		{
			*outError = "request_write_failed";
		}
		if (wroteStrategicMarker)
		{
			std::string relayDir;
			if (resolveRelayDir(&relayDir))
			{
				CompanionBriefingTestSupport::removeStrategicSendMarkerFile(relayDir);
			}
		}
		std::string relayDir;
		if (resolveRelayDir(&relayDir))
		{
			appendRelayButtonEnqueueDiagnostic(relayDir, action, false, "request_write_failed", 0);
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
	std::string relayDir;
	if (resolveRelayDir(&relayDir))
	{
		appendRelayButtonEnqueueDiagnostic(relayDir, action, true, 0, &requestId);
	}
	tryKickEnsureOnDemandDaemonHelper();
	return true;
}

namespace
{

bool isValidationErrorCode(const std::string &code)
{
	if (code == "SNAPSHOT_VALIDATION" || code == "SNAPSHOT_ERROR" || code == "SNAPSHOT_HASH_MISMATCH"
	    || code == "SNAPSHOT_SCHEMA" || code == "SNAPSHOT_SESSION" || code == "TURN_MISMATCH"
	    || code == "SESSION_MISMATCH")
	{
		return true;
	}
	return false;
}

bool isAuthErrorCode(const std::string &code)
{
	return code == "AUTH_FAILED" || code == "AUTH" || code == "gh_not_authenticated";
}

bool tryFormatTerminalCommandOrderLabel(const std::string &lastCommandStatus, std::string *out)
{
	if (out == 0 || lastCommandStatus.empty())
	{
		return false;
	}
	if (lastCommandStatus == "COMPLETED")
	{
		*out = "Order completed";
		return true;
	}
	if (lastCommandStatus == "INTERRUPTED")
	{
		*out = "Order interrupted";
		return true;
	}
	if (lastCommandStatus == "FAILED")
	{
		*out = "Order failed";
		return true;
	}
	if (lastCommandStatus == "REJECTED")
	{
		*out = "Order rejected";
		return true;
	}
	return false;
}

bool tryFormatRetrieveIncompleteCommandLabel(const std::string &lastCommandStatus, std::string *out)
{
	if (out == 0 || lastCommandStatus.empty())
	{
		return false;
	}
	if (lastCommandStatus == "COMPLETED")
	{
		*out = "Order done; relay incomplete";
		return true;
	}
	if (lastCommandStatus == "INTERRUPTED")
	{
		*out = "Interrupted; relay incomplete";
		return true;
	}
	if (lastCommandStatus == "FAILED")
	{
		*out = "Order failed; relay incomplete";
		return true;
	}
	if (lastCommandStatus == "REJECTED")
	{
		*out = "Rejected; relay incomplete";
		return true;
	}
	return false;
}

} // namespace

void formatRelayStatusLabel(const RelayStatusView &status, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	out->clear();
	if (!status.parsed)
	{
		return;
	}
	if (status.outcome == "deferred"
	    || (status.phase == "sending" && status.error_code == "RATE_LIMIT" && !status.terminal))
	{
		*out = "Deferred: RATE LIMIT";
		return;
	}
	if (status.phase == "manual_upload_required" || status.outcome == "manual_upload_required")
	{
		*out = "Manual upload required";
		return;
	}
	if (status.phase == "not_ready")
	{
		*out = "Not ready";
		return;
	}
	if (status.phase == "sending")
	{
		*out = "Sending";
		return;
	}
	if (status.phase == "retrieving")
	{
		*out = "Retrieving";
		return;
	}
	if (status.phase == "no_new_orders")
	{
		*out = "No new orders";
		return;
	}
	if (status.phase == "sent")
	{
		if (status.outcome == "skipped_unchanged")
		{
			*out = "Unchanged";
		}
		else
		{
			*out = "Published";
		}
		return;
	}
	if (status.phase == "completed")
	{
		if (status.action == "retrieve")
		{
			if (tryFormatTerminalCommandOrderLabel(status.last_command_status, out))
			{
				return;
			}
			*out = "Relay complete";
		}
		else
		{
			*out = "Published";
		}
		return;
	}
	if (status.phase == "failed")
	{
		if (status.action == "retrieve")
		{
			if (status.error_code == "RETRIEVAL_ERROR"
			    || status.error_code == "NETWORK_FAILED"
			    || status.error_code == "RATE_LIMIT"
			    || status.error_code == "RELAY_OFFLINE")
			{
				*out = "Retrieve error";
				return;
			}
			if (status.error_code == "RETRIEVE_INCOMPLETE")
			{
				if (tryFormatRetrieveIncompleteCommandLabel(status.last_command_status, out))
				{
					return;
				}
				*out = "Retrieve incomplete";
				return;
			}
			if (status.last_command_status == "COMPLETED")
			{
				*out = "Order done; relay incomplete";
				return;
			}
			if (tryFormatTerminalCommandOrderLabel(status.last_command_status, out))
			{
				return;
			}
			*out = "Retrieve failed";
			return;
		}
		if (isValidationErrorCode(status.error_code))
		{
			*out = "Failed: VALIDATION";
		}
		else if (isAuthErrorCode(status.error_code))
		{
			*out = "Failed: AUTH";
		}
		else
		{
			*out = "Failed";
		}
		return;
	}
	*out = "Status update";
}

void formatStatusLabel(RelayUiPhase phase, std::string *out)
{
	if (out == 0)
	{
		return;
	}
	RelayStatusView view;
	view.parsed = true;
	switch (phase)
	{
	case RELAY_UI_SENDING:
		view.phase = "sending";
		break;
	case RELAY_UI_SENT:
		view.phase = "sent";
		break;
	case RELAY_UI_SEND_FAILED:
		view.phase = "failed";
		view.action = "send";
		break;
	case RELAY_UI_NOT_READY:
		view.phase = "not_ready";
		break;
	case RELAY_UI_RETRIEVING:
		view.phase = "retrieving";
		break;
	case RELAY_UI_COMPLETED:
		view.phase = "completed";
		view.action = "retrieve";
		break;
	case RELAY_UI_NO_NEW_ORDERS:
		view.phase = "no_new_orders";
		break;
	case RELAY_UI_RETRIEVE_FAILED:
		view.phase = "failed";
		view.action = "retrieve";
		break;
	case RELAY_UI_MANUAL_UPLOAD_REQUIRED:
		view.phase = "manual_upload_required";
		break;
	case RELAY_UI_DEFERRED:
		view.phase = "sending";
		view.outcome = "deferred";
		view.error_code = "RATE_LIMIT";
		break;
	case RELAY_UI_UNKNOWN:
		view.phase = "unexpected_phase";
		break;
	default:
		out->clear();
		return;
	}
	formatRelayStatusLabel(view, out);
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
					"Briefing superseded; retry SEND.", wallNowMs, 0);
				removePendingRequestFile(requestPath);
				return true;
			}
		}
	}

	long long createdAtMs = 0;
	CompanionJsonLite::extractJsonInt64Field(requestJson, "created_at_ms", &createdAtMs);
	const std::string readyPath = relayDir + "/" + DAEMON_READY_FILENAME;
	const bool withinStartupGrace = createdAtMs > 0
	    && wallNowMs >= createdAtMs
	    && wallNowMs - createdAtMs < RELAY_OFFLINE_GRACE_MS;
	if (withinStartupGrace)
	{
		return false;
	}
	if (!isDaemonLocallyServing(relayDir, wallNowMs))
	{
		std::string statusJson;
		if (action == "retrieve" && readTextFile(statusPath, &statusJson))
		{
			RelayStatusView current;
			if (parseRelayStatusJson(statusJson, &current)
			    && current.action == "retrieve"
			    && current.terminal
			    && current.phase == "completed"
			    && current.request_id == requestId)
			{
				removePendingRequestFile(requestPath);
				return true;
			}
		}
		writeTerminalFailureStatus(statusPath, requestId, action.c_str(),
			"Relay offline; start companion launcher.", wallNowMs, "RELAY_OFFLINE");
		removePendingRequestFile(requestPath);
		return true;
	}
	return false;
}

} // namespace CompanionOnDemandRelayCore

} // namespace OpenXcom
