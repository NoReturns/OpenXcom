#pragma once
/*
 * Open X-Com Companion — bounded player-perceivable battle events (M1-WP6).
 */
#include "../Battlescape/Position.h"
#include "CompanionPerceptionLogCore.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace OpenXcom
{

class BattleUnit;
class BattleItem;
class SavedBattleGame;

struct RosterCasualtyLedgerEntry
{
	int subjectUnitId;
	int episodeSerial;
	std::string status;
	int turn;
	bool positionKnown;
	Position position;
	std::string subjectName;
};

struct CompanionPerceptionEvent
{
	int seq = 0;
	std::string type;
	int turn = 0;
	std::string perceptionChannel;
	bool positionKnown = false;
	Position position;
	int observerUnitId = -1;
	bool hasObserverUnitId = false;
	int subjectUnitId = -1;
	bool hasSubjectUnitId = false;
	std::string confidence;
	std::string compassDirection;
	std::string travelDirection;
	std::string incomingBearing;
	std::string subjectName;
	std::string explosionOriginClass;
	std::string explosionOriginClassSource;
	int attributedActorUnitId = -1;
	bool hasAttributedActorUnitId = false;
	std::string attributionScope;
};

class CompanionPerceptionLog
{
public:
	static const size_t MAX_RETAINED = 256;

	CompanionPerceptionLog();

	void beginSession(const std::string &sessionId);
	void syncSessionId(const std::string &sessionId);
	/** After battle save load: rebuild ledger from live roster posture without historical perception events. */
	void seedRosterCasualtyLedgerFromLoadedBattle(SavedBattleGame *battle);

	void tryRecordGunfireObserved(SavedBattleGame *battle, BattleUnit *shooter, const Position &origin, bool audible);
	/** Player-legitimate hostile/friendly throw or launch at projectile start (fair-play; M1 P1-D). */
	void tryRecordThrowOrLaunchObserved(
		SavedBattleGame *battle,
		BattleUnit *shooter,
		const Position &origin,
		bool isLaunch,
		const BattleItem *thrownOrLaunchedItem,
		bool audible);
	void tryRecordImpactObserved(SavedBattleGame *battle, const Position &impactTile, bool audible);
	/** Authoritative projectile terminal impact (tile + voxel classification; deduped per flight). */
	void tryRecordProjectileImpactObserved(
		SavedBattleGame *battle,
		const Position &impactMapTile,
		bool audible,
		int voxelPart,
		BattleUnit *victimUnit,
		int flightId,
		int impactOrdinal);
	struct ExplosionPerceptionEngineContext
	{
		bool unitDeathDetonationEngineContext = false;
		BattleUnit *sourceUnitForVisibility = 0;
	};

	void tryRecordExplosionObservedOrHeard(
		SavedBattleGame *battle,
		const Position &centerTile,
		bool audible,
		const ExplosionPerceptionEngineContext *engineContext = 0);
	void tryRecordDoorSoundHeard(SavedBattleGame *battle, const Position &doorTile);
	/** Records a player-legitimate tile observation when a hostile dies (informational; not groundInventory). */
	void tryRecordHostileDeathObserved(
		SavedBattleGame *battle,
		const Position &deathTile,
		bool hadCarriedInventory);
	void noteHostileVisibility(SavedBattleGame *battle, BattleUnit *hostile, bool nowVisible);
	void recordHostileLastSeenBeforeTurnReset(SavedBattleGame *battle, BattleUnit *hostile);

	/**
	 * Authoritative fatal kill attribution when engine murderer and identities are fairly observable.
	 * Called from BattlescapeGame::checkForCasualties with pre–mind-control fatal actor.
	 */
	void tryRecordCombatCasualtyAttributed(
		SavedBattleGame *battle,
		BattleUnit *victim,
		BattleUnit *fatalActor,
		bool mindControlKillCredit,
		bool fatalWoundBleedOutPath,
		bool terrainExplosionWithoutActor,
		bool hiddenExplosionContext);

	/** Roster soldier became unconscious or dead (player-legitimate; session-deduped). */
	void tryRecordSoldierDown(SavedBattleGame *battle, BattleUnit *soldier, bool uiNotificationShown);
	/** Roster soldier woke from unconscious (not from death). */
	void tryRecordSoldierRecovered(SavedBattleGame *battle, BattleUnit *soldier);

	void beginProjectileDirectionObservation(SavedBattleGame *battle, int actorUnitId, int autoShotCounter, int actionType);
	int allocateProjectileImpactOrdinal();
	int getActiveProjectileFlightId() const;
	void noteProjectileDirectionSample(SavedBattleGame *battle, const Position &mapTile);
	void finalizeProjectileDirectionObservation(SavedBattleGame *battle);

	int allocatePsychObservationSerial();
	void tryRecordPsychologicalUiNotification(
		SavedBattleGame *battle,
		BattleUnit *subject,
		const char *eventType,
		int observationSerial,
		bool uiNotificationShown,
		bool subjectVisible,
		const std::string &rosterDisplayName);

	const std::string &getSessionId() const { return _sessionId; }
	int getOldestSeq() const;
	int getLatestSeq() const;
	int getDroppedCount() const { return _droppedCount; }
	const std::vector<CompanionPerceptionEvent> &getEvents() const { return _events; }
	const std::map<int, RosterCasualtyLedgerEntry> &getRosterCasualtyLedger() const { return _rosterCasualtyLedger; }

private:
	void syncRosterCasualtyLedger(
		int subjectUnitId,
		int episodeSerial,
		const char *status,
		int turn,
		bool positionKnown,
		const Position &position,
		const std::string &subjectName);
	void appendEvent(SavedBattleGame *battle, const CompanionPerceptionEvent &entry, const std::string &dedupeKey);
	void appendUnitLastSeen(SavedBattleGame *battle, BattleUnit *hostile, const char *confidence);

	std::string _sessionId;
	std::vector<CompanionPerceptionEvent> _events;
	int _nextSeq;
	int _droppedCount;
	std::set<std::string> _dedupeKeys;
	std::map<int, bool> _hostileWasVisible;
	std::map<int, Position> _hostileLastSeenPosition;

	struct ProjectileDirectionFlight
	{
		bool active;
		int turn;
		int actorUnitId;
		int autoShotCounter;
		int actionType;
		int flightId;
		int nextImpactOrdinal;
		std::vector<Position> orderedTileSamples;
	};
	ProjectileDirectionFlight _projectileDirectionFlight;
	int _nextProjectileFlightId;
	int _nextPsychObservationSerial;
	/** 0=up, 1=unconscious, 2=dead (roster soldiers only). */
	std::map<int, int> _rosterCasualtyState;
	std::map<int, int> _rosterCasualtyEpisodeSerial;
	std::map<int, RosterCasualtyLedgerEntry> _rosterCasualtyLedger;
	std::set<int> _combatCasualtyAttributedVictims;
};

}
