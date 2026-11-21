/*
 * Open X-Com Companion — Mind Probe fair-play observation stats (M1 MIND-PROBE-18).
 */
#include "CompanionMindProbeObservationCore.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedGame.h"
#include "../Mod/Mod.h"
#include "../Engine/Options.h"

namespace OpenXcom
{

namespace CompanionMindProbeObservationCore
{

void appendAuthorizedMindProbeStats(YAML::Node &parent, BattleUnit *unit, SavedGame *campaign, Mod *mod)
{
	if (unit == 0 || unit->getBaseStats() == 0)
	{
		return;
	}
	const UnitStats *stats = unit->getBaseStats();
	const int maxHealth = stats->health > 0 ? stats->health : 1;
	const int health = unit->getHealth();

	parent["timeUnits"] = unit->getTimeUnits();
	parent["energy"] = unit->getEnergy();
	parent["health"] = health;
	parent["fatalWounds"] = unit->getFatalWounds();
	parent["bravery"] = stats->bravery;
	parent["morale"] = unit->getMorale();
	parent["reactions"] = stats->reactions;
	parent["firing"] = (stats->firing * health) / maxHealth;
	parent["throwing"] = (stats->throwing * health) / maxHealth;
	parent["melee"] = (stats->melee * health) / maxHealth;
	parent["strength"] = stats->strength;

	const bool exportPsiStrength =
		stats->psiSkill > 0 ||
		(Options::psiStrengthEval && campaign != 0 && mod != 0 &&
		 campaign->isResearched(mod->getPsiRequirements()));
	if (exportPsiStrength)
	{
		parent["psiStrength"] = stats->psiStrength;
	}
	if (stats->psiSkill > 0)
	{
		parent["psiSkill"] = stats->psiSkill;
	}

	YAML::Node armor;
	armor["front"] = unit->getArmor(SIDE_FRONT);
	armor["left"] = unit->getArmor(SIDE_LEFT);
	armor["right"] = unit->getArmor(SIDE_RIGHT);
	armor["rear"] = unit->getArmor(SIDE_REAR);
	armor["under"] = unit->getArmor(SIDE_UNDER);
	parent["armor"] = armor;
}

} // namespace CompanionMindProbeObservationCore

} // namespace OpenXcom