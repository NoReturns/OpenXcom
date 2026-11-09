/*
 * Open X-Com Companion — ground-item observation core (M1-WP9).
 */
#include "CompanionGroundItemObservationCore.h"
#include "../Mod/RuleItem.h"
#include <algorithm>
#include <sstream>

namespace OpenXcom
{

namespace CompanionGroundItemObservationCore
{

namespace
{

int primaryItemIndex(const std::vector<GroundItemRulesView> &items)
{
	if (items.empty())
	{
		return -1;
	}
	int bestIndex = 0;
	int bestWeight = items[0].weight;
	for (size_t i = 1; i < items.size(); ++i)
	{
		if (items[i].weight > bestWeight)
		{
			bestWeight = items[i].weight;
			bestIndex = static_cast<int>(i);
		}
	}
	return bestIndex;
}

GroundItemObservationItem buildItemObservation(const GroundItemRulesView &view, bool primaryOnTile)
{
	GroundItemObservationItem out;
	out.battleType = view.battleType;
	out.concealedOnTile = !primaryOnTile;
	out.hasItemType = false;
	out.hasQuantity = false;
	out.hasPrimed = false;
	out.hasResearchUsable = false;
	out.researchUsable = false;
	if (primaryOnTile)
	{
		out.itemCategory = view.researched
			? itemCategoryForBattleType(view.battleType)
			: itemCategoryForUnresearchedBattleType(view.battleType);
	}
	else
	{
		out.itemCategory = "additional_ground_item";
	}
	if (primaryOnTile && !view.itemType.empty() && isItemTypeExportAllowed(view.researched, view.battleType))
	{
		out.hasItemType = true;
		out.itemType = view.itemType;
	}
	if (primaryOnTile && view.battleType == BT_AMMO && view.ammoQuantity > 0)
	{
		out.hasQuantity = true;
		out.quantity = view.ammoQuantity;
	}
	if (view.battleType == BT_GRENADE || view.battleType == BT_PROXIMITYGRENADE)
	{
		if (view.primedGrenade)
		{
			out.hasPrimed = true;
			out.primed = true;
		}
	}
	if (primaryOnTile)
	{
		out.hasResearchUsable = true;
		out.researchUsable = view.commandResearchUsable;
	}
	return out;
}

} // namespace

const char *itemCategoryForBattleType(int battleType)
{
	switch (static_cast<BattleType>(battleType))
	{
	case BT_FIREARM: return "firearm";
	case BT_AMMO: return "ammunition";
	case BT_MELEE: return "melee";
	case BT_GRENADE: return "grenade";
	case BT_PROXIMITYGRENADE: return "proximity_grenade";
	case BT_MEDIKIT: return "medikit";
	case BT_SCANNER: return "scanner";
	case BT_MINDPROBE: return "mind_probe";
	case BT_PSIAMP: return "psi_amp";
	case BT_FLARE: return "flare";
	case BT_CORPSE: return "corpse";
	default: return "unknown";
	}
}

const char *itemCategoryForUnresearchedBattleType(int battleType)
{
	switch (static_cast<BattleType>(battleType))
	{
	case BT_FIREARM: return "unidentified_firearm";
	case BT_AMMO: return "unidentified_ammunition";
	case BT_MELEE: return "unidentified_melee";
	case BT_GRENADE: return "unidentified_grenade";
	case BT_PROXIMITYGRENADE: return "unidentified_proximity_grenade";
	case BT_MEDIKIT: return "unidentified_medikit";
	case BT_SCANNER: return "unidentified_scanner";
	case BT_MINDPROBE: return "unidentified_mind_probe";
	case BT_PSIAMP: return "unidentified_psi_amp";
	case BT_FLARE: return "unidentified_flare";
	case BT_CORPSE: return "unidentified_corpse";
	default: return "unidentified_equipment";
	}
}

bool isItemTypeExportAllowed(bool researched, int battleType)
{
	if (researched)
	{
		return true;
	}
	(void)battleType;
	return false;
}

GroundTileObservationPlan planTileObservation(const GroundTileObservationInput &input)
{
	GroundTileObservationPlan plan;
	plan.exportTile = false;
	plan.observationStatus = 0;
	if (input.tileVoid || !input.bodyDiscovered)
	{
		return plan;
	}
	if (!input.hasGroundItems)
	{
		return plan;
	}
	if (!input.inLineOfSight)
	{
		return plan;
	}

	plan.exportTile = true;
	plan.observationStatus = OBS_CONFIRMED_IN_LOS;
	const int primary = primaryItemIndex(input.items);
	for (size_t i = 0; i < input.items.size() && plan.items.size() < MAX_ITEMS_PER_TILE; ++i)
	{
		const bool primaryOnTile = static_cast<int>(i) == primary;
		GroundItemObservationItem item = buildItemObservation(input.items[i], primaryOnTile);
		plan.items.push_back(item);
	}
	return plan;
}

std::string tileMemoryKey(int x, int y, int z)
{
	std::ostringstream key;
	key << x << ',' << y << ',' << z;
	return key.str();
}

} // namespace CompanionGroundItemObservationCore

} // namespace OpenXcom
