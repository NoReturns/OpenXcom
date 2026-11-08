/*
 * Open X-Com Companion — ground inventory helpers (WP8B4).
 */
#include "CompanionGroundInventoryCore.h"
#include "Screen.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Tile.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleItem.h"
#include <algorithm>
#include <vector>

namespace OpenXcom
{

namespace
{

static bool companionGroundStackCompatible(BattleItem *itemA, BattleItem *itemB)
{
	return (itemA != 0 && itemB != 0 &&
		itemA->getRules() == itemB->getRules() &&
		((!itemA->getAmmoItem() && !itemB->getAmmoItem()) ||
		 (itemA->getAmmoItem() && itemB->getAmmoItem() &&
		  itemA->getAmmoItem()->getRules() == itemB->getAmmoItem()->getRules() &&
		  itemA->getAmmoItem()->getAmmoQuantity() == itemB->getAmmoItem()->getAmmoQuantity())) &&
		itemA->getFuseTimer() == -1 && itemB->getFuseTimer() == -1 &&
		itemA->getUnit() == 0 && itemB->getUnit() == 0 &&
		itemA->getPainKillerQuantity() == itemB->getPainKillerQuantity() &&
		itemA->getHealQuantity() == itemB->getHealQuantity() &&
		itemA->getStimulantQuantity() == itemB->getStimulantQuantity());
}

static bool companionGroundOverlapItems(BattleUnit *unit, BattleItem *movingItem, RuleInventory *groundRule, int x, int y)
{
	if (unit == 0 || movingItem == 0 || groundRule == 0)
	{
		return true;
	}
	Tile *tile = unit->getTile();
	if (tile == 0)
	{
		return true;
	}
	for (std::vector<BattleItem*>::const_iterator j = tile->getInventory()->begin();
	     j != tile->getInventory()->end(); ++j)
	{
		BattleItem *other = *j;
		if (other == 0 || other == movingItem)
		{
			continue;
		}
		if (other->occupiesSlot(x, y, movingItem))
		{
			return true;
		}
	}
	return false;
}

} // namespace

void companionArrangeGroundSlotCoordsForUnit(BattleUnit *unit, RuleInventory *groundRule)
{
	if (unit == 0 || groundRule == 0 || groundRule->getType() != INV_GROUND)
	{
		return;
	}
	Tile *tile = unit->getTile();
	if (tile == 0)
	{
		return;
	}
	std::vector<BattleItem*> *groundInv = tile->getInventory();
	if (groundInv == 0 || groundInv->empty())
	{
		return;
	}

	const int slotsX = (Screen::ORIGINAL_WIDTH - groundRule->getX()) / RuleInventory::SLOT_W;
	const int slotsY = (Screen::ORIGINAL_HEIGHT - groundRule->getY()) / RuleInventory::SLOT_H;

	for (std::vector<BattleItem*>::iterator i = groundInv->begin(); i != groundInv->end(); ++i)
	{
		(*i)->setSlot(groundRule);
		(*i)->setSlotX(1000000);
		(*i)->setSlotY(0);
	}

	for (std::vector<BattleItem*>::iterator i = groundInv->begin(); i != groundInv->end(); ++i)
	{
		int x = 0;
		int y = 0;
		bool ok = false;
		// Same scan as Inventory::arrangeGround — cap iterations so an overfull or oversized
		// ground layout cannot spin forever (headless tests use minimal maps).
		const int maxIterations = std::max(1, (slotsX + 1) * (slotsY + 1) * 4);
		int iterations = 0;
		while (!ok)
		{
			if (++iterations > maxIterations)
			{
				break;
			}
			ok = true;
			for (int xd = 0; xd < (*i)->getRules()->getInventoryWidth() && ok; ++xd)
			{
				if ((x + xd) % slotsX < x % slotsX)
				{
					ok = false;
				}
				else
				{
					for (int yd = 0; yd < (*i)->getRules()->getInventoryHeight() && ok; ++yd)
					{
						BattleItem *cellItem = unit->getItem(groundRule, x + xd, y + yd);
						ok = cellItem == 0;
						if (cellItem != 0 && companionGroundStackCompatible(cellItem, *i))
						{
							ok = true;
						}
					}
				}
			}
			if (ok)
			{
				(*i)->setSlotX(x);
				(*i)->setSlotY(y);
			}
			else
			{
				y++;
				if (y > slotsY - (*i)->getRules()->getInventoryHeight())
				{
					y = 0;
					x++;
				}
			}
		}
	}
}

bool companionFindFirstGroundPlacement(
	BattleUnit *unit,
	RuleInventory *groundRule,
	BattleItem *item,
	int *outX,
	int *outY)
{
	if (unit == 0 || groundRule == 0 || item == 0 || item->getRules() == 0 || outX == 0 || outY == 0)
	{
		return false;
	}
	companionArrangeGroundSlotCoordsForUnit(unit, groundRule);

	const int slotsX = (Screen::ORIGINAL_WIDTH - groundRule->getX()) / RuleInventory::SLOT_W;
	const int slotsY = (Screen::ORIGINAL_HEIGHT - groundRule->getY()) / RuleInventory::SLOT_H;

	for (int y2 = 0; y2 <= slotsY - item->getRules()->getInventoryHeight(); ++y2)
	{
		for (int x2 = 0; x2 <= slotsX - item->getRules()->getInventoryWidth(); ++x2)
		{
			if (companionGroundOverlapItems(unit, item, groundRule, x2, y2))
			{
				continue;
			}
			if (!groundRule->fitItemInSlot(item->getRules(), x2, y2))
			{
				continue;
			}
			BattleItem *occupant = unit->getItem(groundRule, x2, y2);
			if (occupant != 0 && occupant != item && companionGroundStackCompatible(occupant, item))
			{
				*outX = occupant->getSlotX();
				*outY = occupant->getSlotY();
				return true;
			}
			if (occupant == 0)
			{
				*outX = x2;
				*outY = y2;
				return true;
			}
		}
	}
	return false;
}

} // namespace OpenXcom
