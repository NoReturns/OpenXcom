#pragma once
/*
 * Open X-Com Companion — tactical battle soldier ownership (M1-25B).
 *
 * Persists AI/human assignments on SavedBattleGame YAML, scoped by tactical_battle_id.
 * Geoscape SavedGame companion.assignments seeds new battles; unrelated battles do not inherit.
 */
#include <set>
#include <string>
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

namespace CompanionTacticalOwnershipCore
{

void seedTacticalAiFromCampaign(const std::set<int> &campaignAiSoldierIds, std::set<int> *tacticalAiSoldierIdsOut);

/// Load companion.tactical_battle_id and companion.assignments from a battle save node (legacy: leaves ids empty).
void loadFromBattleSaveNode(
	const YAML::Node &battleNode,
	std::string *tacticalBattleIdOut,
	std::set<int> *tacticalAiSoldierIdsOut);

void writeToBattleSaveNode(
	YAML::Node *battleNode,
	const std::string &tacticalBattleId,
	const std::set<int> &tacticalAiSoldierIds);

} // namespace CompanionTacticalOwnershipCore

} // namespace OpenXcom
