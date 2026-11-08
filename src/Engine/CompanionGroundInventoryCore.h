#pragma once
/*
 * Open X-Com Companion — ground inventory helpers shared by snapshot export and
 * companion ground PICKUP/DROP (WP8B4).
 */
namespace OpenXcom
{

class BattleItem;
class BattleUnit;
class RuleInventory;

/// Recompute STR_GROUND grid coordinates on the unit's tile (Inventory::arrangeGround semantics).
void companionArrangeGroundSlotCoordsForUnit(BattleUnit *unit, RuleInventory *groundRule);

/**
 * Find first legal ground cell for dropping @a item (engine placement, not a relay coordinate).
 * Calls companionArrangeGroundSlotCoordsForUnit first.
 */
bool companionFindFirstGroundPlacement(
	BattleUnit *unit,
	RuleInventory *groundRule,
	BattleItem *item,
	int *outX,
	int *outY);

} // namespace OpenXcom
