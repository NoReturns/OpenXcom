/*
 * Open X-Com Companion — MC hostile equipment relay (M1 MC-04).
 */
#include "CompanionTacticalMcHostileEquipmentExecutionCore.h"
#include "CompanionGroundInventoryCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/ProjectileFlyBState.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Tile.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInventory.h"
#include <map>
#include <vector>

namespace OpenXcom
{

namespace CompanionTacticalMcHostileEquipmentExecutionCore
{

namespace
{

using CompanionCommandCore::MoveValidationResult;

MoveValidationResult rejectNotSupported()
{
	MoveValidationResult result = {};
	result.accepted = false;
	result.rejectCode = "MC_EXECUTION_NOT_SUPPORTED";
	result.publicReason = "MC hostile equipment relay execution is not enabled.";
	return result;
}

template<typename ContractInput>
MoveValidationResult runEquipmentPreflightT(
	const CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput &authority,
	MoveValidationResult (*contractFn)(const ContractInput &),
	const ContractInput &contract)
{
	MoveValidationResult auth =
		CompanionTacticalMcHostileAuthorityCore::evaluateMcRemoteCommandAuthority(authority);
	if (!auth.accepted)
	{
		return auth;
	}
	MoveValidationResult contractResult = contractFn(contract);
	if (!contractResult.accepted)
	{
		return contractResult;
	}
	if (!mcEquipmentRelayExecutionSupported())
	{
		return rejectNotSupported();
	}
	MoveValidationResult ok = {};
	ok.accepted = true;
	return ok;
}

bool snapWeaponUsable(SavedBattleGame *battle, BattleItem *weapon)
{
	if (battle == 0 || weapon == 0)
	{
		return false;
	}
	return battle->getItemUsable(weapon).empty();
}

bool companionHandGrenadeSupportsPrime(BattleItem *weapon)
{
	if (weapon == 0 || weapon->getRules()->isFixed())
	{
		return false;
	}
	const BattleType bt = weapon->getRules()->getBattleType();
	return bt == BT_GRENADE || bt == BT_PROXIMITYGRENADE;
}

bool companionHandGrenadeSupportsThrow(BattleItem *weapon)
{
	return companionHandGrenadeSupportsPrime(weapon);
}

bool companionGrenadeUsable(SavedBattleGame *battle, BattleItem *weapon)
{
	if (battle == 0 || weapon == 0)
	{
		return false;
	}
	if (!battle->getItemUsable(weapon).empty())
	{
		return false;
	}
	return snapWeaponUsable(battle, weapon);
}

BattleItem *findBattleItemByIdOnUnit(BattleUnit *actor, int battleItemId)
{
	if (actor == 0 || battleItemId <= 0)
	{
		return 0;
	}
	for (std::vector<BattleItem*>::iterator i = actor->getInventory()->begin();
	     i != actor->getInventory()->end(); ++i)
	{
		if (*i != 0 && (*i)->getId() == battleItemId)
		{
			return *i;
		}
	}
	return 0;
}

bool companionReloadWeaponHeldInLeftOrRightHand(BattleUnit *actor, BattleItem *weapon)
{
	if (actor == 0 || weapon == 0)
	{
		return false;
	}
	return actor->getItem("STR_RIGHT_HAND") == weapon || actor->getItem("STR_LEFT_HAND") == weapon;
}

bool companionUnloadOtherHandClear(BattleUnit *actor, BattleItem *weapon)
{
	if (actor == 0 || weapon == 0 || weapon->getSlot() == 0)
	{
		return false;
	}
	const std::string &sid = weapon->getSlot()->getId();
	if (sid == "STR_RIGHT_HAND")
	{
		return actor->getItem("STR_LEFT_HAND") == 0;
	}
	if (sid == "STR_LEFT_HAND")
	{
		return actor->getItem("STR_RIGHT_HAND") == 0;
	}
	return false;
}

bool companionReloadAmmoCompatible(BattleItem *weapon, BattleItem *ammo)
{
	if (weapon == 0 || ammo == 0)
	{
		return false;
	}
	RuleItem *wRules = weapon->getRules();
	RuleItem *aRules = ammo->getRules();
	if (wRules == 0 || aRules == 0)
	{
		return false;
	}
	for (std::vector<std::string>::const_iterator c = wRules->getCompatibleAmmo()->begin();
	     c != wRules->getCompatibleAmmo()->end(); ++c)
	{
		if (*c == aRules->getType())
		{
			return true;
		}
	}
	return false;
}

bool companionRuntimeInventoryOverlap(BattleUnit *unit, BattleItem *movingItem, RuleInventory *destSlot, int destX, int destY)
{
	if (unit == 0 || movingItem == 0 || destSlot == 0)
	{
		return true;
	}
	if (destSlot->getType() == INV_GROUND)
	{
		return true;
	}
	for (std::vector<BattleItem*>::const_iterator i = unit->getInventory()->begin();
	     i != unit->getInventory()->end(); ++i)
	{
		if (*i == 0 || *i == movingItem)
		{
			continue;
		}
		if ((*i)->getSlot() == 0)
		{
			continue;
		}
		if ((*i)->getSlot() == destSlot && (*i)->occupiesSlot(destX, destY, movingItem))
		{
			return true;
		}
	}
	return false;
}

bool isCarriedInventorySectionString(const std::string &id)
{
	return id == "STR_RIGHT_HAND" || id == "STR_LEFT_HAND" || id == "STR_BELT" ||
	       id == "STR_BACK_PACK" || id == "STR_RIGHT_SHOULDER" || id == "STR_LEFT_SHOULDER" ||
	       id == "STR_RIGHT_LEG" || id == "STR_LEFT_LEG";
}

BattleItem *findGroundItemFromActorExport(BattleUnit *actor, int battleItemId)
{
	if (actor == 0 || battleItemId <= 0)
	{
		return 0;
	}
	Tile *tile = actor->getTile();
	if (tile == 0)
	{
		return 0;
	}
	for (std::vector<BattleItem*>::const_iterator i = tile->getInventory()->begin();
	     i != tile->getInventory()->end(); ++i)
	{
		BattleItem *item = *i;
		if (item != 0 && item->getId() == battleItemId &&
		    item->getSlot() != 0 && item->getSlot()->getType() == INV_GROUND)
		{
			return item;
		}
	}
	return 0;
}

} // namespace

bool mcEquipmentRelayExecutionSupported()
{
	return true;
}

MoveValidationResult validateMcPrimeExecutionPreflight(const McPrimeValidationInput &input)
{
	return runEquipmentPreflightT(
		input.authority, CompanionCommandCore::validatePrimeEquipmentContract, input.contract);
}

MoveValidationResult validateMcThrowExecutionPreflight(const McThrowValidationInput &input)
{
	return runEquipmentPreflightT(
		input.authority, CompanionCommandCore::validateThrowEquipmentContract, input.contract);
}

MoveValidationResult validateMcReloadExecutionPreflight(const McReloadValidationInput &input)
{
	return runEquipmentPreflightT(
		input.authority, CompanionCommandCore::validateReloadEquipmentContract, input.contract);
}

MoveValidationResult validateMcUnloadExecutionPreflight(const McUnloadValidationInput &input)
{
	return runEquipmentPreflightT(
		input.authority, CompanionCommandCore::validateUnloadEquipmentContract, input.contract);
}

MoveValidationResult validateMcInventoryMoveExecutionPreflight(const McInventoryMoveValidationInput &input)
{
	return runEquipmentPreflightT(
		input.authority, CompanionCommandCore::validateInventoryMoveEquipmentContract, input.contract);
}

MoveValidationResult validateMcPickupExecutionPreflight(const McPickupValidationInput &input)
{
	return runEquipmentPreflightT(
		input.authority, CompanionCommandCore::validatePickupEquipmentContract, input.contract);
}

MoveValidationResult validateMcDropExecutionPreflight(const McDropValidationInput &input)
{
	return runEquipmentPreflightT(
		input.authority, CompanionCommandCore::validateDropEquipmentContract, input.contract);
}

#include "CompanionTacticalMcHostileEquipmentExecutionCore_build.generated.cpp"

} // namespace CompanionTacticalMcHostileEquipmentExecutionCore

} // namespace OpenXcom