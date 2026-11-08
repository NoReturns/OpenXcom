#pragma once
/*
 * Open X-Com Companion — production tactical snapshot YAML builder (M1-WP2 / WP2B test seam).
 */
#include <yaml-cpp/yaml.h>
#include <string>

namespace OpenXcom
{

class SavedBattleGame;
class SavedGame;

class CompanionSnapshotBuild
{
public:
	/// Builds the same YAML tree written by F6 / idle export (before size-cap resolution).
	/// @p campaign may be null; falls back to geoscape save reachable from @p battle when present.
	static YAML::Node buildSnapshot(
		SavedBattleGame *battle,
		SavedGame *campaign,
		int schemaVersion,
		bool includeTerrain,
		bool testFullRevealGeometry = false);

	static std::string serializeSnapshotYaml(const YAML::Node &root);
};

} // namespace OpenXcom
