/*
 * Open X-Com Companion — read-only battlescape snapshot export (opt-in).
 */
#include "CompanionSnapshot.h"
#include "CompanionSnapshotBuild.h"
#include "CompanionCommandRuntime.h"
#include "CompanionRelayPublicationFreshnessCore.h"
#include "CompanionCommandRuntimeSupport.h"
#include "CompanionSnapshotExportCore.h"
#include "CompanionManualV5Export.h"
#include "CompanionSoldierControlPublishCore.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionSnapshotTerrain.h"
#include "CompanionSnapshotMissionContext.h"
#include "CompanionSnapshotUnitState.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Engine/Game.h"
#include "../Engine/Language.h"
#include "CompanionSnapshotGroundItems.h"
#include "CompanionGroundInventoryCore.h"
#include "CompanionItemResearchEligibilityCore.h"
#include "CompanionSha256.h"
#include "CompanionEventLog.h"
#include "CompanionPerceptionLog.h"
#include "CompanionPerceptionLogCore.h"
#include "../Savegame/Tile.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/SavedGame.h"
#include "../Mod/Mod.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleItem.h"
#include "CrossPlatform.h"
#include "Logger.h"
#include <yaml-cpp/yaml.h>
#include <chrono>
#include <cstdlib>
#include <string>
#include <algorithm>
#include <vector>
#include <cmath>

namespace OpenXcom
{

namespace
{

const int SCHEMA_VERSION_V4 = 4;
const int SCHEMA_VERSION_V5 = 5;
const size_t MAX_INVENTORY_ENTRIES = 24;
const size_t MAX_GROUND_INVENTORY_ENTRIES_PER_UNIT = 16;

#ifdef _WIN32
const char kControlPathSep = '\\';
#else
const char kControlPathSep = '/';
#endif

std::string joinControlPath(const std::string &dir, const char *name)
{
	if (dir.empty())
	{
		return std::string(name);
	}
	return dir + kControlPathSep + name;
}

Mod *modFromBattle(SavedBattleGame *battle)
{
	if (battle == 0)
	{
		return 0;
	}
	BattlescapeState *state = battle->getBattleState();
	if (state != 0 && state->getGame() != 0)
	{
		return state->getGame()->getMod();
	}
	// Unit/integration tests (and some headless fixtures) use SavedBattleGame::setTestMod only.
	return battle->getTestMod();
}

bool isPublishableLivePlayerSoldier(const BattleUnit *unit, const SavedGame *campaign)
{
	if (unit == 0 || campaign == 0 || unit->isOut())
	{
		return false;
	}
	if (unit->getOriginalFaction() != FACTION_PLAYER || unit->getFaction() != FACTION_PLAYER)
	{
		return false;
	}
	Soldier *soldier = unit->getGeoscapeSoldier();
	if (soldier == 0 || soldier->getId() != unit->getId())
	{
		return false;
	}
	if (unit->getId() <= 0 || unit->getId() >= BattleUnit::MAX_SOLDIER_ID)
	{
		return false;
	}
	return campaign->isLiveRosterSoldier(unit->getId());
}

std::vector<int> collectPublishableLivePlayerUnitIds(SavedBattleGame *battle, SavedGame *campaign)
{
	std::vector<int> ids;
	if (battle == 0 || campaign == 0)
	{
		return ids;
	}
	for (std::vector<BattleUnit*>::const_iterator i = battle->getUnits()->begin();
	     i != battle->getUnits()->end(); ++i)
	{
		if (!isPublishableLivePlayerSoldier(*i, campaign))
		{
			continue;
		}
		ids.push_back((*i)->getId());
	}
	std::sort(ids.begin(), ids.end());
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
	return ids;
}

bool tryPublishSoldierControlBesideSnapshot(
	SavedBattleGame *battle,
	SavedGame *campaign,
	const std::string &snapshotDirectoryPath)
{
	if (battle == 0 || campaign == 0 || snapshotDirectoryPath.empty())
	{
		Log(LOG_WARNING) << "Companion soldier control: publish skipped (missing battle or campaign context)";
		return false;
	}

	const std::string sessionId = battle->getCompanionEventLog().getSessionId();
	const std::vector<int> unitIds = collectPublishableLivePlayerUnitIds(battle, campaign);
	battle->ensureCompanionTacticalOwnershipInitialized(campaign);
	const CompanionSoldierControlPublishCore::ControlPublicationPlan plan =
		CompanionSoldierControlPublishCore::buildControlPublicationJson(
			sessionId,
			campaign->getCompanionCampaignId(),
			battle->getCompanionTacticalBattleId(),
			battle->getCompanionTacticalAiSoldierIds(),
			unitIds);
	if (!plan.ok)
	{
		Log(LOG_WARNING) << "Companion soldier control: publish skipped (" << plan.publicReason << ")";
		return false;
	}

	const std::string destPath = joinControlPath(snapshotDirectoryPath, "companion-soldier-control.json");
	if (!CompanionSoldierControlPublishCore::writeControlFileAtomically(destPath, plan.jsonUtf8))
	{
		Log(LOG_WARNING) << "Companion soldier control: atomic write failed: " << destPath;
		return false;
	}

	return true;
}

SavedGame *campaignFromBattle(SavedBattleGame *battle)
{
	if (battle == 0)
	{
		return 0;
	}
	BattlescapeState *state = battle->getBattleState();
	if (state == 0 || state->getGame() == 0)
	{
		return 0;
	}
	return state->getGame()->getSavedGame();
}

Language *languageFromBattle(SavedBattleGame *battle)
{
	if (battle == 0)
	{
		return 0;
	}
	BattlescapeState *state = battle->getBattleState();
	if (state == 0 || state->getGame() == 0)
	{
		return 0;
	}
	return state->getGame()->getLanguage();
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

void appendFirearmAmmoState(YAML::Node &entry, BattleItem *item, SavedGame *campaign)
{
	if (!isFirearmWithChamberedAmmo(item))
	{
		return;
	}

	BattleItem *ammo = item->getAmmoItem();
	if (ammo == 0)
	{
		// Distinguish "no magazine inserted" from "magazine present but dry".
		entry["noMagazineInserted"] = true;
		return;
	}

	if (ammo->getAmmoQuantity() == 0)
	{
		entry["empty"] = true;
		YAML::Node loaded;
		loaded["ammoType"] = ammo->getRules()->getType();
		loaded["rounds"] = 0;
		loaded["battleItemId"] = ammo->getId();
		loaded["researchUsable"] = companionItemResearchUsable(campaign, ammo->getRules());
		entry["loaded"] = loaded;
		return;
	}

	YAML::Node loaded;
	loaded["ammoType"] = ammo->getRules()->getType();
	loaded["rounds"] = ammo->getAmmoQuantity();
	loaded["battleItemId"] = ammo->getId();
	loaded["researchUsable"] = companionItemResearchUsable(campaign, ammo->getRules());
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
		entry["explosiveKind"] = bt == BT_PROXIMITYGRENADE ? "proximity" : "timed";
		const int fuse = item->getFuseTimer();
		entry["primed"] = fuse >= 0;
		if (fuse >= 0)
		{
			entry["fuseTimer"] = fuse;
		}
	}
}

const char *inventorySlotTypeName(InventoryType t)
{
	switch (t)
	{
	case INV_HAND: return "hand";
	case INV_GROUND: return "ground";
	default: return "slot";
	}
}

/// Export nested ammunition instance for a firearm without duplicating that magazine in inventory[].
void appendNestedLoadedMagazine(YAML::Node &entry, BattleItem *weapon, SavedGame *campaign)
{
	if (weapon == 0 || !isFirearmWithChamberedAmmo(weapon))
	{
		return;
	}
	BattleItem *ammo = weapon->getAmmoItem();
	if (ammo == 0)
	{
		return;
	}
	YAML::Node nested;
	nested["battleItemId"] = ammo->getId();
	nested["ammoType"] = ammo->getRules()->getType();
	nested["rounds"] = ammo->getAmmoQuantity();
	nested["researchUsable"] = companionItemResearchUsable(campaign, ammo->getRules());
	entry["nestedAmmo"] = nested;
}

void appendMedikitConsumables(YAML::Node &entry, BattleItem *item)
{
	if (item == 0 || item->getRules()->getBattleType() != BT_MEDIKIT)
	{
		return;
	}
	entry["medikitCharges"] = YAML::Node(YAML::NodeType::Map);
	entry["medikitCharges"]["heal"] = item->getHealQuantity();
	entry["medikitCharges"]["painKiller"] = item->getPainKillerQuantity();
	entry["medikitCharges"]["stimulant"] = item->getStimulantQuantity();
}

void appendItemPlacement(YAML::Node &entry, BattleItem *item, const char *handSlotId)
{
	if (item == 0 || item->getSlot() == 0)
	{
		return;
	}
	RuleInventory *slot = item->getSlot();
	entry["inventorySlotType"] = inventorySlotTypeName(slot->getType());
	if (handSlotId != 0)
	{
		entry["slot"] = std::string(handSlotId);
		if (std::string(handSlotId) == "STR_RIGHT_HAND")
		{
			entry["hand"] = "right";
		}
		else if (std::string(handSlotId) == "STR_LEFT_HAND")
		{
			entry["hand"] = "left";
		}
	}
	else
	{
		entry["slot"] = slot->getId();
	}
	if (slot->getType() == INV_SLOT)
	{
		entry["inventoryX"] = item->getSlotX();
		entry["inventoryY"] = item->getSlotY();
	}
}

YAML::Node buildEquipmentItemNode(BattleItem *item, BattleUnit *unit, SavedGame *campaign, const char *handSlotId)
{
	YAML::Node entry;
	entry["battleItemId"] = item->getId();
	entry["itemType"] = item->getRules()->getType();
	entry["battleType"] = static_cast<int>(item->getRules()->getBattleType());
	entry["researchUsable"] = companionItemResearchUsable(campaign, item->getRules());
	appendItemPlacement(entry, item, handSlotId);
	appendNestedLoadedMagazine(entry, item, campaign);
	appendFirearmAmmoState(entry, item, campaign);
	appendGrenadeState(entry, item);
	appendMedikitConsumables(entry, item);
	(void)unit;
	return entry;
}

YAML::Node buildHandSlotNode(BattleItem *item, BattleUnit *unit, SavedGame *campaign, const char *handSlotId)
{
	if (item == 0)
	{
		return nullNode();
	}
	return buildEquipmentItemNode(item, unit, campaign, handSlotId);
}

bool isItemContiguousInNestedWeaponMagazine(BattleUnit *unit, BattleItem *item)
{
	if (unit == 0 || item == 0 || !item->isAmmo())
	{
		return false;
	}
	for (std::vector<BattleItem*>::iterator i = unit->getInventory()->begin();
	     i != unit->getInventory()->end(); ++i)
	{
		BattleItem *w = *i;
		if (w == 0 || w->getRules()->getBattleType() != BT_FIREARM || !w->needsAmmo())
		{
			continue;
		}
		BattleItem *loaded = w->getAmmoItem();
		if (loaded != 0 && loaded->getId() == item->getId())
		{
			return true;
		}
	}
	return false;
}

void appendInventoryEntries(YAML::Node &inventory, BattleUnit *unit, SavedGame *campaign)
{
	size_t count = 0;
	for (std::vector<BattleItem*>::iterator i = unit->getInventory()->begin();
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
		if (isItemContiguousInNestedWeaponMagazine(unit, item))
		{
			continue;
		}

		YAML::Node entry;
		entry["battleItemId"] = item->getId();
		entry["slot"] = item->getSlot()->getId();
		entry["inventorySlotType"] = inventorySlotTypeName(item->getSlot()->getType());
		if (item->getSlot()->getType() == INV_SLOT)
		{
			entry["inventoryX"] = item->getSlotX();
			entry["inventoryY"] = item->getSlotY();
		}
		entry["itemType"] = item->getRules()->getType();
		entry["battleType"] = static_cast<int>(item->getRules()->getBattleType());
		entry["researchUsable"] = companionItemResearchUsable(campaign, item->getRules());
		appendNestedLoadedMagazine(entry, item, campaign);
		appendFirearmAmmoState(entry, item, campaign);
		appendGrenadeState(entry, item);
		appendMedikitConsumables(entry, item);
		inventory.push_back(entry);
		++count;
	}
}

void appendCarriedWeightEncumbrance(YAML::Node &entry, BattleUnit *unit)
{
	if (unit == 0)
	{
		return;
	}
	const int carried = unit->getCarriedWeight(0);
	entry["carriedWeight"] = carried;
	UnitStats *stats = unit->getBaseStats();
	if (stats != 0)
	{
		entry["strength"] = stats->strength;
		if (carried > 0)
		{
			entry["encumbranceRatio"] = static_cast<double>(stats->strength) / static_cast<double>(carried);
		}
	}
}

void appendActorGroundInventory(YAML::Node &entry, SavedBattleGame *battle, SavedGame *campaign, BattleUnit *unit)
{
	if (battle == 0 || unit == 0)
	{
		return;
	}
	Mod *mod = modFromBattle(battle);
	if (mod == 0)
	{
		return;
	}
	Tile *tile = unit->getTile();
	if (tile == 0 || tile->isVoid() || !tile->isDiscovered(2))
	{
		return;
	}
	RuleInventory *groundRule = mod->getInventory("STR_GROUND", true);
	if (groundRule == 0)
	{
		return;
	}
	// Export uses each item's engine grid coordinates on STR_GROUND (set by gameplay / drop placement).
	// Do not re-run ground rearrangement here: it duplicates Inventory::arrangeGround cost paths and test
	// fixtures may place minimal tile sets without full battlescape inventory context.

	std::vector<BattleItem*> groundItems;
	for (std::vector<BattleItem*>::const_iterator i = tile->getInventory()->begin();
	     i != tile->getInventory()->end(); ++i)
	{
		BattleItem *item = *i;
		if (item == 0 || item->getSlot() == 0 || item->getSlot()->getType() != INV_GROUND)
		{
			continue;
		}
		if (item->isAmmo() && item->getSlot() == 0)
		{
			continue;
		}
		groundItems.push_back(item);
	}
	std::sort(groundItems.begin(), groundItems.end(), [](BattleItem *a, BattleItem *b) {
		if (a->getSlotY() != b->getSlotY())
		{
			return a->getSlotY() < b->getSlotY();
		}
		if (a->getSlotX() != b->getSlotX())
		{
			return a->getSlotX() < b->getSlotX();
		}
		return a->getId() < b->getId();
	});

	YAML::Node groundSeq = YAML::Node(YAML::NodeType::Sequence);
	size_t exported = 0;
	for (std::vector<BattleItem*>::const_iterator i = groundItems.begin();
	     i != groundItems.end() && exported < MAX_GROUND_INVENTORY_ENTRIES_PER_UNIT; ++i)
	{
		BattleItem *item = *i;
		if (item->getRules() == 0)
		{
			continue;
		}
		YAML::Node row;
		row["battleItemId"] = item->getId();
		row["itemType"] = item->getRules()->getType();
		row["battleType"] = static_cast<int>(item->getRules()->getBattleType());
		row["slot"] = std::string("STR_GROUND");
		row["inventorySlotType"] = std::string("ground");
		row["inventoryX"] = item->getSlotX();
		row["inventoryY"] = item->getSlotY();
		appendNestedLoadedMagazine(row, item, campaign);
		appendFirearmAmmoState(row, item, campaign);
		appendGrenadeState(row, item);
		appendMedikitConsumables(row, item);
		row["researchUsable"] = companionItemResearchUsable(campaign, item->getRules());
		groundSeq.push_back(row);
		++exported;
	}
	if (!groundSeq.IsNull() && groundSeq.size() > 0U)
	{
		entry["groundInventory"] = groundSeq;
	}
}

void appendFriendlyEquipment(YAML::Node &entry, SavedBattleGame *battle, SavedGame *campaign, BattleUnit *unit)
{
	entry["kneeled"] = CompanionCommandRuntimeSupport::exportKneeledSnapshotField(unit->isKneeled());
	entry["activeHand"] = unit->getActiveHand();

	YAML::Node hands;
	hands["right"] = buildHandSlotNode(unit->getItem("STR_RIGHT_HAND"), unit, campaign, "STR_RIGHT_HAND");
	hands["left"] = buildHandSlotNode(unit->getItem("STR_LEFT_HAND"), unit, campaign, "STR_LEFT_HAND");
	entry["hands"] = hands;

	YAML::Node inventory = YAML::Node(YAML::NodeType::Sequence);
	appendInventoryEntries(inventory, unit, campaign);
	entry["inventory"] = inventory;
	appendCarriedWeightEncumbrance(entry, unit);
	appendActorGroundInventory(entry, battle, campaign, unit);
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

void appendUnit(YAML::Node &units, SavedBattleGame *battle, SavedGame *campaign, BattleUnit *unit)
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
		appendFriendlyEquipment(entry, battle, campaign, unit);
		if (!unit->isOut())
		{
			appendFriendlyActionCosts(entry, battle, unit);
		}
		CompanionSnapshotUnitState::appendFacing(entry, unit);
		CompanionSnapshotUnitState::appendFriendlyCondition(entry, unit);
		CompanionSnapshotUnitState::appendFriendlyReadiness(entry, unit, campaign);
	}
	else
	{
		CompanionSnapshotUnitState::appendVisibleHostileObservations(entry, unit);
		CompanionSnapshotUnitState::appendUnitObservationIdentity(
			entry, battle, unit, campaign, languageFromBattle(battle));
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

static BattleUnit *findBattleUnitById(SavedBattleGame *battle, int unitId)
{
	if (battle == 0 || unitId < 0)
	{
		return 0;
	}
	for (std::vector<BattleUnit*>::iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		if ((*i)->getId() == unitId)
		{
			return *i;
		}
	}
	return 0;
}

static bool isFriendlyPlayerControlledUnit(const BattleUnit *unit)
{
	return unit != 0 && unit->getFaction() == FACTION_PLAYER && unit->getOriginalFaction() == FACTION_PLAYER;
}

static bool shouldExportObserverUnitId(
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionPerceptionEvent &event)
{
	if (!CompanionPerceptionLogCore::shouldExportObserverAttribution(event.positionKnown, event.hasObserverUnitId))
	{
		return false;
	}
	BattleUnit *observer = findBattleUnitById(battle, event.observerUnitId);
	if (!isFriendlyPlayerControlledUnit(observer))
	{
		return false;
	}
	return CompanionSnapshotUnitState::shouldIncludeUnitInSnapshot(observer, campaign);
}

static std::string resolvePerceptionLogSessionId(const CompanionPerceptionLog &log, SavedBattleGame *battle)
{
	const std::string &perceptionSessionId = log.getSessionId();
	if (!perceptionSessionId.empty())
	{
		return perceptionSessionId;
	}
	if (battle != 0)
	{
		const std::string &eventSessionId = battle->getCompanionEventLog().getSessionId();
		if (!eventSessionId.empty())
		{
			return eventSessionId;
		}
	}
	return perceptionSessionId;
}

void appendPerceptionLog(YAML::Node &root, const CompanionPerceptionLog &log, SavedBattleGame *battle, SavedGame *campaign)
{
	YAML::Node perceptionLog;
	perceptionLog["sessionId"] = resolvePerceptionLogSessionId(log, battle);
	perceptionLog["oldestSeq"] = log.getOldestSeq();
	perceptionLog["latestSeq"] = log.getLatestSeq();
	perceptionLog["droppedCount"] = log.getDroppedCount();

	YAML::Node events(YAML::NodeType::Sequence);
	for (std::vector<CompanionPerceptionEvent>::const_iterator i = log.getEvents().begin();
	     i != log.getEvents().end(); ++i)
	{
		YAML::Node ev;
		ev["seq"] = i->seq;
		ev["type"] = i->type;
		ev["turn"] = i->turn;
		ev["perceptionChannel"] = i->perceptionChannel;
		ev["confidence"] = i->confidence;
		ev["positionKnown"] = i->positionKnown;
		if (i->positionKnown)
		{
			ev["position"] = i->position;
		}
		if (shouldExportObserverUnitId(battle, campaign, *i))
		{
			ev["observerUnitId"] = i->observerUnitId;
		}
		const bool psychEvent = CompanionPerceptionLogCore::isPsychologicalPerceptionEventType(i->type.c_str());
		const bool combatCasualtyEvent = CompanionPerceptionLogCore::isCombatCasualtyPerceptionEventType(i->type.c_str());
		const bool exportSubject =
			i->hasSubjectUnitId && i->type != "door_sound_heard" && i->type != "projectile_direction_observed"
			&& i->type != "hostile_casualty_tile_observed";
		if (exportSubject)
		{
			ev["subjectUnitId"] = i->subjectUnitId;
		}
		if ((psychEvent || combatCasualtyEvent) && !i->subjectName.empty())
		{
			ev["subjectName"] = i->subjectName;
		}
		if (!i->compassDirection.empty())
		{
			ev["compassDirection"] = i->compassDirection;
		}
		if (!i->travelDirection.empty())
		{
			ev["travelDirection"] = i->travelDirection;
		}
		if (!i->incomingBearing.empty())
		{
			ev["incomingBearing"] = i->incomingBearing;
		}
		events.push_back(ev);
	}
	perceptionLog["events"] = events;

	YAML::Node rosterCasualtySummary;
	rosterCasualtySummary["schemaVersion"] = 1;
	YAML::Node ledgerEntries(YAML::NodeType::Sequence);
	for (std::map<int, RosterCasualtyLedgerEntry>::const_iterator i = log.getRosterCasualtyLedger().begin();
	     i != log.getRosterCasualtyLedger().end()
	     && ledgerEntries.size() < CompanionPerceptionLogCore::MAX_ROSTER_CASUALTY_LEDGER_ENTRIES;
	     ++i)
	{
		YAML::Node row;
		row["subjectUnitId"] = i->second.subjectUnitId;
		row["episodeSerial"] = i->second.episodeSerial;
		row["status"] = i->second.status;
		row["turn"] = i->second.turn;
		row["positionKnown"] = i->second.positionKnown;
		if (i->second.positionKnown)
		{
			row["position"] = i->second.position;
		}
		if (!i->second.subjectName.empty())
		{
			row["subjectName"] = i->second.subjectName;
		}
		ledgerEntries.push_back(row);
	}
	rosterCasualtySummary["entries"] = ledgerEntries;
	perceptionLog["rosterCasualtySummary"] = rosterCasualtySummary;

	root["perceptionLog"] = perceptionLog;
}

YAML::Node buildSnapshotImpl(SavedBattleGame *battle, SavedGame *campaign, int schemaVersion, bool includeTerrain,
	bool testFullRevealGeometry)
{
	YAML::Node root;
	root["schemaVersion"] = schemaVersion;
	root["turn"] = battle->getTurn();
	root["side"] = static_cast<int>(battle->getSide());
	BattleUnit *selected = battle->getSelectedUnit();
	root["selectedUnitId"] = selected ? selected->getId() : -1;

	YAML::Node map;
	map["width"] = battle->getMapSizeX();
	map["height"] = battle->getMapSizeY();
	map["depth"] = battle->getMapSizeZ();
	root["map"] = map;

	Mod *mod = modFromBattle(battle);
	CompanionSnapshotMissionContext::appendMissionContext(root, battle, mod);

	if (includeTerrain)
	{
		const int mapW = battle->getMapSizeX();
		const int mapH = battle->getMapSizeY();
		const int mapD = battle->getMapSizeZ();
		CompanionSnapshotTerrain::appendTerrain(root, mapW, mapH, mapD, battle, mod,
			[battle](int x, int y, int z) -> Tile * {
				return battle->getTile(Position(x, y, z));
			},
			testFullRevealGeometry);
	}

	YAML::Node units = YAML::Node(YAML::NodeType::Sequence);
	for (std::vector<BattleUnit*>::const_iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		BattleUnit *unit = *i;
		if (!CompanionSnapshotUnitState::shouldIncludeUnitInSnapshot(unit, campaign))
		{
			continue;
		}
		appendUnit(units, battle, campaign, unit);
	}
	root["units"] = units;
	appendEventLog(root, battle->getCompanionEventLog());
	appendPerceptionLog(root, battle->getCompanionPerceptionLog(), battle, campaign);
	CompanionSnapshotGroundItems::appendGroundItems(root, battle, campaign);
	return root;
}

} // namespace

YAML::Node CompanionSnapshotBuild::buildSnapshot(
	SavedBattleGame *battle,
	SavedGame *campaign,
	int schemaVersion,
	bool includeTerrain,
	bool testFullRevealGeometry)
{
	if (battle == 0)
	{
		return YAML::Node();
	}
	SavedGame *resolvedCampaign = campaign;
	if (resolvedCampaign == 0)
	{
		resolvedCampaign = campaignFromBattle(battle);
	}
	return buildSnapshotImpl(battle, resolvedCampaign, schemaVersion, includeTerrain, testFullRevealGeometry);
}

std::string CompanionSnapshotBuild::serializeSnapshotYaml(const YAML::Node &root)
{
	return CompanionSnapshotExportCore::serializeSnapshotYaml(root);
}

bool CompanionSnapshot::tryExport(SavedBattleGame *battle, std::string *outSnapshotSha256)
{
	if (battle == 0)
	{
		return false;
	}

	const char *path = std::getenv("OPENXCOM_COMPANION_SNAPSHOT_PATH");
	if (path == 0 || path[0] == '\0')
	{
		return false;
	}

	if (battle->getDebugMode())
	{
		return false;
	}

	if (battle->getSide() != FACTION_PLAYER || battle->getUnitsFalling())
	{
		return false;
	}

	SavedGame *campaign = battle->getGeoscapeSave();
	const YAML::Node schemaV5 = CompanionSnapshotBuild::buildSnapshot(battle, campaign, SCHEMA_VERSION_V5, true);
	const std::string v5YamlUtf8 = CompanionSnapshotExportCore::serializeSnapshotYaml(schemaV5);
	const CompanionSnapshotExportCore::ResolvedSnapshot resolved =
		CompanionSnapshotExportCore::resolveSnapshotForSizeCap(schemaV5, [&battle, campaign]() {
			return CompanionSnapshotBuild::buildSnapshot(battle, campaign, SCHEMA_VERSION_V4, false);
		});

	if (!resolved.ok)
	{
		Log(LOG_WARNING) << "Companion snapshot: YAML exceeds " << CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES
			<< " byte cap even after schemaVersion 4 terrain fallback (attempted v5 "
			<< resolved.v5AttemptedBytes << " bytes, v4 " << resolved.v4FallbackBytes << " bytes)";
		return false;
	}

	if (resolved.terrainOmittedDueToSize)
	{
		Log(LOG_WARNING) << CompanionSnapshotExportCore::TERRAIN_OMITTED_DIAGNOSTIC()
			<< " (attempted v5 " << resolved.v5AttemptedBytes << " bytes, fallback v4 "
			<< resolved.v4FallbackBytes << " bytes)";
	}

	std::string exportDigest;
	if (outSnapshotSha256 != 0)
	{
		if (!CompanionSha256::digestHexLowerUtf8(resolved.yamlUtf8, outSnapshotSha256))
		{
			return false;
		}
		exportDigest = *outSnapshotSha256;
	}
	else
	{
		if (!CompanionSha256::digestHexLowerUtf8(resolved.yamlUtf8, &exportDigest))
		{
			return false;
		}
	}

	if (!CompanionSnapshotExportCore::writeSnapshotAtomically(path, resolved.yamlUtf8))
	{
		return false;
	}

	CompanionCommandRuntime::noteSnapshotExportSha256(exportDigest);
	const int exportSeq = battle->getCompanionEventLog().getLatestSeq();
	CompanionCommandRuntime::noteSnapshotExportStateVersion(exportSeq);
	CompanionRelayPublicationFreshnessCore::noteSnapshotExportBinding(
		exportSeq,
		exportDigest,
		battle->getCompanionEventLog().getSessionId(),
		battle->getTurn());

	std::string archiveDigest;
	if (outSnapshotSha256 != 0 && !outSnapshotSha256->empty())
	{
		archiveDigest = *outSnapshotSha256;
	}
	else
	{
		CompanionSha256::digestHexLowerUtf8(resolved.yamlUtf8, &archiveDigest);
	}

	const std::string snapshotDir = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(path);
	const long long exportedAtMs = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
	const std::string sessionId = battle->getCompanionEventLog().getSessionId();
	if (resolved.terrainOmittedDueToSize)
	{
		CompanionManualV5Export::ManualV5ExportInput manualInput = {};
		manualInput.snapshotDir = snapshotDir;
		manualInput.sessionId = sessionId;
		manualInput.turn = battle->getTurn();
		manualInput.v5YamlUtf8 = v5YamlUtf8;
		manualInput.automaticYamlUtf8 = resolved.yamlUtf8;
		manualInput.v5AttemptedBytes = resolved.v5AttemptedBytes;
		manualInput.v4FallbackBytes = resolved.v4FallbackBytes;
		manualInput.exportedAtMs = exportedAtMs;
		if (outSnapshotSha256 != 0 && !outSnapshotSha256->empty())
		{
			manualInput.automaticSnapshotSha256 = *outSnapshotSha256;
		}
		else
		{
			std::string automaticDigest;
			if (CompanionSha256::digestHexLowerUtf8(resolved.yamlUtf8, &automaticDigest))
			{
				manualInput.automaticSnapshotSha256 = automaticDigest;
			}
		}
		const CompanionManualV5Export::ManualV5ExportResult manualOutcome =
			CompanionManualV5Export::tryWriteManualV5OversizeExport(manualInput);
		if (!manualOutcome.artifactWritten)
		{
			Log(LOG_WARNING) << "Companion manual v5 export failed after v4 fallback: "
			                 << manualOutcome.diagnostic;
		}
	}
	else if (resolved.schemaVersion == 5)
	{
		CompanionManualV5Export::clearEngineManualUploadNoticeForSession(snapshotDir, sessionId, exportedAtMs);
	}
	if (!tryPublishSoldierControlBesideSnapshot(battle, campaign, snapshotDir))
	{
		Log(LOG_WARNING) << "Companion soldier control: publication failed (snapshot export succeeded)";
	}
	if (!archiveDigest.empty())
	{
		CompanionCommandRuntime::archiveStaleInboxAfterSnapshotExport(battle, archiveDigest);
	}

	return true;
}

bool CompanionSnapshot::tryPublishSoldierControl(SavedBattleGame *battle, SavedGame *campaign)
{
	if (battle == 0 || campaign == 0)
	{
		return false;
	}
	const char *path = std::getenv("OPENXCOM_COMPANION_SNAPSHOT_PATH");
	if (path == 0 || path[0] == '\0')
	{
		return false;
	}
	const std::string snapshotDir = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(path);
	return tryPublishSoldierControlBesideSnapshot(battle, campaign, snapshotDir);
}

}
