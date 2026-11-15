/*
 * Open X-Com Companion — PSI_PANIC executor engine tests (M1-WP10 S2a).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/Position.h"
#include "../Engine/CompanionRelayPublicationFreshnessCore.h"
#include "../Engine/RNG.h"
#include "../Mod/Armor.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Unit.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include <yaml-cpp/yaml.h>
#include <cstdlib>
#include <iostream>
#include <string>

#if defined(_MSC_VER)
#include <stdlib.h>
#endif

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

static MapData *psiTestFloorMapData()
{
	static MapDataSet *ds = 0;
	static MapData *floor = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("psi_test_floor");
	}
	if (floor == 0)
	{
		floor = new MapData(ds);
		floor->setObjectType(O_FLOOR);
		floor->setTUCosts(4, 4, 4);
	}
	return floor;
}

static void wireFloorTile(SavedBattleGame *battle, const Position &pos)
{
	Tile *tile = battle->getTile(pos);
	expect(tile != 0, "tile exists");
	if (tile == 0)
	{
		return;
	}
	tile->setDiscovered(true, 2);
	tile->setMapData(psiTestFloorMapData(), 1, 0, O_FLOOR);
}

static void wireUnitTile(SavedBattleGame *battle, BattleUnit *unit, const Position &pos)
{
	wireFloorTile(battle, pos);
	unit->setPosition(pos);
	battle->setUnitPosition(unit, pos);
	Tile *tile = battle->getTile(pos);
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
}

static RuleItem *makePsiAmpRules(Mod &mod)
{
	RuleItem *existing = mod.getItem("STR_WP10_TEST_PSI_AMP", false);
	if (existing != 0)
	{
		return existing;
	}
	RuleItem *rules = new RuleItem("STR_WP10_TEST_PSI_AMP");
	YAML::Node m;
	m["battleType"] = static_cast<int>(BT_PSIAMP);
	m["tuUse"] = 25;
	m["flatRate"] = true;
	m["invWidth"] = 1;
	m["invHeight"] = 3;
	m["weight"] = 10;
	rules->load(m, &mod, 0);
	mod.registerTestItemRule(rules);
	return rules;
}

static BattleUnit *makePsiSoldier(Mod &mod, SavedBattleGame &battle, int soldierId, const Position &pos, int tu, int psiSkill)
{
	static RuleSoldier rs("STR_SOLDIER");
	static Armor ar("STR_JUMPSUIT");
	Soldier *soldier = new Soldier(&rs, &ar, soldierId);
	BattleUnit *unit = new BattleUnit(soldier, 0);
	battle.getUnits()->push_back(unit);
	unit->setTimeUnits(tu);
	unit->getBaseStats()->psiSkill = psiSkill;
	unit->getBaseStats()->psiStrength = 90;
	wireUnitTile(&battle, unit, pos);
	return unit;
}

static BattleUnit *makeVisibleHostile(SavedBattleGame &battle, int unitId, const Position &pos, int bravery, int psiStrength)
{
	static Unit alienRules("STR_SECTOID");
	static Armor alienArmor("STR_SECTOID_ARMOR");
	StatAdjustment adjustment;
	BattleUnit *hostile = new BattleUnit(&alienRules, FACTION_HOSTILE, unitId, &alienArmor, &adjustment, 0);
	hostile->setVisible(true);
	hostile->getBaseStats()->bravery = bravery;
	hostile->getBaseStats()->psiStrength = psiStrength;
	hostile->getBaseStats()->psiSkill = 0;
	battle.getUnits()->push_back(hostile);
	wireUnitTile(&battle, hostile, pos);
	return hostile;
}

static BattleItem *equipPsiAmp(SavedBattleGame &battle, Mod &mod, BattleUnit *owner, const char *hand)
{
	RuleInventory *slot = mod.getInventory(hand, true);
	BattleItem *amp = new BattleItem(makePsiAmpRules(mod), battle.getCurrentItemId());
	amp->setSlot(slot);
	amp->setSlotX(0);
	amp->setSlotY(0);
	amp->moveToOwner(owner);
	battle.getItems()->push_back(amp);
	return amp;
}

static void authorizeCompanionActor(SavedBattleGame &battle, int soldierId)
{
	SavedGame campaign;
	battle.setGeoscapeSaveHint(&campaign);
	battle.assignCompanionTacticalSoldierToAi(soldierId);
}

struct PsiScenario
{
	Mod mod;
	SavedBattleGame battle;
	BattlescapeGame *game;
	BattleUnit *actor;
	BattleUnit *hostile;
	BattleItem *amp;

	PsiScenario()
		: game(0), actor(0), hostile(0), amp(0)
	{
		makeHandInventory(mod, "STR_LEFT_HAND");
		makeHandInventory(mod, "STR_RIGHT_HAND");
		battle.setTestMod(&mod);
		battle.initMap(8, 8, 1);
		battle.initUtilities(&mod);
		game = new BattlescapeGame(&battle, 0);
	}

	~PsiScenario()
	{
		delete game;
	}
};

static void testSuccessfulPanicConsumesTu()
{
	PsiScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 90);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(3, 1, 0), 10, 5);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");

	RNG::setSeed(0xA11CE001ULL);
	const int tuBefore = s.actor->getTimeUnits();
	const int moraleBefore = s.hostile->getMorale();
	bool psychicSucceeded = false;
	int tuSpent = 0;
	std::string reject;
	const bool ok = s.game->tryExecuteCompanionPsiPanic(
		s.actor, s.amp, "STR_LEFT_HAND", 100042, &reject, &tuSpent, &psychicSucceeded);
	expect(ok, reject.c_str());
	expect(tuSpent == 25, "panic TU is flat 25 from ruleset");
	expect(s.actor->getTimeUnits() == tuBefore - 25, "actor TU reduced once");
	expect(psychicSucceeded, "seeded panic succeeds");
	expect(s.hostile->getMorale() < moraleBefore, "hostile morale reduced on success");
}

static void testFailedPanicStillConsumesTu()
{
	PsiScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 50);
	s.actor->getBaseStats()->psiStrength = 1;
	s.actor->getBaseStats()->psiSkill = 1;
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(3, 1, 0), 100, 100);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");

	RNG::setSeed(0ULL);
	const int tuBefore = s.actor->getTimeUnits();
	const int moraleBefore = s.hostile->getMorale();
	bool psychicSucceeded = false;
	int tuSpent = 0;
	std::string reject;
	const bool ok = s.game->tryExecuteCompanionPsiPanic(
		s.actor, s.amp, "STR_LEFT_HAND", 100042, &reject, &tuSpent, &psychicSucceeded);
	expect(ok, "failed psychic attack still completes command");
	expect(!psychicSucceeded, "seeded failure");
	expect(tuSpent == 25, "TU spent on failure");
	expect(s.actor->getTimeUnits() == tuBefore - 25, "TU consumed on failure");
	expect(s.hostile->getMorale() == moraleBefore, "morale unchanged on failure");
}

static void testInsufficientTuRejected()
{
	PsiScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 20, 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(3, 1, 0), 50, 20);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	const int tuBefore = s.actor->getTimeUnits();
	std::string reject;
	const bool ok = s.game->tryExecuteCompanionPsiPanic(s.actor, s.amp, "STR_LEFT_HAND", 100042, &reject);
	expect(!ok, "insufficient TU rejected");
	expect(s.actor->getTimeUnits() == tuBefore, "no TU spent when rejected");
}

static void testZeroPsiSkillRejected()
{
	PsiScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 0);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(3, 1, 0), 50, 20);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionPsiPanic(s.actor, s.amp, "STR_LEFT_HAND", 100042, &reject), "zero psi skill rejected");
}

static void testWrongHandRejected()
{
	PsiScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(3, 1, 0), 50, 20);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionPsiPanic(s.actor, s.amp, "STR_RIGHT_HAND", 100042, &reject), "wrong hand rejected");
}

static void testWrongTargetIdRejected()
{
	PsiScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(3, 1, 0), 50, 20);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	const int tuBefore = s.actor->getTimeUnits();
	std::string reject;
	expect(!s.game->tryExecuteCompanionPsiPanic(s.actor, s.amp, "STR_LEFT_HAND", 999, &reject), "missing target rejected");
	expect(s.actor->getTimeUnits() == tuBefore, "no TU on missing target");
}

static void testInvisibleHostileRejected()
{
	PsiScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(3, 1, 0), 50, 20);
	s.hostile->setVisible(false);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionPsiPanic(s.actor, s.amp, "STR_LEFT_HAND", 100042, &reject), "invisible hostile rejected");
}

static void testFriendlyTargetRejected()
{
	PsiScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 80);
	authorizeCompanionActor(s.battle, 1);
	BattleUnit *ally = makePsiSoldier(s.mod, s.battle, 2, Position(3, 1, 0), 0, 0);
	ally->setVisible(true);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionPsiPanic(s.actor, s.amp, "STR_LEFT_HAND", 2, &reject), "friendly target rejected");
}

static void testMindControlledHostileRejected()
{
	PsiScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100001, Position(3, 1, 0), 50, 20);
	s.hostile->convertToFaction(FACTION_PLAYER);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionPsiPanic(s.actor, s.amp, "STR_LEFT_HAND", 100001, &reject), "MC hostile not genuine hostile");
}

static void testSnapshotFreshnessAfterExecution()
{
	using namespace CompanionRelayPublicationFreshnessCore;
#if defined(_MSC_VER)
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", "companion_wp10_psi_panic_freshness.json");
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", "companion_wp10_psi_panic_freshness.json", 1);
#endif
	const int genBefore = getTacticalMutationGeneration();
	PsiScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 90);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(3, 1, 0), 10, 5);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");

	RNG::setSeed(0xA11CE001ULL);
	std::string reject;
	expect(s.game->tryExecuteCompanionPsiPanic(s.actor, s.amp, "STR_LEFT_HAND", 100042, &reject), reject.c_str());
	expect(getTacticalMutationGeneration() > genBefore, "psi panic bumps tactical mutation generation");
}

#include "companion_psi_panic_relay_e2e_harness.cpp"

int main()
{
	testSuccessfulPanicConsumesTu();
	testFailedPanicStillConsumesTu();
	testInsufficientTuRejected();
	testZeroPsiSkillRejected();
	testWrongHandRejected();
	testWrongTargetIdRejected();
	testInvisibleHostileRejected();
	testFriendlyTargetRejected();
	testMindControlledHostileRejected();
	testSnapshotFreshnessAfterExecution();
	runPsiPanicRelayE2eSuite();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return 1;
	}
	std::cout << "companion_psi_panic_engine_test: all passed." << std::endl;
	return 0;
}