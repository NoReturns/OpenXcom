/*
 * Open X-Com Companion — automatic local battle AAR capture at debrief (M1).
 */
#include "CompanionBattleAarCapture.h"
#include "CompanionBattleAarCaptureCore.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionSnapshotExportCore.h"
#include "Logger.h"
#include "../Battlescape/DebriefingState.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"

#include <cstdlib>
#include <string>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <cerrno>
#endif

namespace OpenXcom
{

namespace
{

const char *SNAPSHOT_ENV = "OPENXCOM_COMPANION_SNAPSHOT_PATH";

CompanionBattleAarCaptureCore::PhaseASnapshot g_phaseA = {};
bool g_phaseAStored = false;

static bool ensureDirectoryExists(const std::string &dir)
{
	if (dir.empty())
	{
		return false;
	}
#ifdef _WIN32
	if (_mkdir(dir.c_str()) == 0)
	{
		return true;
	}
	const DWORD attrs = GetFileAttributesA(dir.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	return mkdir(dir.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

bool resolveOutputDirectory(std::string *outDir)
{
	if (outDir == 0)
	{
		return false;
	}
	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	if (snapshotPath == 0 || snapshotPath[0] == '\0')
	{
		return false;
	}
	const std::string parent = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(snapshotPath);
	if (parent.empty())
	{
		return false;
	}
	*outDir = parent;
	return true;
}

} // namespace

bool CompanionBattleAarDebriefAccess::tryFinalizeAndWrite(DebriefingState *debrief, SavedGame *save)
{
	if (debrief == 0 || save == 0 || !g_phaseAStored)
	{
		return false;
	}
	if (save->getDebugMode())
	{
		return false;
	}
	std::string outDir;
	if (!resolveOutputDirectory(&outDir))
	{
		return false;
	}
	SavedBattleGame *battle = save->getSavedBattle();
	if (battle == 0)
	{
		return false;
	}

	CompanionBattleAarCaptureCore::DebriefOutcomeInput outcome = {};
	outcome.missionStatistics = debrief->_missionStatistics;
	outcome.debriefStats = &debrief->_stats;
	outcome.soldierStatIncreases = &debrief->_soldierStats;
	outcome.missionAborted = battle->isAborted();
	outcome.promotionsUiScheduled = debrief->_promotions;

	const CompanionBattleAarCaptureCore::BuildResult built =
		CompanionBattleAarCaptureCore::buildFinalizedJson(g_phaseA, save, battle, outcome);
	if (!built.ok)
	{
		Log(LOG_WARNING) << "Companion battle AAR: build failed (" << built.skipReason << ")";
		return false;
	}

	const std::string path = outDir + "/companion-battle-aar.json";
	if (!CompanionSnapshotExportCore::writeUtf8FileAtomically(path, built.jsonUtf8))
	{
		Log(LOG_WARNING) << "Companion battle AAR: atomic write failed: " << path;
		return false;
	}

	const std::string producedDir = outDir + "/aar-produced";
	if (!ensureDirectoryExists(producedDir))
	{
		Log(LOG_WARNING) << "Companion battle AAR: could not create produced directory: " << producedDir;
		return false;
	}
	const std::string producedPath = producedDir + "/" + built.reportId + ".json";
	if (!CompanionSnapshotExportCore::writeUtf8FileAtomically(producedPath, built.jsonUtf8))
	{
		Log(LOG_WARNING) << "Companion battle AAR: durable produced write failed: " << producedPath;
		return false;
	}

	Log(LOG_INFO) << "Companion battle AAR: wrote " << path << " and " << producedPath << " ("
	              << built.jsonUtf8.size() << " bytes, report_id=" << built.reportId << ")";
	g_phaseAStored = false;
	return true;
}

namespace CompanionBattleAarCapture
{

void capturePhaseA(SavedGame *save, SavedBattleGame *battle)
{
	if (save == 0 || battle == 0 || save->getDebugMode())
	{
		return;
	}
	if (std::getenv(SNAPSHOT_ENV) == 0)
	{
		return;
	}
	g_phaseA = CompanionBattleAarCaptureCore::capturePhaseAData(save, battle);
	g_phaseAStored = g_phaseA.captured;
}

bool tryFinalizeAndWrite(DebriefingState *debrief, SavedGame *save)
{
	return CompanionBattleAarDebriefAccess::tryFinalizeAndWrite(debrief, save);
}

const CompanionBattleAarCaptureCore::PhaseASnapshot *testLastPhaseA()
{
	return g_phaseAStored ? &g_phaseA : 0;
}

void testResetPhaseA()
{
	g_phaseA = CompanionBattleAarCaptureCore::PhaseASnapshot();
	g_phaseAStored = false;
}

} // namespace CompanionBattleAarCapture

} // namespace OpenXcom