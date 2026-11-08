#pragma once
/*
 * Open X-Com Companion — player-legitimate ground-item observation (M1-WP9, testable core).
 */
#include "../Battlescape/Position.h"
#include <string>
#include <vector>

namespace OpenXcom
{

namespace CompanionGroundItemObservationCore
{

/** Bounded per tactical session (WP8C); evict oldest positions first when exceeded. */
static const size_t MAX_EXPORTED_OBSERVATIONS = 64;
static const size_t MAX_ITEMS_PER_TILE = 16;

static const char *OBS_CONFIRMED_IN_LOS = "CONFIRMED_IN_LOS";
static const char *OBS_REMEMBERED_OFF_LOS = "REMEMBERED_OFF_LOS";

struct GroundItemRulesView
{
	std::string itemType;
	int battleType;
	int weight;
	int ammoQuantity;
	bool researched;
	bool primedGrenade;
};

struct GroundItemObservationItem
{
	std::string itemCategory;
	int battleType;
	bool hasItemType;
	std::string itemType;
	bool hasQuantity;
	int quantity;
	bool hasPrimed;
	bool primed;
	bool concealedOnTile;
};

struct GroundTileObservationInput
{
	bool tileVoid;
	bool bodyDiscovered;
	bool inLineOfSight;
	bool hasGroundItems;
	std::vector<GroundItemRulesView> items;
};

struct GroundTileObservationPlan
{
	bool exportTile;
	const char *observationStatus;
	std::vector<GroundItemObservationItem> items;
};

const char *itemCategoryForBattleType(int battleType);
const char *itemCategoryForUnresearchedBattleType(int battleType);

bool isItemTypeExportAllowed(bool researched, int battleType);

GroundTileObservationPlan planTileObservation(const GroundTileObservationInput &input);

std::string tileMemoryKey(int x, int y, int z);

} // namespace CompanionGroundItemObservationCore

} // namespace OpenXcom
