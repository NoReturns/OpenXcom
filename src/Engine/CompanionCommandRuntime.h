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

/// Unit id for companion EXECUTING command, or -1 when idle.
int getInFlightActorUnitId();

/// Native projectile lift-off for an in-flight companion SNAP_SHOT (same actor).
void notifySnapShotProjectileLaunched(int actorUnitId);

/// Native grenade release for an in-flight companion THROW (same actor).
void notifyThrowProjectileLaunched(int actorUnitId);

/// Records native pre-fire failure result while companion SNAP_SHOT is in flight.
void noteSnapShotBattleActionPop(int actorUnitId, const std::string &actionResult);

/// Records native pre-throw failure result while companion THROW is in flight.
void noteThrowBattleActionPop(int actorUnitId, const std::string &actionResult);

/// Records native walk abort result while companion MOVE is in flight.
void noteWalkBattleActionPop(int actorUnitId, const std::string &actionResult);

/// Resets in-flight state when battle session/tactical scope changes; preserves terminal evidence.
void onBattleCompanionBindingContext(SavedBattleGame *battle);

/// Archives inbox commands that cannot match the export just written (F6 lifecycle).
void archiveStaleInboxAfterSnapshotExport(SavedBattleGame *battle, const std::string &snapshotSha256LowerHex);

void tick(BattlescapeGame *game, SavedBattleGame *battle, SavedGame *campaign, bool battleInitialized);

} // namespace CompanionCommandRuntime

} // namespace OpenXcom
