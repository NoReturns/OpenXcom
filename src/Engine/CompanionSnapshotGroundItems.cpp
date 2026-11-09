/*
 * Open X-Com Companion — ground-item snapshot export (M1-WP9).
 */
#include "CompanionSnapshotGroundItems.h"
#include "CompanionGroundItemMemory.h"
#include "CompanionGroundItemObservationCore.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

namespace CompanionSnapshotGroundItems
{

const char *MEMORY_NOTE()
{
	return "Off-LOS ground items replay last confirmed in-LOS observations; hidden tile inventories are never read.";
}

namespace
{

using namespace CompanionGroundItemObservationCore;

void appendItemNode(YAML::Node &items, const GroundItemObservationItem &item)
{
	YAML::Node entry;
	entry["itemCategory"] = item.itemCategory;
	entry["battleType"] = item.battleType;
	if (item.hasItemType)
	{
		entry["itemType"] = item.itemType;
	}
	if (item.hasQuantity)
	{
		entry["quantity"] = item.quantity;
	}
	if (item.hasPrimed)
	{
		entry["primed"] = item.primed;
	}
	if (item.concealedOnTile)
	{
		entry["concealedOnTile"] = true;
	}
	if (item.hasResearchUsable)
	{
		entry["researchUsable"] = item.researchUsable;
	}
	items.push_back(entry);
}

} // namespace

void appendGroundItems(YAML::Node &root, SavedBattleGame *battle, SavedGame *campaign)
{
	if (battle == 0)
	{
		return;
	}
	const std::string &eventSession = battle->getCompanionEventLog().getSessionId();
	if (!eventSession.empty())
	{
		battle->getCompanionGroundItemMemory().syncSessionId(eventSession);
	}
	std::vector<CompanionGroundItemMemory::ExportObservation> observations;
	battle->getCompanionGroundItemMemory().collectExportObservations(battle, campaign, observations);

	YAML::Node groundItems;
	groundItems["schemaVersion"] = 1;
	groundItems["sessionId"] = battle->getCompanionGroundItemMemory().getSessionId();
	groundItems["memoryNote"] = MEMORY_NOTE();
	groundItems["droppedCount"] = battle->getCompanionGroundItemMemory().getDroppedCount();

	YAML::Node obsSeq(YAML::NodeType::Sequence);
	for (std::vector<CompanionGroundItemMemory::ExportObservation>::const_iterator i = observations.begin();
	     i != observations.end(); ++i)
	{
		YAML::Node obs;
		YAML::Node pos;
		pos["x"] = i->position.x;
		pos["y"] = i->position.y;
		pos["z"] = i->position.z;
		obs["position"] = pos;
		obs["observationStatus"] = i->observationStatus;
		YAML::Node items(YAML::NodeType::Sequence);
		for (std::vector<GroundItemObservationItem>::const_iterator j = i->items.begin();
		     j != i->items.end(); ++j)
		{
			appendItemNode(items, *j);
		}
		obs["items"] = items;
		obsSeq.push_back(obs);
	}
	groundItems["observations"] = obsSeq;
	root["groundItems"] = groundItems;
}

} // namespace CompanionSnapshotGroundItems

} // namespace OpenXcom
