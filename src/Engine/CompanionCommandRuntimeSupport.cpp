/*
 * Open X-Com Companion — testable command polling policy (M1-21C).
 */
#include "CompanionCommandRuntimeSupport.h"
#include "CompanionCommandCore.h"
#include "CompanionJsonLite.h"
#include "CompanionSha256.h"
#include <sstream>

namespace OpenXcom
{

namespace CompanionCommandRuntimeSupport
{

namespace
{

bool extractJsonString(const std::string &json, const std::string &key, std::string *out)
{
	const std::string needle = "\"" + key + "\": \"";
	const std::string::size_type start = json.find(needle);
	if (start == std::string::npos)
	{
		return false;
	}
	const std::string::size_type valueStart = start + needle.size();
	const std::string::size_type valueEnd = json.find('"', valueStart);
	if (valueEnd == std::string::npos)
	{
		return false;
	}
	*out = json.substr(valueStart, valueEnd - valueStart);
	return true;
}

bool isTerminalResultStatus(const std::string &status)
{
	return status == "COMPLETED" || status == "INTERRUPTED" || status == "FAILED" || status == "REJECTED";
}

bool shouldPreserveExistingResultOnDuplicateConflict(const std::string &priorStatus, bool moveInFlight)
{
	if (isTerminalResultStatus(priorStatus))
	{
		return true;
	}
	return priorStatus == "EXECUTING" && !moveInFlight;
}

bool priorResultMatchesIncomingCommandId(
	const std::string &incomingCommandId,
	const std::string &priorResultCommandId)
{
	return !incomingCommandId.empty() && !priorResultCommandId.empty() &&
		incomingCommandId == priorResultCommandId;
}

} // namespace

bool computeInboxCommandDigestHex(const std::string &commandJson, std::string *outHex64)
{
	if (outHex64 == 0)
	{
		return false;
	}
	return CompanionSha256::digestHexLowerUtf8(commandJson, outHex64);
}

bool parseTacticalResultBinding(
	const std::string &resultJson,
	std::string *commandIdOut,
	std::string *statusOut,
	std::string *inboxSha256Out)
{
	if (commandIdOut != 0)
	{
		commandIdOut->clear();
	}
	if (statusOut != 0)
	{
		statusOut->clear();
	}
	if (inboxSha256Out != 0)
	{
		inboxSha256Out->clear();
	}
	if (resultJson.empty())
	{
		return false;
	}
	std::string commandId;
	std::string status;
	if (!extractJsonString(resultJson, "command_id", &commandId) ||
	    !extractJsonString(resultJson, "status", &status))
	{
		return false;
	}
	if (commandIdOut != 0)
	{
		*commandIdOut = commandId;
	}
	if (statusOut != 0)
	{
		*statusOut = status;
	}
	if (inboxSha256Out != 0)
	{
		std::string inboxSha256;
		if (extractJsonString(resultJson, "inbox_sha256", &inboxSha256))
		{
			*inboxSha256Out = inboxSha256;
		}
	}
	return true;
}

DuplicateInboxDecision evaluateDuplicateInboxObservation(
	bool duplicateCommandId,
	const std::string &incomingCommandId,
	const std::string &inboxCommandDigestHex,
	const std::string &existingResultJson,
	bool moveInFlight)
{
	if (!duplicateCommandId)
	{
		return DUPLICATE_INBOX_PROCEED;
	}
	if (incomingCommandId.empty())
	{
		return DUPLICATE_INBOX_IDENTITY_MISMATCH;
	}
	if (existingResultJson.empty())
	{
		return DUPLICATE_INBOX_REJECT;
	}
	std::string priorCommandId;
	std::string priorStatus;
	std::string priorInboxSha256;
	if (!parseTacticalResultBinding(existingResultJson, &priorCommandId, &priorStatus, &priorInboxSha256))
	{
		return DUPLICATE_INBOX_IDENTITY_MISMATCH;
	}
	if (!priorResultMatchesIncomingCommandId(incomingCommandId, priorCommandId))
	{
		return DUPLICATE_INBOX_IDENTITY_MISMATCH;
	}
	if (!priorInboxSha256.empty())
	{
		if (priorInboxSha256 == inboxCommandDigestHex)
		{
			return DUPLICATE_INBOX_NO_OP;
		}
		if (shouldPreserveExistingResultOnDuplicateConflict(priorStatus, moveInFlight))
		{
			return DUPLICATE_INBOX_CONFLICT_PRESERVE;
		}
		return DUPLICATE_INBOX_REJECT;
	}
	if (priorStatus == "EXECUTING" && !moveInFlight)
	{
		return DUPLICATE_INBOX_NO_OP;
	}
	if (isTerminalResultStatus(priorStatus))
	{
		// Legacy results without inbox_sha256: payload identity is unknown; preserve terminal and refuse re-exec.
		return DUPLICATE_INBOX_NO_OP;
	}
	return DUPLICATE_INBOX_REJECT;
}

bool shouldOverwriteTacticalResultOnDuplicateDecision(DuplicateInboxDecision decision)
{
	return decision == DUPLICATE_INBOX_REJECT;
}

bool shouldWriteConflictSidecarOnDuplicateDecision(DuplicateInboxDecision decision)
{
	return decision == DUPLICATE_INBOX_CONFLICT_PRESERVE || decision == DUPLICATE_INBOX_IDENTITY_MISMATCH;
}

std::string buildDuplicatePayloadConflictJson(
	const std::string &commandId,
	const std::string &priorResultStatus,
	const std::string &observedInboxDigestHex,
	const std::string &storedInboxDigestHex)
{
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"command_id\": \"" << commandId << "\",\n"
	     << "  \"reject_code\": \"DUPLICATE_COMMAND_ID\",\n"
	     << "  \"public_reason\": \"Command ID already used.\",\n"
	     << "  \"conflict\": \"inbox_payload_mismatch\",\n"
	     << "  \"prior_result_status\": \"" << priorResultStatus << "\",\n"
	     << "  \"observed_inbox_sha256\": \"" << observedInboxDigestHex << "\",\n";
	if (!storedInboxDigestHex.empty())
	{
		json << "  \"stored_inbox_sha256\": \"" << storedInboxDigestHex << "\",\n";
	}
	json << "  \"recorded_at\": \"engine-local\"\n"
	     << "}\n";
	return json.str();
}

std::string buildDuplicateResultIdentityConflictJson(
	const std::string &incomingCommandId,
	const std::string &conflictKind,
	const std::string &priorResultCommandId,
	const std::string &priorResultStatus,
	const std::string &observedInboxDigestHex)
{
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"command_id\": \"" << incomingCommandId << "\",\n"
	     << "  \"reject_code\": \"DUPLICATE_COMMAND_ID\",\n"
	     << "  \"public_reason\": \"Command ID already used.\",\n"
	     << "  \"conflict\": \"" << conflictKind << "\",\n";
	if (!priorResultCommandId.empty())
	{
		json << "  \"prior_result_command_id\": \"" << priorResultCommandId << "\",\n";
	}
	if (!priorResultStatus.empty())
	{
		json << "  \"prior_result_status\": \"" << priorResultStatus << "\",\n";
	}
	if (!observedInboxDigestHex.empty())
	{
		json << "  \"observed_inbox_sha256\": \"" << observedInboxDigestHex << "\",\n";
	}
	json << "  \"recorded_at\": \"engine-local\"\n"
	     << "}\n";
	return json.str();
}

bool shouldDeferInFlightCommandCompletionWhileReentrant(int commandTickDepth)
{
	return commandTickDepth > 1;
}

bool commandSessionBindingMatchesLiveBattle(
	const std::string &liveSessionId,
	const std::string &commandSessionId,
	const std::string &resultSessionId)
{
	if (liveSessionId.empty() || commandSessionId.empty())
	{
		return false;
	}
	if (commandSessionId != liveSessionId)
	{
		return false;
	}
	if (!resultSessionId.empty() && resultSessionId != liveSessionId)
	{
		return false;
	}
	return true;
}

bool shouldRehydrateOrphanedDirectedFireForSession(
	const std::string &liveSessionId,
	const std::string &commandSessionId,
	const std::string &resultSessionId)
{
	return commandSessionBindingMatchesLiveBattle(liveSessionId, commandSessionId, resultSessionId);
}

bool isOpenCompanionResultStatus(const std::string &status)
{
	return status == "EXECUTING" || status == "ACCEPTED";
}

bool isCrossSessionPersistedOpenCommand(
	const std::string &liveSessionId,
	const std::string &inboxSessionId,
	const std::string &resultSessionId,
	const std::string &resultStatus)
{
	if (liveSessionId.empty())
	{
		return false;
	}
	if (!isOpenCompanionResultStatus(resultStatus))
	{
		return false;
	}
	if (!inboxSessionId.empty())
	{
		return inboxSessionId != liveSessionId;
	}
	return !resultSessionId.empty() && resultSessionId != liveSessionId;
}

bool shouldPreserveSameSessionInFlightExecution(
	const std::string &liveSessionId,
	const std::string &activeCommandSessionId,
	const std::string &resultSessionId,
	const std::string &resultCommandId,
	bool activeInFlight,
	const std::string &activeCommandId)
{
	if (!activeInFlight || activeCommandId.empty() || activeCommandId != resultCommandId)
	{
		return false;
	}
	if (!liveSessionId.empty() && !activeCommandSessionId.empty() &&
	    activeCommandSessionId == liveSessionId)
	{
		return true;
	}
	return commandSessionBindingMatchesLiveBattle(liveSessionId, activeCommandSessionId, resultSessionId);
}

bool shouldRecoverPersistedCrossSessionOrphan(
	const std::string &liveSessionId,
	const std::string &inboxSessionId,
	const std::string &resultSessionId,
	const std::string &resultStatus,
	const std::string &inboxCommandId,
	const std::string &resultCommandId,
	bool activeInFlight,
	const std::string &activeCommandId,
	const std::string &activeCommandSessionId)
{
	if (!isCrossSessionPersistedOpenCommand(liveSessionId, inboxSessionId, resultSessionId, resultStatus))
	{
		return false;
	}
	if (!inboxCommandId.empty() && !resultCommandId.empty() && inboxCommandId != resultCommandId)
	{
		return false;
	}
	if (shouldPreserveSameSessionInFlightExecution(
		    liveSessionId,
		    activeCommandSessionId,
		    resultSessionId,
		    resultCommandId,
		    activeInFlight,
		    activeCommandId))
	{
		return false;
	}
	return true;
}

bool parseTacticalResultSessionBinding(
	const std::string &resultJson,
	std::string *commandIdOut,
	std::string *statusOut,
	std::string *sessionIdOut,
	std::string *inboxSha256Out)
{
	if (!parseTacticalResultBinding(resultJson, commandIdOut, statusOut, inboxSha256Out))
	{
		return false;
	}
	if (sessionIdOut != 0)
	{
		sessionIdOut->clear();
		std::string sessionId;
		if (extractJsonString(resultJson, "session_id", &sessionId))
		{
			*sessionIdOut = sessionId;
		}
	}
	return true;
}

const char *sessionBoundaryInterruptedPublicReasonForCommandType(const std::string &commandType)
{
	if (commandType == "SNAP_SHOT" || commandType == "AUTO_SHOT" || commandType == "AIMED_SHOT")
	{
		return "Battle session changed; in-flight directed fire cannot resume after reload.";
	}
	return "Battle session changed; in-flight command cannot resume after reload.";
}

bool parseSnapShotParsedFromOpenResultJson(
	const std::string &resultJson,
	CompanionCommandCore::SnapShotCommandParsed *out)
{
	if (out == 0 || resultJson.empty())
	{
		return false;
	}
	std::string status;
	std::string commandType;
	if (!CompanionJsonLite::extractJsonStringField(resultJson, "status", &status) ||
	    !isOpenCompanionResultStatus(status) ||
	    !CompanionJsonLite::extractJsonStringField(resultJson, "command_type", &commandType))
	{
		return false;
	}
	if (commandType != "SNAP_SHOT" && commandType != "AUTO_SHOT" && commandType != "AIMED_SHOT")
	{
		return false;
	}
	out->valid = false;
	out->commandType = commandType;
	out->targetX = 0;
	out->targetY = 0;
	out->targetZ = 0;
	out->activeHand.clear();
	out->stateVersion = 0;
	out->rejectCode.clear();
	if (!CompanionJsonLite::extractJsonStringField(resultJson, "command_id", &out->commandId) ||
	    !CompanionJsonLite::extractJsonStringField(resultJson, "session_id", &out->sessionId) ||
	    !CompanionJsonLite::extractJsonStringField(resultJson, "snapshot_sha256", &out->snapshotSha256))
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(resultJson, "actor_unit_id", &out->actorUnitId) ||
	    !CompanionJsonLite::extractJsonIntField(resultJson, "turn", &out->turn))
	{
		return false;
	}
	out->valid = !out->commandId.empty() && !out->sessionId.empty();
	return out->valid;
}

bool parseMoveParsedFromOpenResultJson(
	const std::string &resultJson,
	CompanionCommandCore::MoveCommandParsed *out)
{
	if (out == 0 || resultJson.empty())
	{
		return false;
	}
	std::string status;
	if (!CompanionJsonLite::extractJsonStringField(resultJson, "status", &status) ||
	    !isOpenCompanionResultStatus(status))
	{
		return false;
	}
	out->valid = false;
	out->commandType = "MOVE";
	out->destX = 0;
	out->destY = 0;
	out->destZ = 0;
	out->stateVersion = 0;
	out->rejectCode.clear();
	if (!CompanionJsonLite::extractJsonStringField(resultJson, "command_id", &out->commandId) ||
	    !CompanionJsonLite::extractJsonStringField(resultJson, "session_id", &out->sessionId) ||
	    !CompanionJsonLite::extractJsonStringField(resultJson, "snapshot_sha256", &out->snapshotSha256))
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(resultJson, "actor_unit_id", &out->actorUnitId) ||
	    !CompanionJsonLite::extractJsonIntField(resultJson, "turn", &out->turn))
	{
		return false;
	}
	out->valid = !out->commandId.empty() && !out->sessionId.empty();
	return out->valid;
}

bool parseKneelParsedFromOpenResultJson(
	const std::string &resultJson,
	CompanionCommandCore::KneelCommandParsed *out)
{
	if (out == 0 || resultJson.empty())
	{
		return false;
	}
	std::string status;
	std::string commandType;
	if (!CompanionJsonLite::extractJsonStringField(resultJson, "status", &status) ||
	    !isOpenCompanionResultStatus(status) ||
	    !CompanionJsonLite::extractJsonStringField(resultJson, "command_type", &commandType))
	{
		return false;
	}
	if (commandType != "KNEEL" && commandType != "STAND")
	{
		return false;
	}
	out->valid = false;
	out->commandType = commandType;
	out->stateVersion = 0;
	out->rejectCode.clear();
	if (!CompanionJsonLite::extractJsonStringField(resultJson, "command_id", &out->commandId) ||
	    !CompanionJsonLite::extractJsonStringField(resultJson, "session_id", &out->sessionId) ||
	    !CompanionJsonLite::extractJsonStringField(resultJson, "snapshot_sha256", &out->snapshotSha256))
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(resultJson, "actor_unit_id", &out->actorUnitId) ||
	    !CompanionJsonLite::extractJsonIntField(resultJson, "turn", &out->turn))
	{
		return false;
	}
	out->valid = !out->commandId.empty() && !out->sessionId.empty();
	return out->valid;
}

bool shouldRunCompanionCommandTickDuringThink(
	bool playerTurn,
	bool debugMode,
	bool unitsFalling,
	bool battleStatesEmpty,
	bool battleBusy,
	bool moveInFlight)
{
	if (moveInFlight)
	{
		return true;
	}
	if (!playerTurn || debugMode || unitsFalling)
	{
		return false;
	}
	if (!battleStatesEmpty || battleBusy)
	{
		return false;
	}
	return true;
}

const HandleStateCompanionStage *handleStateCompanionStageOrder(size_t *outCount)
{
	static const HandleStateCompanionStage order[] = {
		HANDLE_STATE_COMPANION_BRIEFING,
		HANDLE_STATE_COMPANION_COMMAND_TICK,
		HANDLE_STATE_COMPANION_IDLE_SNAPSHOT
	};
	if (outCount != 0)
	{
		*outCount = sizeof(order) / sizeof(order[0]);
	}
	return order;
}

bool exportKneeledSnapshotField(bool unitIsKneeled)
{
	return unitIsKneeled;
}

bool shouldExportSnapshotBeforeTerminalCommandResult(CompanionCommandCore::CommandLifecycleStatus status)
{
	switch (status)
	{
	case CompanionCommandCore::CMD_COMPLETED:
	case CompanionCommandCore::CMD_INTERRUPTED:
	case CompanionCommandCore::CMD_FAILED:
		return true;
	default:
		return false;
	}
}

bool shouldPollCommandInboxFile(
	bool moveInFlight,
	time_t fileMtime,
	time_t cachedMtime,
	long msSinceLastPoll,
	int minIntervalMs)
{
	if (moveInFlight)
	{
		return true;
	}
	if (fileMtime != cachedMtime)
	{
		return true;
	}
	return msSinceLastPoll >= minIntervalMs;
}

bool parseInboxCommandBinding(const std::string &commandJson, InboxCommandBinding *out)
{
	if (out == 0)
	{
		return false;
	}
	*out = InboxCommandBinding();
	if (commandJson.empty())
	{
		return false;
	}
	std::string commandType;
	if (!CompanionJsonLite::extractJsonStringField(commandJson, "command_type", &commandType) ||
	    (commandType != "MOVE" && commandType != "KNEEL" && commandType != "SNAP_SHOT" &&
	     commandType != "AUTO_SHOT" && commandType != "AIMED_SHOT" &&
	     commandType != "PRIME" && commandType != "THROW"))
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &out->commandId) ||
	    !CompanionJsonLite::extractJsonStringField(commandJson, "session_id", &out->sessionId) ||
	    !CompanionJsonLite::extractJsonStringField(commandJson, "snapshot_sha256", &out->snapshotSha256) ||
	    !CompanionJsonLite::extractJsonIntField(commandJson, "turn", &out->turn) ||
	    !CompanionJsonLite::extractJsonIntField(commandJson, "state_version", &out->stateVersion))
	{
		return false;
	}
	out->parsed = true;
	return true;
}

StaleInboxArchiveReason evaluateStaleInboxArchive(
	const InboxCommandBinding &inbox,
	const SnapshotExportBinding &currentExport,
	bool journalContainsCommandId,
	bool moveInFlight,
	const std::string &inFlightCommandId)
{
	if (!inbox.parsed)
	{
		return STALE_INBOX_KEEP;
	}
	if (moveInFlight && !inFlightCommandId.empty() && inFlightCommandId == inbox.commandId)
	{
		return STALE_INBOX_KEEP;
	}
	if (journalContainsCommandId)
	{
		return STALE_INBOX_JOURNAL_DUPLICATE;
	}
	if (currentExport.sessionId.empty() || currentExport.snapshotSha256.empty())
	{
		return STALE_INBOX_KEEP;
	}
	if (inbox.sessionId != currentExport.sessionId ||
	    inbox.snapshotSha256 != currentExport.snapshotSha256 ||
	    inbox.turn != currentExport.turn ||
	    inbox.stateVersion != currentExport.stateVersion)
	{
		return STALE_INBOX_BINDING_MISMATCH;
	}
	return STALE_INBOX_KEEP;
}

std::string buildArchivedInboxFileName(
	const std::string &reasonToken,
	const std::string &commandId,
	long long epochMillisUtc)
{
	std::string idPrefix = commandId.size() >= 8 ? commandId.substr(0, 8) : commandId;
	if (idPrefix.empty())
	{
		idPrefix = "unknown";
	}
	std::ostringstream name;
	name << "tactical-command." << reasonToken << "-" << epochMillisUtc << "-" << idPrefix << ".json";
	return name.str();
}

} // namespace CompanionCommandRuntimeSupport

} // namespace OpenXcom
