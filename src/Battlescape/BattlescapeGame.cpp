/*
 * Copyright 2010-2016 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <sstream>
#include "BattlescapeGame.h"
#include "BattlescapeState.h"
#include "Map.h"
#include "Camera.h"
#include "NextTurnState.h"
#include "BattleState.h"
#include "UnitTurnBState.h"
#include "UnitWalkBState.h"
#include "ProjectileFlyBState.h"
#include "MeleeAttackBState.h"
#include "PsiAttackBState.h"
#include "ExplosionBState.h"
#include "TileEngine.h"
#include "UnitInfoState.h"
#include "UnitDieBState.h"
#include "UnitPanicBState.h"
#include "AIModule.h"
#include "Pathfinding.h"
#include "../Mod/AlienDeployment.h"
#include "../Engine/Game.h"
#include "../Savegame/SavedGame.h"
#include "../Engine/Language.h"
#include "../Engine/Sound.h"
#include "../Mod/Mod.h"
#include "../Interface/Cursor.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/Armor.h"
#include "../Engine/Options.h"
#include "../Engine/RNG.h"
#include "InfoboxState.h"
#include "InfoboxOKState.h"
#include "UnitFallBState.h"
#include "../Engine/Logger.h"
#include "../Engine/CompanionSnapshot.h"
#include "../Engine/CompanionCommandRuntime.h"
#include "../Engine/CompanionWalkStopTokens.h"
#include "../Engine/CompanionRelayPublicationFreshnessCore.h"
#include "../Engine/CompanionCommandRuntimeSupport.h"
#include "../Engine/CompanionCommandCore.h"
#include "../Engine/CompanionBriefingRequest.h"
#include "../Engine/CompanionBriefingTestSupport.h"
#include "../Engine/CompanionOnDemandRelayCore.h"
#include "../Engine/CompanionTacticalAuthorityCore.h"
#include "../Engine/CompanionPerceptionLog.h"
#include "../Engine/CompanionGroundInventoryCore.h"
#include "../Savegame/BattleUnitStatistics.h"
#include "../Savegame/Soldier.h"
#include "../fmath.h"
#include <map>

namespace OpenXcom
{

bool BattlescapeGame::_debugPlay = false;

namespace
{

static bool companionInventoryOverlapItems(BattleUnit *unit, BattleItem *movingItem,
	RuleInventory *destSlot, int destX, int destY)
{
	if (unit == 0 || movingItem == 0 || destSlot == 0)
	{
		return true;
	}
	if (destSlot->getType() == INV_GROUND)
	{
		return true;
	}
	for (std::vector<BattleItem*>::const_iterator i = unit->getInventory()->begin();
	     i != unit->getInventory()->end(); ++i)
	{
		if (*i == 0 || *i == movingItem)
		{
			continue;
		}
		// Nested magazines use no inventory slot; they do not occupy grid cells.
		if ((*i)->getSlot() == 0)
		{
			continue;
		}
		if ((*i)->getSlot() == destSlot && (*i)->occupiesSlot(destX, destY, movingItem))
		{
			return true;
		}
	}
	return false;
}

} // namespace

/**
 * Initializes all the elements in the Battlescape screen.
 * @param save Pointer to the save game.
 * @param parentState Pointer to the parent battlescape state.
 */
BattlescapeGame::BattlescapeGame(SavedBattleGame *save, BattlescapeState *parentState) : _save(save), _parentState(parentState), _playerPanicHandled(true), _AIActionCounter(0), _AISecondMove(false), _playedAggroSound(false), _endTurnRequested(false), _endTurnProcessed(false), _companionBriefingPending(false)
{

	_currentAction.actor = 0;
	_currentAction.targeting = false;
	_currentAction.type = BA_NONE;

	_debugPlay = false;

	if (_parentState != 0)
	{
		checkForCasualties(0, 0, true);
	}
	cancelCurrentAction();
}


/**
 * Delete BattlescapeGame.
 */
BattlescapeGame::~BattlescapeGame()
{
	for (std::list<BattleState*>::iterator i = _states.begin(); i != _states.end(); ++i)
	{
		delete *i;
	}
	cleanupDeleted();
}

/**
 * Checks for units panicking or falling and so on.
 */
void BattlescapeGame::think()
{
	// nothing is happening - see if we need some alien AI or units panicking or what have you
	if (_states.empty())
	{
		if (_save->getUnitsFalling())
		{
			statePushFront(new UnitFallBState(this));
			_save->setUnitsFalling(false);
			return;
		}
		// it's a non player side (ALIENS or CIVILIANS)
		if (_save->getSide() != FACTION_PLAYER)
		{
			_save->resetUnitHitStates();
			if (!_debugPlay)
			{
				if (_save->getSelectedUnit())
				{
					if (!handlePanickingUnit(_save->getSelectedUnit()))
						handleAI(_save->getSelectedUnit());
				}
				else
				{
					if (_save->selectNextPlayerUnit(true, _AISecondMove) == 0)
					{
						if (!_save->getDebugMode())
						{
							_endTurnRequested = true;
							statePushBack(0); // end AI turn
						}
						else
						{
							_save->selectNextPlayerUnit();
							_debugPlay = true;
						}
					}
				}
			}
		}
		else
		{
			// it's a player side && we have not handled all panicking units
			if (!_playerPanicHandled)
			{
				_playerPanicHandled = handlePanickingPlayer();
				_save->getBattleState()->updateSoldierInfo();
			}
		}
	}
	tryFulfillCompanionBriefing(true);
	if (CompanionCommandRuntimeSupport::shouldRunCompanionCommandTickDuringThink(
		_save->getSide() == FACTION_PLAYER,
		_save->getDebugMode(),
		_save->getUnitsFalling(),
		_states.empty(),
		isBusy(),
		CompanionCommandRuntime::isMoveInFlight()))
	{
		tickCompanionCommand(true);
	}
}

/**
 * Initializes the Battlescape game.
 */
void BattlescapeGame::init()
{
	SavedGame *campaign = _save->getGeoscapeSave();
	_save->ensureCompanionTacticalOwnershipInitialized(campaign);
	CompanionCommandRuntime::onBattleCompanionBindingContext(_save);
	const std::string &companionSessionId = _save->getCompanionEventLog().getSessionId();
	if (companionSessionId.empty())
	{
		_save->getCompanionEventLog().beginSession();
		const std::string &newSessionId = _save->getCompanionEventLog().getSessionId();
		_save->getCompanionPerceptionLog().beginSession(newSessionId);
		_save->getCompanionGroundItemMemory().beginSession(newSessionId);
	}
	else
	{
		if (_save->getCompanionPerceptionLog().getSessionId() != companionSessionId)
		{
			_save->getCompanionPerceptionLog().syncSessionId(companionSessionId);
		}
		if (_save->getCompanionGroundItemMemory().getSessionId() != companionSessionId)
		{
			_save->getCompanionGroundItemMemory().syncSessionId(companionSessionId);
		}
	}
	CompanionSnapshot::tryPublishSoldierControl(_save, campaign);
	if (_save->getSide() == FACTION_PLAYER && _save->getTurn() > 1)
	{
		_playerPanicHandled = false;
	}
}


/**
 * Handles the processing of the AI states of a unit.
 * @param unit Pointer to a unit.
 */
void BattlescapeGame::handleAI(BattleUnit *unit)
{
	std::ostringstream ss;

	if (unit->getTimeUnits() <= 5)
	{
		unit->dontReselect();
	}
	if (_AIActionCounter >= 2 || !unit->reselectAllowed())
	{
		if (_save->selectNextPlayerUnit(true, _AISecondMove) == 0)
		{
			if (!_save->getDebugMode())
			{
				_endTurnRequested = true;
				statePushBack(0); // end AI turn
			}
			else
			{
				_save->selectNextPlayerUnit();
				_debugPlay = true;
			}
		}
		if (_save->getSelectedUnit())
		{
			_parentState->updateSoldierInfo();
			getMap()->getCamera()->centerOnPosition(_save->getSelectedUnit()->getPosition());
			if (_save->getSelectedUnit()->getId() <= unit->getId())
			{
				_AISecondMove = true;
			}
		}
		_AIActionCounter = 0;
		return;
	}

	unit->setVisible(false);

	_save->getTileEngine()->calculateFOV(unit->getPosition()); // might need this populate _visibleUnit for a newly-created alien
		// it might also help chryssalids realize they've zombified someone and need to move on
		// it should also hide units when they've killed the guy spotting them
		// it's also for good luck

	AIModule *ai = unit->getAIModule();
	if (!ai)
	{
		// for some reason the unit had no AI routine assigned..
		unit->setAIModule(new AIModule(_save, unit, 0));
		ai = unit->getAIModule();
	}
	_AIActionCounter++;
	if (_AIActionCounter == 1)
	{
		_playedAggroSound = false;
		unit->setHiding(false);
		if (Options::traceAI) { Log(LOG_INFO) << "#" << unit->getId() << "--" << unit->getType(); }
	}

	BattleAction action;
	action.actor = unit;
	action.number = _AIActionCounter;
	unit->think(&action);

	if (action.type == BA_RETHINK)
	{
		_parentState->debug("Rethink");
		unit->think(&action);
	}

	_AIActionCounter = action.number;
	BattleItem *weapon = unit->getMainHandWeapon();
	if (!weapon || !weapon->getAmmoItem())
	{
		if (unit->getOriginalFaction() == FACTION_HOSTILE && unit->getVisibleUnits()->empty())
		{
			findItem(&action);
		}
	}

	if (unit->getCharging() != 0)
	{
		if (unit->getAggroSound() != -1 && !_playedAggroSound)
		{
			getMod()->getSoundByDepth(_save->getDepth(), unit->getAggroSound())->play(-1, getMap()->getSoundAngle(unit->getPosition()));
			_playedAggroSound = true;
		}
	}
	if (action.type == BA_WALK)
	{
		ss << "Walking to " << action.target;
		_parentState->debug(ss.str());

		if (_save->getTile(action.target))
		{
			_save->getPathfinding()->calculate(action.actor, action.target);//, _save->getTile(action.target)->getUnit());
		}
		if (_save->getPathfinding()->getStartDirection() != -1)
		{
			statePushBack(new UnitWalkBState(this, action));
		}
	}

	if (action.type == BA_SNAPSHOT || action.type == BA_AUTOSHOT || action.type == BA_AIMEDSHOT || action.type == BA_THROW || action.type == BA_HIT || action.type == BA_MINDCONTROL || action.type == BA_PANIC || action.type == BA_LAUNCH)
	{
		ss.clear();
		ss << "Attack type=" << action.type << " target="<< action.target << " weapon=" << action.weapon->getRules()->getName();
		_parentState->debug(ss.str());
		action.TU = unit->getActionTUs(action.type, action.weapon);
		if (action.type == BA_MINDCONTROL || action.type == BA_PANIC)
		{
			statePushBack(new PsiAttackBState(this, action));
		}
		else
		{
			statePushBack(new UnitTurnBState(this, action));
			if (action.type == BA_HIT)
			{
				action.weapon = unit->getMeleeWeapon();
				statePushBack(new MeleeAttackBState(this, action));
			}
			else
			{
				statePushBack(new ProjectileFlyBState(this, action));
			}
		}
	}

	if (action.type == BA_NONE)
	{
		_parentState->debug("Idle");
		_AIActionCounter = 0;
		if (_save->selectNextPlayerUnit(true, _AISecondMove) == 0)
		{
			if (!_save->getDebugMode())
			{
				_endTurnRequested = true;
				statePushBack(0); // end AI turn
			}
			else
			{
				_save->selectNextPlayerUnit();
				_debugPlay = true;
			}
		}
		if (_save->getSelectedUnit())
		{
			_parentState->updateSoldierInfo();
			getMap()->getCamera()->centerOnPosition(_save->getSelectedUnit()->getPosition());
			if (_save->getSelectedUnit()->getId() <= unit->getId())
			{
				_AISecondMove = true;
			}
		}
	}
}

/**
 * Toggles the Kneel/Standup status of the unit.
 * @param bu Pointer to a unit.
 * @return If the action succeeded.
 */
bool BattlescapeGame::kneel(BattleUnit *bu)
{
	int tu = bu->isKneeled()?8:4;
	if (bu->getType() == "SOLDIER" && !bu->isFloating() && ((!bu->isKneeled() && _save->getKneelReserved()) || checkReservedTU(bu, tu)))
	{
		if (bu->spendTimeUnits(tu))
		{
			bu->kneel(!bu->isKneeled());
			// kneeling or standing up can reveal new terrain or units. I guess.
			getTileEngine()->calculateFOV(bu);
			getMap()->cacheUnits();
			_parentState->updateSoldierInfo();
			getTileEngine()->checkReactionFire(bu);
			if (bu->getFaction() == FACTION_PLAYER
			    && (_save->getSide() == FACTION_PLAYER || _debugPlay))
			{
				CompanionRelayPublicationFreshnessCore::notePlayerTacticalMutationIfSnapshotConfigured();
			}
			return true;
		}
		else
		{
			_parentState->warning("STR_NOT_ENOUGH_TIME_UNITS");
		}
	}
	return false;
}

/**
 * Ends the turn.
 */
void BattlescapeGame::endTurn()
{
	_debugPlay = false;
	_currentAction.type = BA_NONE;
	getMap()->getWaypoints()->clear();
	_currentAction.waypoints.clear();
	_parentState->showLaunchButton(false);
	_currentAction.targeting = false;
	_AISecondMove = false;

	if (!_endTurnProcessed)
	{
		if (_save->getTileEngine()->closeUfoDoors() && Mod::SLIDING_DOOR_CLOSE != -1)
		{
			getMod()->getSoundByDepth(_save->getDepth(), Mod::SLIDING_DOOR_CLOSE)->play(); // ufo door closed
		}

		Position p;

		// check for hot grenades on the ground
		if (_save->getSide() != FACTION_NEUTRAL)
		{
			for (int i = 0; i < _save->getMapSizeXYZ(); ++i)
			{
				for (std::vector<BattleItem *>::iterator it = _save->getTiles()[i]->getInventory()->begin(); it != _save->getTiles()[i]->getInventory()->end(); )
				{
					if ((*it)->getRules()->getBattleType() == BT_GRENADE && (*it)->getFuseTimer() == 0)  // it's a grenade to explode now
					{
						p.x = _save->getTiles()[i]->getPosition().x * 16 + 8;
						p.y = _save->getTiles()[i]->getPosition().y * 16 + 8;
						p.z = _save->getTiles()[i]->getPosition().z * 24 - _save->getTiles()[i]->getTerrainLevel();
						statePushNext(new ExplosionBState(this, p, (*it), (*it)->getPreviousOwner()));
						_save->removeItem((*it));
						statePushBack(0);
						return;
					}
					++it;
				}
			}
		}
	}
	// check for terrain explosions
	Tile *t = _save->getTileEngine()->checkForTerrainExplosions();
	if (t)
	{
		Position p = Position(t->getPosition().x * 16, t->getPosition().y * 16, t->getPosition().z * 24);
		statePushNext(new ExplosionBState(this, p, 0, 0, t));
		statePushBack(0);
		return;
	}

	if (!_endTurnProcessed)
	{
		if (_save->getSide() != FACTION_NEUTRAL)
		{
			for (std::vector<BattleItem*>::iterator it = _save->getItems()->begin(); it != _save->getItems()->end(); ++it)
			{
					if (((*it)->getRules()->getBattleType() == BT_GRENADE || (*it)->getRules()->getBattleType() == BT_PROXIMITYGRENADE) && (*it)->getFuseTimer() > 0)
					{
						(*it)->setFuseTimer((*it)->getFuseTimer() - 1);
					}
			}
		}


		_save->endTurn();
		t = _save->getTileEngine()->checkForTerrainExplosions();
		if (t)
		{
			Position p = Position(t->getPosition().x * 16, t->getPosition().y * 16, t->getPosition().z * 24);
			statePushNext(new ExplosionBState(this, p, 0, 0, t));
			statePushBack(0);
			_endTurnProcessed = true;
			return;
		}
	}

	_endTurnProcessed = false;

	if (_save->getSide() == FACTION_PLAYER)
	{
		setupCursor();
	}
	else
	{
		getMap()->setCursorType(CT_NONE);
	}

	checkForCasualties(0, 0, false, false);

	// turn off MCed alien lighting.
	_save->getTileEngine()->calculateUnitLighting();

	// if all units from either faction are killed - the mission is over.
	int liveAliens = 0;
	int liveSoldiers = 0;
	int inExit = 0;

	// Calculate values
	for (std::vector<BattleUnit*>::iterator j = _save->getUnits()->begin(); j != _save->getUnits()->end(); ++j)
	{
		if (!(*j)->isOut())
		{
			if ((*j)->getOriginalFaction() == FACTION_HOSTILE)
			{
				if (!Options::allowPsionicCapture || (*j)->getFaction() != FACTION_PLAYER || !(*j)->getCapturable())
				{
					liveAliens++;
				}
			}
			else if ((*j)->getOriginalFaction() == FACTION_PLAYER)
			{
				if ((*j)->isInExitArea(END_POINT))
				{
					inExit++;
				}
				if ((*j)->getFaction() == FACTION_PLAYER)
				{
					liveSoldiers++;
				}
				else
				{
					liveAliens++;
				}
			}
		}
	}

	if (_save->allObjectivesDestroyed() && _save->getObjectiveType() == MUST_DESTROY)
	{
		_parentState->finishBattle(false, liveSoldiers);
		return;
	}
	if (_save->getTurnLimit() > 0 && _save->getTurn() > _save->getTurnLimit())
	{
		switch (_save->getChronoTrigger())
		{
		case FORCE_ABORT:
			_save->setAborted(true);
			_parentState->finishBattle(true, inExit);
			return;
		case FORCE_WIN:
			_parentState->finishBattle(false, liveSoldiers);
			return;
		case FORCE_LOSE:
		default:
			// force mission failure
			_save->setAborted(true);
			_parentState->finishBattle(false, 0);
			return;
		}
	}

	if (liveAliens > 0 && liveSoldiers > 0)
	{
		showInfoBoxQueue();

		_parentState->updateSoldierInfo();

		if (playableUnitSelected())
		{
			getMap()->getCamera()->centerOnPosition(_save->getSelectedUnit()->getPosition());
			setupCursor();
		}
	}

	bool battleComplete = liveAliens == 0 || liveSoldiers == 0;

	if ((_save->getSide() != FACTION_NEUTRAL || battleComplete)
		&& _endTurnRequested)
	{
		_parentState->getGame()->pushState(new NextTurnState(_save, _parentState));
	}
	_endTurnRequested = false;
}


/**
 * Checks for casualties and adjusts morale accordingly.
 * @param murderweapon Need to know this, for a HE explosion there is an instant death.
 * @param origMurderer This is needed for credits for the kill.
 * @param hiddenExplosion Set to true for the explosions of UFO Power sources at start of battlescape.
 * @param terrainExplosion Set to true for the explosions of terrain.
 */
void BattlescapeGame::checkForCasualties(BattleItem *murderweapon, BattleUnit *origMurderer, bool hiddenExplosion, bool terrainExplosion)
{
	// If the victim was killed by the murderer's death explosion, fetch who killed the murderer and make HIM the murderer!
	if (origMurderer && !origMurderer->getGeoscapeSoldier() && (origMurderer->getUnitRules()->getSpecialAbility() == SPECAB_EXPLODEONDEATH || origMurderer->getUnitRules()->getSpecialAbility() == SPECAB_BURN_AND_EXPLODE)
		&& origMurderer->getStatus() == STATUS_DEAD && origMurderer->getMurdererId() != 0)
	{
		for (std::vector<BattleUnit*>::const_iterator i = _save->getUnits()->begin(); i != _save->getUnits()->end(); ++i)
		{
			if ((*i)->getId() == origMurderer->getMurdererId())
			{
				origMurderer = (*i);
			}
		}
	}

	// Fetch the murder weapon
	std::string tempWeapon = "STR_WEAPON_UNKNOWN", tempAmmo = "STR_WEAPON_UNKNOWN";
	if (origMurderer)
	{
		if (murderweapon)
		{
			tempAmmo = murderweapon->getRules()->getName();
			tempWeapon = tempAmmo;
		}

		BattleItem *weapon = origMurderer->getItem("STR_RIGHT_HAND");
		if (weapon)
		{
			for (std::vector<std::string>::iterator c = weapon->getRules()->getCompatibleAmmo()->begin(); c != weapon->getRules()->getCompatibleAmmo()->end(); ++c)
			{
				if ((*c) == tempAmmo)
				{
					tempWeapon = weapon->getRules()->getName();
				}
			}
		}
		weapon = origMurderer->getItem("STR_LEFT_HAND");
		if (weapon)
		{
			for (std::vector<std::string>::iterator c = weapon->getRules()->getCompatibleAmmo()->begin(); c != weapon->getRules()->getCompatibleAmmo()->end(); ++c)
			{
				if ((*c) == tempAmmo)
				{
					tempWeapon = weapon->getRules()->getName();
				}
			}
		}
	}

	for (std::vector<BattleUnit*>::iterator j = _save->getUnits()->begin(); j != _save->getUnits()->end(); ++j)
	{
		if ((*j)->getStatus() == STATUS_IGNORE_ME) continue;
		BattleUnit *victim = (*j);
		BattleUnit *murderer = origMurderer;

		BattleUnitKills killStat;
		killStat.mission = _parentState->getGame()->getSavedGame()->getMissionStatistics()->size();
		killStat.setTurn(_save->getTurn(), _save->getSide());
		killStat.setUnitStats(victim);
		killStat.faction = victim->getFaction();
		killStat.side = victim->getFatalShotSide();
		killStat.bodypart = victim->getFatalShotBodyPart();
		killStat.id = victim->getId();
		killStat.weapon = tempWeapon;
		killStat.weaponAmmo = tempAmmo;

		// Determine murder type
		if ((*j)->getStatus() != STATUS_DEAD)
		{
			if ((*j)->getHealth() == 0)
			{
				killStat.status = STATUS_DEAD;
			}
			else if ((*j)->getStunlevel() >= (*j)->getHealth() && (*j)->getStatus() != STATUS_UNCONSCIOUS)
			{
				killStat.status = STATUS_UNCONSCIOUS;
			}
		}

		// Assume that, in absence of a murderer and an explosion, the laster unit to hit the victim is the murderer.
		// Possible causes of death: bleed out, fire.
		// Possible causes of unconciousness: wounds, smoke.
		// Assumption : The last person to hit the victim is the murderer.
		if (!murderer && !terrainExplosion)
		{
			for (std::vector<BattleUnit*>::const_iterator i = _save->getUnits()->begin(); i != _save->getUnits()->end(); ++i)
			{
				if ((*i)->getId() == victim->getMurdererId())
				{
					murderer = (*i);
					killStat.weapon = victim->getMurdererWeapon();
					killStat.weaponAmmo = victim->getMurdererWeaponAmmo();
					break;
				}
			}
		}

		if (murderer && killStat.status != STATUS_IGNORE_ME)
		{
			if (murderer->getFaction() == FACTION_PLAYER && murderer->getOriginalFaction() != FACTION_PLAYER)
			{
				// This must be a mind controlled unit. Find out who mind controlled him and award the kill to that unit.
				for (std::vector<BattleUnit*>::iterator i = _save->getUnits()->begin(); i != _save->getUnits()->end(); ++i)
				{
					if ((*i)->getId() == murderer->getMindControllerId() && (*i)->getGeoscapeSoldier())
					{
						(*i)->getStatistics()->kills.push_back(new BattleUnitKills(killStat));
						if (victim->getFaction() == FACTION_HOSTILE)
						{
							(*i)->getStatistics()->slaveKills++;
						}
						victim->setMurdererId((*i)->getId());
						break;
					}
				}
			}
			else if (!murderer->getStatistics()->duplicateEntry(killStat.status, victim->getId()))
			{
				murderer->getStatistics()->kills.push_back(new BattleUnitKills(killStat));
				victim->setMurdererId(murderer->getId());
			}
		}

		bool noSound = false;
		bool noCorpse = false;
		if ((*j)->getStatus() != STATUS_DEAD)
		{
			if ((*j)->getHealth() == 0)
			{
				if ((*j)->getStatus() == STATUS_UNCONSCIOUS)
				{
					noCorpse = true;
				}
				if (murderer)
				{
					murderer->addKillCount();
					victim->killedBy(murderer->getFaction());
					int modifier = murderer->getFaction() == FACTION_PLAYER ? _save->getMoraleModifier() : 100;

					// if there is a known murderer, he will get a morale bonus if he is of a different faction (what with neutral?)
					if ((victim->getOriginalFaction() == FACTION_PLAYER && murderer->getFaction() == FACTION_HOSTILE) ||
						(victim->getOriginalFaction() == FACTION_HOSTILE && murderer->getFaction() == FACTION_PLAYER))
					{
						murderer->moraleChange(20 * modifier / 100);
					}
					// murderer will get a penalty with friendly fire
					if (victim->getOriginalFaction() == murderer->getOriginalFaction())
					{
						murderer->moraleChange(-(2000 / modifier));
					}
					if (victim->getOriginalFaction() == FACTION_NEUTRAL)
					{
						if (murderer->getOriginalFaction() == FACTION_PLAYER)
						{
							murderer->moraleChange(-(1000 / modifier));
						}
						else
						{
							murderer->moraleChange(10);
						}
					}
				}

				if (victim->getFaction() != FACTION_NEUTRAL)
				{
					int modifier = _save->getMoraleModifier(victim);
					int loserMod = victim->getFaction() == FACTION_HOSTILE ? 100 : _save->getMoraleModifier();
					int winnerMod = victim->getFaction() == FACTION_HOSTILE ? _save->getMoraleModifier() : 100;
					for (std::vector<BattleUnit*>::iterator i = _save->getUnits()->begin(); i != _save->getUnits()->end(); ++i)
					{
						if (!(*i)->isOut() && (*i)->getArmor()->getSize() == 1)
						{
							// the losing squad all get a morale loss
							if ((*i)->getOriginalFaction() == victim->getOriginalFaction())
							{
								int bravery = (110 - (*i)->getBaseStats()->bravery) / 10;
								(*i)->moraleChange(-(modifier * 200 * bravery / loserMod / 100));

								if (victim->getFaction() == FACTION_HOSTILE && murderer)
								{
									murderer->setTurnsSinceSpotted(0);
								}
							}
							// the winning squad all get a morale increase
							else
							{
								(*i)->moraleChange(10 * winnerMod / 100);
							}
						}
					}
				}
				if (murderweapon)
				{
					statePushNext(new UnitDieBState(this, (*j), murderweapon->getRules()->getDamageType(), noSound, noCorpse));
				}
				else
				{
					if (hiddenExplosion)
					{
						// this is instant death from UFO powersources, without screaming sounds
						noSound = true;
						statePushNext(new UnitDieBState(this, (*j), DT_HE, noSound, noCorpse));
					}
					else
					{
						if (terrainExplosion)
						{
							// terrain explosion
							statePushNext(new UnitDieBState(this, (*j), DT_HE, noSound, noCorpse));
						}
						else
						{
							// no murderer, and no terrain explosion, must be fatal wounds
							statePushNext(new UnitDieBState(this, (*j), DT_NONE, noSound, noCorpse));  // DT_NONE = STR_HAS_DIED_FROM_A_FATAL_WOUND
						}
					}
				}
				// one of our own died, record the murderer instead of the victim
				if (victim->getGeoscapeSoldier())
				{
					victim->getStatistics()->KIA = true;
					BattleUnitKills *deathStat = new BattleUnitKills(killStat);
					if (murderer)
					{
						deathStat->setUnitStats(murderer);
						deathStat->faction = murderer->getFaction();
					}
					_parentState->getGame()->getSavedGame()->killSoldier(victim->getGeoscapeSoldier(), deathStat);
				}
			}
			else if ((*j)->getStunlevel() >= (*j)->getHealth() && (*j)->getStatus() != STATUS_UNCONSCIOUS)
			{
				if (victim->getGeoscapeSoldier())
				{
					victim->getStatistics()->wasUnconcious = true;
				}
				noSound = true;
				statePushNext(new UnitDieBState(this, (*j), DT_STUN, noSound, noCorpse));
			}
		}
	}

	BattleUnit *bu = _save->getSelectedUnit();
	if (_save->getSide() == FACTION_PLAYER)
	{
		_parentState->showPsiButton(bu && bu->getSpecialWeapon(BT_PSIAMP) && !bu->isOut());
	}
}

/**
 * Shows the infoboxes in the queue (if any).
 */
void BattlescapeGame::showInfoBoxQueue()
{
	for (std::vector<InfoboxOKState*>::iterator i = _infoboxQueue.begin(); i != _infoboxQueue.end(); ++i)
	{
		_parentState->getGame()->pushState(*i);
	}

	_infoboxQueue.clear();
}

/**
 * Sets up a mission complete notification.
 */
void BattlescapeGame::missionComplete()
{
	Game *game = _parentState->getGame();
	if (game->getMod()->getDeployment(_save->getMissionType()))
	{
		std::string missionComplete = game->getMod()->getDeployment(_save->getMissionType())->getObjectivePopup();
		if (!missionComplete.empty())
		{
			_infoboxQueue.push_back(new InfoboxOKState(game->getLanguage()->getString(missionComplete)));
		}
	}
}

/**
 * Handles the result of non target actions, like priming a grenade.
 */
void BattlescapeGame::handleNonTargetAction()
{
	if (!_currentAction.targeting)
	{
		_currentAction.cameraPosition = Position(0,0,-1);
		if (!_currentAction.result.empty())
		{
			_parentState->warning(_currentAction.result);
			_currentAction.result = "";
		}
		else if (_currentAction.type == BA_PRIME && _currentAction.value > -1)
		{
			if (warnIfHumanPlayerOrderDenied(_currentAction.actor))
			{
				_currentAction.type = BA_NONE;
			}
			else if (_currentAction.actor->spendTimeUnits(_currentAction.TU))
			{
				_parentState->warning("STR_GRENADE_IS_ACTIVATED");
				_currentAction.weapon->setFuseTimer(_currentAction.value);
			}
			else
			{
				_parentState->warning("STR_NOT_ENOUGH_TIME_UNITS");
			}
		}
		else if (_currentAction.type == BA_USE)
		{
			_save->reviveUnconsciousUnits();
		}
		else if (_currentAction.type == BA_HIT)
		{
			if (warnIfHumanPlayerOrderDenied(_currentAction.actor))
			{
				_currentAction.type = BA_NONE;
			}
			else if (_currentAction.actor->spendTimeUnits(_currentAction.TU))
			{
				statePushBack(new MeleeAttackBState(this, _currentAction));
			}
			else
			{
				_parentState->warning("STR_NOT_ENOUGH_TIME_UNITS");
			}
		}
		if (_currentAction.type != BA_HIT) // don't clear the action type if we're meleeing, let the melee action state take care of that
		{
			_currentAction.type = BA_NONE;
		}
		_parentState->updateSoldierInfo();
	}

	setupCursor();
}

/**
 * Sets the cursor according to the selected action.
 */
void BattlescapeGame::setupCursor()
{
	if (_currentAction.targeting)
	{
		if (_currentAction.type == BA_THROW)
		{
			getMap()->setCursorType(CT_THROW);
		}
		else if (_currentAction.type == BA_MINDCONTROL || _currentAction.type == BA_PANIC || _currentAction.type == BA_USE)
		{
			getMap()->setCursorType(CT_PSI);
		}
		else if (_currentAction.type == BA_LAUNCH)
		{
			getMap()->setCursorType(CT_WAYPOINT);
		}
		else
		{
			getMap()->setCursorType(CT_AIM);
		}
	}
	else if (_currentAction.type != BA_HIT)
	{
		_currentAction.actor = _save->getSelectedUnit();
		if (_currentAction.actor)
		{
			getMap()->setCursorType(CT_NORMAL, _currentAction.actor->getArmor()->getSize());
		}
		else
		{
			getMap()->setCursorType(CT_NORMAL);
		}
	}
}

/**
 * Determines whether a playable unit is selected. Normally only player side units can be selected, but in debug mode one can play with aliens too :)
 * Is used to see if stats can be displayed.
 * @return Whether a playable unit is selected.
 */
bool BattlescapeGame::playableUnitSelected() const
{
	return _save->getSelectedUnit() != 0 && (_save->getSide() == FACTION_PLAYER || _save->getDebugMode());
}

/**
 * Gives time slice to the front state.
 */
void BattlescapeGame::handleState()
{
	if (!_states.empty())
	{
		// end turn request?
		if (_states.front() == 0)
		{
			_states.pop_front();
			endTurn();
			return;
		}
		else
		{
			_states.front()->think();
		}
		getMap()->invalidate(); // redraw map
	}
}

/**
 * Pushes a state to the front of the queue and starts it.
 * @param bs Battlestate.
 */
void BattlescapeGame::statePushFront(BattleState *bs)
{
	_states.push_front(bs);
	bs->init();
}

/**
 * Pushes a state as the next state after the current one.
 * @param bs Battlestate.
 */
void BattlescapeGame::statePushNext(BattleState *bs)
{
	if (_states.empty())
	{
		_states.push_front(bs);
		bs->init();
	}
	else
	{
		_states.insert(++_states.begin(), bs);
	}

}

/**
 * Pushes a state to the back.
 * @param bs Battlestate.
 */
void BattlescapeGame::statePushBack(BattleState *bs)
{
	if (_states.empty())
	{
		_states.push_front(bs);
		// end turn request?
		if (_states.front() == 0)
		{
			_states.pop_front();
			endTurn();
			return;
		}
		else
		{
			bs->init();
		}
	}
	else
	{
		_states.push_back(bs);
	}
}

/**
 * Removes the current state.
 *
 * This is a very important function. It is called by a BattleState (walking, projectile is flying, explosions,...) at the moment this state has finished its action.
 * Here we check the result of that action and do all the aftermath.
 * The state is popped off the list.
 */
void BattlescapeGame::popState()
{
	if (Options::traceAI)
	{
		Log(LOG_INFO) << "BattlescapeGame::popState() #" << _AIActionCounter << " with " << (_save->getSelectedUnit() ? _save->getSelectedUnit()->getTimeUnits() : -9999) << " TU";
	}
	bool actionFailed = false;

	if (_states.empty()) return;

	BattleAction action = _states.front()->getAction();

	if (action.actor != 0 && action.playerOrdered &&
	    (action.type == BA_SNAPSHOT || action.type == BA_AUTOSHOT || action.type == BA_AIMEDSHOT))
	{
		CompanionCommandRuntime::noteSnapShotBattleActionPop(action.actor->getId(), action.result);
	}
	if (action.actor != 0 && action.playerOrdered && action.type == BA_THROW)
	{
		CompanionCommandRuntime::noteThrowBattleActionPop(action.actor->getId(), action.result);
	}
	if (action.actor != 0 && action.playerOrdered && action.type == BA_WALK)
	{
		CompanionCommandRuntime::noteWalkBattleActionPop(action.actor->getId(), action.result);
	}

	if (action.actor && !action.result.empty() && action.actor->getFaction() == FACTION_PLAYER
		&& _playerPanicHandled && (_save->getSide() == FACTION_PLAYER || _debugPlay))
	{
		const bool companionWalkStopToken = action.type == BA_WALK
		    && CompanionWalkStopTokens::isWalkStopActionResult(action.result);
		if (!companionWalkStopToken && _parentState != 0)
		{
			_parentState->warning(action.result);
			actionFailed = true;
		}
		else if (!companionWalkStopToken)
		{
			actionFailed = true;
		}
	}
	// M1 blue SEND: skip mutation when actionFailed (non-empty action.result warning). The engine may
	// still have applied partial deltas (e.g. walk abort with 1 TU / facing) similar to Companion MOVE
	// interrupt diagnostics — see testSendFreshnessRegressionFailedActionPartialMutation in relay tests.
	if (!actionFailed && action.actor != 0 && action.playerOrdered
	    && action.actor->getFaction() == FACTION_PLAYER
	    && (_save->getSide() == FACTION_PLAYER || _debugPlay))
	{
		switch (action.type)
		{
		case BA_WALK:
		case BA_THROW:
		case BA_AUTOSHOT:
		case BA_SNAPSHOT:
		case BA_AIMEDSHOT:
		case BA_HIT:
		case BA_LAUNCH:
		case BA_USE:
		case BA_PRIME:
			CompanionRelayPublicationFreshnessCore::notePlayerTacticalMutationIfSnapshotConfigured();
			break;
		default:
			break;
		}
	}
	_deleted.push_back(_states.front());
	_states.pop_front();

	// handle the end of this unit's actions
	if (action.actor && noActionsPending(action.actor))
	{
		if (action.actor->getFaction() == FACTION_PLAYER)
		{
			// spend TUs of "target triggered actions" (shooting, throwing) only
			// the other actions' TUs (healing,scanning,..) are already take care of
			if (action.targeting && _save->getSelectedUnit() && !actionFailed)
			{
				action.actor->spendTimeUnits(action.TU);
			}
			if (_save->getSide() == FACTION_PLAYER)
			{
				// after throwing the cursor returns to default cursor, after shooting it stays in targeting mode and the player can shoot again in the same mode (autoshot,snap,aimed)
				if ((action.type == BA_THROW || action.type == BA_LAUNCH) && !actionFailed)
				{
					// clean up the waypoints
					if (action.type == BA_LAUNCH)
					{
						_currentAction.waypoints.clear();
					}

					cancelCurrentAction(true);
				}
				_parentState->getGame()->getCursor()->setVisible(true);
				setupCursor();
			}
		}
		else
		{
			// spend TUs
			action.actor->spendTimeUnits(action.TU);
			if (_save->getSide() != FACTION_PLAYER && !_debugPlay)
			{
				// AI does three things per unit, before switching to the next, or it got killed before doing the second thing
				if (_AIActionCounter > 2 || _save->getSelectedUnit() == 0 || _save->getSelectedUnit()->isOut())
				{
					if (_save->getSelectedUnit())
					{
						_save->getSelectedUnit()->setCache(0);
						getMap()->cacheUnit(_save->getSelectedUnit());
					}
					_AIActionCounter = 0;
					if (_states.empty() && _save->selectNextPlayerUnit(true) == 0)
					{
						if (!_save->getDebugMode())
						{
							_endTurnRequested = true;
							statePushBack(0); // end AI turn
						}
						else
						{
							_save->selectNextPlayerUnit();
							_debugPlay = true;
						}
					}
					if (_save->getSelectedUnit())
					{
						getMap()->getCamera()->centerOnPosition(_save->getSelectedUnit()->getPosition());
					}
				}
			}
			else if (_debugPlay)
			{
				_parentState->getGame()->getCursor()->setVisible(true);
				setupCursor();
			}
		}
	}

	if (!_states.empty())
	{
		// end turn request?
		if (_states.front() == 0)
		{
			while (!_states.empty())
			{
				if (_states.front() == 0)
					_states.pop_front();
				else
					break;
			}
			if (_states.empty())
			{
				endTurn();
				return;
			}
			else
			{
				_states.push_back(0);
			}
		}
		// init the next state in queue
		_states.front()->init();
	}

	// the currently selected unit died or became unconscious or disappeared inexplicably
	if (_save->getSelectedUnit() == 0 || _save->getSelectedUnit()->isOut())
	{
		cancelCurrentAction();
		getMap()->setCursorType(CT_NORMAL, 1);
		_parentState->getGame()->getCursor()->setVisible(true);
		if (_save->getSide() == FACTION_PLAYER)
			_save->setSelectedUnit(0);
		else
			_save->selectNextPlayerUnit(true, true);
	}
	_parentState->updateSoldierInfo();

	tryFulfillCompanionBriefing(true);
	tickCompanionCommand(true);

	if (_states.empty() && _save->getSide() == FACTION_PLAYER && !_save->getUnitsFalling() && !_save->getDebugMode())
	{
		CompanionSnapshot::tryExport(_save);
	}
}

/**
 * Determines whether there are any actions pending for the given unit.
 * @param bu BattleUnit.
 * @return True if there are no actions pending.
 */
bool BattlescapeGame::noActionsPending(BattleUnit *bu)
{
	if (_states.empty()) return true;

	for (std::list<BattleState*>::iterator i = _states.begin(); i != _states.end(); ++i)
	{
		if ((*i) != 0 && (*i)->getAction().actor == bu)
			return false;
	}

	return true;
}

/**
 * Sets the timer interval for think() calls of the state.
 * @param interval An interval in ms.
 */
void BattlescapeGame::setStateInterval(Uint32 interval)
{
	if (_parentState != 0)
	{
		_parentState->setStateInterval(interval);
	}
}


/**
 * Checks against reserved time units.
 * @param bu Pointer to the unit.
 * @param tu Number of time units to check.
 * @param justChecking True to suppress error messages, false otherwise.
 * @return bool Whether or not we got enough time units.
 */
bool BattlescapeGame::checkReservedTU(BattleUnit *bu, int tu, bool justChecking)
{
	BattleActionType effectiveTuReserved = _save->getTUReserved(); // avoid changing _tuReserved in this method

	if (_save->getSide() != bu->getFaction() || _save->getSide() == FACTION_NEUTRAL)
	{
		return tu <= bu->getTimeUnits();
	}

	if (_save->getSide() == FACTION_HOSTILE && !_debugPlay) // aliens reserve TUs as a percentage rather than just enough for a single action.
	{
		AIModule *ai = bu->getAIModule();
		if (ai)
		{
			effectiveTuReserved = ai->getReserveMode();
		}
		switch (effectiveTuReserved)
		{
		case BA_SNAPSHOT: return tu + (bu->getBaseStats()->tu / 3) <= bu->getTimeUnits(); // 33%
		case BA_AUTOSHOT: return tu + ((bu->getBaseStats()->tu / 5)*2) <= bu->getTimeUnits(); // 40%
		case BA_AIMEDSHOT: return tu + (bu->getBaseStats()->tu / 2) <= bu->getTimeUnits(); // 50%
		default: return tu <= bu->getTimeUnits();
		}
	}

	// check TUs against slowest weapon if we have two weapons
	BattleItem *slowestWeapon = bu->getMainHandWeapon(false);
	// if the weapon has no autoshot, reserve TUs for snapshot
	if (bu->getActionTUs(effectiveTuReserved, slowestWeapon) == 0 && effectiveTuReserved == BA_AUTOSHOT)
	{
		effectiveTuReserved = BA_SNAPSHOT;
	}
	// likewise, if we don't have a snap shot available, try aimed.
	if (bu->getActionTUs(effectiveTuReserved, slowestWeapon) == 0 && effectiveTuReserved == BA_SNAPSHOT)
	{
		effectiveTuReserved = BA_AIMEDSHOT;
	}
	const int tuKneel = (_save->getKneelReserved() && !bu->isKneeled()  && bu->getType() == "SOLDIER") ? 4 : 0;
	// no aimed shot available? revert to none.
	if (bu->getActionTUs(effectiveTuReserved, slowestWeapon) == 0 && effectiveTuReserved == BA_AIMEDSHOT)
	{
		if (tuKneel > 0)
		{
			effectiveTuReserved = BA_NONE;
		}
		else
		{
			return true;
		}
	}

	if ((effectiveTuReserved != BA_NONE || _save->getKneelReserved()) &&
		tu + tuKneel + bu->getActionTUs(effectiveTuReserved, slowestWeapon) > bu->getTimeUnits() &&
		(tuKneel + bu->getActionTUs(effectiveTuReserved, slowestWeapon) <= bu->getTimeUnits() || justChecking))
	{
		if (!justChecking)
		{
			if (tuKneel)
			{
				switch (effectiveTuReserved)
				{
				case BA_NONE: _parentState->warning("STR_TIME_UNITS_RESERVED_FOR_KNEELING"); break;
				default: _parentState->warning("STR_TIME_UNITS_RESERVED_FOR_KNEELING_AND_FIRING");
				}
			}
			else
			{
				switch (_save->getTUReserved())
				{
				case BA_SNAPSHOT: _parentState->warning("STR_TIME_UNITS_RESERVED_FOR_SNAP_SHOT"); break;
				case BA_AUTOSHOT: _parentState->warning("STR_TIME_UNITS_RESERVED_FOR_AUTO_SHOT"); break;
				case BA_AIMEDSHOT: _parentState->warning("STR_TIME_UNITS_RESERVED_FOR_AIMED_SHOT"); break;
				default: ;
				}
			}
		}
		return false;
	}

	return true;
}



/**
 * Picks the first soldier that is panicking.
 * @return True when all panicking is over.
 */
bool BattlescapeGame::handlePanickingPlayer()
{
	for (std::vector<BattleUnit*>::iterator j = _save->getUnits()->begin(); j != _save->getUnits()->end(); ++j)
	{
		if ((*j)->getFaction() == FACTION_PLAYER && (*j)->getOriginalFaction() == FACTION_PLAYER && handlePanickingUnit(*j))
			return false;
	}
	return true;
}

namespace
{

bool isLiveRosterSoldierIdentityForPsychObservation(const BattleUnit *unit, const SavedGame *campaign)
{
	if (unit == 0 || campaign == 0)
	{
		return false;
	}
	Soldier *soldier = unit->getGeoscapeSoldier();
	if (soldier == 0 || soldier->getId() != unit->getId())
	{
		return false;
	}
	if (unit->getId() <= 0 || unit->getId() >= BattleUnit::MAX_SOLDIER_ID)
	{
		return false;
	}
	return campaign->isLiveRosterSoldier(unit->getId());
}

const char *psychPanicObserveEventType(const BattleUnit *unit, UnitStatus status, const SavedGame *campaign)
{
	const bool roster = isLiveRosterSoldierIdentityForPsychObservation(unit, campaign);
	if (status == STATUS_PANICKING)
	{
		return roster ? "soldier_panicked_observed" : "alien_panicked_observed";
	}
	return roster ? "soldier_berserk_observed" : "alien_berserk_observed";
}

} // namespace

/**
 * Common function for handling panicking units.
 * @return False when unit not in panicking mode.
 */
bool BattlescapeGame::handlePanickingUnit(BattleUnit *unit)
{
	UnitStatus status = unit->getStatus();
	if (status != STATUS_PANICKING && status != STATUS_BERSERK) return false;
	_save->setSelectedUnit(unit);
	_parentState->getMap()->setCursorType(CT_NONE);

	// show a little infobox with the name of the unit and "... is panicking"
	Game *game = _parentState->getGame();
	// Matches OpenXcom infobox gate. While panic states are on the stack, think() does not re-enter here;
	// perception dedupe also keys panic/berserk by subject+turn (one engine episode per turn).
	const bool uiNotificationShown = unit->getVisible() || !Options::noAlienPanicMessages;
	if (uiNotificationShown)
	{
		getMap()->getCamera()->centerOnPosition(unit->getPosition());
		if (status == STATUS_PANICKING)
		{
			game->pushState(new InfoboxState(game->getLanguage()->getString("STR_HAS_PANICKED", unit->getGender()).arg(unit->getName(game->getLanguage()))));
		}
		else
		{
			game->pushState(new InfoboxState(game->getLanguage()->getString("STR_HAS_GONE_BERSERK", unit->getGender()).arg(unit->getName(game->getLanguage()))));
		}
		const SavedGame *campaign = _save->getGeoscapeSave();
		std::string rosterDisplayName;
		if (isLiveRosterSoldierIdentityForPsychObservation(unit, campaign))
		{
			rosterDisplayName = unit->getGeoscapeSoldier()->getName(true);
		}
		const int observationSerial = _save->getCompanionPerceptionLog().allocatePsychObservationSerial();
		_save->getCompanionPerceptionLog().tryRecordPsychologicalUiNotification(
			_save,
			unit,
			psychPanicObserveEventType(unit, status, campaign),
			observationSerial,
			uiNotificationShown,
			unit->getVisible(),
			rosterDisplayName);
	}


	int flee = RNG::generate(0,100);
	BattleAction ba;
	ba.actor = unit;
	if (status == STATUS_PANICKING && flee <= 50) // 1/2 chance to freeze and 1/2 chance try to flee, STATUS_BERSERK is handled in the panic state.
	{
		BattleItem *item = unit->getItem("STR_RIGHT_HAND");
		if (item)
		{
			dropItem(unit->getPosition(), item, false, true);
		}
		item = unit->getItem("STR_LEFT_HAND");
		if (item)
		{
			dropItem(unit->getPosition(), item, false, true);
		}
		unit->setCache(0);
		// let's try a few times to get a tile to run to.
		for (int i= 0; i < 20; i++)
		{
			ba.target = Position(unit->getPosition().x + RNG::generate(-5,5), unit->getPosition().y + RNG::generate(-5,5), unit->getPosition().z);

			if (i >= 10 && ba.target.z > 0) // if we've had more than our fair share of failures, try going down.
			{
				ba.target.z--;
				if (i >= 15 && ba.target.z > 0) // still failing? try further down.
				{
					ba.target.z--;
				}
			}
			if (_save->getTile(ba.target)) // sanity check the tile.
			{
				_save->getPathfinding()->calculate(ba.actor, ba.target);
				if (_save->getPathfinding()->getStartDirection() != -1) // sanity check the path.
				{
					statePushBack(new UnitWalkBState(this, ba));
					break;
				}
			}
		}
	}
	// Time units can only be reset after everything else occurs
	statePushBack(new UnitPanicBState(this, ba.actor));
	unit->moraleChange(+15);

	return true;
}

/**
  * Cancels the current action the user had selected (firing, throwing,..)
  * @param bForce Force the action to be cancelled.
  * @return Whether an action was cancelled or not.
  */
bool BattlescapeGame::cancelCurrentAction(bool bForce)
{
	// Unit tests and companion-only harnesses construct BattlescapeGame without BattlescapeState.
	if (_parentState == 0)
	{
		_currentAction = BattleAction();
		_currentAction.actor = 0;
		_currentAction.targeting = false;
		_currentAction.type = BA_NONE;
		return false;
	}

	bool bPreviewed = Options::battleNewPreviewPath != PATH_NONE;

	if (_save->getPathfinding()->removePreview() && bPreviewed) return true;

	if (_states.empty() || bForce)
	{
		if (_currentAction.targeting)
		{
			if (_currentAction.type == BA_LAUNCH && !_currentAction.waypoints.empty())
			{
				_currentAction.waypoints.pop_back();
				if (!getMap()->getWaypoints()->empty())
				{
					getMap()->getWaypoints()->pop_back();
				}
				if (_currentAction.waypoints.empty())
				{
					_parentState->showLaunchButton(false);
				}
				return true;
			}
			else
			{
				if (Options::battleConfirmFireMode && !_currentAction.waypoints.empty())
				{
					_currentAction.waypoints.pop_back();
					getMap()->getWaypoints()->pop_back();
					return true;
				}
				_currentAction.targeting = false;
				_currentAction.type = BA_NONE;
				setupCursor();
				_parentState->getGame()->getCursor()->setVisible(true);
				return true;
			}
		}
	}
	else if (!_states.empty() && _states.front() != 0)
	{
		_states.front()->cancel();
		return true;
	}

	return false;
}

/**
  * Cancels all selected user actions.
  */
void BattlescapeGame::cancelAllActions()
{
	_save->getPathfinding()->removePreview();

	_currentAction.waypoints.clear();
	getMap()->getWaypoints()->clear();
	_parentState->showLaunchButton(false);

	_currentAction.targeting = false;
	_currentAction.type = BA_NONE;
	setupCursor();
	_parentState->getGame()->getCursor()->setVisible(true);
}

/**
 * Gets a pointer to access action members directly.
 * @return Pointer to action.
 */
BattleAction *BattlescapeGame::getCurrentAction()
{
	return &_currentAction;
}

bool BattlescapeGame::getRearmostQueuedBattleAction(BattleAction *out) const
{
	if (out == 0 || _states.empty())
	{
		return false;
	}
	*out = _states.back()->getAction();
	return true;
}

/**
 * Determines whether an action is currently going on?
 * @return true or false.
 */
bool BattlescapeGame::isBusy() const
{
	return !_states.empty();
}

/**
 * Activates primary action (left click).
 * @param pos Position on the map.
 */
void BattlescapeGame::primaryAction(Position pos)
{
	bool bPreviewed = Options::battleNewPreviewPath != PATH_NONE;

	getMap()->resetObstacles();

	if (_currentAction.targeting && _save->getSelectedUnit())
	{
		if (_currentAction.type == BA_LAUNCH)
		{
			int maxWaypoints = _currentAction.weapon->getRules()->getWaypoints();
			if (maxWaypoints == 0)
			{
				maxWaypoints = _currentAction.weapon->getAmmoItem()->getRules()->getWaypoints();
			}
			if ((int)_currentAction.waypoints.size() < maxWaypoints || maxWaypoints == -1)
			{
				_parentState->showLaunchButton(true);
				_currentAction.waypoints.push_back(pos);
				getMap()->getWaypoints()->push_back(pos);
			}
		}
		else if (_currentAction.type == BA_USE && _currentAction.weapon->getRules()->getBattleType() == BT_MINDPROBE)
		{
			if (_save->selectUnit(pos) && _save->selectUnit(pos)->getFaction() != _save->getSelectedUnit()->getFaction() && _save->selectUnit(pos)->getVisible())
			{
				if (warnIfHumanPlayerOrderDenied(_currentAction.actor))
				{
					return;
				}
				if (!_currentAction.weapon->getRules()->isLOSRequired() ||
					std::find(_currentAction.actor->getVisibleUnits()->begin(), _currentAction.actor->getVisibleUnits()->end(), _save->selectUnit(pos)) != _currentAction.actor->getVisibleUnits()->end())
				{
					if (_currentAction.actor->spendTimeUnits(_currentAction.TU))
					{
						_parentState->getGame()->getMod()->getSoundByDepth(_save->getDepth(), _currentAction.weapon->getRules()->getHitSound())->play(-1, getMap()->getSoundAngle(pos));
						_parentState->getGame()->pushState (new UnitInfoState(_save->selectUnit(pos), _parentState, false, true));
						cancelCurrentAction();
					}
					else
					{
						_parentState->warning("STR_NOT_ENOUGH_TIME_UNITS");
					}
				}
				else
				{
					_parentState->warning("STR_NO_LINE_OF_FIRE");
				}
			}
		}
		else if (_currentAction.type == BA_PANIC || _currentAction.type == BA_MINDCONTROL)
		{
			if (_save->selectUnit(pos) && _save->selectUnit(pos)->getFaction() != _save->getSelectedUnit()->getFaction() && _save->selectUnit(pos)->getVisible())
			{
				BattleUnit *actor = _currentAction.actor != 0 ? _currentAction.actor : _save->getSelectedUnit();
				if (warnIfHumanPlayerOrderDenied(actor))
				{
					return;
				}
				_currentAction.TU = _currentAction.actor->getActionTUs(_currentAction.type, _currentAction.weapon);
				_currentAction.target = pos;
				if (!_currentAction.weapon->getRules()->isLOSRequired() ||
					std::find(_currentAction.actor->getVisibleUnits()->begin(), _currentAction.actor->getVisibleUnits()->end(), _save->selectUnit(pos)) != _currentAction.actor->getVisibleUnits()->end())
				{
					// get the sound/animation started
					getMap()->setCursorType(CT_NONE);
					_parentState->getGame()->getCursor()->setVisible(false);
					_currentAction.cameraPosition = getMap()->getCamera()->getMapOffset();
					statePushBack(new PsiAttackBState(this, _currentAction));
				}
				else
				{
					_parentState->warning("STR_NO_LINE_OF_FIRE");
				}
			}
		}
		else if (Options::battleConfirmFireMode && (_currentAction.waypoints.empty() || pos != _currentAction.waypoints.front()))
		{
			_currentAction.waypoints.clear();
			_currentAction.waypoints.push_back(pos);
			getMap()->getWaypoints()->clear();
			getMap()->getWaypoints()->push_back(pos);
		}
		else
		{
			BattleUnit *actor = _currentAction.actor != 0 ? _currentAction.actor : _save->getSelectedUnit();
			if (warnIfHumanPlayerOrderDenied(actor))
			{
				return;
			}
			_currentAction.target = pos;
			getMap()->setCursorType(CT_NONE);

			if (Options::battleConfirmFireMode)
			{
				_currentAction.waypoints.clear();
				getMap()->getWaypoints()->clear();
			}

			_parentState->getGame()->getCursor()->setVisible(false);
			_currentAction.cameraPosition = getMap()->getCamera()->getMapOffset();
			_currentAction.playerOrdered = true;
			_states.push_back(new ProjectileFlyBState(this, _currentAction));
			statePushFront(new UnitTurnBState(this, _currentAction)); // first of all turn towards the target
		}
	}
	else
	{
		_currentAction.actor = _save->getSelectedUnit();
		BattleUnit *unit = _save->selectUnit(pos);
		if (unit && unit != _save->getSelectedUnit() && (unit->getVisible() || _debugPlay))
		{
		//  -= select unit =-
			if (unit->getFaction() == _save->getSide())
			{
				_save->setSelectedUnit(unit);
				_parentState->updateSoldierInfo();
				cancelCurrentAction();
				setupCursor();
				_currentAction.actor = unit;
			}
		}
		else if (playableUnitSelected())
		{
			BattleUnit *selected = _save->getSelectedUnit();
			if (warnIfHumanPlayerOrderDenied(selected))
			{
				return;
			}
			bool modifierPressed = (SDL_GetModState() & KMOD_CTRL) != 0;
			if (bPreviewed &&
				(_currentAction.target != pos || (_save->getPathfinding()->isModifierUsed() != modifierPressed)))
			{
				_save->getPathfinding()->removePreview();
			}
			_currentAction.target = pos;
			_save->getPathfinding()->calculate(_currentAction.actor, _currentAction.target);
			_currentAction.run = false;
			_currentAction.strafe = Options::strafe && modifierPressed && _save->getSelectedUnit()->getArmor()->getSize() == 1;
			if (_currentAction.strafe && _save->getPathfinding()->getPath().size() > 1)
			{
				_currentAction.run = true;
				_currentAction.strafe = false;
			}
			if (bPreviewed && !_save->getPathfinding()->previewPath() && _save->getPathfinding()->getStartDirection() != -1)
			{
				_save->getPathfinding()->removePreview();
				bPreviewed = false;
			}

			if (!bPreviewed && _save->getPathfinding()->getStartDirection() != -1)
			{
				//  -= start walking =-
				getMap()->setCursorType(CT_NONE);
				_parentState->getGame()->getCursor()->setVisible(false);
				statePushBack(new UnitWalkBState(this, _currentAction));
			}
		}
	}
}

/**
 * Activates secondary action (right click).
 * @param pos Position on the map.
 */
void BattlescapeGame::secondaryAction(Position pos)
{
	BattleUnit *unit = _save->getSelectedUnit();
	if (warnIfHumanPlayerOrderDenied(unit))
	{
		return;
	}
	//  -= turn to or open door =-
	_currentAction.target = pos;
	_currentAction.actor = _save->getSelectedUnit();
	_currentAction.strafe = Options::strafe && (SDL_GetModState() & KMOD_CTRL) != 0 && _save->getSelectedUnit()->getTurretType() > -1;
	statePushBack(new UnitTurnBState(this, _currentAction));
}

/**
 * Handler for the blaster launcher button.
 */
void BattlescapeGame::launchAction()
{
	if (warnIfHumanPlayerOrderDenied(_currentAction.actor != 0 ? _currentAction.actor : _save->getSelectedUnit()))
	{
		return;
	}
	_parentState->showLaunchButton(false);
	getMap()->getWaypoints()->clear();
	_currentAction.target = _currentAction.waypoints.front();
	getMap()->setCursorType(CT_NONE);
	_parentState->getGame()->getCursor()->setVisible(false);
	_currentAction.cameraPosition = getMap()->getCamera()->getMapOffset();
	_currentAction.playerOrdered = true;
	_states.push_back(new ProjectileFlyBState(this, _currentAction));
	statePushFront(new UnitTurnBState(this, _currentAction)); // first of all turn towards the target
}

/**
 * Handler for the psi button.
 */
void BattlescapeGame::psiButtonAction()
{
	if (!_currentAction.waypoints.empty()) // in case waypoints were set with a blaster launcher, avoid accidental misclick
		return;
	BattleUnit *unit = _save->getSelectedUnit();
	if (warnIfHumanPlayerOrderDenied(unit))
	{
		return;
	}
	_currentAction.weapon = _save->getSelectedUnit()->getSpecialWeapon(BT_PSIAMP);
	_currentAction.targeting = true;
	_currentAction.type = BA_PANIC;
	_currentAction.TU = _currentAction.weapon->getRules()->getTUUse();
	if (!_currentAction.weapon->getRules()->getFlatRate())
	{
		_currentAction.TU = (int)floor(_save->getSelectedUnit()->getBaseStats()->tu * _currentAction.TU / 100.0f);
	}
	setupCursor();
}


/**
 * Moves a unit up or down.
 * @param unit The unit.
 * @param dir Direction DIR_UP or DIR_DOWN.
 */
void BattlescapeGame::moveUpDown(BattleUnit *unit, int dir)
{
	if (warnIfHumanPlayerOrderDenied(unit))
	{
		return;
	}
	_currentAction.target = unit->getPosition();
	if (dir == Pathfinding::DIR_UP)
	{
		_currentAction.target.z++;
	}
	else
	{
		_currentAction.target.z--;
	}
	getMap()->setCursorType(CT_NONE);
	_parentState->getGame()->getCursor()->setVisible(false);
	if (_save->getSelectedUnit()->isKneeled())
	{
		kneel(_save->getSelectedUnit());
	}
	_save->getPathfinding()->calculate(_currentAction.actor, _currentAction.target);
	statePushBack(new UnitWalkBState(this, _currentAction));
}

/**
 * Requests the end of the turn (waits for explosions etc to really end the turn).
 */
void BattlescapeGame::requestEndTurn()
{
	cancelCurrentAction();
	if (!_endTurnRequested)
	{
		_endTurnRequested = true;
		statePushBack(0);
	}
}

/**
 * Sets the TU reserved type.
 * @param tur A battleactiontype.
 * @param player is this requested by the player?
 */
void BattlescapeGame::setTUReserved(BattleActionType tur)
{
	_save->setTUReserved(tur);
}

/**
 * Drops an item to the floor and affects it with gravity.
 * @param position Position to spawn the item.
 * @param item Pointer to the item.
 * @param newItem Bool whether this is a new item.
 * @param removeItem Bool whether to remove the item from the owner.
 */
void BattlescapeGame::dropItem(Position position, BattleItem *item, bool newItem, bool removeItem)
{
	getTileEngine()->itemDrop(_save->getTile(position), item, getMod(), newItem, removeItem);
}

/**
 * Converts a unit into a unit of another type.
 * @param unit The unit to convert.
 * @return Pointer to the new unit.
 */
BattleUnit *BattlescapeGame::convertUnit(BattleUnit *unit)
{
	getSave()->getBattleState()->showPsiButton(false);
	BattleUnit* newUnit = getSave()->convertUnit(unit, _parentState->getGame()->getSavedGame(), getMod());
	getMap()->cacheUnit(newUnit);
	return newUnit;

}

/**
 * Gets the map.
 * @return map.
 */
Map *BattlescapeGame::getMap()
{
	return _parentState->getMap();
}

/**
 * Gets the save.
 * @return save.
 */
SavedBattleGame *BattlescapeGame::getSave()
{
	return _save;
}

/**
 * Gets the tilengine.
 * @return tilengine.
 */
TileEngine *BattlescapeGame::getTileEngine()
{
	return _save->getTileEngine();
}

/**
 * Gets the pathfinding.
 * @return pathfinding.
 */
Pathfinding *BattlescapeGame::getPathfinding()
{
	return _save->getPathfinding();
}

/**
 * Gets the mod.
 * @return mod.
 */
Mod *BattlescapeGame::getMod()
{
	if (_parentState != 0 && _parentState->getGame() != 0)
	{
		return _parentState->getGame()->getMod();
	}
	if (_save != 0)
	{
		BattlescapeState *bs = _save->getBattleState();
		if (bs != 0 && bs->getGame() != 0)
		{
			return bs->getGame()->getMod();
		}
		if (_save->getTestMod() != 0)
		{
			return _save->getTestMod();
		}
	}
	return 0;
}

SavedGame *BattlescapeGame::companionCampaignSaveForValidation() const
{
	if (_parentState != 0 && _parentState->getGame() != 0)
	{
		return _parentState->getGame()->getSavedGame();
	}
	if (_save != 0)
	{
		return _save->getGeoscapeSave();
	}
	return 0;
}

void BattlescapeGame::companionUpdateSoldierInfoIfUiPresent(bool checkFOV)
{
	if (_parentState != 0)
	{
		_parentState->updateSoldierInfo(checkFOV);
	}
}


/**
 * Tries to find an item and pick it up if possible.
 */
void BattlescapeGame::findItem(BattleAction *action)
{
	// terrorists don't have hands.
	if (action->actor->getRankString() != "STR_LIVE_TERRORIST")
	{
		// pick the best available item
		BattleItem *targetItem = surveyItems(action);
		// make sure it's worth taking
		if (targetItem && worthTaking(targetItem, action))
		{
			// if we're already standing on it...
			if (targetItem->getTile()->getPosition() == action->actor->getPosition())
			{
				// try to pick it up
				if (takeItemFromGround(targetItem, action) == 0)
				{
					// if it isn't loaded or it is ammo
					if (!targetItem->getAmmoItem())
					{
						// try to load our weapon
						action->actor->checkAmmo();
					}
				}
			}
			else if (!targetItem->getTile()->getUnit() || targetItem->getTile()->getUnit()->isOut())
			{
				// if we're not standing on it, we should try to get to it.
				action->target = targetItem->getTile()->getPosition();
				action->type = BA_WALK;
			}
		}
	}
}


/**
 * Searches through items on the map that were dropped on an alien turn, then picks the most "attractive" one.
 * @param action A pointer to the action being performed.
 * @return The item to attempt to take.
 */
BattleItem *BattlescapeGame::surveyItems(BattleAction *action)
{
	std::vector<BattleItem*> droppedItems;

	// first fill a vector with items on the ground that were dropped on the alien turn, and have an attraction value.
	for (std::vector<BattleItem*>::iterator i = _save->getItems()->begin(); i != _save->getItems()->end(); ++i)
	{
		if ((*i)->getSlot() && (*i)->getSlot()->getId() == "STR_GROUND" && (*i)->getTile() && (*i)->getTurnFlag() && (*i)->getRules()->getAttraction())
		{
			droppedItems.push_back(*i);
		}
	}

	BattleItem *targetItem = 0;
	int maxWorth = 0;

	// now select the most suitable candidate depending on attractiveness and distance
	// (are we still talking about items?)
	for (std::vector<BattleItem*>::iterator i = droppedItems.begin(); i != droppedItems.end(); ++i)
	{
		int currentWorth = (*i)->getRules()->getAttraction() / ((_save->getTileEngine()->distance(action->actor->getPosition(), (*i)->getTile()->getPosition()) * 2)+1);
		if (currentWorth > maxWorth)
		{
			maxWorth = currentWorth;
			targetItem = *i;
		}
	}

	return targetItem;
}


/**
 * Assesses whether this item is worth trying to pick up, taking into account how many units we see,
 * whether or not the Weapon has ammo, and if we have ammo FOR it,
 * or, if it's ammo, checks if we have the weapon to go with it,
 * assesses the attraction value of the item and compares it with the distance to the object,
 * then returns false anyway.
 * @param item The item to attempt to take.
 * @param action A pointer to the action being performed.
 * @return false.
 */
bool BattlescapeGame::worthTaking(BattleItem* item, BattleAction *action)
{
	int worthToTake = 0;

	// don't even think about making a move for that gun if you can see a target, for some reason
	// (maybe this should check for enemies spotting the tile the item is on?)
	if (action->actor->getVisibleUnits()->empty())
	{
		// retrieve an insignificantly low value from the ruleset.
		worthToTake = item->getRules()->getAttraction();

		// it's always going to be worth while to try and take a blaster launcher, apparently
		if (item->getRules()->getWaypoints() == 0 && item->getRules()->getBattleType() != BT_AMMO)
		{
			// we only want weapons that HAVE ammo, or weapons that we have ammo FOR
			bool ammoFound = true;
			if (!item->getAmmoItem())
			{
				ammoFound = false;
				for (std::vector<BattleItem*>::iterator i = action->actor->getInventory()->begin(); i != action->actor->getInventory()->end() && !ammoFound; ++i)
				{
					if ((*i)->getRules()->getBattleType() == BT_AMMO)
					{
						for (std::vector<std::string>::iterator j = item->getRules()->getCompatibleAmmo()->begin(); j != item->getRules()->getCompatibleAmmo()->end() && !ammoFound; ++j)
						{
							if ((*i)->getRules()->getName() == *j)
							{
								ammoFound = true;
								break;
							}
						}
					}
				}
			}
			if (!ammoFound)
			{
				return false;
			}
		}

		if ( item->getRules()->getBattleType() == BT_AMMO)
		{
			// similar to the above, but this time we're checking if the ammo is suitable for a weapon we have.
			bool weaponFound = false;
			for (std::vector<BattleItem*>::iterator i = action->actor->getInventory()->begin(); i != action->actor->getInventory()->end() && !weaponFound; ++i)
			{
				if ((*i)->getRules()->getBattleType() == BT_FIREARM)
				{
					for (std::vector<std::string>::iterator j = (*i)->getRules()->getCompatibleAmmo()->begin(); j != (*i)->getRules()->getCompatibleAmmo()->end() && !weaponFound; ++j)
					{
						if ((*i)->getRules()->getName() == *j)
						{
							weaponFound = true;
							break;
						}
					}
				}
			}
			if (!weaponFound)
			{
				return false;
			}
		}
	}

	if (worthToTake)
	{
		// use bad logic to determine if we'll have room for the item
		int freeSlots = 25;
		for (std::vector<BattleItem*>::iterator i = action->actor->getInventory()->begin(); i != action->actor->getInventory()->end(); ++i)
		{
			freeSlots -= (*i)->getRules()->getInventoryHeight() * (*i)->getRules()->getInventoryWidth();
		}
		int size = item->getRules()->getInventoryHeight() * item->getRules()->getInventoryWidth();
		if (freeSlots < size)
		{
			return false;
		}
	}

	// return false for any item that we aren't standing directly on top of with an attraction value less than 6 (aka always)
	return (worthToTake - (_save->getTileEngine()->distance(action->actor->getPosition(), item->getTile()->getPosition())*2)) > 5;
}


/**
 * Picks the item up from the ground.
 *
 * At this point we've decided it's worth our while to grab this item, so we try to do just that.
 * First we check to make sure we have time units, then that we have space (using horrifying logic)
 * then we attempt to actually recover the item.
 * @param item The item to attempt to take.
 * @param action A pointer to the action being performed.
 * @return 0 if successful, 1 for no TUs, 2 for not enough room, 3 for "won't fit" and -1 for "something went horribly wrong".
 */
int BattlescapeGame::takeItemFromGround(BattleItem* item, BattleAction *action)
{
	const int success = 0;
	const int notEnoughTimeUnits = 1;
	const int notEnoughSpace = 2;
	const int couldNotFit = 3;
	int freeSlots = 25;

	// make sure we have time units
	if (action->actor->getTimeUnits() < 6)
	{
		return notEnoughTimeUnits;
	}
	else
	{
		// check to make sure we have enough space by checking all the sizes of items in our inventory
		for (std::vector<BattleItem*>::iterator i = action->actor->getInventory()->begin(); i != action->actor->getInventory()->end(); ++i)
		{
			freeSlots -= (*i)->getRules()->getInventoryHeight() * (*i)->getRules()->getInventoryWidth();
		}
		if (freeSlots < item->getRules()->getInventoryHeight() * item->getRules()->getInventoryWidth())
		{
			return notEnoughSpace;
		}
		else
		{
			// check that the item will fit in our inventory, and if so, take it
			if (takeItem(item, action))
			{
				action->actor->spendTimeUnits(6);
				item->getTile()->removeItem(item);
				return success;
			}
			else
			{
				return couldNotFit;
			}
		}
	}
}


/**
 * Tries to fit an item into the unit's inventory, return false if you can't.
 * @param item The item to attempt to take.
 * @param action A pointer to the action being performed.
 * @return Whether or not the item was successfully retrieved.
 */
bool BattlescapeGame::takeItem(BattleItem* item, BattleAction *action)
{
	bool placed = false;
	Mod *mod = _parentState->getGame()->getMod();
	switch (item->getRules()->getBattleType())
	{
	case BT_AMMO:
		// find equipped weapons that can be loaded with this ammo
		if (action->actor->getItem("STR_RIGHT_HAND") && action->actor->getItem("STR_RIGHT_HAND")->getAmmoItem() == 0)
		{
			if (action->actor->getItem("STR_RIGHT_HAND")->setAmmoItem(item) == 0)
			{
				placed = true;
			}
		}
		else
		{
			for (int i = 0; i != 4; ++i)
			{
				if (!action->actor->getItem("STR_BELT", i))
				{
					item->moveToOwner(action->actor);
					item->setSlot(mod->getInventory("STR_BELT", true));
					item->setSlotX(i);
					placed = true;
					break;
				}
			}
		}
		break;
	case BT_GRENADE:
	case BT_PROXIMITYGRENADE:
		for (int i = 0; i != 4; ++i)
		{
			if (!action->actor->getItem("STR_BELT", i))
			{
				item->moveToOwner(action->actor);
				item->setSlot(mod->getInventory("STR_BELT", true));
				item->setSlotX(i);
				placed = true;
				break;
			}
		}
		break;
	case BT_FIREARM:
	case BT_MELEE:
		if (!action->actor->getItem("STR_RIGHT_HAND"))
		{
			item->moveToOwner(action->actor);
			item->setSlot(mod->getInventory("STR_RIGHT_HAND", true));
			placed = true;
		}
		break;
	case BT_MEDIKIT:
	case BT_SCANNER:
		if (!action->actor->getItem("STR_BACK_PACK"))
		{
			item->moveToOwner(action->actor);
			item->setSlot(mod->getInventory("STR_BACK_PACK", true));
			placed = true;
		}
		break;
	case BT_MINDPROBE:
		if (!action->actor->getItem("STR_LEFT_HAND"))
		{
			item->moveToOwner(action->actor);
			item->setSlot(mod->getInventory("STR_LEFT_HAND", true));
			placed = true;
		}
		break;
	default: break;
	}
	return placed;
}

/**
 * Returns the action type that is reserved.
 * @return The type of action that is reserved.
 */
BattleActionType BattlescapeGame::getReservedAction()
{
	return _save->getTUReserved();
}

/**
 * Tallies the living units in the game and, if required, converts units into their spawn unit.
 * @param &liveAliens The integer in which to store the live alien tally.
 * @param &liveSoldiers The integer in which to store the live XCom tally.
 * @param convert Should we convert infected units?
 */
void BattlescapeGame::tallyUnits(int &liveAliens, int &liveSoldiers)
{
	liveSoldiers = 0;
	liveAliens = 0;

	for (std::vector<BattleUnit*>::iterator j = _save->getUnits()->begin(); j != _save->getUnits()->end(); ++j)
	{
		if (!(*j)->isOut())
		{
			if ((*j)->getOriginalFaction() == FACTION_HOSTILE)
			{
				if (!Options::allowPsionicCapture || (*j)->getFaction() != FACTION_PLAYER || !(*j)->getCapturable())
				{
					liveAliens++;
				}
			}
			else if ((*j)->getOriginalFaction() == FACTION_PLAYER)
			{
				if ((*j)->getFaction() == FACTION_PLAYER)
				{
					liveSoldiers++;
				}
				else
				{
					liveAliens++;
				}
			}
		}
	}
}

bool BattlescapeGame::convertInfected()
{
	bool retVal = false;
	for (std::vector<BattleUnit*>::iterator i = _save->getUnits()->begin(); i != _save->getUnits()->end(); ++i)
	{
		if ((*i)->getHealth() > 0 && (*i)->getHealth() >= (*i)->getStunlevel() && (*i)->getRespawn())
		{
			retVal = true;
			(*i)->setRespawn(false);
			if (Options::battleNotifyDeath && (*i)->getFaction() == FACTION_PLAYER)
			{
				Game *game = _parentState->getGame();
				game->pushState(new InfoboxState(game->getLanguage()->getString("STR_HAS_BEEN_KILLED", (*i)->getGender()).arg((*i)->getName(game->getLanguage()))));
			}

			convertUnit((*i));
			i = _save->getUnits()->begin();
		}
	}
	return retVal;
}

/**
 * Sets the kneel reservation setting.
 * @param reserved Should we reserve an extra 4 TUs to kneel?
 */
void BattlescapeGame::setKneelReserved(bool reserved)
{
	_save->setKneelReserved(reserved);
}

/**
 * Gets the kneel reservation setting.
 * @return Kneel reservation setting.
 */
bool BattlescapeGame::getKneelReserved() const
{
	return _save->getKneelReserved();
}

/**
 * Checks if a unit has moved next to a proximity grenade.
 * Checks one tile around the unit in every direction.
 * For a large unit we check every tile it occupies.
 * @param unit Pointer to a unit.
 * @return True if a proximity grenade was triggered.
 */
bool BattlescapeGame::checkForProximityGrenades(BattleUnit *unit)
{
	int size = unit->getArmor()->getSize() - 1;
	for (int x = size; x >= 0; x--)
	{
		for (int y = size; y >= 0; y--)
		{
			for (int tx = -1; tx < 2; tx++)
			{
				for (int ty = -1; ty < 2; ty++)
				{
					Tile *t = _save->getTile(unit->getPosition() + Position(x,y,0) + Position(tx,ty,0));
					if (t)
					{
						for (std::vector<BattleItem*>::iterator i = t->getInventory()->begin(); i != t->getInventory()->end(); ++i)
						{
							if ((*i)->getRules()->getBattleType() == BT_PROXIMITYGRENADE && (*i)->getFuseTimer() == 0)
							{
								Position p;
								p.x = t->getPosition().x*16 + 8;
								p.y = t->getPosition().y*16 + 8;
								p.z = t->getPosition().z*24 + t->getTerrainLevel();
								statePushNext(new ExplosionBState(this, p, (*i), (*i)->getPreviousOwner()));
								getSave()->removeItem(*i);
								unit->setCache(0);
								getMap()->cacheUnit(unit);
								return true;
							}
						}
					}
				}
			}
		}
	}
	return false;
}

/**
 * Cleans up all the deleted states.
 */
void BattlescapeGame::cleanupDeleted()
{
	for (std::list<BattleState*>::iterator i = _deleted.begin(); i != _deleted.end(); ++i)
	{
		delete *i;
	}
	_deleted.clear();
}

/**
 * Gets the depth of the battlescape.
 * @return the depth of the battlescape.
 */
int BattlescapeGame::getDepth() const
{
	return _save->getDepth();
}

std::list<BattleState*> BattlescapeGame::getStates()
{
	return _states;
}

/**
 * Ends the turn if auto-end battle is enabled
 * and all mission objectives are completed.
 */
void BattlescapeGame::autoEndBattle()
{
	if (Options::battleAutoEnd)
	{
		bool end = false;
		if (_save->getObjectiveType() == MUST_DESTROY)
		{
			end = _save->allObjectivesDestroyed();
		}
		else
		{
			int liveAliens = 0;
			int liveSoldiers = 0;
			tallyUnits(liveAliens, liveSoldiers);
			end = (liveAliens == 0 || liveSoldiers == 0);
		}
		if (end)
		{
			_save->setSelectedUnit(0);
			cancelCurrentAction(true);
			requestEndTurn();
		}
	}
}

namespace
{

void enqueueCompanionOnDemandTacticalSend(const std::string &briefingRequestId)
{
	std::string relayRequestId;
	std::string error;
	if (!CompanionOnDemandRelayCore::tryEnqueueSend(
		"tactical", briefingRequestId.c_str(), &relayRequestId, &error))
	{
		Log(LOG_INFO) << "Companion on-demand send not queued: " << error;
	}
}

} // namespace

void BattlescapeGame::requestCompanionBriefing(bool battleInitialized)
{
	CompanionBriefingTestSupport::BriefingGateInput input = {};
	input.snapshotPathConfigured = CompanionBriefingRequest::snapshotPathConfigured();
	input.debugMode = _save->getDebugMode();
	input.playerTurn = _save->getSide() == FACTION_PLAYER;
	input.unitsFalling = _save->getUnitsFalling();
	input.battleBusy = isBusy();
	input.battleInitialized = battleInitialized;

	CompanionBriefingTestSupport::BriefingPressState state = { _companionBriefingPending };
	const CompanionBriefingTestSupport::BriefingPressOutcome outcome =
		CompanionBriefingTestSupport::evaluateBriefingKeyPress(input, &state);
	_companionBriefingPending = state.pending;

	switch (outcome)
	{
	case CompanionBriefingTestSupport::BRIEFING_PRESS_IGNORED:
		return;
	case CompanionBriefingTestSupport::BRIEFING_PRESS_REJECTED:
		Log(LOG_INFO) << "Companion briefing: request rejected";
		return;
	case CompanionBriefingTestSupport::BRIEFING_PRESS_DEFERRED:
		Log(LOG_INFO) << "Companion briefing: deferred until idle";
		return;
	case CompanionBriefingTestSupport::BRIEFING_PRESS_FULFILL_NOW:
		{
			std::string briefingRequestId;
			if (CompanionBriefingRequest::fulfill(_save, &briefingRequestId))
			{
				_companionBriefingPending = false;
				enqueueCompanionOnDemandTacticalSend(briefingRequestId);
			}
		}
		return;
	}
}

void BattlescapeGame::tryFulfillCompanionBriefing(bool battleInitialized)
{
	if (!_companionBriefingPending)
	{
		return;
	}

	if (_save->getDebugMode())
	{
		_companionBriefingPending = false;
		Log(LOG_INFO) << "Companion briefing: pending cleared (debug mode)";
		return;
	}

	if (!CompanionBriefingRequest::canFulfill(_save, isBusy(), battleInitialized))
	{
		return;
	}

	std::string briefingRequestId;
	if (CompanionBriefingRequest::fulfill(_save, &briefingRequestId))
	{
		_companionBriefingPending = false;
		enqueueCompanionOnDemandTacticalSend(briefingRequestId);
	}
}

bool BattlescapeGame::tryExecuteCompanionMove(BattleUnit *unit, Position dest, std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}
	cancelCurrentAction();
	_save->setSelectedUnit(unit);
	if (_parentState != 0)
	{
		_parentState->updateSoldierInfo();
	}
	_currentAction = BattleAction();
	_currentAction.actor = unit;
	_currentAction.target = dest;
	_currentAction.type = BA_WALK;
	_save->getPathfinding()->calculate(unit, dest);
	if (_save->getPathfinding()->getStartDirection() == -1)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "No valid path to destination.";
		}
		cancelCurrentAction();
		return false;
	}
	if (_parentState != 0)
	{
		getMap()->setCursorType(CT_NONE);
		if (_parentState->getGame() != 0)
		{
			_parentState->getGame()->getCursor()->setVisible(false);
		}
	}
	_currentAction.playerOrdered = true;
	statePushBack(new UnitWalkBState(this, _currentAction));
	return true;
}

bool BattlescapeGame::tryExecuteCompanionStandUp(BattleUnit *unit, std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}
	if (!unit->isKneeled())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit is not kneeling.";
		}
		return false;
	}
	if (unit->getType() != "SOLDIER" || unit->isFloating())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit cannot stand.";
		}
		return false;
	}
	cancelCurrentAction();
	_save->setSelectedUnit(unit);
	_parentState->updateSoldierInfo();
	if (!kneel(unit))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to stand.";
		}
		return false;
	}
	return true;
}

bool BattlescapeGame::tryExecuteCompanionKneelDown(BattleUnit *unit, std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}
	if (unit->isKneeled())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit is already kneeling.";
		}
		return false;
	}
	if (unit->getType() != "SOLDIER" || unit->isFloating())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit cannot kneel.";
		}
		return false;
	}
	cancelCurrentAction();
	_save->setSelectedUnit(unit);
	_parentState->updateSoldierInfo();
	if (!kneel(unit))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to kneel.";
		}
		return false;
	}
	return true;
}

static bool companionFirearmHasAmmo(BattleItem *weapon)
{
	if (weapon == 0 || weapon->getRules()->getBattleType() != BT_FIREARM)
	{
		return false;
	}
	if (!weapon->needsAmmo())
	{
		return true;
	}
	BattleItem *ammo = weapon->getAmmoItem();
	return ammo != 0 && ammo->getAmmoQuantity() > 0;
}

static bool companionFirearmSupportsDirectedFire(BattleItem *weapon, BattleActionType actionType)
{
	if (weapon == 0)
	{
		return false;
	}
	RuleItem *rules = weapon->getRules();
	if (rules->getBattleType() != BT_FIREARM)
	{
		return false;
	}
	if (rules->getWaypoints() != 0)
	{
		return false;
	}
	BattleItem *ammo = weapon->getAmmoItem();
	if (ammo != 0 && ammo->getRules()->getWaypoints() != 0)
	{
		return false;
	}
	switch (actionType)
	{
	case BA_SNAPSHOT:
		return rules->getAccuracySnap() != 0;
	case BA_AUTOSHOT:
		return rules->getAccuracyAuto() != 0;
	case BA_AIMEDSHOT:
		return rules->getAccuracyAimed() != 0;
	default:
		return false;
	}
}

static const char *companionDirectedFireModeLabel(BattleActionType actionType)
{
	switch (actionType)
	{
	case BA_SNAPSHOT: return "snap shot";
	case BA_AUTOSHOT: return "automatic fire";
	case BA_AIMEDSHOT: return "aimed shot";
	default: return "fire";
	}
}

bool BattlescapeGame::tryExecuteCompanionDirectedFire(
	BattleUnit *unit,
	Position target,
	BattleItem *weapon,
	BattleActionType actionType,
	std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (weapon == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "No weapon in hand.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}
	if (!companionFirearmSupportsDirectedFire(weapon, actionType))
	{
		if (rejectReason != 0)
		{
			*rejectReason = std::string("Weapon does not support ") + companionDirectedFireModeLabel(actionType) + ".";
		}
		return false;
	}
	if (!_save->getItemUsable(weapon).empty())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon is not usable.";
		}
		return false;
	}
	if (!companionFirearmHasAmmo(weapon))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon has no ammunition.";
		}
		return false;
	}
	const int fireTu = unit->getActionTUs(actionType, weapon);
	if (fireTu <= 0 || !checkReservedTU(unit, fireTu, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = std::string("Not enough time units for ") + companionDirectedFireModeLabel(actionType) + ".";
		}
		return false;
	}

	cancelCurrentAction();
	_save->setSelectedUnit(unit);
	_parentState->updateSoldierInfo();

	_currentAction = BattleAction();
	_currentAction.type = actionType;
	_currentAction.actor = unit;
	_currentAction.weapon = weapon;
	_currentAction.target = target;
	_currentAction.TU = fireTu;
	_currentAction.targeting = true;
	_currentAction.playerOrdered = true;
	_currentAction.autoShotCounter = 0;
	_currentAction.cameraPosition = getMap()->getCamera()->getMapOffset();

	getMap()->setCursorType(CT_NONE);
	_parentState->getGame()->getCursor()->setVisible(false);
	_states.push_back(new ProjectileFlyBState(this, _currentAction));
	statePushFront(new UnitTurnBState(this, _currentAction));
	return true;
}

bool BattlescapeGame::tryExecuteCompanionSnapShot(BattleUnit *unit, Position target, BattleItem *weapon, std::string *rejectReason)
{
	return tryExecuteCompanionDirectedFire(unit, target, weapon, BA_SNAPSHOT, rejectReason);
}

static bool companionItemIsThrowableGrenade(BattleItem *weapon)
{
	if (weapon == 0 || weapon->getRules()->isFixed())
	{
		return false;
	}
	const BattleType bt = weapon->getRules()->getBattleType();
	return bt == BT_GRENADE || bt == BT_PROXIMITYGRENADE;
}

bool BattlescapeGame::tryExecuteCompanionPrime(BattleUnit *unit, BattleItem *weapon, int fuseTimer, std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (weapon == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "No grenade in hand.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}
	const BattleType bt = weapon->getRules()->getBattleType();
	if (bt != BT_GRENADE && bt != BT_PROXIMITYGRENADE)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item in hand does not support priming.";
		}
		return false;
	}
	if (weapon->getFuseTimer() >= 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Grenade is already primed.";
		}
		return false;
	}
	if (!CompanionCommandCore::isValidPrimeFuseTimerForBattleType(fuseTimer, bt))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Fuse timer selection is invalid for this grenade type.";
		}
		return false;
	}
	if (!_save->getItemUsable(weapon).empty())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Grenade is not usable.";
		}
		return false;
	}
	const int primeTu = unit->getActionTUs(BA_PRIME, weapon);
	if (primeTu <= 0 || !checkReservedTU(unit, primeTu, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to prime grenade.";
		}
		return false;
	}

	cancelCurrentAction();
	_save->setSelectedUnit(unit);
	companionUpdateSoldierInfoIfUiPresent(true);
	if (!unit->spendTimeUnits(primeTu))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to prime grenade.";
		}
		return false;
	}
	weapon->setFuseTimer(fuseTimer);
	companionUpdateSoldierInfoIfUiPresent(false);
	return true;
}

static bool companionWeaponIsHeldInLeftOrRightHand(BattleItem *weapon)
{
	if (weapon == 0 || weapon->getSlot() == 0)
	{
		return false;
	}
	if (weapon->getSlot()->getType() != INV_HAND)
	{
		return false;
	}
	const std::string &id = weapon->getSlot()->getId();
	return id == "STR_LEFT_HAND" || id == "STR_RIGHT_HAND";
}

/**
 * Replicates Inventory::moveItem for a unit without the full Inventory UI (companion UNLOAD path).
 */
static void companionInventoryMoveItemForUnit(BattleUnit *unit, BattleItem *item, RuleInventory *slot, int x, int y)
{
	if (item == 0 || unit == 0)
	{
		return;
	}
	// Make items vanish (eg. ammo in weapons) — same logic as Inventory::moveItem.
	if (slot == 0)
	{
		if (item->getSlot()->getType() == INV_GROUND)
		{
			unit->getTile()->removeItem(item);
		}
		else
		{
			item->moveToOwner(0);
		}
	}
	else
	{
		// Handle dropping from/to ground.
		if (slot != item->getSlot())
		{
		if (slot->getType() == INV_GROUND)
		{
			item->moveToOwner(0);
			Tile *unitTile = unit->getTile();
			if (unitTile != 0)
			{
				unitTile->addItem(item, item->getSlot());
			}
			if (item->getUnit() && item->getUnit()->getStatus() == STATUS_UNCONSCIOUS)
			{
				item->getUnit()->setPosition(unit->getPosition());
			}
		}
		else if (item->getSlot() == 0 || item->getSlot()->getType() == INV_GROUND)
		{
			item->moveToOwner(unit);
			Tile *unitTile = unit->getTile();
			if (unitTile != 0)
			{
				unitTile->removeItem(item);
			}
			item->setTurnFlag(false);
				if (item->getUnit() && item->getUnit()->getStatus() == STATUS_UNCONSCIOUS)
				{
					item->getUnit()->setPosition(Position(-1, -1, -1));
				}
			}
		}
		item->setSlot(slot);
		item->setSlotX(x);
		item->setSlotY(y);
	}
}

static bool companionUnloadOtherHandIsClear(BattleUnit *unit, BattleItem *weapon)
{
	if (unit == 0 || weapon == 0 || weapon->getSlot() == 0)
	{
		return false;
	}
	const std::string &sid = weapon->getSlot()->getId();
	if (sid == "STR_RIGHT_HAND")
	{
		return unit->getItem("STR_LEFT_HAND") == 0;
	}
	if (sid == "STR_LEFT_HAND")
	{
		return unit->getItem("STR_RIGHT_HAND") == 0;
	}
	return false;
}

bool BattlescapeGame::tryExecuteCompanionReload(BattleUnit *unit, BattleItem *weapon, BattleItem *ammo, std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (weapon == 0 || ammo == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon or ammunition missing.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}

	RuleItem *weaponRules = weapon->getRules();
	RuleItem *ammoRules = ammo->getRules();
	if (weaponRules == 0 || ammoRules == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid item rules.";
		}
		return false;
	}

	if (weaponRules->getBattleType() != BT_FIREARM)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item is not a firearm.";
		}
		return false;
	}
	if (!weapon->needsAmmo())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon does not use insertable ammunition.";
		}
		return false;
	}
	// Vanilla magazine weapons list compatibleAmmo on the firearm; clip capacity is on the BT_AMMO rule
	// (weapon clipSize is often 0). Rely on needsAmmo + compatibleAmmo, not weapon clipSize.
	if (weaponRules->getCompatibleAmmo()->empty())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon does not use insertable ammunition.";
		}
		return false;
	}
	if (!companionWeaponIsHeldInLeftOrRightHand(weapon))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon must be in STR_LEFT_HAND or STR_RIGHT_HAND.";
		}
		return false;
	}
	if (weapon->getOwner() != unit || ammo->getOwner() != unit)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon and ammunition must belong to the actor.";
		}
		return false;
	}
	if (ammo->getSlot() == 0 || ammo->getSlot()->getType() == INV_GROUND)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Cannot reload from ground ammunition.";
		}
		return false;
	}
	if (ammoRules->getBattleType() != BT_AMMO)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Selected item is not ammunition.";
		}
		return false;
	}
	if (weapon->getAmmoItem() != 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon already contains a magazine.";
		}
		return false;
	}

	bool compatible = false;
	for (std::vector<std::string>::const_iterator c = weaponRules->getCompatibleAmmo()->begin();
	     c != weaponRules->getCompatibleAmmo()->end(); ++c)
	{
		if (*c == ammoRules->getType())
		{
			compatible = true;
			break;
		}
	}
	if (!compatible)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Ammunition type is not compatible with this weapon.";
		}
		return false;
	}

	SavedGame *campaign = companionCampaignSaveForValidation();
	if (campaign == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Campaign save unavailable for research validation.";
		}
		return false;
	}
	if (!_save->getItemUsable(weapon).empty() || !_save->getItemUsable(ammo).empty())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon or ammunition is not usable in current battlescape conditions.";
		}
		return false;
	}
	if (!campaign->isResearched(weaponRules->getRequirements()) ||
	    !campaign->isResearched(ammoRules->getRequirements()))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon or ammunition is not researched for use.";
		}
		return false;
	}

	const int reloadTu = 15;
	if (reloadTu <= 0 || !checkReservedTU(unit, reloadTu, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to reload (15 TU).";
		}
		return false;
	}

	cancelCurrentAction();
	_save->setSelectedUnit(unit);
	companionUpdateSoldierInfoIfUiPresent(true);

	const int tuBefore = unit->getTimeUnits();
	if (!unit->spendTimeUnits(reloadTu))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to reload (15 TU).";
		}
		return false;
	}

	const int setCode = weapon->setAmmoItem(ammo);
	if (setCode != 0)
	{
		unit->setTimeUnits(tuBefore);
		if (rejectReason != 0)
		{
			*rejectReason = setCode == -1
				? "Weapon already contains a magazine."
				: "Ammunition could not be attached to weapon.";
		}
		return false;
	}

	ammo->moveToOwner(0);
	if (_parentState != 0 && _parentState->getGame() != 0)
	{
		_parentState->getGame()->getMod()->getSoundByDepth(_save->getDepth(), Mod::ITEM_RELOAD)->play(
			-1, getMap()->getSoundAngle(unit->getPosition()));
	}
	companionUpdateSoldierInfoIfUiPresent(false);
	return true;
}

bool BattlescapeGame::tryExecuteCompanionUnload(BattleUnit *unit, BattleItem *weapon, int expectedLoadedAmmoBattleItemId, std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (weapon == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon missing.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}

	RuleItem *weaponRules = weapon->getRules();
	if (weaponRules == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid item rules.";
		}
		return false;
	}

	if (weaponRules->getBattleType() != BT_FIREARM)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item is not a firearm.";
		}
		return false;
	}
	if (!weapon->needsAmmo())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon does not use insertable ammunition.";
		}
		return false;
	}
	if (weaponRules->getCompatibleAmmo()->empty())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon does not use insertable ammunition.";
		}
		return false;
	}
	if (!companionWeaponIsHeldInLeftOrRightHand(weapon))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon must be in STR_LEFT_HAND or STR_RIGHT_HAND.";
		}
		return false;
	}
	if (weapon->getOwner() != unit)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon must belong to the actor.";
		}
		return false;
	}

	BattleItem *ammo = weapon->getAmmoItem();
	if (ammo == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon has no magazine inserted.";
		}
		return false;
	}
	if (expectedLoadedAmmoBattleItemId > 0 && ammo->getId() != expectedLoadedAmmoBattleItemId)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "loaded_ammo_battle_item_id does not match inserted magazine.";
		}
		return false;
	}
	RuleItem *ammoRules = ammo->getRules();
	if (ammoRules == 0 || ammoRules->getBattleType() != BT_AMMO)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Inserted item is not ammunition.";
		}
		return false;
	}

	if (!companionUnloadOtherHandIsClear(unit, weapon))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Other hand must be empty to unload (matches inventory unload rules).";
		}
		return false;
	}

	SavedGame *campaign = companionCampaignSaveForValidation();
	if (campaign == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Campaign save unavailable for research validation.";
		}
		return false;
	}
	if (!_save->getItemUsable(weapon).empty() || !_save->getItemUsable(ammo).empty())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon or ammunition is not usable in current battlescape conditions.";
		}
		return false;
	}
	if (!campaign->isResearched(weaponRules->getRequirements()) ||
	    !campaign->isResearched(ammoRules->getRequirements()))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Weapon or ammunition is not researched for use.";
		}
		return false;
	}

	Mod *mod = getMod();
	if (mod == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Mod unavailable.";
		}
		return false;
	}
	RuleInventory *leftHand = mod->getInventory("STR_LEFT_HAND", true);
	RuleInventory *rightHand = mod->getInventory("STR_RIGHT_HAND", true);
	if (leftHand == 0 || rightHand == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Hand inventory rules unavailable.";
		}
		return false;
	}

	const int unloadTu = 8;
	if (unloadTu <= 0 || !checkReservedTU(unit, unloadTu, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to unload (8 TU).";
		}
		return false;
	}

	cancelCurrentAction();
	_save->setSelectedUnit(unit);
	companionUpdateSoldierInfoIfUiPresent(true);

	const int tuBefore = unit->getTimeUnits();
	if (!unit->spendTimeUnits(unloadTu))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to unload (8 TU).";
		}
		return false;
	}

	companionInventoryMoveItemForUnit(unit, ammo, leftHand, 0, 0);
	ammo->moveToOwner(unit);
	companionInventoryMoveItemForUnit(unit, weapon, rightHand, 0, 0);
	weapon->moveToOwner(unit);
	const int clearCode = weapon->setAmmoItem(0);
	if (clearCode != 0)
	{
		unit->setTimeUnits(tuBefore);
		if (rejectReason != 0)
		{
			*rejectReason = "Could not detach magazine from weapon.";
		}
		return false;
	}

	if (_parentState != 0 && _parentState->getGame() != 0)
	{
		_parentState->getGame()->getMod()->getSoundByDepth(_save->getDepth(), Mod::ITEM_DROP)->play(
			-1, getMap()->getSoundAngle(unit->getPosition()));
	}
	companionUpdateSoldierInfoIfUiPresent(false);
	return true;
}

bool BattlescapeGame::tryExecuteCompanionInventoryMove(
	BattleUnit *unit,
	BattleItem *item,
	RuleInventory *destSlot,
	int destX,
	int destY,
	std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (item == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item missing.";
		}
		return false;
	}
	if (destSlot == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Destination section missing.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}

	RuleItem *rules = item->getRules();
	if (rules == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid item rules.";
		}
		return false;
	}
	if (rules->isFixed())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Fixed equipment cannot be moved.";
		}
		return false;
	}

	if (item->isAmmo() && item->getSlot() == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Nested ammunition cannot be moved (unload first).";
		}
		return false;
	}

	if (item->getOwner() != unit)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item does not belong to the actor.";
		}
		return false;
	}

	RuleInventory *srcSlot = item->getSlot();
	if (srcSlot == 0 || srcSlot->getType() == INV_GROUND)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item must be carried in inventory, not on ground.";
		}
		return false;
	}

	if (destSlot->getType() == INV_GROUND)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Ground moves are not supported.";
		}
		return false;
	}

	if (item->getFuseTimer() >= 0 &&
	    (rules->getBattleType() == BT_GRENADE || rules->getBattleType() == BT_PROXIMITYGRENADE))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Primed explosives cannot be moved via inventory.";
		}
		return false;
	}

	Mod *mod = 0;
	if (_parentState != 0 && _parentState->getGame() != 0)
	{
		mod = _parentState->getGame()->getMod();
	}
	else if (_save != 0)
	{
		BattlescapeState *bs = _save->getBattleState();
		if (bs != 0 && bs->getGame() != 0)
		{
			mod = bs->getGame()->getMod();
		}
		else if (_save->getTestMod() != 0)
		{
			mod = _save->getTestMod();
		}
	}
	if (mod == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Mod unavailable.";
		}
		return false;
	}

	const std::map<std::string, int> &srcCosts = srcSlot->getTransferCosts();
	std::map<std::string, int>::const_iterator costIt = srcCosts.find(destSlot->getId());
	if (costIt == srcCosts.end())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "No TU cost defined between these sections in the active ruleset.";
		}
		return false;
	}
	const int moveTu = costIt->second;
	if (moveTu < 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid transfer TU cost.";
		}
		return false;
	}

	if (!destSlot->fitItemInSlot(rules, destX, destY))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item does not fit at destination coordinates.";
		}
		return false;
	}
	if (companionInventoryOverlapItems(unit, item, destSlot, destX, destY))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Destination overlaps another item.";
		}
		return false;
	}

	if (moveTu > 0 && !checkReservedTU(unit, moveTu, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units for inventory move.";
		}
		return false;
	}

	if (_parentState != 0)
	{
		cancelCurrentAction();
	}
	_save->setSelectedUnit(unit);
	companionUpdateSoldierInfoIfUiPresent(true);

	const int tuBefore = unit->getTimeUnits();
	if (moveTu > 0 && !unit->spendTimeUnits(moveTu))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units for inventory move.";
		}
		return false;
	}

	companionInventoryMoveItemForUnit(unit, item, destSlot, destX, destY);
	item->moveToOwner(unit);

	if (_parentState != 0 && _parentState->getGame() != 0)
	{
		_parentState->getGame()->getMod()->getSoundByDepth(_save->getDepth(), Mod::ITEM_DROP)->play(
			-1, getMap()->getSoundAngle(unit->getPosition()));
	}
	companionUpdateSoldierInfoIfUiPresent(false);
	return true;
}

bool BattlescapeGame::tryExecuteCompanionPickup(
	BattleUnit *unit,
	BattleItem *item,
	Position groundPosition,
	RuleInventory *destSlot,
	int destX,
	int destY,
	std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (item == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item missing.";
		}
		return false;
	}
	if (destSlot == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Destination section missing.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}

	Tile *actorTile = unit->getTile();
	if (actorTile == 0 || actorTile->isVoid())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Actor has no ground tile.";
		}
		return false;
	}
	if (groundPosition != unit->getPosition())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Ground position does not match actor tile.";
		}
		return false;
	}

	RuleItem *rules = item->getRules();
	if (rules == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid item rules.";
		}
		return false;
	}
	if (rules->isFixed())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Fixed equipment cannot be picked up.";
		}
		return false;
	}

	if (item->isAmmo() && item->getSlot() == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Nested ammunition cannot be picked up directly.";
		}
		return false;
	}

	RuleInventory *srcSlot = item->getSlot();
	if (srcSlot == 0 || srcSlot->getType() != INV_GROUND)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item is not on the ground.";
		}
		return false;
	}
	if (item->getOwner() != 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Ground item has unexpected owner.";
		}
		return false;
	}
	Tile *itemTile = item->getTile();
	if (itemTile == 0 || itemTile != actorTile)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Ground item is not on the actor tile.";
		}
		return false;
	}
	if (destSlot->getType() == INV_GROUND)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Destination must be a carried inventory section.";
		}
		return false;
	}

	Mod *mod = 0;
	if (_parentState != 0 && _parentState->getGame() != 0)
	{
		mod = _parentState->getGame()->getMod();
	}
	else if (_save != 0)
	{
		BattlescapeState *bs = _save->getBattleState();
		if (bs != 0 && bs->getGame() != 0)
		{
			mod = bs->getGame()->getMod();
		}
		else if (_save->getTestMod() != 0)
		{
			mod = _save->getTestMod();
		}
	}
	if (mod == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Mod unavailable.";
		}
		return false;
	}

	RuleInventory *groundRule = mod->getInventory("STR_GROUND", true);
	if (groundRule == 0 || srcSlot != groundRule)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Ground slot mismatch.";
		}
		return false;
	}

	const std::map<std::string, int> &groundCosts = groundRule->getTransferCosts();
	std::map<std::string, int>::const_iterator costIt = groundCosts.find(destSlot->getId());
	if (costIt == groundCosts.end())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "No TU cost defined from ground to destination in the active ruleset.";
		}
		return false;
	}
	const int moveTu = costIt->second;
	if (moveTu < 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid transfer TU cost.";
		}
		return false;
	}

	if (!destSlot->fitItemInSlot(rules, destX, destY))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item does not fit at destination coordinates.";
		}
		return false;
	}
	if (companionInventoryOverlapItems(unit, item, destSlot, destX, destY))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Destination overlaps another item.";
		}
		return false;
	}

	if (moveTu > 0 && !checkReservedTU(unit, moveTu, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units for ground pickup.";
		}
		return false;
	}

	if (_parentState != 0)
	{
		cancelCurrentAction();
	}
	_save->setSelectedUnit(unit);
	companionUpdateSoldierInfoIfUiPresent(true);

	const int tuBefore = unit->getTimeUnits();
	if (moveTu > 0 && !unit->spendTimeUnits(moveTu))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units for ground pickup.";
		}
		return false;
	}

	companionInventoryMoveItemForUnit(unit, item, destSlot, destX, destY);
	item->moveToOwner(unit);

	if (_parentState != 0 && _parentState->getGame() != 0)
	{
		_parentState->getGame()->getMod()->getSoundByDepth(_save->getDepth(), Mod::ITEM_DROP)->play(
			-1, getMap()->getSoundAngle(unit->getPosition()));
	}
	companionUpdateSoldierInfoIfUiPresent(false);
	return true;
}

bool BattlescapeGame::tryExecuteCompanionDrop(BattleUnit *unit, BattleItem *item, std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (item == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item missing.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}

	RuleItem *rules = item->getRules();
	if (rules == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid item rules.";
		}
		return false;
	}
	if (rules->isFixed())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Fixed equipment cannot be dropped.";
		}
		return false;
	}

	if (item->isAmmo() && item->getSlot() == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Nested ammunition cannot be dropped directly.";
		}
		return false;
	}

	if (item->getOwner() != unit)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item does not belong to the actor.";
		}
		return false;
	}

	RuleInventory *srcSlot = item->getSlot();
	if (srcSlot == 0 || srcSlot->getType() == INV_GROUND)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item must be carried to drop to ground.";
		}
		return false;
	}

	Tile *actorTile = unit->getTile();
	if (actorTile == 0 || actorTile->isVoid())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Actor has no ground tile.";
		}
		return false;
	}

	Mod *mod = 0;
	if (_parentState != 0 && _parentState->getGame() != 0)
	{
		mod = _parentState->getGame()->getMod();
	}
	else if (_save != 0)
	{
		BattlescapeState *bs = _save->getBattleState();
		if (bs != 0 && bs->getGame() != 0)
		{
			mod = bs->getGame()->getMod();
		}
		else if (_save->getTestMod() != 0)
		{
			mod = _save->getTestMod();
		}
	}
	if (mod == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Mod unavailable.";
		}
		return false;
	}

	RuleInventory *groundRule = mod->getInventory("STR_GROUND", true);
	if (groundRule == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Ground inventory rules unavailable.";
		}
		return false;
	}

	const std::map<std::string, int> &srcCosts = srcSlot->getTransferCosts();
	std::map<std::string, int>::const_iterator dropCostIt = srcCosts.find(groundRule->getId());
	if (dropCostIt == srcCosts.end())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "No TU cost defined from this section to ground in the active ruleset.";
		}
		return false;
	}
	const int moveTu = dropCostIt->second;
	if (moveTu < 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid transfer TU cost.";
		}
		return false;
	}

	int destX = 0;
	int destY = 0;
	if (!companionFindFirstGroundPlacement(unit, groundRule, item, &destX, &destY))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "No room on ground for this item.";
		}
		return false;
	}

	if (moveTu > 0 && !checkReservedTU(unit, moveTu, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units for ground drop.";
		}
		return false;
	}

	if (_parentState != 0)
	{
		cancelCurrentAction();
	}
	_save->setSelectedUnit(unit);
	companionUpdateSoldierInfoIfUiPresent(true);

	if (moveTu > 0 && !unit->spendTimeUnits(moveTu))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units for ground drop.";
		}
		return false;
	}

	companionInventoryMoveItemForUnit(unit, item, groundRule, destX, destY);

	if (_parentState != 0 && _parentState->getGame() != 0)
	{
		_parentState->getGame()->getMod()->getSoundByDepth(_save->getDepth(), Mod::ITEM_DROP)->play(
			-1, getMap()->getSoundAngle(unit->getPosition()));
	}
	companionUpdateSoldierInfoIfUiPresent(false);
	return true;
}

bool BattlescapeGame::tryExecuteCompanionThrow(BattleUnit *unit, Position target, BattleItem *weapon, std::string *rejectReason)
{
	if (unit == 0 || unit->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit unavailable.";
		}
		return false;
	}
	if (weapon == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "No grenade in hand.";
		}
		return false;
	}
	if (isBusy())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle is busy.";
		}
		return false;
	}
	if (!companionItemIsThrowableGrenade(weapon))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item in hand does not support throwing.";
		}
		return false;
	}
	if (!_save->getItemUsable(weapon).empty())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Grenade is not usable.";
		}
		return false;
	}
	const int throwTu = unit->getActionTUs(BA_THROW, weapon);
	if (throwTu <= 0 || !checkReservedTU(unit, throwTu, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to throw grenade.";
		}
		return false;
	}
	if (target.x < 0 || target.y < 0 || target.z < 0 ||
	    target.x >= _save->getMapSizeX() || target.y >= _save->getMapSizeY() || target.z >= _save->getMapSizeZ())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Target out of map bounds.";
		}
		return false;
	}
	Tile *targetTile = _save->getTile(target);
	if (targetTile == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid target.";
		}
		return false;
	}

	cancelCurrentAction();
	_save->setSelectedUnit(unit);
	_parentState->updateSoldierInfo();

	_currentAction = BattleAction();
	_currentAction.type = BA_THROW;
	_currentAction.actor = unit;
	_currentAction.weapon = weapon;
	_currentAction.target = target;
	_currentAction.TU = throwTu;
	_currentAction.targeting = true;
	_currentAction.playerOrdered = true;
	_currentAction.cameraPosition = getMap()->getCamera()->getMapOffset();

	if (!ProjectileFlyBState::validThrowRange(&_currentAction, unit->getPosition(), targetTile))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Target is outside throwing range.";
		}
		cancelCurrentAction();
		return false;
	}

	getMap()->setCursorType(CT_NONE);
	_parentState->getGame()->getCursor()->setVisible(false);
	_states.push_back(new ProjectileFlyBState(this, _currentAction));
	statePushFront(new UnitTurnBState(this, _currentAction));
	return true;
}

bool BattlescapeGame::humanPlayerOrderPermitted(BattleUnit *unit) const
{
	if (unit == 0)
	{
		return true;
	}
	SavedGame *campaign = companionCampaignSaveForValidation();
	std::string controlJson;
	CompanionTacticalAuthorityCore::tryReadRelayControlJsonFromSnapshotEnvironment(&controlJson);
	return CompanionTacticalAuthorityCore::humanPlayerOrderPermitted(campaign, _save, unit, controlJson);
}

bool BattlescapeGame::warnIfHumanPlayerOrderDenied(BattleUnit *unit) const
{
	if (humanPlayerOrderPermitted(unit))
	{
		return false;
	}
	_parentState->warning("STR_COMPANION_MOVEMENT_DENIED");
	return true;
}

void BattlescapeGame::tickCompanionCommand(bool battleInitialized)
{
	SavedGame *campaign = companionCampaignSaveForValidation();
	CompanionCommandRuntime::tick(this, _save, campaign, battleInitialized);
}

}
