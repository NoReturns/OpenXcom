#pragma once
/*
 * Open X-Com Companion — local command file polling and results (M1-19).
 */
#include <string>

namespace OpenXcom
{

class BattlescapeGame;
class SavedBattleGame;

namespace CompanionCommandRuntime
{

void noteSnapshotExportSha256(const std::string &sha256LowerHex);
void noteSnapshotExportStateVersion(int latestEventSeq);

bool isMoveInFlight();

void tick(BattlescapeGame *game, SavedBattleGame *battle, bool battleInitialized);

} // namespace CompanionCommandRuntime

} // namespace OpenXcom
