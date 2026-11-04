/*
 * Open X-Com Companion — context-sensitive relay control placement (M1-26A).
 */
#include "CompanionOnDemandRelayLayout.h"
#include <algorithm>

namespace OpenXcom
{

namespace CompanionOnDemandRelayLayout
{

bool compactBarFitsBaseViewport(int viewportWidth)
{
	const int total = COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP + COMPACT_BUTTON_WIDTH;
	return viewportWidth >= total;
}

bool compactBarFitsBattlescapeTop(int screenWidth, int topReservedRightPx)
{
	const int total = COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP + COMPACT_BUTTON_WIDTH;
	const int usable = screenWidth - topReservedRightPx;
	return usable >= total;
}

bool computeBaseViewportBarLayout(int viewportX, int viewportY, int viewportWidth, CompactBarLayout *out)
{
	if (out == 0 || viewportWidth <= 0)
	{
		return false;
	}
	if (!compactBarFitsBaseViewport(viewportWidth))
	{
		return false;
	}
	const int totalWidth = COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP + COMPACT_BUTTON_WIDTH;
	const int barX = viewportX + (viewportWidth - totalWidth) / 2;
	int barY = viewportY - COMPACT_BUTTON_HEIGHT - 1;
	if (barY < 0)
	{
		barY = 0;
	}
	out->buttonWidth = COMPACT_BUTTON_WIDTH;
	out->buttonHeight = COMPACT_BUTTON_HEIGHT;
	out->totalWidth = totalWidth;
	out->sendX = barX;
	out->sendY = barY;
	out->retrieveX = barX + COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP;
	out->retrieveY = barY;
	return true;
}

bool computeBattlescapeTopBarLayout(int screenWidth, CompactBarLayout *out)
{
	if (out == 0 || screenWidth <= 0)
	{
		return false;
	}
	const int topReservedRightPx = 36;
	if (!compactBarFitsBattlescapeTop(screenWidth, topReservedRightPx))
	{
		return false;
	}
	const int totalWidth = COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP + COMPACT_BUTTON_WIDTH;
	const int barX = (screenWidth - totalWidth) / 2;
	out->buttonWidth = COMPACT_BUTTON_WIDTH;
	out->buttonHeight = COMPACT_BUTTON_HEIGHT;
	out->totalWidth = totalWidth;
	out->sendX = barX;
	out->sendY = BATTLESCAPE_TOP_BAR_Y;
	out->retrieveX = barX + COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP;
	out->retrieveY = BATTLESCAPE_TOP_BAR_Y;
	return true;
}

} // namespace CompanionOnDemandRelayLayout

} // namespace OpenXcom
