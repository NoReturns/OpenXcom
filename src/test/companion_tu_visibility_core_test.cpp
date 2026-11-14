/*
 * ASTER-M1-TU-VISIBILITY-01 — lightweight TU visibility core tests (no BattlescapeGame).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "Engine/CompanionTuVisibilityCore.h"
#include <iostream>
#include <string>

#ifdef main
#undef main
#endif

using namespace OpenXcom;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static void testFireModeLabels()
{
	expect(std::string(CompanionTuVisibilityCore::tuReservationFireModeLabel(BA_NONE)) == "none", "none");
	expect(std::string(CompanionTuVisibilityCore::tuReservationFireModeLabel(BA_SNAPSHOT)) == "snap", "snap");
	expect(std::string(CompanionTuVisibilityCore::tuReservationFireModeLabel(BA_AUTOSHOT)) == "auto", "auto");
	expect(std::string(CompanionTuVisibilityCore::tuReservationFireModeLabel(BA_AIMEDSHOT)) == "aimed", "aimed");
}

static void testClassifyEdgeCases()
{
	expect(CompanionTuVisibilityCore::classifyTuOutcome(0, 0, 5).empty(), "null unit");
	expect(CompanionTuVisibilityCore::classifyTuOutcome(0, 0, -1).empty(), "negative tu");
}

static void testOctagonalTurnSteps()
{
	expect(CompanionTuVisibilityCore::octagonalTurnSteps(0, 0) == 0, "same dir");
	expect(CompanionTuVisibilityCore::octagonalTurnSteps(0, 2) == 2, "two steps");
	expect(CompanionTuVisibilityCore::octagonalTurnSteps(0, 4) == 4, "opposite");
	expect(CompanionTuVisibilityCore::octagonalTurnSteps(7, 1) == 2, "wrap");
}

static void testFacingTuSpentEstimate()
{
	expect(CompanionTuVisibilityCore::estimateFacingTuSpent(31, 29, 30) == 2, "31-29-30 facing");
	expect(CompanionTuVisibilityCore::estimateFacingTuSpent(32, 29, 30) == 3, "facing-only path");
	expect(CompanionTuVisibilityCore::estimateFacingTuSpent(-1, 0, 10) == -1, "invalid");
}

static void testTuAccountingJsonShape()
{
	CompanionTuVisibilityCore::TuAccountingFields fields;
	fields.timeUnitsAtAcceptance = 40;
	fields.timeUnitsAtValidation = 38;
	fields.actionTuCost = 10;
	fields.outcomeClass = "reserved_tu_blocked";
	fields.rejectionPhase = "command_acceptance";
	const std::string fragment = CompanionTuVisibilityCore::buildTuAccountingJsonObject(fields);
	expect(fragment.find("\"time_units_at_acceptance\": 40") != std::string::npos, "acceptance TU");
	expect(fragment.find("\"outcome_class\": \"reserved_tu_blocked\"") != std::string::npos, "outcome class");
	expect(fragment.find("\"rejection_phase\": \"command_acceptance\"") != std::string::npos, "rejection phase");
}

int main()
{
	std::cerr << "companion_tu_visibility_core_test: starting" << std::endl;
	testFireModeLabels();
	testClassifyEdgeCases();
	testOctagonalTurnSteps();
	testFacingTuSpentEstimate();
	testTuAccountingJsonShape();
	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return 1;
	}
	std::cout << "companion_tu_visibility_core_test: all tests passed." << std::endl;
	return 0;
}