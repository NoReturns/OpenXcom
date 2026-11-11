/*
 * Open X-Com Companion — PICKUP / DROP engine integration tests (M1-WP8B4V).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/Position.h"
#include "../Mod/Mod.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleSoldier.h"
#include "../Mod/Armor.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include "../Engine/CompanionGroundInventoryCore.h"
#include "../Engine/CompanionDropGroundPlacementCore.h"
#include <iostream>
#include <string>
#include <vector>

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

static RuleInventory *makeHandInventory(Mod &mod, const char *id, int costToGround)
{
	YAML::Node rh;
	rh["id"] = id;
	rh["x"] = 0;
	rh["y"] = 0;
	rh["type"] = static_cast<int>(INV_HAND);
	rh["costs"]["STR_GROUND"] = costToGround;
	RuleInventory *inv = new RuleInventory(id);
	inv->load(rh, 0);
	mod.registerTestInventoryRule(inv);
	return inv;
}

static RuleInventory *makeBeltInventory(Mod &mod, int costToGround)
{
	YAML::Node b;
	b["id"] = "STR_BELT";
	b["x"] = 0;
	b["y"] = 0;
	b["type"] = static_cast<int>(INV_SLOT);
	b["slots"] = YAML::Load("[[0,0],[1,0],[2,0]]");
	b["costs"]["STR_RIGHT_HAND"] = 4;
	b["costs"]["STR_GROUND"] = costToGround;
	RuleInventory *belt = new RuleInventory("STR_BELT");
	belt->load(b, 0);
	mod.registerTestInventoryRule(belt);
	return belt;
}

static RuleInventory *makeGroundInventory(Mod &mod, int costToRightHand, int costToBelt)
{
	YAML::Node g;
	g["id"] = "STR_GROUND";
	g["x"] = 0;
	g["y"] = 0;
	g["type"] = static_cast<int>(INV_GROUND);
	g["costs"]["STR_RIGHT_HAND"] = costToRightHand;
	g["costs"]["STR_BELT"] = costToBelt;
	RuleInventory *ground = new RuleInventory("STR_GROUND");
	ground->load(g, 0);
	mod.registerTestInventoryRule(ground);
	return ground;
}

static MapData *makeFloorMapData(MapDataSet *ds)
{
	MapData *floor = new MapData(ds);
	floor->setObjectType(O_FLOOR);
	floor->setTUCosts(4, 4, 4);
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
	static MapDataSet *ds = 0;
	if (ds == 0)
	{
		ds = new MapDataSet("pickup_drop_test");
	}
	MapData *floor = makeFloorMapData(ds);
	tile->setMapData(floor, 1, 0, O_FLOOR);
	unit->setPosition(pos);
	Tile *below = battle->getTile(Position(pos.x, pos.y, pos.z - 1));
	unit->setTile(tile, below);
}

static RuleItem &makeUnconsciousCorpseRules(Mod &mod)
{
	static RuleItem rules("STR_P1C_UNCONSCIOUS_CORPSE");
	static bool loaded = false;
	if (!loaded)
	{
		YAML::Node n;
		n["type"] = "STR_P1C_UNCONSCIOUS_CORPSE";
		n["battleType"] = static_cast<int>(BT_CORPSE);
		n["size"] = 1;
		n["weight"] = 10;
		rules.load(n, &mod, 0);
		loaded = true;
	}
	return rules;
}

static RuleInventory *makeBackpackInventory(Mod &mod, int costToGround)
{
	RuleInventory *backpack = new RuleInventory("STR_BACK_PACK");
	YAML::Node bp;
	bp["id"] = "STR_BACK_PACK";
	bp["x"] = 0;
	bp["y"] = 0;
	bp["type"] = static_cast<int>(INV_SLOT);
	bp["slots"] = YAML::Load("[[0,0],[1,0]]");
	bp["costs"]["STR_GROUND"] = costToGround;
	backpack->load(bp, 0);
	mod.registerTestInventoryRule(backpack);
	return backpack;
}

static BattleUnit *makeUnconsciousRosterSoldier(SavedBattleGame *battle, Mod &mod, int soldierId)
{
	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, soldierId);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	battle->getUnits()->push_back(unit);
	unit->load(YAML::Load(
		"status: 7\nstunlevel: 200\nhealth: 20\ntu: 0\nenergy: 0\nmorale: 40\n"
		"armor: [0,0,0,0,0]\nfatalWounds: [0,0,0,0,0,0]\n"));
	unit->setPosition(Position(-1, -1, -1));
	return unit;
}

static void carryItemOnSoldier(BattleUnit *carrier, BattleItem *item, RuleInventory *slot, int x, int y)
{
	item->setSlot(slot);
	item->setSlotX(x);
	item->setSlotY(y);
	item->moveToOwner(carrier);
}

static BattleUnit *makeSoldier(SavedBattleGame *battle, Mod &mod, int soldierId, const Position &pos)
{
	RuleSoldier rs("STR_SOLDIER");
	Armor ar("STR_JUMPSUIT");
	Soldier soldier(&rs, &ar, soldierId);
	BattleUnit *unit = new BattleUnit(&soldier, 0);
	battle->getUnits()->push_back(unit);
	unit->setTimeUnits(200);
	wireUnitTile(battle, unit, pos);
	return unit;
}

static BattleItem *placeGroundClip(
	SavedBattleGame *battle,
	RuleItem *clipRules,
	RuleInventory *groundRule,
	const Position &pos,
	int gx,
	int gy)
{
	Tile *tile = battle->getTile(pos);
	expect(tile != 0, "ground tile for item");
	if (tile == 0)
	{
		return 0;
	}
	tile->setDiscovered(true, 2);
	BattleItem *clip = new BattleItem(clipRules, battle->getCurrentItemId());
	clip->setSlot(groundRule);
	clip->setSlotX(gx);
	clip->setSlotY(gy);
	tile->addItem(clip, groundRule);
	registerBattleItem(battle, clip);
	return clip;
}

static int countItemsOnTile(Tile *tile)
{
	if (tile == 0)
	{
		return -1;
	}
	return static_cast<int>(tile->getInventory()->size());
}

static bool vectorContains(std::vector<BattleItem *> *items, BattleItem *needle)
{
	if (items == 0)
	{
		return false;
	}
	for (std::vector<BattleItem *>::const_iterator i = items->begin(); i != items->end(); ++i)
	{
		if (*i == needle)
		{
			return true;
		}
	}
	return false;
}

static void snapshotTileItemIds(Tile *tile, std::vector<int> *outIds)
{
	outIds->clear();
	if (tile == 0)
	{
		return;
	}
	for (std::vector<BattleItem *>::const_iterator i = tile->getInventory()->begin(); i != tile->getInventory()->end(); ++i)
	{
		if (*i != 0)
		{
			outIds->push_back((*i)->getId());
		}
	}
}

static bool vectorSameInts(const std::vector<int> &a, const std::vector<int> &b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (a[i] != b[i])
		{
			return false;
		}
	}
	return true;
}

static RuleItem makeStandardClipRules(Mod &mod)
{
	RuleItem clipRules("STR_PD_CLIP");
	YAML::Node clipNode;
	clipNode["weight"] = 1;
	clipNode["battleType"] = static_cast<int>(BT_AMMO);
	clipNode["clipSize"] = 20;
	clipNode["invWidth"] = 1;
	clipNode["invHeight"] = 1;
	clipRules.load(clipNode, &mod, 0);
	return clipRules;
}

static void testPickupGroundToRightHandSuccess(Mod &mod, RuleItem &clipRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 1, Position(1, 1, 0));
	BattleItem *clip = placeGroundClip(&battle, &clipRules, groundRule, Position(1, 1, 0), 0, 0);

	const int idBefore = clip->getId();
	const int tuBefore = unit->getTimeUnits();
	const int onTileBefore = countItemsOnTile(unit->getTile());

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const std::size_t battleItemCountBefore = battle.getItems()->size();
	const bool ok = game.tryExecuteCompanionPickup(
		unit,
		clip,
		Position(1, 1, 0),
		rightHand,
		0,
		0,
		&reject);
	expect(ok, reject.c_str());
	expect(clip->getId() == idBefore, "pickup: battle item id stable");
	expect(clip->getOwner() == unit, "pickup: owner is actor");
	expect(clip->getSlot() == rightHand, "pickup: slot is right hand");
	expect(clip->getTile() == 0 || clip->getTile() != unit->getTile(), "pickup: not on ground tile as carried");
	expect(vectorContains(unit->getInventory(), clip), "pickup: unit inventory lists item");
	expect(unit->getTimeUnits() == tuBefore - 10, "pickup: TU matches STR_GROUND->STR_RIGHT_HAND cost");
	expect(countItemsOnTile(unit->getTile()) == onTileBefore - 1, "pickup: tile lost ground item");
	expect(!vectorContains(unit->getTile()->getInventory(), clip), "pickup: tile inventory no longer has clip");
	expect(battle.getItems()->size() == battleItemCountBefore, "pickup: no duplicate battle item registration");
}

static void testDropBeltToGroundSuccess(Mod &mod, RuleItem &clipRules)
{
	makeGroundInventory(mod, 10, 12);
	makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	RuleInventory *belt = makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	RuleInventory *groundRule = mod.getInventory("STR_GROUND", true);

	BattleUnit *unit = makeSoldier(&battle, mod, 2, Position(2, 1, 0));
	BattleItem *clip = new BattleItem(&clipRules, battle.getCurrentItemId());
	clip->setSlot(belt);
	clip->setSlotX(0);
	clip->setSlotY(0);
	clip->moveToOwner(unit);
	registerBattleItem(&battle, clip);

	const int idBefore = clip->getId();
	const int tuBefore = unit->getTimeUnits();
	const int onTileBefore = countItemsOnTile(unit->getTile());

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const std::size_t battleItemCountBefore = battle.getItems()->size();
	const bool ok = game.tryExecuteCompanionDrop(unit, clip, &reject);
	expect(ok, reject.c_str());
	expect(clip->getId() == idBefore, "drop: battle item id stable");
	expect(clip->getOwner() == 0, "drop: no soldier owner on ground");
	expect(clip->getSlot() == groundRule, "drop: ground slot");
	expect(clip->getTile() == unit->getTile(), "drop: item on actor tile");
	expect(!vectorContains(unit->getInventory(), clip), "drop: not in unit inventory");
	expect(vectorContains(unit->getTile()->getInventory(), clip), "drop: tile inventory has item");
	expect(unit->getTimeUnits() == tuBefore - 6, "drop: TU matches STR_BELT->STR_GROUND");
	expect(countItemsOnTile(unit->getTile()) == onTileBefore + 1, "drop: tile gained item");
	expect(battle.getItems()->size() == battleItemCountBefore, "drop: no duplicate battle item registration");

	CompanionDropGroundPlacementCore::GroundPlacementEvidence placement = {};
	expect(CompanionDropGroundPlacementCore::captureAfterSuccessfulDrop(clip, unit, idBefore, &placement),
		"drop: capture placement evidence");
	expect(placement.present, "drop: placement present");
	expect(placement.verified, "drop: placement verified");
	expect(placement.verificationStatus == "engine_postcondition_verified", "drop: verification status");
	expect(placement.itemBattleItemId == idBefore, "drop: placement item id");
	expect(placement.tileX == 2 && placement.tileY == 1 && placement.tileZ == 0, "drop: placement tile");
	expect(placement.inventoryX == clip->getSlotX() && placement.inventoryY == clip->getSlotY(),
		"drop: placement inventory cell");
}

static void testDropCarriedUnconsciousBodyToGround(Mod &mod)
{
	makeGroundInventory(mod, 10, 12);
	RuleInventory *backpack = makeBackpackInventory(mod, 6);
	RuleInventory *groundRule = mod.getInventory("STR_GROUND", true);
	RuleItem &corpseRules = makeUnconsciousCorpseRules(mod);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	const Position carrierPos(2, 2, 0);
	BattleUnit *carrier = makeSoldier(&battle, mod, 1, carrierPos);
	BattleUnit *injured = makeUnconsciousRosterSoldier(&battle, mod, 3);
	BattleItem *body = new BattleItem(&corpseRules, battle.getCurrentItemId());
	body->setUnit(injured);
	carryItemOnSoldier(carrier, body, backpack, 0, 0);
	registerBattleItem(&battle, body);

	const int bodyId = body->getId();
	expect(injured->getPosition() == Position(-1, -1, -1), "pre-drop: carried soldier off-map");
	expect(vectorContains(carrier->getInventory(), body), "pre-drop: body in carrier inventory");

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionDrop(carrier, body, &reject);
	expect(ok, reject.c_str());
	expect(body->getId() == bodyId, "drop body: battle item id stable");
	expect(body->getOwner() == 0, "drop body: not soldier-owned");
	expect(body->getSlot() == groundRule, "drop body: ground slot");
	expect(body->getTile() == carrier->getTile(), "drop body: on carrier tile ground");
	expect(vectorContains(carrier->getTile()->getInventory(), body), "drop body: tile ground inventory");
	expect(!vectorContains(carrier->getInventory(), body), "drop body: removed from carrier");
	expect(body->getUnit() == injured, "drop body: corpse still linked to unconscious soldier");
	expect(injured->getPosition() == carrierPos, "drop body: unconscious soldier placed on-map at carrier");
	expect(injured->getPosition() != Position(-1, -1, -1), "drop body: not contradictory carried/off-map");
	expect(injured->getStatus() == STATUS_UNCONSCIOUS, "drop body: soldier remains unconscious");

	CompanionDropGroundPlacementCore::GroundPlacementEvidence placement = {};
	expect(CompanionDropGroundPlacementCore::captureAfterSuccessfulDrop(body, carrier, bodyId, &placement),
		"drop body: capture placement");
	expect(placement.present && placement.verified, "drop body: placement verified");
	expect(placement.itemBattleItemId == bodyId, "drop body: placement item id");
	expect(placement.tileX == carrierPos.x && placement.tileY == carrierPos.y && placement.tileZ == carrierPos.z,
		"drop body: placement tile");

}

static void testPickupWrongGroundPositionRejected(Mod &mod, RuleItem &clipRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 3, Position(1, 1, 0));
	BattleItem *clip = placeGroundClip(&battle, &clipRules, groundRule, Position(1, 1, 0), 0, 0);
	const int tuBefore = unit->getTimeUnits();
	std::vector<int> tileIdsBefore;
	snapshotTileItemIds(unit->getTile(), &tileIdsBefore);
	const std::size_t battleItemsBefore = battle.getItems()->size();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(
		unit,
		clip,
		Position(2, 1, 0),
		rightHand,
		0,
		0,
		&reject);
	expect(!ok, "wrong ground position must fail");
	expect(clip->getOwner() == 0, "reject pickup: still unowned");
	expect(unit->getTimeUnits() == tuBefore, "reject pickup: TU unchanged");
	expect(vectorContains(unit->getTile()->getInventory(), clip), "reject pickup: clip stays on tile");
	std::vector<int> tileIdsAfter;
	snapshotTileItemIds(unit->getTile(), &tileIdsAfter);
	expect(vectorSameInts(tileIdsBefore, tileIdsAfter), "reject pickup: tile membership unchanged");
	expect(battle.getItems()->size() == battleItemsBefore, "reject pickup: no new battle items");
}

static void testPickupItemOnOtherTileRejected(Mod &mod, RuleItem &clipRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	Tile *remote = battle.getTile(Position(3, 1, 0));
	expect(remote != 0, "remote tile");
	if (remote != 0)
	{
		remote->setDiscovered(true, 2);
	}

	BattleUnit *unit = makeSoldier(&battle, mod, 4, Position(1, 1, 0));
	BattleItem *clip = placeGroundClip(&battle, &clipRules, groundRule, Position(3, 1, 0), 0, 0);
	const int tuBefore = unit->getTimeUnits();
	std::vector<int> remoteIdsBefore;
	snapshotTileItemIds(remote, &remoteIdsBefore);
	const std::size_t battleItemsBefore = battle.getItems()->size();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(
		unit,
		clip,
		Position(1, 1, 0),
		rightHand,
		0,
		0,
		&reject);
	expect(!ok, "foreign tile ground item must fail");
	expect(clip->getOwner() == 0, "reject foreign tile: unowned");
	expect(unit->getTimeUnits() == tuBefore, "reject foreign tile: TU unchanged");
	std::vector<int> remoteIdsAfter;
	snapshotTileItemIds(remote, &remoteIdsAfter);
	expect(vectorSameInts(remoteIdsBefore, remoteIdsAfter), "reject foreign tile: remote tile unchanged");
	expect(battle.getItems()->size() == battleItemsBefore, "reject foreign tile: no new battle items");
}

static void testPickupInsufficientTuNoMutation(Mod &mod, RuleItem &clipRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 5, Position(1, 1, 0));
	unit->setTimeUnits(5);
	BattleItem *clip = placeGroundClip(&battle, &clipRules, groundRule, Position(1, 1, 0), 0, 0);
	std::vector<int> tileIdsBefore;
	snapshotTileItemIds(unit->getTile(), &tileIdsBefore);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(
		unit,
		clip,
		Position(1, 1, 0),
		rightHand,
		0,
		0,
		&reject);
	expect(!ok, "insufficient TU pickup must fail");
	expect(clip->getOwner() == 0, "insufficient TU: still ground");
	expect(unit->getTimeUnits() == 5, "insufficient TU: TU unchanged");
	std::vector<int> tileIdsAfter;
	snapshotTileItemIds(unit->getTile(), &tileIdsAfter);
	expect(vectorSameInts(tileIdsBefore, tileIdsAfter), "insufficient TU: tile unchanged");
}

static void testPickupReservedTuRejected(Mod &mod, RuleItem &clipRules, RuleItem &rifleRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);
	battle.setTUReserved(BA_SNAPSHOT);
	battle.setKneelReserved(false);

	BattleUnit *unit = makeSoldier(&battle, mod, 6, Position(1, 1, 0));
	// Flat snap 30 + pickup 10 must exceed remaining TU (checkReservedTU compares sum to current TU).
	unit->setTimeUnits(39);
	BattleItem *rifle = new BattleItem(&rifleRules, battle.getCurrentItemId());
	BattleItem *mag = new BattleItem(&clipRules, battle.getCurrentItemId());
	expect(rifle->setAmmoItem(mag) == 0, "pickup reserve: rifle loaded");
	rifle->setSlot(rightHand);
	rifle->setSlotX(0);
	rifle->setSlotY(0);
	rifle->moveToOwner(unit);
	registerBattleItem(&battle, rifle);
	registerBattleItem(&battle, mag);

	BattleItem *clip = placeGroundClip(&battle, &clipRules, groundRule, Position(1, 1, 0), 0, 1);
	std::vector<int> tileIdsBefore;
	snapshotTileItemIds(unit->getTile(), &tileIdsBefore);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(
		unit,
		clip,
		Position(1, 1, 0),
		rightHand,
		0,
		0,
		&reject);
	expect(!ok, "reserved TU must block pickup");
	expect(clip->getOwner() == 0, "reserved TU: clip on ground");
	expect(unit->getTimeUnits() == 39, "reserved TU: no spend");
	std::vector<int> tileIdsAfter;
	snapshotTileItemIds(unit->getTile(), &tileIdsAfter);
	expect(vectorSameInts(tileIdsBefore, tileIdsAfter), "reserved TU: tile unchanged");
}

static void testPickupOccupiedRightHandRejected(Mod &mod, RuleItem &clipRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 7, Position(1, 1, 0));
	BattleItem *inHand = new BattleItem(&clipRules, battle.getCurrentItemId());
	inHand->setSlot(rightHand);
	inHand->setSlotX(0);
	inHand->setSlotY(0);
	inHand->moveToOwner(unit);
	registerBattleItem(&battle, inHand);

	BattleItem *onGround = placeGroundClip(&battle, &clipRules, groundRule, Position(1, 1, 0), 1, 0);
	std::vector<int> tileIdsBefore;
	snapshotTileItemIds(unit->getTile(), &tileIdsBefore);
	const int gx = onGround->getSlotX();
	const int gy = onGround->getSlotY();
	const std::size_t battleItemsBefore = battle.getItems()->size();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(
		unit,
		onGround,
		Position(1, 1, 0),
		rightHand,
		0,
		0,
		&reject);
	expect(!ok, "occupied right hand must fail pickup");
	expect(onGround->getOwner() == 0, "occupied hand: ground item stays ground");
	expect(onGround->getSlot() == groundRule, "occupied hand: still ground slot");
	expect(onGround->getSlotX() == gx && onGround->getSlotY() == gy, "occupied hand: grid coords stable");
	std::vector<int> tileIdsAfter;
	snapshotTileItemIds(unit->getTile(), &tileIdsAfter);
	expect(vectorSameInts(tileIdsBefore, tileIdsAfter), "occupied hand: tile membership unchanged");
	expect(battle.getItems()->size() == battleItemsBefore, "occupied hand: no new battle items");
}

static void testPickupInvalidBeltCoordsRejected(Mod &mod, RuleItem &clipRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	(void)makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	RuleInventory *belt = makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 8, Position(1, 1, 0));
	BattleItem *clip = placeGroundClip(&battle, &clipRules, groundRule, Position(1, 1, 0), 0, 0);
	std::vector<int> tileIdsBefore;
	snapshotTileItemIds(unit->getTile(), &tileIdsBefore);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(
		unit,
		clip,
		Position(1, 1, 0),
		belt,
		99,
		99,
		&reject);
	expect(!ok, "invalid belt geometry must fail");
	expect(clip->getOwner() == 0, "invalid belt: item stays ground");
	std::vector<int> tileIdsAfter;
	snapshotTileItemIds(unit->getTile(), &tileIdsAfter);
	expect(vectorSameInts(tileIdsBefore, tileIdsAfter), "invalid belt: tile unchanged");
}

static void testDropInsufficientTuNoMutation(Mod &mod, RuleItem &clipRules)
{
	makeGroundInventory(mod, 10, 12);
	makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	RuleInventory *belt = makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 9, Position(1, 1, 0));
	unit->setTimeUnits(3);
	BattleItem *clip = new BattleItem(&clipRules, battle.getCurrentItemId());
	clip->setSlot(belt);
	clip->setSlotX(0);
	clip->setSlotY(0);
	clip->moveToOwner(unit);
	registerBattleItem(&battle, clip);
	std::vector<int> tileIdsBefore;
	snapshotTileItemIds(unit->getTile(), &tileIdsBefore);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionDrop(unit, clip, &reject);
	expect(!ok, "insufficient TU drop must fail");
	expect(clip->getOwner() == unit, "insufficient TU drop: still carried");
	expect(unit->getTimeUnits() == 3, "insufficient TU drop: TU unchanged");
	expect(clip->getSlot() == belt, "insufficient TU drop: still on belt");
	std::vector<int> tileIdsAfter;
	snapshotTileItemIds(unit->getTile(), &tileIdsAfter);
	expect(vectorSameInts(tileIdsBefore, tileIdsAfter), "insufficient TU drop: tile unchanged");
}

static void testDropNestedAmmoRejected(Mod &mod, RuleItem &clipRules, RuleItem &rifleRules)
{
	makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 10, Position(1, 1, 0));
	BattleItem *rifle = new BattleItem(&rifleRules, battle.getCurrentItemId());
	BattleItem *nested = new BattleItem(&clipRules, battle.getCurrentItemId());
	expect(rifle->setAmmoItem(nested) == 0, "rifle accepts magazine");
	rifle->setSlot(rightHand);
	rifle->setSlotX(0);
	rifle->setSlotY(0);
	rifle->moveToOwner(unit);
	registerBattleItem(&battle, rifle);
	registerBattleItem(&battle, nested);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionDrop(unit, nested, &reject);
	expect(!ok, "nested magazine drop must fail");
	expect(nested->getSlot() == 0, "nested drop reject: slot still null");
	expect(nested->getOwner() == 0, "nested magazine stays non-carried while loaded");
	expect(nested->getTile() == 0, "nested drop reject: not on tile");
}

static void testPickupWrongBattleItemIdRejected(Mod &mod, RuleItem &clipRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 50, Position(1, 1, 0));
	(void)placeGroundClip(&battle, &clipRules, groundRule, Position(1, 1, 0), 0, 0);
	BattleItem *wrong = new BattleItem(&clipRules, battle.getCurrentItemId());
	wrong->setSlot(rightHand);
	wrong->setSlotX(0);
	wrong->setSlotY(0);
	wrong->moveToOwner(unit);
	registerBattleItem(&battle, wrong);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(
		unit,
		wrong,
		Position(1, 1, 0),
		rightHand,
		0,
		0,
		&reject);
	expect(!ok, "item not on ground must fail pickup");
	expect(wrong->getOwner() == unit, "wrong id reject: still carried");
}

static void testPickupGroundItemWithSoldierOwnerRejected(Mod &mod, RuleItem &clipRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *a = makeSoldier(&battle, mod, 51, Position(1, 1, 0));
	BattleUnit *b = makeSoldier(&battle, mod, 52, Position(1, 1, 0));
	Tile *tile = a->getTile();
	expect(tile == b->getTile(), "stacked units same tile");

	BattleItem *clip = placeGroundClip(&battle, &clipRules, groundRule, Position(1, 1, 0), 0, 0);
	const int slotX = clip->getSlotX();
	const int slotY = clip->getSlotY();
	// Engine forbids non-null owner on ground pickup; synthesize invalid-but-diagnosable state.
	clip->setOwner(b);
	b->getInventory()->push_back(clip);
	const int tuBefore = a->getTimeUnits();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(a, clip, Position(1, 1, 0), rightHand, 0, 0, &reject);
	expect(!ok, "ground item with owner must fail pickup");
	expect(clip->getOwner() == b, "owner reject: still other soldier");
	expect(a->getTimeUnits() == tuBefore, "owner reject: TU unchanged");
	expect(clip->getSlot() == groundRule && clip->getSlotX() == slotX && clip->getSlotY() == slotY, "owner reject: slot coords stable");
}

static void testPickupNestedMagazineRejected(Mod &mod, RuleItem &clipRules, RuleItem &rifleRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 53, Position(1, 1, 0));
	BattleItem *rifle = new BattleItem(&rifleRules, battle.getCurrentItemId());
	BattleItem *nested = new BattleItem(&clipRules, battle.getCurrentItemId());
	expect(rifle->setAmmoItem(nested) == 0, "nested accepts");
	rifle->setSlot(groundRule);
	rifle->setSlotX(0);
	rifle->setSlotY(0);
	unit->getTile()->addItem(rifle, groundRule);
	registerBattleItem(&battle, rifle);
	registerBattleItem(&battle, nested);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(unit, nested, Position(1, 1, 0), rightHand, 0, 0, &reject);
	expect(!ok, "nested ammo on ground pickup must fail");
	expect(nested->getSlot() == 0, "nested pickup reject: nested slot null");
	expect(rifle->getAmmoItem() == nested, "nested pickup reject: still in rifle");
}

static void testDropWrongBattleItemIdRejected(Mod &mod, RuleItem &clipRules)
{
	makeGroundInventory(mod, 10, 12);
	makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	RuleInventory *belt = makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 60, Position(2, 2, 0));
	BattleItem *carried = new BattleItem(&clipRules, battle.getCurrentItemId());
	carried->setSlot(belt);
	carried->setSlotX(0);
	carried->setSlotY(0);
	carried->moveToOwner(unit);
	registerBattleItem(&battle, carried);

	BattleItem *ghost = new BattleItem(&clipRules, battle.getCurrentItemId());
	registerBattleItem(&battle, ghost);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionDrop(unit, ghost, &reject);
	expect(!ok, "drop unrelated battle item id must fail");
}

static void testDropItemOwnedByOtherSoldierRejected(Mod &mod, RuleItem &clipRules)
{
	makeGroundInventory(mod, 10, 12);
	makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	RuleInventory *belt = makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *a = makeSoldier(&battle, mod, 61, Position(1, 1, 0));
	BattleUnit *b = makeSoldier(&battle, mod, 62, Position(2, 1, 0));
	BattleItem *clip = new BattleItem(&clipRules, battle.getCurrentItemId());
	clip->setSlot(belt);
	clip->setSlotX(0);
	clip->setSlotY(0);
	clip->moveToOwner(b);
	registerBattleItem(&battle, clip);
	const int tuBefore = a->getTimeUnits();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionDrop(a, clip, &reject);
	expect(!ok, "cannot drop item owned by another soldier");
	expect(clip->getOwner() == b, "wrong owner drop: clip still with B");
	expect(a->getTimeUnits() == tuBefore, "wrong owner drop: TU unchanged");
}

static void testDropAlreadyOnGroundRejected(Mod &mod, RuleItem &clipRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 63, Position(1, 1, 0));
	BattleItem *clip = placeGroundClip(&battle, &clipRules, groundRule, Position(1, 1, 0), 0, 0);
	const int tuBefore = unit->getTimeUnits();

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionDrop(unit, clip, &reject);
	expect(!ok, "drop ground-only item must fail");
	expect(clip->getOwner() == 0, "ground drop reject: still unowned");
	expect(unit->getTimeUnits() == tuBefore, "ground drop reject: TU unchanged");
}

static void testDropReservedTuRejected(Mod &mod, RuleItem &clipRules, RuleItem &rifleRules)
{
	makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	RuleInventory *belt = makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);
	battle.setTUReserved(BA_SNAPSHOT);
	battle.setKneelReserved(false);

	BattleUnit *unit = makeSoldier(&battle, mod, 64, Position(1, 1, 0));
	// Flat snap 30 + belt->ground 6 must exceed remaining TU.
	unit->setTimeUnits(35);
	BattleItem *rifle = new BattleItem(&rifleRules, battle.getCurrentItemId());
	BattleItem *mag = new BattleItem(&clipRules, battle.getCurrentItemId());
	expect(rifle->setAmmoItem(mag) == 0, "drop reserve: rifle accepts magazine");
	rifle->setSlot(rightHand);
	rifle->setSlotX(0);
	rifle->setSlotY(0);
	rifle->moveToOwner(unit);
	registerBattleItem(&battle, rifle);
	registerBattleItem(&battle, mag);

	BattleItem *clip = new BattleItem(&clipRules, battle.getCurrentItemId());
	clip->setSlot(belt);
	clip->setSlotX(0);
	clip->setSlotY(0);
	clip->moveToOwner(unit);
	registerBattleItem(&battle, clip);
	std::vector<int> tileIdsBefore;
	snapshotTileItemIds(unit->getTile(), &tileIdsBefore);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionDrop(unit, clip, &reject);
	expect(!ok, "reserved TU must block drop");
	expect(clip->getOwner() == unit, "reserved TU drop: still carried");
	expect(unit->getTimeUnits() == 35, "reserved TU drop: TU unchanged");
	std::vector<int> tileIdsAfter;
	snapshotTileItemIds(unit->getTile(), &tileIdsAfter);
	expect(vectorSameInts(tileIdsBefore, tileIdsAfter), "reserved TU drop: tile unchanged");
}

static void testDropNoGroundPlacementRejected(Mod &mod, RuleItem &clipRules)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	RuleInventory *belt = makeBeltInventory(mod, 6);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 65, Position(1, 1, 0));
	// One ground item larger than the STR_GROUND grid leaves no legal cell for a 1×1 drop.
	RuleItem floorBlocking("STR_PD_BLOCK");
	YAML::Node bn;
	bn["weight"] = 1;
	bn["battleType"] = static_cast<int>(BT_AMMO);
	bn["clipSize"] = 1;
	bn["invWidth"] = 200;
	bn["invHeight"] = 200;
	floorBlocking.load(bn, &mod, 0);
	BattleItem *blocker = new BattleItem(&floorBlocking, battle.getCurrentItemId());
	blocker->setSlotX(0);
	blocker->setSlotY(0);
	unit->getTile()->addItem(blocker, groundRule);
	registerBattleItem(&battle, blocker);

	BattleItem *clip = new BattleItem(&clipRules, battle.getCurrentItemId());
	clip->setSlot(belt);
	clip->setSlotX(0);
	clip->setSlotY(0);
	clip->moveToOwner(unit);
	registerBattleItem(&battle, clip);
	int destX = 0;
	int destY = 0;
	expect(!companionFindFirstGroundPlacement(unit, groundRule, clip, &destX, &destY), "fixture has no ground cell");

	const int tuBefore = unit->getTimeUnits();
	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionDrop(unit, clip, &reject);
	expect(!ok, "full ground must reject drop");
	expect(clip->getOwner() == unit, "full ground reject: still carried");
	expect(unit->getTimeUnits() == tuBefore, "full ground reject: TU unchanged");
}

static void testPrimedGrenadePickupPreservesFuse(Mod &mod)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	RuleItem nadeRules("STR_PD_NADE");
	YAML::Node n;
	n["weight"] = 1;
	n["battleType"] = static_cast<int>(BT_GRENADE);
	n["invWidth"] = 1;
	n["invHeight"] = 1;
	nadeRules.load(n, &mod, 0);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 11, Position(1, 1, 0));
	BattleItem *grenade = new BattleItem(&nadeRules, battle.getCurrentItemId());
	grenade->setFuseTimer(3);
	grenade->setSlot(groundRule);
	grenade->setSlotX(0);
	grenade->setSlotY(0);
	unit->getTile()->addItem(grenade, groundRule);
	registerBattleItem(&battle, grenade);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(
		unit,
		grenade,
		Position(1, 1, 0),
		rightHand,
		0,
		0,
		&reject);
	expect(ok, reject.c_str());
	expect(grenade->getFuseTimer() == 3, "primed grenade fuse preserved after pickup");
}

static void testProximityPrimedPickupPreservesFuse(Mod &mod)
{
	RuleInventory *groundRule = makeGroundInventory(mod, 10, 12);
	RuleInventory *rightHand = makeHandInventory(mod, "STR_RIGHT_HAND", 8);
	(void)makeBeltInventory(mod, 6);

	RuleItem proxRules("STR_PD_PROX");
	YAML::Node n;
	n["weight"] = 1;
	n["battleType"] = static_cast<int>(BT_PROXIMITYGRENADE);
	n["invWidth"] = 1;
	n["invHeight"] = 1;
	proxRules.load(n, &mod, 0);

	SavedBattleGame battle;
	battle.setTestMod(&mod);
	battle.initMap(4, 4, 1);
	battle.initUtilities(&mod);

	BattleUnit *unit = makeSoldier(&battle, mod, 12, Position(1, 2, 0));
	BattleItem *prox = new BattleItem(&proxRules, battle.getCurrentItemId());
	prox->setFuseTimer(0);
	prox->setSlot(groundRule);
	prox->setSlotX(0);
	prox->setSlotY(0);
	unit->getTile()->addItem(prox, groundRule);
	registerBattleItem(&battle, prox);

	BattlescapeGame game(&battle, 0);
	std::string reject;
	const bool ok = game.tryExecuteCompanionPickup(
		unit,
		prox,
		Position(1, 2, 0),
		rightHand,
		0,
		0,
		&reject);
	expect(ok, reject.c_str());
	expect(prox->getFuseTimer() == 0, "proximity armed fuse 0 preserved");
}

int main()
{
	Mod mod;
	RuleItem clipRules = makeStandardClipRules(mod);

	RuleItem rifleRules("STR_PD_RIFLE");
	YAML::Node rifleNode;
	rifleNode["weight"] = 5;
	rifleNode["battleType"] = static_cast<int>(BT_FIREARM);
	rifleNode["clipSize"] = 20;
	rifleNode["compatibleAmmo"] = YAML::Load("[STR_PD_CLIP]");
	rifleNode["invWidth"] = 1;
	rifleNode["invHeight"] = 3;
	rifleNode["accuracySnap"] = 50;
	rifleNode["tuSnap"] = 30;
	rifleNode["flatRate"] = true;
	rifleRules.load(rifleNode, &mod, 0);

	testPickupGroundToRightHandSuccess(mod, clipRules);
	testDropBeltToGroundSuccess(mod, clipRules);
	testDropCarriedUnconsciousBodyToGround(mod);
	testPickupWrongGroundPositionRejected(mod, clipRules);
	testPickupItemOnOtherTileRejected(mod, clipRules);
	testPickupInsufficientTuNoMutation(mod, clipRules);
	testPickupReservedTuRejected(mod, clipRules, rifleRules);
	testPickupOccupiedRightHandRejected(mod, clipRules);
	testPickupInvalidBeltCoordsRejected(mod, clipRules);
	testDropInsufficientTuNoMutation(mod, clipRules);
	testDropNestedAmmoRejected(mod, clipRules, rifleRules);
	testPickupWrongBattleItemIdRejected(mod, clipRules);
	testPickupGroundItemWithSoldierOwnerRejected(mod, clipRules);
	testPickupNestedMagazineRejected(mod, clipRules, rifleRules);
	testDropWrongBattleItemIdRejected(mod, clipRules);
	testDropItemOwnedByOtherSoldierRejected(mod, clipRules);
	testDropAlreadyOnGroundRejected(mod, clipRules);
	testDropReservedTuRejected(mod, clipRules, rifleRules);
	testDropNoGroundPlacementRejected(mod, clipRules);
	testPrimedGrenadePickupPreservesFuse(mod);
	testProximityPrimedPickupPreservesFuse(mod);

	if (g_failures != 0)
	{
		std::cerr << g_failures << " failure(s)." << std::endl;
		return 1;
	}
	std::cout << "companion_pickup_drop_engine_test: all tests passed." << std::endl;
	return 0;
}
