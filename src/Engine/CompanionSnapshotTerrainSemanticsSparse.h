#pragma once
/*
 * Open X-Com Companion — sparse-v1 terrain semantics blob encoder (M1-WP1).
 */
#include "CompanionSnapshotTerrainCapture.h"
#include <string>
#include <vector>

namespace OpenXcom
{

namespace CompanionSnapshotTerrain
{

const size_t SEMANTICS_SPARSE_V1_HEADER_BYTES = 12;
const size_t SEMANTICS_SPARSE_V1_MAX_RECORD_BYTES = 7;
const size_t SEMANTICS_SPARSE_V2_MAX_RECORD_BYTES = 8;

std::vector<unsigned char> encodeSemanticsSparseV1Blob(const std::vector<TerrainTileExport> &tiles);
std::vector<unsigned char> encodeSemanticsSparseV2Blob(const std::vector<TerrainTileExport> &tiles);

} // namespace CompanionSnapshotTerrain

} // namespace OpenXcom
