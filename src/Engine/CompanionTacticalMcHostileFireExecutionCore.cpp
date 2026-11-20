/*
 * Open X-Com Companion — MC hostile directed fire (M1 MC-03A).
 */
#include "CompanionTacticalMcHostileFireExecutionCore.h"
#include "CompanionTuVisibilityCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/ProjectileFlyBState.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Mod/RuleItem.h"

namespace OpenXcom
{

namespace CompanionTacticalMcHostileFireExecutionCore
{

namespace
{

bool weaponSupportsAction(BattleItem *weapon, BattleActionType actionType)
{
	if (weapon == 0 || weapon->getRules() == 0)
	{
		return false;
	}
	RuleItem *rules = weapon->getRules();
	if (rules->getBattleType() != BT_FIREARM)
	{
		return false;
	}
	switch (actionType)
	{
	case BA_SNAPSHOT: return rules->getAccuracySnap() != 0;
	case BA_AUTOSHOT: return rules->getAccuracyAuto() != 0;
	case BA_AIMEDSHOT: return rules->getAccuracyAimed() != 0;
	default: return false;
	}
}

bool weaponHasAmmo(BattleItem *weapon)
{
	if (weapon == 0)
	{
		return false;
	}
	if (!weapon->needsAmmo())
	{
		return true;
	}
	BattleItem *ammo = weapon->getAmmoItem();
	return ammo != 0 && ammo->getAmmoQuantity() > 0;
}

bool weaponUsable(SavedBattleGame *battle, BattleItem *weapon)
{
	if (battle == 0 || weapon == 0)
	{
		return false;
	}
	return battle->getItemUsable(weapon).empty();
}

BattleActionType actionTypeForCommandType(const std::string &commandType)
{
	if (commandType == "MC_AUTO_SHOT")
	{
		return BA_AUTOSHOT;
	}
	if (commandType == "MC_AIMED_SHOT")
	{
		return BA_AIMEDSHOT;
	}
	return BA_SNAPSHOT;
}

} // namespace

bool mcFireRelayExecutionSupported()
{
	return true;
}

CompanionCommandCore::MoveValidationResult validateMcDirectedFireContract(
	const McDirectedFireContractInput &input,
	const char *unsupportedModeReason,
	const char *insufficientTuReason)
{
	CompanionCommandCore::MoveValidationResult result = {};
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
	if (input.visibleUnitTargetDefinitelyBlocked)
	{
		result.rejectCode = "NO_LINE_OF_FIRE";
		result.publicReason = "No line of fire to visible target.";
		return result;
	}
	result.accepted = true;
	return result;
}

CompanionCommandCore::MoveValidationResult validateMcDirectedFireExecutionPreflight(
	const McDirectedFireValidationInput &input,
	const char *unsupportedModeReason,
	const char *insufficientTuReason)
{
	CompanionCommandCore::MoveValidationResult authority =
		CompanionTacticalMcHostileAuthorityCore::evaluateMcRemoteCommandAuthority(input.authority);
	if (!authority.accepted)
	{
		return authority;
	}
	CompanionCommandCore::MoveValidationResult contract = validateMcDirectedFireContract(
		input.contract, unsupportedModeReason, insufficientTuReason);
	if (!contract.accepted)
	{
		return contract;
	}
	if (!mcFireRelayExecutionSupported())
	{
		CompanionCommandCore::MoveValidationResult result = {};
		result.accepted = false;
		result.rejectCode = "MC_EXECUTION_NOT_SUPPORTED";
		result.publicReason = "MC hostile fire relay execution is not enabled.";
		return result;
	}
	CompanionCommandCore::MoveValidationResult ok = {};
	ok.accepted = true;
	return ok;
}

McDirectedFireContractInput buildMcDirectedFireContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	BattleUnit *actor,
	const CompanionCommandCore::SnapShotCommandParsed &parsed)
{
	McDirectedFireContractInput input = {};
	if (battle != 0)
	{
		input.mapSizeX = battle->getMapSizeX();
		input.mapSizeY = battle->getMapSizeY();
		input.mapSizeZ = battle->getMapSizeZ();
	}
	input.targetX = parsed.targetX;
	input.targetY = parsed.targetY;
	input.targetZ = parsed.targetZ;
	input.activeHandValid = parsed.activeHand == "STR_LEFT_HAND" || parsed.activeHand == "STR_RIGHT_HAND";
	BattleItem *weapon = actor != 0 ? actor->getItem(parsed.activeHand) : 0;
	input.weaponPresentInHand = weapon != 0;
	const BattleActionType actionType = actionTypeForCommandType(parsed.commandType);
	input.weaponSupportsFireMode = weaponSupportsAction(weapon, actionType);
	input.weaponHasAmmo = weaponHasAmmo(weapon);
	input.weaponUsable = weaponUsable(battle, weapon);
	const int fireTu = actor != 0 && weapon != 0 ? actor->getActionTUs(actionType, weapon) : 0;
	input.insufficientTuForFireMode = actor == 0 || weapon == 0 || fireTu <= 0 || game == 0 ||
		!game->checkReservedTU(actor, fireTu, true);
	input.visibleUnitTargetDefinitelyBlocked = false;
	if (actor != 0 && weapon != 0 && battle != 0 && game != 0)
	{
		const Position target(parsed.targetX, parsed.targetY, parsed.targetZ);
		if (target.x >= 0 && target.y >= 0 && target.z >= 0 &&
		    target.x < battle->getMapSizeX() && target.y < battle->getMapSizeY() && target.z < battle->getMapSizeZ())
		{
			BattleAction probe = {};
			probe.type = actionType;
			probe.actor = actor;
			probe.weapon = weapon;
			probe.target = target;
			probe.autoShotCounter = 0;
			input.visibleUnitTargetDefinitelyBlocked =
				ProjectileFlyBState::companionPreflightVisibleUnitTargetBlocked(
					probe, battle, game, actor->getPosition());
		}
	}
	return input;
}

bool tryStartMcDirectedFire(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	BattleItem *weapon,
	BattleActionType actionType,
	std::string *rejectReason)
{
	if (game == 0 || actor == 0 || weapon == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle context missing.";
		}
		return false;
	}
	return game->tryExecuteCompanionDirectedFire(actor, target, weapon, actionType, rejectReason);
}

int estimateFacingStepsForMcFire(BattleUnit *actor, const Position &target)
{
	if (actor == 0)
	{
		return -1;
	}
	return CompanionTuVisibilityCore::estimateDirectedFireFacingSteps(actor, target);
}

} // namespace CompanionTacticalMcHostileFireExecutionCore

} // namespace OpenXcom