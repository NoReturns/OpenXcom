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
#include "MeleeAttackBState.h"
#include "ExplosionBState.h"
#include "BattlescapeGame.h"
#include "BattlescapeState.h"
#include "TileEngine.h"
#include "Map.h"
#include "Camera.h"
#include "AIModule.h"
#include "../Savegame/Tile.h"
#include "../Engine/RNG.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Engine/Sound.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleItem.h"
#include "../Engine/CompanionPerceptionLog.h"
#include "../Engine/CompanionCommandRuntime.h"
#include "../fmath.h"

namespace OpenXcom
{

/**
 * Sets up a MeleeAttackBState.
 */
MeleeAttackBState::MeleeAttackBState(BattlescapeGame *parent, BattleAction action) : BattleState(parent, action), _unit(0), _target(0), _weapon(0), _ammo(0), _initialized(false)
{
}

/**
 * Deletes the MeleeAttackBState.
 */
MeleeAttackBState::~MeleeAttackBState()
{
}

/**
 * Initializes the sequence.
 * does a lot of validity checking.
 */
void MeleeAttackBState::init()
{
	if (_initialized) return;
	_initialized = true;

	_weapon = _action.weapon;
	if (!_weapon) // can't shoot without weapon
	{
		_parent->popState();
		return;
	}
	_ammo = _weapon->getAmmoItem();
	if (!_ammo)
	{
		_ammo = _weapon;
	}

	if (!_parent->getSave()->getTile(_action.target)) // invalid target position
	{
		_parent->popState();
		return;
	}

	_unit = _action.actor;

	if (_unit->isOut() || _unit->getHealth() == 0 || _unit->getHealth() < _unit->getStunlevel())
	{
		// something went wrong - we can't shoot when dead or unconscious, or if we're about to fall over.
		_parent->popState();
		return;
	}

	// reaction fire
	if (_unit->getFaction() != _parent->getSave()->getSide())
	{
		// no ammo or target is dead: give the time units back and cancel the shot.
		if (_ammo == 0
			|| !_parent->getSave()->getTile(_action.target)->getUnit()
			|| _parent->getSave()->getTile(_action.target)->getUnit()->isOut()
			|| _parent->getSave()->getTile(_action.target)->getUnit() != _parent->getSave()->getSelectedUnit())
		{
			_unit->setTimeUnits(_unit->getTimeUnits() + _unit->getActionTUs(_action.type, _action.weapon));
			_parent->popState();
			return;
		}
		_unit->lookAt(_action.target, _unit->getTurretType() != -1);
		while (_unit->getStatus() == STATUS_TURNING)
		{
			_unit->turn();
		}
	}


	AIModule *ai = _unit->getAIModule();

	if (_unit->getFaction() == _parent->getSave()->getSide() &&
		_unit->getFaction() != FACTION_PLAYER &&
		_parent->_debugPlay == false &&
		ai && ai->getTarget())
	{
		_target = ai->getTarget();
	}
	else
	{
		_target = _parent->getSave()->getTile(_action.target)->getUnit();
	}

	int height = _target->getFloatHeight() + (_target->getHeight() / 2) - _parent->getSave()->getTile(_action.target)->getTerrainLevel();
	_voxel = _action.target * Position(16, 16, 24) + Position(8, 8, height);

	performMeleeAttack();
}

/**
 * Performs all the overall functions of the state, this code runs AFTER the explosion state pops.
 */
void MeleeAttackBState::think()
{
	_parent->getSave()->getBattleState()->clearMouseScrollingState();

	// if the unit burns floortiles, burn floortiles
	if (_unit->getSpecialAbility() == SPECAB_BURNFLOOR || _unit->getSpecialAbility() == SPECAB_BURN_AND_EXPLODE)
	{
		_parent->getSave()->getTile(_action.target)->ignite(15);
	}
	// Determine if the attack was successful
	// we do this here instead of letting the explosionBState take care of damage and casualty checking
	// this is because unlike regular bullet hits or explosions, melee attacks can MISS.
	// we also do it at this point instead of in performMeleeAttack because we want the scream to come AFTER the hit sound
	resolveHit();
		// aliens
	if (_unit->getFaction() != FACTION_PLAYER &&
		// not performing a reaction attack
		_unit->getFaction() == _parent->getSave()->getSide() &&
		// with enough TU for a second attack (*2 because they'll get charged for the initial attack when this state pops.)
		_unit->getTimeUnits() >= _unit->getActionTUs(BA_HIT, _action.weapon) * 2 &&
		// whose target is still alive or at least conscious
		_target && _target->getHealth() > 0 &&
		_target->getHealth() > _target->getStunlevel() &&
		// and we still have ammo to make the attack
		_weapon->getAmmoItem())
	{
		// spend the TUs immediately
		_unit->spendTimeUnits(_unit->getActionTUs(BA_HIT, _weapon));
		performMeleeAttack();
	}
	else
	{
		if (_action.cameraPosition.z != -1)
		{
			_parent->getMap()->getCamera()->setMapOffset(_action.cameraPosition);
			_parent->getMap()->invalidate();
		}
//		melee doesn't trigger a reaction, remove comments to enable.
//		if (!_parent->getSave()->getUnitsFalling())
//		{
//			_parent->getTileEngine()->checkReactionFire(_unit);
//		}

		if (_unit->getFaction() == _parent->getSave()->getSide()) // not a reaction attack
		{
			_parent->getCurrentAction()->type = BA_NONE; // do this to restore cursor
		}

		if (_parent->getSave()->getSide() == FACTION_PLAYER || _parent->getSave()->getDebugMode())
		{
			_parent->setupCursor();
		}
		_parent->convertInfected();
		_parent->popState();
	}
}

/**
 * Sets up a melee attack, inserts an explosion into the map and make noises.
 */
void MeleeAttackBState::performMeleeAttack()
{
	// set the soldier in an aiming position
	_unit->aim(true);
	Map *map = _parent->getMap();
	if (map != 0)
	{
		_unit->setCache(0);
		map->cacheUnit(_unit);
	}
	// make some noise
	if (!_action.companionHeadlessMelee && map != 0)
	{
		if (_ammo && _ammo->getRules()->getMeleeAttackSound() != -1)
		{
			_parent->getMod()->getSoundByDepth(_parent->getDepth(), _ammo->getRules()->getMeleeAttackSound())->play(-1, map->getSoundAngle(_action.target));
		}
		else if (_weapon->getRules()->getMeleeAttackSound() != -1)
		{
			_parent->getMod()->getSoundByDepth(_parent->getDepth(), _weapon->getRules()->getMeleeAttackSound())->play(-1, map->getSoundAngle(_action.target));
		}
	}
	// use up ammo if applicable
	if (!_parent->getSave()->getDebugMode() && _weapon->getRules()->getBattleType() == BT_MELEE && _ammo && _ammo->spendBullet() == false)
	{
		_parent->getSave()->removeItem(_ammo);
		_action.weapon->setAmmoItem(0);
	}
	if (map != 0)
	{
		map->setCursorType(CT_NONE);
	}

	// make an explosion animation (skipped for companionExecuteMelee — damage applies in resolveHit)
	if (!_action.companionHeadlessMelee)
	{
		_parent->statePushFront(new ExplosionBState(_parent, _voxel, _action.weapon, _action.actor, 0, true, true));
	}

	if (_action.reactionFire)
	{
		bool reactionAudible = false;
		if (_ammo != 0 && _ammo->getRules()->getMeleeAttackSound() != -1)
		{
			reactionAudible = true;
		}
		else if (_weapon != 0 && _weapon->getRules()->getMeleeAttackSound() != -1)
		{
			reactionAudible = true;
		}
		_parent->getSave()->getCompanionPerceptionLog().tryRecordReactionAttackExecuted(
			_parent->getSave(), _unit, _unit->getPosition(), "melee", reactionAudible);
	}
}

/**
 * Determines if the melee attack hit, and performs all the applicable duties.
 */
void MeleeAttackBState::resolveHit()
{
	const bool rolledHit = RNG::percent(_unit->getFiringAccuracy(BA_HIT, _weapon));
	if (_action.companionRelayMelee && _unit != 0)
	{
		CompanionCommandRuntime::noteMeleeAttackResolved(_unit->getId(), _target, rolledHit);
	}
	if (rolledHit)
	{
		// Give soldiers XP
		if (_unit->getGeoscapeSoldier() &&
			_target && _target->getOriginalFaction() == FACTION_HOSTILE)
		{
			_unit->addMeleeExp();
		}

		// check if this unit turns others into zombies
		if (_weapon->getRules()->getBattleType() == BT_MELEE
			&& _ammo
			&& !_ammo->getRules()->getZombieUnit().empty()
			&& _target
			&& (_target->getGeoscapeSoldier() || _target->getUnitRules()->getRace() == "STR_CIVILIAN")
			&& _target->getSpawnUnit().empty())
		{
			// converts the victim to a zombie on death
			_target->setRespawn(true);
			_target->setSpawnUnit(_ammo->getRules()->getZombieUnit());
		}

		// assume rifle butt to begin with.
		ItemDamageType type = DT_STUN;
		int power = _weapon->getRules()->getMeleePower();
		// override it as needed.
		if (_weapon->getRules()->getBattleType() == BT_MELEE && _ammo)
		{
			type = _ammo->getRules()->getDamageType();;
			power = _ammo->getRules()->getPower();
		}

		// since melee aliens don't use a conventional weapon type, we use their strength instead.
		if (_weapon->getRules()->isStrengthApplied())
		{
			power += _unit->getBaseStats()->strength;
		}
		// make some noise to signal the hit.
		Map *map = _parent->getMap();
		if (!_action.companionHeadlessMelee && map != 0 && _weapon->getRules()->getMeleeHitSound() != -1)
		{
			_parent->getMod()->getSoundByDepth(_parent->getDepth(), _action.weapon->getRules()->getMeleeHitSound())->play(-1, map->getSoundAngle(_action.target));
		}

		// offset the damage voxel ever so slightly so that the target knows which side the attack came from
		Position difference = _unit->getPosition() - _action.target;
		// large units may cause it to offset too much, so we'll clamp the values.
		difference.x = Clamp(difference.x, -1, 1);
		difference.y = Clamp(difference.y, -1, 1);

		Position damagePosition = _voxel + difference;
		// damage the unit.
		_parent->getSave()->getTileEngine()->hit(damagePosition, power, type, _unit);
		// now check for new casualties
		_parent->checkForCasualties(_ammo, _unit);
	}
}

bool MeleeAttackBState::companionExecuteMelee(
	BattlescapeGame *parent,
	BattleAction action,
	BattleUnit *target,
	std::string *rejectReason,
	bool *meleeHitOut,
	bool *targetUnconsciousOut,
	bool *targetKilledOut)
{
	if (meleeHitOut != 0)
	{
		*meleeHitOut = false;
	}
	if (targetUnconsciousOut != 0)
	{
		*targetUnconsciousOut = false;
	}
	if (targetKilledOut != 0)
	{
		*targetKilledOut = false;
	}
	if (parent == 0 || action.actor == 0 || action.weapon == 0 || target == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid melee parameters.";
		}
		return false;
	}
	SavedBattleGame *save = parent->getSave();
	if (save == 0 || save->getTile(target->getPosition()) == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid target position.";
		}
		return false;
	}

	action.type = BA_HIT;
	action.target = target->getPosition();
	action.weapon = action.actor->getMeleeWeapon();
	if (action.weapon == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "No melee weapon available.";
		}
		return false;
	}
	if (action.TU <= 0)
	{
		action.TU = action.actor->getActionTUs(BA_HIT, action.weapon);
	}
	if (action.actor->getTimeUnits() < action.TU)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units.";
		}
		return false;
	}
	if (target->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Target unit unavailable.";
		}
		return false;
	}

	const int stunBefore = target->getStunlevel();
	const int healthBefore = target->getHealth();

	MeleeAttackBState state(parent, action);
	state._unit = action.actor;
	state._target = target;
	state._weapon = action.weapon;
	state._ammo = action.weapon->getAmmoItem() != 0 ? action.weapon->getAmmoItem() : action.weapon;
	state._action = action;
	state._action.companionHeadlessMelee = true;
	state._initialized = true;

	int height = target->getFloatHeight() + (target->getHeight() / 2) - save->getTile(action.target)->getTerrainLevel();
	state._voxel = action.target * Position(16, 16, 24) + Position(8, 8, height);

	state.performMeleeAttack();
	state.resolveHit();

	if (!action.actor->spendTimeUnits(action.TU))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units.";
		}
		return false;
	}

	const bool hit = target->getStunlevel() > stunBefore || target->getHealth() < healthBefore;
	const bool unconscious = target->getStatus() == STATUS_UNCONSCIOUS
		|| (target->getStunlevel() >= target->getHealth() && target->getHealth() > 0);
	const bool killed = target->getStatus() == STATUS_DEAD || target->getHealth() <= 0;

	if (meleeHitOut != 0)
	{
		*meleeHitOut = hit;
	}
	if (targetUnconsciousOut != 0)
	{
		*targetUnconsciousOut = unconscious;
	}
	if (targetKilledOut != 0)
	{
		*targetKilledOut = killed;
	}
	return true;
}

}
