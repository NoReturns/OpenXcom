/*
 * Open X-Com Companion — basescape assignment unsaved-state tests (M1-23E).
 */
#include "Engine/CompanionCampaignOwnershipCore.h"
#include <iostream>
#include <set>

using namespace OpenXcom;
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

static void testPersistedSnapshotMatchesAfterLoad()
{
	std::set<int> current;
	std::set<int> persisted;
	current.insert(3);
	persisted.insert(3);
	expect(!hasUnsavedCompanionSoldierAssignments(current, persisted),
		"matching snapshots must not be unsaved");
	expect(!isCompanionSoldierAssignmentUnsaved(current, persisted, 3),
		"matching soldier must not be unsaved");
}

static void testAssignToAiUnsavedUntilPersisted()
{
	std::set<int> current;
	std::set<int> persisted;
	current.insert(5);
	expect(hasUnsavedCompanionSoldierAssignments(current, persisted),
		"new AI assignment must be unsaved vs empty persisted");
	expect(isCompanionSoldierAssignmentUnsaved(current, persisted, 5),
		"assigned soldier must be unsaved");
	expect(!isCompanionSoldierAssignmentUnsaved(current, persisted, 6),
		"other soldiers must not be unsaved");
}

static void testReclaimUnsavedUntilPersisted()
{
	std::set<int> current;
	std::set<int> persisted;
	persisted.insert(8);
	expect(hasUnsavedCompanionSoldierAssignments(current, persisted),
		"reclaim in memory must differ from persisted AI set");
	expect(isCompanionSoldierAssignmentUnsaved(current, persisted, 8),
		"reclaimed soldier must be unsaved");
}

static void testRevertToSavedClearsUnsaved()
{
	std::set<int> current;
	std::set<int> persisted;
	persisted.insert(2);
	current.insert(2);
	current.insert(9);
	current.erase(9);
	expect(!hasUnsavedCompanionSoldierAssignments(current, persisted),
		"revert to persisted set must clear unsaved");
}

static void testDuplicateNamesUseDistinctIds()
{
	std::set<int> current;
	std::set<int> persisted;
	current.insert(101);
	current.insert(202);
	persisted.insert(202);
	expect(isCompanionSoldierAssignmentUnsaved(current, persisted, 101),
		"first id unsaved when only second id persisted");
	expect(!isCompanionSoldierAssignmentUnsaved(current, persisted, 202),
		"second id matches persisted");
}

static void testSaveLoadRoundTripSnapshot()
{
	std::set<int> loaded;
	loaded.insert(11);
	loaded.insert(22);
	std::set<int> persisted = loaded;
	expect(!hasUnsavedCompanionSoldierAssignments(loaded, persisted),
		"post-save snapshot must match loaded assignments");
	loaded.insert(33);
	expect(hasUnsavedCompanionSoldierAssignments(loaded, persisted),
		"in-memory assign before save must be unsaved");
	persisted = loaded;
	expect(!hasUnsavedCompanionSoldierAssignments(loaded, persisted),
		"sync after save must clear unsaved");
}

static void testFailedSaveLeavesPersistedSnapshot()
{
	std::set<int> current;
	std::set<int> persisted;
	current.insert(4);
	persisted.insert(4);
	current.insert(7);
	// Simulates in-memory assign when disk save fails: current changes, persisted snapshot not synced.
	expect(hasUnsavedCompanionSoldierAssignments(current, persisted),
		"failed save must leave persisted snapshot reflecting last successful write");
	expect(isCompanionSoldierAssignmentUnsaved(current, persisted, 7),
		"new assignment stays unsaved until successful save sync");
	expect(!isCompanionSoldierAssignmentUnsaved(current, persisted, 4),
		"unchanged persisted assignment must not show unsaved");
}

int main()
{
	testPersistedSnapshotMatchesAfterLoad();
	testAssignToAiUnsavedUntilPersisted();
	testReclaimUnsavedUntilPersisted();
	testRevertToSavedClearsUnsaved();
	testDuplicateNamesUseDistinctIds();
	testSaveLoadRoundTripSnapshot();
	testFailedSaveLeavesPersistedSnapshot();

	if (g_failures == 0)
	{
		std::cout << "companion_soldier_assignment_ui_test: all tests passed." << std::endl;
		return 0;
	}
	std::cerr << "companion_soldier_assignment_ui_test: " << g_failures << " failure(s)." << std::endl;
	return 1;
}
