/*
 * Open X-Com Companion — tactical MOVE command validation (M1-19).
 */
#include "CompanionCommandCore.h"
#include "CompanionWalkStopTokens.h"
#include "CompanionJsonLite.h"
#include "CompanionSha256.h"
#include "../Mod/RuleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Mod/RuleInventory.h"
#include <cctype>

namespace OpenXcom
{

namespace CompanionCommandCore
{

namespace
{

bool extractCoordObject(const std::string &json, const char *objectKey, int *outX, int *outY, int *outZ)
{
	if (objectKey == 0 || outX == 0 || outY == 0 || outZ == 0)
	{
		return false;
	}
	std::string::size_type valueStart = 0;
	if (!CompanionJsonLite::findJsonKeyValueStart(json, objectKey, &valueStart))
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
	const std::string coordObject = json.substr(valueStart, pos - valueStart);
	return CompanionJsonLite::extractJsonIntField(coordObject, "x", outX) &&
	       CompanionJsonLite::extractJsonIntField(coordObject, "y", outY) &&
	       CompanionJsonLite::extractJsonIntField(coordObject, "z", outZ);
}

bool extractDestinationCoords(const std::string &json, int *destX, int *destY, int *destZ)
{
	return extractCoordObject(json, "destination", destX, destY, destZ);
}

bool extractTargetCoords(const std::string &json, int *targetX, int *targetY, int *targetZ)
{
	return extractCoordObject(json, "target", targetX, targetY, targetZ);
}

bool extractGroundPositionCoords(const std::string &json, int *groundX, int *groundY, int *groundZ)
{
	return extractCoordObject(json, "ground_position", groundX, groundY, groundZ);
}

bool isValidActiveHandSlot(const std::string &hand)
{
	return hand == "STR_LEFT_HAND" || hand == "STR_RIGHT_HAND";
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

bool parseKneelCommandJson(const std::string &json, KneelCommandParsed *out)
{
	if (out == 0)
	{
		return false;
	}
	KneelCommandParsed parsed = {};
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
	    parsed.commandType != "KNEEL")
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

bool parseStandCommandJson(const std::string &json, StandCommandParsed *out)
{
	if (out == 0)
	{
		return false;
	}
	StandCommandParsed parsed = {};
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
	    parsed.commandType != "STAND")
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
	parsed.commandType = "STAND";
	*out = parsed;
	return true;
}

static bool parseDirectedFireCommandJson(const std::string &json, const char *expectedCommandType, SnapShotCommandParsed *out)
{
	if (out == 0 || expectedCommandType == 0)
	{
		return false;
	}
	SnapShotCommandParsed parsed = {};
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
	    parsed.commandType != expectedCommandType)
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
	if (!extractTargetCoords(json, &parsed.targetX, &parsed.targetY, &parsed.targetZ))
	{
		parsed.rejectCode = "INVALID_TARGET";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "active_hand", &parsed.activeHand) ||
	    !isValidActiveHandSlot(parsed.activeHand))
	{
		parsed.rejectCode = "INVALID_ACTIVE_HAND";
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

bool parseSnapShotCommandJson(const std::string &json, SnapShotCommandParsed *out)
{
	return parseDirectedFireCommandJson(json, "SNAP_SHOT", out);
}

bool parseAutoShotCommandJson(const std::string &json, SnapShotCommandParsed *out)
{
	return parseDirectedFireCommandJson(json, "AUTO_SHOT", out);
}

bool parseAimedShotCommandJson(const std::string &json, SnapShotCommandParsed *out)
{
	return parseDirectedFireCommandJson(json, "AIMED_SHOT", out);
}

bool isValidPrimeFuseTimerForBattleType(int fuseTimer, int battleType)
{
	if (battleType == BT_GRENADE)
	{
		return fuseTimer >= 0 && fuseTimer <= 23;
	}
	if (battleType == BT_PROXIMITYGRENADE)
	{
		return fuseTimer == 0;
	}
	return false;
}

bool parsePrimeCommandJson(const std::string &json, PrimeCommandParsed *out)
{
	if (out == 0)
	{
		return false;
	}
	PrimeCommandParsed parsed = {};
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
	    parsed.commandType != "PRIME")
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
	if (!CompanionJsonLite::extractJsonStringField(json, "active_hand", &parsed.activeHand) ||
	    !isValidActiveHandSlot(parsed.activeHand))
	{
		parsed.rejectCode = "INVALID_ACTIVE_HAND";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(json, "fuse_timer", &parsed.fuseTimer) ||
	    parsed.fuseTimer < 0 || parsed.fuseTimer > 23)
	{
		parsed.rejectCode = "INVALID_FUSE_TIMER";
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

bool parseThrowCommandJson(const std::string &json, ThrowCommandParsed *out)
{
	if (out == 0)
	{
		return false;
	}
	ThrowCommandParsed parsed = {};
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
	    parsed.commandType != "THROW")
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
	if (!extractTargetCoords(json, &parsed.targetX, &parsed.targetY, &parsed.targetZ))
	{
		parsed.rejectCode = "INVALID_TARGET";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "active_hand", &parsed.activeHand) ||
	    !isValidActiveHandSlot(parsed.activeHand))
	{
		parsed.rejectCode = "INVALID_ACTIVE_HAND";
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

bool parseReloadCommandJson(const std::string &json, ReloadCommandParsed *out)
{
	if (out == 0)
	{
		return false;
	}
	ReloadCommandParsed parsed = {};
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
	    parsed.commandType != "RELOAD")
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
	if (!CompanionJsonLite::extractJsonIntField(json, "weapon_battle_item_id", &parsed.weaponBattleItemId) ||
	    parsed.weaponBattleItemId <= 0)
	{
		parsed.rejectCode = "INVALID_WEAPON_INSTANCE";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(json, "ammo_battle_item_id", &parsed.ammoBattleItemId) ||
	    parsed.ammoBattleItemId <= 0)
	{
		parsed.rejectCode = "INVALID_AMMO_INSTANCE";
		*out = parsed;
		return false;
	}
	if (parsed.weaponBattleItemId == parsed.ammoBattleItemId)
	{
		parsed.rejectCode = "INVALID_AMMO_INSTANCE";
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

bool parseUnloadCommandJson(const std::string &json, UnloadCommandParsed *out)
{
	if (out == 0)
	{
		return false;
	}
	UnloadCommandParsed parsed = {};
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
	    parsed.commandType != "UNLOAD")
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
	if (!CompanionJsonLite::extractJsonIntField(json, "weapon_battle_item_id", &parsed.weaponBattleItemId) ||
	    parsed.weaponBattleItemId <= 0)
	{
		parsed.rejectCode = "INVALID_WEAPON_INSTANCE";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(json, "loaded_ammo_battle_item_id", &parsed.loadedAmmoBattleItemId) ||
	    parsed.loadedAmmoBattleItemId <= 0)
	{
		parsed.rejectCode = "INVALID_AMMO_INSTANCE";
		*out = parsed;
		return false;
	}
	if (parsed.weaponBattleItemId == parsed.loadedAmmoBattleItemId)
	{
		parsed.rejectCode = "INVALID_AMMO_INSTANCE";
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

bool parseInventoryMoveCommandJson(const std::string &json, InventoryMoveCommandParsed *out)
{
	if (out == 0)
	{
		return false;
	}
	InventoryMoveCommandParsed parsed = {};
	parsed.valid = false;
	parsed.sourceCoordsGuard = false;
	parsed.sourceX = 0;
	parsed.sourceY = 0;

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
	    parsed.commandType != "INVENTORY_MOVE")
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
	if (!CompanionJsonLite::extractJsonIntField(json, "item_battle_item_id", &parsed.itemBattleItemId) ||
	    parsed.itemBattleItemId <= 0)
	{
		parsed.rejectCode = "INVALID_ITEM_ID";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "source_slot", &parsed.sourceSlot) ||
	    parsed.sourceSlot.empty())
	{
		parsed.rejectCode = "INVALID_SOURCE_SECTION";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "destination_slot", &parsed.destinationSlot) ||
	    parsed.destinationSlot.empty())
	{
		parsed.rejectCode = "INVALID_DESTINATION_SECTION";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(json, "destination_x", &parsed.destinationX) ||
	    !CompanionJsonLite::extractJsonIntField(json, "destination_y", &parsed.destinationY))
	{
		parsed.rejectCode = "INVALID_DESTINATION_COORDINATES";
		*out = parsed;
		return false;
	}

	std::string::size_type sxStart = 0, syStart = 0;
	const bool hasSourceX = CompanionJsonLite::findJsonKeyValueStart(json, "source_x", &sxStart);
	const bool hasSourceY = CompanionJsonLite::findJsonKeyValueStart(json, "source_y", &syStart);
	if (hasSourceX != hasSourceY)
	{
		parsed.rejectCode = "INVALID_SOURCE_COORDINATES";
		*out = parsed;
		return false;
	}
	if (hasSourceX)
	{
		if (!CompanionJsonLite::parseJsonIntValue(json, sxStart, &parsed.sourceX) ||
		    !CompanionJsonLite::parseJsonIntValue(json, syStart, &parsed.sourceY))
		{
			parsed.rejectCode = "INVALID_SOURCE_COORDINATES";
			*out = parsed;
			return false;
		}
		parsed.sourceCoordsGuard = true;
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

bool parsePickupCommandJson(const std::string &json, PickupCommandParsed *out)
{
	if (out == 0)
	{
		return false;
	}
	PickupCommandParsed parsed = {};
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
	    parsed.commandType != "PICKUP")
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
	if (!CompanionJsonLite::extractJsonIntField(json, "item_battle_item_id", &parsed.itemBattleItemId) ||
	    parsed.itemBattleItemId <= 0)
	{
		parsed.rejectCode = "INVALID_ITEM_ID";
		*out = parsed;
		return false;
	}
	if (!extractGroundPositionCoords(json, &parsed.groundX, &parsed.groundY, &parsed.groundZ))
	{
		parsed.rejectCode = "INVALID_GROUND_POSITION";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "destination_slot", &parsed.destinationSlot) ||
	    parsed.destinationSlot.empty())
	{
		parsed.rejectCode = "INVALID_DESTINATION_SECTION";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(json, "destination_x", &parsed.destinationX) ||
	    !CompanionJsonLite::extractJsonIntField(json, "destination_y", &parsed.destinationY))
	{
		parsed.rejectCode = "INVALID_DESTINATION_COORDINATES";
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

bool parseDropCommandJson(const std::string &json, DropCommandParsed *out)
{
	if (out == 0)
	{
		return false;
	}
	DropCommandParsed parsed = {};
	parsed.valid = false;
	parsed.sourceCoordsGuard = false;
	parsed.sourceX = 0;
	parsed.sourceY = 0;

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
	    parsed.commandType != "DROP")
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
	if (!CompanionJsonLite::extractJsonIntField(json, "item_battle_item_id", &parsed.itemBattleItemId) ||
	    parsed.itemBattleItemId <= 0)
	{
		parsed.rejectCode = "INVALID_ITEM_ID";
		*out = parsed;
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "source_slot", &parsed.sourceSlot) ||
	    parsed.sourceSlot.empty())
	{
		parsed.rejectCode = "INVALID_SOURCE_SECTION";
		*out = parsed;
		return false;
	}

	std::string::size_type sxStart = 0, syStart = 0;
	const bool hasSourceX = CompanionJsonLite::findJsonKeyValueStart(json, "source_x", &sxStart);
	const bool hasSourceY = CompanionJsonLite::findJsonKeyValueStart(json, "source_y", &syStart);
	if (hasSourceX != hasSourceY)
	{
		parsed.rejectCode = "INVALID_SOURCE_COORDINATES";
		*out = parsed;
		return false;
	}
	if (hasSourceX)
	{
		if (!CompanionJsonLite::parseJsonIntValue(json, sxStart, &parsed.sourceX) ||
		    !CompanionJsonLite::parseJsonIntValue(json, syStart, &parsed.sourceY))
		{
			parsed.rejectCode = "INVALID_SOURCE_COORDINATES";
			*out = parsed;
			return false;
		}
		parsed.sourceCoordsGuard = true;
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

MoveValidationResult validateTacticalCommandBinding(const TacticalCommandBindingValidationInput &input)
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
	if (input.battleBusy)
	{
		result.rejectCode = "BATTLE_BUSY";
		result.publicReason = "Battle is busy with animation or actions.";
		return result;
	}

	result.accepted = true;
	return result;
}

MoveValidationResult validateMoveCommand(const MoveValidationInput &input)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;
	if (input.destX < 0 || input.destY < 0 || input.destZ < 0 ||
	    input.destX >= input.mapSizeX || input.destY >= input.mapSizeY || input.destZ >= input.mapSizeZ)
	{
		result.rejectCode = "INVALID_DESTINATION";
		result.publicReason = "Destination out of map bounds.";
		return result;
	}
	result.accepted = true;
	return result;
}

MoveValidationResult validateKneelCommand(const KneelValidationInput &input)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;
	if (input.actorAlreadyKneeled)
	{
		result.rejectCode = "ALREADY_KNEELING";
		result.publicReason = "Unit is already kneeling.";
		return result;
	}
	if (!input.actorCanKneelPosture)
	{
		result.rejectCode = "INVALID_ACTOR";
		result.publicReason = "Actor cannot kneel.";
		return result;
	}
	if (input.insufficientTuForKneelDown)
	{
		result.rejectCode = "INSUFFICIENT_TU";
		result.publicReason = "Not enough time units to kneel.";
		return result;
	}
	result.accepted = true;
	return result;
}

MoveValidationResult validateStandCommand(const StandValidationInput &input)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;
	if (!input.actorKneeling)
	{
		result.rejectCode = "ALREADY_STANDING";
		result.publicReason = "Unit is already standing.";
		return result;
	}
	if (!input.actorCanKneelPosture)
	{
		result.rejectCode = "INVALID_ACTOR";
		result.publicReason = "Actor cannot change posture.";
		return result;
	}
	if (input.insufficientTuForStandUp)
	{
		result.rejectCode = "INSUFFICIENT_TU";
		result.publicReason = "Not enough time units to stand.";
		return result;
	}
	result.accepted = true;
	return result;
}

static MoveValidationResult validateDirectedFireCommand(
	const SnapShotValidationInput &input,
	const char *unsupportedModeReason,
	const char *insufficientTuReason)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;
	if (input.targetX < 0 || input.targetY < 0 || input.targetZ < 0 ||
	    input.targetX >= input.mapSizeX || input.targetY >= input.mapSizeY || input.targetZ >= input.mapSizeZ)
	{
		result.rejectCode = "INVALID_TARGET";
		result.publicReason = "Target out of map bounds.";
		return result;
	}
	if (!input.activeHandValid)
	{
		result.rejectCode = "INVALID_ACTIVE_HAND";
		result.publicReason = "Active hand must be STR_LEFT_HAND or STR_RIGHT_HAND.";
		return result;
	}
	if (!input.weaponPresentInHand)
	{
		result.rejectCode = "WEAPON_NOT_IN_HAND";
		result.publicReason = "No weapon in the selected hand.";
		return result;
	}
	if (!input.weaponSupportsFireMode)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = unsupportedModeReason;
		return result;
	}
	if (!input.weaponUsable)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Weapon is not usable for firing.";
		return result;
	}
	if (!input.weaponHasAmmo)
	{
		result.rejectCode = "NO_AMMUNITION";
		result.publicReason = "Weapon has no ammunition.";
		return result;
	}
	if (input.insufficientTuForFireMode)
	{
		result.rejectCode = "INSUFFICIENT_TU";
		result.publicReason = insufficientTuReason;
		return result;
	}
	result.accepted = true;
	return result;
}

MoveValidationResult validateSnapShotCommand(const SnapShotValidationInput &input)
{
	return validateDirectedFireCommand(
		input,
		"Weapon does not support snap shot.",
		"Not enough time units for snap shot.");
}

MoveValidationResult validateAutoShotCommand(const SnapShotValidationInput &input)
{
	return validateDirectedFireCommand(
		input,
		"Weapon does not support automatic fire.",
		"Not enough time units for automatic fire.");
}

MoveValidationResult validateAimedShotCommand(const SnapShotValidationInput &input)
{
	return validateDirectedFireCommand(
		input,
		"Weapon does not support aimed shot.",
		"Not enough time units for aimed shot.");
}

MoveValidationResult validatePrimeCommand(const PrimeValidationInput &input)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;
	if (!input.activeHandValid)
	{
		result.rejectCode = "INVALID_ACTIVE_HAND";
		result.publicReason = "Active hand must be STR_LEFT_HAND or STR_RIGHT_HAND.";
		return result;
	}
	if (!input.grenadePresentInHand)
	{
		result.rejectCode = "GRENADE_NOT_IN_HAND";
		result.publicReason = "No grenade in the selected hand.";
		return result;
	}
	if (!input.grenadeSupportsPrime)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Item in hand does not support priming.";
		return result;
	}
	if (input.grenadeAlreadyPrimed)
	{
		result.rejectCode = "ALREADY_PRIMED";
		result.publicReason = "Grenade is already primed.";
		return result;
	}
	if (input.invalidFuseTimer)
	{
		result.rejectCode = "INVALID_FUSE_TIMER";
		result.publicReason = "Fuse timer selection is invalid for this grenade type.";
		return result;
	}
	if (!input.grenadeUsable)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Grenade is not usable for priming.";
		return result;
	}
	if (input.insufficientTuForPrime)
	{
		result.rejectCode = "INSUFFICIENT_TU";
		result.publicReason = "Not enough time units to prime grenade.";
		return result;
	}
	result.accepted = true;
	return result;
}

MoveValidationResult validateThrowCommand(const ThrowValidationInput &input)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;
	if (input.targetX < 0 || input.targetY < 0 || input.targetZ < 0 ||
	    input.targetX >= input.mapSizeX || input.targetY >= input.mapSizeY || input.targetZ >= input.mapSizeZ)
	{
		result.rejectCode = "INVALID_TARGET";
		result.publicReason = "Target out of map bounds.";
		return result;
	}
	if (!input.activeHandValid)
	{
		result.rejectCode = "INVALID_ACTIVE_HAND";
		result.publicReason = "Active hand must be STR_LEFT_HAND or STR_RIGHT_HAND.";
		return result;
	}
	if (!input.grenadePresentInHand)
	{
		result.rejectCode = "GRENADE_NOT_IN_HAND";
		result.publicReason = "No grenade in the selected hand.";
		return result;
	}
	if (!input.grenadeSupportsThrow)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Item in hand does not support throwing.";
		return result;
	}
	if (!input.grenadeUsable)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Grenade is not usable for throwing.";
		return result;
	}
	if (input.targetOutOfThrowRange)
	{
		result.rejectCode = "OUT_OF_RANGE";
		result.publicReason = "Target is outside throwing range.";
		return result;
	}
	if (input.insufficientTuForThrow)
	{
		result.rejectCode = "INSUFFICIENT_TU";
		result.publicReason = "Not enough time units to throw grenade.";
		return result;
	}
	result.accepted = true;
	return result;
}

MoveValidationResult validateReloadCommand(const ReloadValidationInput &input)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;

	if (!input.campaignResearchAvailable)
	{
		result.rejectCode = "RESEARCH_CONTEXT_UNAVAILABLE";
		result.publicReason = "Campaign save unavailable for research validation.";
		return result;
	}
	if (!input.weaponResolved)
	{
		result.rejectCode = "INVALID_WEAPON_INSTANCE";
		result.publicReason = "Weapon battle item not found.";
		return result;
	}
	if (!input.ammoResolved)
	{
		result.rejectCode = "INVALID_AMMO_INSTANCE";
		result.publicReason = "Ammunition battle item not found.";
		return result;
	}
	if (!input.weaponBelongsToActor)
	{
		result.rejectCode = "INVALID_WEAPON_INSTANCE";
		result.publicReason = "Weapon does not belong to the actor.";
		return result;
	}
	if (!input.ammoBelongsToActor)
	{
		result.rejectCode = "INVALID_AMMO_INSTANCE";
		result.publicReason = "Ammunition does not belong to the actor.";
		return result;
	}
	if (!input.weaponInEitherHand)
	{
		result.rejectCode = "WEAPON_NOT_IN_HAND";
		result.publicReason = "Weapon must be in STR_LEFT_HAND or STR_RIGHT_HAND.";
		return result;
	}
	if (!input.weaponIsFirearm)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Item is not a firearm.";
		return result;
	}
	if (!input.weaponNeedsPhysicalMagazine)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Weapon does not use insertable ammunition.";
		return result;
	}
	if (!input.weaponChamberEmptyForReloadUi)
	{
		result.rejectCode = "WEAPON_ALREADY_LOADED";
		result.publicReason = "Weapon already contains a magazine (unload or use ammo first).";
		return result;
	}
	if (!input.ammoIsPhysicalClip)
	{
		result.rejectCode = "INCOMPATIBLE_AMMUNITION";
		result.publicReason = "Selected item is not ammunition.";
		return result;
	}
	if (!input.ammunitionCompatible)
	{
		result.rejectCode = "INCOMPATIBLE_AMMUNITION";
		result.publicReason = "Ammunition type is not compatible with this weapon.";
		return result;
	}
	if (!input.ammoNotAlreadyLoadedInWeapon)
	{
		result.rejectCode = "INVALID_AMMO_INSTANCE";
		result.publicReason = "Selected ammunition is already loaded in this weapon.";
		return result;
	}
	if (!input.ammoCarriedOffGround)
	{
		result.rejectCode = "AMMO_LOCATION_INVALID";
		result.publicReason = "Ammunition must be carried on the soldier, not on the ground.";
		return result;
	}
	if (!input.weaponUsableEnvironment)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Weapon is not usable in current battlescape conditions.";
		return result;
	}
	if (!input.weaponResearchAuthorized)
	{
		result.rejectCode = "RESEARCH_RESTRICTED";
		result.publicReason = "Weapon is not researched for use.";
		return result;
	}
	if (!input.ammoResearchAuthorized)
	{
		result.rejectCode = "RESEARCH_RESTRICTED";
		result.publicReason = "Ammunition is not researched for use.";
		return result;
	}
	if (input.insufficientTuForReload)
	{
		result.rejectCode = "INSUFFICIENT_TU";
		result.publicReason = "Not enough time units to reload (15 TU).";
		return result;
	}

	result.accepted = true;
	return result;
}

MoveValidationResult validateUnloadCommand(const UnloadValidationInput &input)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;

	if (!input.campaignResearchAvailable)
	{
		result.rejectCode = "RESEARCH_CONTEXT_UNAVAILABLE";
		result.publicReason = "Campaign save unavailable for research validation.";
		return result;
	}
	if (!input.weaponResolved)
	{
		result.rejectCode = "INVALID_WEAPON_INSTANCE";
		result.publicReason = "Weapon battle item not found.";
		return result;
	}
	if (!input.weaponBelongsToActor)
	{
		result.rejectCode = "INVALID_WEAPON_INSTANCE";
		result.publicReason = "Weapon does not belong to the actor.";
		return result;
	}
	if (!input.weaponInEitherHand)
	{
		result.rejectCode = "WEAPON_NOT_IN_HAND";
		result.publicReason = "Weapon must be in STR_LEFT_HAND or STR_RIGHT_HAND.";
		return result;
	}
	if (!input.weaponIsFirearm)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Item is not a firearm.";
		return result;
	}
	if (!input.weaponNeedsPhysicalMagazine)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Weapon does not use insertable ammunition.";
		return result;
	}
	if (!input.weaponHasLoadedMagazine)
	{
		result.rejectCode = "NO_MAGAZINE_INSERTED";
		result.publicReason = "Weapon has no magazine inserted.";
		return result;
	}
	if (!input.loadedAmmoIdentityMatches)
	{
		result.rejectCode = "MAGAZINE_IDENTITY_MISMATCH";
		result.publicReason = "loaded_ammo_battle_item_id does not match the weapon's inserted magazine.";
		return result;
	}
	if (!input.loadedAmmoIsPhysicalClip)
	{
		result.rejectCode = "INCOMPATIBLE_AMMUNITION";
		result.publicReason = "Inserted item is not ammunition.";
		return result;
	}
	if (!input.weaponUsableEnvironment)
	{
		result.rejectCode = "UNSUPPORTED_WEAPON";
		result.publicReason = "Weapon or ammunition is not usable in current battlescape conditions.";
		return result;
	}
	if (!input.weaponResearchAuthorized)
	{
		result.rejectCode = "RESEARCH_RESTRICTED";
		result.publicReason = "Weapon is not researched for use.";
		return result;
	}
	if (!input.ammoResearchAuthorized)
	{
		result.rejectCode = "RESEARCH_RESTRICTED";
		result.publicReason = "Magazine is not researched for use.";
		return result;
	}
	if (!input.otherHandClearExceptWeapon)
	{
		result.rejectCode = "UNLOAD_HANDS_NOT_FREE";
		result.publicReason = "Both hands must be free except the firearm (matches inventory unload UI).";
		return result;
	}
	if (input.insufficientTuForUnload)
	{
		result.rejectCode = "INSUFFICIENT_TU";
		result.publicReason = "Not enough time units to unload (8 TU).";
		return result;
	}

	result.accepted = true;
	return result;
}

MoveValidationResult validateInventoryMoveCommand(const InventoryMoveValidationInput &input)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;

	if (!input.itemResolved)
	{
		result.rejectCode = "INVALID_ITEM_ID";
		result.publicReason = "Battle item not found on actor.";
		return result;
	}
	if (!input.itemBelongsToActor)
	{
		result.rejectCode = "UNAUTHORIZED_ITEM";
		result.publicReason = "Item does not belong to the actor.";
		return result;
	}
	if (!input.itemCarriedOffGround)
	{
		result.rejectCode = "ITEM_NOT_CARRIED";
		result.publicReason = "Item must be carried in soldier inventory, not on ground.";
		return result;
	}
	if (!input.itemSlotMatchesCommand)
	{
		result.rejectCode = "SOURCE_LOCATION_MISMATCH";
		result.publicReason = "Item is not in the requested source_slot.";
		return result;
	}
	if (input.sourceCoordsGuard && !input.sourceCoordsMatch)
	{
		result.rejectCode = "SOURCE_LOCATION_MISMATCH";
		result.publicReason = "source_x/source_y do not match current item placement.";
		return result;
	}
	if (input.itemIsFixedWeapon)
	{
		result.rejectCode = "UNSUPPORTED_ITEM";
		result.publicReason = "Fixed or built-in equipment cannot be moved.";
		return result;
	}
	if (input.itemNestedInFirearm)
	{
		result.rejectCode = "NESTED_AMMUNITION";
		result.publicReason = "Loaded ammunition must be unloaded before moving (use UNLOAD).";
		return result;
	}
	if (input.primedExplosiveBlocked)
	{
		result.rejectCode = "PRIMED_EXPLOSIVE_NOT_MOVABLE";
		result.publicReason = "Primed explosives cannot be moved via inventory.";
		return result;
	}
	if (!input.destinationSlotResolved)
	{
		result.rejectCode = "INVALID_DESTINATION_SECTION";
		result.publicReason = "Unknown or missing destination inventory section.";
		return result;
	}
	if (input.destinationIsGround)
	{
		result.rejectCode = "INVALID_DESTINATION_SECTION";
		result.publicReason = "Ground inventory is not supported for INVENTORY_MOVE.";
		return result;
	}
	if (!input.destinationAllowedCarriedType)
	{
		result.rejectCode = "INVALID_DESTINATION_SECTION";
		result.publicReason = "Destination is not a valid carried inventory section.";
		return result;
	}
	if (!input.destinationSameSectionAsCommand)
	{
		result.rejectCode = "INVALID_DESTINATION_SECTION";
		result.publicReason = "destination_slot does not match resolved ruleset section.";
		return result;
	}
	if (!input.itemFitsDestinationGeometry)
	{
		result.rejectCode = "ITEM_DOES_NOT_FIT";
		result.publicReason = "Item does not fit at the requested destination coordinates.";
		return result;
	}
	if (input.destinationOccupied)
	{
		result.rejectCode = "DESTINATION_OCCUPIED";
		result.publicReason = "Destination cells overlap another item.";
		return result;
	}
	if (!input.transferCostResolved)
	{
		result.rejectCode = "TRANSFER_COST_UNAVAILABLE";
		result.publicReason = "No defined TU cost between these inventory sections in the active ruleset.";
		return result;
	}
	if (input.insufficientTuForInventoryMove)
	{
		result.rejectCode = "INSUFFICIENT_TU";
		result.publicReason = "Not enough time units for this inventory transfer.";
		return result;
	}

	result.accepted = true;
	return result;
}

MoveValidationResult validatePickupCommand(const PickupValidationInput &input)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;

	if (!input.campaignResearchAvailable)
	{
		result.rejectCode = "RESEARCH_CONTEXT_UNAVAILABLE";
		result.publicReason = "Campaign save unavailable for research validation.";
		return result;
	}
	if (!input.groundPositionMatchesActor)
	{
		result.rejectCode = "GROUND_POSITION_MISMATCH";
		result.publicReason = "ground_position does not match the actor tile.";
		return result;
	}
	if (!input.itemResolvedOnActorGround)
	{
		result.rejectCode = "GROUND_ITEM_NOT_KNOWN";
		result.publicReason = "Ground item not found in snapshot export for this soldier.";
		return result;
	}
	if (!input.itemResearchAuthorized)
	{
		result.rejectCode = "RESEARCH_RESTRICTED";
		result.publicReason = "Item is not researched for use.";
		return result;
	}
	if (input.itemIsFixedWeapon)
	{
		result.rejectCode = "UNSUPPORTED_ITEM";
		result.publicReason = "Fixed or built-in equipment cannot be picked up.";
		return result;
	}
	if (!input.destinationSlotResolved)
	{
		result.rejectCode = "INVALID_DESTINATION_SECTION";
		result.publicReason = "Unknown or missing destination inventory section.";
		return result;
	}
	if (!input.destinationAllowedCarriedType)
	{
		result.rejectCode = "INVALID_DESTINATION_SECTION";
		result.publicReason = "Destination is not a valid carried inventory section.";
		return result;
	}
	if (!input.destinationSameSectionAsCommand)
	{
		result.rejectCode = "INVALID_DESTINATION_SECTION";
		result.publicReason = "destination_slot does not match resolved ruleset section.";
		return result;
	}
	if (!input.itemFitsDestinationGeometry)
	{
		result.rejectCode = "ITEM_DOES_NOT_FIT";
		result.publicReason = "Item does not fit at the requested destination coordinates.";
		return result;
	}
	if (input.destinationOccupied)
	{
		result.rejectCode = "DESTINATION_OCCUPIED";
		result.publicReason = "Destination cells overlap another item.";
		return result;
	}
	if (!input.transferCostResolved)
	{
		result.rejectCode = "TRANSFER_COST_UNAVAILABLE";
		result.publicReason = "No defined TU cost from ground to destination in the active ruleset.";
		return result;
	}
	if (input.insufficientTuForPickup)
	{
		result.rejectCode = "INSUFFICIENT_TU";
		result.publicReason = "Not enough time units for ground pickup.";
		return result;
	}

	result.accepted = true;
	return result;
}

MoveValidationResult validateDropCommand(const DropValidationInput &input)
{
	MoveValidationResult result = validateTacticalCommandBinding(input);
	if (!result.accepted)
	{
		return result;
	}
	result.accepted = false;

	if (!input.campaignResearchAvailable)
	{
		result.rejectCode = "RESEARCH_CONTEXT_UNAVAILABLE";
		result.publicReason = "Campaign save unavailable for research validation.";
		return result;
	}
	if (!input.itemResolved)
	{
		result.rejectCode = "INVALID_ITEM_ID";
		result.publicReason = "Battle item not found on actor.";
		return result;
	}
	if (!input.itemBelongsToActor)
	{
		result.rejectCode = "UNAUTHORIZED_ITEM";
		result.publicReason = "Item does not belong to the actor.";
		return result;
	}
	if (!input.itemCarriedOffGround)
	{
		result.rejectCode = "ITEM_NOT_CARRIED";
		result.publicReason = "Item must be carried in soldier inventory, not on ground.";
		return result;
	}
	if (!input.itemSlotMatchesCommand)
	{
		result.rejectCode = "SOURCE_LOCATION_MISMATCH";
		result.publicReason = "Item is not in the requested source_slot.";
		return result;
	}
	if (input.sourceCoordsGuard && !input.sourceCoordsMatch)
	{
		result.rejectCode = "SOURCE_LOCATION_MISMATCH";
		result.publicReason = "source_x/source_y do not match current item placement.";
		return result;
	}
	if (input.itemIsFixedWeapon)
	{
		result.rejectCode = "UNSUPPORTED_ITEM";
		result.publicReason = "Fixed or built-in equipment cannot be dropped.";
		return result;
	}
	if (input.itemNestedInFirearm)
	{
		result.rejectCode = "NESTED_AMMUNITION";
		result.publicReason = "Loaded ammunition must be unloaded before dropping (use UNLOAD).";
		return result;
	}
	if (!input.itemResearchAuthorized)
	{
		result.rejectCode = "RESEARCH_RESTRICTED";
		result.publicReason = "Item is not researched for use.";
		return result;
	}
	if (!input.groundRuleResolved)
	{
		result.rejectCode = "INVALID_GROUND_DESTINATION";
		result.publicReason = "Ground inventory rules unavailable.";
		return result;
	}
	if (!input.transferCostResolved)
	{
		result.rejectCode = "TRANSFER_COST_UNAVAILABLE";
		result.publicReason = "No defined TU cost from source section to ground in the active ruleset.";
		return result;
	}
	if (!input.groundHasRoom)
	{
		result.rejectCode = "ITEM_DOES_NOT_FIT";
		result.publicReason = "No room on ground for this item.";
		return result;
	}
	if (input.insufficientTuForDrop)
	{
		result.rejectCode = "INSUFFICIENT_TU";
		result.publicReason = "Not enough time units for ground drop.";
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

static bool walkEngineResultSetsPublicReason(const std::string &actionResult, const char **outReason,
	const char **outPublic)
{
	if (actionResult == CompanionWalkStopTokens::HOSTILE_SPOTTED)
	{
		*outReason = "hostile_spotted";
		*outPublic = "Movement halted: hostile unit spotted.";
		return true;
	}
	if (actionResult == CompanionWalkStopTokens::REACTION_FIRE)
	{
		*outReason = "reaction_fire";
		*outPublic = "Movement halted: reaction fire.";
		return true;
	}
	if (actionResult == CompanionWalkStopTokens::PATH_BLOCKED)
	{
		*outReason = "path_blocked";
		*outPublic = "Movement halted: path blocked.";
		return true;
	}
	if (actionResult == CompanionWalkStopTokens::PROXIMITY_GRENADE)
	{
		*outReason = "proximity_grenade";
		*outPublic = "Movement halted: proximity grenade.";
		return true;
	}
	if (actionResult == "STR_NOT_ENOUGH_TIME_UNITS")
	{
		*outReason = "insufficient_time_units";
		*outPublic = "Movement halted: insufficient time units.";
		return true;
	}
	if (actionResult == "STR_NOT_ENOUGH_ENERGY")
	{
		*outReason = "insufficient_energy";
		*outPublic = "Movement halted: insufficient stamina.";
		return true;
	}
	return false;
}

WalkStopDiagnostics resolveWalkStopDiagnostics(
	CommandLifecycleStatus terminal,
	const WalkCompletionInput &input)
{
	WalkStopDiagnostics out = {};
	if (terminal == CMD_COMPLETED || terminal == CMD_EXECUTING
	    || terminal == CMD_ACCEPTED || terminal == CMD_REJECTED)
	{
		return out;
	}
	if (!input.actorAlive)
	{
		out.movementStopReason = "actor_incapacitated";
		out.publicReason = "Movement ended: actor incapacitated.";
		return out;
	}
	if (!input.actorStillAiControlled)
	{
		out.movementStopReason = "authority_lost";
		out.publicReason = "Movement interrupted: soldier is no longer AI-controlled.";
		return out;
	}
	const char *mappedReason = 0;
	const char *mappedPublic = 0;
	if (!input.actionResult.empty()
	    && walkEngineResultSetsPublicReason(input.actionResult, &mappedReason, &mappedPublic))
	{
		out.movementStopReason = mappedReason;
		out.publicReason = mappedPublic;
		out.engineActionResult = input.actionResult;
		return out;
	}
	if (terminal == CMD_INTERRUPTED && input.currentPos != input.startPos)
	{
		out.movementStopReason = "movement_incomplete";
		out.publicReason = "Movement interrupted before destination (cause not recorded by engine).";
		return out;
	}
	if (terminal == CMD_FAILED && input.currentPos == input.startPos)
	{
		out.movementStopReason = "no_progress";
		out.publicReason = "Movement did not advance toward destination.";
		return out;
	}
	if (terminal == CMD_INTERRUPTED || terminal == CMD_FAILED)
	{
		out.movementStopReason = "movement_incomplete";
		out.publicReason = "Movement interrupted or incomplete.";
	}
	return out;
}

CommandLifecycleStatus evaluateSnapShotTerminalStatus(const SnapShotCompletionInput &input)
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
	if (!input.projectileLaunched)
	{
		return CMD_FAILED;
	}
	return CMD_COMPLETED;
}

CommandLifecycleStatus evaluateThrowTerminalStatus(const SnapShotCompletionInput &input)
{
	return evaluateSnapShotTerminalStatus(input);
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
