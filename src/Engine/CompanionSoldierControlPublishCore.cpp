/*
 * Open X-Com Companion — publish companion-soldier-control.json from campaign (M1-23F3).
 */
#include "CompanionSoldierControlPublishCore.h"
#include "CompanionSnapshotExportCore.h"
#include "CompanionCommandCore.h"
#include "CompanionCampaignOwnershipCore.h"
#include "CompanionCampaignIdentityCore.h"
#include "CompanionSoldierOwnershipCore.h"
#include <sstream>

namespace OpenXcom
{

namespace CompanionSoldierControlPublishCore
{

ControlPublicationPlan buildControlPublicationJson(
	const std::string &sessionId,
	const std::string &campaignId,
	const std::string &tacticalBattleId,
	const std::set<int> &tacticalAiSoldierIds,
	const std::vector<int> &publishableLivePlayerUnitIds)
{
	ControlPublicationPlan plan = {};
	plan.ok = false;
	if (!CompanionCommandCore::isValidSessionIdFormat(sessionId))
	{
		plan.publicReason = "invalid session_id";
		return plan;
	}
	if (!CompanionCampaignIdentityCore::isValidCampaignIdFormat(campaignId))
	{
		plan.publicReason = "invalid campaign_id";
		return plan;
	}
	if (!CompanionCampaignIdentityCore::isValidCampaignIdFormat(tacticalBattleId))
	{
		plan.publicReason = "invalid tactical_battle_id";
		return plan;
	}

	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"session_id\": \"" << sessionId << "\",\n"
	     << "  \"campaign_id\": \"" << campaignId << "\",\n"
	     << "  \"tactical_battle_id\": \"" << tacticalBattleId << "\",\n"
	     << "  \"assignments\": {\n";

	bool firstAssignment = true;
	for (std::vector<int>::const_iterator it = publishableLivePlayerUnitIds.begin();
	     it != publishableLivePlayerUnitIds.end(); ++it)
	{
		const CompanionSoldierOwnershipCore::SoldierControlMode mode =
			CompanionCampaignOwnershipCore::resolveCampaignSoldierControlMode(
				tacticalAiSoldierIds, *it, true);
		if (mode != CompanionSoldierOwnershipCore::CONTROL_AI)
		{
			continue;
		}
		if (!firstAssignment)
		{
			json << ",\n";
		}
		json << "    \"" << *it << "\": \"ai\"";
		firstAssignment = false;
	}

	json << "\n  }\n"
	     << "}\n";
	plan.ok = true;
	plan.sessionId = sessionId;
	plan.jsonUtf8 = json.str();
	return plan;
}

bool writeControlFileAtomically(const std::string &destPath, const std::string &jsonUtf8)
{
	return CompanionSnapshotExportCore::writeUtf8FileAtomically(destPath, jsonUtf8);
}

} // namespace CompanionSoldierControlPublishCore

} // namespace OpenXcom
