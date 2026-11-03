#pragma once
/*
 * Open X-Com Companion — durable campaign identity (M1-23B).
 */
#include <string>
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

namespace CompanionCampaignIdentityCore
{

enum CampaignIdLoadOutcome
{
	CAMPAIGN_ID_LOADED_FROM_SAVE,
	CAMPAIGN_ID_LEGACY_GENERATED
};

std::string makeCampaignId();
bool isValidCampaignIdFormat(const std::string &campaignId);
CampaignIdLoadOutcome loadCompanionCampaignId(const YAML::Node &saveDocument, std::string *campaignIdOut);
void writeCompanionCampaignId(YAML::Node *saveDocumentNode, const std::string &campaignId);

} // namespace CompanionCampaignIdentityCore

} // namespace OpenXcom
