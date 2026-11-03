/*
 * Open X-Com Companion — testable command polling policy (M1-21C).
 */
#include "CompanionCommandRuntimeSupport.h"
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

} // namespace CompanionCommandRuntimeSupport

} // namespace OpenXcom
