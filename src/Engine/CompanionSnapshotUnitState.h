#pragma once
/*
 * Open X-Com Companion — friendly unit facing, condition, and readiness for tactical snapshots (M1-WP2).
 */
#include <yaml-cpp/yaml.h>
#include "../Savegame/BattleUnit.h"
#include "../Mod/MapData.h"

namespace OpenXcom
{

class BattleUnit;
class SavedGame;
class SavedBattleGame;
class Language;

class CompanionSnapshotUnitState
{
public:
	/// Live roster X-COM soldier identity (includes unconscious/dead on battlescape).
	static bool isLiveRosterFriendlyUnit(const BattleUnit *unit, const SavedGame *campaign);

	/// Whether this unit row should appear in the snapshot units list.
	static bool shouldIncludeUnitInSnapshot(const BattleUnit *unit, const SavedGame *campaign);

	/// Engine direction 0–7 → map-space delta (north = decreasing Y), matching Pathfinding::directionToVector.
	static void directionToMapVector(int direction, int *outDx, int *outDy);

	static const char *directionLabel(int direction);

	static const char *unitStatusId(UnitStatus status);
	static const char *movementTypeId(MovementType movementType);

	/// True when engine facing is stable and should be exported (not collapsing/dead transition).
	static bool shouldExportFacing(const BattleUnit *unit);

	static void appendFacing(YAML::Node &entry, const BattleUnit *unit);
	static void appendFriendlyCondition(YAML::Node &entry, BattleUnit *unit);
	static void appendFriendlyReadiness(YAML::Node &entry, const BattleUnit *unit, const SavedGame *campaign);

	/// Player-visible posture/orientation for visible non-friendly units only.
	static void appendVisibleHostileObservations(YAML::Node &entry, const BattleUnit *unit);

	/// Fair-play unit type / MC roster name (current LOS); does not export friendly tactical detail.
	static void appendUnitObservationIdentity(
		YAML::Node &entry,
		SavedBattleGame *battle,
		const BattleUnit *unit,
		const SavedGame *campaign,
		Language *lang);
};

} // namespace OpenXcom
