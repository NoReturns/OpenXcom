/*
 * Open X-Com Companion — remote HWP command actor authority (M1 HWP-S3A).
 */
#include "CompanionTacticalHwpAuthorityCore.h"
#include "CompanionSoldierOwnershipCore.h"
#include "CompanionTacticalAuthorityCore.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"

namespace OpenXcom
{

namespace CompanionTacticalHwpAuthorityCore
{

namespace
{

using CompanionCommandCore::MoveValidationResult;

MoveValidationResult reject(const char *code, const char *reason)
{
	MoveValidationResult result = {};
	result.accepted = false;
	result.rejectCode = code;
	result.publicReason = reason;
	return result;
}

MoveValidationResult accept()
{
	MoveValidationResult result = {};
	result.accepted = true;
	return result;
}

bool relayAiHwpAssigned(
	const std::string &controlFileJson,
	bool controlFileReadOk,
	const CompanionSoldierOwnershipCore::RelayControlBindingContext &binding,
	int hwpUnitId)
{
	if (!controlFileReadOk || controlFileJson.empty())
	{
		return false;
	}
	return CompanionSoldierOwnershipCore::isAiControlledHwpUnit(controlFileJson, binding, hwpUnitId);
}

bool tacticalAiHwpAssigned(const SavedBattleGame *battle, int hwpUnitId)
{
	if (battle == 0 || hwpUnitId < BattleUnit::MAX_SOLDIER_ID)
	{
		return false;
	}
	const std::set<int> &ai = battle->getCompanionTacticalAiHwpIds();
	return ai.find(hwpUnitId) != ai.end();
}

MoveValidationResult validateBindingFreshness(const HwpRemoteAuthorityInput &input)
{
	if (!input.requireBindingFreshness)
	{
		return accept();
	}
	if (input.battleSessionId.empty() || input.commandSnapshotSha256.empty())
	{
		return reject("INVALID_BINDING", "Missing session or snapshot binding.");
	}
	if (input.commandSnapshotSha256 != input.lastSnapshotSha256)
	{
		return reject("STALE_SNAPSHOT", "Snapshot SHA-256 does not match current export.");
	}
	if (input.commandSessionId != input.battleSessionId)
	{
		return reject("STALE_SESSION", "Session ID does not match current battle.");
	}
	if (input.commandTurn != input.turn)
	{
		return reject("STALE_TURN", "Turn does not match current battle.");
	}
	if (input.lastExportedStateVersion < 0)
	{
		return reject("INVALID_BINDING", "No exported state version binding.");
	}
	if (input.commandStateVersion != input.lastExportedStateVersion)
	{
		return reject("STALE_STATE_VERSION", "State version does not match last tactical export.");
	}
	if (input.turn <= 0)
	{
		return reject("INVALID_TURN", "Invalid turn.");
	}
	return accept();
}

} // namespace

bool hwpTacticalCommandExecutionSupported()
{
	return false;
}

bool hwpMoveRelayExecutionSupported()
{
	return true;
}

const BattleUnit *findBattleUnitById(SavedBattleGame *battle, int unitId)
{
	if (battle == 0 || unitId <= 0)
	{
		return 0;
	}
	for (std::vector<BattleUnit*>::const_iterator i = battle->getUnits()->begin();
	     i != battle->getUnits()->end();
	     ++i)
	{
		if (*i != 0 && (*i)->getId() == unitId)
		{
			return *i;
		}
	}
	return 0;
}

bool isOperationalFriendlyHwpForRemoteCommands(const BattleUnit *unit)
{
	if (unit == 0)
	{
		return false;
	}
	if (unit->getOriginalFaction() != FACTION_PLAYER || unit->getFaction() != FACTION_PLAYER)
	{
		return false;
	}
	if (unit->getGeoscapeSoldier() != 0)
	{
		return false;
	}
	if (unit->getId() < BattleUnit::MAX_SOLDIER_ID)
	{
		return false;
	}
	if (unit->isOut())
	{
		return false;
	}
	if (unit->getStatus() == STATUS_DEAD || unit->getStatus() == STATUS_UNCONSCIOUS)
	{
		return false;
	}
	if (unit->getOriginalFaction() == FACTION_PLAYER && unit->getFaction() != FACTION_PLAYER)
	{
		return false;
	}
	return true;
}

MoveValidationResult evaluateHwpRemoteCommandAuthority(const HwpRemoteAuthorityInput &input)
{
	if (input.campaign == 0 || input.battle == 0)
	{
		return reject("NO_BATTLE_CONTEXT", "Campaign or battle context missing.");
	}
	if (!input.battleInitialized)
	{
		return reject("BATTLE_NOT_READY", "Battle is not initialized.");
	}
	if (!input.playerTurn)
	{
		return reject("WRONG_TURN", "Not player turn.");
	}
	if (input.unitsFalling)
	{
		return reject("UNITS_FALLING", "Units are falling.");
	}
	if (input.battleBusy)
	{
		return reject("BATTLE_BUSY", "Battle is busy with animation or actions.");
	}
	if (input.actorUnitId <= 0)
	{
		return reject("INVALID_ACTOR", "Missing actor unit id.");
	}

	const BattleUnit *unit = findBattleUnitById(input.battle, input.actorUnitId);
	if (unit == 0)
	{
		return reject("INVALID_ACTOR", "Actor not present on current battlescape.");
	}

	if (CompanionTacticalAuthorityCore::isLiveCampaignPlayerSoldierUnit(unit, input.campaign))
	{
		return reject("INVALID_ACTOR", "Actor is a roster soldier, not a friendly HWP.");
	}

	if (unit->getOriginalFaction() != FACTION_PLAYER)
	{
		return reject("INVALID_ACTOR", "Actor is not a friendly HWP.");
	}

	if (unit->getGeoscapeSoldier() != 0 || unit->getId() < BattleUnit::MAX_SOLDIER_ID)
	{
		return reject("INVALID_ACTOR", "Actor id is not a battle HWP.");
	}

	if (!isOperationalFriendlyHwpForRemoteCommands(unit))
	{
		return reject("INVALID_ACTOR", "HWP is not alive and operational.");
	}

	if (input.actorNativeActionInProgress)
	{
		return reject("ACTOR_BUSY", "HWP is executing an incompatible native action.");
	}

	if (!tacticalAiHwpAssigned(input.battle, input.actorUnitId))
	{
		return reject("NOT_AI_CONTROLLED", "HWP is not assigned to Aster in this battle.");
	}

	CompanionSoldierOwnershipCore::RelayControlBindingContext binding = {};
	binding.battleSessionId = input.battleSessionId;
	binding.campaignId = input.relayCampaignId;
	binding.tacticalBattleId = input.relayTacticalBattleId;
	if (!relayAiHwpAssigned(input.controlFileJson, input.controlFileReadOk, binding, input.actorUnitId))
	{
		return reject("NOT_AI_CONTROLLED", "HWP is not Aster-controlled in relay binding.");
	}

	MoveValidationResult bindingVerdict = validateBindingFreshness(input);
	if (!bindingVerdict.accepted)
	{
		return bindingVerdict;
	}

	return accept();
}

MoveValidationResult validateHwpTacticalCommandExecutionPreflight(const HwpRemoteAuthorityInput &input)
{
	MoveValidationResult authority = evaluateHwpRemoteCommandAuthority(input);
	if (!authority.accepted)
	{
		return authority;
	}
	if (!hwpTacticalCommandExecutionSupported())
	{
		return reject("HWP_EXECUTION_NOT_SUPPORTED", "HWP tactical command execution is not enabled.");
	}
	return accept();
}

MoveValidationResult validateHwpMoveCommand(const HwpMoveValidationInput &input)
{
	MoveValidationResult authority = evaluateHwpRemoteCommandAuthority(input.authority);
	if (!authority.accepted)
	{
		return authority;
	}
	authority.accepted = false;
	if (input.destX < 0 || input.destY < 0 || input.destZ < 0 ||
	    input.destX >= input.mapSizeX || input.destY >= input.mapSizeY || input.destZ >= input.mapSizeZ)
	{
		return reject("INVALID_DESTINATION", "Destination out of map bounds.");
	}
	return accept();
}

MoveValidationResult validateHwpMoveExecutionPreflight(const HwpMoveValidationInput &input)
{
	MoveValidationResult contract = validateHwpMoveCommand(input);
	if (!contract.accepted)
	{
		return contract;
	}
	if (!hwpMoveRelayExecutionSupported())
	{
		return reject("HWP_EXECUTION_NOT_SUPPORTED", "HWP_MOVE relay execution is not enabled.");
	}
	return accept();
}

} // namespace CompanionTacticalHwpAuthorityCore

} // namespace OpenXcom