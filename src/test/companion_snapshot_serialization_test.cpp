/*
 * Open X-Com Companion — production snapshot serialization integration tests (M1-WP2B).
 */
#include "Engine/CompanionSnapshotBuild.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Base.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Armor.h"
#include "../Mod/Unit.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/MapData.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Tile.h"
#include "../Engine/CompanionPerceptionLog.h"
#include "../Engine/CompanionCommandCasualtySettlement.h"
#include "../Engine/CompanionItemResearchEligibilityCore.h"
#include "../Mod/RuleResearch.h"
#include <yaml-cpp/yaml.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace OpenXcom;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static Mod &fixtureMod()
{
	static Mod mod;
	return mod;
}

static RuleInventory *snapshotTestGroundInventoryRule();

struct SoldierFixture
{
	RuleSoldier rules;
	Armor armor;
	Soldier soldier;
	SoldierFixture(int id) : rules("STR_SOLDIER"), armor("STR_JUMPSUIT"), soldier(&rules, &armor, id) {}
};

struct AlienFixture
{
	Unit rules;
	Armor armor;
	BattleUnit *unit;
	AlienFixture(int id, bool visible, int direction, bool kneeled)
		: rules("STR_SECTOID"), armor("STR_SECTOID_ARMOR"),
		  unit(0)
	{
		StatAdjustment adjustment;
		unit = new BattleUnit(&rules, FACTION_HOSTILE, id, &armor, &adjustment, 0);
		unit->setVisible(visible);
		unit->kneel(kneeled);
		unit->setDirection(direction);
		unit->setPosition(Position(2, 2, 0));
	}
};

struct CyberdiscFixture
{
	Unit rules;
	Armor armor;
	BattleUnit *unit;
	CyberdiscFixture(int id, bool visible, int direction, bool kneeled, const Position &pos)
		: rules("STR_CYBERDISC_TERRORIST"), armor("STR_SECTOID_ARMOR"), unit(0)
	{
		StatAdjustment adjustment;
		unit = new BattleUnit(&rules, FACTION_HOSTILE, id, &armor, &adjustment, 0);
		unit->setVisible(visible);
		unit->kneel(kneeled);
		unit->setDirection(direction);
		unit->setPosition(pos);
	}
};

/// Owns heap bases/battles so SavedGame::~SavedGame never deletes static fixture memory.
struct TacticalFixture
{
	SavedGame game;
	Base *base;
	SavedBattleGame *battle;
	std::vector<SoldierFixture *> soldierBundles;
	std::vector<AlienFixture *> alienBundles;
	std::vector<CyberdiscFixture *> cyberdiscBundles;

	TacticalFixture()
		: base(new Base(&fixtureMod())),
		  battle(new SavedBattleGame())
	{
		game.getBases()->push_back(base);
		battle->initMap(4, 4, 1);
		battle->setTestMod(&fixtureMod());
		battle->getCompanionEventLog().beginSession();
	}

	~TacticalFixture()
	{
		delete battle;
		battle = 0;
		for (std::vector<AlienFixture *>::iterator i = alienBundles.begin(); i != alienBundles.end(); ++i)
		{
			delete *i;
		}
		alienBundles.clear();
		for (std::vector<CyberdiscFixture *>::iterator i = cyberdiscBundles.begin(); i != cyberdiscBundles.end(); ++i)
		{
			delete *i;
		}
		cyberdiscBundles.clear();
		base->getSoldiers()->clear();
		game.getBases()->clear();
		delete base;
		base = 0;
		for (std::vector<SoldierFixture *>::iterator i = soldierBundles.begin(); i != soldierBundles.end(); ++i)
		{
			delete *i;
		}
		soldierBundles.clear();
	}

	Soldier *addRosterSoldier(int id)
	{
		SoldierFixture *bundle = new SoldierFixture(id);
		soldierBundles.push_back(bundle);
		base->getSoldiers()->push_back(&bundle->soldier);
		return &bundle->soldier;
	}

	BattleUnit *addSoldierUnit(Soldier *soldier, const Position &pos, int direction)
	{
		BattleUnit *unit = new BattleUnit(soldier, 0);
		unit->setPosition(pos);
		unit->setDirection(direction);
		battle->getUnits()->push_back(unit);
		return unit;
	}

	BattleUnit *addAlienUnit(int id, bool visible, int direction, bool kneeled)
	{
		AlienFixture *bundle = new AlienFixture(id, visible, direction, kneeled);
		alienBundles.push_back(bundle);
		battle->getUnits()->push_back(bundle->unit);
		return bundle->unit;
	}
};

struct Wp8TestRules
{
	RuleInventory rightHand;
	RuleInventory leftHand;
	RuleInventory belt;
	RuleInventory backpack;
	RuleInventory rightShoulder;
	RuleItem rifle;
	RuleItem clip;
	RuleItem laser;
	RuleItem grenade;
	RuleItem proximity;
	RuleItem medikit;

	Wp8TestRules()
		: rightHand("STR_RIGHT_HAND"), leftHand("STR_LEFT_HAND"), belt("STR_BELT"),
		  backpack("STR_BACK_PACK"), rightShoulder("STR_RIGHT_SHOULDER"),
		  rifle("STR_WP8_RIFLE"), clip("STR_WP8_CLIP"), laser("STR_WP8_LASER"),
		  grenade("STR_WP8_GRENADE"), proximity("STR_WP8_PROX"), medikit("STR_WP8_MEDIKIT")
	{
		YAML::Node rh;
		rh["id"] = "STR_RIGHT_HAND";
		rh["x"] = 0;
		rh["y"] = 0;
		rh["type"] = static_cast<int>(INV_HAND);
		rh["costs"] = YAML::Node(YAML::NodeType::Map);
		rightHand.load(rh, 0);

		YAML::Node lh;
		lh["id"] = "STR_LEFT_HAND";
		lh["x"] = 0;
		lh["y"] = 0;
		lh["type"] = static_cast<int>(INV_HAND);
		lh["costs"] = YAML::Node(YAML::NodeType::Map);
		leftHand.load(lh, 0);

		YAML::Node b;
		b["id"] = "STR_BELT";
		b["x"] = 0;
		b["y"] = 0;
		b["type"] = static_cast<int>(INV_SLOT);
		b["slots"] = YAML::Load("[[0,0],[1,0],[2,0]]");
		b["costs"]["STR_BACK_PACK"] = 16;
		b["costs"]["STR_RIGHT_HAND"] = 4;
		belt.load(b, 0);

		YAML::Node bp;
		bp["id"] = "STR_BACK_PACK";
		bp["x"] = 0;
		bp["y"] = 0;
		bp["type"] = static_cast<int>(INV_SLOT);
		bp["slots"] = YAML::Load("[[0,0],[1,0],[2,0],[0,1],[1,1],[2,1]]");
		bp["costs"]["STR_BELT"] = 12;
		bp["costs"]["STR_RIGHT_HAND"] = 8;
		backpack.load(bp, 0);

		YAML::Node rs;
		rs["id"] = "STR_RIGHT_SHOULDER";
		rs["x"] = 0;
		rs["y"] = 0;
		rs["type"] = static_cast<int>(INV_SLOT);
		rs["slots"] = YAML::Load("[[0,0],[1,0]]");
		rs["costs"]["STR_RIGHT_HAND"] = 3;
		rightShoulder.load(rs, 0);

		YAML::Node rifleNode;
		rifleNode["weight"] = 5;
		rifleNode["battleType"] = static_cast<int>(BT_FIREARM);
		rifleNode["clipSize"] = 20;
		rifleNode["compatibleAmmo"] = YAML::Load("[STR_WP8_CLIP]");
		rifleNode["accuracySnap"] = 50;
		rifleNode["tuSnap"] = 30;
		rifleNode["invWidth"] = 1;
		rifleNode["invHeight"] = 3;
		rifle.load(rifleNode, &fixtureMod(), 0);

		YAML::Node clipNode;
		clipNode["weight"] = 1;
		clipNode["battleType"] = static_cast<int>(BT_AMMO);
		clipNode["clipSize"] = 20;
		clipNode["power"] = 40;
		clipNode["damageType"] = static_cast<int>(DT_AP);
		clipNode["invWidth"] = 1;
		clipNode["invHeight"] = 1;
		clip.load(clipNode, &fixtureMod(), 0);

		YAML::Node laserNode;
		laserNode["weight"] = 8;
		laserNode["battleType"] = static_cast<int>(BT_FIREARM);
		laserNode["clipSize"] = -1;
		laserNode["compatibleAmmo"] = YAML::Node(YAML::NodeType::Sequence);
		laserNode["accuracySnap"] = 50;
		laserNode["tuSnap"] = 30;
		laserNode["invWidth"] = 1;
		laserNode["invHeight"] = 3;
		laser.load(laserNode, &fixtureMod(), 0);

		YAML::Node g;
		g["weight"] = 1;
		g["battleType"] = static_cast<int>(BT_GRENADE);
		g["invWidth"] = 1;
		g["invHeight"] = 2;
		grenade.load(g, &fixtureMod(), 0);

		YAML::Node p;
		p["weight"] = 1;
		p["battleType"] = static_cast<int>(BT_PROXIMITYGRENADE);
		p["invWidth"] = 1;
		p["invHeight"] = 2;
		proximity.load(p, &fixtureMod(), 0);

		YAML::Node m;
		m["weight"] = 3;
		m["battleType"] = static_cast<int>(BT_MEDIKIT);
		m["heal"] = 3;
		m["painKiller"] = 3;
		m["stimulant"] = 3;
		m["tuUse"] = 30;
		m["invWidth"] = 1;
		m["invHeight"] = 2;
		medikit.load(m, &fixtureMod(), 0);

		// Heap STR_GROUND only — do not register stack RuleInventory members; Mod::~Mod deletes registered rules.
		snapshotTestGroundInventoryRule();
	}
};

static Wp8TestRules &wp8Rules()
{
	static Wp8TestRules rules;
	return rules;
}

static RuleInventory *snapshotTestGroundInventoryRule()
{
	RuleInventory *existing = fixtureMod().getInventory("STR_GROUND", false);
	if (existing != 0)
	{
		return existing;
	}
	YAML::Node g;
	g["id"] = "STR_GROUND";
	g["x"] = 0;
	g["y"] = 0;
	g["type"] = static_cast<int>(INV_GROUND);
	g["costs"]["STR_RIGHT_HAND"] = 10;
	g["costs"]["STR_BELT"] = 12;
	RuleInventory *ground = new RuleInventory("STR_GROUND");
	ground->load(g, 0);
	fixtureMod().registerTestInventoryRule(ground);
	return ground;
}

static void placeUnitOnDiscoveredTile(TacticalFixture &fixture, BattleUnit *unit, const Position &pos)
{
	Tile *tile = fixture.battle->getTile(pos);
	expect(tile != 0, "tile exists for unit placement");
	if (tile == 0)
	{
		return;
	}
	tile->setDiscovered(true, 2);
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("snapshot_ground_inv_test");
	}
	MapData *floor = new MapData(ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	tile->setMapData(floor, 1, 0, O_FLOOR);
	unit->setPosition(pos);
	unit->setTile(tile, fixture.battle->getTile(Position(pos.x, pos.y, pos.z - 1)));
}

static void registerBattleItem(SavedBattleGame *battle, BattleItem *item)
{
	if (battle == 0 || item == 0)
	{
		return;
	}
	battle->getItems()->push_back(item);
}

static void adoptGroundItemForBattleCleanup(SavedBattleGame *battle, Tile *tile, BattleItem *item)
{
	if (battle == 0 || tile == 0 || item == 0)
	{
		return;
	}
	tile->removeItem(item);
	// Avoid duplicate pointers in battle->_items (double-delete in ~SavedBattleGame).
	std::vector<BattleItem *> *all = battle->getItems();
	for (std::size_t i = 0; i < all->size(); ++i)
	{
		if ((*all)[i] == item)
		{
			return;
		}
	}
	registerBattleItem(battle, item);
}

static void placeEquippedItem(SavedBattleGame *battle, BattleUnit *unit, BattleItem *item, RuleInventory *slot, int x, int y)
{
	item->setSlot(slot);
	item->setSlotX(x);
	item->setSlotY(y);
	unit->getInventory()->push_back(item);
	registerBattleItem(battle, item);
}

static YAML::Node findUnitById(const YAML::Node &units, int id)
{
	if (!units.IsDefined())
	{
		return YAML::Node();
	}
	for (std::size_t i = 0; i < units.size(); ++i)
	{
		if (units[i]["id"].IsDefined() && units[i]["id"].as<int>() == id)
		{
			return units[i];
		}
	}
	return YAML::Node();
}

static YAML::Node buildParsedSnapshot(TacticalFixture &fixture, int schemaVersion = 4)
{
	const YAML::Node root = CompanionSnapshotBuild::buildSnapshot(fixture.battle, &fixture.game, schemaVersion, false);
	const std::string yamlText = CompanionSnapshotBuild::serializeSnapshotYaml(root);
	return YAML::Load(yamlText);
}

/// Snapshot tree without serialize→parse round-trip (avoids emitter edge cases while validating structure).
static YAML::Node findUnitByIdDirect(TacticalFixture &fixture, int id, int schemaVersion = 4)
{
	const YAML::Node root = CompanionSnapshotBuild::buildSnapshot(fixture.battle, &fixture.game, schemaVersion, false);
	return findUnitById(root["units"], id);
}

static void testHealthySoldierWp2Fields()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(1);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 1, 0), 2);
	fixture.battle->setSelectedUnit(unit);

	const YAML::Node parsed = buildParsedSnapshot(fixture);
	const YAML::Node entry = findUnitById(parsed["units"], 1);
	expect(entry.IsDefined(), "roster soldier exported");
	expect(entry["facing"].IsDefined() && entry["facing"]["direction"].as<int>() == 2, "facing direction");
	expect(entry["facing"]["vector"].IsDefined()
		&& entry["facing"]["vector"]["dx"].as<int>() == 1, "facing dx east");
	expect(entry["condition"].IsDefined() && entry["condition"]["conscious"].as<bool>(), "conscious");
	expect(entry["readiness"].IsDefined()
		&& entry["readiness"]["engineActingUnitAvailable"].as<bool>(), "engine acting available");
	expect(entry["actionCosts"].IsDefined(), "actionCosts present when active");
}

static void testUnconsciousOmitsActionCosts()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(2);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 2, 0), 4);
	unit->load(YAML::Load(
		"status: 7\nstunlevel: 200\nhealth: 20\ntu: 0\nenergy: 0\nmorale: 40\n"
		"armor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));

	const YAML::Node entry = findUnitById(buildParsedSnapshot(fixture)["units"], 2);
	expect(entry.IsDefined(), "unconscious roster soldier still listed");
	expect(entry["condition"].IsDefined() && entry["condition"]["conscious"].as<bool>() == false, "not conscious");
	expect(entry["condition"]["stunLevel"].as<int>() == 200, "stun level preserved");
	expect(entry["readiness"].IsDefined()
		&& entry["readiness"]["summary"].as<std::string>() == "unconscious", "unconscious summary");
	expect(!entry["actionCosts"], "actionCosts omitted when isOut");
	expect(entry["readiness"].IsDefined()
		&& entry["readiness"]["engineActingUnitAvailable"].as<bool>() == false, "not engine-acting when out");
}

static void eraseSoldierFromLiveCampaignRoster(TacticalFixture &fixture, Soldier *soldier)
{
	for (std::vector<Base*>::iterator bi = fixture.game.getBases()->begin(); bi != fixture.game.getBases()->end(); ++bi)
	{
		std::vector<Soldier*> *roster = (*bi)->getSoldiers();
		for (std::vector<Soldier*>::iterator si = roster->begin(); si != roster->end(); ++si)
		{
			if (*si == soldier)
			{
				roster->erase(si);
				return;
			}
		}
	}
}

static void testSoldierDownRecordsAfterKillSoldierRemovesLiveRoster()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(4);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(3, 4, 0), 0);
	unit->load(YAML::Load("status: 6\nhealth: 0\ntu: 0\nenergy: 0\nmorale: 0\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	fixture.battle->setGeoscapeSaveHint(&fixture.game);
	eraseSoldierFromLiveCampaignRoster(fixture, soldier);
	expect(!fixture.game.isLiveRosterSoldier(4), "campaign roster erased before delayed death hook");
	expect(unit->getGeoscapeSoldier() == soldier, "battle unit retains geoscape soldier pointer");

	CompanionPerceptionLog &perception = fixture.battle->getCompanionPerceptionLog();
	perception.beginSession("kia-identity-test");
	perception.tryRecordSoldierDown(fixture.battle, unit, true);

	expect(perception.getRosterCasualtyLedger().count(4) == 1, "ledger entry after post-KIA record");
	const YAML::Node root = buildParsedSnapshot(fixture);
	expect(root["units"].size() == 0, "dead soldier excluded from units export");
	const YAML::Node ledger = root["perceptionLog"]["rosterCasualtySummary"];
	expect(ledger["entries"].size() == 1, "ledger exported");
	expect(ledger["entries"][0]["status"].as<std::string>() == "dead", "dead status");
	const YAML::Node events = root["perceptionLog"]["events"];
	expect(events.size() == 1, "soldier_down event");
	expect(events[0]["type"].as<std::string>() == "soldier_down", "soldier_down type");
}

static void testSoldierCasualtyUnconsciousThenRecoverEpisode()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(2);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 1, 0), 0);
	unit->load(YAML::Load(
		"status: 7\nstunlevel: 80\nhealth: 20\ntu: 0\nenergy: 0\nmorale: 50\n"
		"armor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	fixture.battle->setGeoscapeSaveHint(&fixture.game);
	CompanionPerceptionLog &perception = fixture.battle->getCompanionPerceptionLog();
	perception.beginSession("casualty-episode-test");
	perception.tryRecordSoldierDown(fixture.battle, unit, true);
	expect(perception.getRosterCasualtyLedger().count(2) == 1, "unconscious ledger row");
	unit->load(YAML::Load("status: 0\nstunlevel: 0\nhealth: 30\ntu: 40\nenergy: 40\nmorale: 60\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	perception.tryRecordSoldierRecovered(fixture.battle, unit);
	unit->load(YAML::Load(
		"status: 7\nstunlevel: 90\nhealth: 10\ntu: 0\nenergy: 0\nmorale: 40\n"
		"armor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	perception.tryRecordSoldierDown(fixture.battle, unit, true);
	expect(perception.getRosterCasualtyLedger().at(2).episodeSerial == 2, "second unconscious episode");
}

static void testMindControlledOriginalSoldierFatalityAfterRosterErase()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(3);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(2, 2, 0), 0);
	unit->load(YAML::Load("status: 6\nhealth: 0\ntu: 0\nenergy: 0\nmorale: 0\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	unit->convertToFaction(FACTION_HOSTILE);
	fixture.battle->setGeoscapeSaveHint(&fixture.game);
	eraseSoldierFromLiveCampaignRoster(fixture, soldier);
	CompanionPerceptionLog &perception = fixture.battle->getCompanionPerceptionLog();
	perception.beginSession("mc-kia-test");
	perception.tryRecordSoldierDown(fixture.battle, unit, false);
	expect(perception.getRosterCasualtyLedger().count(3) == 1, "MC original X-COM fatality ledger");
	expect(perception.getRosterCasualtyLedger().at(3).status == "dead", "MC fatality dead");
}

static void testRosterCasualtyLedgerSeededAfterBattleLoad()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(6);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(2, 3, 0), 0);
	unit->load(YAML::Load(
		"status: 7\nstunlevel: 120\nhealth: 15\ntu: 0\nenergy: 0\nmorale: 30\n"
		"armor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	unit->setVisible(true);
	fixture.battle->setGeoscapeSaveHint(&fixture.game);

	CompanionPerceptionLog &perception = fixture.battle->getCompanionPerceptionLog();
	perception.beginSession("load-sim-session");
	perception.seedRosterCasualtyLedgerFromLoadedBattle(fixture.battle);

	const YAML::Node root = buildParsedSnapshot(fixture);
	const YAML::Node ledger = root["perceptionLog"]["rosterCasualtySummary"];
	expect(ledger.IsDefined() && ledger["entries"].size() == 1, "one ledger entry after load seed");
	expect(ledger["entries"][0]["subjectUnitId"].as<int>() == 6, "subject id");
	expect(ledger["entries"][0]["status"].as<std::string>() == "unconscious", "unconscious posture");
	expect(ledger["entries"][0]["episodeSerial"].as<int>() == 1, "episode serial baseline on load");
	const YAML::Node events = root["perceptionLog"]["events"];
	expect(!events.IsDefined() || events.size() == 0, "load seed must not invent soldier_down events");
}

static void testRosterCasualtySnapshotUnsettledWhileCollapsing()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(3);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(5, 5, 0), 0);
	unit->load(YAML::Load(
		"status: 5\nhealth: 0\ntu: 0\nenergy: 0\nmorale: 0\n"
		"armor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	fixture.battle->setGeoscapeSaveHint(&fixture.game);
	expect(CompanionCommandCasualtySettlement::isRosterCasualtySnapshotWorkUnsettled(0, fixture.battle),
		"collapsing roster soldier defers terminal snapshot settlement");
	unit->load(YAML::Load("status: 6\nhealth: 0\ntu: 0\nenergy: 0\nmorale: 0\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	expect(!CompanionCommandCasualtySettlement::isRosterCasualtySnapshotWorkUnsettled(0, fixture.battle),
		"settled dead roster soldier without pending die state");
}

static void testRosterCasualtyLedgerSeededForDeadAfterKillSoldier()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(8);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(4, 4, 0), 0);
	unit->load(YAML::Load("status: 6\nhealth: 0\ntu: 0\nenergy: 0\nmorale: 0\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	fixture.battle->setGeoscapeSaveHint(&fixture.game);
	eraseSoldierFromLiveCampaignRoster(fixture, soldier);

	CompanionPerceptionLog &perception = fixture.battle->getCompanionPerceptionLog();
	perception.beginSession("load-dead-seed");
	perception.seedRosterCasualtyLedgerFromLoadedBattle(fixture.battle);

	const YAML::Node ledger = buildParsedSnapshot(fixture)["perceptionLog"]["rosterCasualtySummary"];
	expect(ledger["entries"].size() == 1, "dead soldier seeded after roster erase");
	expect(ledger["entries"][0]["status"].as<std::string>() == "dead", "seeded dead");
}

static void testVisibleHostileObservationOnly()
{
	TacticalFixture fixture;
	BattleUnit *alien = fixture.addAlienUnit(1000000, true, 6, true);
	const Position pos = Position(2, 2, 0);
	placeUnitOnDiscoveredTile(fixture, alien, pos);

	const YAML::Node entry = findUnitById(buildParsedSnapshot(fixture)["units"], 1000000);
	expect(entry.IsDefined(), "visible alien exported");
	expect(!entry["name"], "no alien name field");
	expect(entry["observedUnitType"].IsDefined(), "observed unit type from engine rules");
	expect(entry["observedUnitType"].as<std::string>() == "STR_SECTOID", "sectoid mod type id");
	expect(!entry["condition"], "no alien condition");
	expect(entry["kneeled"].as<bool>(), "kneeled observation");
	expect(entry["facing"].IsDefined(), "visible hostile facing allowed");
}

static void testVisibleHostileExportsObservedUnitTypeWhenSpotted()
{
	TacticalFixture fixture;
	BattleUnit *alien = fixture.addAlienUnit(1000000, true, 6, true);
	placeUnitOnDiscoveredTile(fixture, alien, Position(2, 2, 0));

	const YAML::Node entry = findUnitById(buildParsedSnapshot(fixture)["units"], 1000000);
	expect(entry["observedUnitType"].IsDefined(), "spotted hostile exports observedUnitType");
	expect(entry["observedUnitType"].as<std::string>() == "STR_SECTOID", "sectoid mod type id");
}

static void testVisibleCyberdiscUsesUnitTypeNotRace()
{
	TacticalFixture fixture;
	CyberdiscFixture *bundle = new CyberdiscFixture(1000007, true, 4, false, Position(2, 2, 0));
	fixture.cyberdiscBundles.push_back(bundle);
	fixture.battle->getUnits()->push_back(bundle->unit);
	placeUnitOnDiscoveredTile(fixture, bundle->unit, Position(2, 2, 0));

	const YAML::Node entry = findUnitById(buildParsedSnapshot(fixture)["units"], 1000007);
	expect(entry.IsDefined(), "cyberdisc visible shell exported");
	expect(entry["observedUnitType"].IsDefined(), "cyberdisc type exported under LOS");
	expect(entry["observedUnitType"].as<std::string>() == "STR_CYBERDISC_TERRORIST",
		"cyberdisc exports mod unit type not race");
}

static void testHiddenHostileOmitted()
{
	TacticalFixture fixture;
	fixture.addAlienUnit(1000001, false, 0, false);
	expect(buildParsedSnapshot(fixture)["units"].size() == 0, "hidden alien omitted");
}

static void testMindControlledRosterSoldierNoFriendlyLeak()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(3);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(2, 1, 0), 0);
	unit->convertToFaction(FACTION_HOSTILE);
	unit->setVisible(true);
	placeUnitOnDiscoveredTile(fixture, unit, Position(2, 1, 0));

	const YAML::Node entry = findUnitById(buildParsedSnapshot(fixture)["units"], 3);
	expect(entry.IsDefined(), "MC soldier visible shell");
	expect(entry["name"].IsDefined(), "MC roster name preserved under current LOS");
	expect(!entry["condition"], "no friendly condition leak");
	expect(!entry["observedUnitType"], "MC roster soldier is not a hostile unit type");
	expect(entry["facing"].IsDefined(), "visible MC hostile-minimal shell includes facing");
}

static void testHiddenMindControlledRosterSoldierOmitted()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(5);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(3, 3, 0), 0);
	unit->convertToFaction(FACTION_HOSTILE);
	unit->setVisible(false);
	expect(buildParsedSnapshot(fixture)["units"].size() == 0,
		"hidden mind-controlled roster soldier omitted");
}

static void testRosterSoldierIgnoreMeStillExported()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(13);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(2, 2, 0), 4);
	unit->load(YAML::Load("status: 6\nhealth: 0\ntu: 0\nenergy: 0\nmorale: 0\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	const YAML::Node entry = findUnitById(buildParsedSnapshot(fixture)["units"], 13);
	expect(entry.IsDefined(), "roster soldier with STATUS_DEAD still exported");
	expect(entry["condition"].IsDefined() && entry["condition"]["alive"].as<bool>() == false, "dead condition preserved");
}

static void testSerializedYamlFieldNames()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(4);
	fixture.addSoldierUnit(soldier, Position(0, 0, 0), 0);
	const std::string yaml = CompanionSnapshotBuild::serializeSnapshotYaml(
		CompanionSnapshotBuild::buildSnapshot(fixture.battle, &fixture.game, 4, false));
	expect(yaml.find("engineActingUnitAvailable:") != std::string::npos, "yaml contains readiness field");
	expect(yaml.find("observationNote:") != std::string::npos, "yaml contains observation note");
	expect(yaml.find("companionCommandActorEligible:") == std::string::npos, "deprecated field absent");
}

static void testGroundItemsBlockPresentOnV4()
{
	TacticalFixture fixture;
	const YAML::Node root = buildParsedSnapshot(fixture);
	expect(root["groundItems"].IsDefined(), "groundItems block expected");
	expect(root["groundItems"]["schemaVersion"].as<int>() == 1, "groundItems schemaVersion");
	expect(root["groundItems"]["observations"].size() == 0, "empty map has no ground observations");
}

static void testGroundWeaponExportsInLos()
{
	TacticalFixture fixture;
	MapDataSet ds("test");
	MapData *floor = new MapData(&ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	Tile *tile = fixture.battle->getTile(Position(1, 1, 0));
	tile->setMapData(floor, 1, 0, O_FLOOR);
	tile->setDiscovered(true, 2);
	tile->setVisible(+1);
	RuleInventory ground("STR_GROUND");
	RuleItem rifleRules("STR_RIFLE");
	YAML::Node rifleNode;
	rifleNode["weight"] = 30;
	rifleNode["battleType"] = static_cast<int>(BT_FIREARM);
	rifleNode["clipSize"] = -1;
	rifleRules.load(rifleNode, &fixtureMod(), 0);
	BattleItem *item = new BattleItem(&rifleRules, fixture.battle->getCurrentItemId());
	tile->addItem(item, &ground);

	const YAML::Node root = buildParsedSnapshot(fixture);
	const YAML::Node observations = root["groundItems"]["observations"];
	expect(observations.size() == 1, "one ground observation expected");
	expect(observations[0]["observationStatus"].as<std::string>() == "CONFIRMED_IN_LOS", "confirmed status");
	expect(observations[0]["items"][0]["itemType"].as<std::string>() == "STR_RIFLE", "rifle itemType");
	adoptGroundItemForBattleCleanup(fixture.battle, tile, item);
}

static void testPerceptionLogSessionMatchesEventLogWhenUnset()
{
	TacticalFixture fixture;
	fixture.battle->getCompanionPerceptionLog().tryRecordDoorSoundHeard(fixture.battle, Position(1, 1, 0));
	const YAML::Node parsed = buildParsedSnapshot(fixture);
	expect(parsed["perceptionLog"].IsDefined(), "perception log exported");
	expect(parsed["eventLog"]["sessionId"].IsDefined(), "event log session exported");
	const std::string eventSession = parsed["eventLog"]["sessionId"].as<std::string>();
	expect(!eventSession.empty(), "event session non-empty");
	expect(parsed["perceptionLog"]["sessionId"].as<std::string>() == eventSession,
		"perception session must match event log when perception session unset");
}

static void testGroundItemRememberedOffLos()
{
	TacticalFixture fixture;
	MapDataSet ds("test");
	MapData *floor = new MapData(&ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	Tile *tile = fixture.battle->getTile(Position(1, 0, 0));
	tile->setMapData(floor, 1, 0, O_FLOOR);
	tile->setDiscovered(true, 2);
	tile->setVisible(+1);
	RuleInventory ground("STR_GROUND");
	RuleItem rifleRules("STR_RIFLE");
	YAML::Node rifleNode;
	rifleNode["weight"] = 30;
	rifleNode["battleType"] = static_cast<int>(BT_FIREARM);
	rifleNode["clipSize"] = -1;
	rifleRules.load(rifleNode, &fixtureMod(), 0);
	BattleItem *item = new BattleItem(&rifleRules, fixture.battle->getCurrentItemId());
	tile->addItem(item, &ground);
	(void)buildParsedSnapshot(fixture);
	tile->setVisible(-1);
	const YAML::Node root = buildParsedSnapshot(fixture);
	expect(root["groundItems"]["observations"].size() == 1, "remembered observation expected");
	expect(root["groundItems"]["observations"][0]["observationStatus"].as<std::string>() == "REMEMBERED_OFF_LOS",
		"remembered status");
	adoptGroundItemForBattleCleanup(fixture.battle, tile, item);
}

static void testWp8InventoryObservationIdentityAndPlacement()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(1);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 1, 0), 2);
	Wp8TestRules &R = wp8Rules();

	BattleItem *rifle = new BattleItem(&R.rifle, fixture.battle->getCurrentItemId());
	BattleItem *clipLoaded = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	BattleItem *clipSpareA = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	BattleItem *clipSpareB = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	clipSpareB->setAmmoQuantity(7);

	expect(rifle->setAmmoItem(clipLoaded) == 0, "rifle accepts magazine");
	rifle->setSlot(&R.rightHand);
	rifle->setSlotX(0);
	rifle->setSlotY(0);
	unit->getInventory()->push_back(rifle);
	registerBattleItem(fixture.battle, rifle);
	registerBattleItem(fixture.battle, clipLoaded);
	// clipLoaded is nested in rifle; do not list as top-level inventory row.

	placeEquippedItem(fixture.battle, unit, clipSpareA, &R.belt, 1, 0);
	placeEquippedItem(fixture.battle, unit, clipSpareB, &R.backpack, 2, 1);

	const YAML::Node entry = findUnitById(buildParsedSnapshot(fixture)["units"], 1);
	expect(entry.IsDefined(), "wp8 unit exported");
	const YAML::Node rh = entry["hands"]["right"];
	expect(rh["battleItemId"].IsDefined(), "hand battleItemId");
	expect(rh["inventorySlotType"].as<std::string>() == "hand", "hand slot type");
	expect(rh["hand"].as<std::string>() == "right", "hand identity");
	expect(rh["nestedAmmo"]["battleItemId"].as<int>() == clipLoaded->getId(), "nested ammo id");
	expect(rh["nestedAmmo"]["ammoType"].as<std::string>() == "STR_WP8_CLIP", "nested ammo type");
	expect(rh["loaded"]["battleItemId"].as<int>() == clipLoaded->getId(), "loaded block ammo id");
	expect(rh["loaded"]["rounds"].as<int>() == 20, "loaded rounds");
	expect(rh["researchUsable"].as<bool>() == true, "carried weapon empty requirements usable");
	expect(rh["nestedAmmo"]["researchUsable"].as<bool>() == true, "nested magazine empty requirements usable");
	expect(rh["loaded"]["researchUsable"].as<bool>() == true, "loaded block researchUsable");

	expect(entry["inventory"].size() == 2U, "two non-nested inventory rows");
	YAML::Node rowBelt, rowPack;
	for (std::size_t i = 0; i < entry["inventory"].size(); ++i)
	{
		const std::string sid = entry["inventory"][i]["slot"].as<std::string>();
		if (sid == "STR_BELT")
		{
			rowBelt = entry["inventory"][i];
		}
		else if (sid == "STR_BACK_PACK")
		{
			rowPack = entry["inventory"][i];
		}
	}
	expect(rowBelt.IsDefined() && rowPack.IsDefined(), "belt and backpack rows");
	expect(rowBelt["inventoryX"].as<int>() == 1 && rowBelt["inventoryY"].as<int>() == 0, "belt grid");
	expect(rowPack["inventoryX"].as<int>() == 2 && rowPack["inventoryY"].as<int>() == 1, "backpack grid");
	expect(rowBelt["battleItemId"].as<int>() != rowPack["battleItemId"].as<int>(), "distinct spare clip ids");
	expect(rowBelt["battleItemId"].as<int>() != clipLoaded->getId(), "spare differs from loaded magazine");
	expect(rowBelt["researchUsable"].as<bool>() == true, "spare belt clip researchUsable");
	expect(rowPack["researchUsable"].as<bool>() == true, "spare backpack clip researchUsable");

	const YAML::Node yaml1 = buildParsedSnapshot(fixture);
	const YAML::Node yaml2 = buildParsedSnapshot(fixture);
	expect(findUnitById(yaml1["units"], 1)["hands"]["right"]["battleItemId"].as<int>()
		== findUnitById(yaml2["units"], 1)["hands"]["right"]["battleItemId"].as<int>(),
		"stable ids across snapshots");

	clipSpareA->setSlot(&R.backpack);
	clipSpareA->setSlotX(0);
	clipSpareA->setSlotY(2);
	const YAML::Node afterMove = findUnitById(buildParsedSnapshot(fixture)["units"], 1);
	const YAML::Node moved = afterMove["inventory"][0];
	expect(moved["battleItemId"].as<int>() == clipSpareA->getId(), "id stable after slot move");
	expect(moved["inventoryX"].as<int>() == 0 && moved["inventoryY"].as<int>() == 2, "new grid after move");
}

static void testWp8WeaponAndExplosiveStates()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(2);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(2, 2, 0), 0);
	Wp8TestRules &R = wp8Rules();

	BattleItem *emptyRifle = new BattleItem(&R.rifle, fixture.battle->getCurrentItemId());
	emptyRifle->setSlot(&R.rightHand);
	emptyRifle->setSlotX(0);
	emptyRifle->setSlotY(0);
	unit->getInventory()->push_back(emptyRifle);
	registerBattleItem(fixture.battle, emptyRifle);

	BattleItem *dryRifle = new BattleItem(&R.rifle, fixture.battle->getCurrentItemId());
	BattleItem *dryClip = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	dryClip->setAmmoQuantity(0);
	expect(dryRifle->setAmmoItem(dryClip) == 0, "dry magazine insert");
	dryRifle->setSlot(&R.leftHand);
	dryRifle->setSlotX(0);
	dryRifle->setSlotY(0);
	unit->getInventory()->push_back(dryRifle);
	registerBattleItem(fixture.battle, dryRifle);
	registerBattleItem(fixture.battle, dryClip);

	BattleItem *laser = new BattleItem(&R.laser, fixture.battle->getCurrentItemId());
	laser->setSlot(&R.rightShoulder);
	laser->setSlotX(0);
	laser->setSlotY(0);
	unit->getInventory()->push_back(laser);
	registerBattleItem(fixture.battle, laser);

	BattleItem *grenade = new BattleItem(&R.grenade, fixture.battle->getCurrentItemId());
	grenade->setFuseTimer(2);
	placeEquippedItem(fixture.battle, unit, grenade, &R.backpack, 0, 0);

	BattleItem *prox = new BattleItem(&R.proximity, fixture.battle->getCurrentItemId());
	placeEquippedItem(fixture.battle, unit, prox, &R.belt, 0, 0);

	const YAML::Node entry = findUnitById(buildParsedSnapshot(fixture)["units"], 2);
	const YAML::Node r = entry["hands"]["right"];
	expect(r["noMagazineInserted"].as<bool>() == true, "no magazine flag");

	const YAML::Node l = entry["hands"]["left"];
	expect(l["empty"].as<bool>() == true, "dry mag empty");
	expect(l["loaded"]["rounds"].as<int>() == 0, "dry loaded rounds zero");
	expect(l["nestedAmmo"]["rounds"].as<int>() == 0, "nested dry rounds");

	YAML::Node laserNode;
	YAML::Node gnode;
	YAML::Node pnode;
	for (std::size_t i = 0; i < entry["inventory"].size(); ++i)
	{
		const std::string it = entry["inventory"][i]["itemType"].as<std::string>();
		if (it == "STR_WP8_LASER")
		{
			laserNode = entry["inventory"][i];
		}
		else if (it == "STR_WP8_GRENADE")
		{
			gnode = entry["inventory"][i];
		}
		else if (it == "STR_WP8_PROX")
		{
			pnode = entry["inventory"][i];
		}
	}
	expect(laserNode.IsDefined(), "laser row");
	expect(!laserNode["nestedAmmo"], "laser has no nested ammo");

	expect(gnode.IsDefined(), "grenade row found");
	expect(gnode["explosiveKind"].as<std::string>() == "timed", "timed grenade kind");
	expect(gnode["primed"].as<bool>() == true, "primed grenade");
	expect(gnode["fuseTimer"].as<int>() == 2, "fuse value");

	expect(pnode.IsDefined(), "proximity row");
	expect(pnode["explosiveKind"].as<std::string>() == "proximity", "proximity kind");
}

static void testActorGroundInventoryExportOnDiscoveredTile()
{
	Wp8TestRules &R = wp8Rules();
	RuleInventory *ground = snapshotTestGroundInventoryRule();
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(20);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 1, 0), 0);
	placeUnitOnDiscoveredTile(fixture, unit, Position(1, 1, 0));
	Tile *tile = unit->getTile();
	expect(tile != 0, "unit tile wired");
	if (tile == 0)
	{
		return;
	}

	BattleItem *a = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	a->setSlotX(0);
	a->setSlotY(0);
	tile->addItem(a, ground);
	registerBattleItem(fixture.battle, a);

	const YAML::Node entry = findUnitByIdDirect(fixture, 20, 4);
	expect(entry.IsDefined(), "friendly unit exported");
	expect(entry["position"].IsSequence() && entry["position"].size() == 3, "position is [x,y,z] triple");
	expect(entry["position"][0].as<int>() == 1 && entry["position"][1].as<int>() == 1 && entry["position"][2].as<int>() == 0,
		"position matches actor");
	expect(entry["groundInventory"].IsDefined(), "groundInventory under friendly unit");
	expect(entry["groundInventory"].size() == 1, "one ground row");
	expect(entry["groundInventory"][0]["battleItemId"].as<int>() == a->getId(), "row id matches engine");
	expect(entry["groundInventory"][0]["itemType"].as<std::string>() == "STR_WP8_CLIP", "itemType serialized");
	expect(entry["groundInventory"][0]["slot"].as<std::string>() == "STR_GROUND", "ground slot id");
	expect(entry["groundInventory"][0]["inventorySlotType"].as<std::string>() == "ground", "inventorySlotType ground");
	expect(entry["groundInventory"][0]["inventoryX"].IsDefined() && entry["groundInventory"][0]["inventoryY"].IsDefined(),
		"ground grid coords present");

	adoptGroundItemForBattleCleanup(fixture.battle, tile, a);
}

static void testGroundInventoryDeterministicOrderTwoSameTypeItems()
{
	Wp8TestRules &R = wp8Rules();
	RuleInventory *ground = snapshotTestGroundInventoryRule();
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(33);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 3, 0), 0);
	placeUnitOnDiscoveredTile(fixture, unit, Position(1, 3, 0));
	Tile *tile = unit->getTile();
	if (tile == 0)
	{
		return;
	}

	BattleItem *first = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	first->setSlotX(1);
	first->setSlotY(0);
	tile->addItem(first, ground);
	registerBattleItem(fixture.battle, first);

	BattleItem *second = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	second->setSlotX(0);
	second->setSlotY(1);
	tile->addItem(second, ground);
	registerBattleItem(fixture.battle, second);

	const YAML::Node entry = findUnitByIdDirect(fixture, 33, 4);
	expect(entry["groundInventory"].IsDefined() && entry["groundInventory"].size() == 2, "two rows exported");
	// Sorted by slotY, then slotX, then battleItemId — (1,0) before (0,1).
	expect(entry["groundInventory"][0]["battleItemId"].as<int>() == first->getId(), "lower Y row first");
	expect(entry["groundInventory"][1]["battleItemId"].as<int>() == second->getId(), "higher Y row second");

	adoptGroundItemForBattleCleanup(fixture.battle, tile, first);
	adoptGroundItemForBattleCleanup(fixture.battle, tile, second);
}

static void testUndiscoveredActorTileOmitsGroundInventory()
{
	Wp8TestRules &R = wp8Rules();
	RuleInventory *ground = snapshotTestGroundInventoryRule();
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(21);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(2, 2, 0), 0);
	Tile *tile = fixture.battle->getTile(Position(2, 2, 0));
	expect(tile != 0, "tile for undiscovered test");
	if (tile == 0)
	{
		return;
	}
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("snapshot_undisc_ground");
	}
	MapData *floor = new MapData(ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	tile->setMapData(floor, 1, 0, O_FLOOR);
	// Layer 2 not discovered: appendActorGroundInventory must not export.
	unit->setPosition(Position(2, 2, 0));
	unit->setTile(tile, fixture.battle->getTile(Position(2, 2, -1)));

	BattleItem *clip = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	clip->setSlotX(0);
	clip->setSlotY(0);
	tile->addItem(clip, ground);
	registerBattleItem(fixture.battle, clip);

	const YAML::Node entry = findUnitByIdDirect(fixture, 21, 4);
	expect(!entry["groundInventory"], "no groundInventory when tile not discovered for observer");

	adoptGroundItemForBattleCleanup(fixture.battle, tile, clip);
}

static void testRemoteGroundItemNotInActorGroundInventory()
{
	Wp8TestRules &R = wp8Rules();
	RuleInventory *ground = snapshotTestGroundInventoryRule();
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(22);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 1, 0), 0);
	placeUnitOnDiscoveredTile(fixture, unit, Position(1, 1, 0));

	Tile *remote = fixture.battle->getTile(Position(3, 0, 0));
	expect(remote != 0, "remote tile");
	if (remote == 0)
	{
		return;
	}
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("snapshot_remote_ground");
	}
	MapData *floor = new MapData(ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	remote->setMapData(floor, 1, 0, O_FLOOR);
	// Remote tile: not discovered for player layer — must not appear under actor groundInventory.
	BattleItem *farClip = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	farClip->setSlotX(0);
	farClip->setSlotY(0);
	remote->addItem(farClip, ground);
	registerBattleItem(fixture.battle, farClip);

	const YAML::Node entry = findUnitByIdDirect(fixture, 22, 4);
	expect(!entry["groundInventory"] || entry["groundInventory"].size() == 0, "no remote tile ground leak");

	adoptGroundItemForBattleCleanup(fixture.battle, remote, farClip);
}

static void testTwoSoldiersSameTileEachGroundInventoryListsSharedGround()
{
	Wp8TestRules &R = wp8Rules();
	RuleInventory *ground = snapshotTestGroundInventoryRule();
	TacticalFixture fixture;
	Soldier *s1 = fixture.addRosterSoldier(30);
	Soldier *s2 = fixture.addRosterSoldier(31);
	BattleUnit *u1 = fixture.addSoldierUnit(s1, Position(1, 2, 0), 0);
	BattleUnit *u2 = fixture.addSoldierUnit(s2, Position(1, 2, 0), 0);
	placeUnitOnDiscoveredTile(fixture, u1, Position(1, 2, 0));
	placeUnitOnDiscoveredTile(fixture, u2, Position(1, 2, 0));
	Tile *tile = u1->getTile();
	expect(tile == u2->getTile(), "both on same tile");
	if (tile == 0)
	{
		return;
	}

	BattleItem *clip = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	clip->setSlotX(0);
	clip->setSlotY(0);
	tile->addItem(clip, ground);
	registerBattleItem(fixture.battle, clip);

	const YAML::Node root = CompanionSnapshotBuild::buildSnapshot(fixture.battle, &fixture.game, 4, false);
	const YAML::Node units = root["units"];
	const YAML::Node e1 = findUnitById(units, 30);
	const YAML::Node e2 = findUnitById(units, 31);
	expect(e1["groundInventory"].IsDefined() && e1["groundInventory"].size() == 1, "first soldier sees ground");
	expect(e2["groundInventory"].IsDefined() && e2["groundInventory"].size() == 1, "second soldier sees ground");
	expect(e1["groundInventory"][0]["battleItemId"].as<int>() == clip->getId(), "u1 battleItemId");
	expect(e2["groundInventory"][0]["battleItemId"].as<int>() == clip->getId(), "u2 same id — per-unit list unambiguous");

	adoptGroundItemForBattleCleanup(fixture.battle, tile, clip);
}

static void testV5SnapshotToleratesGroundInventoryField()
{
	Wp8TestRules &R = wp8Rules();
	RuleInventory *ground = snapshotTestGroundInventoryRule();
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(32);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(0, 3, 0), 0);
	placeUnitOnDiscoveredTile(fixture, unit, Position(0, 3, 0));
	Tile *tile = unit->getTile();
	BattleItem *clip = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	clip->setSlotX(0);
	clip->setSlotY(0);
	tile->addItem(clip, ground);
	registerBattleItem(fixture.battle, clip);

	const YAML::Node entry = findUnitByIdDirect(fixture, 32, 5);
	expect(entry["groundInventory"].IsDefined() && entry["groundInventory"].size() >= 1, "v5 includes groundInventory");

	adoptGroundItemForBattleCleanup(fixture.battle, tile, clip);
}

static void testWp8MedikitChargesAndCarriedWeight()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(3);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(0, 0, 0), 0);
	Wp8TestRules &R = wp8Rules();
	BattleItem *mk = new BattleItem(&R.medikit, fixture.battle->getCurrentItemId());
	mk->setHealQuantity(2);
	mk->setPainKillerQuantity(1);
	mk->setStimulantQuantity(0);
	placeEquippedItem(fixture.battle, unit, mk, &R.belt, 2, 0);

	const YAML::Node entry = findUnitById(buildParsedSnapshot(fixture)["units"], 3);
	YAML::Node med;
	for (std::size_t i = 0; i < entry["inventory"].size(); ++i)
	{
		if (entry["inventory"][i]["itemType"].as<std::string>() == "STR_WP8_MEDIKIT")
		{
			med = entry["inventory"][i];
			break;
		}
	}
	expect(med.IsDefined(), "medikit row");
	expect(med["medikitCharges"]["heal"].as<int>() == 2, "medikit heal");
	expect(med["medikitCharges"]["painKiller"].as<int>() == 1, "medikit pk");
	expect(med["medikitCharges"]["stimulant"].as<int>() == 0, "medikit stim");

	const int engineWeight = unit->getCarriedWeight(0);
	expect(entry["carriedWeight"].IsDefined()
		&& entry["carriedWeight"].as<int>() == engineWeight, "carriedWeight matches engine");
	expect(entry["strength"].as<int>() == unit->getBaseStats()->strength, "strength exported");
	if (engineWeight > 0)
	{
		const double ratio = static_cast<double>(unit->getBaseStats()->strength) / static_cast<double>(engineWeight);
		expect(entry["encumbranceRatio"].IsDefined()
			&& std::fabs(entry["encumbranceRatio"].as<double>() - ratio) < 1e-6, "encumbrance ratio");
	}
}

static void testHostileDeathPerceptionWhenTileInFriendlySight()
{
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(40);
	BattleUnit *friendly = fixture.addSoldierUnit(soldier, Position(0, 0, 0), 0);
	placeUnitOnDiscoveredTile(fixture, friendly, Position(0, 0, 0));
	Tile *deathTile = fixture.battle->getTile(Position(1, 0, 0));
	expect(deathTile != 0, "death tile");
	deathTile->setDiscovered(true, 2);
	deathTile->setVisible(+1);
	fixture.battle->getCompanionPerceptionLog().tryRecordHostileDeathObserved(
		fixture.battle, Position(1, 0, 0), true);
	const YAML::Node root = buildParsedSnapshot(fixture);
	const YAML::Node events = root["perceptionLog"]["events"];
	expect(events.size() == 1, "one hostile death perception");
	expect(events[0]["type"].as<std::string>() == "hostile_casualty_tile_observed", "death cue type");
	expect(events[0]["confidence"].as<std::string>() == "possible_ground_equipment", "inventory inference");
	expect(events[0]["positionKnown"].as<bool>(), "position known");
	expect(events[0]["position"][0].as<int>() == 1 && events[0]["position"][1].as<int>() == 0
		&& events[0]["position"][2].as<int>() == 0, "death tile coords");
}

static void testHostileDeathPerceptionOmitsWhenTileNotInSight()
{
	TacticalFixture fixture;
	Tile *deathTile = fixture.battle->getTile(Position(1, 0, 0));
	expect(deathTile != 0, "death tile");
	deathTile->setDiscovered(true, 2);
	deathTile->setVisible(0);
	fixture.battle->getCompanionPerceptionLog().tryRecordHostileDeathObserved(
		fixture.battle, Position(1, 0, 0), true);
	const YAML::Node root = buildParsedSnapshot(fixture);
	expect(!root["perceptionLog"]["events"].IsDefined() || root["perceptionLog"]["events"].size() == 0,
		"no perception when tile not in friendly sight");
}

/**
 * WP8B2: Companion RELOAD/UNLOAD used RuleItem::clipSize on the firearm; vanilla UFO rifles use clipSize 0
 * on the weapon and put capacity on STR_RIFLE_CLIP. Magazine eligibility must follow needsAmmo + compatibleAmmo.
 */
static RuleItem *scavengingAlienWeaponRules()
{
	static RuleItem *rules = 0;
	if (rules != 0)
	{
		return rules;
	}
	RuleResearch *gate = new RuleResearch("STR_SCAVENGE_ALIEN_GATE");
	YAML::Node rn;
	rn["name"] = "STR_SCAVENGE_ALIEN_GATE";
	gate->load(rn, 0);
	fixtureMod().registerTestResearchRule(gate);
	rules = new RuleItem("STR_SCAVENGE_ALIEN_WEAPON");
	YAML::Node inode;
	inode["battleType"] = static_cast<int>(BT_FIREARM);
	inode["clipSize"] = -1;
	inode["requires"] = YAML::Load("[STR_SCAVENGE_ALIEN_GATE]");
	rules->load(inode, &fixtureMod(), 0);
	return rules;
}

static void testGroundInventoryResearchUsableUnresearchedAlienWeapon()
{
	RuleInventory *ground = snapshotTestGroundInventoryRule();
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(40);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 1, 0), 0);
	placeUnitOnDiscoveredTile(fixture, unit, Position(1, 1, 0));
	Tile *tile = unit->getTile();
	if (tile == 0)
	{
		return;
	}
	BattleItem *weapon = new BattleItem(scavengingAlienWeaponRules(), fixture.battle->getCurrentItemId());
	weapon->setSlotX(0);
	weapon->setSlotY(0);
	tile->addItem(weapon, ground);
	registerBattleItem(fixture.battle, weapon);

	const YAML::Node entry = findUnitByIdDirect(fixture, 40, 4);
	expect(entry["groundInventory"][0]["researchUsable"].as<bool>() == false,
		"unresearched alien ground weapon researchUsable false");
	const bool helperUsable = companionItemResearchUsable(&fixture.game, scavengingAlienWeaponRules());
	expect(!helperUsable, "helper agrees unresearched");

	expect(!helperUsable, "PICKUP-aligned helper false when unresearched");

	adoptGroundItemForBattleCleanup(fixture.battle, tile, weapon);
}

static void testGroundInventoryResearchUsableResearchedAlienWeapon()
{
	RuleInventory *ground = snapshotTestGroundInventoryRule();
	TacticalFixture fixture;
	fixture.game.addFinishedResearchSimple(fixtureMod().getResearch("STR_SCAVENGE_ALIEN_GATE", true));
	Soldier *soldier = fixture.addRosterSoldier(41);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 1, 0), 0);
	placeUnitOnDiscoveredTile(fixture, unit, Position(1, 1, 0));
	Tile *tile = unit->getTile();
	if (tile == 0)
	{
		return;
	}
	BattleItem *weapon = new BattleItem(scavengingAlienWeaponRules(), fixture.battle->getCurrentItemId());
	weapon->setSlotX(0);
	weapon->setSlotY(0);
	tile->addItem(weapon, ground);
	registerBattleItem(fixture.battle, weapon);

	const YAML::Node entry = findUnitByIdDirect(fixture, 41, 4);
	expect(entry["groundInventory"][0]["researchUsable"].as<bool>() == true,
		"researched alien ground weapon researchUsable true");

	adoptGroundItemForBattleCleanup(fixture.battle, tile, weapon);
}

static void testGroundItemsObservationExportsResearchUsableWhenIdentified()
{
	TacticalFixture fixture;
	MapDataSet ds("scavenge_ground_items");
	MapData *floor = new MapData(&ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	Tile *tile = fixture.battle->getTile(Position(1, 1, 0));
	tile->setMapData(floor, 1, 0, O_FLOOR);
	tile->setDiscovered(true, 2);
	tile->setVisible(+1);
	RuleInventory ground("STR_GROUND");
	fixture.game.addFinishedResearchSimple(fixtureMod().getResearch("STR_SCAVENGE_ALIEN_GATE", true));
	BattleItem *weapon = new BattleItem(scavengingAlienWeaponRules(), fixture.battle->getCurrentItemId());
	tile->addItem(weapon, &ground);
	registerBattleItem(fixture.battle, weapon);

	const YAML::Node root = buildParsedSnapshot(fixture);
	const YAML::Node observations = root["groundItems"]["observations"];
	expect(observations.size() == 1, "groundItems observation present");
	expect(observations[0]["items"][0]["itemType"].as<std::string>() == "STR_SCAVENGE_ALIEN_WEAPON",
		"researched weapon itemType visible");
	expect(observations[0]["items"][0]["researchUsable"].as<bool>() == true,
		"groundItems researchUsable true when identified");

	adoptGroundItemForBattleCleanup(fixture.battle, tile, weapon);
}

static void testCompanionItemResearchUsableMultipleRequirements()
{
	Mod &mod = fixtureMod();
	RuleResearch *r1 = new RuleResearch("STR_SCAVENGE_REQ_A");
	RuleResearch *r2 = new RuleResearch("STR_SCAVENGE_REQ_B");
	YAML::Node n1, n2;
	n1["name"] = "STR_SCAVENGE_REQ_A";
	n2["name"] = "STR_SCAVENGE_REQ_B";
	r1->load(n1, 0);
	r2->load(n2, 0);
	mod.registerTestResearchRule(r1);
	mod.registerTestResearchRule(r2);
	RuleItem rules("STR_SCAVENGE_MULTI_REQ_ITEM");
	YAML::Node inode;
	inode["battleType"] = static_cast<int>(BT_FIREARM);
	inode["clipSize"] = -1;
	inode["requires"] = YAML::Load("[STR_SCAVENGE_REQ_A, STR_SCAVENGE_REQ_B]");
	rules.load(inode, &mod, 0);
	SavedGame save;
	expect(!companionItemResearchUsable(&save, &rules), "none researched");
	save.addFinishedResearchSimple(mod.getResearch("STR_SCAVENGE_REQ_A", true));
	expect(!companionItemResearchUsable(&save, &rules), "one of two researched");
	save.addFinishedResearchSimple(mod.getResearch("STR_SCAVENGE_REQ_B", true));
	expect(companionItemResearchUsable(&save, &rules), "all researched");
}

static void testCompanionItemResearchUsableNoCampaignOrEmptyReqs()
{
	RuleItem emptyReqs("STR_SCAVENGE_NO_REQ");
	YAML::Node node;
	node["battleType"] = static_cast<int>(BT_FIREARM);
	node["clipSize"] = -1;
	emptyReqs.load(node, &fixtureMod(), 0);
	SavedGame save;
	expect(companionItemResearchUsable(&save, &emptyReqs), "empty requirements usable");
	expect(!companionItemResearchUsable(0, &emptyReqs), "missing campaign not usable");
}

static RuleItem *scavengingGatedCarriedClipRules()
{
	static RuleItem *rules = 0;
	if (rules != 0)
	{
		return rules;
	}
	RuleResearch *gate = new RuleResearch("STR_SCAVENGE_CARRIED_CLIP_GATE");
	YAML::Node rn;
	rn["name"] = "STR_SCAVENGE_CARRIED_CLIP_GATE";
	gate->load(rn, 0);
	fixtureMod().registerTestResearchRule(gate);
	rules = new RuleItem("STR_SCAVENGE_CARRIED_GATED_CLIP");
	YAML::Node inode;
	inode["battleType"] = static_cast<int>(BT_AMMO);
	inode["clipSize"] = 20;
	inode["requires"] = YAML::Load("[STR_SCAVENGE_CARRIED_CLIP_GATE]");
	rules->load(inode, &fixtureMod(), 0);
	return rules;
}

static RuleItem *scavengingGatedCarriedWeaponRules()
{
	static RuleItem *rules = 0;
	if (rules != 0)
	{
		return rules;
	}
	RuleResearch *gate = new RuleResearch("STR_SCAVENGE_CARRIED_WEAPON_GATE");
	YAML::Node rn;
	rn["name"] = "STR_SCAVENGE_CARRIED_WEAPON_GATE";
	gate->load(rn, 0);
	fixtureMod().registerTestResearchRule(gate);
	rules = new RuleItem("STR_SCAVENGE_CARRIED_GATED_WEAPON");
	YAML::Node inode;
	inode["battleType"] = static_cast<int>(BT_FIREARM);
	inode["clipSize"] = 20;
	inode["compatibleAmmo"] = YAML::Load("[STR_SCAVENGE_CARRIED_GATED_CLIP]");
	inode["requires"] = YAML::Load("[STR_SCAVENGE_CARRIED_WEAPON_GATE]");
	rules->load(inode, &fixtureMod(), 0);
	return rules;
}

static void testCarriedEquipmentResearchUsableSplitWeaponAmmoGates()
{
	Wp8TestRules &R = wp8Rules();
	RuleItem *weaponRules = scavengingGatedCarriedWeaponRules();
	RuleItem *clipRules = scavengingGatedCarriedClipRules();
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(60);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 1, 0), 0);
	BattleItem *rifle = new BattleItem(weaponRules, fixture.battle->getCurrentItemId());
	BattleItem *loadedClip = new BattleItem(clipRules, fixture.battle->getCurrentItemId());
	BattleItem *spareClip = new BattleItem(clipRules, fixture.battle->getCurrentItemId());
	expect(rifle->setAmmoItem(loadedClip) == 0, "gated rifle accepts gated clip");
	rifle->setSlot(&R.rightHand);
	rifle->setSlotX(0);
	rifle->setSlotY(0);
	unit->getInventory()->push_back(rifle);
	registerBattleItem(fixture.battle, rifle);
	registerBattleItem(fixture.battle, loadedClip);
	placeEquippedItem(fixture.battle, unit, spareClip, &R.belt, 0, 0);

	const YAML::Node none = findUnitById(buildParsedSnapshot(fixture)["units"], 60);
	const YAML::Node rhNone = none["hands"]["right"];
	expect(rhNone["researchUsable"].as<bool>() == false, "unresearched carried weapon");
	expect(rhNone["nestedAmmo"]["researchUsable"].as<bool>() == false, "unresearched nested magazine");
	expect(rhNone["loaded"]["researchUsable"].as<bool>() == false, "unresearched loaded magazine");
	expect(none["inventory"][0]["researchUsable"].as<bool>() == false, "unresearched spare magazine");

	fixture.game.addFinishedResearchSimple(fixtureMod().getResearch("STR_SCAVENGE_CARRIED_WEAPON_GATE", true));
	const YAML::Node weaponOnly = findUnitById(buildParsedSnapshot(fixture)["units"], 60);
	const YAML::Node rhWeapon = weaponOnly["hands"]["right"];
	expect(rhWeapon["researchUsable"].as<bool>() == true, "researched carried weapon");
	expect(rhWeapon["nestedAmmo"]["researchUsable"].as<bool>() == false, "nested still unresearched clip");
	expect(weaponOnly["inventory"][0]["researchUsable"].as<bool>() == false, "spare still unresearched");

	fixture.game.addFinishedResearchSimple(fixtureMod().getResearch("STR_SCAVENGE_CARRIED_CLIP_GATE", true));
	const YAML::Node both = findUnitById(buildParsedSnapshot(fixture)["units"], 60);
	const YAML::Node rhBoth = both["hands"]["right"];
	expect(rhBoth["researchUsable"].as<bool>() == true, "both gates weapon usable");
	expect(rhBoth["nestedAmmo"]["researchUsable"].as<bool>() == true, "both gates nested usable");
	expect(rhBoth["loaded"]["researchUsable"].as<bool>() == true, "both gates loaded usable");
	expect(both["inventory"][0]["researchUsable"].as<bool>() == true, "both gates spare usable");
}

static void testCarriedEquipmentResearchUsableNullCampaignFailClosed()
{
	Wp8TestRules &R = wp8Rules();
	TacticalFixture fixture;
	Soldier *soldier = fixture.addRosterSoldier(61);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 1, 0), 0);
	BattleItem *rifle = new BattleItem(&R.rifle, fixture.battle->getCurrentItemId());
	BattleItem *clip = new BattleItem(&R.clip, fixture.battle->getCurrentItemId());
	expect(rifle->setAmmoItem(clip) == 0, "rifle load");
	rifle->setSlot(&R.rightHand);
	rifle->setSlotX(0);
	rifle->setSlotY(0);
	unit->getInventory()->push_back(rifle);
	registerBattleItem(fixture.battle, rifle);
	registerBattleItem(fixture.battle, clip);
	placeEquippedItem(fixture.battle, unit, new BattleItem(&R.clip, fixture.battle->getCurrentItemId()), &R.belt, 0, 0);

	const YAML::Node root = CompanionSnapshotBuild::buildSnapshot(fixture.battle, 0, 4, false);
	const YAML::Node entry = findUnitById(root["units"], 61);
	expect(entry["hands"]["right"]["researchUsable"].as<bool>() == false, "null campaign hand fail closed");
	expect(entry["hands"]["right"]["nestedAmmo"]["researchUsable"].as<bool>() == false, "null campaign nested fail closed");
	expect(entry["inventory"][0]["researchUsable"].as<bool>() == false, "null campaign inventory fail closed");
}

static void testGroundInventoryNestedMagazineResearchUsable()
{
	RuleInventory *ground = snapshotTestGroundInventoryRule();
	TacticalFixture fixture;
	fixture.game.addFinishedResearchSimple(fixtureMod().getResearch("STR_SCAVENGE_CARRIED_WEAPON_GATE", true));
	Soldier *soldier = fixture.addRosterSoldier(62);
	BattleUnit *unit = fixture.addSoldierUnit(soldier, Position(1, 1, 0), 0);
	placeUnitOnDiscoveredTile(fixture, unit, Position(1, 1, 0));
	Tile *tile = unit->getTile();
	BattleItem *weapon = new BattleItem(scavengingGatedCarriedWeaponRules(), fixture.battle->getCurrentItemId());
	BattleItem *clip = new BattleItem(scavengingGatedCarriedClipRules(), fixture.battle->getCurrentItemId());
	expect(weapon->setAmmoItem(clip) == 0, "ground gated weapon load");
	weapon->setSlotX(0);
	weapon->setSlotY(0);
	tile->addItem(weapon, ground);
	registerBattleItem(fixture.battle, weapon);
	registerBattleItem(fixture.battle, clip);

	const YAML::Node entry = findUnitByIdDirect(fixture, 62, 4);
	expect(entry["groundInventory"][0]["researchUsable"].as<bool>() == true, "ground weapon researched");
	expect(entry["groundInventory"][0]["nestedAmmo"]["researchUsable"].as<bool>() == false, "ground nested clip unresearched");

	adoptGroundItemForBattleCleanup(fixture.battle, tile, weapon);
}

static void testGroundItemsUnresearchedExportsResearchUsableFalseWithoutItemType()
{
	TacticalFixture fixture;
	MapDataSet ds("scavenge_ground_unres");
	MapData *floor = new MapData(&ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	Tile *tile = fixture.battle->getTile(Position(2, 1, 0));
	tile->setMapData(floor, 1, 0, O_FLOOR);
	tile->setDiscovered(true, 2);
	tile->setVisible(+1);
	RuleInventory ground("STR_GROUND");
	BattleItem *weapon = new BattleItem(scavengingAlienWeaponRules(), fixture.battle->getCurrentItemId());
	tile->addItem(weapon, &ground);
	registerBattleItem(fixture.battle, weapon);

	const YAML::Node root = buildParsedSnapshot(fixture);
	const YAML::Node item = root["groundItems"]["observations"][0]["items"][0];
	expect(!item["itemType"], "unresearched ground item omits itemType");
	expect(item["itemCategory"].as<std::string>() == "unidentified_firearm", "unidentified category");
	expect(item["researchUsable"].as<bool>() == false, "researchUsable false without leaking tree");

	adoptGroundItemForBattleCleanup(fixture.battle, tile, weapon);
}

static void testWp8VanillaRifleMagazineEligibilityRegression()
{
	RuleItem rifleRules("STR_TEST_VANILLA_RIFLE");
	YAML::Node rifleNode;
	rifleNode["battleType"] = static_cast<int>(BT_FIREARM);
	rifleNode["clipSize"] = 0;
	rifleNode["compatibleAmmo"] = YAML::Load("[STR_TEST_VANILLA_CLIP]");
	rifleRules.load(rifleNode, &fixtureMod(), 0);

	RuleItem clipRules("STR_TEST_VANILLA_CLIP");
	YAML::Node clipNode;
	clipNode["battleType"] = static_cast<int>(BT_AMMO);
	clipNode["clipSize"] = 20;
	clipRules.load(clipNode, &fixtureMod(), 0);

	int nextId = 9000;
	BattleItem rifle(&rifleRules, &nextId);
	BattleItem clip(&clipRules, &nextId);
	expect(rifle.setAmmoItem(&clip) == 0, "vanilla-style rifle accepts magazine");
	expect(rifle.needsAmmo(), "loaded magazine-fed rifle uses needsAmmo for round tracking");
	const bool magazineFedLikeCompanion =
		rifle.needsAmmo() && !rifle.getRules()->getCompatibleAmmo()->empty();
	expect(magazineFedLikeCompanion, "clipSize 0 + compatibleAmmo must still be magazine-fed");
	(void)rifle.setAmmoItem(0);
}

#ifdef main
#undef main
#endif

#define SNAPSHOT_TEST_STEP(name) \
	do \
	{ \
		std::cerr << "companion_snapshot_serialization_test: " #name << std::endl; \
		name(); \
	} while (0)

int main()
{
	SNAPSHOT_TEST_STEP(testHealthySoldierWp2Fields);
	SNAPSHOT_TEST_STEP(testUnconsciousOmitsActionCosts);
	SNAPSHOT_TEST_STEP(testRosterCasualtySnapshotUnsettledWhileCollapsing);
	SNAPSHOT_TEST_STEP(testSoldierDownRecordsAfterKillSoldierRemovesLiveRoster);
	SNAPSHOT_TEST_STEP(testSoldierCasualtyUnconsciousThenRecoverEpisode);
	SNAPSHOT_TEST_STEP(testMindControlledOriginalSoldierFatalityAfterRosterErase);
	SNAPSHOT_TEST_STEP(testRosterCasualtyLedgerSeededAfterBattleLoad);
	SNAPSHOT_TEST_STEP(testRosterCasualtyLedgerSeededForDeadAfterKillSoldier);
	SNAPSHOT_TEST_STEP(testVisibleHostileObservationOnly);
	SNAPSHOT_TEST_STEP(testVisibleHostileExportsObservedUnitTypeWhenSpotted);
	SNAPSHOT_TEST_STEP(testVisibleCyberdiscUsesUnitTypeNotRace);
	SNAPSHOT_TEST_STEP(testHiddenHostileOmitted);
	SNAPSHOT_TEST_STEP(testMindControlledRosterSoldierNoFriendlyLeak);
	SNAPSHOT_TEST_STEP(testHiddenMindControlledRosterSoldierOmitted);
	SNAPSHOT_TEST_STEP(testRosterSoldierIgnoreMeStillExported);
	SNAPSHOT_TEST_STEP(testSerializedYamlFieldNames);
	SNAPSHOT_TEST_STEP(testGroundItemsBlockPresentOnV4);
	SNAPSHOT_TEST_STEP(testGroundWeaponExportsInLos);
	SNAPSHOT_TEST_STEP(testPerceptionLogSessionMatchesEventLogWhenUnset);
	SNAPSHOT_TEST_STEP(testGroundItemRememberedOffLos);
	SNAPSHOT_TEST_STEP(testWp8InventoryObservationIdentityAndPlacement);
	SNAPSHOT_TEST_STEP(testWp8WeaponAndExplosiveStates);
	SNAPSHOT_TEST_STEP(testActorGroundInventoryExportOnDiscoveredTile);
	SNAPSHOT_TEST_STEP(testGroundInventoryDeterministicOrderTwoSameTypeItems);
	SNAPSHOT_TEST_STEP(testUndiscoveredActorTileOmitsGroundInventory);
	SNAPSHOT_TEST_STEP(testRemoteGroundItemNotInActorGroundInventory);
	SNAPSHOT_TEST_STEP(testTwoSoldiersSameTileEachGroundInventoryListsSharedGround);
	SNAPSHOT_TEST_STEP(testV5SnapshotToleratesGroundInventoryField);
	SNAPSHOT_TEST_STEP(testWp8MedikitChargesAndCarriedWeight);
	SNAPSHOT_TEST_STEP(testHostileDeathPerceptionWhenTileInFriendlySight);
	SNAPSHOT_TEST_STEP(testHostileDeathPerceptionOmitsWhenTileNotInSight);
	SNAPSHOT_TEST_STEP(testWp8VanillaRifleMagazineEligibilityRegression);
	SNAPSHOT_TEST_STEP(testGroundInventoryResearchUsableUnresearchedAlienWeapon);
	SNAPSHOT_TEST_STEP(testGroundInventoryResearchUsableResearchedAlienWeapon);
	SNAPSHOT_TEST_STEP(testGroundItemsObservationExportsResearchUsableWhenIdentified);
	SNAPSHOT_TEST_STEP(testGroundItemsUnresearchedExportsResearchUsableFalseWithoutItemType);
	SNAPSHOT_TEST_STEP(testCompanionItemResearchUsableMultipleRequirements);
	SNAPSHOT_TEST_STEP(testCompanionItemResearchUsableNoCampaignOrEmptyReqs);
	SNAPSHOT_TEST_STEP(testCarriedEquipmentResearchUsableSplitWeaponAmmoGates);
	SNAPSHOT_TEST_STEP(testCarriedEquipmentResearchUsableNullCampaignFailClosed);
	SNAPSHOT_TEST_STEP(testGroundInventoryNestedMagazineResearchUsable);

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_snapshot_serialization_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
