/*
 * Open X-Com Companion — executable regression tests for CompanionEventLog (M1-14C).
 */
#include "Engine/CompanionEventLog.h"
#include "Engine/CompanionEventLogTestSupport.h"
#include "Engine/RNG.h"
#include "../Savegame/BattleUnit.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace OpenXcom;
using namespace OpenXcom::CompanionEventLogTestSupport;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static EligibilityInput baseEligibleInput()
{
	EligibilityInput input = {};
	input.debugMode = false;
	input.battleSide = FACTION_PLAYER;
	input.actionType = BA_SNAPSHOT;
	input.playerOrdered = true;
	input.actorFaction = FACTION_PLAYER;
	input.actorOriginalFaction = FACTION_PLAYER;
	input.actorHasGeoscapeSoldier = true;
	return input;
}

static void testSessionIdFormat()
{
	const std::string id = makeSessionId();
	expect(isValidSessionIdFormat(id), "session id must be 32 lowercase hex chars");
	const std::string id2 = makeSessionId();
	expect(id != id2, "successive session ids should differ");
}

static void testGameplayRngUntouched()
{
	RNG::setSeed(0x123456789ABCDEFULL);
	const uint64_t beforeConstruct = RNG::getSeed();
	CompanionEventLog log;
	const uint64_t afterConstruct = RNG::getSeed();
	expect(beforeConstruct == afterConstruct, "constructing CompanionEventLog must not change gameplay RNG");

	log.beginSession();
	const uint64_t afterBeginSession = RNG::getSeed();
	expect(afterConstruct == afterBeginSession, "beginSession must not change gameplay RNG");

	log.beginSession();
	const uint64_t afterSecondBegin = RNG::getSeed();
	expect(afterBeginSession == afterSecondBegin, "repeated beginSession must not change gameplay RNG");

	CompanionEventLog reloadLog;
	reloadLog.beginSession();
	const uint64_t afterReloadSimulation = RNG::getSeed();
	expect(afterSecondBegin == afterReloadSimulation, "battle reload companion session must not change gameplay RNG");
	expect(!log.getSessionId().empty(), "companion session id must be set");
	expect(log.getSessionId() != reloadLog.getSessionId(),
		"battle save reload must assign a new companion session id");
}

static void testEligibility()
{
	EligibilityInput input = baseEligibleInput();
	expect(shouldRecordDeliberatePlayerWeaponFire(input), "player-ordered snap on player turn should record");

	input.actionType = BA_AUTOSHOT;
	expect(shouldRecordDeliberatePlayerWeaponFire(input), "player-ordered auto fire should record");

	input.playerOrdered = false;
	expect(!shouldRecordDeliberatePlayerWeaponFire(input), "reaction fire (not player ordered) must not record");

	input = baseEligibleInput();
	input.battleSide = FACTION_HOSTILE;
	expect(!shouldRecordDeliberatePlayerWeaponFire(input), "alien turn must not record");

	input = baseEligibleInput();
	input.actorFaction = FACTION_HOSTILE;
	input.actorOriginalFaction = FACTION_HOSTILE;
	expect(!shouldRecordDeliberatePlayerWeaponFire(input), "alien actor must not record");

	input = baseEligibleInput();
	input.playerOrdered = false;
	expect(!shouldRecordDeliberatePlayerWeaponFire(input), "panic/berserk (not player ordered) must not record");

	input = baseEligibleInput();
	input.debugMode = true;
	expect(!shouldRecordDeliberatePlayerWeaponFire(input), "debug/reveal mode must not record");

	input = baseEligibleInput();
	input.actorHasGeoscapeSoldier = false;
	expect(!shouldRecordDeliberatePlayerWeaponFire(input), "actors without geoscape soldier must not record");
}

#ifdef main
#undef main
#endif
int main()
{
	testSessionIdFormat();
	testGameplayRngUntouched();
	testEligibility();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_event_log_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
