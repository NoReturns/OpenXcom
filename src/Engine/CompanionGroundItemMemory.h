#pragma once
/*
 * Open X-Com Companion — session ground-item memory (M1-WP9, not saved to battle saves).
 */
#include "CompanionGroundItemObservationCore.h"
#include <map>
#include <string>
#include <vector>

namespace OpenXcom
{

class SavedBattleGame;
class SavedGame;
class Tile;

class CompanionGroundItemMemory
{
public:
	CompanionGroundItemMemory() : _droppedCount(0) {}

	void beginSession(const std::string &sessionId);
	void syncSessionId(const std::string &sessionId);

	const std::string &getSessionId() const { return _sessionId; }
	int getDroppedCount() const { return _droppedCount; }

	struct ExportObservation
	{
		Position position;
		const char *observationStatus;
		std::vector<CompanionGroundItemObservationCore::GroundItemObservationItem> items;
	};

	void collectExportObservations(
		SavedBattleGame *battle,
		SavedGame *campaign,
		std::vector<ExportObservation> &out);

private:
	struct RememberedTile
	{
		std::vector<CompanionGroundItemObservationCore::GroundItemObservationItem> items;
	};

	void refreshMemoryFromBattle(SavedBattleGame *battle, SavedGame *campaign);

	std::string _sessionId;
	std::map<std::string, RememberedTile> _remembered;
	int _droppedCount;
};

} // namespace OpenXcom
