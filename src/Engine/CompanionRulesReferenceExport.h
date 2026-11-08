#pragma once
/*
 * Open X-Com Companion — manual equipment rules reference export (M1-WP5).
 */
namespace OpenXcom
{

class SavedGame;
class Mod;

class CompanionRulesReferenceExport
{
public:
	static bool snapshotPathConfigured();
	static bool tryExport(SavedGame *save, Mod *mod);
};

} // namespace OpenXcom
