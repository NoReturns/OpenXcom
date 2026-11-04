#pragma once
/*
 * Open X-Com Companion — local command file polling and results (M1-19).
 */
#include <string>

namespace OpenXcom
{

class BattlescapeGame;
class SavedBattleGame;
class SavedGame;

namespace CompanionCommandRuntime
{

void noteSnapshotExportSha256(const std::string &sha256LowerHex);
void noteSnapshotExportStateVersion(int latestEventSeq);

bool isMoveInFlight();

/// Resets in-flight state when battle session/tactical scope changes; preserves terminal evidence.
void onBattleCompanionBindingContext(SavedBattleGame *battle);

/// Archives inbox commands that cannot match the export just written (F6 lifecycle).
void archiveStaleInboxAfterSnapshotExport(SavedBattleGame *battle, const std::string &snapshotSha256LowerHex);

void tick(BattlescapeGame *game, SavedBattleGame *battle, SavedGame *campaign, bool battleInitialized);

} // namespace CompanionCommandRuntime

} // namespace OpenXcom
