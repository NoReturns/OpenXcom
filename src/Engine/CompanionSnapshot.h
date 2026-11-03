#pragma once
/*
 * Open X-Com Companion — read-only battlescape snapshot export (opt-in).
 */
#include <string>

namespace OpenXcom
{

class SavedBattleGame;

class CompanionSnapshot
{
public:
	/// Writes schema v5 YAML when under the 256 KiB cap; otherwise schema v4 without terrain (same intelligence).
	/// When OPENXCOM_COMPANION_SNAPSHOT_PATH is set and battle state qualifies.
	/// When @p outSnapshotSha256 is non-null, receives 64-char lowercase hex SHA-256 of the final written UTF-8 YAML.
	static bool tryExport(SavedBattleGame *battle, std::string *outSnapshotSha256 = 0);
};

}
