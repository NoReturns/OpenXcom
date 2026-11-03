#pragma once
/*
 * Open X-Com Companion — snapshot size-cap resolution and atomic write (M1-17H).
 */
#include <functional>
#include <string>
#include <yaml-cpp/yaml.h>

namespace OpenXcom
{

namespace CompanionSnapshotExportCore
{

struct ResolvedSnapshot
{
	bool ok;
	std::string yamlUtf8;
	int schemaVersion;
	bool terrainOmittedDueToSize;
	size_t v5AttemptedBytes;
	size_t v4FallbackBytes;
};

/// Log/diagnostic text when v5 exceeds cap and v4 fallback is used.
const char *TERRAIN_OMITTED_DIAGNOSTIC();

std::string serializeSnapshotYaml(const YAML::Node &root);

/**
 * Try schema v5 @p schemaV5WithTerrain first; on size cap overflow build v4 via @p buildSchemaV4WithoutTerrain.
 * Fails when both exceed CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES.
 */
ResolvedSnapshot resolveSnapshotForSizeCap(const YAML::Node &schemaV5WithTerrain,
	const std::function<YAML::Node()> &buildSchemaV4WithoutTerrain);

bool writeSnapshotAtomically(const std::string &destPath, const std::string &yamlContent);

} // namespace CompanionSnapshotExportCore

} // namespace OpenXcom
