/*
 * Open X-Com Companion — INVENTORY_MOVE engine integration tests (M1-WP8B3).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Armor.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/SavedGame.h"
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
	if (battle == 0 || item == 0)
	{
		return;
	}
	battle->getItems()->push_back(item);
}

static RuleInventory *makeHandInventory(const char *id)
{
	YAML::Node rh;
	rh["id"] = id;
	rh["x"] = 0;
	rh["y"] = 0;
	rh["type"] = static_cast<int>(INV_HAND);
	rh["costs"] = YAML::Node(YAML::NodeType::Map);
	rh["costs"]["STR_BELT"] = 8;
	RuleInventory *inv = new RuleInventory(id);
	inv->load(rh, 0);
	return inv;
}

static RuleInventory *makeBeltInventory()
{
	YAML::Node b;
	b["id"] = "STR_BELT";
	b["x"] = 0;
	b["y"] = 0;
	b["type"] = static_cast<int>(INV_SLOT);
	b["slots"] = YAML::Load("[[0,0],[1,0],[2,0]]");
	b["costs"]["STR_RIGHT_HAND"] = 4;
	RuleInventory *belt = new RuleInventory("STR_BELT");
	belt->load(b, 0);
	return belt;
}

static void testClipRightHandToBelt()
{
	Mod mod;
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));
	mod.registerTestInventoryRule(makeBeltInventory());

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 1);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->setTimeUnits(100);

	RuleItem clipRules("STR_TEST_CLIP");
	YAML::Node clipNode;
	clipNode["weight"] = 1;
	clipNode["battleType"] = static_cast<int>(BT_AMMO);
	clipNode["clipSize"] = 20;
	clipNode["invWidth"] = 1;
	clipNode["invHeight"] = 1;
	clipRules.load(clipNode, &mod, 0);

	BattleItem *clip = new BattleItem(&clipRules, battle.getCurrentItemId());
	clip->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	clip->setSlotX(0);
	clip->setSlotY(0);
	clip->moveToOwner(unit);
	registerBattleItem(&battle, clip);

	const int idBefore = clip->getId();
	const int tuBefore = unit->getTimeUnits();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionInventoryMove(
		unit,
		clip,
		mod.getInventory("STR_BELT", true),
		0,
		0,
		&reject);
	expect(ok, reject.c_str());
	expect(clip->getId() == idBefore, "battle item id stable");
	expect(clip->getSlot()->getId() == "STR_BELT", "clip in belt");
	expect(clip->getSlotX() == 0 && clip->getSlotY() == 0, "belt coords");
	expect(unit->getTimeUnits() == tuBefore - 8, "TU spent matches ruleset transfer cost");
}

static void testNestedAmmoRejected()
{
	Mod mod;
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));
	mod.registerTestInventoryRule(makeHandInventory("STR_LEFT_HAND"));
	mod.registerTestInventoryRule(makeBeltInventory());

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 2);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->setTimeUnits(100);

	RuleItem rifleRules("STR_TEST_RIFLE");
	YAML::Node rifleNode;
	rifleNode["weight"] = 5;
	rifleNode["battleType"] = static_cast<int>(BT_FIREARM);
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
	BattleItem *nested = new BattleItem(&clipRules, battle.getCurrentItemId());
	expect(rifle->setAmmoItem(nested) == 0, "rifle accepts nested magazine");

	rifle->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	rifle->setSlotX(0);
	rifle->setSlotY(0);
	rifle->moveToOwner(unit);
	registerBattleItem(&battle, rifle);
	registerBattleItem(&battle, nested);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionInventoryMove(
		unit,
		nested,
		mod.getInventory("STR_BELT", true),
		0,
		0,
		&reject);
	expect(!ok, "nested magazine move must fail");
	expect(nested->getSlot() == 0, "nested item stays nested (no slot)");
}

static void testInsufficientTuNoMutation()
{
	Mod mod;
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));
	mod.registerTestInventoryRule(makeBeltInventory());

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 3);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->setTimeUnits(4);

	RuleItem clipRules("STR_TEST_CLIP");
	YAML::Node clipNode;
	clipNode["weight"] = 1;
	clipNode["battleType"] = static_cast<int>(BT_AMMO);
	clipNode["clipSize"] = 20;
	clipNode["invWidth"] = 1;
	clipNode["invHeight"] = 1;
	clipRules.load(clipNode, &mod, 0);

	BattleItem *clip = new BattleItem(&clipRules, battle.getCurrentItemId());
	clip->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	clip->setSlotX(0);
	clip->setSlotY(0);
	clip->moveToOwner(unit);
	registerBattleItem(&battle, clip);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionInventoryMove(
		unit,
		clip,
		mod.getInventory("STR_BELT", true),
		0,
		0,
		&reject);
	expect(!ok, "insufficient TU must fail");
	expect(clip->getSlot()->getId() == "STR_RIGHT_HAND", "clip stays in hand on failure");
	expect(unit->getTimeUnits() == 4, "TU unchanged on failure");
}

/**
 * WP8: companion UNLOAD must not reuse human-order admission (warnIfHumanPlayerOrderDenied).
 * AI-assigned roster soldiers have humanOrderPermitted == false; execution is gated by CompanionCommandRuntime only.
 */
static void testWp8CompanionUnloadSucceedsForAiAssignedSoldierWithoutBattlescapeState()
{
	Mod mod;
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));
	mod.registerTestInventoryRule(makeHandInventory("STR_LEFT_HAND"));

	SavedGame campaign;

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.setGeoscapeSaveHint(&campaign);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 1);

	battle.assignCompanionTacticalSoldierToAi(1);

	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->setTimeUnits(100);

	RuleItem rifleRules("STR_WP8_UNLOAD_RIFLE");
	YAML::Node rifleNode;
	rifleNode["weight"] = 5;
	rifleNode["battleType"] = static_cast<int>(BT_FIREARM);
	rifleNode["clipSize"] = 0;
	rifleNode["compatibleAmmo"] = YAML::Load("[STR_WP8_UNLOAD_CLIP]");
	rifleNode["invWidth"] = 1;
	rifleNode["invHeight"] = 3;
	rifleRules.load(rifleNode, &mod, 0);

	RuleItem clipRules("STR_WP8_UNLOAD_CLIP");
	YAML::Node clipNode;
	clipNode["weight"] = 1;
	clipNode["battleType"] = static_cast<int>(BT_AMMO);
	clipNode["clipSize"] = 20;
	clipNode["invWidth"] = 1;
	clipNode["invHeight"] = 1;
	clipRules.load(clipNode, &mod, 0);

	BattleItem *rifle = new BattleItem(&rifleRules, battle.getCurrentItemId());
	BattleItem *clip = new BattleItem(&clipRules, battle.getCurrentItemId());
	expect(rifle->setAmmoItem(clip) == 0, "rifle loads magazine");

	rifle->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	rifle->setSlotX(0);
	rifle->setSlotY(0);
	rifle->moveToOwner(unit);
	registerBattleItem(&battle, rifle);
	registerBattleItem(&battle, clip);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionUnload(unit, rifle, clip->getId(), &reject);
	expect(ok, reject.c_str());
	expect(rifle->getAmmoItem() == 0, "magazine cleared from weapon");
	expect(unit->getItem("STR_LEFT_HAND") == clip, "clip in left hand after unload");
	expect(unit->getItem("STR_RIGHT_HAND") == rifle, "rifle in right hand after unload");
}

int main()
{
	testClipRightHandToBelt();
	testNestedAmmoRejected();
	testInsufficientTuNoMutation();
	testWp8CompanionUnloadSucceedsForAiAssignedSoldierWithoutBattlescapeState();
	return g_failures != 0 ? 1 : 0;
}
