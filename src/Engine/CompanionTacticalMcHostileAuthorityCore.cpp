/*
 * Open X-Com Companion — remote MC hostile command actor authority (M1 MC-02B).
 */
#include "CompanionTacticalMcHostileAuthorityCore.h"
#include "CompanionSoldierOwnershipCore.h"
#include "CompanionTacticalAuthorityCore.h"
#include "CompanionTacticalHwpAuthorityCore.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include <set>

namespace OpenXcom
{

namespace CompanionTacticalMcHostileAuthorityCore
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

bool relayAiMcHostileAssigned(
	const std::string &controlFileJson,
	bool controlFileReadOk,
	const CompanionSoldierOwnershipCore::RelayControlBindingContext &binding,
	int unitId)
{
	if (!controlFileReadOk || controlFileJson.empty())
	{
		return false;
	}
	return CompanionSoldierOwnershipCore::isAiControlledMcHostileUnit(controlFileJson, binding, unitId);
}

bool tacticalAiMcHostileAssigned(const SavedBattleGame *battle, int unitId)
{
	if (battle == 0 || unitId < BattleUnit::MAX_SOLDIER_ID)
	{
		return false;
	}
	const std::set<int> &ai = battle->getCompanionTacticalAiMcHostileIds();
	return ai.find(unitId) != ai.end();
}

MoveValidationResult validateBindingFreshness(const McRemoteAuthorityInput &input)
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

bool mcTacticalCommandExecutionSupported()
{
	return false;
}

bool mcMoveRelayExecutionSupported()
{
	return true;
}

bool mcTurnFaceRelayExecutionSupported()
{
	return true;
}

bool mcKneelRelayExecutionSupported()
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

bool isXcomControlledMcHostileBattleUnit(const BattleUnit *unit)
{
	if (unit == 0)
	{
		return false;
	}
	return unit->getOriginalFaction() == FACTION_HOSTILE && unit->getFaction() == FACTION_PLAYER;
}

bool isOperationalMcHostileForRemoteCommands(const BattleUnit *unit)
{
	if (unit == 0)
	{
		return false;
	}
	if (!isXcomControlledMcHostileBattleUnit(unit))
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
	if (CompanionTacticalHwpAuthorityCore::isOperationalFriendlyHwpForRemoteCommands(unit))
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
	return true;
}

MoveValidationResult evaluateMcRemoteCommandAuthority(const McRemoteAuthorityInput &input)
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
		return reject("INVALID_ACTOR", "Actor is a roster soldier, not an MC hostile.");
	}

	if (unit->getGeoscapeSoldier() != 0 || unit->getId() < BattleUnit::MAX_SOLDIER_ID)
	{
		return reject("INVALID_ACTOR", "Actor id is not a mind-controlled hostile battle unit.");
	}

	if (CompanionTacticalHwpAuthorityCore::isOperationalFriendlyHwpForRemoteCommands(unit))
	{
		return reject("INVALID_ACTOR", "Actor is a friendly HWP, not an MC hostile.");
	}

	if (!isOperationalMcHostileForRemoteCommands(unit))
	{
		return reject("INVALID_ACTOR", "MC hostile is not alive and operational.");
	}

	if (input.actorNativeActionInProgress)
	{
		return reject("ACTOR_BUSY", "Unit is executing an incompatible native action.");
	}

	if (!tacticalAiMcHostileAssigned(input.battle, input.actorUnitId))
	{
		return reject("NOT_AI_CONTROLLED", "MC hostile is not assigned to Aster in this battle.");
	}

	CompanionSoldierOwnershipCore::RelayControlBindingContext binding = {};
	binding.battleSessionId = input.battleSessionId;
	binding.campaignId = input.relayCampaignId;
	binding.tacticalBattleId = input.relayTacticalBattleId;
	if (!relayAiMcHostileAssigned(input.controlFileJson, input.controlFileReadOk, binding, input.actorUnitId))
	{
		return reject("NOT_AI_CONTROLLED", "MC hostile is not Aster-controlled in relay binding.");
	}

	MoveValidationResult bindingVerdict = validateBindingFreshness(input);
	if (!bindingVerdict.accepted)
	{
		return bindingVerdict;
	}

	return accept();
}

MoveValidationResult validateMcTacticalCommandExecutionPreflight(const McRemoteAuthorityInput &input)
{
	MoveValidationResult authority = evaluateMcRemoteCommandAuthority(input);
	if (!authority.accepted)
	{
		return authority;
	}
	if (!mcTacticalCommandExecutionSupported())
	{
		return reject("MC_EXECUTION_NOT_SUPPORTED", "MC hostile tactical command execution is not enabled.");
	}
	return accept();
}

MoveValidationResult validateMcMoveCommand(const McMoveValidationInput &input)
{
	MoveValidationResult authority = evaluateMcRemoteCommandAuthority(input.authority);
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

MoveValidationResult validateMcMoveExecutionPreflight(const McMoveValidationInput &input)
{
	MoveValidationResult contract = validateMcMoveCommand(input);
	if (!contract.accepted)
	{
		return contract;
	}
	if (!mcMoveRelayExecutionSupported())
	{
		return reject("MC_EXECUTION_NOT_SUPPORTED", "MC_MOVE relay execution is not enabled.");
	}
	return accept();
}

} // namespace CompanionTacticalMcHostileAuthorityCore

} // namespace OpenXcom