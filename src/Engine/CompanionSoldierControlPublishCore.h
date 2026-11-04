#pragma once
/*
 * Open X-Com Companion — publish companion-soldier-control.json from campaign (M1-23F3).
 */
#include <set>
#include <string>
#include <vector>

namespace OpenXcom
{

namespace CompanionSoldierControlPublishCore
{

struct ControlPublicationPlan
{
	bool ok;
	std::string sessionId;
	std::string jsonUtf8;
	std::string publicReason;
};

/**
 * Build control JSON from in-memory campaign AI assignments and validated unit ids.
 * Omits human soldiers (default human). Includes only "ai" entries.
 */
ControlPublicationPlan buildControlPublicationJson(
	const std::string &sessionId,
	const std::string &campaignId,
	const std::string &tacticalBattleId,
	const std::set<int> &tacticalAiSoldierIds,
	const std::vector<int> &publishableLivePlayerUnitIds);

/// Atomic UTF-8 write; leaves @p destPath unchanged on failure.
bool writeControlFileAtomically(const std::string &destPath, const std::string &jsonUtf8);

} // namespace CompanionSoldierControlPublishCore

} // namespace OpenXcom
