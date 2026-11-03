/*
 * Open X-Com Companion — snapshot size-cap resolution and atomic write (M1-17H / M1-17N).
 */
#include "CompanionSnapshotExportCore.h"
#include "CompanionSnapshotTerrain.h"
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#endif

namespace OpenXcom
{

namespace
{

bool deletePath(const std::string &path)
{
#ifdef _WIN32
	return DeleteFileA(path.c_str()) != 0;
#else
	return std::remove(path.c_str()) == 0;
#endif
}

bool movePath(const std::string &src, const std::string &dest)
{
#ifdef _WIN32
	return MoveFileExA(src.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
	std::ifstream srcStream;
	std::ofstream destStream;
	srcStream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	destStream.exceptions(std::ofstream::failbit | std::ofstream::badbit);
	try
	{
		srcStream.open(src.c_str(), std::ios::binary);
		destStream.open(dest.c_str(), std::ios::binary);
		destStream << srcStream.rdbuf();
		srcStream.close();
		destStream.close();
	}
	catch (const std::fstream::failure &)
	{
		return false;
	}
	return deletePath(src);
#endif
}

} // namespace

namespace CompanionSnapshotExportCore
{

const char *TERRAIN_OMITTED_DIAGNOSTIC()
{
	return "Companion snapshot: terrain omitted because serialized YAML exceeded 256 KiB cap; exported schemaVersion 4 without terrain";
}

std::string serializeSnapshotYaml(const YAML::Node &root)
{
	YAML::Emitter emitter;
	emitter << root;
	return std::string(emitter.c_str());
}

ResolvedSnapshot resolveSnapshotForSizeCap(const YAML::Node &schemaV5WithTerrain,
	const std::function<YAML::Node()> &buildSchemaV4WithoutTerrain)
{
	ResolvedSnapshot result = {};
	result.ok = false;
	result.schemaVersion = 0;
	result.terrainOmittedDueToSize = false;
	result.v5AttemptedBytes = 0;
	result.v4FallbackBytes = 0;

	const std::string v5Yaml = serializeSnapshotYaml(schemaV5WithTerrain);
	result.v5AttemptedBytes = v5Yaml.size();
	if (v5Yaml.size() <= CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES)
	{
		result.ok = true;
		result.yamlUtf8 = v5Yaml;
		result.schemaVersion = 5;
		return result;
	}

	const YAML::Node schemaV4WithoutTerrain = buildSchemaV4WithoutTerrain();
	const std::string v4Yaml = serializeSnapshotYaml(schemaV4WithoutTerrain);
	result.v4FallbackBytes = v4Yaml.size();
	if (v4Yaml.size() <= CompanionSnapshotTerrain::SNAPSHOT_MAX_BYTES)
	{
		result.ok = true;
		result.yamlUtf8 = v4Yaml;
		result.schemaVersion = 4;
		result.terrainOmittedDueToSize = true;
		return result;
	}

	return result;
}

bool writeSnapshotAtomically(const std::string &destPath, const std::string &yamlContent)
{
	const std::string tmpPath = destPath + ".companion.tmp";

	{
		std::ofstream out(tmpPath.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
		if (!out)
		{
			std::cerr << "Companion snapshot: failed to open temp file for write: " << tmpPath << std::endl;
			return false;
		}
		out << yamlContent;
		out.flush();
		if (!out)
		{
			std::cerr << "Companion snapshot: failed while writing temp file: " << tmpPath << std::endl;
			deletePath(tmpPath);
			return false;
		}
	}

	if (!movePath(tmpPath, destPath))
	{
		std::cerr << "Companion snapshot: failed to replace snapshot file: " << destPath << std::endl;
		deletePath(tmpPath);
		return false;
	}

	return true;
}

} // namespace CompanionSnapshotExportCore

} // namespace OpenXcom
