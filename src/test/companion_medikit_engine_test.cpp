/*
 * Open X-Com Companion — medikit executor engine tests (M1-WP9 S2).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/Position.h"
#include "../Engine/CompanionMedikitExecutionCore.h"
#include "../Mod/Mod.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Armor.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <string>

#ifdef main
#undef main
#endif

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

static void registerBattleItem(SavedBattleGame *battle, BattleItem *item)
{
	if (battle != 0 && item != 0)
	{
		battle->getItems()->push_back(item);
	}
}

static RuleInventory *makeHandInventory(Mod &mod, const char *id)
{
	RuleInventory *inv = new RuleInventory(id);
	YAML::Node rh;
	rh["id"] = id;
	rh["x"] = 0;
	rh["y"] = 0;
	rh["type"] = static_cast<int>(INV_HAND);
	mod.registerTestInventoryRule(inv);
	inv->load(rh, 0);
	return inv;
}

static MapData *makeFloorMapData(MapDataSet *ds)
{
	MapData *floor = new MapData(ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
	return floor;
}

static MapDataSet *medikitHarnessFloorMapDataSet()
{
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("medikit_harness_floor");
	}
	return ds;
}

// Mirrors companion_guided_launch_engine_test.cpp wireOpenFloor (per-tile floor MapData).
static void wireMedikitOpenFloor(
	SavedBattleGame *battle,
	int x0,
	int y0,
	int x1,
	int y1,
	int z)
{
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			Tile *tile = battle->getTile(Position(x, y, z));
			if (tile == 0)
			{
				continue;
			}
			tile->setDiscovered(true, 2);
			tile->setMapData(makeFloorMapData(medikitHarnessFloorMapDataSet()), 1, 0, O_FLOOR);
		}
	}
}

static void wireUnitTile(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	expect(tile != 0, "tile exists");
	if (tile == 0)
	{
		return;
	}
	tile->setDiscovered(true, 2);
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("medikit_test");
	}
	tile->setMapData(makeFloorMapData(ds), 1, 0, O_FLOOR);
	unit->setPosition(pos);
	battle->setUnitPosition(unit, pos);
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
}

static RuleItem *makeMedikitRules(Mod &mod)
{
	RuleItem *rules = new RuleItem("STR_WP9_TEST_MEDIKIT");
	YAML::Node m;
	m["battleType"] = static_cast<int>(BT_MEDIKIT);
	m["tuUse"] = 10;
	m["flatRate"] = true;
	m["heal"] = 2;
	m["painKiller"] = 2;
	m["stimulant"] = 2;
	m["woundRecovery"] = 1;
	m["healthRecovery"] = 3;
	m["stunRecovery"] = 4;
	m["energyRecovery"] = 10;
	m["invWidth"] = 1;
	m["invHeight"] = 2;
	m["weight"] = 3;
	rules->load(m, &mod, 0);
	mod.registerTestItemRule(rules);
	return rules;
}

static BattleUnit *makeMedic(Mod &mod, SavedBattleGame &battle, int soldierId, const Position &pos, int tu)
{
	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier *soldier = new Soldier(&rs, &ar, soldierId);
	BattleUnit *unit = new BattleUnit(soldier, 0);
	battle.getUnits()->push_back(unit);
	unit->setTimeUnits(tu);
	wireUnitTile(&battle, unit, pos);
	return unit;
}

static BattleUnit *makePatient(
	SavedBattleGame &battle,
	int soldierId,
	const char *yamlExtra)
{
	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier *soldier = new Soldier(&rs, &ar, soldierId);
	BattleUnit *unit = new BattleUnit(soldier, 0);
	battle.getUnits()->push_back(unit);
	YAML::Node node = YAML::Load(yamlExtra);
	unit->load(node);
	return unit;
}

static void placeUnconsciousCoTile(BattleUnit *patient, BattleUnit *medic)
{
	patient->setPosition(medic->getPosition());
}

static BattleItem *equipMedikit(SavedBattleGame &battle, Mod &mod, BattleUnit *owner, const char *hand)
{
	RuleInventory *slot = mod.getInventory(hand, true);
	BattleItem *mk = new BattleItem(makeMedikitRules(mod), battle.getCurrentItemId());
	mk->setSlot(slot);
	mk->setSlotX(0);
	mk->setSlotY(0);
	mk->moveToOwner(owner);
	registerBattleItem(&battle, mk);
	return mk;
}

static void authorizeCompanionActor(SavedBattleGame &battle, int soldierId)
{
	SavedGame campaign;
	battle.setGeoscapeSaveHint(&campaign);
	battle.assignCompanionTacticalSoldierToAi(soldierId);
}

static void testHealWoundedPart()
{
	Mod mod;
	makeHandInventory(mod, "STR_RIGHT_HAND");
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(6, 6, 1);
	battle.initUtilities(&mod);

	BattleUnit *medic = makeMedic(mod, battle, 1, Position(1, 1, 0), 100);
	authorizeCompanionActor(battle, 1);
	BattleUnit *patient = makePatient(
		battle, 2,
		"status: 7\nhealth: 20\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,1,0,0,0,0]\n");
	placeUnconsciousCoTile(patient, medic);

	BattleItem *mk = equipMedikit(battle, mod, medic, "STR_RIGHT_HAND");
	int tuSpent = 0;
	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionMedikitHeal(medic, mk, "STR_RIGHT_HAND", 2, 1, &reject, &tuSpent);
	expect(ok, reject.c_str());
	expect(tuSpent == 10, "heal TU from ruleset");
	expect(patient->getFatalWound(1) == 0, "heal removes fatal wound");
	expect(mk->getHealQuantity() == 1, "heal charge consumed");
}

static void testHealUnwoundedPartConsumesCharge()
{
	Mod mod;
	makeHandInventory(mod, "STR_RIGHT_HAND");
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(6, 6, 1);
	battle.initUtilities(&mod);

	BattleUnit *medic = makeMedic(mod, battle, 1, Position(1, 1, 0), 100);
	authorizeCompanionActor(battle, 1);
	BattleUnit *patient = makePatient(
		battle, 2,
		"status: 7\nhealth: 40\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,1,0,0,0,0]\n");
	placeUnconsciousCoTile(patient, medic);
	BattleItem *mk = equipMedikit(battle, mod, medic, "STR_RIGHT_HAND");

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const int woundPart1Before = patient->getFatalWound(1);
	const int tuBefore = medic->getTimeUnits();
	const int chargesBefore = mk->getHealQuantity();
	const bool ok = game.tryExecuteCompanionMedikitHeal(medic, mk, "STR_RIGHT_HAND", 2, 0, &reject);
	expect(ok, "unwounded heal still succeeds");
	expect(patient->getFatalWound(1) == woundPart1Before, "part 0 no-op leaves torso wound");
	expect(medic->getTimeUnits() == tuBefore - 10, "TU spent on no-op heal");
	expect(mk->getHealQuantity() == chargesBefore - 1, "charge consumed on no-op heal");
}

static void testStimulantRevivalSameTile()
{
	Mod mod;
	makeHandInventory(mod, "STR_RIGHT_HAND");
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(6, 6, 1);
	battle.initUtilities(&mod);

	BattleUnit *medic = makeMedic(mod, battle, 1, Position(1, 1, 0), 100);
	authorizeCompanionActor(battle, 1);
	BattleUnit *patient = makePatient(
		battle, 2,
		"status: 7\nhealth: 30\ntu: 0\nenergy: 5\nmorale: 50\nstunlevel: 15\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,0,0,0,0,0]\n");
	placeUnconsciousCoTile(patient, medic);
	BattleItem *mk = equipMedikit(battle, mod, medic, "STR_RIGHT_HAND");

	bool revived = false;
	int tuSpent = 0;
	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionMedikitStimulant(medic, mk, "STR_RIGHT_HAND", 2, &reject, &tuSpent, &revived);
	expect(ok, reject.c_str());
	expect(revived, "stimulant revives unconscious target");
	expect(patient->getTimeUnits() == 0, "revived target TU cleared");
	expect(patient->getStunlevel() < 15, "stun reduced");
	expect(mk->getStimulantQuantity() == 1, "stimulant charge consumed");
}

static void testPainKillerMorale()
{
	Mod mod;
	makeHandInventory(mod, "STR_RIGHT_HAND");
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(6, 6, 1);
	battle.initUtilities(&mod);

	BattleUnit *medic = makeMedic(mod, battle, 1, Position(1, 1, 0), 100);
	authorizeCompanionActor(battle, 1);
	BattleUnit *patient = makePatient(
		battle, 2,
		"status: 7\nhealth: 15\ntu: 0\nenergy: 50\nmorale: 25\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,0,0,0,0,0]\n");
	placeUnconsciousCoTile(patient, medic);
	BattleItem *mk = equipMedikit(battle, mod, medic, "STR_RIGHT_HAND");

	const int moraleBefore = patient->getMorale();
	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionMedikitPainKiller(medic, mk, "STR_RIGHT_HAND", 2, &reject);
	expect(ok, reject.c_str());
	expect(patient->getMorale() >= moraleBefore, "painkiller morale not reduced");
	expect(mk->getPainKillerQuantity() == 1, "pk charge consumed");
}

static void testWrongTargetIdRejected()
{
	Mod mod;
	makeHandInventory(mod, "STR_RIGHT_HAND");
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(6, 6, 1);
	battle.initUtilities(&mod);

	BattleUnit *medic = makeMedic(mod, battle, 1, Position(1, 1, 0), 100);
	authorizeCompanionActor(battle, 1);
	BattleUnit *patient = makePatient(
		battle, 2,
		"status: 7\nhealth: 20\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,1,0,0,0,0]\n");
	placeUnconsciousCoTile(patient, medic);
	BattleItem *mk = equipMedikit(battle, mod, medic, "STR_RIGHT_HAND");

	const int tuBefore = medic->getTimeUnits();
	const int chargesBefore = mk->getHealQuantity();
	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionMedikitHeal(medic, mk, "STR_RIGHT_HAND", 99, 1, &reject);
	expect(!ok, "wrong target id rejected");
	expect(medic->getTimeUnits() == tuBefore, "no TU spent");
	expect(mk->getHealQuantity() == chargesBefore, "no charge spent");
}

static void testWrongHandRejected()
{
	Mod mod;
	makeHandInventory(mod, "STR_RIGHT_HAND");
	makeHandInventory(mod, "STR_LEFT_HAND");
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(6, 6, 1);
	battle.initUtilities(&mod);

	BattleUnit *medic = makeMedic(mod, battle, 1, Position(1, 1, 0), 100);
	authorizeCompanionActor(battle, 1);
	BattleUnit *patient = makePatient(
		battle, 2,
		"status: 7\nhealth: 20\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,1,0,0,0,0]\n");
	placeUnconsciousCoTile(patient, medic);
	BattleItem *mk = equipMedikit(battle, mod, medic, "STR_RIGHT_HAND");

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionMedikitHeal(medic, mk, "STR_LEFT_HAND", 2, 1, &reject);
	expect(!ok, "wrong hand slot rejected");
}

static void testMissingChargesRejected()
{
	Mod mod;
	makeHandInventory(mod, "STR_RIGHT_HAND");
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(6, 6, 1);
	battle.initUtilities(&mod);

	BattleUnit *medic = makeMedic(mod, battle, 1, Position(1, 1, 0), 100);
	authorizeCompanionActor(battle, 1);
	BattleUnit *patient = makePatient(
		battle, 2,
		"status: 7\nhealth: 20\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,1,0,0,0,0]\n");
	placeUnconsciousCoTile(patient, medic);
	BattleItem *mk = equipMedikit(battle, mod, medic, "STR_RIGHT_HAND");
	mk->setHealQuantity(0);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionMedikitHeal(medic, mk, "STR_RIGHT_HAND", 2, 1, &reject);
	expect(!ok, "zero heal charges rejected");
}

static void testInsufficientTuRejected()
{
	Mod mod;
	makeHandInventory(mod, "STR_RIGHT_HAND");
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(6, 6, 1);
	battle.initUtilities(&mod);

	BattleUnit *medic = makeMedic(mod, battle, 1, Position(1, 1, 0), 4);
	authorizeCompanionActor(battle, 1);
	BattleUnit *patient = makePatient(
		battle, 2,
		"status: 7\nhealth: 20\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,1,0,0,0,0]\n");
	placeUnconsciousCoTile(patient, medic);
	BattleItem *mk = equipMedikit(battle, mod, medic, "STR_RIGHT_HAND");

	const int chargesBefore = mk->getHealQuantity();
	const int woundBefore = patient->getFatalWound(1);
	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionMedikitHeal(medic, mk, "STR_RIGHT_HAND", 2, 1, &reject);
	expect(!ok, "insufficient TU rejected");
	expect(mk->getHealQuantity() == chargesBefore, "no charge mutation");
	expect(patient->getFatalWound(1) == woundBefore, "no wound mutation");
}

static void testUnauthorizedHumanActorRejected()
{
	Mod mod;
	makeHandInventory(mod, "STR_RIGHT_HAND");
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(6, 6, 1);
	battle.initUtilities(&mod);

	BattleUnit *medic = makeMedic(mod, battle, 1, Position(1, 1, 0), 100);
	BattleUnit *patient = makePatient(
		battle, 2,
		"status: 7\nhealth: 20\ntu: 0\nenergy: 50\nmorale: 100\nstunlevel: 80\narmor: [0,0,0,0,0]\n"
		"fatalWounds: [0,1,0,0,0,0]\n");
	placeUnconsciousCoTile(patient, medic);
	BattleItem *mk = equipMedikit(battle, mod, medic, "STR_RIGHT_HAND");

	SavedGame campaign;
	battle.setGeoscapeSaveHint(&campaign);
	battle.assignCompanionTacticalSoldierToAi(99);
	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionMedikitHeal(medic, mk, "STR_RIGHT_HAND", 2, 1, &reject);
	expect(!ok, "companion medikit blocked when actor not in tactical AI assignment set");
}

#include "companion_medikit_relay_e2e_harness.cpp"

int main()
{
	testHealWoundedPart();
	testHealUnwoundedPartConsumesCharge();
	testStimulantRevivalSameTile();
	testPainKillerMorale();
	testWrongTargetIdRejected();
	testWrongHandRejected();
	testMissingChargesRejected();
	testInsufficientTuRejected();
	testUnauthorizedHumanActorRejected();

	runMedikitRelayE2eSuite();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test failure(s)." << std::endl;
		return 1;
	}
	std::cout << "All companion medikit engine tests passed." << std::endl;
	return 0;
}