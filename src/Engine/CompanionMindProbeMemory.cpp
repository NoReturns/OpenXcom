/*
 * Open X-Com Companion — per-battle Mind Probe observations.
 */
#include "CompanionMindProbeMemory.h"
#include "CompanionMindProbeObservationCore.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Engine/Game.h"
#include "../Mod/Mod.h"

namespace OpenXcom
{

static Mod *modFromBattle(SavedBattleGame *battle)
{
	if (battle == 0)
	{
		return 0;
	}
	if (battle->getBattleState() != 0)
	{
		return battle->getBattleState()->getGame()->getMod();
	}
	return battle->getTestMod();
}

void CompanionMindProbeMemory::beginSession(const std::string &sessionId)
{
	_sessionId = sessionId;
	_observationsByUnitId.clear();
}

void CompanionMindProbeMemory::syncSessionId(const std::string &sessionId)
{
	if (_sessionId != sessionId)
	{
		beginSession(sessionId);
	}
}

void CompanionMindProbeMemory::recordSuccessfulProbe(
	SavedBattleGame *battle,
	SavedGame *campaign,
	BattleUnit *target,
	int turn)
{
	if (battle == 0 || target == 0)
	{
		return;
	}
	Mod *mod = modFromBattle(battle);
	YAML::Node obs;
	obs["unitId"] = target->getId();
	obs["probedTurn"] = turn;
	YAML::Node stats;
	CompanionMindProbeObservationCore::appendAuthorizedMindProbeStats(stats, target, campaign, mod);
	obs["stats"] = stats;
	_observationsByUnitId[target->getId()] = obs;
}

bool CompanionMindProbeMemory::hasObservationForUnit(int unitId) const
{
	return _observationsByUnitId.find(unitId) != _observationsByUnitId.end();
}

void CompanionMindProbeMemory::appendMindProbeObservationsV1(
	YAML::Node &root,
	SavedBattleGame *battle,
	SavedGame *campaign) const
{
	if (_observationsByUnitId.empty())
	{
		return;
	}
	YAML::Node block;
	block["schemaVersion"] = 1;
	block["sessionId"] = _sessionId;
	block["memoryNote"] =
		"Mind Probe stats captured at successful probe execution; repeated F6 exports repeat that snapshot until a new successful probe or session change.";
	YAML::Node observations(YAML::NodeType::Sequence);
	for (std::map<int, YAML::Node>::const_iterator i = _observationsByUnitId.begin();
	     i != _observationsByUnitId.end();
	     ++i)
	{
		if (battle != 0)
		{
			bool inBattle = false;
			for (std::vector<BattleUnit *>::iterator u = battle->getUnits()->begin();
			     u != battle->getUnits()->end();
			     ++u)
			{
				if ((*u)->getId() == i->first)
				{
					inBattle = true;
					break;
				}
			}
			if (!inBattle)
			{
				continue;
			}
		}
		observations.push_back(i->second);
	}
	if (observations.size() == 0)
	{
		return;
	}
	block["observations"] = observations;
	root["mindProbeObservationsV1"] = block;
}

} // namespace OpenXcom