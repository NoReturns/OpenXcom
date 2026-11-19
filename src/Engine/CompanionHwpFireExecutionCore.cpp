/*
 * Open X-Com Companion — HWP directed fire (M1 HWP-S5-1b).
 */
#include "CompanionHwpFireExecutionCore.h"
#include "CompanionTacticalHwpAuthorityCore.h"
#include "CompanionTuVisibilityCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/ProjectileFlyBState.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Mod/RuleItem.h"

namespace OpenXcom
{

namespace CompanionHwpFireExecutionCore
{

static BattleActionType actionTypeForFireMode(const std::string &fireMode, std::string *rejectReason)
{
	if (fireMode == "snap")
	{
		return BA_SNAPSHOT;
	}
	if (fireMode == "aimed")
	{
		return BA_AIMEDSHOT;
	}
	if (rejectReason != 0)
	{
		*rejectReason = "fire_mode must be snap or aimed.";
	}
	return BA_NONE;
}

namespace
{

void appendInventoryItemsWithId(BattleUnit *actor, int battleItemId, std::vector<BattleItem *> *matches)
{
	if (actor == 0 || matches == 0 || actor->getInventory() == 0)
	{
		return;
	}
	for (std::vector<BattleItem *>::iterator i = actor->getInventory()->begin();
	     i != actor->getInventory()->end();
	     ++i)
	{
		if (*i != 0 && (*i)->getId() == battleItemId)
		{
			matches->push_back(*i);
		}
	}
}

void appendSpecialFixedFirearm(BattleUnit *actor, int battleItemId, std::vector<BattleItem *> *matches)
{
	if (actor == 0 || matches == 0)
	{
		return;
	}
	BattleItem *spec = actor->getSpecialWeapon(BT_FIREARM);
	if (spec != 0 && spec->getId() == battleItemId)
	{
		matches->push_back(spec);
	}
}

void collectFixedFirearms(BattleUnit *actor, std::vector<BattleItem *> *fixed)
{
	if (actor == 0 || fixed == 0)
	{
		return;
	}
	if (actor->getInventory() != 0)
	{
		for (std::vector<BattleItem *>::iterator i = actor->getInventory()->begin();
		     i != actor->getInventory()->end();
		     ++i)
		{
			if (*i != 0 && (*i)->getRules() != 0 && (*i)->getRules()->isFixed()
			    && (*i)->getRules()->getBattleType() == BT_FIREARM)
			{
				fixed->push_back(*i);
			}
		}
	}
	BattleItem *spec = actor->getSpecialWeapon(BT_FIREARM);
	if (spec != 0 && spec->getRules() != 0 && spec->getRules()->isFixed())
	{
		fixed->push_back(spec);
	}
}

BattleItem *resolveWeaponById(BattleUnit *actor, int battleItemId)
{
	std::vector<BattleItem *> matches;
	appendInventoryItemsWithId(actor, battleItemId, &matches);
	appendSpecialFixedFirearm(actor, battleItemId, &matches);
	if (matches.size() == 1)
	{
		return matches.front();
	}
	if (battleItemId == 0)
	{
		std::vector<BattleItem *> fixed;
		collectFixedFirearms(actor, &fixed);
		if (fixed.size() == 1)
		{
			return fixed.front();
		}
	}
	return 0;
}

} // namespace

BattleItem *findBattleItemOnActorById(BattleUnit *actor, int battleItemId)
{
	return resolveWeaponById(actor, battleItemId);
}

bool isFriendlyHwpBattleActor(const BattleUnit *unit)
{
	return CompanionTacticalHwpAuthorityCore::isOperationalFriendlyHwpForRemoteCommands(unit);
}

bool preflightHwpFire(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	const std::string &fireMode,
	int weaponBattleItemId,
	std::string *rejectReason,
	int *facingStepsOut)
{
	if (facingStepsOut != 0)
	{
		*facingStepsOut = -1;
	}
	if (game == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle context missing.";
		}
		return false;
	}
	if (!isFriendlyHwpBattleActor(actor))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Actor is not an operational friendly HWP.";
		}
		return false;
	}
	if (actor->getGeoscapeSoldier() != 0 || actor->getId() < BattleUnit::MAX_SOLDIER_ID)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Actor is a roster soldier, not a friendly HWP.";
		}
		return false;
	}
	const BattleActionType actionType = actionTypeForFireMode(fireMode, rejectReason);
	if (actionType == BA_NONE)
	{
		return false;
	}

	BattleItem *weapon = resolveWeaponById(actor, weaponBattleItemId);
	if (weapon == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "weapon_battle_item_id must match exactly one fixed weapon on HWP.";
		}
		return false;
	}
	if (weapon->getRules() == 0 || !weapon->getRules()->isFixed())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Resolved weapon is not a fixed HWP firearm.";
		}
		return false;
	}
	if (facingStepsOut != 0)
	{
		*facingStepsOut = CompanionTuVisibilityCore::estimateDirectedFireFacingSteps(actor, target);
	}
	return game->preflightCompanionDirectedFire(actor, target, weapon, actionType, rejectReason);
}

bool tryStartHwpFire(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	const std::string &fireMode,
	int weaponBattleItemId,
	std::string *rejectReason,
	HwpFireStartOutcome *outcome)
{
	if (outcome != 0)
	{
		outcome->started = false;
		outcome->rejectReason.clear();
		outcome->tuAtAcceptance = actor != 0 ? actor->getTimeUnits() : -1;
		outcome->facingStepsEstimated = -1;
	}
	const int tuBefore = actor != 0 ? actor->getTimeUnits() : -1;

	int facingSteps = -1;
	if (!preflightHwpFire(game, actor, target, fireMode, weaponBattleItemId, rejectReason, &facingSteps))
	{
		if (outcome != 0 && rejectReason != 0)
		{
			outcome->rejectReason = *rejectReason;
		}
		return false;
	}
	BattleItem *weapon = findBattleItemOnActorById(actor, weaponBattleItemId);
	const BattleActionType actionType = actionTypeForFireMode(fireMode, 0);
	if (weapon == 0 || actionType == BA_NONE)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Internal weapon resolve failed.";
		}
		return false;
	}

	const int ammoBefore = weapon->needsAmmo() && weapon->getAmmoItem() != 0
		? weapon->getAmmoItem()->getAmmoQuantity() : -1;

	const bool started = game->tryExecuteCompanionDirectedFire(actor, target, weapon, actionType, rejectReason);
	if (!started)
	{
		if (outcome != 0 && rejectReason != 0)
		{
			outcome->rejectReason = *rejectReason;
		}
		return false;
	}
	if (outcome != 0)
	{
		outcome->started = true;
		outcome->tuAtAcceptance = tuBefore;
		outcome->facingStepsEstimated = facingSteps;
	}
	if (game->isBusy())
	{
		// Started asynchronous turn+projectile stack — not a completed shot.
		return true;
	}
	if (rejectReason != 0)
	{
		*rejectReason = "Directed fire did not queue battle states.";
	}
	if (ammoBefore >= 0 && weapon->getAmmoItem() != 0)
	{
		(void)ammoBefore;
	}
	return false;
}

CompanionCommandCore::MoveValidationResult buildHwpFireContractValidation(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	const std::string &fireMode,
	int weaponBattleItemId)
{
	CompanionCommandCore::HwpFireValidationInput input = {};
	SavedBattleGame *battle = game != 0 ? game->getSave() : 0;
	if (battle != 0)
	{
		input.mapSizeX = battle->getMapSizeX();
		input.mapSizeY = battle->getMapSizeY();
		input.mapSizeZ = battle->getMapSizeZ();
	}
	input.targetX = target.x;
	input.targetY = target.y;
	input.targetZ = target.z;

	std::string reject;
	input.invalidFireMode = actionTypeForFireMode(fireMode, &reject) == BA_NONE;
	if (actor != 0)
	{
		input.actorIsRosterSoldier = actor->getGeoscapeSoldier() != 0
			|| actor->getId() < BattleUnit::MAX_SOLDIER_ID;
	}
	BattleItem *weapon = actor != 0 ? resolveWeaponById(actor, weaponBattleItemId) : 0;
	input.weaponResolved = weapon != 0;
	input.weaponBelongsToActor = weapon != 0 && weapon->getOwner() == actor;
	const BattleActionType actionType = actionTypeForFireMode(fireMode, 0);
	if (weapon != 0 && actionType != BA_NONE)
	{
		RuleItem *rules = weapon->getRules();
		input.weaponSupportsFireMode = rules != 0 && rules->getBattleType() == BT_FIREARM
			&& ((actionType == BA_SNAPSHOT && rules->getAccuracySnap() != 0)
				|| (actionType == BA_AIMEDSHOT && rules->getAccuracyAimed() != 0));
		if (battle != 0)
		{
			input.weaponUsable = battle->getItemUsable(weapon).empty();
		}
		if (!weapon->needsAmmo())
		{
			input.weaponHasAmmo = true;
		}
		else
		{
			BattleItem *ammo = weapon->getAmmoItem();
			input.weaponHasAmmo = ammo != 0 && ammo->getAmmoQuantity() > 0;
		}
		if (actor != 0 && game != 0)
		{
			const int fireTu = actor->getActionTUs(actionType, weapon);
			input.insufficientTuForFireMode = fireTu <= 0 || fireTu > actor->getTimeUnits()
				|| !game->checkReservedTU(actor, fireTu, true);
		}
		if (game != 0 && game->getPanicHandled())
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
	return CompanionCommandCore::validateHwpFireContract(input);
}

} // namespace CompanionHwpFireExecutionCore

} // namespace OpenXcom