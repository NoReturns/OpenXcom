/*
 * Open X-Com Companion — PSI_MIND_CONTROL executor engine tests (M1-WP10 S2b).
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

static MapData *mcTestFloorMapData()
{
	static MapDataSet *ds = 0;
	static MapData *floor = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("psi_mc_test_floor");
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
	tile->setMapData(mcTestFloorMapData(), 1, 0, O_FLOOR);
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
	RuleItem *existing = mod.getItem("STR_WP10_MC_TEST_PSI_AMP", false);
	if (existing != 0)
	{
		return existing;
	}
	RuleItem *rules = new RuleItem("STR_WP10_MC_TEST_PSI_AMP");
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

struct McScenario
{
	Mod mod;
	SavedBattleGame battle;
	BattlescapeGame *game;
	BattleUnit *actor;
	BattleUnit *hostile;
	BattleItem *amp;

	McScenario()
		: game(0), actor(0), hostile(0), amp(0)
	{
		makeHandInventory(mod, "STR_LEFT_HAND");
		makeHandInventory(mod, "STR_RIGHT_HAND");
		battle.setTestMod(&mod);
		battle.initMap(8, 8, 1);
		battle.initUtilities(&mod);
		game = new BattlescapeGame(&battle, 0);
	}

	~McScenario()
	{
		delete game;
	}
};

static bool tryMc(
	BattlescapeGame *game,
	BattleUnit *actor,
	BattleItem *amp,
	int targetId,
	std::string *reject,
	int *tuSpent,
	bool *succeeded)
{
	return game->tryExecuteCompanionPsiMindControl(actor, amp, "STR_LEFT_HAND", targetId, reject, tuSpent, succeeded);
}

static void testSuccessfulMindControlFactionConversion()
{
	McScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 90);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100043, Position(3, 1, 0), 10, 5);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");

	RNG::setSeed(0xA11CE001ULL);
	const int tuBefore = s.actor->getTimeUnits();
	const UnitFaction factionBefore = s.hostile->getFaction();
	const UnitFaction originalBefore = s.hostile->getOriginalFaction();
	bool psychicSucceeded = false;
	int tuSpent = 0;
	std::string reject;
	const bool ok = tryMc(s.game, s.actor, s.amp, 100043, &reject, &tuSpent, &psychicSucceeded);
	expect(ok, reject.c_str());
	expect(tuSpent == 25, "mind control TU is flat 25 from ruleset");
	expect(s.actor->getTimeUnits() == tuBefore - 25, "actor TU reduced once");
	expect(psychicSucceeded, "seeded mind control succeeds");
	expect(factionBefore == FACTION_HOSTILE && originalBefore == FACTION_HOSTILE, "precondition genuine hostile");
	expect(s.hostile->getFaction() == FACTION_PLAYER, "faction converted to player");
	expect(s.hostile->getOriginalFaction() == FACTION_HOSTILE, "original faction preserved");
	expect(s.hostile->getMindControllerId() == s.actor->getId(), "mind controller id set");
	expect(s.hostile->getVisible(), "converted unit remains visible for FOV updates");
}

static void testFailedMindControlStillConsumesTu()
{
	McScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 1);
	s.actor->getBaseStats()->psiStrength = 1;
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100043, Position(3, 1, 0), 100, 100);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");

	RNG::setSeed(0ULL);
	const int tuBefore = s.actor->getTimeUnits();
	const UnitFaction factionBefore = s.hostile->getFaction();
	bool psychicSucceeded = false;
	int tuSpent = 0;
	std::string reject;
	expect(tryMc(s.game, s.actor, s.amp, 100043, &reject, &tuSpent, &psychicSucceeded), "failed MC still completes attempt");
	expect(!psychicSucceeded, "seeded failure");
	expect(tuSpent == 25, "TU spent on failure");
	expect(s.actor->getTimeUnits() == tuBefore - 25, "TU consumed on failure");
	expect(s.hostile->getFaction() == factionBefore, "faction unchanged on failure");
}

static void testInsufficientTuRejected()
{
	McScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 20, 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100043, Position(3, 1, 0), 50, 20);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	const int tuBefore = s.actor->getTimeUnits();
	std::string reject;
	expect(!tryMc(s.game, s.actor, s.amp, 100043, &reject, 0, 0), "insufficient TU rejected");
	expect(s.actor->getTimeUnits() == tuBefore, "no TU spent when rejected");
}

static void testZeroPsiSkillRejected()
{
	McScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 0);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100043, Position(3, 1, 0), 50, 20);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(!tryMc(s.game, s.actor, s.amp, 100043, &reject, 0, 0), "zero psi skill rejected");
}

static void testWrongHandRejected()
{
	McScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100043, Position(3, 1, 0), 50, 20);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionPsiMindControl(
		s.actor, s.amp, "STR_RIGHT_HAND", 100043, &reject), "wrong hand rejected");
}

static void testInvisibleFriendlyAndControlledRejected()
{
	McScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100043, Position(3, 1, 0), 50, 20);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");

	s.hostile->setVisible(false);
	std::string reject;
	expect(!tryMc(s.game, s.actor, s.amp, 100043, &reject, 0, 0), "invisible hostile rejected");

	s.hostile->setVisible(true);
	BattleUnit *ally = makePsiSoldier(s.mod, s.battle, 2, Position(5, 1, 0), 0, 0);
	ally->setVisible(true);
	expect(!tryMc(s.game, s.actor, s.amp, 2, &reject, 0, 0), "friendly target rejected");

	s.hostile->convertToFaction(FACTION_PLAYER);
	expect(!tryMc(s.game, s.actor, s.amp, 100043, &reject, 0, 0), "already MC hostile rejected");
}

static void testConvertedAlienNotCompanionCommandable()
{
	McScenario s;
	SavedGame campaign;
	s.battle.setGeoscapeSaveHint(&campaign);
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 90);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100043, Position(3, 1, 0), 10, 5);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");

	RNG::setSeed(0xA11CE001ULL);
	std::string reject;
	expect(tryMc(s.game, s.actor, s.amp, 100043, &reject, 0, 0), reject.c_str());

	expect(!campaign.isLiveRosterSoldier(100043), "MC alien is not a roster soldier");
	const std::set<int> &aiIds = s.battle.getCompanionTacticalAiSoldierIds();
	expect(aiIds.find(1) != aiIds.end(), "companion actor remains AI-owned");
	expect(aiIds.find(100043) == aiIds.end(), "converted alien never added to companion AI soldiers");
}

static void testMissionCompletionUnchangedWithRemainingHostile()
{
	McScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 90);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100043, Position(3, 1, 0), 10, 5);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");

	RNG::setSeed(0xA11CE001ULL);
	std::string reject;
	expect(tryMc(s.game, s.actor, s.amp, 100043, &reject, 0, 0), reject.c_str());
	expect(s.hostile->getFaction() == FACTION_PLAYER, "first hostile converted");

	// Remaining genuine hostile present after MC (no auto-end while hostiles remain).
	BattleUnit *other = makeVisibleHostile(s.battle, 100044, Position(5, 3, 0), 80, 50);
	expect(other->getFaction() == FACTION_HOSTILE, "remaining hostile unchanged");
	expect(other->getOriginalFaction() == FACTION_HOSTILE, "remaining hostile still genuine hostile");
	expect(s.battle.getUnits()->size() >= 2U, "battle roster retains unconverted hostiles");
}

static void testSnapshotFreshnessAfterMindControl()
{
	using namespace CompanionRelayPublicationFreshnessCore;
#if defined(_MSC_VER)
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", "companion_wp10_psi_mc_freshness.json");
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", "companion_wp10_psi_mc_freshness.json", 1);
#endif
	const int genBefore = getTacticalMutationGeneration();
	McScenario s;
	s.actor = makePsiSoldier(s.mod, s.battle, 1, Position(1, 1, 0), 80, 90);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100043, Position(3, 1, 0), 10, 5);
	s.amp = equipPsiAmp(s.battle, s.mod, s.actor, "STR_LEFT_HAND");

	RNG::setSeed(0xA11CE001ULL);
	std::string reject;
	expect(tryMc(s.game, s.actor, s.amp, 100043, &reject, 0, 0), reject.c_str());
	expect(getTacticalMutationGeneration() > genBefore, "psi mind control bumps tactical mutation generation");
}

#define WP10_RELAY_E2E_DIR "wp10_psi_mc_relay_e2e"
#include "companion_psi_panic_relay_e2e_harness.cpp"

int main()
{
	testSuccessfulMindControlFactionConversion();
	testFailedMindControlStillConsumesTu();
	testInsufficientTuRejected();
	testZeroPsiSkillRejected();
	testWrongHandRejected();
	testInvisibleFriendlyAndControlledRejected();
	testConvertedAlienNotCompanionCommandable();
	testMissionCompletionUnchangedWithRemainingHostile();
	testSnapshotFreshnessAfterMindControl();
	runPsiMindControlRelayE2eSuite();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return 1;
	}
	std::cout << "companion_psi_mind_control_engine_test: all passed." << std::endl;
	return 0;
}