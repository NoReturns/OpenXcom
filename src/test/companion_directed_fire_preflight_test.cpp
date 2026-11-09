/*
 * P1-FIRE-LANE-03 — conservative directed-fire visible-unit voxel preflight (engine).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/ProjectileFlyBState.h"
#include "../Engine/CompanionCommandCore.h"
#include "../Engine/RNG.h"
#include "../Mod/Armor.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleSoldier.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <string>

#ifdef main
#undef main
#endif

using namespace OpenXcom;
using namespace CompanionCommandCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static RuleInventory *makeHandInventory(const char *id)
{
	YAML::Node rh;
	rh["id"] = id;
	rh["x"] = 0;
	rh["y"] = 0;
	rh["type"] = static_cast<int>(INV_HAND);
	rh["costs"] = YAML::Node(YAML::NodeType::Map);
	RuleInventory *inv = new RuleInventory(id);
	inv->load(rh, 0);
	return inv;
}

static void setupOpenBattle(Mod &mod, SavedBattleGame &battle, BattleUnit **outShooter, BattleUnit **outTarget)
{
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));
	battle.setTestMod(&mod);
	battle.initMap(5, 5, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 1);
	BattleUnit *shooter = new BattleUnit(&soldier, 0);
	shooter->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(shooter);
	battle.setUnitPosition(shooter, Position(1, 1, 0));
	shooter->setTimeUnits(80);

	Soldier alienSoldier(&rs, &ar, 2);
	BattleUnit *target = new BattleUnit(&alienSoldier, 0);
	target->convertToFaction(FACTION_HOSTILE);
	target->setPosition(Position(3, 1, 0));
	target->setVisible(true);
	battle.getUnits()->push_back(target);
	battle.setUnitPosition(target, Position(3, 1, 0));

	RuleItem rifleRules("STR_TEST_RIFLE");
	YAML::Node rifleNode;
	rifleNode["weight"] = 5;
	rifleNode["battleType"] = static_cast<int>(BT_FIREARM);
	rifleNode["accuracySnap"] = 50;
	rifleNode["snapShot"] = 25;
	rifleNode["clipSize"] = 20;
	rifleNode["compatibleAmmo"] = YAML::Load("[STR_TEST_CLIP]");
	rifleNode["invWidth"] = 1;
	rifleNode["invHeight"] = 3;
	rifleRules.load(rifleNode, &mod, 0);

	RuleItem clipRules("STR_TEST_CLIP");
	YAML::Node clipNode;
	clipNode["weight"] = 1;
	clipNode["battleType"] = static_cast<int>(BT_AMMO);
	clipNode["clipSize"] = 20;
	clipNode["invWidth"] = 1;
	clipNode["invHeight"] = 1;
	clipRules.load(clipNode, &mod, 0);

	BattleItem *rifle = new BattleItem(&rifleRules, battle.getCurrentItemId());
	BattleItem *clip = new BattleItem(&clipRules, battle.getCurrentItemId());
	rifle->setAmmoItem(clip);
	rifle->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	rifle->setSlotX(0);
	rifle->setSlotY(0);
	rifle->moveToOwner(shooter);
	battle.getItems()->push_back(rifle);
	battle.getItems()->push_back(clip);
	shooter->setActiveHand("STR_RIGHT_HAND");

	if (outShooter != 0)
	{
		*outShooter = shooter;
	}
	if (outTarget != 0)
	{
		*outTarget = target;
	}
}

static void testPreflightEmptyTileTargetNotBlocked()
{
	Mod mod;
	SavedBattleGame battle;
	BattleUnit *shooter = 0;
	BattleUnit *target = 0;
	setupOpenBattle(mod, battle, &shooter, &target);
	BattlescapeGame game(&battle, 0);

	BattleAction probe = {};
	probe.type = BA_SNAPSHOT;
	probe.actor = shooter;
	probe.weapon = shooter->getItem("STR_RIGHT_HAND");
	probe.target = Position(4, 4, 0);

	const bool blocked = ProjectileFlyBState::companionPreflightVisibleUnitTargetBlocked(
		probe, &battle, &game, shooter->getPosition());
	expect(!blocked, "empty tile must not use unit-target preflight block");
}

static void testPreflightDoesNotTouchRngOrTu()
{
	Mod mod;
	SavedBattleGame battle;
	BattleUnit *shooter = 0;
	BattleUnit *target = 0;
	setupOpenBattle(mod, battle, &shooter, &target);
	BattlescapeGame game(&battle, 0);

	BattleAction probe = {};
	probe.type = BA_AIMEDSHOT;
	probe.actor = shooter;
	probe.weapon = shooter->getItem("STR_RIGHT_HAND");
	probe.target = target->getPosition();

	RNG::setSeed(0xC0FFEEULL);
	const uint64_t seedBefore = RNG::getSeed();
	const int tuBefore = shooter->getTimeUnits();
	const int facingBefore = shooter->getDirection();
	(void)ProjectileFlyBState::companionPreflightVisibleUnitTargetBlocked(
		probe, &battle, &game, shooter->getPosition());
	expect(RNG::getSeed() == seedBefore, "preflight must not advance RNG");
	expect(shooter->getTimeUnits() == tuBefore, "preflight must not spend TU");
	expect(shooter->getDirection() == facingBefore, "preflight must not change facing");
}

static void testPreflightSkipsHiddenUnitTarget()
{
	Mod mod;
	SavedBattleGame battle;
	BattleUnit *shooter = 0;
	BattleUnit *target = 0;
	setupOpenBattle(mod, battle, &shooter, &target);
	target->setVisible(false);
	BattlescapeGame game(&battle, 0);

	BattleAction probe = {};
	probe.type = BA_AIMEDSHOT;
	probe.actor = shooter;
	probe.weapon = shooter->getItem("STR_RIGHT_HAND");
	probe.target = target->getPosition();

	const bool blocked = ProjectileFlyBState::companionPreflightVisibleUnitTargetBlocked(
		probe, &battle, &game, shooter->getPosition());
	expect(!blocked, "hidden unit must not trigger conservative block");
}

int main()
{
	testPreflightEmptyTileTargetNotBlocked();
	testPreflightDoesNotTouchRngOrTu();
	testPreflightSkipsHiddenUnitTarget();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " failure(s)." << std::endl;
		return 1;
	}
	std::cout << "companion_directed_fire_preflight_test: all tests passed." << std::endl;
	return 0;
}
