/*
 * Open X-Com Companion — HWP guided launch (M1 HWP-S6-GUIDED).
 */
#include "CompanionHwpGuidedLaunchExecutionCore.h"
#include "CompanionHwpFireExecutionCore.h"
#include "CompanionTacticalHwpAuthorityCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Mod/RuleItem.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"

namespace OpenXcom
{

namespace CompanionHwpGuidedLaunchExecutionCore
{

bool fixedWeaponSupportsGuidedLaunch(BattleItem *weapon)
{
	if (weapon == 0 || weapon->getRules() == 0 || weapon->getRules()->getBattleType() != BT_FIREARM)
	{
		return false;
	}
	RuleItem *rules = weapon->getRules();
	if (rules->getWaypoints() != 0)
	{
		return true;
	}
	BattleItem *ammo = weapon->getAmmoItem();
	return ammo != 0 && ammo->getRules() != 0 && ammo->getRules()->getWaypoints() != 0;
}

int guidedLaunchWaypointLimit(BattleItem *weapon)
{
	if (weapon == 0)
	{
		return 0;
	}
	int maxWaypoints = weapon->getRules()->getWaypoints();
	if (maxWaypoints == 0)
	{
		BattleItem *ammo = weapon->getAmmoItem();
		if (ammo != 0 && ammo->getRules() != 0)
		{
			maxWaypoints = ammo->getRules()->getWaypoints();
		}
	}
	return maxWaypoints;
}

bool preflightHwpGuidedLaunch(
	BattlescapeGame *game,
	BattleUnit *actor,
	const std::vector<Position> &waypoints,
	int weaponBattleItemId,
	std::string *rejectReason)
{
	if (game == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle context missing.";
		}
		return false;
	}
	if (!CompanionHwpFireExecutionCore::isFriendlyHwpBattleActor(actor))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Actor is not an operational friendly HWP.";
		}
		return false;
	}
	if (waypoints.empty())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "No waypoints supplied.";
		}
		return false;
	}
	if (game->isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}
	BattleItem *weapon = CompanionHwpFireExecutionCore::findBattleItemOnActorById(actor, weaponBattleItemId);
	if (weapon == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "weapon_battle_item_id must match exactly one fixed weapon on HWP.";
		}
		return false;
	}
	if (!fixedWeaponSupportsGuidedLaunch(weapon))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon does not support guided launch.";
		}
		return false;
	}
	SavedBattleGame *battle = game->getSave();
	if (battle != 0 && !battle->getItemUsable(weapon).empty())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon is not usable.";
		}
		return false;
	}
	if (weapon->needsAmmo())
	{
		BattleItem *ammo = weapon->getAmmoItem();
		if (ammo == 0 || ammo->getAmmoQuantity() <= 0)
		{
			if (rejectReason != 0)
			{
				*rejectReason = "Guided launcher has no ammunition loaded.";
			}
			return false;
		}
	}
	if (battle != 0)
	{
		for (std::vector<Position>::const_iterator wp = waypoints.begin(); wp != waypoints.end(); ++wp)
		{
			if (wp->x < 0 || wp->y < 0 || wp->z < 0 ||
			    wp->x >= battle->getMapSizeX() || wp->y >= battle->getMapSizeY() || wp->z >= battle->getMapSizeZ())
			{
				if (rejectReason != 0)
				{
					*rejectReason = "One or more waypoints are out of map bounds.";
				}
				return false;
			}
		}
	}
	const int maxWaypoints = guidedLaunchWaypointLimit(weapon);
	if (maxWaypoints != -1 && static_cast<int>(waypoints.size()) > maxWaypoints)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Waypoint count exceeds ruleset maximum.";
		}
		return false;
	}
	const int launchTu = actor->getActionTUs(BA_LAUNCH, weapon);
	if (launchTu <= 0 || launchTu > actor->getTimeUnits() || !game->checkReservedTU(actor, launchTu, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units for guided launch.";
		}
		return false;
	}
	return true;
}

bool tryStartHwpGuidedLaunch(
	BattlescapeGame *game,
	BattleUnit *actor,
	const std::vector<Position> &waypoints,
	int weaponBattleItemId,
	std::string *rejectReason,
	HwpGuidedLaunchStartOutcome *outcome)
{
	if (outcome != 0)
	{
		outcome->started = false;
		outcome->rejectReason.clear();
		outcome->tuAtAcceptance = actor != 0 ? actor->getTimeUnits() : -1;
	}
	BattleItem *weapon = actor != 0
		? CompanionHwpFireExecutionCore::findBattleItemOnActorById(actor, weaponBattleItemId)
		: 0;
	const int tuBefore = actor != 0 ? actor->getTimeUnits() : -1;

	if (!preflightHwpGuidedLaunch(game, actor, waypoints, weaponBattleItemId, rejectReason))
	{
		if (outcome != 0 && rejectReason != 0)
		{
			outcome->rejectReason = *rejectReason;
		}
		return false;
	}
	std::string localReject;
	if (!CompanionHwpFireExecutionCore::isFriendlyHwpBattleActor(actor) || weapon == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Internal weapon resolve failed.";
		}
		return false;
	}
	const bool started = game->tryExecuteCompanionLaunch(actor, waypoints, weapon, &localReject);
	if (!started)
	{
		if (rejectReason != 0)
		{
			*rejectReason = localReject;
		}
		if (outcome != 0)
		{
			outcome->rejectReason = localReject;
		}
		return false;
	}
	if (outcome != 0)
	{
		outcome->started = true;
		outcome->tuAtAcceptance = tuBefore;
	}
	if (game->isBusy())
	{
		return true;
	}
	if (rejectReason != 0)
	{
		*rejectReason = "Guided launch did not queue battle states.";
	}
	return false;
}

CompanionCommandCore::MoveValidationResult buildHwpGuidedLaunchContractValidation(
	BattlescapeGame *game,
	BattleUnit *actor,
	const std::vector<Position> &waypoints,
	int weaponBattleItemId)
{
	CompanionCommandCore::HwpGuidedLaunchValidationInput input = {};
	SavedBattleGame *battle = game != 0 ? game->getSave() : 0;
	if (battle != 0)
	{
		input.mapSizeX = battle->getMapSizeX();
		input.mapSizeY = battle->getMapSizeY();
		input.mapSizeZ = battle->getMapSizeZ();
	}
	if (actor != 0)
	{
		input.actorIsRosterSoldier = actor->getGeoscapeSoldier() != 0
			|| actor->getId() < BattleUnit::MAX_SOLDIER_ID;
	}
	input.waypoints = waypoints;
	input.waypointOutOfBounds = false;
	for (std::vector<Position>::const_iterator wp = waypoints.begin(); wp != waypoints.end(); ++wp)
	{
		if (wp->x < 0 || wp->y < 0 || wp->z < 0 ||
		    wp->x >= input.mapSizeX || wp->y >= input.mapSizeY || wp->z >= input.mapSizeZ)
		{
			input.waypointOutOfBounds = true;
			break;
		}
	}
	BattleItem *weapon = actor != 0
		? CompanionHwpFireExecutionCore::findBattleItemOnActorById(actor, weaponBattleItemId)
		: 0;
	input.weaponResolved = weapon != 0;
	input.weaponBelongsToActor = weapon != 0 && weapon->getOwner() == actor;
	input.weaponSupportsGuidedLaunch = fixedWeaponSupportsGuidedLaunch(weapon);
	if (battle != 0 && weapon != 0)
	{
		input.weaponUsable = battle->getItemUsable(weapon).empty();
	}
	if (weapon != 0)
	{
		if (!weapon->needsAmmo())
		{
			input.weaponHasAmmo = true;
		}
		else
		{
			BattleItem *ammo = weapon->getAmmoItem();
			input.weaponHasAmmo = ammo != 0 && ammo->getAmmoQuantity() > 0;
		}
		const int maxWaypoints = guidedLaunchWaypointLimit(weapon);
		input.waypointCountExceedsLimit = maxWaypoints != -1
			&& static_cast<int>(waypoints.size()) > maxWaypoints;
		if (actor != 0 && game != 0)
		{
			const int launchTu = actor->getActionTUs(BA_LAUNCH, weapon);
			input.insufficientTuForLaunch = launchTu <= 0 || launchTu > actor->getTimeUnits()
				|| !game->checkReservedTU(actor, launchTu, true);
		}
	}
	return CompanionCommandCore::validateHwpGuidedLaunchContract(input);
}

} // namespace CompanionHwpGuidedLaunchExecutionCore

} // namespace OpenXcom