/*
 * Open X-Com Companion — player-perceivable event logic (M1-WP6, unit-test core).
 */
#include "CompanionPerceptionLogCore.h"
#include "CompanionSnapshotUnitStateCore.h"
#include "../Savegame/BattleUnit.h"
#include "../fmath.h"
#include <cstdio>
#include <cstring>

namespace OpenXcom
{

namespace CompanionPerceptionLogCore
{

const char *perceptionChannelId(PerceptionChannel channel)
{
	switch (channel)
	{
	case PERCEPTION_HEARD: return "heard";
	case PERCEPTION_SEEN: return "seen";
	case PERCEPTION_SEEN_AND_HEARD: return "seen_and_heard";
	default: return "heard";
	}
}

UnitLastSeenExportResolution resolveUnitLastSeenExport(const UnitLastSeenExportInput &input)
{
	UnitLastSeenExportResolution out = {};
	if (input.debugMode || input.confidence == 0 || input.confidence[0] == '\0')
	{
		return out;
	}
	if (std::strcmp(input.confidence, "sighted") == 0)
	{
		if (!input.hostileTileCurrentlyVisible)
		{
			return out;
		}
		out.shouldEmit = true;
		out.positionKnown = true;
		out.exportPosition = input.currentHostilePosition;
		out.channel = PERCEPTION_SEEN;
		return out;
	}
	if (std::strcmp(input.confidence, "last_seen") == 0)
	{
		if (!input.hasStoredLastPosition)
		{
			return out;
		}
		out.shouldEmit = true;
		out.positionKnown = true;
		out.exportPosition = input.storedLastPosition;
		out.channel = PERCEPTION_SEEN;
		return out;
	}
	return out;
}

std::string makeDedupeKey(
	const char *type,
	int turn,
	int subjectUnitId,
	const Position *pos,
	bool positionKnown,
	PerceptionChannel channel)
{
	char buf[256];
	if (positionKnown && pos != 0)
	{
		std::sprintf(buf, "%s:%d:%d:%d:%d:%d:%d",
			type, turn, subjectUnitId, pos->x, pos->y, pos->z, static_cast<int>(channel));
	}
	else
	{
		std::sprintf(buf, "%s:%d:%d:unknown:%d", type, turn, subjectUnitId, static_cast<int>(channel));
	}
	return std::string(buf);
}

int oppositeCompassDirection(int direction)
{
	if (direction < 0 || direction > 7)
	{
		return -1;
	}
	return (direction + 4) % 8;
}

int directionBetweenMapTiles(const Position &fromTile, const Position &toTile)
{
	const double ox = static_cast<double>(toTile.x - fromTile.x);
	const double oy = static_cast<double>(toTile.y - fromTile.y);
	if (ox == 0.0 && oy == 0.0)
	{
		return -1;
	}
	const double angle = std::atan2(ox, -oy);
	const double pie[4] = {
		(M_PI_4 * 4.0) - M_PI_4 / 2.0,
		(M_PI_4 * 3.0) - M_PI_4 / 2.0,
		(M_PI_4 * 2.0) - M_PI_4 / 2.0,
		(M_PI_4 * 1.0) - M_PI_4 / 2.0};
	int dir = 0;
	if (angle > pie[0] || angle < -pie[0])
	{
		dir = 4;
	}
	else if (angle > pie[1])
	{
		dir = 3;
	}
	else if (angle > pie[2])
	{
		dir = 2;
	}
	else if (angle > pie[3])
	{
		dir = 1;
	}
	else if (angle < -pie[1])
	{
		dir = 5;
	}
	else if (angle < -pie[2])
	{
		dir = 6;
	}
	else if (angle < -pie[3])
	{
		dir = 7;
	}
	return dir;
}

const char *directionLabelForDirection(int direction)
{
	return CompanionSnapshotUnitStateCore::directionLabel(direction);
}

bool shouldAcceptProjectileDirectionSample(const ProjectileDirectionSampleInput &input)
{
	if (input.debugMode || !input.projectileInFov || !input.tileInFriendlySight || !input.voxelRenderable)
	{
		return false;
	}
	return input.onTacticalViewport;
}

ProjectileDirectionResolution resolveProjectileDirectionFromOrderedSamples(
	const std::vector<Position> &orderedTileSamples)
{
	ProjectileDirectionResolution out = {};
	if (orderedTileSamples.empty())
	{
		return out;
	}
	out.emitEvent = true;
	out.referenceTile = orderedTileSamples.front();
	out.positionKnown = true;
	if (orderedTileSamples.size() < 2)
	{
		out.trajectoryKnown = false;
		return out;
	}
	std::vector<Position> distinct;
	for (std::vector<Position>::const_iterator i = orderedTileSamples.begin(); i != orderedTileSamples.end(); ++i)
	{
		if (distinct.empty() || distinct.back().x != i->x || distinct.back().y != i->y || distinct.back().z != i->z)
		{
			distinct.push_back(*i);
		}
	}
	if (distinct.size() < 2)
	{
		out.trajectoryKnown = false;
		return out;
	}
	const Position &first = distinct.front();
	const Position &last = distinct.back();
	const int travelDir = directionBetweenMapTiles(first, last);
	if (travelDir < 0)
	{
		out.trajectoryKnown = false;
		return out;
	}
	const int incomingDir = oppositeCompassDirection(travelDir);
	const char *travelLabel = directionLabelForDirection(travelDir);
	const char *incomingLabel = directionLabelForDirection(incomingDir);
	if (travelLabel == 0 || incomingLabel == 0)
	{
		out.trajectoryKnown = false;
		return out;
	}
	out.trajectoryKnown = true;
	out.travelDirection = travelLabel;
	out.incomingBearing = incomingLabel;
	out.referenceTile.x = (first.x + last.x) / 2;
	out.referenceTile.y = (first.y + last.y) / 2;
	out.referenceTile.z = (first.z + last.z) / 2;
	return out;
}

std::string makeProjectileDirectionDedupeKey(int turn, int flightId)
{
	char buf[128];
	std::sprintf(buf, "projectile_direction_observed:%d:%d", turn, flightId);
	return std::string(buf);
}

bool shouldExportObserverAttribution(bool positionKnown, bool hasObserverUnitId)
{
	return positionKnown && hasObserverUnitId;
}

std::string makePsychologicalObservationDedupeKey(
	const char *eventType,
	int subjectUnitId,
	int turn,
	int observationSerial)
{
	char buf[256];
	if (isPsychologicalTurnEpisodeEventType(eventType))
	{
		std::sprintf(buf, "psych:%s:%d:%d", eventType, subjectUnitId, turn);
	}
	else
	{
		std::sprintf(buf, "psych:%s:%d:%d:%d", eventType, subjectUnitId, turn, observationSerial);
	}
	return std::string(buf);
}

const char *psychologicalConfidenceId(bool subjectVisible)
{
	return subjectVisible ? "unit_visible" : "message_notification";
}

const char *psychologicalPerceptionChannelId()
{
	return "notification";
}

bool isPsychologicalPerceptionEventType(const char *eventType)
{
	if (eventType == 0)
	{
		return false;
	}
	return std::strcmp(eventType, "soldier_panicked_observed") == 0
		|| std::strcmp(eventType, "soldier_berserk_observed") == 0
		|| std::strcmp(eventType, "alien_panicked_observed") == 0
		|| std::strcmp(eventType, "alien_berserk_observed") == 0
		|| std::strcmp(eventType, "soldier_mind_controlled_observed") == 0
		|| std::strcmp(eventType, "alien_mind_controlled_observed") == 0
		|| std::strcmp(eventType, "psi_morale_attack_observed") == 0;
}

bool isAlienPanicOrBerserkObservationEventType(const char *eventType)
{
	if (eventType == 0)
	{
		return false;
	}
	return std::strcmp(eventType, "alien_panicked_observed") == 0
		|| std::strcmp(eventType, "alien_berserk_observed") == 0;
}

bool isPsychologicalTurnEpisodeEventType(const char *eventType)
{
	if (eventType == 0)
	{
		return false;
	}
	return isAlienPanicOrBerserkObservationEventType(eventType)
		|| std::strcmp(eventType, "soldier_panicked_observed") == 0
		|| std::strcmp(eventType, "soldier_berserk_observed") == 0;
}

ProjectileImpactExportResolution resolveProjectileImpactExport(const ProjectileImpactExportInput &input)
{
	ProjectileImpactExportResolution out = {};
	if (input.debugMode)
	{
		return out;
	}
	if (input.tileVisible && input.voxelPart == COMPANION_VOXEL_PART_UNIT && input.victimUnitVisible)
	{
		out.shouldEmit = true;
		out.positionKnown = true;
		out.confidenceId = "visible_unit_hit";
		return out;
	}
	if (input.tileVisible)
	{
		out.shouldEmit = true;
		out.positionKnown = true;
		// Generic visible terminal impact: same label for terrain and hidden-unit voxel hits (fair-play).
		out.confidenceId = "impact_visible";
		return out;
	}
	if (input.audible)
	{
		out.shouldEmit = true;
		out.positionKnown = false;
		out.confidenceId = "audio_only";
		return out;
	}
	return out;
}

std::string makeProjectileImpactDedupeKey(int turn, int flightId, int impactOrdinal)
{
	char buf[96];
	std::sprintf(buf, "impact_observed:%d:%d:%d", turn, flightId, impactOrdinal);
	return std::string(buf);
}

bool isCombatCasualtyPerceptionEventType(const char *eventType)
{
	if (eventType == 0)
	{
		return false;
	}
	return std::strcmp(eventType, "soldier_down") == 0
		|| std::strcmp(eventType, "soldier_recovered") == 0;
}

bool isCombatCasualtyAttributionPerceptionEventType(const char *eventType)
{
	return eventType != 0 && std::strcmp(eventType, "combat_casualty_attributed") == 0;
}

CombatCasualtyAttributionExportResolution resolveCombatCasualtyAttributionExport(
	const CombatCasualtyAttributionExportInput &input)
{
	CombatCasualtyAttributionExportResolution out = {};
	if (input.debugMode || !input.victimConfirmedDead || !input.fatalActorPresent)
	{
		return out;
	}
	if (input.mindControlKillCredit || input.fatalWoundBleedOutPath || input.terrainExplosionWithoutActor
		|| input.hiddenExplosionContext)
	{
		return out;
	}
	if (!input.fatalActorVisible)
	{
		return out;
	}
	if (input.victimHostileOriginal)
	{
		if (!input.victimVisible)
		{
			return out;
		}
		out.exportVictimSubjectId = true;
	}
	else if (input.victimRosterSoldier)
	{
		out.exportVictimSubjectId = true;
	}
	else
	{
		return out;
	}
	out.exportAttributedActorId = true;
	out.shouldEmit = true;
	return out;
}

std::string makeCombatCasualtyAttributionDedupeKey(int victimUnitId)
{
	char buf[64];
	std::sprintf(buf, "combat_casualty_attributed:%d", victimUnitId);
	return std::string(buf);
}

ExplosionOriginExportResolution resolveExplosionOriginExport(const ExplosionOriginExportInput &input)
{
	ExplosionOriginExportResolution out = {};
	if (input.debugMode)
	{
		return out;
	}
	if (!input.unitDeathDetonationEngineContext)
	{
		return out;
	}
	if (!input.blastLegitimatelyObservable || !input.sourceUnitCurrentlyVisible)
	{
		return out;
	}
	out.explosionOriginClass = "unit_death_detonation";
	out.explosionOriginClassSource = "engine_state";
	return out;
}

bool isOriginalXcomRosterCasualtySubject(const RosterCasualtySubjectInput &input)
{
	if (input.unitId <= 0 || input.unitId >= ROSTER_SOLDIER_ID_MAX)
	{
		return false;
	}
	if (!input.hasGeoscapeSoldierLink || input.geoscapeSoldierId != input.unitId)
	{
		return false;
	}
	// FACTION_PLAYER — original deployment identity (includes mind-controlled roster soldiers).
	if (input.originalFaction != 0)
	{
		return false;
	}
	return true;
}

std::string makeSoldierCasualtyEpisodeDedupeKey(
	const char *eventType,
	int subjectUnitId,
	int episodeSerial,
	const char *confidence)
{
	char buf[128];
	std::sprintf(buf, "%s:%d:%d:%s", eventType, subjectUnitId, episodeSerial, confidence != 0 ? confidence : "");
	return std::string(buf);
}

namespace
{

/** Mirrors RuleItem::BattleType values used for throw classification (avoid Mod include in core). */
const int BT_GRENADE_CORE = 4;
const int BT_PROXIMITYGRENADE_CORE = 5;

const char *classifyVisibleThrowLaunchEventType(bool isLaunch, int battleType)
{
	if (isLaunch)
	{
		return "explosive_launch_observed";
	}
	if (battleType == BT_GRENADE_CORE || battleType == BT_PROXIMITYGRENADE_CORE)
	{
		return "grenade_throw_observed";
	}
	return "object_throw_observed";
}

} // namespace

ThrowLaunchPerceptionResolution resolveThrowLaunchPerceptionExport(const ThrowLaunchPerceptionInput &input)
{
	ThrowLaunchPerceptionResolution out = {};
	if (input.debugMode)
	{
		return out;
	}
	if (!input.positionKnown && !input.audible)
	{
		return out;
	}
	out.shouldEmit = true;
	out.confidence = input.positionKnown ? "source_visible" : "audio_only";
	if (input.positionKnown)
	{
		if (input.throwerVisible)
		{
			out.eventType = classifyVisibleThrowLaunchEventType(input.isLaunch, input.battleType);
		}
		else
		{
			out.eventType = "throw_observed";
		}
	}
	else
	{
		out.eventType = "throw_observed";
	}
	return out;
}

bool shouldAttachThrowLaunchHostileSubject(
	bool debugMode,
	bool hasShooter,
	bool shooterVisibleFlag)
{
	if (debugMode || !hasShooter)
	{
		return false;
	}
	return shooterVisibleFlag;
}

bool shouldRecordReactionAttackExecuted(const ReactionAttackFairPlayInput &input)
{
	if (input.debugMode)
	{
		return false;
	}
	return input.audible || input.positionKnown;
}

bool shouldAttachReactionAttackSubjectId(const ReactionAttackFairPlayInput &input)
{
	if (input.debugMode || !input.hasActor)
	{
		return false;
	}
	if (input.actorOriginalFaction == FACTION_HOSTILE)
	{
		return input.actorVisible;
	}
	if (input.actorOriginalFaction == FACTION_PLAYER && input.actorHasGeoscapeSoldier)
	{
		return input.actorVisible;
	}
	return false;
}

} // namespace CompanionPerceptionLogCore

namespace CompanionPerceptionLogTestSupport
{

bool shouldRecordGunfire(const GunfireInput &input)
{
	if (input.debugMode || !input.audible)
	{
		return false;
	}
	return true;
}

bool shouldIncludeGunfirePosition(const GunfireInput &input)
{
	if (input.debugMode)
	{
		return false;
	}
	return input.shooterVisibleFlag || input.originTileVisible;
}

bool shouldIncludeSpatialPosition(const SpatialInput &input)
{
	if (input.debugMode)
	{
		return false;
	}
	return input.tileVisible;
}

bool spatialObservationUsesSeenChannel(const SpatialInput &input)
{
	if (input.debugMode || !input.tileVisible)
	{
		return false;
	}
	return input.audible;
}

bool shouldAttachGunfireSubjectId(const GunfireInput &input)
{
	if (input.debugMode || !input.hasShooterUnit)
	{
		return false;
	}
	return input.shooterVisibleFlag;
}

bool shouldRecordThrowLaunch(const ThrowLaunchFairPlayInput &input)
{
	if (input.debugMode || !input.audible)
	{
		return false;
	}
	return true;
}

const char *resolveThrowLaunchEventTypeForTest(const ThrowLaunchFairPlayInput &input)
{
	CompanionPerceptionLogCore::ThrowLaunchPerceptionInput core = {};
	core.debugMode = input.debugMode;
	core.audible = input.audible;
	core.isLaunch = input.isLaunch;
	core.battleType = input.battleType;
	core.positionKnown = input.positionKnown;
	core.throwerVisible = input.throwerVisible;
	const CompanionPerceptionLogCore::ThrowLaunchPerceptionResolution resolution =
		CompanionPerceptionLogCore::resolveThrowLaunchPerceptionExport(core);
	return resolution.shouldEmit ? resolution.eventType : 0;
}

bool shouldAttachThrowLaunchSubjectId(const ThrowLaunchFairPlayInput &input)
{
	if (input.debugMode || !input.hasShooterUnit)
	{
		return false;
	}
	return input.shooterVisibleFlag;
}

bool shouldEmitUnitLastSeenOnHide(const VisibilityInput &input)
{
	if (input.debugMode)
	{
		return false;
	}
	return input.wasVisible && !input.nowVisible && input.hostileUnitId > 0;
}

bool shouldEmitUnitLastSeenOnShow(const VisibilityInput &input)
{
	if (input.debugMode)
	{
		return false;
	}
	return !input.wasVisible && input.nowVisible && input.hostileUnitId > 0;
}

bool shouldRecordPsychologicalNotification(const PsychNotificationInput &input)
{
	if (input.debugMode)
	{
		return false;
	}
	return input.uiNotificationShown;
}

bool shouldExportPsychologicalSubjectUnitId(
	const PsychNotificationInput &input,
	const char *eventType,
	bool rosterSoldierIdentity)
{
	if (input.debugMode || !input.uiNotificationShown)
	{
		return false;
	}
	if (rosterSoldierIdentity)
	{
		return true;
	}
	if (CompanionPerceptionLogCore::isAlienPanicOrBerserkObservationEventType(eventType))
	{
		return input.subjectVisible;
	}
	return true;
}

bool shouldExportPsychologicalSubjectName(bool rosterSoldierIdentity)
{
	return rosterSoldierIdentity;
}

} // namespace CompanionPerceptionLogTestSupport

} // namespace OpenXcom
