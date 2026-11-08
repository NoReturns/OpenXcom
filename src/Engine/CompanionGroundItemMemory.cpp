/*
 * Open X-Com Companion — session ground-item memory (M1-WP9).
 */
#include "CompanionGroundItemMemory.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Tile.h"
#include "../Savegame/BattleItem.h"
#include "../Mod/RuleItem.h"
#include <algorithm>

namespace OpenXcom
{

namespace
{

using namespace CompanionGroundItemObservationCore;

bool isItemResearched(SavedGame *campaign, RuleItem *rules)
{
	if (campaign == 0 || rules == 0)
	{
		return true;
	}
	return campaign->isResearched(rules->getRequirements());
}

GroundItemRulesView viewFromBattleItem(BattleItem *item, SavedGame *campaign)
{
	GroundItemRulesView view;
	if (item == 0 || item->getRules() == 0)
	{
		return view;
	}
	RuleItem *rules = item->getRules();
	view.itemType = rules->getType();
	view.battleType = static_cast<int>(rules->getBattleType());
	view.weight = rules->getWeight();
	view.ammoQuantity = item->getAmmoQuantity();
	view.researched = isItemResearched(campaign, rules);
	const BattleType bt = rules->getBattleType();
	view.primedGrenade = (bt == BT_GRENADE || bt == BT_PROXIMITYGRENADE) && item->getFuseTimer() >= 0;
	return view;
}

void collectTileItemViews(Tile *tile, SavedGame *campaign, std::vector<GroundItemRulesView> &views)
{
	if (tile == 0)
	{
		return;
	}
	for (std::vector<BattleItem*>::const_iterator i = tile->getInventory()->begin();
	     i != tile->getInventory()->end(); ++i)
	{
		BattleItem *item = *i;
		if (item == 0 || item->getTile() != tile)
		{
			continue;
		}
		views.push_back(viewFromBattleItem(item, campaign));
	}
}

GroundTileObservationInput inputFromTile(Tile *tile, SavedGame *campaign)
{
	GroundTileObservationInput input;
	input.tileVoid = tile == 0 || tile->isVoid();
	input.bodyDiscovered = tile != 0 && tile->isDiscovered(2);
	input.inLineOfSight = tile != 0 && tile->getVisible() > 0;
	input.hasGroundItems = false;
	if (tile != 0 && !tile->isVoid())
	{
		collectTileItemViews(tile, campaign, input.items);
		input.hasGroundItems = !input.items.empty();
	}
	return input;
}

bool comparePosition(const Position &a, const Position &b)
{
	if (a.z != b.z)
	{
		return a.z < b.z;
	}
	if (a.y != b.y)
	{
		return a.y < b.y;
	}
	return a.x < b.x;
}

} // namespace

void CompanionGroundItemMemory::beginSession(const std::string &sessionId)
{
	_sessionId = sessionId;
	_remembered.clear();
	_droppedCount = 0;
}

void CompanionGroundItemMemory::syncSessionId(const std::string &sessionId)
{
	if (_sessionId != sessionId)
	{
		beginSession(sessionId);
	}
}

void CompanionGroundItemMemory::refreshMemoryFromBattle(SavedBattleGame *battle, SavedGame *campaign)
{
	if (battle == 0)
	{
		return;
	}
	const int mapW = battle->getMapSizeX();
	const int mapH = battle->getMapSizeY();
	const int mapD = battle->getMapSizeZ();
	for (int z = 0; z < mapD; ++z)
	{
		for (int y = 0; y < mapH; ++y)
		{
			for (int x = 0; x < mapW; ++x)
			{
				Tile *tile = battle->getTile(Position(x, y, z));
				if (tile == 0 || tile->isVoid() || !tile->isDiscovered(2))
				{
					continue;
				}
				const std::string key = tileMemoryKey(x, y, z);
				if (tile->getVisible() > 0)
				{
					GroundTileObservationInput input = inputFromTile(tile, campaign);
					if (!input.hasGroundItems)
					{
						_remembered.erase(key);
						continue;
					}
					GroundTileObservationPlan plan = planTileObservation(input);
					if (!plan.exportTile || plan.items.empty())
					{
						continue;
					}
					RememberedTile remembered;
					remembered.items = plan.items;
					_remembered[key] = remembered;
				}
			}
		}
	}
}

void CompanionGroundItemMemory::collectExportObservations(
	SavedBattleGame *battle,
	SavedGame *campaign,
	std::vector<ExportObservation> &out)
{
	out.clear();
	if (battle == 0)
	{
		return;
	}
	refreshMemoryFromBattle(battle, campaign);

	const int mapW = battle->getMapSizeX();
	const int mapH = battle->getMapSizeY();
	const int mapD = battle->getMapSizeZ();
	for (int z = 0; z < mapD; ++z)
	{
		for (int y = 0; y < mapH; ++y)
		{
			for (int x = 0; x < mapW; ++x)
			{
				Tile *tile = battle->getTile(Position(x, y, z));
				if (tile == 0 || tile->isVoid() || !tile->isDiscovered(2))
				{
					continue;
				}
				const std::string key = tileMemoryKey(x, y, z);
				const bool inLos = tile->getVisible() > 0;
				if (inLos)
				{
					GroundTileObservationInput input = inputFromTile(tile, campaign);
					if (!input.hasGroundItems)
					{
						continue;
					}
					GroundTileObservationPlan plan = planTileObservation(input);
					if (!plan.exportTile)
					{
						continue;
					}
					ExportObservation obs;
					obs.position = Position(x, y, z);
					obs.observationStatus = OBS_CONFIRMED_IN_LOS;
					obs.items = plan.items;
					out.push_back(obs);
				}
				else
				{
					std::map<std::string, RememberedTile>::const_iterator remembered = _remembered.find(key);
					if (remembered == _remembered.end() || remembered->second.items.empty())
					{
						continue;
					}
					ExportObservation obs;
					obs.position = Position(x, y, z);
					obs.observationStatus = OBS_REMEMBERED_OFF_LOS;
					obs.items = remembered->second.items;
					out.push_back(obs);
				}
			}
		}
	}

	std::sort(out.begin(), out.end(), [](const ExportObservation &a, const ExportObservation &b) {
		return comparePosition(a.position, b.position);
	});

	if (out.size() > MAX_EXPORTED_OBSERVATIONS)
	{
		_droppedCount += static_cast<int>(out.size() - MAX_EXPORTED_OBSERVATIONS);
		out.resize(MAX_EXPORTED_OBSERVATIONS);
	}
}

} // namespace OpenXcom
