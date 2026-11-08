/*
 * Open X-Com Companion — battlescape ownership handoff tests (M1-WP7A).
 */
#include "Engine/CompanionOwnershipHandoffCore.h"
#include "Engine/CompanionBattlescapeOwnershipCore.h"
#include "Engine/CompanionCampaignOwnershipCore.h"
#include "Engine/CompanionSoldierOwnershipCore.h"
#include <iostream>
#include <set>
#include <cstdint>

namespace OpenXcom
{
class BattlescapeState;
}

using namespace OpenXcom;
using namespace OpenXcom::CompanionOwnershipHandoffCore;
using namespace OpenXcom::CompanionBattlescapeOwnershipCore;
using namespace OpenXcom::CompanionCampaignOwnershipCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static void testHandoffIdleAllowsChange()
{
	expect(evaluateHandoffBlockForActor(3, false, -1, false) == HANDOFF_OK,
		"idle battle must allow handoff");
}

static void testHandoffBlocksInFlightActor()
{
	expect(evaluateHandoffBlockForActor(3, true, 3, false) == HANDOFF_COMPANION_COMMAND_IN_FLIGHT,
		"companion in-flight actor must block handoff");
	expect(evaluateHandoffBlockForActor(4, true, 3, false) == HANDOFF_OK,
		"other soldiers may handoff while companion command runs for different actor");
}

static void testHandoffBlocksNativeAction()
{
	expect(evaluateHandoffBlockForActor(2, true, -1, true) == HANDOFF_NATIVE_ACTION_IN_PROGRESS,
		"native action in progress must block handoff");
}

static void testAsterNameColorConstant()
{
	expect(BATTLESCAPE_ASTER_SOLDIER_NAME_COLOR == 133,
		"ASTER name colour uses palette index 133 (minty green)");
}

static void testEffectiveOwnerFromTacticalSet()
{
	std::set<int> ai;
	ai.insert(5);
	expect(resolveCampaignSoldierControlMode(ai, 5, true) == CompanionSoldierOwnershipCore::CONTROL_AI,
		"tactical ai set must resolve ASTER");
	expect(resolveCampaignSoldierControlMode(ai, 6, true) == CompanionSoldierOwnershipCore::CONTROL_HUMAN,
		"absent id must resolve HUMAN");
}

static SavedGame *dummyCampaign()
{
	return reinterpret_cast<SavedGame *>(static_cast<uintptr_t>(0x20));
}

static void testSquadButtonHiddenWithoutBattlescapeParent()
{
	expect(!shouldShowCompanionSquadInventoryButton(0, dummyCampaign(), false),
		"craft equipment inventory must hide Squad button (no BattlescapeState parent)");
}

static void testSquadButtonHiddenBeforeCombat()
{
	expect(!shouldShowCompanionSquadInventoryButton(
		reinterpret_cast<BattlescapeState *>(static_cast<uintptr_t>(0x10)), dummyCampaign(), true),
		"pre-mission deployment inventory must hide Squad button (beforeGame)");
}

static void testSquadButtonVisibleInMissionInventory()
{
	expect(shouldShowCompanionSquadInventoryButton(
		reinterpret_cast<BattlescapeState *>(static_cast<uintptr_t>(0x10)), dummyCampaign(), false),
		"active battlescape inventory must show Squad button");
}

static void testSquadButtonHiddenWithoutCampaign()
{
	expect(!shouldShowCompanionSquadInventoryButton(
		reinterpret_cast<BattlescapeState *>(static_cast<uintptr_t>(0x10)), 0, false),
		"no campaign pointer must hide Squad button");
}

int main()
{
	testAsterNameColorConstant();
	testHandoffIdleAllowsChange();
	testHandoffBlocksInFlightActor();
	testHandoffBlocksNativeAction();
	testEffectiveOwnerFromTacticalSet();
	testSquadButtonHiddenWithoutBattlescapeParent();
	testSquadButtonHiddenBeforeCombat();
	testSquadButtonVisibleInMissionInventory();
	testSquadButtonHiddenWithoutCampaign();

	if (g_failures == 0)
	{
		std::cout << "companion_battlescape_ownership_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_battlescape_ownership_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}
