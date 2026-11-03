#pragma once
/*
 * Open X-Com Companion — campaign-save soldier ownership (M1-23C).
 *
 * Persistence: assignments live in save YAML under companion.assignments and become
 * durable only when SavedGame::save() writes the campaign file (normal save, Save As,
 * ironman/autosave paths). In-memory changes are lost if the process exits before save.
 *
 * Does not read companion-soldier-control.json and does not grant tactical execution
 * authority (M1-23D).
 */
#include <set>
#include <string>
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

namespace CompanionCampaignOwnershipCore
{

bool isValidOwnershipModeString(const std::string &mode);
bool parseSoldierIdKey(const std::string &key, int *soldierIdOut);
void loadCompanionAssignments(const YAML::Node &saveDocument, std::set<int> *aiSoldierIdsOut);
void writeCompanionAssignments(YAML::Node *saveDocumentNode, const std::set<int> &aiSoldierIds);

} // namespace CompanionCampaignOwnershipCore

} // namespace OpenXcom
