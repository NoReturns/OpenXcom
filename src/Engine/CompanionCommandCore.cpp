/*
 * Open X-Com Companion — tactical MOVE command validation (M1-19).
 */
#include "CompanionCommandCore.h"
#include "CompanionJsonLite.h"
#include "CompanionSha256.h"
#include <cctype>

namespace OpenXcom
{

namespace CompanionCommandCore
{

namespace
{

bool extractDestinationCoords(const std::string &json, int *destX, int *destY, int *destZ)
{
	if (destX == 0 || destY == 0 || destZ == 0)
	{
		return false;
	}
	std::string::size_type valueStart = 0;
	if (!CompanionJsonLite::findJsonKeyValueStart(json, "destination", &valueStart))
	{
		return false;
	}
	if (valueStart >= json.size() || json[valueStart] != '{')
	{
		return false;
	}
	std::string::size_type pos = valueStart + 1;
	int depth = 1;
	while (pos < json.size() && depth > 0)
	{
		const char c = json[pos];
		if (c == '"')
		{
			++pos;
			while (pos < json.size())
			{
				if (json[pos] == '\\')
				{
					pos += 2;
					continue;
				}
				if (json[pos] == '"')
				{
					++pos;
					break;
				}
				++pos;
			}
			continue;
		}
		if (c == '{')
		{
			++depth;
		}
		else if (c == '}')
		{
			--depth;
		}
		++pos;
	}
	if (depth != 0)
	{
		return false;
	}
	const std::string destObject = json.substr(valueStart, pos - valueStart);
	return CompanionJsonLite::extractJsonIntField(destObject, "x", destX) &&
	       CompanionJsonLite::extractJsonIntField(destObject, "y", destY) &&
	       CompanionJsonLite::extractJsonIntField(destObject, "z", destZ);
}

} // namespace

bool isValidCommandIdFormat(const std::string &commandId)
{
	if (commandId.empty() || commandId.size() > 64)
	{
		return false;
	}
	if (!std::isalnum(static_cast<unsigned char>(commandId[0])))
	{
		return false;
	}
	for (std::string::size_type i = 0; i < commandId.size(); ++i)
	{
		const char c = commandId[i];
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
		{
			return false;
		}
	}
	return true;
}

bool isValidSessionIdFormat(const std::string &sessionId)
{
	if (sessionId.size() != 32)
	{
		return false;
	}
	for (std::string::size_type i = 0; i < sessionId.size(); ++i)
	{
		const char c = sessionId[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			return false;
		}
	}
	return true;
}

bool isValidSnapshotSha256Format(const std::string &sha)
{
	return CompanionSha256::isValidHexDigest(sha);
}

bool parseMoveCommandJson(const std::string &json, MoveCommandParsed *out)
{
	if (out == 0)
	{
		return false;
	}
	MoveCommandParsed parsed = {};
	parsed.valid = false;

	int schemaVersion = 0;
	if (!CompanionJsonLite::extractJsonIntField(json, "schema_version", &schemaVersion) || schemaVersion != 1)
	{
		parsed.rejectCode = "UNSUPPORTED_SCHEMA";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "command_id", &parsed.commandId) ||
	    !isValidCommandIdFormat(parsed.commandId))
	{
		parsed.rejectCode = "INVALID_COMMAND_ID";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "command_type", &parsed.commandType) ||
	    parsed.commandType != "MOVE")
	{
		parsed.rejectCode = "UNSUPPORTED_COMMAND_TYPE";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(json, "actor_unit_id", &parsed.actorUnitId) ||
	    parsed.actorUnitId <= 0)
	{
		parsed.rejectCode = "INVALID_ACTOR";
		*out = parsed;
		return false;
	}
	if (!extractDestinationCoords(json, &parsed.destX, &parsed.destY, &parsed.destZ))
	{
		parsed.rejectCode = "INVALID_DESTINATION";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "snapshot_sha256", &parsed.snapshotSha256) ||
	    !isValidSnapshotSha256Format(parsed.snapshotSha256))
	{
		parsed.rejectCode = "INVALID_SNAPSHOT_SHA256";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "session_id", &parsed.sessionId) ||
	    !isValidSessionIdFormat(parsed.sessionId))
	{
		parsed.rejectCode = "INVALID_SESSION_ID";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(json, "turn", &parsed.turn) || parsed.turn < 1)
	{
		parsed.rejectCode = "INVALID_TURN";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(json, "state_version", &parsed.stateVersion) ||
	    parsed.stateVersion < 0)
	{
		parsed.rejectCode = "INVALID_STATE_VERSION";
		*out = parsed;
		return false;
	}

	parsed.valid = true;
	*out = parsed;
	return true;
}

MoveValidationResult validateMoveCommand(const MoveValidationInput &input)
{
	MoveValidationResult result = {};
	result.accepted = false;

	if (!input.executorEnabled)
	{
		result.rejectCode = "EXECUTOR_DISABLED";
		result.publicReason = "Companion command executor is disabled.";
		return result;
	}
	if (!input.battleInitialized)
	{
		result.rejectCode = "BATTLE_NOT_READY";
		result.publicReason = "Battle is not initialized.";
		return result;
	}
	if (input.debugMode)
	{
		result.rejectCode = "DEBUG_MODE";
		result.publicReason = "Commands blocked in debug mode.";
		return result;
	}
	if (!input.playerTurn)
	{
		result.rejectCode = "WRONG_TURN";
		result.publicReason = "Not player turn.";
		return result;
	}
	if (input.unitsFalling)
	{
		result.rejectCode = "UNITS_FALLING";
		result.publicReason = "Units are falling.";
		return result;
	}
	if (input.priorCommandNonTerminal)
	{
		result.rejectCode = "PRIOR_COMMAND_ACTIVE";
		result.publicReason = "Previous command has not reached a terminal state.";
		return result;
	}
	if (input.duplicateCommandId)
	{
		result.rejectCode = "DUPLICATE_COMMAND_ID";
		result.publicReason = "Command ID already used.";
		return result;
	}
	if (input.humanOverrideRevoked)
	{
		result.rejectCode = "HUMAN_OVERRIDE";
		result.publicReason = "Soldier control returned to human.";
		return result;
	}
	if (input.sessionId.empty() || input.commandSnapshotSha256.empty())
	{
		result.rejectCode = "INVALID_BINDING";
		result.publicReason = "Missing session or snapshot binding.";
		return result;
	}
	if (input.commandSnapshotSha256 != input.lastSnapshotSha256)
	{
		result.rejectCode = "STALE_SNAPSHOT";
		result.publicReason = "Snapshot SHA-256 does not match current export.";
		return result;
	}
	if (input.commandSessionId != input.sessionId)
	{
		result.rejectCode = "STALE_SESSION";
		result.publicReason = "Session ID does not match current battle.";
		return result;
	}
	if (input.commandTurn != input.turn)
	{
		result.rejectCode = "STALE_TURN";
		result.publicReason = "Turn does not match current battle.";
		return result;
	}
	if (input.lastExportedStateVersion < 0)
	{
		result.rejectCode = "INVALID_BINDING";
		result.publicReason = "No exported state version binding.";
		return result;
	}
	if (input.commandStateVersion != input.lastExportedStateVersion)
	{
		result.rejectCode = "STALE_STATE_VERSION";
		result.publicReason = "State version does not match last tactical export.";
		return result;
	}
	if (input.turn <= 0)
	{
		result.rejectCode = "INVALID_TURN";
		result.publicReason = "Invalid turn.";
		return result;
	}
	if (!input.actorIsPlayerSoldier || !input.actorAlive)
	{
		result.rejectCode = "INVALID_ACTOR";
		result.publicReason = "Actor is not an alive player soldier.";
		return result;
	}
	if (!input.actorAiControlled)
	{
		result.rejectCode = "NOT_AI_CONTROLLED";
		result.publicReason = "Soldier is human-controlled.";
		return result;
	}
	if (input.destX < 0 || input.destY < 0 || input.destZ < 0 ||
	    input.destX >= input.mapSizeX || input.destY >= input.mapSizeY || input.destZ >= input.mapSizeZ)
	{
		result.rejectCode = "INVALID_DESTINATION";
		result.publicReason = "Destination out of map bounds.";
		return result;
	}
	if (input.battleBusy)
	{
		result.rejectCode = "BATTLE_BUSY";
		result.publicReason = "Battle is busy with animation or actions.";
		return result;
	}

	result.accepted = true;
	return result;
}

CommandLifecycleStatus evaluateWalkTerminalStatus(const WalkCompletionInput &input)
{
	if (!input.actorAlive)
	{
		return CMD_FAILED;
	}
	if (!input.actorStillAiControlled)
	{
		return CMD_INTERRUPTED;
	}
	if (input.battleBusy)
	{
		return CMD_EXECUTING;
	}
	if (!input.actionResult.empty())
	{
		return CMD_INTERRUPTED;
	}
	if (input.currentPos == input.requestedDest)
	{
		return CMD_COMPLETED;
	}
	if (input.currentPos != input.startPos)
	{
		return CMD_INTERRUPTED;
	}
	return CMD_FAILED;
}

const char *lifecycleStatusToString(CommandLifecycleStatus status)
{
	switch (status)
	{
	case CMD_REJECTED: return "REJECTED";
	case CMD_ACCEPTED: return "ACCEPTED";
	case CMD_EXECUTING: return "EXECUTING";
	case CMD_COMPLETED: return "COMPLETED";
	case CMD_INTERRUPTED: return "INTERRUPTED";
	case CMD_FAILED: return "FAILED";
	default: return "FAILED";
	}
}

} // namespace CompanionCommandCore

} // namespace OpenXcom
