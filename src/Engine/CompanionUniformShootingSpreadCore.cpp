/*
 * Open X-Com Companion — OXCE uniform shooting spread core (Meridian 9f0c13c).
 */
#include "CompanionUniformShootingSpreadCore.h"
#include "RNG.h"

namespace OpenXcom
{
namespace CompanionUniformShootingSpreadCore
{

int computeUniformXyShift(int xDist, int yDist)
{
	int xyShift;
	if (xDist <= yDist)
		xyShift = xDist / 4 + yDist;
	else
		xyShift = xDist + yDist / 4;

	xyShift = static_cast<int>(xyShift * kUniformXyShiftCalibration);
	return xyShift;
}

int computeVanillaXyShift(int xDist, int yDist)
{
	if (xDist / 2 <= yDist)
		return xDist / 4 + yDist;
	return (xDist + yDist) / 2;
}

int computeZShift(int xyShift, int zDist)
{
	if (xyShift <= zDist)
		return xyShift / 2 + zDist;
	return xyShift + zDist / 2;
}

void applyCircularXyJitter(int originX, int originY, int xDist, int yDist, int deviation, int &targetX, int &targetY)
{
	const double targetDist2D = sqrt(static_cast<double>(xDist * xDist + yDist * yDist));
	bool resultShifted = false;
	int dX = 0, dY = 0;
	int workingDeviation = deviation;

	for (int i = 0; i < kCircularSpreadMaxAttempts; ++i)
	{
		dX = RNG::generate(0, workingDeviation) - workingDeviation / 2;
		dY = RNG::generate(0, workingDeviation) - workingDeviation / 2;

		const double exprX = targetX + dX - originX;
		const double exprY = targetY + dY - originY;
		const double deviateDist2D = sqrt(exprX * exprX + exprY * exprY);

		if (resultShifted &&
			deviateDist2D > targetDist2D - 2 &&
			deviateDist2D < targetDist2D + 2)
			break;

		if (!resultShifted)
		{
			const int radiusSq = dX * dX + dY * dY;
			const int deviateRadius = static_cast<int>(kOverallSpreadCoeff * workingDeviation / 2);
			const int deviateRadiusSq = deviateRadius * deviateRadius;
			if (radiusSq <= deviateRadiusSq)
				break;

			resultShifted = true;
			workingDeviation = static_cast<int>(workingDeviation * kInnerSpreadCoeff);
		}
	}

	targetX += dX;
	targetY += dY;
}

void applyVanillaSquareXyJitter(int deviation, int &targetX, int &targetY)
{
	targetX += RNG::generate(0, deviation) - deviation / 2;
	targetY += RNG::generate(0, deviation) - deviation / 2;
}

}
}
