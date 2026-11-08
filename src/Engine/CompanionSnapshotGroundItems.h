#pragma once
/*
 * Open X-Com Companion — ground-item snapshot export (M1-WP9).
 */
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

class SavedBattleGame;
class SavedGame;

namespace CompanionSnapshotGroundItems
{

const char *MEMORY_NOTE();

void appendGroundItems(YAML::Node &root, SavedBattleGame *battle, SavedGame *campaign);

} // namespace CompanionSnapshotGroundItems

} // namespace OpenXcom
