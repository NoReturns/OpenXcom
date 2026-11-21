/*
 * Open X-Com Companion — Mind Probe executor engine tests (M1 MIND-PROBE-18).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/Position.h"
#include "../Engine/CompanionMindProbeMemory.h"
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

static MapData *floorMapData()
{
	static MapDataSet *ds = 0;
	static MapData *floor = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("mind_probe_test_floor");
	}
	if (floor == 0)
	{
		floor = new MapData(ds);
		floor->setObjectType(O_FLOOR);
		floor->setTUCosts(4, 4, 4);
	}
	return floor;
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
	tile->setMapData(floorMapData(), 1, 0, O_FLOOR);
	unit->setPosition(pos);
	battle->setUnitPosition(unit, pos);
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
}

static RuleItem *makeMindProbeRules(Mod &mod)
{
	RuleItem *rules = new RuleItem("STR_MIND_PROBE_TEST");
	YAML::Node m;
	m["battleType"] = static_cast<int>(BT_MINDPROBE);
	m["tuUse"] = 50;
	m["flatRate"] = true;
	m["invWidth"] = 2;
	m["invHeight"] = 2;
	m["weight"] = 5;
	rules->load(m, &mod, 0);
	mod.registerTestItemRule(rules);
	return rules;
}

static BattleUnit *makeActor(Mod &mod, SavedBattleGame &battle, int soldierId, const Position &pos, int tu)
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

static BattleUnit *makeVisibleHostile(SavedBattleGame &battle, int unitId, const Position &pos)
{
	static Unit alienRules("STR_SECTOID");
	static Armor alienArmor("STR_SECTOID_ARMOR");
	StatAdjustment adjustment;
	BattleUnit *hostile = new BattleUnit(&alienRules, FACTION_HOSTILE, unitId, &alienArmor, &adjustment, 0);
	hostile->setVisible(true);
	hostile->getBaseStats()->firing = 60;
	hostile->getBaseStats()->health = 50;
	hostile->damage({ 0, 0, 0 }, 10, DT_NONE, true);
	battle.getUnits()->push_back(hostile);
	wireUnitTile(&battle, hostile, pos);
	return hostile;
}

static BattleItem *equipMindProbe(SavedBattleGame &battle, Mod &mod, BattleUnit *owner, const char *hand)
{
	RuleInventory *slot = mod.getInventory(hand, true);
	BattleItem *probe = new BattleItem(makeMindProbeRules(mod), battle.getCurrentItemId());
	probe->setSlot(slot);
	probe->setSlotX(0);
	probe->setSlotY(0);
	probe->moveToOwner(owner);
	battle.getItems()->push_back(probe);
	return probe;
}

static void authorizeCompanionActor(SavedBattleGame &battle, int soldierId)
{
	SavedGame campaign;
	battle.setGeoscapeSaveHint(&campaign);
	battle.assignCompanionTacticalSoldierToAi(soldierId);
}

struct ProbeScenario
{
	Mod mod;
	SavedBattleGame battle;
	BattlescapeGame *game;
	BattleUnit *actor;
	BattleUnit *hostile;
	BattleItem *probe;

	ProbeScenario()
		: game(0), actor(0), hostile(0), probe(0)
	{
		makeHandInventory(mod, "STR_LEFT_HAND");
		makeHandInventory(mod, "STR_RIGHT_HAND");
		battle.setTestMod(&mod);
		battle.initMap(8, 8, 1);
		battle.initUtilities(&mod);
		battle.getCompanionEventLog().beginSession();
		battle.getCompanionMindProbeMemory().beginSession(battle.getCompanionEventLog().getSessionId());
		game = new BattlescapeGame(&battle, 0);
	}

	~ProbeScenario()
	{
		delete game;
	}
};

static void testSuccessfulProbeRecordsMemoryAndTu()
{
	ProbeScenario s;
	s.actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(2, 1, 0));
	s.probe = equipMindProbe(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	const int tuBefore = s.actor->getTimeUnits();
	std::string reject;
	int tuSpent = 0;
	const bool ok = s.game->tryExecuteCompanionMindProbeUse(
		s.actor, s.probe, "STR_LEFT_HAND", 100042, &reject, &tuSpent);
	expect(ok, reject.c_str());
	expect(tuSpent == 50, "probe TU is 50 from ruleset");
	expect(s.actor->getTimeUnits() == tuBefore - 50, "actor TU reduced once");
	expect(s.battle.getCompanionMindProbeMemory().hasObservationForUnit(100042), "probed unit recorded");
}

static void testInsufficientTuRejected()
{
	ProbeScenario s;
	s.actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 30);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(2, 1, 0));
	s.probe = equipMindProbe(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	const int tuBefore = s.actor->getTimeUnits();
	std::string reject;
	expect(!s.game->tryExecuteCompanionMindProbeUse(s.actor, s.probe, "STR_LEFT_HAND", 100042, &reject), "insufficient TU rejected");
	expect(s.actor->getTimeUnits() == tuBefore, "no TU spent when rejected");
}

static void testWrongHandRejected()
{
	ProbeScenario s;
	s.actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(2, 1, 0));
	s.probe = equipMindProbe(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionMindProbeUse(s.actor, s.probe, "STR_RIGHT_HAND", 100042, &reject), "wrong hand rejected");
}

static void testInvisibleHostileRejected()
{
	ProbeScenario s;
	s.actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(2, 1, 0));
	s.hostile->setVisible(false);
	s.probe = equipMindProbe(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionMindProbeUse(s.actor, s.probe, "STR_LEFT_HAND", 100042, &reject), "invisible hostile rejected");
}

static int exportStatForUnit(const SavedBattleGame &battle, int unitId, const char *statKey)
{
	YAML::Node root;
	SavedGame campaign;
	battle.getCompanionMindProbeMemory().appendMindProbeObservationsV1(
		root, const_cast<SavedBattleGame *>(&battle), &campaign);
	if (!root["mindProbeObservationsV1"])
	{
		return -1;
	}
	for (std::size_t idx = 0; idx < root["mindProbeObservationsV1"]["observations"].size(); ++idx)
	{
		const YAML::Node &obs = root["mindProbeObservationsV1"]["observations"][idx];
		if (obs["unitId"].as<int>() == unitId)
		{
			return obs["stats"][statKey].as<int>();
		}
	}
	return -1;
}

static void testFrozenStatsSurviveDamageAndExport()
{
	ProbeScenario s;
	s.actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(2, 1, 0));
	s.probe = equipMindProbe(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	int tuSpent = 0;
	expect(s.game->tryExecuteCompanionMindProbeUse(s.actor, s.probe, "STR_LEFT_HAND", 100042, &reject, &tuSpent), reject.c_str());
	const int captured = exportStatForUnit(s.battle, 100042, "morale");
	expect(captured == s.hostile->getMorale(), "initial export matches probe-time morale");
	s.hostile->moraleChange(-30);
	expect(s.hostile->getMorale() < captured, "morale mutation changed live unit");
	expect(exportStatForUnit(s.battle, 100042, "morale") == captured, "F6 export keeps frozen morale after mutation");
}

static void testReprobeRefreshesStatsAndChargesTu()
{
	ProbeScenario s;
	s.actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 160);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(2, 1, 0));
	s.probe = equipMindProbe(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	int tuSpent = 0;
	expect(s.game->tryExecuteCompanionMindProbeUse(s.actor, s.probe, "STR_LEFT_HAND", 100042, &reject, &tuSpent), reject.c_str());
	const int first = exportStatForUnit(s.battle, 100042, "morale");
	s.hostile->moraleChange(-20);
	const int tuAfterFirst = s.actor->getTimeUnits();
	expect(s.game->tryExecuteCompanionMindProbeUse(s.actor, s.probe, "STR_LEFT_HAND", 100042, &reject, &tuSpent), reject.c_str());
	expect(tuSpent == 50, "re-probe charges TU");
	expect(s.actor->getTimeUnits() == tuAfterFirst - 50, "re-probe spent TU once");
	const int second = exportStatForUnit(s.battle, 100042, "morale");
	expect(second == s.hostile->getMorale(), "re-probe refreshes stored stats");
	expect(second < first, "re-probe reflects morale change");
}

static void testFailedReprobePreservesFrozenStats()
{
	ProbeScenario s;
	s.actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(2, 1, 0));
	s.probe = equipMindProbe(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	int tuSpent = 0;
	expect(s.game->tryExecuteCompanionMindProbeUse(s.actor, s.probe, "STR_LEFT_HAND", 100042, &reject, &tuSpent), reject.c_str());
	const int frozen = exportStatForUnit(s.battle, 100042, "morale");
	s.hostile->moraleChange(-15);
	s.actor->setTimeUnits(20);
	expect(!s.game->tryExecuteCompanionMindProbeUse(s.actor, s.probe, "STR_LEFT_HAND", 100042, &reject, &tuSpent), "failed re-probe rejected");
	expect(exportStatForUnit(s.battle, 100042, "morale") == frozen, "failed re-probe leaves frozen stats");
}

static void testVisibilityLossDoesNotRefreshExport()
{
	ProbeScenario s;
	s.actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(2, 1, 0));
	s.probe = equipMindProbe(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(s.game->tryExecuteCompanionMindProbeUse(s.actor, s.probe, "STR_LEFT_HAND", 100042, &reject), reject.c_str());
	const int frozen = exportStatForUnit(s.battle, 100042, "morale");
	s.hostile->moraleChange(-10);
	s.hostile->setVisible(false);
	expect(exportStatForUnit(s.battle, 100042, "morale") == frozen, "losing visibility does not refresh stats");
}

static void testNewSessionClearsObservations()
{
	ProbeScenario s;
	s.actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeCompanionActor(s.battle, 1);
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(2, 1, 0));
	s.probe = equipMindProbe(s.battle, s.mod, s.actor, "STR_LEFT_HAND");
	std::string reject;
	expect(s.game->tryExecuteCompanionMindProbeUse(s.actor, s.probe, "STR_LEFT_HAND", 100042, &reject), reject.c_str());
	expect(s.battle.getCompanionMindProbeMemory().hasObservationForUnit(100042), "probed before session change");
	s.battle.getCompanionMindProbeMemory().syncSessionId("fedcba0987654321fedcba0987654321");
	expect(!s.battle.getCompanionMindProbeMemory().hasObservationForUnit(100042), "new session drops prior observations");
	expect(exportStatForUnit(s.battle, 100042, "morale") == -1, "export has no unprobed stats after session change");
}

static void testUnprobedUnitAbsentFromExport()
{
	ProbeScenario s;
	s.hostile = makeVisibleHostile(s.battle, 100042, Position(2, 1, 0));
	expect(exportStatForUnit(s.battle, 100042, "morale") == -1, "unprobed target omitted from mindProbeObservationsV1");
}

int main()
{
	testSuccessfulProbeRecordsMemoryAndTu();
	testInsufficientTuRejected();
	testWrongHandRejected();
	testInvisibleHostileRejected();
	testFrozenStatsSurviveDamageAndExport();
	testReprobeRefreshesStatsAndChargesTu();
	testFailedReprobePreservesFrozenStats();
	testVisibilityLossDoesNotRefreshExport();
	testNewSessionClearsObservations();
	testUnprobedUnitAbsentFromExport();
	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return 1;
	}
	std::cout << "companion_mind_probe_engine_test: OK" << std::endl;
	return 0;
}