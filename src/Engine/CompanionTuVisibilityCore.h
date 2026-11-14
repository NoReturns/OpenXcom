#pragma once
/*
 * Open X-Com Companion — TU reservation visibility for snapshots and results (M1).
 * Uses BattlescapeGame::checkReservedTU; does not alter spending rules.
 */
#include <string>
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

class BattlescapeGame;
class BattleUnit;
class Position;
class SavedBattleGame;
enum BattleActionType;

namespace CompanionTuVisibilityCore
{

struct TuAccountingFields
{
	int timeUnitsAtAcceptance = -1;
	int timeUnitsAtValidation = -1;
	int timeUnitsRemaining = -1;
	int actionTuCost = -1;
	int facingTuEstimate = -1;
	int standUpTuBeforeWalk = -1;
	int facingTuStepsEstimated = -1;
	int facingTuSpentEstimate = -1;
	std::string outcomeClass;
	std::string rejectionPhase;
	std::string observationNote;
};

/// Minimum 0..4 octagonal steps between two engine direction indices (0–7).
int octagonalTurnSteps(int fromDirection, int toDirection);

/// Engine-facing direction used before directed fire (body or turret when present).
int facingDirectionForDirectedFire(const BattleUnit *unit);

/// 1 TU per facing step during UnitTurnBState; -1 when unit is null.
int estimateDirectedFireFacingSteps(const BattleUnit *unit, const Position &target);

/// Observed facing spend when acceptance and terminal TU are known; else -1.
int estimateFacingTuSpent(int tuAtAcceptance, int tuRemaining, int fireActionTuCost);

/// Player UI shot-reserve mode label for tactical export.
const char *tuReservationFireModeLabel(int battleActionType);

void appendBattleTuReservation(YAML::Node &root, SavedBattleGame *battle);

/// Adds canAffordTuWithCurrentReservation / reservationWouldBlock when engine check is available.
void appendActionTuReservationAdvisory(
	YAML::Node &actionEntry,
	SavedBattleGame *battle,
	BattleUnit *unit,
	int tuCost,
	bool legacyCanAffordTu);

void appendPostureTuReservationAdvisory(YAML::Node &posture, SavedBattleGame *battle, BattleUnit *unit);

/// Kneeling MOVE: implicit stand-up cost; no path TU claim.
void appendMovementTuAdvisory(YAML::Node &unitEntry, SavedBattleGame *battle, BattleUnit *unit);

/// Directed-fire actions: per-step facing cost; steps need a target.
void appendDirectedFireFacingAdvisory(YAML::Node &actionEntry, BattleActionType actionType);

/// insufficient_tu_reject | reserved_tu_blocked | empty when indeterminate
std::string classifyTuOutcome(BattleUnit *unit, BattlescapeGame *game, int tuCost);

std::string buildTuAccountingJsonObject(const TuAccountingFields &fields);

bool insertTuAccountingIntoResultJson(std::string &json, const TuAccountingFields &fields);

} // namespace CompanionTuVisibilityCore

} // namespace OpenXcom