/*
 * Open X-Com Companion — regression tests for terrain snapshot export (M1-17B).
 */
#include "Engine/CompanionSnapshotTerrain.h"
#include "Engine/CompanionSnapshotTerrainCapture.h"
#include "Engine/CompanionSnapshotTerrainPacked.h"
#include "../Savegame/Tile.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include <yaml-cpp/yaml.h>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

static YAML::Node emitTerrainRoot(int mapW, int mapH, int mapD,
	const std::function<Tile *(int x, int y, int z)> &getTile)
{
	YAML::Node root;
	CompanionSnapshotTerrain::appendTerrainVerbose(root, mapW, mapH, mapD, getTile);
	return root;
}

static std::string emitTerrainYaml(int mapW, int mapH, int mapD,
	const std::function<Tile *(int x, int y, int z)> &getTile)
{
	YAML::Emitter emitter;
	emitter << emitTerrainRoot(mapW, mapH, mapD, getTile);
	return std::string(emitter.c_str());
}

static bool tilesSortedByZThenYThenX(const YAML::Node &tiles)
{
	for (std::size_t i = 1; i < tiles.size(); ++i)
	{
		const YAML::Node prev = tiles[i - 1]["p"];
		const YAML::Node cur = tiles[i]["p"];
		const int pz = prev[2].as<int>(), cz = cur[2].as<int>();
		if (pz < cz)
		{
			continue;
		}
		if (pz > cz)
		{
			return false;
		}
		const int py = prev[1].as<int>(), cy = cur[1].as<int>();
		if (py < cy)
		{
			continue;
		}
		if (py > cy)
		{
			return false;
		}
		if (prev[0].as<int>() >= cur[0].as<int>())
		{
			return false;
		}
	}
	return true;
}

static MapData *makeFloor(MapDataSet *ds, int tuWalk, int elev)
{
	MapData *md = new MapData(ds);
	md->setObjectType(O_FLOOR);
	md->setTUCosts(tuWalk, tuWalk, tuWalk);
	md->setTerrainLevel(elev);
	return md;
}

static MapData *makeWall(MapDataSet *ds, TilePart part, int tuWalk, bool stopLos)
{
	MapData *md = new MapData(ds);
	md->setObjectType(part);
	md->setTUCosts(tuWalk, tuWalk, tuWalk);
	md->setStopLOS(stopLos);
	return md;
}

static void testUndiscoveredOmitted()
{
	MapDataSet ds("test");
	MapData *floor = makeFloor(&ds, 4, 0);
	Tile tile(Position(0, 0, 0));
	tile.setMapData(floor, 1, 0, O_FLOOR);

	const std::string yaml = emitTerrainYaml(1, 1, 1, [&](int x, int y, int z) -> Tile * {
		if (x == 0 && y == 0 && z == 0)
		{
			return &tile;
		}
		return 0;
	});
	expect(yaml.find("tiles:") != std::string::npos, "terrain wrapper expected");
	expect(yaml.find("- p:") == std::string::npos, "undiscovered tile must be omitted");
	delete floor;
}

static void testDiscoveredBodyAndLos()
{
	MapDataSet ds("test");
	MapData *floor = makeFloor(&ds, 4, -2);
	Tile tile(Position(2, 1, 0));
	tile.setMapData(floor, 1, 0, O_FLOOR);
	tile.setDiscovered(true, 2);
	tile.setVisible(+1);

	expect(tile.isDiscovered(0) && tile.isDiscovered(1) && tile.isDiscovered(2),
		"Tile::setDiscovered(true, 2) must set west, north, and body bits");

	auto getTile = [&](int x, int y, int z) -> Tile * {
		if (x == 2 && y == 1 && z == 0)
		{
			return &tile;
		}
		return 0;
	};

	const YAML::Node root = emitTerrainRoot(3, 3, 1, getTile);
	const YAML::Node tiles = root["terrain"]["tiles"];
	expect(tiles.size() == 1, "one discovered tile expected");
	expect(tiles[0]["disc"].as<int>() == 7, "discovery bitmask 1|2|4 from engine tile state");
	expect(tiles[0]["inLos"].as<bool>() == true, "inLos true expected");
	expect(tiles[0]["body"]["elev"].as<int>() == -2, "elevation expected");
	expect(tiles[0]["body"]["walkF"].as<int>() == 4, "floor walk TU expected");
	delete floor;
}

static void testWestWallWithoutBody()
{
	MapDataSet ds("test");
	MapData *wall = makeWall(&ds, O_WESTWALL, 255, true);
	Tile tile(Position(1, 0, 0));
	tile.setMapData(wall, 2, 0, O_WESTWALL);
	tile.setDiscovered(true, 0);

	const std::string yaml = emitTerrainYaml(2, 2, 1, [&](int x, int y, int z) -> Tile * {
		if (x == 1 && y == 0 && z == 0)
		{
			return &tile;
		}
		return 0;
	});
	expect(yaml.find("disc: 1") != std::string::npos, "west-only discovery expected");
	expect(yaml.find("body:") == std::string::npos, "body must not export without body discovery");
	expect(yaml.find("west:") != std::string::npos, "west wall expected");
	expect(yaml.find("blockSight: true") != std::string::npos, "wall blockSight expected");
	delete wall;
}

static void testDiscoveredVoidAir()
{
	Tile tile(Position(0, 0, 1));
	tile.setDiscovered(true, 2);

	const std::string yaml = emitTerrainYaml(1, 1, 2, [&](int x, int y, int z) -> Tile * {
		if (x == 0 && y == 0 && z == 1)
		{
			return &tile;
		}
		return 0;
	});
	expect(yaml.find("air: true") != std::string::npos, "discovered void air tile expected");
	expect(yaml.find("noFloor: true") != std::string::npos, "void air marks noFloor");
}

static void testNoUnitLeakage()
{
	MapDataSet ds("test");
	MapData *floor = makeFloor(&ds, 4, 0);
	Tile tile(Position(0, 0, 0));
	tile.setMapData(floor, 1, 0, O_FLOOR);
	tile.setDiscovered(true, 2);
	tile.setVisible(+1);

	const std::string yaml = emitTerrainYaml(1, 1, 1, [&](int, int, int) -> Tile * {
		return &tile;
	});
	expect(yaml.find("unit") == std::string::npos, "terrain record must not mention units");
	delete floor;
}

static void testDeterministicOrdering()
{
	MapDataSet ds("test");
	MapData *floorA = makeFloor(&ds, 4, 0);
	MapData *floorB = makeFloor(&ds, 6, 0);
	Tile tileA(Position(0, 0, 0));
	Tile tileB(Position(1, 0, 1));
	tileA.setMapData(floorA, 1, 0, O_FLOOR);
	tileB.setMapData(floorB, 1, 0, O_FLOOR);
	tileA.setDiscovered(true, 2);
	tileB.setDiscovered(true, 2);

	auto getTile = [&](int x, int y, int z) -> Tile * {
		if (x == 0 && y == 0 && z == 0)
		{
			return &tileA;
		}
		if (x == 1 && y == 0 && z == 1)
		{
			return &tileB;
		}
		return 0;
	};

	const YAML::Node rootFirst = emitTerrainRoot(2, 1, 2, getTile);
	const YAML::Node rootSecond = emitTerrainRoot(2, 1, 2, getTile);
	YAML::Emitter emitterFirst;
	YAML::Emitter emitterSecond;
	emitterFirst << rootFirst;
	emitterSecond << rootSecond;
	expect(std::string(emitterFirst.c_str()) == std::string(emitterSecond.c_str()),
		"terrain YAML must be deterministic");

	const YAML::Node tiles = rootFirst["terrain"]["tiles"];
	expect(tiles.size() == 2, "two exported tiles expected");
	expect(tiles[0]["p"][0].as<int>() == 0 && tiles[0]["p"][1].as<int>() == 0 && tiles[0]["p"][2].as<int>() == 0,
		"first tile at (0,0,0)");
	expect(tiles[1]["p"][0].as<int>() == 1 && tiles[1]["p"][1].as<int>() == 0 && tiles[1]["p"][2].as<int>() == 1,
		"second tile at (1,0,1)");
	expect(tilesSortedByZThenYThenX(tiles), "tiles must sort by z then y then x");

	delete floorA;
	delete floorB;
}

static std::string bytesToHex(const std::vector<unsigned char> &bytes)
{
	std::ostringstream out;
	out << std::hex << std::setfill('0');
	for (size_t i = 0; i < bytes.size(); ++i)
	{
		if (i != 0)
		{
			out << ' ';
		}
		out << std::setw(2) << static_cast<unsigned int>(bytes[i]);
	}
	return out.str();
}

static void testPackedNegativeElevationGoldenVector()
{
	MapDataSet ds("test");
	MapData *floor = makeFloor(&ds, 4, -2);
	Tile tile(Position(2, 1, 0));
	tile.setMapData(floor, 1, 0, O_FLOOR);
	tile.setDiscovered(true, 2);
	tile.setVisible(+1);

	std::vector<CompanionSnapshotTerrain::TerrainTileExport> records;
	CompanionSnapshotTerrain::captureTerrainTiles(3, 3, 1, [&](int x, int y, int z) -> Tile * {
		return (x == 2 && y == 1 && z == 0) ? &tile : 0;
	}, records);

	const std::vector<unsigned char> blob = CompanionSnapshotTerrain::encodePackedV1Blob(3, 3, 1, records);
	const std::string hex = bytesToHex(blob);
	expect(hex.find("02 01 00 07 01") != std::string::npos, "packed golden prefix expected (body discovery implies disc 7)");
	expect(hex.find("04 fe 04") != std::string::npos, "packed golden body expected");

	YAML::Node rootPacked;
	CompanionSnapshotTerrain::appendTerrain(rootPacked, 3, 3, 1, [&](int x, int y, int z) -> Tile * {
		return (x == 2 && y == 1 && z == 0) ? &tile : 0;
	});
	expect(rootPacked["terrain"]["encoding"].as<std::string>() == "packed-v1", "default export must be packed-v1");

	delete floor;
}

static void testPackedWorstCaseRecordSize()
{
	MapDataSet ds("test");
	MapData *floor = makeFloor(&ds, 4, 0);
	MapData *object = new MapData(&ds);
	object->setObjectType(O_OBJECT);
	object->setTUCosts(255, 255, 255);
	MapData *west = makeWall(&ds, O_WESTWALL, 255, true);
	MapData *north = makeWall(&ds, O_NORTHWALL, 8, false);
	Tile tile(Position(2, 1, 0));
	tile.setMapData(floor, 1, 0, O_FLOOR);
	tile.setMapData(object, 1, 0, O_OBJECT);
	tile.setMapData(west, 1, 0, O_WESTWALL);
	tile.setMapData(north, 1, 0, O_NORTHWALL);
	tile.setDiscovered(true, 2);
	tile.setVisible(+1);

	std::vector<CompanionSnapshotTerrain::TerrainTileExport> records;
	CompanionSnapshotTerrain::captureTerrainTiles(3, 3, 1, [&](int x, int y, int z) -> Tile * {
		return (x == 2 && y == 1 && z == 0) ? &tile : 0;
	}, records);
	const std::vector<unsigned char> blob = CompanionSnapshotTerrain::encodePackedV1Blob(3, 3, 1, records);
	expect(blob.size() == CompanionSnapshotTerrain::PACKED_V1_HEADER_BYTES + 13,
		"worst-case per-record size must be 13 bytes");

	delete floor;
	delete object;
	delete west;
	delete north;
}

#ifdef main
#undef main
#endif
int main()
{
	testUndiscoveredOmitted();
	testDiscoveredBodyAndLos();
	testWestWallWithoutBody();
	testDiscoveredVoidAir();
	testNoUnitLeakage();
	testDeterministicOrdering();
	testPackedNegativeElevationGoldenVector();
	testPackedWorstCaseRecordSize();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test(s) failed." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_snapshot_terrain_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
