#pragma once
/*
 * Open X-Com Companion — packed-v1 terrain blob encoder (M1-17N).
 */
#include "CompanionSnapshotTerrainCapture.h"
#include <cstddef>
#include <string>
#include <vector>

namespace OpenXcom
{

namespace CompanionSnapshotTerrain
{

const size_t PACKED_V1_HEADER_BYTES = 18;
const size_t PACKED_V1_MAX_TILE_RECORD_BYTES = 13;

std::vector<unsigned char> encodePackedV1Blob(int mapSizeX, int mapSizeY, int mapSizeZ,
	const std::vector<TerrainTileExport> &tiles);

std::string encodeCanonicalBase64(const std::vector<unsigned char> &data);

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
