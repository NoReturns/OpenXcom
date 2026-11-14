/*
 * Open X-Com Companion — TU reservation visibility (M1).
 */
#include "CompanionTuVisibilityCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/Position.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include <cstdlib>
#include <sstream>

namespace OpenXcom
{

namespace CompanionTuVisibilityCore
{

namespace
{

void jsonEscape(std::ostringstream &out, const std::string &value)
{
	for (std::string::size_type i = 0; i < value.size(); ++i)
	{
		const char c = value[i];
		if (c == '"' || c == '\\')
		{
			out << '\\' << c;
		}
		else if (c == '\n')
		{
			out << "\\n";
		}
		else
		{
			out << c;
		}
	}
}

BattlescapeGame *battleGameFromSave(SavedBattleGame *battle)
{
	return battle != 0 ? battle->getBattleGame() : 0;
}

bool canAffordWithEngineReservation(BattlescapeGame *game, BattleUnit *unit, int tuCost)
{
	return game != 0 && unit != 0 && tuCost >= 0 && game->checkReservedTU(unit, tuCost, true);
}

} // namespace

const char *tuReservationFireModeLabel(int battleActionType)
{
	switch (battleActionType)
	{
	case BA_NONE:
		return "none";
	case BA_SNAPSHOT:
		return "snap";
	case BA_AUTOSHOT:
		return "auto";
	case BA_AIMEDSHOT:
		return "aimed";
	default:
		return "unknown";
	}
}

void appendBattleTuReservation(YAML::Node &root, SavedBattleGame *battle)
{
	if (battle == 0)
	{
		return;
	}
	YAML::Node block;
	block["schemaVersion"] = 1;
	const BattleActionType reserved = battle->getTUReserved();
	block["fireMode"] = tuReservationFireModeLabel(reserved);
	block["fireModeActionType"] = static_cast<int>(reserved);
	block["kneelReserve"] = battle->getKneelReserved();
	const bool checkAvailable = battleGameFromSave(battle) != 0;
	block["engineReservationCheckAvailable"] = checkAvailable;
	block["observationNote"] =
		"Advisory only: player UI shot reserve and kneel-reserve at export. "
		"canAffordTu ignores reservation; use canAffordTuWithCurrentReservation when engineReservationCheckAvailable is true.";
	root["tuReservation"] = block;
}

void appendActionTuReservationAdvisory(
	YAML::Node &actionEntry,
	SavedBattleGame *battle,
	BattleUnit *unit,
	int tuCost,
	bool legacyCanAffordTu)
{
	BattlescapeGame *game = battleGameFromSave(battle);
	if (game == 0 || unit == 0 || unit->getFaction() != FACTION_PLAYER)
	{
		return;
	}
	const bool withReserve = canAffordWithEngineReservation(game, unit, tuCost);
	actionEntry["canAffordTuWithCurrentReservation"] = withReserve;
	if (legacyCanAffordTu && !withReserve)
	{
		actionEntry["reservationWouldBlock"] = true;
	}
}

void appendPostureTuReservationAdvisory(YAML::Node &posture, SavedBattleGame *battle, BattleUnit *unit)
{
	BattlescapeGame *game = battleGameFromSave(battle);
	if (game == 0 || unit == 0 || unit->getFaction() != FACTION_PLAYER)
	{
		return;
	}
	const int kneelTu = 4;
	const int standTu = 8;
	const bool kneelOk = canAffordWithEngineReservation(game, unit, kneelTu);
	const bool standOk = canAffordWithEngineReservation(game, unit, standTu);
	posture["kneelDownCanAffordWithCurrentReservation"] = kneelOk;
	posture["standUpCanAffordWithCurrentReservation"] = standOk;
	const bool kneelLegacy = kneelTu <= unit->getTimeUnits();
	const bool standLegacy = standTu <= unit->getTimeUnits();
	if (kneelLegacy && !kneelOk)
	{
		posture["reservationWouldBlockKneelDown"] = true;
	}
	if (standLegacy && !standOk)
	{
		posture["reservationWouldBlockStandUp"] = true;
	}
}

int octagonalTurnSteps(int fromDirection, int toDirection)
{
	const int a = fromDirection & 7;
	const int b = toDirection & 7;
	int delta = a - b;
	if (delta < 0)
	{
		delta = -delta;
	}
	if (delta > 4)
	{
		delta = 8 - delta;
	}
	return delta;
}

int facingDirectionForDirectedFire(const BattleUnit *unit)
{
	if (unit == 0)
	{
		return 0;
	}
	if (unit->getTurretType() != -1)
	{
		return unit->getTurretDirection();
	}
	return unit->getDirection();
}

int estimateDirectedFireFacingSteps(const BattleUnit *unit, const Position &target)
{
	if (unit == 0)
	{
		return -1;
	}
	const int desired = unit->directionTo(target);
	const int current = facingDirectionForDirectedFire(unit);
	return octagonalTurnSteps(current, desired);
}

int estimateFacingTuSpent(int tuAtAcceptance, int tuRemaining, int fireActionTuCost)
{
	if (tuAtAcceptance < 0 || tuRemaining < 0)
	{
		return -1;
	}
	if (fireActionTuCost >= 0 && tuRemaining < fireActionTuCost)
	{
		const int facingOnly = tuAtAcceptance - tuRemaining;
		if (facingOnly >= 0 && facingOnly <= 8)
		{
			return facingOnly;
		}
		return -1;
	}
	if (fireActionTuCost < 0)
	{
		return -1;
	}
	const int spent = tuAtAcceptance - tuRemaining - fireActionTuCost;
	if (spent < 0 || spent > 8)
	{
		return -1;
	}
	return spent;
}

void appendMovementTuAdvisory(YAML::Node &unitEntry, SavedBattleGame *battle, BattleUnit *unit)
{
	if (unit == 0 || unit->getFaction() != FACTION_PLAYER)
	{
		return;
	}
	YAML::Node adv;
	adv["schemaVersion"] = 1;
	adv["pathTuEstimateIncluded"] = false;
	adv["facingTuDuringPathPerStep"] = 0;
	adv["observationNote"] =
		"Advisory only: kneeling soldiers spend standUpTu before the first walk step (engine kneel()). "
		"Path tile costs are not exported; engine accepts MOVE without a TU preflight.";
	const bool needsStand = unit->getType() == "SOLDIER" && !unit->isFloating() && unit->isKneeled();
	if (needsStand)
	{
		const int standTu = 8;
		adv["standUpRequiredBeforeWalk"] = true;
		adv["standUpTuBeforeWalk"] = standTu;
		const int tu = unit->getTimeUnits();
		adv["standUpCanAffordTu"] = tu >= standTu;
		if (tu >= standTu)
		{
			adv["timeUnitsAfterStandEstimate"] = tu - standTu;
		}
		BattlescapeGame *game = battleGameFromSave(battle);
		if (game != 0)
		{
			const bool standWithReserve = canAffordWithEngineReservation(game, unit, standTu);
			adv["standUpCanAffordWithCurrentReservation"] = standWithReserve;
			if (tu >= standTu && !standWithReserve)
			{
				adv["reservationWouldBlockStandBeforeWalk"] = true;
			}
		}
	}
	unitEntry["movementTuAdvisory"] = adv;
}

void appendDirectedFireFacingAdvisory(YAML::Node &actionEntry, BattleActionType actionType)
{
	if (actionType != BA_SNAPSHOT && actionType != BA_AUTOSHOT && actionType != BA_AIMEDSHOT)
	{
		return;
	}
	actionEntry["facingTuPerStep"] = 1;
	actionEntry["facingTuStepsUnknownWithoutTarget"] = true;
	actionEntry["facingObservationNote"] =
		"Directed fire queues UnitTurnBState (1 TU per facing step) before the shot. "
		"canAffordTu is fire TU only; composite affordability needs target bearing.";
}

std::string classifyTuOutcome(BattleUnit *unit, BattlescapeGame *game, int tuCost)
{
	if (unit == 0 || tuCost < 0)
	{
		return std::string();
	}
	if (tuCost > unit->getTimeUnits())
	{
		return "insufficient_tu_reject";
	}
	if (game != 0 && !game->checkReservedTU(unit, tuCost, true))
	{
		return "reserved_tu_blocked";
	}
	return std::string();
}

std::string buildTuAccountingJsonObject(const TuAccountingFields &fields)
{
	std::ostringstream json;
	json << "  \"tu_accounting\": {\n";
	bool first = true;
	auto field = [&](const char *key, int value) {
		if (value < 0)
		{
			return;
		}
		if (!first)
		{
			json << ",\n";
		}
		first = false;
		json << "    \"" << key << "\": " << value;
	};
	field("time_units_at_acceptance", fields.timeUnitsAtAcceptance);
	field("time_units_at_validation", fields.timeUnitsAtValidation);
	field("time_units_remaining", fields.timeUnitsRemaining);
	field("action_tu_cost", fields.actionTuCost);
	field("facing_tu_estimate", fields.facingTuEstimate);
	field("stand_up_tu_before_walk", fields.standUpTuBeforeWalk);
	field("facing_tu_steps_estimated", fields.facingTuStepsEstimated);
	field("facing_tu_spent_estimate", fields.facingTuSpentEstimate);
	if (!fields.outcomeClass.empty())
	{
		if (!first)
		{
			json << ",\n";
		}
		first = false;
		json << "    \"outcome_class\": \"";
		jsonEscape(json, fields.outcomeClass);
		json << "\"";
	}
	if (!fields.rejectionPhase.empty())
	{
		if (!first)
		{
			json << ",\n";
		}
		first = false;
		json << "    \"rejection_phase\": \"";
		jsonEscape(json, fields.rejectionPhase);
		json << "\"";
	}
	if (!fields.observationNote.empty())
	{
		if (!first)
		{
			json << ",\n";
		}
		first = false;
		json << "    \"observation_note\": \"";
		jsonEscape(json, fields.observationNote);
		json << "\"";
	}
	json << "\n  }";
	return json.str();
}

bool insertTuAccountingIntoResultJson(std::string &json, const TuAccountingFields &fields)
{
	const bool hasData = fields.timeUnitsAtAcceptance >= 0
		|| fields.timeUnitsAtValidation >= 0
		|| fields.timeUnitsRemaining >= 0
		|| fields.actionTuCost >= 0
		|| fields.facingTuEstimate >= 0
		|| fields.standUpTuBeforeWalk >= 0
		|| fields.facingTuStepsEstimated >= 0
		|| fields.facingTuSpentEstimate >= 0
		|| !fields.outcomeClass.empty();
	if (!hasData)
	{
		return false;
	}
	const std::string::size_type insertAt = json.rfind("  \"completed_at\"");
	if (insertAt == std::string::npos)
	{
		return false;
	}
	const std::string fragment = buildTuAccountingJsonObject(fields);
	json = json.substr(0, insertAt) + fragment + ",\n" + json.substr(insertAt);
	return true;
}

} // namespace CompanionTuVisibilityCore

} // namespace OpenXcom