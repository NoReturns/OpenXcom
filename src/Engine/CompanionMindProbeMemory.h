#pragma once
/*
 * Open X-Com Companion — per-battle Mind Probe observations (not serialized to battle saves).
 */
#include <map>
#include <string>
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

class SavedBattleGame;
class SavedGame;
class BattleUnit;

class CompanionMindProbeMemory
{
public:
	CompanionMindProbeMemory() {}

	void beginSession(const std::string &sessionId);
	void syncSessionId(const std::string &sessionId);

	const std::string &getSessionId() const { return _sessionId; }

	void recordSuccessfulProbe(SavedBattleGame *battle, SavedGame *campaign, BattleUnit *target, int turn);
	void appendMindProbeObservationsV1(YAML::Node &root, SavedBattleGame *battle, SavedGame *campaign) const;

	bool hasObservationForUnit(int unitId) const;

private:
	std::string _sessionId;
	std::map<int, YAML::Node> _observationsByUnitId;
};

} // namespace OpenXcom