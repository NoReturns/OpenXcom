#pragma once
/*
 * Open X-Com Companion — in-memory weapon fire observation (not saved to game YAML).
 */
#include "../Battlescape/Position.h"
#include "../Battlescape/BattlescapeGame.h"
#include <string>
#include <vector>

namespace OpenXcom
{

class BattleUnit;
class SavedBattleGame;

struct CompanionWeaponFireEvent
{
	int seq;
	int turn;
	int actorUnitId;
	std::string weaponItemType;
	BattleActionType actionType;
	Position target;
	int autoShotIndex;
};

/**
 * Bounded ring of companion-observed player weapon fire events for the current battle session.
 */
class CompanionEventLog
{
public:
	static const size_t MAX_RETAINED = 256;

	CompanionEventLog();

	void beginSession();
	void tryRecordWeaponFired(SavedBattleGame *battle, const BattleAction &action, BattleUnit *actor);

	const std::string &getSessionId() const { return _sessionId; }
	int getOldestSeq() const;
	int getLatestSeq() const;
	int getDroppedCount() const { return _droppedCount; }
	const std::vector<CompanionWeaponFireEvent> &getEvents() const { return _events; }

private:
	std::string _sessionId;
	std::vector<CompanionWeaponFireEvent> _events;
	int _nextSeq;
	int _droppedCount;
};

}
