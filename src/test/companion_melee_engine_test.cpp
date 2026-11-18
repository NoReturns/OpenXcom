/*
 * Open X-Com Companion — MELEE_ATTACK executor engine tests (M1 melee S2a).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/Position.h"
#include "../Engine/CompanionMeleeExecutionCore.h"
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

static MapData *meleeTestFloorMapData()
{
	static MapDataSet *ds = 0;
	static MapData *floor = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("melee_test_floor");
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
	tile->setMapData(meleeTestFloorMapData(), 1, 0, O_FLOOR);
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

static RuleItem *makeStunRodRules(Mod &mod)
{
	RuleItem *existing = mod.getItem("STR_WP_MELEE_TEST_STUN_ROD", false);
	if (existing != 0)
	{
		return existing;
	}
	RuleItem *rules = new RuleItem("STR_WP_MELEE_TEST_STUN_ROD");
	YAML::Node m;
	m["battleType"] = static_cast<int>(BT_MELEE);
	m["tuMelee"] = 30;
	m["accuracyMelee"] = 100;
	m["power"] = 65;
	m["damageType"] = static_cast<int>(DT_STUN);
	m["invWidth"] = 1;
	m["invHeight"] = 3;
	m["weight"] = 6;
	rules->load(m, &mod, 0);
	mod.registerTestItemRule(rules);
	return rules;
}

static RuleItem *makeMeleeKnifeRules(Mod &mod)
{
	RuleItem *existing = mod.getItem("STR_WP_MELEE_TEST_KNIFE", false);
	if (existing != 0)
	{
		return existing;
	}
	RuleItem *rules = new RuleItem("STR_WP_MELEE_TEST_KNIFE");
	YAML::Node m;
	m["battleType"] = static_cast<int>(BT_MELEE);
	m["tuMelee"] = 20;
	m["accuracyMelee"] = 100;
	m["power"] = 40;
	m["damageType"] = static_cast<int>(DT_MELEE);
	m["invWidth"] = 1;
	m["invHeight"] = 2;
	m["weight"] = 4;
	rules->load(m, &mod, 0);
	mod.registerTestItemRule(rules);
	return rules;
}

static BattleUnit *makeActor(Mod &mod, SavedBattleGame &battle, int id, const Position &pos, int tu)
{
	static RuleSoldier rs("STR_SOLDIER");
	static Armor ar("STR_JUMPSUIT");
	Soldier *soldier = new Soldier(&rs, &ar, id);
	BattleUnit *unit = new BattleUnit(soldier, 0);
	battle.getUnits()->push_back(unit);
	unit->setTimeUnits(tu);
	wireUnitTile(&battle, unit, pos);
	return unit;
}

static BattleUnit *makeHostile(SavedBattleGame &battle, int unitId, const Position &pos)
{
	static Unit alienRules("STR_SECTOID");
	static Armor alienArmor("STR_SECTOID_ARMOR");
	StatAdjustment adjustment;
	BattleUnit *hostile = new BattleUnit(&alienRules, FACTION_HOSTILE, unitId, &alienArmor, &adjustment, 0);
	hostile->setVisible(true);
	hostile->getBaseStats()->health = 30;
	hostile->getBaseStats()->melee = 0;
	battle.getUnits()->push_back(hostile);
	wireUnitTile(&battle, hostile, pos);
	return hostile;
}

static BattleItem *equipMelee(Mod &mod, SavedBattleGame &battle, BattleUnit *owner, RuleItem *rules, const char *hand)
{
	RuleInventory *slot = mod.getInventory(hand, true);
	BattleItem *item = new BattleItem(rules, battle.getCurrentItemId());
	item->setSlot(slot);
	item->setSlotX(0);
	item->setSlotY(0);
	item->moveToOwner(owner);
	battle.getItems()->push_back(item);
	return item;
}

static void authorizeActor(SavedBattleGame &battle, int soldierId)
{
	SavedGame campaign;
	battle.setGeoscapeSaveHint(&campaign);
	battle.assignCompanionTacticalSoldierToAi(soldierId);
}

static void linkVisibility(BattleUnit *actor, BattleUnit *hostile)
{
	std::vector<BattleUnit *> *visible = actor->getVisibleUnits();
	if (visible != 0)
	{
		visible->push_back(hostile);
	}
}

struct MeleeScenario
{
	Mod mod;
	SavedBattleGame battle;
	BattlescapeGame *game;

	MeleeScenario() : game(0)
	{
		makeHandInventory(mod, "STR_LEFT_HAND");
		makeHandInventory(mod, "STR_RIGHT_HAND");
		battle.setTestMod(&mod);
		battle.initMap(8, 8, 1);
		battle.initUtilities(&mod);
		game = new BattlescapeGame(&battle, 0);
	}

	~MeleeScenario()
	{
		delete game;
	}
};

static void testStunRodAdjacentSuccess()
{
	RNG::setSeed(7);
	MeleeScenario s;
	BattleUnit *actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeActor(s.battle, 1);
	BattleUnit *hostile = makeHostile(s.battle, 100042, Position(2, 1, 0));
	linkVisibility(actor, hostile);
	BattleItem *rod = equipMelee(s.mod, s.battle, actor, makeStunRodRules(s.mod), "STR_RIGHT_HAND");

	std::string reject;
	int tuSpent = 0;
	bool hit = false;
	bool unconscious = false;
	const int stunBefore = hostile->getStunlevel();
	const bool ok = s.game->tryExecuteCompanionMeleeAttack(
		actor, rod, "STR_RIGHT_HAND", hostile->getId(), &reject, &tuSpent, &hit, &unconscious, 0);
	expect(ok, "stun rod melee executes");
	expect(tuSpent > 0, "TU spent once (turn+melee)");
	expect(hit, "melee hit under fixed seed");
	expect(hostile->getStunlevel() > stunBefore, "stun increased on hit");
	expect(unconscious || hostile->getStunlevel() >= hostile->getHealth(), "incapacitation path");
}

static void testLeftHandKnife()
{
	RNG::setSeed(9);
	MeleeScenario s;
	BattleUnit *actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeActor(s.battle, 1);
	BattleUnit *hostile = makeHostile(s.battle, 100043, Position(1, 2, 0));
	linkVisibility(actor, hostile);
	BattleItem *knife = equipMelee(s.mod, s.battle, actor, makeMeleeKnifeRules(s.mod), "STR_LEFT_HAND");

	std::string reject;
	bool ok = s.game->tryExecuteCompanionMeleeAttack(
		actor, knife, "STR_LEFT_HAND", hostile->getId(), &reject, 0, 0, 0, 0);
	expect(ok, "left hand melee executes");
}

static void testWrongHandRejects()
{
	MeleeScenario s;
	BattleUnit *actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeActor(s.battle, 1);
	BattleUnit *hostile = makeHostile(s.battle, 100044, Position(2, 1, 0));
	linkVisibility(actor, hostile);
	BattleItem *rod = equipMelee(s.mod, s.battle, actor, makeStunRodRules(s.mod), "STR_RIGHT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionMeleeAttack(
		actor, rod, "STR_LEFT_HAND", hostile->getId(), &reject), "wrong hand rejects");
}

static void testOutOfRangeRejects()
{
	MeleeScenario s;
	BattleUnit *actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeActor(s.battle, 1);
	BattleUnit *hostile = makeHostile(s.battle, 100045, Position(4, 1, 0));
	linkVisibility(actor, hostile);
	BattleItem *rod = equipMelee(s.mod, s.battle, actor, makeStunRodRules(s.mod), "STR_RIGHT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionMeleeAttack(
		actor, rod, "STR_RIGHT_HAND", hostile->getId(), &reject), "out of range rejects");
}

static void testInsufficientTuRejects()
{
	MeleeScenario s;
	BattleUnit *actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 1);
	authorizeActor(s.battle, 1);
	BattleUnit *hostile = makeHostile(s.battle, 100046, Position(2, 1, 0));
	linkVisibility(actor, hostile);
	BattleItem *rod = equipMelee(s.mod, s.battle, actor, makeStunRodRules(s.mod), "STR_RIGHT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionMeleeAttack(
		actor, rod, "STR_RIGHT_HAND", hostile->getId(), &reject), "low TU rejects");
}

static void testUnauthorizedActorRejects()
{
	MeleeScenario s;
	BattleUnit *actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	BattleUnit *hostile = makeHostile(s.battle, 100047, Position(2, 1, 0));
	linkVisibility(actor, hostile);
	BattleItem *rod = equipMelee(s.mod, s.battle, actor, makeStunRodRules(s.mod), "STR_RIGHT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionMeleeAttack(
		actor, rod, "STR_RIGHT_HAND", hostile->getId(), &reject), "unauthorized actor rejects");
}

static void testHiddenHostileRejects()
{
	MeleeScenario s;
	BattleUnit *actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeActor(s.battle, 1);
	BattleUnit *hostile = makeHostile(s.battle, 100048, Position(2, 1, 0));
	hostile->setVisible(false);
	BattleItem *rod = equipMelee(s.mod, s.battle, actor, makeStunRodRules(s.mod), "STR_RIGHT_HAND");
	std::string reject;
	expect(!s.game->tryExecuteCompanionMeleeAttack(
		actor, rod, "STR_RIGHT_HAND", hostile->getId(), &reject), "hidden target rejects");
}

static void testMissOutcome()
{
	RNG::setSeed(1);
	MeleeScenario s;
	BattleUnit *actor = makeActor(s.mod, s.battle, 1, Position(1, 1, 0), 80);
	authorizeActor(s.battle, 1);
	BattleUnit *hostile = makeHostile(s.battle, 100049, Position(2, 1, 0));
	linkVisibility(actor, hostile);
	BattleItem *rod = equipMelee(s.mod, s.battle, actor, makeStunRodRules(s.mod), "STR_RIGHT_HAND");
	const int stunBefore = hostile->getStunlevel();
	std::string reject;
	bool hit = true;
	const bool ok = s.game->tryExecuteCompanionMeleeAttack(
		actor, rod, "STR_RIGHT_HAND", hostile->getId(), &reject, 0, &hit, 0, 0);
	expect(ok, "miss still completes attack attempt");
	if (ok && !hit)
	{
		expect(hostile->getStunlevel() == stunBefore, "miss leaves stun unchanged");
	}
}

int main()
{
	testStunRodAdjacentSuccess();
	testLeftHandKnife();
	testWrongHandRejects();
	testOutOfRangeRejects();
	testInsufficientTuRejects();
	testUnauthorizedActorRejects();
	testHiddenHostileRejects();
	testMissOutcome();
	if (g_failures == 0)
	{
		std::cout << "All companion melee engine tests passed." << std::endl;
		return EXIT_SUCCESS;
	}
	std::cerr << g_failures << " failure(s)." << std::endl;
	return EXIT_FAILURE;
}