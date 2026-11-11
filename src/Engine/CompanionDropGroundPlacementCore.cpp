/*
 * Open X-Com Companion — DROP ground placement evidence (M1 P1-C).
 */
#include "CompanionDropGroundPlacementCore.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Tile.h"
#include "../Mod/RuleInventory.h"
#include "../Battlescape/Position.h"
#include <sstream>

namespace OpenXcom
{

namespace CompanionDropGroundPlacementCore
{

static void initEvidence(GroundPlacementEvidence *out)
{
	out->present = false;
	out->verified = false;
	out->itemBattleItemId = 0;
	out->tileX = 0;
	out->tileY = 0;
	out->tileZ = 0;
	out->inventoryX = 0;
	out->inventoryY = 0;
	out->groundSlotId.clear();
	out->verificationStatus = "engine_postcondition_unverified";
	out->evidenceClass = "engine_immediate";
}

bool captureAfterSuccessfulDrop(BattleItem *item, BattleUnit *actor, int expectedItemId, GroundPlacementEvidence *out)
{
	if (out == 0)
	{
		return false;
	}
	initEvidence(out);
	if (item == 0 || actor == 0 || expectedItemId < 0)
	{
		return true;
	}

	out->present = true;
	out->itemBattleItemId = item->getId();
	out->inventoryX = item->getSlotX();
	out->inventoryY = item->getSlotY();

	RuleInventory *slot = item->getSlot();
	if (slot != 0)
	{
		out->groundSlotId = slot->getId();
	}

	Tile *itemTile = item->getTile();
	Tile *actorTile = actor->getTile();
	if (itemTile != 0)
	{
		const Position p = itemTile->getPosition();
		out->tileX = p.x;
		out->tileY = p.y;
		out->tileZ = p.z;
	}

	bool verified = true;
	if (expectedItemId >= 0 && item->getId() != expectedItemId)
	{
		verified = false;
	}
	if (item->getOwner() != 0)
	{
		verified = false;
	}
	if (slot == 0 || slot->getType() != INV_GROUND)
	{
		verified = false;
	}
	if (itemTile == 0 || actorTile == 0 || itemTile != actorTile)
	{
		verified = false;
	}

	out->verified = verified;
	out->verificationStatus = verified ? "engine_postcondition_verified" : "engine_postcondition_unverified";
	return true;
}

void appendGroundPlacementJson(std::ostringstream &json, const GroundPlacementEvidence &evidence)
{
	if (!evidence.present)
	{
		return;
	}
	json << "  \"ground_placement\": {\n"
	     << "    \"item_battle_item_id\": " << evidence.itemBattleItemId << ",\n"
	     << "    \"tile\": { \"x\": " << evidence.tileX
	     << ", \"y\": " << evidence.tileY
	     << ", \"z\": " << evidence.tileZ << " },\n"
	     << "    \"inventory_x\": " << evidence.inventoryX << ",\n"
	     << "    \"inventory_y\": " << evidence.inventoryY << ",\n";
	if (!evidence.groundSlotId.empty())
	{
		json << "    \"ground_slot\": \"" << evidence.groundSlotId << "\",\n";
	}
	json << "    \"verification_status\": \"" << evidence.verificationStatus << "\",\n"
	     << "    \"evidence_class\": \"" << evidence.evidenceClass << "\",\n"
	     << "    \"independent_snapshot_confirmed\": false\n"
	     << "  },\n";
}

} // namespace CompanionDropGroundPlacementCore

} // namespace OpenXcom
