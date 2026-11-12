/*
 * Open X-Com Companion — perception log fairness tests (M1-WP6).
 */
#include "Engine/CompanionPerceptionLogCore.h"
#include "Engine/RNG.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace OpenXcom;
using namespace OpenXcom::CompanionPerceptionLogTestSupport;
using namespace OpenXcom::CompanionPerceptionLogCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static void testHiddenGunfireOmitsPosition()
{
	GunfireInput input = {};
	input.debugMode = false;
	input.audible = true;
	input.shooterVisibleFlag = false;
	input.originTileVisible = false;
	expect(shouldRecordGunfire(input), "audible gunfire should record");
	expect(!shouldIncludeGunfirePosition(input), "hidden shooter must not expose coordinates");
}

static void testVisibleGunfireIncludesPosition()
{
	GunfireInput input = {};
	input.debugMode = false;
	input.audible = true;
	input.shooterVisibleFlag = true;
	input.originTileVisible = false;
	expect(shouldIncludeGunfirePosition(input), "visible shooter may include position");
}

static void testAudioOnlySpatialOmitsPosition()
{
	SpatialInput input = {};
	input.debugMode = false;
	input.audible = true;
	input.tileVisible = false;
	expect(!shouldIncludeSpatialPosition(input), "off-screen impact must not expose coordinates");
}

static void testUnitLastSeenTransitions()
{
	VisibilityInput input = {};
	input.debugMode = false;
	input.hostileUnitId = 1000001;
	input.wasVisible = false;
	input.nowVisible = true;
	expect(shouldEmitUnitLastSeenOnShow(input), "first sighting should emit last-seen memory");
	input.wasVisible = true;
	input.nowVisible = false;
	expect(shouldEmitUnitLastSeenOnHide(input), "visibility loss should emit last-seen memory");
}

static void testUnitLastSeenExportNovosibirskTurnBoundaryMemory()
{
	UnitLastSeenExportInput sighted = {};
	sighted.confidence = "sighted";
	sighted.currentHostilePosition = Position(17, 7, 0);
	sighted.hostileTileCurrentlyVisible = true;
	const UnitLastSeenExportResolution sightedRes = resolveUnitLastSeenExport(sighted);
	expect(sightedRes.shouldEmit, "sighted emits when tile visible");
	expect(sightedRes.channel == PERCEPTION_SEEN, "sighted uses seen channel not heard");
	expect(sightedRes.positionKnown, "sighted position known");
	expect(sightedRes.exportPosition.x == 17 && sightedRes.exportPosition.y == 7, "sighted uses live tile");

	UnitLastSeenExportInput memory = {};
	memory.confidence = "last_seen";
	memory.hasStoredLastPosition = true;
	memory.storedLastPosition = Position(17, 7, 0);
	memory.currentHostilePosition = Position(15, 6, 0);
	memory.hostileTileCurrentlyVisible = false;
	const UnitLastSeenExportResolution memoryRes = resolveUnitLastSeenExport(memory);
	expect(memoryRes.shouldEmit, "turn-end memory emits with stored last position");
	expect(memoryRes.channel == PERCEPTION_SEEN, "visibility loss must not classify as heard");
	expect(memoryRes.exportPosition.x == 17 && memoryRes.exportPosition.y == 7,
		"last_seen must not export subsequent hidden movement tile");
	expect(memoryRes.exportPosition.x != memory.currentHostilePosition.x
	    || memoryRes.exportPosition.y != memory.currentHostilePosition.y,
		"hidden current position must not replace stored memory");
}

static void testUnitLastSeenExportRequiresStoredMemory()
{
	UnitLastSeenExportInput memory = {};
	memory.confidence = "last_seen";
	memory.hasStoredLastPosition = false;
	memory.currentHostilePosition = Position(15, 6, 0);
	const UnitLastSeenExportResolution memoryRes = resolveUnitLastSeenExport(memory);
	expect(!memoryRes.shouldEmit, "last_seen without memory must not emit hidden coordinates");
}

static void testGunfireSubjectRequiresVisibleShooter()
{
	GunfireInput input = {};
	input.debugMode = false;
	input.hasShooterUnit = true;
	input.shooterVisibleFlag = false;
	input.originTileVisible = true;
	expect(!shouldAttachGunfireSubjectId(input), "LOS tile alone must not identify hidden shooter");
	input.shooterVisibleFlag = true;
	expect(shouldAttachGunfireSubjectId(input), "visible shooter may carry subjectUnitId");
}

static void testHiddenThrowUsesGenericAudioType()
{
	ThrowLaunchFairPlayInput input = {};
	input.debugMode = false;
	input.audible = true;
	input.isLaunch = false;
	input.battleType = 4;
	input.positionKnown = false;
	const char *eventType = resolveThrowLaunchEventTypeForTest(input);
	expect(eventType != 0 && std::strcmp(eventType, "throw_observed") == 0,
		"hidden throw must not claim grenade class");
}

static void testVisibleGrenadeThrowClassified()
{
	ThrowLaunchFairPlayInput input = {};
	input.debugMode = false;
	input.audible = true;
	input.isLaunch = false;
	input.battleType = 4;
	input.positionKnown = true;
	input.throwerVisible = true;
	const char *eventType = resolveThrowLaunchEventTypeForTest(input);
	expect(eventType != 0 && std::strcmp(eventType, "grenade_throw_observed") == 0,
		"visible grenade throw uses grenade_throw_observed");
}

static void testHiddenThrowerVisibleTileDoesNotClaimGrenade()
{
	ThrowLaunchFairPlayInput input = {};
	input.debugMode = false;
	input.audible = true;
	input.isLaunch = false;
	input.battleType = 4;
	input.positionKnown = true;
	input.throwerVisible = false;
	const char *eventType = resolveThrowLaunchEventTypeForTest(input);
	expect(eventType != 0 && std::strcmp(eventType, "throw_observed") == 0,
		"origin tile alone must not classify grenade");
}

static void testInaudibleHiddenThrowSilent()
{
	ThrowLaunchFairPlayInput input = {};
	input.debugMode = false;
	input.audible = false;
	input.positionKnown = false;
	const char *eventType = resolveThrowLaunchEventTypeForTest(input);
	expect(eventType == 0, "inaudible hidden throw must not emit");
}

static void testVisibleLaunchClassified()
{
	ThrowLaunchFairPlayInput input = {};
	input.debugMode = false;
	input.audible = true;
	input.isLaunch = true;
	input.battleType = 2;
	input.positionKnown = true;
	input.throwerVisible = true;
	const char *eventType = resolveThrowLaunchEventTypeForTest(input);
	expect(eventType != 0 && std::strcmp(eventType, "explosive_launch_observed") == 0,
		"visible launch uses explosive_launch_observed");
}

static void testNonGrenadeThrowObjectType()
{
	ThrowLaunchFairPlayInput input = {};
	input.debugMode = false;
	input.audible = true;
	input.isLaunch = false;
	input.battleType = 10;
	input.positionKnown = true;
	input.throwerVisible = true;
	const char *eventType = resolveThrowLaunchEventTypeForTest(input);
	expect(eventType != 0 && std::strcmp(eventType, "object_throw_observed") == 0,
		"visible non-grenade throw uses object_throw_observed");
}

static void testThrowSubjectRequiresVisibleShooter()
{
	ThrowLaunchFairPlayInput input = {};
	input.debugMode = false;
	input.hasShooterUnit = true;
	input.shooterVisibleFlag = false;
	expect(!shouldAttachThrowLaunchSubjectId(input), "throw must not attach hidden hostile id");
	input.shooterVisibleFlag = true;
	expect(shouldAttachThrowLaunchSubjectId(input), "visible hostile throw may attach subject");
}

static void testVisibleDoorUsesSeenChannel()
{
	SpatialInput input = {};
	input.debugMode = false;
	input.audible = true;
	input.tileVisible = true;
	expect(shouldIncludeSpatialPosition(input), "visible door exports tile");
	expect(spatialObservationUsesSeenChannel(input), "visible audible door uses seen/heard channel not heard-only");
	input.tileVisible = false;
	expect(!spatialObservationUsesSeenChannel(input), "off-screen door stays heard-only");
}

static void testHeardOnlyOmitsObserverAttribution()
{
	expect(!CompanionPerceptionLogCore::shouldExportObserverAttribution(false, true),
		"heard-only events must not export observerUnitId even if flagged");
	expect(CompanionPerceptionLogCore::shouldExportObserverAttribution(true, true),
		"visible events may export observer when known");
	expect(!CompanionPerceptionLogCore::shouldExportObserverAttribution(true, false),
		"missing observer must not export");
}

static void testPsychDedupeUsesObservationSerial()
{
	const std::string first = CompanionPerceptionLogCore::makePsychologicalObservationDedupeKey(
		"psi_morale_attack_observed", 1, 3, 1);
	const std::string second = CompanionPerceptionLogCore::makePsychologicalObservationDedupeKey(
		"psi_morale_attack_observed", 1, 3, 2);
	const std::string repeat = CompanionPerceptionLogCore::makePsychologicalObservationDedupeKey(
		"psi_morale_attack_observed", 1, 3, 1);
	expect(first != second, "distinct observation serials must not dedupe each other");
	expect(first == repeat, "same observation serial must dedupe duplicate hook processing");
}

static void testPsychPanicEpisodeDedupesAcrossSerials()
{
	const std::string serialOne = CompanionPerceptionLogCore::makePsychologicalObservationDedupeKey(
		"soldier_panicked_observed", 1, 3, 1);
	const std::string serialTwo = CompanionPerceptionLogCore::makePsychologicalObservationDedupeKey(
		"soldier_panicked_observed", 1, 3, 2);
	expect(serialOne == serialTwo, "panic episode dedupe must ignore observationSerial");
}

static void testHiddenAlienPanicOmitsSubjectIdExport()
{
	PsychNotificationInput input = {};
	input.debugMode = false;
	input.uiNotificationShown = true;
	input.subjectVisible = false;
	expect(!shouldExportPsychologicalSubjectUnitId(input, "alien_panicked_observed", false),
		"hidden alien panic must not export subjectUnitId");
	input.subjectVisible = true;
	expect(shouldExportPsychologicalSubjectUnitId(input, "alien_panicked_observed", false),
		"visible alien panic may export subjectUnitId");
	expect(shouldExportPsychologicalSubjectUnitId(input, "soldier_panicked_observed", true),
		"roster soldier panic exports subjectUnitId when notified");
}

static void testPsychNotificationEligibility()
{
	PsychNotificationInput input = {};
	input.debugMode = false;
	input.uiNotificationShown = true;
	expect(shouldRecordPsychologicalNotification(input), "ui notification shown should record");
	input.uiNotificationShown = false;
	expect(!shouldRecordPsychologicalNotification(input), "hidden notification must not record");
	input.debugMode = true;
	input.uiNotificationShown = true;
	expect(!shouldRecordPsychologicalNotification(input), "debug mode must not record");
}

static void testPsychEventTypeRegistry()
{
	expect(CompanionPerceptionLogCore::isPsychologicalPerceptionEventType("soldier_panicked_observed"),
		"soldier panic type registered");
	expect(!CompanionPerceptionLogCore::isPsychologicalPerceptionEventType("gunfire_observed"),
		"non-psych types excluded");
}

static void testGameplayRngUntouched()
{
	RNG::setSeed(0xABCDEF1234567890ULL);
	const uint64_t before = RNG::getSeed();
	GunfireInput input = {};
	input.audible = true;
	(void)shouldRecordGunfire(input);
	const uint64_t after = RNG::getSeed();
	expect(before == after, "perception eligibility must not touch gameplay RNG");
}

static void testProjectileDirectionSeToNw()
{
	std::vector<Position> samples;
	samples.push_back(Position(2, 2, 0));
	samples.push_back(Position(0, 0, 0));
	const ProjectileDirectionResolution resolution = resolveProjectileDirectionFromOrderedSamples(samples);
	expect(resolution.emitEvent, "visible segment should emit");
	expect(resolution.trajectoryKnown, "two samples establish direction");
	expect(resolution.travelDirection == "northwest", "travel northwest");
	expect(resolution.incomingBearing == "southeast", "incoming southeast");
}

static void testProjectileDirectionReversedBearing()
{
	std::vector<Position> samples;
	samples.push_back(Position(0, 0, 0));
	samples.push_back(Position(2, 2, 0));
	const ProjectileDirectionResolution resolution = resolveProjectileDirectionFromOrderedSamples(samples);
	expect(resolution.trajectoryKnown, "reverse path still observed");
	expect(resolution.travelDirection == "southeast", "travel southeast");
	expect(resolution.incomingBearing == "northwest", "incoming northwest");
}

static void testProjectileDirectionSingleSampleUnavailable()
{
	std::vector<Position> samples;
	samples.push_back(Position(4, 4, 0));
	const ProjectileDirectionResolution resolution = resolveProjectileDirectionFromOrderedSamples(samples);
	expect(resolution.emitEvent, "single sample still reports");
	expect(!resolution.trajectoryKnown, "one sample is insufficient for direction");
	expect(resolution.positionKnown, "reference tile may be known");
}

static void testProjectileDirectionNoSamples()
{
	std::vector<Position> samples;
	const ProjectileDirectionResolution resolution = resolveProjectileDirectionFromOrderedSamples(samples);
	expect(!resolution.emitEvent, "invisible projectile emits nothing");
}

static void testProjectileSampleViewportFairness()
{
	ProjectileDirectionSampleInput input = {};
	input.debugMode = false;
	input.projectileInFov = true;
	input.tileInFriendlySight = true;
	input.voxelRenderable = true;
	input.onTacticalViewport = false;
	expect(!shouldAcceptProjectileDirectionSample(input), "off-viewport voxel must not sample");
	input.onTacticalViewport = true;
	expect(shouldAcceptProjectileDirectionSample(input), "on-viewport voxel may sample");
}

static void testProjectileSampleHiddenMovementGate()
{
	ProjectileDirectionSampleInput input = {};
	input.projectileInFov = false;
	input.tileInFriendlySight = true;
	input.voxelRenderable = true;
	input.onTacticalViewport = true;
	expect(!shouldAcceptProjectileDirectionSample(input), "projectile outside FOV gate must not sample");
}

static void testProjectileImpactVisibleUnitRequiresVictimVisible()
{
	ProjectileImpactExportInput input = {};
	input.tileVisible = true;
	input.voxelPart = COMPANION_VOXEL_PART_UNIT;
	input.victimUnitVisible = false;
	ProjectileImpactExportResolution res = resolveProjectileImpactExport(input);
	expect(res.shouldEmit, "visible tile impact emits");
	expect(res.positionKnown, "tile visible");
	expect(std::strcmp(res.confidenceId, "impact_visible") == 0,
		"V_UNIT without visible victim uses generic visible impact (fair-play)");
}

static void testProjectileImpactUnitHitClassification()
{
	ProjectileImpactExportInput input = {};
	input.tileVisible = true;
	input.voxelPart = COMPANION_VOXEL_PART_UNIT;
	input.victimUnitVisible = true;
	ProjectileImpactExportResolution res = resolveProjectileImpactExport(input);
	expect(res.shouldEmit && std::strcmp(res.confidenceId, "visible_unit_hit") == 0,
		"visible victim hit classified");
}

static void testProjectileImpactUnobservableSilent()
{
	ProjectileImpactExportInput input = {};
	input.audible = false;
	ProjectileImpactExportResolution res = resolveProjectileImpactExport(input);
	expect(!res.shouldEmit, "off-LOS inaudible impact omitted");
}

static void testProjectileImpactDedupeKeyPerOrdinal()
{
	expect(makeProjectileImpactDedupeKey(3, 7, 1) != makeProjectileImpactDedupeKey(3, 7, 2),
		"auto burst ordinals must not collide");
}

static void testSoldierCasualtyDedupeKeysDistinct()
{
	const std::string down = makeSoldierCasualtyEpisodeDedupeKey("soldier_down", 3, 1, "unconscious");
	const std::string recovered = makeSoldierCasualtyEpisodeDedupeKey("soldier_recovered", 3, 1, "standing");
	expect(down != recovered, "down vs recovered dedupe distinct");
}

static void testSoldierCasualtyEpisodeSerialAllowsRepeatKnockout()
{
	const std::string first = makeSoldierCasualtyEpisodeDedupeKey("soldier_down", 8, 1, "unconscious");
	const std::string second = makeSoldierCasualtyEpisodeDedupeKey("soldier_down", 8, 2, "unconscious");
	expect(first != second, "second unconscious episode must not reuse first dedupe key");
}

static void testSoldierCasualtyUnconsciousToDeadSameEpisode()
{
	const std::string unconscious = makeSoldierCasualtyEpisodeDedupeKey("soldier_down", 4, 2, "unconscious");
	const std::string dead = makeSoldierCasualtyEpisodeDedupeKey("soldier_down", 4, 2, "dead");
	expect(unconscious != dead, "unconscious then dead in same episode uses distinct keys");
}

static void testProjectileImpactTerrainStillClassified()
{
	ProjectileImpactExportInput input = {};
	input.tileVisible = true;
	input.voxelPart = 0;
	ProjectileImpactExportResolution res = resolveProjectileImpactExport(input);
	expect(std::strcmp(res.confidenceId, "impact_visible") == 0,
		"floor voxel uses same generic visible impact");
}

static void testProjectileImpactHiddenUnitMatchesTerrainFairPlay()
{
	ProjectileImpactExportInput terrain = {};
	terrain.tileVisible = true;
	terrain.voxelPart = 0;
	ProjectileImpactExportResolution terrainRes = resolveProjectileImpactExport(terrain);

	ProjectileImpactExportInput hiddenUnit = {};
	hiddenUnit.tileVisible = true;
	hiddenUnit.voxelPart = COMPANION_VOXEL_PART_UNIT;
	hiddenUnit.victimUnitVisible = false;
	ProjectileImpactExportResolution hiddenRes = resolveProjectileImpactExport(hiddenUnit);

	expect(std::strcmp(terrainRes.confidenceId, hiddenRes.confidenceId) == 0,
		"hidden unit impact must match terrain impact classification");
	expect(terrainRes.positionKnown == hiddenRes.positionKnown,
		"player-visible position exposure must match");
}

static void testCombatCasualtyEventRegistry()
{
	expect(CompanionPerceptionLogCore::isCombatCasualtyPerceptionEventType("soldier_down"),
		"soldier_down registered");
	expect(!CompanionPerceptionLogCore::isCombatCasualtyPerceptionEventType("impact_observed"),
		"impact not casualty type");
}

static void testExplosionOriginVisibleDeathDetonation()
{
	ExplosionOriginExportInput input = {};
	input.unitDeathDetonationEngineContext = true;
	input.blastLegitimatelyObservable = true;
	input.sourceUnitCurrentlyVisible = true;
	ExplosionOriginExportResolution res = resolveExplosionOriginExport(input);
	expect(res.explosionOriginClass != 0 && std::strcmp(res.explosionOriginClass, "unit_death_detonation") == 0,
		"visible death detonation exports class");
	expect(res.explosionOriginClassSource != 0 && std::strcmp(res.explosionOriginClassSource, "engine_state") == 0,
		"origin source is engine_state");
}

static void testExplosionOriginHiddenSourceOmitsClass()
{
	ExplosionOriginExportInput input = {};
	input.unitDeathDetonationEngineContext = true;
	input.blastLegitimatelyObservable = true;
	input.sourceUnitCurrentlyVisible = false;
	ExplosionOriginExportResolution res = resolveExplosionOriginExport(input);
	expect(res.explosionOriginClass == 0 || res.explosionOriginClass[0] == '\0',
		"hidden source must not export death detonation class");
}

static void testExplosionOriginAudioOnlyOmitsClass()
{
	ExplosionOriginExportInput input = {};
	input.unitDeathDetonationEngineContext = true;
	input.blastLegitimatelyObservable = false;
	input.sourceUnitCurrentlyVisible = true;
	ExplosionOriginExportResolution res = resolveExplosionOriginExport(input);
	expect(res.explosionOriginClass == 0 || res.explosionOriginClass[0] == '\0',
		"non-observable blast must not export class");
}

static void testExplosionOriginGrenadeContextOmitsClass()
{
	ExplosionOriginExportInput input = {};
	input.unitDeathDetonationEngineContext = false;
	input.blastLegitimatelyObservable = true;
	input.sourceUnitCurrentlyVisible = true;
	ExplosionOriginExportResolution res = resolveExplosionOriginExport(input);
	expect(res.explosionOriginClass == 0 || res.explosionOriginClass[0] == '\0',
		"non-death engine context must not export class");
}

static void testRosterCasualtySubjectAcceptsPostKiaIdentity()
{
	using CompanionPerceptionLogCore::RosterCasualtySubjectInput;
	RosterCasualtySubjectInput dead = {};
	dead.unitId = 4;
	dead.hasGeoscapeSoldierLink = true;
	dead.geoscapeSoldierId = 4;
	dead.originalFaction = 0;
	expect(CompanionPerceptionLogCore::isOriginalXcomRosterCasualtySubject(dead),
		"KIA roster soldier with geoscape link and original player faction");

	RosterCasualtySubjectInput mc = dead;
	mc.unitId = 2;
	mc.geoscapeSoldierId = 2;
	expect(CompanionPerceptionLogCore::isOriginalXcomRosterCasualtySubject(mc),
		"mind-controlled original X-COM still casualty subject");

	RosterCasualtySubjectInput alien = {};
	alien.unitId = 1000007;
	alien.hasGeoscapeSoldierLink = false;
	alien.originalFaction = 1;
	expect(!CompanionPerceptionLogCore::isOriginalXcomRosterCasualtySubject(alien),
		"hostile alien rejected");

	RosterCasualtySubjectInput mismatch = dead;
	mismatch.geoscapeSoldierId = 9;
	expect(!CompanionPerceptionLogCore::isOriginalXcomRosterCasualtySubject(mismatch),
		"id mismatch rejected");
}

#ifdef main
#undef main
#endif
int main()
{
	testPsychDedupeUsesObservationSerial();
	testPsychPanicEpisodeDedupesAcrossSerials();
	testHiddenAlienPanicOmitsSubjectIdExport();
	testPsychNotificationEligibility();
	testPsychEventTypeRegistry();
	testHiddenGunfireOmitsPosition();
	testVisibleGunfireIncludesPosition();
	testAudioOnlySpatialOmitsPosition();
	testUnitLastSeenTransitions();
	testUnitLastSeenExportNovosibirskTurnBoundaryMemory();
	testUnitLastSeenExportRequiresStoredMemory();
	testGunfireSubjectRequiresVisibleShooter();
	testHiddenThrowUsesGenericAudioType();
	testVisibleGrenadeThrowClassified();
	testHiddenThrowerVisibleTileDoesNotClaimGrenade();
	testInaudibleHiddenThrowSilent();
	testVisibleLaunchClassified();
	testNonGrenadeThrowObjectType();
	testThrowSubjectRequiresVisibleShooter();
	testVisibleDoorUsesSeenChannel();
	testHeardOnlyOmitsObserverAttribution();
	testGameplayRngUntouched();
	testProjectileDirectionSeToNw();
	testProjectileDirectionReversedBearing();
	testProjectileDirectionSingleSampleUnavailable();
	testProjectileDirectionNoSamples();
	testProjectileSampleViewportFairness();
	testProjectileSampleHiddenMovementGate();
	testProjectileImpactVisibleUnitRequiresVictimVisible();
	testProjectileImpactUnitHitClassification();
	testProjectileImpactUnobservableSilent();
	testProjectileImpactDedupeKeyPerOrdinal();
	testSoldierCasualtyDedupeKeysDistinct();
	testSoldierCasualtyEpisodeSerialAllowsRepeatKnockout();
	testSoldierCasualtyUnconsciousToDeadSameEpisode();
	testProjectileImpactTerrainStillClassified();
	testProjectileImpactHiddenUnitMatchesTerrainFairPlay();
	testCombatCasualtyEventRegistry();
	testRosterCasualtySubjectAcceptsPostKiaIdentity();
	testExplosionOriginVisibleDeathDetonation();
	testExplosionOriginHiddenSourceOmitsClass();
	testExplosionOriginAudioOnlyOmitsClass();
	testExplosionOriginGrenadeContextOmitsClass();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_perception_log_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
