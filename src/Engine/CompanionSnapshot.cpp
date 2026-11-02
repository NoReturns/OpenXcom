/*
 * Open X-Com Companion — read-only battlescape snapshot export (opt-in).
 */
#include "CompanionSnapshot.h"
#include "CompanionEventLog.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/SavedGame.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleItem.h"
#include "CrossPlatform.h"
#include "Logger.h"
#include <yaml-cpp/yaml.h>
#include <cstdlib>
#include <fstream>
#include <string>

namespace OpenXcom
{

namespace
{

const int SCHEMA_VERSION = 4;
const size_t MAX_INVENTORY_ENTRIES = 24;

bool shouldIncludeUnit(const BattleUnit *unit)
{
	if (unit == 0 || unit->isOut())
	{
		return false;
	}
	if (unit->getFaction() == FACTION_PLAYER)
	{
		return true;
	}
	return unit->getVisible();
}

bool exportFriendlyTacticalDetails(const BattleUnit *unit)
{
	return unit->getOriginalFaction() == FACTION_PLAYER
		&& unit->getFaction() == FACTION_PLAYER
		&& unit->getGeoscapeSoldier() != 0;
}

std::string friendlySoldierDisplayName(const BattleUnit *unit)
{
	Soldier *soldier = unit->getGeoscapeSoldier();
	if (soldier == 0)
	{
		return std::string();
	}
	return soldier->getName(true);
}

bool isHandInventorySlot(const BattleItem *item)
{
	if (item == 0 || item->getSlot() == 0)
	{
		return false;
	}
	if (item->getSlot()->getType() == INV_HAND)
	{
		return true;
	}
	const std::string &slotId = item->getSlot()->getId();
	return slotId == "STR_RIGHT_HAND" || slotId == "STR_LEFT_HAND";
}

YAML::Node nullNode()
{
	return YAML::Node(YAML::NodeType::Null);
}

bool isFirearmWithChamberedAmmo(BattleItem *item)
{
	if (item == 0)
	{
		return false;
	}
	return item->getRules()->getBattleType() == BT_FIREARM && item->needsAmmo();
}

void appendFirearmAmmoState(YAML::Node &entry, BattleItem *item)
{
	if (!isFirearmWithChamberedAmmo(item))
	{
		return;
	}

	BattleItem *ammo = item->getAmmoItem();
	if (ammo == 0 || ammo->getAmmoQuantity() == 0)
	{
		entry["empty"] = true;
		return;
	}

	YAML::Node loaded;
	loaded["ammoType"] = ammo->getRules()->getType();
	loaded["rounds"] = ammo->getAmmoQuantity();
	entry["loaded"] = loaded;
}

void appendGrenadeState(YAML::Node &entry, BattleItem *item)
{
	if (item == 0)
	{
		return;
	}
	const BattleType bt = item->getRules()->getBattleType();
	if (bt == BT_GRENADE || bt == BT_PROXIMITYGRENADE)
	{
		entry["primed"] = item->getFuseTimer() >= 0;
	}
}

YAML::Node buildEquipmentItemNode(BattleItem *item)
{
	YAML::Node entry;
	entry["itemType"] = item->getRules()->getType();
	entry["battleType"] = static_cast<int>(item->getRules()->getBattleType());
	appendFirearmAmmoState(entry, item);
	appendGrenadeState(entry, item);
	return entry;
}

YAML::Node buildHandSlotNode(BattleItem *item)
{
	if (item == 0)
	{
		return nullNode();
	}
	return buildEquipmentItemNode(item);
}

void appendInventoryEntries(YAML::Node &inventory, BattleUnit *unit)
{
	size_t count = 0;
	for (std::vector<BattleItem*>::const_iterator i = unit->getInventory()->begin();
		i != unit->getInventory()->end() && count < MAX_INVENTORY_ENTRIES; ++i)
	{
		BattleItem *item = *i;
		if (item == 0 || isHandInventorySlot(item))
		{
			continue;
		}
		if (item->getSlot() == 0)
		{
			continue;
		}

		YAML::Node entry;
		entry["slot"] = item->getSlot()->getId();
		entry["itemType"] = item->getRules()->getType();
		entry["battleType"] = static_cast<int>(item->getRules()->getBattleType());
		appendFirearmAmmoState(entry, item);
		appendGrenadeState(entry, item);
		inventory.push_back(entry);
		++count;
	}
}

void appendFriendlyEquipment(YAML::Node &entry, BattleUnit *unit)
{
	entry["kneeled"] = unit->isKneeled();
	entry["activeHand"] = unit->getActiveHand();

	YAML::Node hands;
	hands["right"] = buildHandSlotNode(unit->getItem("STR_RIGHT_HAND"));
	hands["left"] = buildHandSlotNode(unit->getItem("STR_LEFT_HAND"));
	entry["hands"] = hands;

	YAML::Node inventory = YAML::Node(YAML::NodeType::Sequence);
	appendInventoryEntries(inventory, unit);
	entry["inventory"] = inventory;
}

bool companionEnvironmentUsable(SavedBattleGame *battle, BattleItem *weapon)
{
	if (battle == 0 || weapon == 0)
	{
		return true;
	}
	return battle->getItemUsable(weapon).empty();
}

bool companionResearchUsable(SavedBattleGame *battle, BattleItem *weapon, BattleActionType actionType)
{
	if (actionType == BA_THROW || battle == 0 || weapon == 0)
	{
		return true;
	}
	SavedGame *geoscape = battle->getGeoscapeSave();
	if (geoscape == 0)
	{
		return true;
	}
	return geoscape->isResearched(weapon->getRules()->getRequirements());
}

bool companionFirearmHasAmmo(BattleItem *weapon)
{
	if (weapon == 0 || weapon->getRules()->getBattleType() != BT_FIREARM)
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

void appendActionEntry(YAML::Node &actions, BattleUnit *unit, BattleItem *weapon, BattleActionType type, const char *id, bool usable)
{
	const int tu = unit->getActionTUs(type, weapon);
	YAML::Node entry;
	entry["id"] = id;
	entry["actionType"] = static_cast<int>(type);
	entry["tuCost"] = tu;
	entry["canAffordTu"] = tu <= unit->getTimeUnits();
	entry["usable"] = usable;
	actions.push_back(entry);
}

void appendHandActionCosts(YAML::Node &handCosts, SavedBattleGame *battle, BattleUnit *unit, BattleItem *weapon)
{
	YAML::Node actions = YAML::Node(YAML::NodeType::Sequence);
	if (weapon == 0)
	{
		handCosts["actions"] = actions;
		return;
	}

	handCosts["itemType"] = weapon->getRules()->getType();
	RuleItem *rules = weapon->getRules();
	const bool envOk = companionEnvironmentUsable(battle, weapon);

	if (!rules->isFixed())
	{
		appendActionEntry(actions, unit, weapon, BA_THROW, "throw", envOk);
	}

	if ((rules->getBattleType() == BT_GRENADE || rules->getBattleType() == BT_PROXIMITYGRENADE)
		&& weapon->getFuseTimer() == -1)
	{
		appendActionEntry(actions, unit, weapon, BA_PRIME, "prime", envOk && companionResearchUsable(battle, weapon, BA_PRIME));
	}

	if (rules->getBattleType() == BT_FIREARM)
	{
		const bool ammoOk = companionFirearmHasAmmo(weapon);
		const bool researchOk = envOk && companionResearchUsable(battle, weapon, BA_SNAPSHOT);
		if (rules->getWaypoints() != 0
			|| (weapon->getAmmoItem() != 0 && weapon->getAmmoItem()->getRules()->getWaypoints() != 0))
		{
			appendActionEntry(actions, unit, weapon, BA_LAUNCH, "launch", researchOk && ammoOk);
		}
		else
		{
			if (rules->getAccuracyAuto() != 0)
			{
				appendActionEntry(actions, unit, weapon, BA_AUTOSHOT, "auto", researchOk && ammoOk);
			}
			if (rules->getAccuracySnap() != 0)
			{
				appendActionEntry(actions, unit, weapon, BA_SNAPSHOT, "snap", researchOk && ammoOk);
			}
			if (rules->getAccuracyAimed() != 0)
			{
				appendActionEntry(actions, unit, weapon, BA_AIMEDSHOT, "aimed", researchOk && ammoOk);
			}
		}
	}

	if (rules->getTUMelee() > 0)
	{
		appendActionEntry(actions, unit, weapon, BA_HIT, "melee", envOk && companionResearchUsable(battle, weapon, BA_HIT));
	}

	handCosts["actions"] = actions;
}

void appendFriendlyActionCosts(YAML::Node &entry, SavedBattleGame *battle, BattleUnit *unit)
{
	YAML::Node costs;
	YAML::Node posture;
	posture["kneelDownTu"] = 4;
	posture["standUpTu"] = 8;
	costs["posture"] = posture;

	YAML::Node hands;
	YAML::Node rightHand;
	YAML::Node leftHand;
	appendHandActionCosts(rightHand, battle, unit, unit->getItem("STR_RIGHT_HAND"));
	appendHandActionCosts(leftHand, battle, unit, unit->getItem("STR_LEFT_HAND"));
	hands["right"] = rightHand;
	hands["left"] = leftHand;
	costs["hands"] = hands;

	entry["actionCosts"] = costs;
}

void appendUnit(YAML::Node &units, SavedBattleGame *battle, BattleUnit *unit)
{
	YAML::Node entry;
	entry["id"] = unit->getId();
	entry["faction"] = static_cast<int>(unit->getFaction());
	entry["originalFaction"] = static_cast<int>(unit->getOriginalFaction());
	entry["position"] = unit->getPosition();

	if (exportFriendlyTacticalDetails(unit))
	{
		entry["name"] = friendlySoldierDisplayName(unit);
		entry["health"] = unit->getHealth();
		entry["timeUnits"] = unit->getTimeUnits();
		appendFriendlyEquipment(entry, unit);
		appendFriendlyActionCosts(entry, battle, unit);
	}

	units.push_back(entry);
}

std::string weaponFireActionTypeId(BattleActionType type)
{
	switch (type)
	{
	case BA_SNAPSHOT:
		return "snap";
	case BA_AIMEDSHOT:
		return "aimed";
	case BA_AUTOSHOT:
		return "auto";
	case BA_LAUNCH:
		return "launch";
	default:
		return "snap";
	}
}

void appendEventLog(YAML::Node &root, const CompanionEventLog &log)
{
	YAML::Node eventLog;
	eventLog["sessionId"] = log.getSessionId();
	eventLog["oldestSeq"] = log.getOldestSeq();
	eventLog["latestSeq"] = log.getLatestSeq();
	eventLog["droppedCount"] = log.getDroppedCount();

	YAML::Node events(YAML::NodeType::Sequence);
	for (std::vector<CompanionWeaponFireEvent>::const_iterator i = log.getEvents().begin(); i != log.getEvents().end(); ++i)
	{
		YAML::Node ev;
		ev["seq"] = i->seq;
		ev["type"] = "weapon_fired";
		ev["turn"] = i->turn;
		ev["actorUnitId"] = i->actorUnitId;
		ev["weaponItemType"] = i->weaponItemType;
		ev["actionType"] = weaponFireActionTypeId(i->actionType);
		ev["target"] = i->target;
		if (i->actionType == BA_AUTOSHOT)
		{
			ev["autoShotIndex"] = i->autoShotIndex;
		}
		events.push_back(ev);
	}
	eventLog["events"] = events;
	root["eventLog"] = eventLog;
}

YAML::Node buildSnapshot(SavedBattleGame *battle)
{
	YAML::Node root;
	root["schemaVersion"] = SCHEMA_VERSION;
	root["turn"] = battle->getTurn();
	root["side"] = static_cast<int>(battle->getSide());
	BattleUnit *selected = battle->getSelectedUnit();
	root["selectedUnitId"] = selected ? selected->getId() : -1;

	YAML::Node map;
	map["width"] = battle->getMapSizeX();
	map["height"] = battle->getMapSizeY();
	map["depth"] = battle->getMapSizeZ();
	root["map"] = map;

	YAML::Node units = YAML::Node(YAML::NodeType::Sequence);
	for (std::vector<BattleUnit*>::const_iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		BattleUnit *unit = *i;
		if (!shouldIncludeUnit(unit))
		{
			continue;
		}
		appendUnit(units, battle, unit);
	}
	root["units"] = units;
	appendEventLog(root, battle->getCompanionEventLog());
	return root;
}

bool writeSnapshotAtomically(const std::string &destPath, const std::string &yamlContent)
{
	const std::string tmpPath = destPath + ".companion.tmp";

	{
		std::ofstream out(tmpPath.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
		if (!out)
		{
			Log(LOG_WARNING) << "Companion snapshot: failed to open temporary output file";
			return false;
		}
		out << yamlContent;
		out.flush();
		if (!out)
		{
			Log(LOG_WARNING) << "Companion snapshot: failed to write temporary output file";
			CrossPlatform::deleteFile(tmpPath);
			return false;
		}
	}

	if (!CrossPlatform::moveFile(tmpPath, destPath))
	{
		Log(LOG_WARNING) << "Companion snapshot: failed to replace destination file";
		CrossPlatform::deleteFile(tmpPath);
		return false;
	}

	return true;
}

} // namespace

void CompanionSnapshot::tryExport(SavedBattleGame *battle)
{
	if (battle == 0)
	{
		return;
	}

	const char *path = std::getenv("OPENXCOM_COMPANION_SNAPSHOT_PATH");
	if (path == 0 || path[0] == '\0')
	{
		return;
	}

	if (battle->getDebugMode())
	{
		return;
	}

	if (battle->getSide() != FACTION_PLAYER || battle->getUnitsFalling())
	{
		return;
	}

	YAML::Node root = buildSnapshot(battle);
	YAML::Emitter emitter;
	emitter << root;

	if (!writeSnapshotAtomically(path, emitter.c_str()))
	{
		return;
	}
}

}
