/*
 * Open X-Com Companion — durable campaign identity (M1-23B).
 */
#include "CompanionCampaignIdentityCore.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace OpenXcom
{

namespace
{

uint32_t mixCampaignWord(uint32_t a, uint32_t b)
{
	uint64_t x = (static_cast<uint64_t>(a) << 32) ^ static_cast<uint64_t>(b);
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdULL;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ULL;
	x ^= x >> 33;
	return static_cast<uint32_t>(x ^ (x >> 32));
}

std::string formatCampaignId(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
	char buf[33];
	std::sprintf(buf, "%08x%08x%08x%08x", w0, w1, w2, w3);
	return std::string(buf);
}

} // namespace

namespace CompanionCampaignIdentityCore
{

std::string makeCampaignId()
{
	static std::atomic<uint32_t> campaignNonce(0);
	const uint32_t nonce = ++campaignNonce;
	const uint64_t ticks = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	const uint32_t w0 = mixCampaignWord(0x434d504eU ^ nonce, static_cast<uint32_t>(ticks));
	const uint32_t w1 = mixCampaignWord(static_cast<uint32_t>(ticks >> 32), static_cast<uint32_t>(ticks) ^ nonce);
	const uint32_t w2 = mixCampaignWord(w0, w1 ^ 0x43414947U);
	const uint32_t w3 = mixCampaignWord(w2, nonce ^ static_cast<uint32_t>(ticks >> 16));
	return formatCampaignId(w0, w1, w2, w3);
}

bool isValidCampaignIdFormat(const std::string &campaignId)
{
	if (campaignId.size() != 32)
	{
		return false;
	}
	for (size_t i = 0; i < campaignId.size(); ++i)
	{
		const char c = campaignId[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			return false;
		}
	}
	return true;
}

CampaignIdLoadOutcome loadCompanionCampaignId(const YAML::Node &saveDocument, std::string *campaignIdOut)
{
	if (campaignIdOut == 0)
	{
		return CAMPAIGN_ID_LEGACY_GENERATED;
	}
	if (saveDocument["companion"] && saveDocument["companion"]["campaign_id"])
	{
		const std::string stored = saveDocument["companion"]["campaign_id"].as<std::string>();
		if (isValidCampaignIdFormat(stored))
		{
			*campaignIdOut = stored;
			return CAMPAIGN_ID_LOADED_FROM_SAVE;
		}
	}
	*campaignIdOut = makeCampaignId();
	return CAMPAIGN_ID_LEGACY_GENERATED;
}

void writeCompanionCampaignId(YAML::Node *saveDocumentNode, const std::string &campaignId)
{
	if (saveDocumentNode == 0 || !isValidCampaignIdFormat(campaignId))
	{
		return;
	}
	YAML::Node companion = (*saveDocumentNode)["companion"];
	if (!companion || !companion.IsMap())
	{
		companion = YAML::Node(YAML::NodeType::Map);
	}
	companion["campaign_id"] = campaignId;
	(*saveDocumentNode)["companion"] = companion;
}

} // namespace CompanionCampaignIdentityCore

} // namespace OpenXcom
