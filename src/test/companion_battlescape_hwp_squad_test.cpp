/*
 * Open X-Com Companion — battlescape squad UI HWP ownership tests (M1 HWP-S2B).
 */
#define SDL_MAIN_HANDLED
#include "Engine/CompanionBattlescapeOwnershipCore.h"
#include "Engine/CompanionSoldierOwnershipCore.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Base.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Armor.h"
#include "../Mod/Unit.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <string>
#include <vector>

using namespace OpenXcom;
using namespace OpenXcom::CompanionBattlescapeOwnershipCore;
using CompanionSoldierOwnershipCore::CONTROL_AI;
using CompanionSoldierOwnershipCore::CONTROL_HUMAN;

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

struct SoldierFixture
{
	RuleSoldier rules;
	Armor armor;
	Soldier soldier;
	SoldierFixture(int id) : rules("STR_SOLDIER"), armor("STR_JUMPSUIT"), soldier(&rules, &armor, id) {}
};

struct FriendlyHwpFixture
{
	Unit rules;
	Armor armor;
	BattleUnit *unit;
	FriendlyHwpFixture(const char *type, int id, const Position &pos)
		: rules(type), armor("STR_SECTOID_ARMOR"), unit(0)
	{
		StatAdjustment adjustment;
		unit = new BattleUnit(&rules, FACTION_PLAYER, id, &armor, &adjustment, 0);
		unit->setPosition(pos);
		unit->setDirection(4);
		unit->load(YAML::Load(
			"status: 0\nstunlevel: 0\nhealth: 120\ntu: 75\nenergy: 60\nmorale: 100\narmor: [50,50,50,50,50]\nfatalWounds: [0,0,0,0,0,0]\n"));
	}
};

struct TacticalFixture
{
	SavedGame game;
	Base *base;
	SavedBattleGame *battle;
	std::vector<SoldierFixture *> soldierBundles;
	std::vector<FriendlyHwpFixture *> hwpBundles;

	TacticalFixture()
		: base(new Base(&fixtureMod())),
		  battle(new SavedBattleGame())
	{
		game.getBases()->push_back(base);
		battle->initMap(4, 4, 1);
		battle->setTestMod(&fixtureMod());
	}

	~TacticalFixture()
	{
		delete battle;
		for (std::vector<FriendlyHwpFixture *>::iterator i = hwpBundles.begin(); i != hwpBundles.end(); ++i)
		{
			delete *i;
		}
		for (std::vector<SoldierFixture *>::iterator i = soldierBundles.begin(); i != soldierBundles.end(); ++i)
		{
			delete *i;
		}
		if (base != 0)
		{
			base->getSoldiers()->clear();
		}
		game.getBases()->clear();
		delete base;
		base = 0;
	}

	Soldier *addRosterSoldier(int id)
	{
		SoldierFixture *bundle = new SoldierFixture(id);
		soldierBundles.push_back(bundle);
		base->getSoldiers()->push_back(&bundle->soldier);
		return &bundle->soldier;
	}

	BattleUnit *addSoldierUnit(Soldier *soldier, const Position &pos)
	{
		BattleUnit *unit = new BattleUnit(soldier, 0);
		unit->setPosition(pos);
		unit->setDirection(4);
		battle->getUnits()->push_back(unit);
		return unit;
	}

	BattleUnit *addFriendlyHwp(const char *type, int id, const Position &pos)
	{
		FriendlyHwpFixture *bundle = new FriendlyHwpFixture(type, id, pos);
		hwpBundles.push_back(bundle);
		battle->getUnits()->push_back(bundle->unit);
		return bundle->unit;
	}
};

static const SquadOwnershipRow *findRow(const std::vector<SquadOwnershipRow> &rows, int unitId)
{
	for (size_t i = 0; i < rows.size(); ++i)
	{
		if (rows[i].soldierId == unitId)
		{
			return &rows[i];
		}
	}
	return 0;
}

static size_t countHwpRows(const std::vector<SquadOwnershipRow> &rows)
{
	size_t n = 0;
	for (size_t i = 0; i < rows.size(); ++i)
	{
		if (rows[i].isFriendlyHwp)
		{
			++n;
		}
	}
	return n;
}

static void testSingleFriendlyHwpListed()
{
	TacticalFixture fx;
	fx.addFriendlyHwp("STR_HWP_OBS_TEST_TANK", 1000000, Position(5, 6, 0));
	const std::vector<SquadOwnershipRow> rows = buildSquadOwnershipRows(&fx.game, fx.battle, 0, 0, -1);
	expect(countHwpRows(rows) == 1, "one HWP must appear in squad rows");
	const SquadOwnershipRow *row = findRow(rows, 1000000);
	expect(row != 0 && row->isFriendlyHwp, "row flagged as HWP");
	expect(row != 0 && row->effectiveOwner == CONTROL_HUMAN, "default human owner");
	expect(row != 0 && row->positionLabel == "5,6,0", "position column");
	expect(row != 0 && row->displayName.find("#1") != std::string::npos, "first tank ordinal #1");
	expect(row != 0 && row->canAssignToAster, "assign to Aster allowed");
}

static void testTwoIdenticalHwpsOrdinalById()
{
	TacticalFixture fx;
	fx.addFriendlyHwp("STR_HWP_OBS_TEST_TANK", 1000002, Position(1, 1, 0));
	fx.addFriendlyHwp("STR_HWP_OBS_TEST_TANK", 1000001, Position(2, 2, 0));
	const std::vector<SquadOwnershipRow> rows = buildSquadOwnershipRows(&fx.game, fx.battle, 0, 0, -1);
	expect(countHwpRows(rows) == 2, "two HWPs listed");
	const SquadOwnershipRow *a = findRow(rows, 1000001);
	const SquadOwnershipRow *b = findRow(rows, 1000002);
	expect(a != 0 && a->displayName.find("#1") != std::string::npos, "lower id is #1");
	expect(b != 0 && b->displayName.find("#2") != std::string::npos, "higher id is #2");
}

static void testTwoDifferentHwpTypes()
{
	TacticalFixture fx;
	fx.addFriendlyHwp("STR_HWP_OBS_TEST_TANK", 1000000, Position(1, 1, 0));
	fx.addFriendlyHwp("STR_TANK_CANNON", 1000001, Position(2, 2, 0));
	const std::vector<SquadOwnershipRow> rows = buildSquadOwnershipRows(&fx.game, fx.battle, 0, 0, -1);
	const SquadOwnershipRow *tank = findRow(rows, 1000000);
	const SquadOwnershipRow *cannon = findRow(rows, 1000001);
	expect(tank != 0 && tank->displayName.find("STR_HWP_OBS_TEST_TANK") != std::string::npos, "tank type label");
	expect(cannon != 0 && cannon->displayName.find("STR_TANK_CANNON") != std::string::npos, "cannon type label");
}

static void testMixedSoldierAndHwp()
{
	TacticalFixture fx;
	Soldier *roster = fx.addRosterSoldier(3);
	fx.addSoldierUnit(roster, Position(3, 3, 0));
	fx.addFriendlyHwp("STR_HWP_OBS_TEST_TANK", 1000000, Position(4, 4, 0));
	const std::vector<SquadOwnershipRow> rows = buildSquadOwnershipRows(&fx.game, fx.battle, 0, 0, -1);
	expect(findRow(rows, 3) != 0 && !findRow(rows, 3)->isFriendlyHwp, "soldier row preserved");
	expect(findRow(rows, 1000000) != 0 && findRow(rows, 1000000)->isFriendlyHwp, "HWP row present");
	const SquadOwnershipRow &first = rows.front();
	expect(!first.isFriendlyHwp, "soldiers sort before HWPs");
}

static void testDestroyedHwpExcluded()
{
	TacticalFixture fx;
	BattleUnit *dead = fx.addFriendlyHwp("STR_HWP_OBS_TEST_TANK", 1000000, Position(1, 1, 0));
	dead->load(YAML::Load(
		"status: 6\nstunlevel: 0\nhealth: 0\ntu: 0\nenergy: 0\nmorale: 0\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	expect(countHwpRows(buildSquadOwnershipRows(&fx.game, fx.battle, 0, 0, -1)) == 0, "destroyed HWP not listed");
}

static void testStableOrdinalSurvivesEarlierCasualty()
{
	TacticalFixture fx;
	BattleUnit *first = fx.addFriendlyHwp("STR_HWP_OBS_TEST_TANK", 1000001, Position(1, 1, 0));
	fx.addFriendlyHwp("STR_HWP_OBS_TEST_TANK", 1000002, Position(2, 2, 0));
	const std::vector<SquadOwnershipRow> initialRows = buildSquadOwnershipRows(&fx.game, fx.battle, 0, 0, -1);
	const SquadOwnershipRow *initial = findRow(initialRows, 1000002);
	expect(initial != 0 && initial->displayName.find("#2") != std::string::npos, "initial survivor is #2 before casualties");
	first->load(YAML::Load(
		"status: 6\nstunlevel: 0\nhealth: 0\ntu: 0\nenergy: 0\nmorale: 0\narmor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	const std::vector<SquadOwnershipRow> afterLoss = buildSquadOwnershipRows(&fx.game, fx.battle, 0, 0, -1);
	expect(countHwpRows(afterLoss) == 1, "only surviving tank listed");
	const SquadOwnershipRow *survivor = findRow(afterLoss, 1000002);
	expect(survivor != 0, "survivor row present");
	expect(survivor->displayName.find("#2") != std::string::npos, "survivor stays #2 after first destroyed");
	expect(survivor->displayName.find(" #1") == std::string::npos, "survivor must not rename to #1");
	OwnershipChangeOutcome o = tryApplyOwnershipToggle(&fx.game, fx.battle, 0, 1000002, true, -1);
	expect(o.ok, "ownership still toggles on survivor id");
	const std::vector<SquadOwnershipRow> reopenRows = buildSquadOwnershipRows(&fx.game, fx.battle, 0, 0, -1);
	const SquadOwnershipRow *reopen = findRow(reopenRows, 1000002);
	expect(reopen != 0, "squad reopen lists survivor");
	if (reopen != 0)
	{
		expect(reopen->displayName.find("#2") != std::string::npos, "squad reopen preserves #2");
		expect(reopen->effectiveOwner == CONTROL_AI, "assignment keyed to engine unit id");
	}
	const std::vector<SquadOwnershipRow> againRows = buildSquadOwnershipRows(&fx.game, fx.battle, 0, 0, -1);
	const SquadOwnershipRow *again = findRow(againRows, 1000002);
	expect(again != 0 && again->displayName.find("#2") != std::string::npos, "second rebuild still #2");
}

static void testHwpOwnershipTogglePersists()
{
	TacticalFixture fx;
	fx.addFriendlyHwp("STR_HWP_OBS_TEST_TANK", 1000000, Position(1, 1, 0));
	OwnershipChangeOutcome o = tryApplyOwnershipToggle(&fx.game, fx.battle, 0, 1000000, true, -1);
	expect(o.ok, "assign HWP to Aster");
	expect(effectiveTacticalHwpOwner(fx.battle, 1000000) == CONTROL_AI, "tactical AI set updated");
	const std::vector<SquadOwnershipRow> toggleRows = buildSquadOwnershipRows(&fx.game, fx.battle, 0, 0, -1);
	const SquadOwnershipRow *row = findRow(toggleRows, 1000000);
	expect(row != 0 && row->effectiveOwner == CONTROL_AI && row->canReclaimForHuman, "UI shows Aster after assign");
	o = tryApplyOwnershipToggle(&fx.game, fx.battle, 0, 1000000, false, -1);
	expect(o.ok, "reclaim HWP for human");
	expect(effectiveTacticalHwpOwner(fx.battle, 1000000) == CONTROL_HUMAN, "reclaimed to human");
}

static void testStaleHwpIdRejected()
{
	TacticalFixture fx;
	expect(!fx.battle->assignCompanionTacticalHwpToAi(1000099), "nonexistent HWP assign rejected");
}

static void testSoldierToggleUnchanged()
{
	TacticalFixture fx;
	Soldier *roster = fx.addRosterSoldier(2);
	fx.addSoldierUnit(roster, Position(1, 1, 0));
	OwnershipChangeOutcome o = tryApplyOwnershipToggle(&fx.game, fx.battle, 0, 2, true, -1);
	expect(o.ok, "soldier assign to Aster still works");
	expect(fx.game.getCompanionSoldierOwnership(2) == CONTROL_AI, "campaign ai assignment for soldier");
}

int companion_battlescape_hwp_squad_test_body()
{
	testSingleFriendlyHwpListed();
	testTwoIdenticalHwpsOrdinalById();
	testTwoDifferentHwpTypes();
	testMixedSoldierAndHwp();
	testDestroyedHwpExcluded();
	testStableOrdinalSurvivesEarlierCasualty();
	testHwpOwnershipTogglePersists();
	testStaleHwpIdRejected();
	testSoldierToggleUnchanged();

	if (g_failures == 0)
	{
		std::cout << "companion_battlescape_hwp_squad_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_battlescape_hwp_squad_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}