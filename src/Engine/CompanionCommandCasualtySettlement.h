#pragma once
/*
 * Open X-Com Companion — detect in-flight roster casualty work affecting snapshot export.
 */
namespace OpenXcom
{

class BattlescapeGame;
class SavedBattleGame;

namespace CompanionCommandCasualtySettlement
{

/** True while a roster-soldier death animation or UnitDieBState can still change ledger/perception/units export. */
bool isRosterCasualtySnapshotWorkUnsettled(BattlescapeGame *game, SavedBattleGame *battle);

} // namespace CompanionCommandCasualtySettlement

} // namespace OpenXcom
