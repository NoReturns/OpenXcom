/*
 * Open X-Com Companion — player-perceivable battle event recording (M1-WP6).
 */
#include "CompanionPerceptionLog.h"
#include "CompanionPerceptionLogCore.h"
#include "CompanionSnapshotUnitStateCore.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Tile.h"
#include "../Battlescape/TileEngine.h"
#include "../Battlescape/BattlescapeGame.h"
#include <cstring>

namespace OpenXcom
{

namespace
{

bool shouldRecordPerception(SavedBattleGame *battle)
{
	return battle != 0 && !battle->getDebugMode();
}

bool isHostileUnitCurrentlySeen(const BattleUnit *hostile)
{
	return hostile != 0 && hostile->getVisible();
}

bool isLiveFriendlyObserver(const BattleUnit *unit, SavedBattleGame *battle)
{
	if (unit == 0 || battle == 0 || unit->isOut())
	{
		return false;
	}
	if (unit->getOriginalFaction() != FACTION_PLAYER || unit->getFaction() != FACTION_PLAYER)
	{
		return false;
	}
	return unit->getGeoscapeSoldier() != 0;
}

bool isTileCurrentlySeenByAnyFriendly(SavedBattleGame *battle, const Position &pos)
{
	if (battle == 0)
	{
		return false;
	}
	Tile *tile = battle->getTile(pos);
	return tile != 0 && tile->getVisible() > 0;
}

BattleUnit *findUnitById(SavedBattleGame *battle, int unitId)
{
	if (battle == 0 || unitId < 0)
	{
		return 0;
	}
	for (std::vector<BattleUnit*>::iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		if ((*i)->getId() == unitId)
		{
			return *i;
		}
	}
	return 0;
}

bool pickObserverToward(
	TileEngine *tileEngine,
	SavedBattleGame *battle,
	const Position &target,
	int *outObserverUnitId)
{
	if (tileEngine == 0 || battle == 0 || outObserverUnitId == 0)
	{
		return false;
	}
	Tile *targetTile = battle->getTile(target);
	if (targetTile == 0)
	{
		return false;
	}
	for (std::vector<BattleUnit*>::iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		BattleUnit *observer = *i;
		if (!isLiveFriendlyObserver(observer, battle) || !tileEngine->visible(observer, targetTile))
		{
			continue;
		}
		*outObserverUnitId = observer->getId();
		return true;
	}
	return false;
}

CompanionPerceptionLogCore::ObserverResolution resolveSpatialObservation(
	SavedBattleGame *battle,
	TileEngine *tileEngine,
	const Position &pos,
	bool audible)
{
	CompanionPerceptionLogCore::ObserverResolution out = {};
	out.position = pos;
	out.positionKnown = isTileCurrentlySeenByAnyFriendly(battle, pos);
	const bool seen = out.positionKnown;
	if (seen && audible)
	{
		out.channel = CompanionPerceptionLogCore::PERCEPTION_SEEN_AND_HEARD;
	}
	else if (seen)
	{
		out.channel = CompanionPerceptionLogCore::PERCEPTION_SEEN;
	}
	else
	{
		out.channel = CompanionPerceptionLogCore::PERCEPTION_HEARD;
	}
	if (out.positionKnown)
	{
		int observerId = -1;
		if (pickObserverToward(tileEngine, battle, pos, &observerId))
		{
			out.hasObserver = true;
			out.observerUnitId = observerId;
			BattleUnit *observer = findUnitById(battle, observerId);
			if (observer != 0)
			{
				out.hasCompassDirection = true;
				out.compassDirection = tileEngine->getDirectionTo(observer->getPosition(), pos);
			}
		}
	}
	return out;
}

CompanionPerceptionLogCore::ObserverResolution resolveGunfireObservation(
	SavedBattleGame *battle,
	TileEngine *tileEngine,
	BattleUnit *shooter,
	const Position &origin,
	bool audible)
{
	CompanionPerceptionLogCore::ObserverResolution out = {};
	out.position = origin;
	const bool shooterSeen = isHostileUnitCurrentlySeen(shooter);
	const bool originSeen = isTileCurrentlySeenByAnyFriendly(battle, origin);
	out.positionKnown = shooterSeen || originSeen;
	const bool seen = out.positionKnown;
	if (seen && audible)
	{
		out.channel = CompanionPerceptionLogCore::PERCEPTION_SEEN_AND_HEARD;
	}
	else if (seen)
	{
		out.channel = CompanionPerceptionLogCore::PERCEPTION_SEEN;
	}
	else
	{
		out.channel = CompanionPerceptionLogCore::PERCEPTION_HEARD;
	}
	if (out.positionKnown)
	{
		const Position bearingFrom = shooterSeen && shooter != 0 ? shooter->getPosition() : origin;
		int observerId = -1;
		if (pickObserverToward(tileEngine, battle, bearingFrom, &observerId))
		{
			out.hasObserver = true;
			out.observerUnitId = observerId;
			BattleUnit *observer = findUnitById(battle, observerId);
			if (observer != 0)
			{
				out.hasCompassDirection = true;
				out.compassDirection = tileEngine->getDirectionTo(observer->getPosition(), bearingFrom);
			}
		}
	}
	return out;
}

void applyObserverResolution(CompanionPerceptionEvent &entry, const CompanionPerceptionLogCore::ObserverResolution &obs)
{
	entry.perceptionChannel = CompanionPerceptionLogCore::perceptionChannelId(obs.channel);
	entry.positionKnown = obs.positionKnown;
	entry.position = obs.position;
	if (CompanionPerceptionLogCore::shouldExportObserverAttribution(obs.positionKnown, obs.hasObserver))
	{
		entry.hasObserverUnitId = true;
		entry.observerUnitId = obs.observerUnitId;
	}
	if (obs.hasCompassDirection)
	{
		const char *label = CompanionSnapshotUnitStateCore::directionLabel(obs.compassDirection);
		if (label != 0)
		{
			entry.compassDirection = label;
		}
	}
}

} // namespace

CompanionPerceptionLog::CompanionPerceptionLog()
	: _nextSeq(1), _droppedCount(0), _nextProjectileFlightId(1), _nextPsychObservationSerial(0)
{
	_projectileDirectionFlight.active = false;
}

void CompanionPerceptionLog::beginSession(const std::string &sessionId)
{
	_sessionId = sessionId;
	_events.clear();
	_nextSeq = 1;
	_droppedCount = 0;
	_dedupeKeys.clear();
	_hostileWasVisible.clear();
	_hostileLastSeenPosition.clear();
	_rosterCasualtyState.clear();
	_rosterCasualtyEpisodeSerial.clear();
	_rosterCasualtyLedger.clear();
	_projectileDirectionFlight.active = false;
	_projectileDirectionFlight.orderedTileSamples.clear();
	_projectileDirectionFlight.nextImpactOrdinal = 0;
	_nextPsychObservationSerial = 0;
}

void CompanionPerceptionLog::syncSessionId(const std::string &sessionId)
{
	_sessionId = sessionId;
}

void CompanionPerceptionLog::appendEvent(SavedBattleGame *battle, const CompanionPerceptionEvent &entry, const std::string &dedupeKey)
{
	if (battle == 0 || !shouldRecordPerception(battle))
	{
		return;
	}
	if (_dedupeKeys.find(dedupeKey) != _dedupeKeys.end())
	{
		return;
	}
	if (_events.size() >= MAX_RETAINED)
	{
		const std::string droppedKey = _events.front().type + ":" + std::to_string(_events.front().seq);
		_dedupeKeys.erase(droppedKey);
		_events.erase(_events.begin());
		++_droppedCount;
	}
	CompanionPerceptionEvent stored = entry;
	stored.seq = _nextSeq++;
	_events.push_back(stored);
	_dedupeKeys.insert(dedupeKey);
}

void CompanionPerceptionLog::appendUnitLastSeen(SavedBattleGame *battle, BattleUnit *hostile, const char *confidence)
{
	if (hostile == 0 || hostile->getOriginalFaction() != FACTION_HOSTILE)
	{
		return;
	}
	const Position currentPos = hostile->getPosition();
	CompanionPerceptionLogCore::UnitLastSeenExportInput exportInput = {};
	exportInput.confidence = confidence;
	exportInput.currentHostilePosition = currentPos;
	exportInput.hostileTileCurrentlyVisible = isTileCurrentlySeenByAnyFriendly(battle, currentPos);
	const std::map<int, Position>::const_iterator stored = _hostileLastSeenPosition.find(hostile->getId());
	exportInput.hasStoredLastPosition = stored != _hostileLastSeenPosition.end();
	if (exportInput.hasStoredLastPosition)
	{
		exportInput.storedLastPosition = stored->second;
	}
	const CompanionPerceptionLogCore::UnitLastSeenExportResolution exportResolution =
		CompanionPerceptionLogCore::resolveUnitLastSeenExport(exportInput);
	if (!exportResolution.shouldEmit)
	{
		return;
	}
	CompanionPerceptionEvent entry{};
	entry.type = "unit_last_seen";
	entry.turn = battle->getTurn();
	entry.confidence = confidence;
	entry.hasSubjectUnitId = true;
	entry.subjectUnitId = hostile->getId();
	entry.perceptionChannel =
		CompanionPerceptionLogCore::perceptionChannelId(exportResolution.channel);
	entry.positionKnown = exportResolution.positionKnown;
	entry.position = exportResolution.exportPosition;
	TileEngine *tileEngine = battle->getTileEngine();
	if (tileEngine != 0 && isTileCurrentlySeenByAnyFriendly(battle, exportResolution.exportPosition))
	{
		int observerId = -1;
		if (pickObserverToward(tileEngine, battle, exportResolution.exportPosition, &observerId))
		{
			entry.hasObserverUnitId = true;
			entry.observerUnitId = observerId;
			BattleUnit *observer = findUnitById(battle, observerId);
			if (observer != 0)
			{
				const int compassDirection = tileEngine->getDirectionTo(observer->getPosition(), exportResolution.exportPosition);
				const char *label = CompanionSnapshotUnitStateCore::directionLabel(compassDirection);
				if (label != 0)
				{
					entry.compassDirection = label;
				}
			}
		}
	}
	const std::string dedupeKey = std::string(confidence) + ":" + CompanionPerceptionLogCore::makeDedupeKey(
		entry.type.c_str(),
		entry.turn,
		entry.subjectUnitId,
		&exportResolution.exportPosition,
		exportResolution.positionKnown,
		exportResolution.channel);
	appendEvent(battle, entry, dedupeKey);
	if (std::strcmp(confidence, "sighted") == 0)
	{
		_hostileLastSeenPosition[hostile->getId()] = exportResolution.exportPosition;
	}
}

void CompanionPerceptionLog::tryRecordGunfireObserved(SavedBattleGame *battle, BattleUnit *shooter, const Position &origin, bool audible)
{
	if (!audible)
	{
		return;
	}
	TileEngine *tileEngine = battle != 0 ? battle->getTileEngine() : 0;
	CompanionPerceptionLogCore::ObserverResolution obs =
		resolveGunfireObservation(battle, tileEngine, shooter, origin, audible);
	CompanionPerceptionEvent entry{};
	entry.type = "gunfire_observed";
	entry.turn = battle->getTurn();
	entry.confidence = obs.positionKnown ? "source_visible" : "audio_only";
	if (shooter != 0 && shooter->getOriginalFaction() == FACTION_HOSTILE && isHostileUnitCurrentlySeen(shooter))
	{
		entry.hasSubjectUnitId = true;
		entry.subjectUnitId = shooter->getId();
	}
	applyObserverResolution(entry, obs);
	const int subjectId = entry.hasSubjectUnitId ? entry.subjectUnitId : -1;
	const std::string dedupeKey = CompanionPerceptionLogCore::makeDedupeKey(
		entry.type.c_str(),
		entry.turn,
		subjectId,
		obs.positionKnown ? &obs.position : 0,
		obs.positionKnown,
		obs.channel);
	appendEvent(battle, entry, dedupeKey);
}

void CompanionPerceptionLog::tryRecordImpactObserved(SavedBattleGame *battle, const Position &impactTile, bool audible)
{
	tryRecordProjectileImpactObserved(battle, impactTile, audible, -1, 0, 0, 0);
}

void CompanionPerceptionLog::tryRecordProjectileImpactObserved(
	SavedBattleGame *battle,
	const Position &impactMapTile,
	bool audible,
	int voxelPart,
	BattleUnit *victimUnit,
	int flightId,
	int impactOrdinal)
{
	if (battle == 0)
	{
		return;
	}
	const bool tileVisible = isTileCurrentlySeenByAnyFriendly(battle, impactMapTile);
	const bool victimVisible = victimUnit != 0 && victimUnit->getVisible();
	CompanionPerceptionLogCore::ProjectileImpactExportInput exportInput = {};
	exportInput.debugMode = battle->getDebugMode();
	exportInput.tileVisible = tileVisible;
	exportInput.audible = audible;
	exportInput.voxelPart = voxelPart;
	exportInput.victimUnitVisible = victimVisible;
	const CompanionPerceptionLogCore::ProjectileImpactExportResolution exportResolution =
		CompanionPerceptionLogCore::resolveProjectileImpactExport(exportInput);
	if (!exportResolution.shouldEmit)
	{
		return;
	}
	TileEngine *tileEngine = battle->getTileEngine();
	CompanionPerceptionLogCore::ObserverResolution obs =
		resolveSpatialObservation(battle, tileEngine, impactMapTile, audible);
	CompanionPerceptionEvent entry{};
	entry.type = "impact_observed";
	entry.turn = battle->getTurn();
	entry.confidence = exportResolution.confidenceId != 0 ? exportResolution.confidenceId : "audio_only";
	if (!exportResolution.positionKnown)
	{
		obs.positionKnown = false;
	}
	applyObserverResolution(entry, obs);
	const std::string dedupeKey = flightId > 0 && impactOrdinal > 0
		? CompanionPerceptionLogCore::makeProjectileImpactDedupeKey(entry.turn, flightId, impactOrdinal)
		: CompanionPerceptionLogCore::makeDedupeKey(
			entry.type.c_str(),
			entry.turn,
			-1,
			exportResolution.positionKnown ? &impactMapTile : 0,
			exportResolution.positionKnown,
			obs.channel);
	appendEvent(battle, entry, dedupeKey);
}

void CompanionPerceptionLog::tryRecordExplosionObservedOrHeard(SavedBattleGame *battle, const Position &centerTile, bool audible)
{
	TileEngine *tileEngine = battle != 0 ? battle->getTileEngine() : 0;
	CompanionPerceptionLogCore::ObserverResolution obs =
		resolveSpatialObservation(battle, tileEngine, centerTile, audible);
	CompanionPerceptionEvent entry{};
	entry.type = "explosion_observed_or_heard";
	entry.turn = battle->getTurn();
	entry.confidence = obs.positionKnown ? "blast_visible" : "audio_only";
	applyObserverResolution(entry, obs);
	const std::string dedupeKey = CompanionPerceptionLogCore::makeDedupeKey(
		entry.type.c_str(),
		entry.turn,
		-1,
		obs.positionKnown ? &centerTile : 0,
		obs.positionKnown,
		obs.channel);
	appendEvent(battle, entry, dedupeKey);
}

void CompanionPerceptionLog::tryRecordDoorSoundHeard(SavedBattleGame *battle, const Position &doorTile)
{
	TileEngine *tileEngine = battle != 0 ? battle->getTileEngine() : 0;
	CompanionPerceptionLogCore::ObserverResolution obs =
		resolveSpatialObservation(battle, tileEngine, doorTile, true);
	CompanionPerceptionEvent entry{};
	entry.type = "door_sound_heard";
	entry.turn = battle->getTurn();
	entry.confidence = obs.positionKnown ? "door_visible" : "audio_only";
	applyObserverResolution(entry, obs);
	const std::string dedupeKey = CompanionPerceptionLogCore::makeDedupeKey(
		entry.type.c_str(),
		entry.turn,
		-1,
		obs.positionKnown ? &doorTile : 0,
		obs.positionKnown,
		obs.channel);
	appendEvent(battle, entry, dedupeKey);
}

void CompanionPerceptionLog::tryRecordHostileDeathObserved(
	SavedBattleGame *battle,
	const Position &deathTile,
	bool hadCarriedInventory)
{
	if (battle == 0 || !shouldRecordPerception(battle))
	{
		return;
	}
	TileEngine *tileEngine = battle->getTileEngine();
	CompanionPerceptionLogCore::ObserverResolution obs =
		resolveSpatialObservation(battle, tileEngine, deathTile, false);
	if (!obs.positionKnown)
	{
		return;
	}
	CompanionPerceptionEvent entry{};
	entry.type = "hostile_casualty_tile_observed";
	entry.turn = battle->getTurn();
	entry.confidence = hadCarriedInventory ? "possible_ground_equipment" : "alien_casualty_tile";
	entry.hasSubjectUnitId = false;
	applyObserverResolution(entry, obs);
	const std::string dedupeKey = CompanionPerceptionLogCore::makeDedupeKey(
		entry.type.c_str(),
		entry.turn,
		-1,
		&deathTile,
		true,
		obs.channel);
	appendEvent(battle, entry, dedupeKey);
}

void CompanionPerceptionLog::noteHostileVisibility(SavedBattleGame *battle, BattleUnit *hostile, bool nowVisible)
{
	if (hostile == 0 || hostile->getOriginalFaction() != FACTION_HOSTILE)
	{
		return;
	}
	const int id = hostile->getId();
	const bool wasVisible = _hostileWasVisible.find(id) != _hostileWasVisible.end() && _hostileWasVisible[id];
	if (nowVisible && !wasVisible)
	{
		appendUnitLastSeen(battle, hostile, "sighted");
	}
	else if (wasVisible && nowVisible)
	{
		_hostileLastSeenPosition[id] = hostile->getPosition();
	}
	_hostileWasVisible[id] = nowVisible;
}

void CompanionPerceptionLog::recordHostileLastSeenBeforeTurnReset(SavedBattleGame *battle, BattleUnit *hostile)
{
	if (hostile == 0 || hostile->getOriginalFaction() != FACTION_HOSTILE)
	{
		return;
	}
	const int id = hostile->getId();
	const bool wasVisible = _hostileWasVisible.find(id) != _hostileWasVisible.end() && _hostileWasVisible[id];
	if (!wasVisible)
	{
		return;
	}
	appendUnitLastSeen(battle, hostile, "last_seen");
	_hostileWasVisible[id] = false;
}

int CompanionPerceptionLog::getOldestSeq() const
{
	if (_events.empty())
	{
		return 0;
	}
	return _events.front().seq;
}

int CompanionPerceptionLog::getLatestSeq() const
{
	if (_events.empty())
	{
		return 0;
	}
	return _events.back().seq;
}

void CompanionPerceptionLog::beginProjectileDirectionObservation(SavedBattleGame *battle, int actorUnitId, int autoShotCounter, int actionType)
{
	if (!shouldRecordPerception(battle))
	{
		return;
	}
	if (actionType == BA_THROW || actionType == BA_LAUNCH)
	{
		return;
	}
	_projectileDirectionFlight.active = true;
	_projectileDirectionFlight.turn = battle->getTurn();
	_projectileDirectionFlight.actorUnitId = actorUnitId;
	_projectileDirectionFlight.autoShotCounter = autoShotCounter;
	_projectileDirectionFlight.actionType = actionType;
	_projectileDirectionFlight.flightId = _nextProjectileFlightId++;
	_projectileDirectionFlight.nextImpactOrdinal = 0;
	_projectileDirectionFlight.orderedTileSamples.clear();
}

int CompanionPerceptionLog::allocateProjectileImpactOrdinal()
{
	if (!_projectileDirectionFlight.active)
	{
		return 0;
	}
	return ++_projectileDirectionFlight.nextImpactOrdinal;
}

int CompanionPerceptionLog::getActiveProjectileFlightId() const
{
	return _projectileDirectionFlight.active ? _projectileDirectionFlight.flightId : 0;
}

void CompanionPerceptionLog::noteProjectileDirectionSample(SavedBattleGame *battle, const Position &mapTile)
{
	if (!_projectileDirectionFlight.active || !shouldRecordPerception(battle))
	{
		return;
	}
	std::vector<Position> &samples = _projectileDirectionFlight.orderedTileSamples;
	if (!samples.empty() &&
	    samples.back().x == mapTile.x &&
	    samples.back().y == mapTile.y &&
	    samples.back().z == mapTile.z)
	{
		return;
	}
	samples.push_back(mapTile);
}

void CompanionPerceptionLog::finalizeProjectileDirectionObservation(SavedBattleGame *battle)
{
	if (!_projectileDirectionFlight.active || !shouldRecordPerception(battle))
	{
		_projectileDirectionFlight.active = false;
		_projectileDirectionFlight.orderedTileSamples.clear();
		return;
	}
	const ProjectileDirectionFlight flight = _projectileDirectionFlight;
	_projectileDirectionFlight.active = false;
	_projectileDirectionFlight.orderedTileSamples.clear();

	const CompanionPerceptionLogCore::ProjectileDirectionResolution resolution =
		CompanionPerceptionLogCore::resolveProjectileDirectionFromOrderedSamples(flight.orderedTileSamples);
	if (!resolution.emitEvent)
	{
		return;
	}
	CompanionPerceptionEvent entry{};
	entry.type = "projectile_direction_observed";
	entry.turn = flight.turn;
	entry.perceptionChannel = "seen";
	entry.positionKnown = resolution.positionKnown;
	entry.position = resolution.referenceTile;
	if (resolution.trajectoryKnown)
	{
		entry.confidence = "trajectory_observed";
		entry.travelDirection = resolution.travelDirection;
		entry.incomingBearing = resolution.incomingBearing;
	}
	else
	{
		entry.confidence = "trajectory_unavailable";
	}
	const std::string dedupeKey = CompanionPerceptionLogCore::makeProjectileDirectionDedupeKey(flight.turn, flight.flightId);
	appendEvent(battle, entry, dedupeKey);
}

int CompanionPerceptionLog::allocatePsychObservationSerial()
{
	return ++_nextPsychObservationSerial;
}

namespace
{

bool isLiveRosterSoldierIdentity(const BattleUnit *unit, const SavedGame *campaign)
{
	if (unit == 0 || campaign == 0)
	{
		return false;
	}
	Soldier *soldier = unit->getGeoscapeSoldier();
	if (soldier == 0 || soldier->getId() != unit->getId())
	{
		return false;
	}
	if (unit->getId() <= 0 || unit->getId() >= BattleUnit::MAX_SOLDIER_ID)
	{
		return false;
	}
	return campaign->isLiveRosterSoldier(unit->getId());
}

bool isOriginalXcomRosterCasualtySubjectUnit(const BattleUnit *unit)
{
	if (unit == 0)
	{
		return false;
	}
	Soldier *soldier = unit->getGeoscapeSoldier();
	CompanionPerceptionLogCore::RosterCasualtySubjectInput input = {};
	input.unitId = unit->getId();
	input.hasGeoscapeSoldierLink = soldier != 0;
	input.geoscapeSoldierId = soldier != 0 ? soldier->getId() : -1;
	input.originalFaction = static_cast<int>(unit->getOriginalFaction());
	return CompanionPerceptionLogCore::isOriginalXcomRosterCasualtySubject(input);
}

} // namespace

void CompanionPerceptionLog::seedRosterCasualtyLedgerFromLoadedBattle(SavedBattleGame *battle)
{
	if (battle == 0 || battle->getDebugMode())
	{
		return;
	}
	const SavedGame *campaign = battle->getGeoscapeSave();
	if (campaign == 0)
	{
		return;
	}
	for (std::vector<BattleUnit*>::iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		BattleUnit *unit = *i;
		if (!isOriginalXcomRosterCasualtySubjectUnit(unit))
		{
			continue;
		}
		const int subjectId = unit->getId();
		const bool dead = unit->getStatus() == STATUS_DEAD;
		const bool unconscious = unit->getStatus() == STATUS_UNCONSCIOUS;
		if (!dead && !unconscious)
		{
			continue;
		}
		const char *status = dead ? "dead" : "unconscious";
		const int episodeSerial = 1;
		_rosterCasualtyEpisodeSerial[subjectId] = episodeSerial;
		_rosterCasualtyState[subjectId] = dead ? 2 : 1;
		const Position pos = unit->getPosition();
		const bool positionKnown = unit->getVisible()
			|| isTileCurrentlySeenByAnyFriendly(battle, pos);
		std::string displayName;
		Soldier *geoscape = unit->getGeoscapeSoldier();
		if (geoscape != 0)
		{
			displayName = geoscape->getName(true);
		}
		syncRosterCasualtyLedger(
			subjectId,
			episodeSerial,
			status,
			battle->getTurn(),
			positionKnown,
			pos,
			displayName);
	}
}

void CompanionPerceptionLog::syncRosterCasualtyLedger(
	int subjectUnitId,
	int episodeSerial,
	const char *status,
	int turn,
	bool positionKnown,
	const Position &position,
	const std::string &subjectName)
{
	if (subjectUnitId <= 0 || status == 0 || status[0] == '\0')
	{
		return;
	}
	RosterCasualtyLedgerEntry entry;
	entry.subjectUnitId = subjectUnitId;
	entry.episodeSerial = episodeSerial;
	entry.status = status;
	entry.turn = turn;
	entry.positionKnown = positionKnown;
	entry.position = position;
	entry.subjectName = subjectName;
	_rosterCasualtyLedger[subjectUnitId] = entry;
}

void CompanionPerceptionLog::tryRecordSoldierDown(SavedBattleGame *battle, BattleUnit *soldier, bool uiNotificationShown)
{
	if (battle == 0 || soldier == 0 || battle->getDebugMode())
	{
		return;
	}
	if (!isOriginalXcomRosterCasualtySubjectUnit(soldier))
	{
		return;
	}
	const bool dead = soldier->getStatus() == STATUS_DEAD;
	const bool unconscious = soldier->getStatus() == STATUS_UNCONSCIOUS;
	if (!dead && !unconscious)
	{
		return;
	}
	const char *confidence = dead ? "dead" : "unconscious";
	const int subjectId = soldier->getId();
	int &state = _rosterCasualtyState[subjectId];
	int &episodeSerial = _rosterCasualtyEpisodeSerial[subjectId];
	if (dead)
	{
		if (state == 2)
		{
			return;
		}
		if (state == 0)
		{
			++episodeSerial;
		}
	}
	else
	{
		if (state >= 1)
		{
			return;
		}
		++episodeSerial;
	}

	const Position downPos = soldier->getPosition();
	const bool positionKnown = soldier->getVisible()
		|| isTileCurrentlySeenByAnyFriendly(battle, downPos);
	CompanionPerceptionEvent entry{};
	entry.type = "soldier_down";
	entry.turn = battle->getTurn();
	entry.confidence = confidence;
	entry.perceptionChannel = uiNotificationShown
		? "notification"
		: (positionKnown ? "seen" : "notification");
	if (positionKnown)
	{
		entry.positionKnown = true;
		entry.position = downPos;
	}
	entry.hasSubjectUnitId = true;
	entry.subjectUnitId = subjectId;
	std::string displayName;
	Soldier *geoscape = soldier->getGeoscapeSoldier();
	if (geoscape != 0)
	{
		displayName = geoscape->getName(true);
		entry.subjectName = displayName;
	}

	const std::string dedupeKey = CompanionPerceptionLogCore::makeSoldierCasualtyEpisodeDedupeKey(
		"soldier_down", subjectId, episodeSerial, confidence);
	appendEvent(battle, entry, dedupeKey);
	state = dead ? 2 : 1;
	syncRosterCasualtyLedger(
		subjectId,
		episodeSerial,
		confidence,
		entry.turn,
		positionKnown,
		downPos,
		displayName);
}

void CompanionPerceptionLog::tryRecordSoldierRecovered(SavedBattleGame *battle, BattleUnit *soldier)
{
	if (battle == 0 || soldier == 0 || battle->getDebugMode())
	{
		return;
	}
	if (!isOriginalXcomRosterCasualtySubjectUnit(soldier))
	{
		return;
	}
	const int subjectId = soldier->getId();
	std::map<int, int>::const_iterator found = _rosterCasualtyState.find(subjectId);
	if (found == _rosterCasualtyState.end() || found->second != 1)
	{
		return;
	}
	if (soldier->isOut())
	{
		return;
	}
	const int episodeSerial = _rosterCasualtyEpisodeSerial[subjectId];
	const Position pos = soldier->getPosition();
	const bool positionKnown = soldier->getVisible()
		|| isTileCurrentlySeenByAnyFriendly(battle, pos);
	CompanionPerceptionEvent entry{};
	entry.type = "soldier_recovered";
	entry.turn = battle->getTurn();
	entry.perceptionChannel = positionKnown ? "seen" : "notification";
	entry.confidence = "standing";
	entry.hasSubjectUnitId = true;
	entry.subjectUnitId = subjectId;
	if (positionKnown)
	{
		entry.positionKnown = true;
		entry.position = pos;
	}
	std::string displayName;
	Soldier *geoscape = soldier->getGeoscapeSoldier();
	if (geoscape != 0)
	{
		displayName = geoscape->getName(true);
		entry.subjectName = displayName;
	}
	const std::string dedupeKey = CompanionPerceptionLogCore::makeSoldierCasualtyEpisodeDedupeKey(
		"soldier_recovered", subjectId, episodeSerial, "standing");
	appendEvent(battle, entry, dedupeKey);
	_rosterCasualtyState[subjectId] = 0;
	syncRosterCasualtyLedger(
		subjectId,
		episodeSerial,
		"standing",
		entry.turn,
		positionKnown,
		pos,
		displayName);
}

void CompanionPerceptionLog::tryRecordPsychologicalUiNotification(
	SavedBattleGame *battle,
	BattleUnit *subject,
	const char *eventType,
	int observationSerial,
	bool uiNotificationShown,
	bool subjectVisible,
	const std::string &rosterDisplayName)
{
	if (battle == 0 || subject == 0 || eventType == 0
		|| !CompanionPerceptionLogCore::isPsychologicalPerceptionEventType(eventType))
	{
		return;
	}
	CompanionPerceptionLogTestSupport::PsychNotificationInput input = {};
	input.debugMode = battle->getDebugMode();
	input.uiNotificationShown = uiNotificationShown;
	input.subjectVisible = subjectVisible;
	if (!CompanionPerceptionLogTestSupport::shouldRecordPsychologicalNotification(input))
	{
		return;
	}

	CompanionPerceptionEvent entry{};
	entry.type = eventType;
	entry.turn = battle->getTurn();
	entry.perceptionChannel = CompanionPerceptionLogCore::psychologicalPerceptionChannelId();
	entry.confidence = CompanionPerceptionLogCore::psychologicalConfidenceId(subjectVisible);
	entry.positionKnown = false;
	const SavedGame *campaign = battle->getGeoscapeSave();
	const bool rosterSoldierIdentity = isLiveRosterSoldierIdentity(subject, campaign);
	if (CompanionPerceptionLogTestSupport::shouldExportPsychologicalSubjectUnitId(
		input, eventType, rosterSoldierIdentity))
	{
		entry.hasSubjectUnitId = true;
		entry.subjectUnitId = subject->getId();
	}
	if (CompanionPerceptionLogTestSupport::shouldExportPsychologicalSubjectName(
		rosterSoldierIdentity)
		&& !rosterDisplayName.empty())
	{
		entry.subjectName = rosterDisplayName;
	}

	const std::string dedupeKey = CompanionPerceptionLogCore::makePsychologicalObservationDedupeKey(
		eventType,
		subject->getId(),
		battle->getTurn(),
		observationSerial);
	appendEvent(battle, entry, dedupeKey);
}

}
