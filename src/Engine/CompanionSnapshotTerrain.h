#pragma once
/*
 * Open X-Com Companion — sparse terrain export for tactical snapshots (M1-17B).
 */
#include <functional>
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

class Tile;

namespace CompanionSnapshotTerrain
{

const size_t SNAPSHOT_MAX_BYTES = 256 * 1024;

/// Fixed note: engine has no separate terrain memory; off-LOS geometry may be stale vs last observation.
const char *MEMORY_NOTE();

/**
 * Append packed-v1 terrain (encoding, recordCount, data) for discovered tiles.
 * @p getTile returns nullptr for out-of-map (skipped).
 * Deterministic order: z, then y, then x.
 */
void appendTerrain(YAML::Node &root, int mapSizeX, int mapSizeY, int mapSizeZ,
	const std::function<Tile *(int x, int y, int z)> &getTile);

/** Verbose terrain.tiles[] export (compatibility / parity tests). */
void appendTerrainVerbose(YAML::Node &root, int mapSizeX, int mapSizeY, int mapSizeZ,
	const std::function<Tile *(int x, int y, int z)> &getTile);

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
