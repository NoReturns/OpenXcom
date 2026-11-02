#pragma once
/*
 * Open X-Com Companion — read-only battlescape snapshot export (opt-in).
 */
namespace OpenXcom
{

class SavedBattleGame;

class CompanionSnapshot
{
public:
	/// Writes schema v3 YAML when OPENXCOM_COMPANION_SNAPSHOT_PATH is set and battle state qualifies.
	static void tryExport(SavedBattleGame *battle);
};

}
