#pragma once
/*
 * Open X-Com Companion — player-perceivable event logic (M1-WP6, testable core).
 */
#include "../Battlescape/Position.h"
#include <string>
#include <vector>

namespace OpenXcom
{

namespace CompanionPerceptionLogCore
{

enum PerceptionChannel
{
	PERCEPTION_HEARD = 0,
	PERCEPTION_SEEN = 1,
	PERCEPTION_SEEN_AND_HEARD = 2
};

const char *perceptionChannelId(PerceptionChannel channel);

struct ObserverResolution
{
	int observerUnitId;
	bool hasObserver;
	PerceptionChannel channel;
	bool positionKnown;
	Position position;
	bool hasCompassDirection;
	int compassDirection;
};

std::string makeDedupeKey(
	const char *type,
	int turn,
	int subjectUnitId,
	const Position *pos,
	bool positionKnown,
	PerceptionChannel channel);

struct UnitLastSeenExportInput
{
	bool debugMode;
	const char *confidence;
	bool hasStoredLastPosition;
	Position storedLastPosition;
	Position currentHostilePosition;
	bool hostileTileCurrentlyVisible;
};

struct UnitLastSeenExportResolution
{
	bool shouldEmit;
	bool positionKnown;
	Position exportPosition;
	PerceptionChannel channel;
};

/** Visual sighting / memory export — never maps visibility loss to heard-only channel. */
UnitLastSeenExportResolution resolveUnitLastSeenExport(const UnitLastSeenExportInput &input);

int oppositeCompassDirection(int direction);
int directionBetweenMapTiles(const Position &fromTile, const Position &toTile);
const char *directionLabelForDirection(int direction);

struct ProjectileDirectionSampleInput
{
	bool debugMode;
	bool projectileInFov;
	bool tileInFriendlySight;
	bool voxelRenderable;
	bool onTacticalViewport;
};

struct ProjectileDirectionResolution
{
	bool emitEvent;
	bool trajectoryKnown;
	bool positionKnown;
	Position referenceTile;
	std::string travelDirection;
	std::string incomingBearing;
};

bool shouldAcceptProjectileDirectionSample(const ProjectileDirectionSampleInput &input);
ProjectileDirectionResolution resolveProjectileDirectionFromOrderedSamples(
	const std::vector<Position> &orderedTileSamples);

std::string makeProjectileDirectionDedupeKey(int turn, int flightId);

bool shouldExportObserverAttribution(bool positionKnown, bool hasObserverUnitId);

std::string makePsychologicalObservationDedupeKey(
	const char *eventType,
	int subjectUnitId,
	int turn,
	int observationSerial);

const char *psychologicalConfidenceId(bool subjectVisible);
const char *psychologicalPerceptionChannelId();

bool isPsychologicalPerceptionEventType(const char *eventType);

/** Panic/berserk: at most one engine episode per subject per turn; dedupe keys omit observationSerial. */
bool isPsychologicalTurnEpisodeEventType(const char *eventType);

bool isAlienPanicOrBerserkObservationEventType(const char *eventType);

/** Matches OpenXcom MapData V_UNIT (do not include MapData in unit-test core). */
const int COMPANION_VOXEL_PART_UNIT = 4;
const int MAX_ROSTER_CASUALTY_LEDGER_ENTRIES = 32;

struct ProjectileImpactExportInput
{
	bool debugMode;
	bool tileVisible;
	bool audible;
	int voxelPart;
	bool victimUnitVisible;
};

struct ProjectileImpactExportResolution
{
	bool shouldEmit;
	bool positionKnown;
	const char *confidenceId;
};

/** Classify player-legitimate projectile terminal impacts (M1 combat feedback). */
ProjectileImpactExportResolution resolveProjectileImpactExport(const ProjectileImpactExportInput &input);

std::string makeProjectileImpactDedupeKey(int turn, int flightId, int impactOrdinal);

bool isCombatCasualtyPerceptionEventType(const char *eventType);

/** Roster soldier ID ceiling (matches BattleUnit::MAX_SOLDIER_ID). */
const int ROSTER_SOLDIER_ID_MAX = 1000000;

/**
 * Battle-time roster casualty subject (M1 Step 1E).
 * Uses original X-COM deployment identity + geoscape soldier link — not campaign live roster
 * (KIA soldiers are erased from bases before UnitDieBState records soldier_down).
 */
struct RosterCasualtySubjectInput
{
	int unitId;
	bool hasGeoscapeSoldierLink;
	int geoscapeSoldierId;
	int originalFaction;
};

bool isOriginalXcomRosterCasualtySubject(const RosterCasualtySubjectInput &input);

/** One perception event per subject episode serial + confidence per battle companion session. */
std::string makeSoldierCasualtyEpisodeDedupeKey(
	const char *eventType,
	int subjectUnitId,
	int episodeSerial,
	const char *confidence);

/**
 * Fair-play throw / launch perception (M1 P1-D).
 * Item class is reported only when the throw origin is player-visible (positionKnown).
 */
struct ThrowLaunchPerceptionInput
{
	bool debugMode;
	bool audible;
	bool isLaunch;
	int battleType;
	bool positionKnown;
	/** Visible hostile/origin is insufficient for grenade class — requires thrower sprite visible. */
	bool throwerVisible;
};

struct ThrowLaunchPerceptionResolution
{
	bool shouldEmit;
	const char *eventType;
	const char *confidence;
};

ThrowLaunchPerceptionResolution resolveThrowLaunchPerceptionExport(const ThrowLaunchPerceptionInput &input);

bool shouldAttachThrowLaunchHostileSubject(
	bool debugMode,
	bool hasShooter,
	bool shooterVisibleFlag);

} // namespace CompanionPerceptionLogCore

namespace CompanionPerceptionLogTestSupport
{

struct GunfireInput
{
	bool debugMode;
	bool audible;
	bool shooterVisibleFlag;
	bool originTileVisible;
	bool hasShooterUnit;
	int shooterUnitId;
	Position origin;
};

struct SpatialInput
{
	bool debugMode;
	bool audible;
	bool tileVisible;
	Position pos;
};

struct VisibilityInput
{
	bool debugMode;
	bool wasVisible;
	bool nowVisible;
	int hostileUnitId;
	Position position;
};

bool shouldRecordGunfire(const GunfireInput &input);
bool shouldIncludeGunfirePosition(const GunfireInput &input);
bool shouldIncludeSpatialPosition(const SpatialInput &input);
bool spatialObservationUsesSeenChannel(const SpatialInput &input);
bool shouldAttachGunfireSubjectId(const GunfireInput &input);

struct ThrowLaunchFairPlayInput
{
	bool debugMode;
	bool audible;
	bool isLaunch;
	int battleType;
	bool positionKnown;
	bool throwerVisible;
	bool shooterVisibleFlag;
	bool hasShooterUnit;
};

bool shouldRecordThrowLaunch(const ThrowLaunchFairPlayInput &input);
const char *resolveThrowLaunchEventTypeForTest(const ThrowLaunchFairPlayInput &input);
bool shouldAttachThrowLaunchSubjectId(const ThrowLaunchFairPlayInput &input);

bool shouldEmitUnitLastSeenOnHide(const VisibilityInput &input);
bool shouldEmitUnitLastSeenOnShow(const VisibilityInput &input);

struct PsychNotificationInput
{
	bool debugMode;
	bool uiNotificationShown;
	bool subjectVisible;
};

bool shouldRecordPsychologicalNotification(const PsychNotificationInput &input);
bool shouldExportPsychologicalSubjectUnitId(
	const PsychNotificationInput &input,
	const char *eventType,
	bool rosterSoldierIdentity);
bool shouldExportPsychologicalSubjectName(bool rosterSoldierIdentity);

} // namespace CompanionPerceptionLogTestSupport

} // namespace OpenXcom
