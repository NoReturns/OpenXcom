/*
 * Open X-Com Companion — unit facing/condition/readiness helpers (M1-WP2).
 */
#include "Engine/CompanionSnapshotUnitStateCore.h"
#include "../Savegame/BattleUnit.h"
#include <cstdlib>
#include <iostream>
#include <string>

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

static void testAllEightDirectionsMatchPathfinding()
{
	static const int expectDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
	static const int expectDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
	for (int dir = 0; dir < 8; ++dir)
	{
		int dx = 0;
		int dy = 0;
		CompanionSnapshotUnitStateCore::directionToMapVector(dir, &dx, &dy);
		expect(dx == expectDx[dir], "dx must match OpenXcom pathfinding table");
		expect(dy == expectDy[dir], "dy must match OpenXcom pathfinding table");
		const char *label = CompanionSnapshotUnitStateCore::directionLabel(dir);
		expect(label != 0 && label[0] != '\0', "direction label must exist");
	}
}

static void testInvalidDirectionVectorZero()
{
	int dx = 99;
	int dy = 99;
	CompanionSnapshotUnitStateCore::directionToMapVector(-1, &dx, &dy);
	expect(dx == 0 && dy == 0, "invalid direction dx/dy zero");
	CompanionSnapshotUnitStateCore::directionToMapVector(9, &dx, &dy);
	expect(dx == 0 && dy == 0, "out of range direction dx/dy zero");
	expect(CompanionSnapshotUnitStateCore::directionLabel(-1) == 0, "invalid direction has no label");
}

static void testCardinalLabels()
{
	expect(std::string(CompanionSnapshotUnitStateCore::directionLabel(0)) == "north", "dir 0 north");
	expect(std::string(CompanionSnapshotUnitStateCore::directionLabel(2)) == "east", "dir 2 east");
	expect(std::string(CompanionSnapshotUnitStateCore::directionLabel(4)) == "south", "dir 4 south");
	expect(std::string(CompanionSnapshotUnitStateCore::directionLabel(6)) == "west", "dir 6 west");
}

static void testReadinessStatusStrings()
{
	expect(std::string(CompanionSnapshotUnitStateCore::unitStatusId(STATUS_UNCONSCIOUS)) == "unconscious",
		"unconscious status id");
	expect(std::string(CompanionSnapshotUnitStateCore::unitStatusId(STATUS_DEAD)) == "dead", "dead status id");
	expect(std::string(CompanionSnapshotUnitStateCore::movementTypeId(MT_FLY)) == "fly", "fly movement");
}

#ifdef main
#undef main
#endif
int main()
{
	testAllEightDirectionsMatchPathfinding();
	testInvalidDirectionVectorZero();
	testCardinalLabels();
	testReadinessStatusStrings();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_snapshot_unit_state_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
