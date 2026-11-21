CompanionCommandCore::PrimeValidationInput buildMcPrimeContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::PrimeCommandParsed &parsed,
	BattleUnit *actor)
{
	CompanionCommandCore::PrimeValidationInput input = {};
	input.activeHand = parsed.activeHand;
	input.activeHandValid = true;
	BattleItem *weapon = actor != 0 ? actor->getItem(parsed.activeHand) : 0;
	input.grenadePresentInHand = weapon != 0;
	input.grenadeSupportsPrime = companionHandGrenadeSupportsPrime(weapon);
	input.grenadeAlreadyPrimed = weapon != 0 && weapon->getFuseTimer() >= 0;
	const int battleType = weapon != 0 ? static_cast<int>(weapon->getRules()->getBattleType()) : BT_NONE;
	input.invalidFuseTimer = weapon == 0 ||
		!CompanionCommandCore::isValidPrimeFuseTimerForBattleType(parsed.fuseTimer, battleType);
	input.grenadeUsable = companionGrenadeUsable(battle, weapon);
	const int primeTu = actor != 0 && weapon != 0 ? actor->getActionTUs(BA_PRIME, weapon) : 0;
	input.insufficientTuForPrime = actor == 0 || weapon == 0 || primeTu <= 0 ||
		!game->checkReservedTU(actor, primeTu, true);
	return input;
}

CompanionCommandCore::ThrowValidationInput buildMcThrowContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::ThrowCommandParsed &parsed,
	BattleUnit *actor)
{
	CompanionCommandCore::ThrowValidationInput input = {};
	input.targetX = parsed.targetX;
	input.targetY = parsed.targetY;
	input.targetZ = parsed.targetZ;
	input.mapSizeX = battle->getMapSizeX();
	input.mapSizeY = battle->getMapSizeY();
	input.mapSizeZ = battle->getMapSizeZ();
	input.activeHand = parsed.activeHand;
	input.activeHandValid = true;
	BattleItem *weapon = actor != 0 ? actor->getItem(parsed.activeHand) : 0;
	input.grenadePresentInHand = weapon != 0;
	input.grenadeSupportsThrow = companionHandGrenadeSupportsThrow(weapon);
	input.grenadeUsable = companionGrenadeUsable(battle, weapon);
	input.targetOutOfThrowRange = false;
	if (actor != 0 && weapon != 0)
	{
		const Position target(parsed.targetX, parsed.targetY, parsed.targetZ);
		if (target.x >= 0 && target.y >= 0 && target.z >= 0 &&
		    target.x < battle->getMapSizeX() && target.y < battle->getMapSizeY() && target.z < battle->getMapSizeZ())
		{
			Tile *targetTile = battle->getTile(target);
			if (targetTile != 0)
			{
				BattleAction probe = {};
				probe.type = BA_THROW;
				probe.actor = actor;
				probe.weapon = weapon;
				probe.target = target;
				input.targetOutOfThrowRange = !ProjectileFlyBState::validThrowRange(&probe, actor->getPosition(), targetTile);
			}
		}
	}
	const int throwTu = actor != 0 && weapon != 0 ? actor->getActionTUs(BA_THROW, weapon) : 0;
	input.insufficientTuForThrow = actor == 0 || weapon == 0 || throwTu <= 0 ||
		!game->checkReservedTU(actor, throwTu, true);
	return input;
}

CompanionCommandCore::ReloadValidationInput buildMcReloadContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::ReloadCommandParsed &parsed,
	BattleUnit *actor)
{
	CompanionCommandCore::ReloadValidationInput input = {};
	input.weaponBattleItemId = parsed.weaponBattleItemId;
	input.ammoBattleItemId = parsed.ammoBattleItemId;

	BattleItem *weapon = findBattleItemByIdOnUnit(actor, parsed.weaponBattleItemId);
	BattleItem *ammo = findBattleItemByIdOnUnit(actor, parsed.ammoBattleItemId);

	input.weaponResolved = weapon != 0;
	input.ammoResolved = ammo != 0;
	input.campaignResearchAvailable = campaign != 0;

	input.weaponBelongsToActor = weapon != 0 && weapon->getOwner() == actor;
	input.ammoBelongsToActor = ammo != 0 && ammo->getOwner() == actor;

	input.weaponInEitherHand =
		weapon != 0 &&
		weapon->getSlot() != 0 &&
		weapon->getSlot()->getType() == INV_HAND &&
		companionReloadWeaponHeldInLeftOrRightHand(actor, weapon);

	input.weaponIsFirearm = weapon != 0 && weapon->getRules() != 0 &&
		weapon->getRules()->getBattleType() == BT_FIREARM;
	input.weaponNeedsPhysicalMagazine = weapon != 0 && weapon->needsAmmo() &&
		weapon->getRules() != 0 && !weapon->getRules()->getCompatibleAmmo()->empty();
	input.weaponChamberEmptyForReloadUi = weapon != 0 && weapon->getAmmoItem() == 0;

	input.ammoIsPhysicalClip = ammo != 0 && ammo->getRules() != 0 &&
		ammo->getRules()->getBattleType() == BT_AMMO;
	input.ammoCarriedOffGround = ammo != 0 && ammo->getSlot() != 0 &&
		ammo->getSlot()->getType() != INV_GROUND;
	input.ammoNotAlreadyLoadedInWeapon = !(weapon != 0 && ammo != 0 && weapon->getAmmoItem() == ammo);
	input.ammunitionCompatible = companionReloadAmmoCompatible(weapon, ammo);

	input.weaponUsableEnvironment = battle != 0 && weapon != 0 && ammo != 0 &&
		battle->getItemUsable(weapon).empty() && battle->getItemUsable(ammo).empty();

	const bool resOk = campaign != 0 && weapon != 0 && ammo != 0 && weapon->getRules() != 0 && ammo->getRules() != 0;
	input.weaponResearchAuthorized = resOk && campaign->isResearched(weapon->getRules()->getRequirements());
	input.ammoResearchAuthorized = resOk && campaign->isResearched(ammo->getRules()->getRequirements());

	const int reloadTu = 15;
	input.insufficientTuForReload = actor == 0 || weapon == 0 || ammo == 0 ||
		!game->checkReservedTU(actor, reloadTu, true);
	return input;
}

CompanionCommandCore::UnloadValidationInput buildMcUnloadContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::UnloadCommandParsed &parsed,
	BattleUnit *actor)
{
	CompanionCommandCore::UnloadValidationInput input = {};
	input.weaponBattleItemId = parsed.weaponBattleItemId;
	input.loadedAmmoBattleItemId = parsed.loadedAmmoBattleItemId;

	BattleItem *weapon = findBattleItemByIdOnUnit(actor, parsed.weaponBattleItemId);
	BattleItem *loadedAmmo = weapon != 0 ? weapon->getAmmoItem() : 0;

	input.weaponResolved = weapon != 0;
	input.campaignResearchAvailable = campaign != 0;

	input.weaponBelongsToActor = weapon != 0 && weapon->getOwner() == actor;

	input.weaponInEitherHand =
		weapon != 0 &&
		weapon->getSlot() != 0 &&
		weapon->getSlot()->getType() == INV_HAND &&
		companionReloadWeaponHeldInLeftOrRightHand(actor, weapon);

	input.weaponIsFirearm = weapon != 0 && weapon->getRules() != 0 &&
		weapon->getRules()->getBattleType() == BT_FIREARM;
	input.weaponNeedsPhysicalMagazine = weapon != 0 && weapon->needsAmmo() &&
		weapon->getRules() != 0 && !weapon->getRules()->getCompatibleAmmo()->empty();

	input.weaponHasLoadedMagazine = loadedAmmo != 0;
	input.loadedAmmoIdentityMatches =
		loadedAmmo != 0 && loadedAmmo->getId() == parsed.loadedAmmoBattleItemId;
	input.loadedAmmoIsPhysicalClip =
		loadedAmmo != 0 && loadedAmmo->getRules() != 0 &&
		loadedAmmo->getRules()->getBattleType() == BT_AMMO;

	input.otherHandClearExceptWeapon = companionUnloadOtherHandClear(actor, weapon);

	input.weaponUsableEnvironment = battle != 0 && weapon != 0 && loadedAmmo != 0 &&
		battle->getItemUsable(weapon).empty() && battle->getItemUsable(loadedAmmo).empty();

	const bool resOk =
		campaign != 0 && weapon != 0 && loadedAmmo != 0 &&
		weapon->getRules() != 0 && loadedAmmo->getRules() != 0;
	input.weaponResearchAuthorized = resOk && campaign->isResearched(weapon->getRules()->getRequirements());
	input.ammoResearchAuthorized = resOk && campaign->isResearched(loadedAmmo->getRules()->getRequirements());

	const int unloadTu = 8;
	input.insufficientTuForUnload = actor == 0 || weapon == 0 || loadedAmmo == 0 ||
		!game->checkReservedTU(actor, unloadTu, true);
	return input;
}

CompanionCommandCore::InventoryMoveValidationInput buildMcInventoryMoveContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::InventoryMoveCommandParsed &parsed,
	BattleUnit *actor)
{
	CompanionCommandCore::InventoryMoveValidationInput input = {};
	input.itemBattleItemId = parsed.itemBattleItemId;
	input.sourceSlotId = parsed.sourceSlot;
	input.destinationSlotId = parsed.destinationSlot;
	input.destinationX = parsed.destinationX;
	input.destinationY = parsed.destinationY;
	input.sourceCoordsGuard = parsed.sourceCoordsGuard;
	input.sourceX = parsed.sourceX;
	input.sourceY = parsed.sourceY;

	BattleItem *item = findBattleItemByIdOnUnit(actor, parsed.itemBattleItemId);
	Mod *mod = game != 0 ? game->getMod() : 0;
	RuleInventory *destRule = mod != 0 ? mod->getInventory(parsed.destinationSlot, false) : 0;

	input.itemResolved = item != 0;
	input.itemBelongsToActor = item != 0 && item->getOwner() == actor;
	input.itemCarriedOffGround = item != 0 && item->getSlot() != 0 && item->getSlot()->getType() != INV_GROUND;

	RuleInventory *srcSlot = item != 0 ? item->getSlot() : 0;
	input.itemSlotMatchesCommand =
		item != 0 && srcSlot != 0 && srcSlot->getId() == parsed.sourceSlot;

	input.sourceCoordsMatch =
		!parsed.sourceCoordsGuard ||
		(item != 0 &&
		 item->getSlotX() == parsed.sourceX &&
		 item->getSlotY() == parsed.sourceY);

	RuleItem *itemRules = item != 0 ? item->getRules() : 0;
	input.itemIsFixedWeapon = itemRules != 0 && itemRules->isFixed();
	input.itemNestedInFirearm = item != 0 && item->isAmmo() && item->getSlot() == 0;

	input.primedExplosiveBlocked = false;
	if (item != 0 && itemRules != 0 &&
	    item->getFuseTimer() >= 0 &&
	    (itemRules->getBattleType() == BT_GRENADE || itemRules->getBattleType() == BT_PROXIMITYGRENADE))
	{
		input.primedExplosiveBlocked = true;
	}

	input.destinationSlotResolved = destRule != 0;
	input.destinationIsGround = parsed.destinationSlot == "STR_GROUND";
	input.destinationAllowedCarriedType = isCarriedInventorySectionString(parsed.destinationSlot);
	input.destinationSameSectionAsCommand =
		destRule != 0 && destRule->getId() == parsed.destinationSlot;

	input.itemFitsDestinationGeometry = false;
	input.destinationOccupied = false;
	input.transferCostResolved = false;
	input.transferCostTu = -1;
	input.insufficientTuForInventoryMove = false;

	if (item == 0 || srcSlot == 0 || mod == 0 || destRule == 0 || input.primedExplosiveBlocked)
	{
		return input;
	}

	input.itemFitsDestinationGeometry = destRule->fitItemInSlot(itemRules, parsed.destinationX, parsed.destinationY);
	input.destinationOccupied =
		input.itemFitsDestinationGeometry &&
		companionRuntimeInventoryOverlap(actor, item, destRule, parsed.destinationX, parsed.destinationY);

	int moveTu = 0;
	if (srcSlot->getId() == destRule->getId())
	{
		moveTu = 0;
		input.transferCostResolved = true;
	}
	else
	{
		const std::map<std::string, int> &costs = srcSlot->getTransferCosts();
		std::map<std::string, int>::const_iterator cit = costs.find(destRule->getId());
		if (cit != costs.end())
		{
			moveTu = cit->second;
			input.transferCostResolved = true;
		}
	}
	input.transferCostTu = moveTu;

	if (!input.transferCostResolved || input.destinationOccupied || !input.itemFitsDestinationGeometry ||
	    input.primedExplosiveBlocked)
	{
		return input;
	}

	input.insufficientTuForInventoryMove =
		moveTu > 0 && (actor == 0 || !game->checkReservedTU(actor, moveTu, true));
	return input;
}

CompanionCommandCore::PickupValidationInput buildMcPickupContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::PickupCommandParsed &parsed,
	BattleUnit *actor)
{
	CompanionCommandCore::PickupValidationInput input = {};
	input.itemBattleItemId = parsed.itemBattleItemId;
	input.groundX = parsed.groundX;
	input.groundY = parsed.groundY;
	input.groundZ = parsed.groundZ;
	input.destinationSlotId = parsed.destinationSlot;
	input.destinationX = parsed.destinationX;
	input.destinationY = parsed.destinationY;

	input.campaignResearchAvailable = campaign != 0;
	input.groundPositionMatchesActor =
		actor != 0 &&
		actor->getPosition().x == parsed.groundX &&
		actor->getPosition().y == parsed.groundY &&
		actor->getPosition().z == parsed.groundZ;

	BattleItem *groundItem = findGroundItemFromActorExport(actor, parsed.itemBattleItemId);
	input.itemResolvedOnActorGround = groundItem != 0;

	RuleItem *itemRules = groundItem != 0 ? groundItem->getRules() : 0;
	input.itemIsFixedWeapon = itemRules != 0 && itemRules->isFixed();
	input.itemResearchAuthorized =
		itemRules != 0 && campaign != 0 && campaign->isResearched(itemRules->getRequirements());

	Mod *mod = game != 0 ? game->getMod() : 0;
	RuleInventory *groundRule = mod != 0 ? mod->getInventory("STR_GROUND", false) : 0;
	RuleInventory *destRule = mod != 0 ? mod->getInventory(parsed.destinationSlot, false) : 0;

	input.destinationSlotResolved = destRule != 0;
	input.destinationAllowedCarriedType = isCarriedInventorySectionString(parsed.destinationSlot);
	input.destinationSameSectionAsCommand =
		destRule != 0 && destRule->getId() == parsed.destinationSlot;

	input.itemFitsDestinationGeometry = false;
	input.destinationOccupied = false;
	input.transferCostResolved = false;
	input.transferCostTu = -1;
	input.insufficientTuForPickup = false;

	if (campaign == 0 || groundItem == 0 || itemRules == 0 || mod == 0 || groundRule == 0 || destRule == 0)
	{
		return input;
	}

	input.itemFitsDestinationGeometry = destRule->fitItemInSlot(itemRules, parsed.destinationX, parsed.destinationY);
	input.destinationOccupied =
		input.itemFitsDestinationGeometry &&
		companionRuntimeInventoryOverlap(actor, groundItem, destRule, parsed.destinationX, parsed.destinationY);

	const std::map<std::string, int> &fromGround = groundRule->getTransferCosts();
	std::map<std::string, int>::const_iterator cit = fromGround.find(destRule->getId());
	if (cit != fromGround.end() && cit->second >= 0)
	{
		input.transferCostResolved = true;
		input.transferCostTu = cit->second;
	}

	if (!input.transferCostResolved || input.destinationOccupied || !input.itemFitsDestinationGeometry)
	{
		return input;
	}

	const int moveTu = input.transferCostTu;
	input.insufficientTuForPickup =
		moveTu > 0 && (actor == 0 || !game->checkReservedTU(actor, moveTu, true));
	return input;
}

CompanionCommandCore::DropValidationInput buildMcDropContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::DropCommandParsed &parsed,
	BattleUnit *actor)
{
	CompanionCommandCore::DropValidationInput input = {};
	input.itemBattleItemId = parsed.itemBattleItemId;
	input.sourceSlotId = parsed.sourceSlot;
	input.sourceCoordsGuard = parsed.sourceCoordsGuard;
	input.sourceX = parsed.sourceX;
	input.sourceY = parsed.sourceY;

	input.campaignResearchAvailable = campaign != 0;

	BattleItem *item = findBattleItemByIdOnUnit(actor, parsed.itemBattleItemId);
	Mod *mod = game != 0 ? game->getMod() : 0;
	RuleInventory *groundRule = mod != 0 ? mod->getInventory("STR_GROUND", false) : 0;

	input.itemResolved = item != 0;
	input.itemBelongsToActor = item != 0 && item->getOwner() == actor;
	input.itemCarriedOffGround = item != 0 && item->getSlot() != 0 && item->getSlot()->getType() != INV_GROUND;

	RuleInventory *srcSlot = item != 0 ? item->getSlot() : 0;
	input.itemSlotMatchesCommand =
		item != 0 && srcSlot != 0 && srcSlot->getId() == parsed.sourceSlot;

	input.sourceCoordsMatch =
		!parsed.sourceCoordsGuard ||
		(item != 0 &&
		 item->getSlotX() == parsed.sourceX &&
		 item->getSlotY() == parsed.sourceY);

	RuleItem *itemRules = item != 0 ? item->getRules() : 0;
	input.itemIsFixedWeapon = itemRules != 0 && itemRules->isFixed();
	input.itemNestedInFirearm = item != 0 && item->isAmmo() && item->getSlot() == 0;
	input.itemResearchAuthorized =
		itemRules != 0 && campaign != 0 && campaign->isResearched(itemRules->getRequirements());

	input.groundRuleResolved = groundRule != 0;
	input.groundHasRoom = false;
	input.transferCostResolved = false;
	input.transferCostTu = -1;
	input.insufficientTuForDrop = false;

	if (campaign == 0 || item == 0 || itemRules == 0 || mod == 0 || groundRule == 0 || srcSlot == 0 ||
	    srcSlot->getType() == INV_GROUND || input.itemNestedInFirearm)
	{
		return input;
	}

	int gx = 0;
	int gy = 0;
	input.groundHasRoom = OpenXcom::companionFindFirstGroundPlacement(actor, groundRule, item, &gx, &gy);

	const std::map<std::string, int> &costs = srcSlot->getTransferCosts();
	std::map<std::string, int>::const_iterator cit = costs.find(groundRule->getId());
	if (cit != costs.end() && cit->second >= 0)
	{
		input.transferCostResolved = true;
		input.transferCostTu = cit->second;
	}

	if (!input.transferCostResolved || !input.groundHasRoom)
	{
		return input;
	}

	const int moveTu = input.transferCostTu;
	input.insufficientTuForDrop =
		moveTu > 0 && (actor == 0 || !game->checkReservedTU(actor, moveTu, true));
	return input;
}
