/*
 * Open X-Com Companion — tactical battle soldier ownership (M1-25B).
 */
#include "CompanionTacticalOwnershipCore.h"
#include "CompanionCampaignOwnershipCore.h"
#include "CompanionCampaignIdentityCore.h"

namespace OpenXcom
{

namespace CompanionTacticalOwnershipCore
{

void seedTacticalAiFromCampaign(const std::set<int> &campaignAiSoldierIds, std::set<int> *tacticalAiSoldierIdsOut)
{
	if (tacticalAiSoldierIdsOut == 0)
	{
		return;
	}
	tacticalAiSoldierIdsOut->clear();
	for (std::set<int>::const_iterator it = campaignAiSoldierIds.begin(); it != campaignAiSoldierIds.end(); ++it)
	{
		if (*it > 0)
		{
			tacticalAiSoldierIdsOut->insert(*it);
		}
	}
}

void loadFromBattleSaveNode(
	const YAML::Node &battleNode,
	std::string *tacticalBattleIdOut,
	std::set<int> *tacticalAiSoldierIdsOut)
{
	if (tacticalBattleIdOut != 0)
	{
		tacticalBattleIdOut->clear();
	}
	if (tacticalAiSoldierIdsOut != 0)
	{
		tacticalAiSoldierIdsOut->clear();
	}
	if (!battleNode["companion"])
	{
		return;
	}
	const YAML::Node companion = battleNode["companion"];
	if (companion["tactical_battle_id"] && companion["tactical_battle_id"].IsScalar() && tacticalBattleIdOut != 0)
	{
		const std::string id = companion["tactical_battle_id"].as<std::string>();
		if (CompanionCampaignIdentityCore::isValidCampaignIdFormat(id))
		{
			*tacticalBattleIdOut = id;
		}
	}
	if (tacticalAiSoldierIdsOut != 0 && companion["assignments"])
	{
		const YAML::Node assignments = companion["assignments"];
		if (assignments.IsMap())
		{
			for (YAML::const_iterator it = assignments.begin(); it != assignments.end(); ++it)
			{
				int soldierId = 0;
				if (!CompanionCampaignOwnershipCore::parseSoldierIdKey(it->first.as<std::string>(), &soldierId))
				{
					continue;
				}
				if (!it->second.IsScalar())
				{
					continue;
				}
				const std::string mode = it->second.as<std::string>();
				if (mode == "ai")
				{
					tacticalAiSoldierIdsOut->insert(soldierId);
				}
			}
		}
	}
}

void writeToBattleSaveNode(
	YAML::Node *battleNode,
	const std::string &tacticalBattleId,
	const std::set<int> &tacticalAiSoldierIds)
{
	if (battleNode == 0 || tacticalBattleId.empty())
	{
		return;
	}
	YAML::Node companion = (*battleNode)["companion"];
	if (!companion || !companion.IsMap())
	{
		companion = YAML::Node(YAML::NodeType::Map);
	}
	companion["tactical_battle_id"] = tacticalBattleId;
	if (tacticalAiSoldierIds.empty())
	{
		if (companion["assignments"])
		{
			companion.remove("assignments");
		}
	}
	else
	{
		YAML::Node assignments(YAML::NodeType::Map);
		for (std::set<int>::const_iterator it = tacticalAiSoldierIds.begin(); it != tacticalAiSoldierIds.end(); ++it)
		{
			assignments[std::to_string(*it)] = "ai";
		}
		companion["assignments"] = assignments;
	}
	(*battleNode)["companion"] = companion;
}

} // namespace CompanionTacticalOwnershipCore

} // namespace OpenXcom
