#pragma once
/*
 * Open X-Com Companion — player-visible mission context for tactical snapshots (M1-WP1).
 */
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

class Mod;
class SavedBattleGame;

namespace CompanionSnapshotMissionContext
{

void appendMissionContext(YAML::Node &root, SavedBattleGame *battle, Mod *mod);

} // namespace CompanionSnapshotMissionContext

} // namespace OpenXcom
