/*
 * Open X-Com Companion — campaign-save soldier ownership (M1-23C).
 */
#include "CompanionCampaignOwnershipCore.h"
#include <cstdlib>

namespace OpenXcom
{

namespace CompanionCampaignOwnershipCore
{

bool isValidOwnershipModeString(const std::string &mode)
{
	return mode == "ai" || mode == "human";
}

bool parseSoldierIdKey(const std::string &key, int *soldierIdOut)
{
	if (soldierIdOut == 0 || key.empty())
	{
		return false;
	}
	char *endPtr = 0;
	const long parsed = std::strtol(key.c_str(), &endPtr, 10);
	if (endPtr == key.c_str() || *endPtr != '\0' || parsed <= 0)
	{
		return false;
	}
	*soldierIdOut = static_cast<int>(parsed);
	return true;
}

void loadCompanionAssignments(const YAML::Node &saveDocument, std::set<int> *aiSoldierIdsOut)
{
	if (aiSoldierIdsOut == 0)
	{
		return;
	}
	aiSoldierIdsOut->clear();
	if (!saveDocument["companion"] || !saveDocument["companion"]["assignments"])
	{
		return;
	}
	const YAML::Node assignments = saveDocument["companion"]["assignments"];
	if (!assignments.IsMap())
	{
		return;
	}
	for (YAML::const_iterator it = assignments.begin(); it != assignments.end(); ++it)
	{
		const std::string key = it->first.as<std::string>();
		int soldierId = 0;
		if (!parseSoldierIdKey(key, &soldierId))
		{
			continue;
		}
		if (!it->second.IsScalar())
		{
			continue;
		}
		const std::string mode = it->second.as<std::string>();
		if (!isValidOwnershipModeString(mode))
		{
			continue;
		}
		if (mode == "ai")
		{
			aiSoldierIdsOut->insert(soldierId);
		}
	}
}

void writeCompanionAssignments(YAML::Node *saveDocumentNode, const std::set<int> &aiSoldierIds)
{
	if (saveDocumentNode == 0)
	{
		return;
	}
	YAML::Node companion = (*saveDocumentNode)["companion"];
	if (!companion || !companion.IsMap())
	{
		if (aiSoldierIds.empty())
		{
			return;
		}
		companion = YAML::Node(YAML::NodeType::Map);
	}
	if (aiSoldierIds.empty())
	{
		if (companion["assignments"])
		{
			companion.remove("assignments");
		}
		(*saveDocumentNode)["companion"] = companion;
		return;
	}
	YAML::Node assignments(YAML::NodeType::Map);
	for (std::set<int>::const_iterator it = aiSoldierIds.begin(); it != aiSoldierIds.end(); ++it)
	{
		assignments[std::to_string(*it)] = "ai";
	}
	companion["assignments"] = assignments;
	(*saveDocumentNode)["companion"] = companion;
}

bool companionAiAssignmentSetsEqual(const std::set<int> &currentAiIds, const std::set<int> &persistedAiIds)
{
	return currentAiIds == persistedAiIds;
}

bool hasUnsavedCompanionSoldierAssignments(const std::set<int> &currentAiIds, const std::set<int> &persistedAiIds)
{
	return !companionAiAssignmentSetsEqual(currentAiIds, persistedAiIds);
}

bool isCompanionSoldierAssignmentUnsaved(const std::set<int> &currentAiIds, const std::set<int> &persistedAiIds, int soldierId)
{
	if (soldierId <= 0)
	{
		return false;
	}
	const bool currentAi = currentAiIds.find(soldierId) != currentAiIds.end();
	const bool persistedAi = persistedAiIds.find(soldierId) != persistedAiIds.end();
	return currentAi != persistedAi;
}

CompanionSoldierOwnershipCore::SoldierControlMode resolveCampaignSoldierControlMode(
	const std::set<int> &currentCampaignAiSoldierIds,
	int soldierId,
	bool isLivePlayerSoldier)
{
	if (soldierId <= 0 || !isLivePlayerSoldier)
	{
		return CompanionSoldierOwnershipCore::CONTROL_HUMAN;
	}
	if (currentCampaignAiSoldierIds.find(soldierId) == currentCampaignAiSoldierIds.end())
	{
		return CompanionSoldierOwnershipCore::CONTROL_HUMAN;
	}
	return CompanionSoldierOwnershipCore::CONTROL_AI;
}

} // namespace CompanionCampaignOwnershipCore

} // namespace OpenXcom
