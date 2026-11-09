/*
 * Open X-Com Companion — inventory executor engine integration tests (M1-WP8B3 / WP8D).
 * Covers BattlescapeGame::tryExecuteCompanionInventoryMove, tryExecuteCompanionPrime,
 * and tryExecuteCompanionThrow (pre-BattlescapeState reject paths only).
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

/** WP8D — grenade belt → right hand via real executor (not validation-only). */
static void testUnprimedGrenadeBeltToRightHand()
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
	Soldier soldier(&rs, &ar, 10);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->setTimeUnits(100);

	RuleItem grenadeRules("STR_WP8D_TEST_GRENADE");
	YAML::Node grenadeNode;
	grenadeNode["weight"] = 1;
	grenadeNode["battleType"] = static_cast<int>(BT_GRENADE);
	grenadeNode["invWidth"] = 1;
	grenadeNode["invHeight"] = 1;
	grenadeRules.load(grenadeNode, &mod, 0);

	BattleItem *grenade = new BattleItem(&grenadeRules, battle.getCurrentItemId());
	expect(grenade->getFuseTimer() == -1, "fixture grenade starts unprimed");
	grenade->setSlot(mod.getInventory("STR_BELT", true));
	grenade->setSlotX(0);
	grenade->setSlotY(0);
	grenade->moveToOwner(unit);
	registerBattleItem(&battle, grenade);

	const int idBefore = grenade->getId();
	const int tuBefore = unit->getTimeUnits();
	const size_t itemCountBefore = battle.getItems()->size();
	expect(unit->getItem("STR_RIGHT_HAND") == 0, "right hand empty before move");

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionInventoryMove(
		unit,
		grenade,
		mod.getInventory("STR_RIGHT_HAND", true),
		0,
		0,
		&reject);
	expect(ok, reject.c_str());
	expect(grenade->getId() == idBefore, "grenade battle item id stable");
	expect(grenade->getOwner() == unit, "grenade still owned by actor");
	expect(grenade->getSlot()->getId() == "STR_RIGHT_HAND", "grenade in right hand");
	expect(grenade->getSlotX() == 0 && grenade->getSlotY() == 0, "hand coords");
	expect(unit->getItem("STR_RIGHT_HAND") == grenade, "unit hand query matches grenade");
	expect(unit->getItem("STR_BELT") == 0, "belt vacated after move");
	expect(grenade->getFuseTimer() == -1, "unprimed fuse unchanged");
	expect(battle.getItems()->size() == itemCountBefore, "no duplicate battle items");
	expect(unit->getTimeUnits() == tuBefore - 4, "TU spent matches belt→hand ruleset transfer cost");
}

/** WP8D — primed grenade inventory move blocked at executor with no mutation. */
static void testPrimedGrenadeInventoryMoveRejectedNoMutation()
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
	Soldier soldier(&rs, &ar, 11);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->setTimeUnits(100);

	RuleItem grenadeRules("STR_WP8D_PRIMED_GRENADE");
	YAML::Node grenadeNode;
	grenadeNode["weight"] = 1;
	grenadeNode["battleType"] = static_cast<int>(BT_GRENADE);
	grenadeNode["invWidth"] = 1;
	grenadeNode["invHeight"] = 1;
	grenadeRules.load(grenadeNode, &mod, 0);

	BattleItem *grenade = new BattleItem(&grenadeRules, battle.getCurrentItemId());
	grenade->setFuseTimer(3);
	grenade->setSlot(mod.getInventory("STR_BELT", true));
	grenade->setSlotX(0);
	grenade->setSlotY(0);
	grenade->moveToOwner(unit);
	registerBattleItem(&battle, grenade);

	const int idBefore = grenade->getId();
	const int tuBefore = unit->getTimeUnits();
	const int fuseBefore = grenade->getFuseTimer();
	expect(unit->getItem("STR_RIGHT_HAND") == 0, "right hand empty before rejected move");
	expect(unit->getItem("STR_BELT", 0, 0) == grenade, "grenade in belt before rejected move");

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionInventoryMove(
		unit,
		grenade,
		mod.getInventory("STR_RIGHT_HAND", true),
		0,
		0,
		&reject);
	expect(!ok, "primed grenade inventory move must fail");
	expect(reject == "Primed explosives cannot be moved via inventory.", "executor reject reason matches WP8D policy");
	expect(grenade->getId() == idBefore, "grenade id unchanged on reject");
	expect(grenade->getOwner() == unit, "grenade still owned by actor");
	expect(grenade->getSlot()->getId() == "STR_BELT", "grenade remains in belt");
	expect(grenade->getSlotX() == 0 && grenade->getSlotY() == 0, "belt coords unchanged");
	expect(unit->getItem("STR_BELT", 0, 0) == grenade, "unit belt query still holds grenade");
	expect(grenade->getFuseTimer() == fuseBefore && fuseBefore == 3, "fuse state unchanged");
	expect(unit->getTimeUnits() == tuBefore, "TU unchanged on primed reject (precheck before spend)");
	expect(unit->getItem("STR_RIGHT_HAND") == 0, "right hand still empty");
}

/** WP8D — PRIME executor on hand-held unprimed timed grenade. */
static void testUnprimedHandGrenadePrimeSucceeds()
{
	Mod mod;
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 20);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->getBaseStats()->tu = 60;
	unit->setTimeUnits(100);

	RuleItem grenadeRules("STR_WP8D_PRIME_GRENADE");
	YAML::Node grenadeNode;
	grenadeNode["weight"] = 1;
	grenadeNode["battleType"] = static_cast<int>(BT_GRENADE);
	grenadeNode["invWidth"] = 1;
	grenadeNode["invHeight"] = 1;
	grenadeRules.load(grenadeNode, &mod, 0);

	BattleItem *grenade = new BattleItem(&grenadeRules, battle.getCurrentItemId());
	expect(grenade->getFuseTimer() == -1, "hand grenade starts unprimed");
	grenade->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	grenade->setSlotX(0);
	grenade->setSlotY(0);
	grenade->moveToOwner(unit);
	registerBattleItem(&battle, grenade);

	const int idBefore = grenade->getId();
	const int tuBefore = unit->getTimeUnits();
	const size_t itemCountBefore = battle.getItems()->size();
	const int fuseRequest = 3;
	const int primeTu = unit->getActionTUs(BA_PRIME, grenade);
	expect(primeTu > 0, "BA_PRIME cost derivable for fixture grenade");
	expect(tuBefore > primeTu, "fixture TU sufficient for prime");

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPrime(unit, grenade, fuseRequest, &reject);
	expect(ok, reject.c_str());
	expect(grenade->getId() == idBefore, "grenade battle item id stable");
	expect(grenade->getOwner() == unit, "grenade still owned by actor");
	expect(grenade->getSlot()->getId() == "STR_RIGHT_HAND", "grenade remains in right hand");
	expect(grenade->getSlotX() == 0 && grenade->getSlotY() == 0, "hand coords unchanged");
	expect(unit->getItem("STR_RIGHT_HAND") == grenade, "unit hand query matches grenade");
	expect(grenade->getFuseTimer() == fuseRequest, "fuse set to requested timer");
	expect(battle.getItems()->size() == itemCountBefore, "no duplicate battle items");
	expect(unit->getTimeUnits() == tuBefore - primeTu, "TU spent matches getActionTUs(BA_PRIME, grenade)");
}

/** WP8D — PRIME fuse 0 on timed grenade (owner tactic: immediate-on-land / end-turn ground). */
static void testPrimeFuseZeroSucceeds()
{
	Mod mod;
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 23);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->getBaseStats()->tu = 60;
	unit->setTimeUnits(100);

	RuleItem grenadeRules("STR_WP8D_PRIME_FUSE0");
	YAML::Node grenadeNode;
	grenadeNode["weight"] = 1;
	grenadeNode["battleType"] = static_cast<int>(BT_GRENADE);
	grenadeNode["invWidth"] = 1;
	grenadeNode["invHeight"] = 1;
	grenadeRules.load(grenadeNode, &mod, 0);

	BattleItem *grenade = new BattleItem(&grenadeRules, battle.getCurrentItemId());
	expect(grenade->getFuseTimer() == -1, "starts unprimed");
	grenade->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	grenade->setSlotX(0);
	grenade->setSlotY(0);
	grenade->moveToOwner(unit);
	registerBattleItem(&battle, grenade);

	const int idBefore = grenade->getId();
	const int tuBefore = unit->getTimeUnits();
	const int primeTu = unit->getActionTUs(BA_PRIME, grenade);
	const int fuseRequest = 0;

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPrime(unit, grenade, fuseRequest, &reject);
	expect(ok, reject.c_str());
	expect(grenade->getFuseTimer() == 0, "fuse exactly 0 after prime (not confused with unprimed -1)");
	expect(grenade->getFuseTimer() >= 0, "primed semantics: fuse >= 0");
	expect(grenade->getId() == idBefore, "item id stable");
	expect(unit->getTimeUnits() == tuBefore - primeTu, "TU spent for prime");
	expect(unit->getItem("STR_RIGHT_HAND") == grenade, "grenade remains in hand");
}

/** WP8D — PRIME rejected when TU below BA_PRIME cost; no fuse mutation. */
static void testPrimeInsufficientTuNoMutation()
{
	Mod mod;
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 21);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->getBaseStats()->tu = 60;

	RuleItem grenadeRules("STR_WP8D_PRIME_LOW_TU");
	YAML::Node grenadeNode;
	grenadeNode["weight"] = 1;
	grenadeNode["battleType"] = static_cast<int>(BT_GRENADE);
	grenadeNode["invWidth"] = 1;
	grenadeNode["invHeight"] = 1;
	grenadeRules.load(grenadeNode, &mod, 0);

	BattleItem *grenade = new BattleItem(&grenadeRules, battle.getCurrentItemId());
	grenade->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	grenade->setSlotX(0);
	grenade->setSlotY(0);
	grenade->moveToOwner(unit);
	registerBattleItem(&battle, grenade);

	const int primeTu = unit->getActionTUs(BA_PRIME, grenade);
	expect(primeTu > 0, "BA_PRIME cost derivable");
	unit->setTimeUnits(primeTu > 0 ? primeTu - 1 : 0);

	const int tuBefore = unit->getTimeUnits();
	const int fuseBefore = grenade->getFuseTimer();
	const int idBefore = grenade->getId();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPrime(unit, grenade, 3, &reject);
	expect(!ok, "prime must fail when TU below cost");
	expect(reject == "Not enough time units to prime grenade.", "insufficient TU reject reason");
	expect(grenade->getFuseTimer() == fuseBefore && fuseBefore == -1, "fuse remains unprimed");
	expect(unit->getTimeUnits() == tuBefore, "TU unchanged on failed prime spend");
	expect(grenade->getId() == idBefore, "item id unchanged");
	expect(grenade->getSlot()->getId() == "STR_RIGHT_HAND", "grenade remains in right hand");
	expect(unit->getItem("STR_RIGHT_HAND") == grenade, "hand inventory unchanged");
}

/** WP8D — PRIME on already-primed grenade rejected before TU spend. */
static void testPrimeAlreadyPrimedRejectedNoMutation()
{
	Mod mod;
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 22);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->getBaseStats()->tu = 60;
	unit->setTimeUnits(100);

	RuleItem grenadeRules("STR_WP8D_ALREADY_PRIMED");
	YAML::Node grenadeNode;
	grenadeNode["weight"] = 1;
	grenadeNode["battleType"] = static_cast<int>(BT_GRENADE);
	grenadeNode["invWidth"] = 1;
	grenadeNode["invHeight"] = 1;
	grenadeRules.load(grenadeNode, &mod, 0);

	BattleItem *grenade = new BattleItem(&grenadeRules, battle.getCurrentItemId());
	grenade->setFuseTimer(2);
	grenade->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	grenade->setSlotX(0);
	grenade->setSlotY(0);
	grenade->moveToOwner(unit);
	registerBattleItem(&battle, grenade);

	const int tuBefore = unit->getTimeUnits();
	const int fuseBefore = grenade->getFuseTimer();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPrime(unit, grenade, 3, &reject);
	expect(!ok, "second prime must fail");
	expect(reject == "Grenade is already primed.", "already primed reject reason");
	expect(grenade->getFuseTimer() == fuseBefore && fuseBefore == 2, "fuse unchanged (not re-written)");
	expect(unit->getTimeUnits() == tuBefore, "TU unchanged on already-primed reject");
	expect(grenade->getSlot()->getId() == "STR_RIGHT_HAND", "inventory position unchanged");
}

static void wp8dLoadTimedGrenadeRules(Mod &mod, RuleItem &grenadeRules)
{
	YAML::Node grenadeNode;
	grenadeNode["weight"] = 1;
	grenadeNode["battleType"] = static_cast<int>(BT_GRENADE);
	grenadeNode["invWidth"] = 1;
	grenadeNode["invHeight"] = 1;
	grenadeRules.load(grenadeNode, &mod, 0);
}

/** Caller must keep {@p grenadeRules} alive for the test (same scope as BattleItem). */
static BattleItem *wp8dPlacePrimedHandGrenade(
	Mod &mod,
	SavedBattleGame &battle,
	BattleUnit *unit,
	RuleItem &grenadeRules)
{
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));
	BattleItem *grenade = new BattleItem(&grenadeRules, battle.getCurrentItemId());
	grenade->setFuseTimer(3);
	grenade->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	grenade->setSlotX(0);
	grenade->setSlotY(0);
	grenade->moveToOwner(unit);
	registerBattleItem(&battle, grenade);
	return grenade;
}

/** WP8D — THROW rejected when current TU below BA_THROW cost (precheck; no spend in executor). */
static void testThrowInsufficientTuNoMutation()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 30);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	RuleItem grenadeRules("STR_WP8D_THROW_LOW_TU");
	wp8dLoadTimedGrenadeRules(mod, grenadeRules);
	BattleItem *grenade = wp8dPlacePrimedHandGrenade(mod, battle, unit, grenadeRules);
	unit->getBaseStats()->tu = 0;
	const int throwTu = unit->getActionTUs(BA_THROW, grenade);
	expect(throwTu <= 0, "BA_THROW cost zero when base TU stat is zero");
	unit->setTimeUnits(100);

	const int tuBefore = unit->getTimeUnits();
	const int fuseBefore = grenade->getFuseTimer();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionThrow(unit, Position(2, 2, 0), grenade, &reject);
	expect(!ok, "throw must fail when throw cost is zero");
	expect(reject == "Not enough time units to throw grenade.", "insufficient TU throw reject");
	expect(unit->getTimeUnits() == tuBefore, "TU unchanged on throw precheck fail");
	expect(grenade->getFuseTimer() == fuseBefore, "fuse unchanged");
	expect(unit->getItem("STR_RIGHT_HAND") == grenade, "grenade remains in hand");
}

/** WP8D — THROW target outside map bounds rejected before BattlescapeState use. */
static void testThrowTargetOutOfMapBoundsRejected()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 31);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->getBaseStats()->tu = 60;
	unit->setTimeUnits(100);

	RuleItem grenadeRules("STR_WP8D_THROW_OOB");
	wp8dLoadTimedGrenadeRules(mod, grenadeRules);
	BattleItem *grenade = wp8dPlacePrimedHandGrenade(mod, battle, unit, grenadeRules);
	const int tuBefore = unit->getTimeUnits();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionThrow(unit, Position(99, 99, 0), grenade, &reject);
	expect(!ok, "throw must fail for OOB target");
	expect(reject == "Target out of map bounds.", "OOB throw reject reason");
	expect(unit->getTimeUnits() == tuBefore, "TU unchanged");
	expect(unit->getItem("STR_RIGHT_HAND") == grenade, "grenade remains in hand");
}

/** WP8D — THROW rejects non-grenade item (BT_AMMO clip); executor does not require primed fuse. */
static void testThrowNonGrenadeInHandRejected()
{
	Mod mod;
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 32);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->getBaseStats()->tu = 60;
	unit->setTimeUnits(100);

	RuleItem clipRules("STR_WP8D_THROW_CLIP");
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
	const bool ok = game.tryExecuteCompanionThrow(unit, Position(2, 2, 0), clip, &reject);
	expect(!ok, "throw must fail for non-grenade");
	expect(reject == "Item in hand does not support throwing.", "non-grenade throw reject");
	expect(unit->getItem("STR_RIGHT_HAND") == clip, "clip remains in hand");
}

/** WP8D — THROW with null weapon pointer (actor/item association enforced only by caller). */
static void testThrowNullWeaponRejected()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 33);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->getBaseStats()->tu = 60;
	unit->setTimeUnits(100);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionThrow(unit, Position(2, 2, 0), 0, &reject);
	expect(!ok, "throw must fail without weapon");
	expect(reject == "No grenade in hand.", "null weapon throw reject");
}

/** WP8D — unprimed grenade passes throwable precheck (fuse not checked before BattlescapeState). */
static void testThrowUnprimedGrenadePassesPrecheckToOobReject()
{
	Mod mod;
	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, 34);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	unit->setPosition(Position(1, 1, 0));
	battle.getUnits()->push_back(unit);
	battle.setUnitPosition(unit, Position(1, 1, 0));
	unit->getBaseStats()->tu = 60;
	unit->setTimeUnits(100);

	RuleItem grenadeRules("STR_WP8D_THROW_UNPRIMED");
	YAML::Node grenadeNode;
	grenadeNode["weight"] = 1;
	grenadeNode["battleType"] = static_cast<int>(BT_GRENADE);
	grenadeNode["invWidth"] = 1;
	grenadeNode["invHeight"] = 1;
	grenadeRules.load(grenadeNode, &mod, 0);
	mod.registerTestInventoryRule(makeHandInventory("STR_RIGHT_HAND"));
	BattleItem *grenade = new BattleItem(&grenadeRules, battle.getCurrentItemId());
	expect(grenade->getFuseTimer() == -1, "unprimed grenade fixture");
	grenade->setSlot(mod.getInventory("STR_RIGHT_HAND", true));
	grenade->setSlotX(0);
	grenade->setSlotY(0);
	grenade->moveToOwner(unit);
	registerBattleItem(&battle, grenade);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionThrow(unit, Position(50, 50, 0), grenade, &reject);
	expect(!ok, "still rejected on OOB");
	expect(reject == "Target out of map bounds.", "unprimed grenade reached bounds check (not blocked as unprimed)");
	expect(grenade->getFuseTimer() == -1, "fuse still unprimed");
}

int main()
{
	testClipRightHandToBelt();
	testNestedAmmoRejected();
	testInsufficientTuNoMutation();
	testWp8CompanionUnloadSucceedsForAiAssignedSoldierWithoutBattlescapeState();
	testUnprimedGrenadeBeltToRightHand();
	testPrimedGrenadeInventoryMoveRejectedNoMutation();
	testUnprimedHandGrenadePrimeSucceeds();
	testPrimeFuseZeroSucceeds();
	testPrimeInsufficientTuNoMutation();
	testPrimeAlreadyPrimedRejectedNoMutation();
	testThrowInsufficientTuNoMutation();
	testThrowTargetOutOfMapBoundsRejected();
	testThrowNonGrenadeInHandRejected();
	testThrowNullWeaponRejected();
	testThrowUnprimedGrenadePassesPrecheckToOobReject();
	return g_failures != 0 ? 1 : 0;
}
