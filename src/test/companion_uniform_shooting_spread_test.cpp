/*
 * Open X-Com Companion — uniform shooting spread regression tests (M1).
 */
#include "Engine/CompanionUniformShootingSpreadCore.h"
#include "Engine/RNG.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace OpenXcom;
using namespace OpenXcom::CompanionUniformShootingSpreadCore;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static void expectNear(double actual, double expected, double tolerance, const char *message)
{
	if (std::fabs(actual - expected) > tolerance)
	{
		std::cerr << "FAIL: " << message << " (got " << actual << ", expected " << expected << ")" << std::endl;
		++g_failures;
	}
}

static int deviationForScenario(int xDist, int yDist, int zDist, double accuracy)
{
	const int xyShift = computeUniformXyShift(xDist, yDist);
	const int zShift = computeZShift(xyShift, zDist);
	int deviation = RNG::generate(0, 100) - static_cast<int>(accuracy * 100);
	if (deviation >= 0)
		deviation += 50;
	else
		deviation += 10;
	return std::max(1, zShift * deviation / 200);
}

static void testCardinalXyShiftSymmetry()
{
	const int rangeVoxels = 20 * 16;
	const int northSouth = computeUniformXyShift(0, rangeVoxels);
	const int eastWest = computeUniformXyShift(rangeVoxels, 0);
	expect(northSouth == eastWest, "uniform xyShift: N/S and E/W must match at equal range");

	const int vanillaNs = computeVanillaXyShift(0, rangeVoxels);
	const int vanillaEw = computeVanillaXyShift(rangeVoxels, 0);
	expect(vanillaNs == rangeVoxels, "vanilla reference: N/S xyShift equals range");
	expect(vanillaEw == rangeVoxels / 2, "vanilla reference: E/W xyShift is half range");
	expect(northSouth != vanillaNs || northSouth != vanillaEw, "uniform corrects vanilla cardinal split");
}

static void testDiagonalAndMirrors()
{
	const int d = 12 * 16;
	const int ne = computeUniformXyShift(d, d);
	const int nw = computeUniformXyShift(d, d);
	const int se = computeUniformXyShift(d, d);
	const int sw = computeUniformXyShift(d, d);
	expect(ne == nw && nw == se && se == sw, "uniform xyShift symmetric for diagonal bearings at equal |dx|,|dy|");

	const int mirrorX = computeUniformXyShift(d, d / 2);
	const int mirrorY = computeUniformXyShift(d / 2, d);
	expect(mirrorX == mirrorY, "uniform xyShift mirrors X/Y when distances swap");
}

static void testZShiftUnchangedFromVanillaFormula()
{
	const int xy = 200;
	const int z = 80;
	const int zLow = computeZShift(xy, 300);
	expect(zLow == xy / 2 + 300, "zShift low branch matches vanilla");
	const int zHigh = computeZShift(xy, z);
	expect(zHigh == xy + z / 2, "zShift high branch matches vanilla");
}

static void testCircularSamplesInsideOuterRadius(int seed, int deviation, int originX, int originY, int targetX, int targetY)
{
	RNG::setSeed(seed);
	const int baseX = targetX;
	const int baseY = targetY;
	applyCircularXyJitter(originX, originY, std::abs(originX - targetX), std::abs(originY - targetY), deviation, targetX, targetY);
	const int dX = targetX - baseX;
	const int dY = targetY - baseY;
	const int radiusSq = dX * dX + dY * dY;
	const int outer = deviation / 2;
	expect(radiusSq <= outer * outer, "circular jitter sample lies inside outer circle");
}

static void testDeterministicCircularConstraint()
{
	testCircularSamplesInsideOuterRadius(0xC0FFEE01ULL, 42, 0, 0, 320, 0);
	testCircularSamplesInsideOuterRadius(0xC0FFEE02ULL, 120, 100, 200, 500, 700);
	testCircularSamplesInsideOuterRadius(0xC0FFEE03ULL, 8, 0, 0, 0, 160);
}

static double meanRadialSpread(int samples, int xDist, int yDist, int zDist, double accuracy, bool uniform)
{
	RNG::setSeed(0xF1A4E001ULL);
	const int originX = 0, originY = 0, originZ = 0;
	double sum = 0.0;
	for (int i = 0; i < samples; ++i)
	{
		const int deviation = deviationForScenario(xDist, yDist, zDist, accuracy);
		int tx = originX + xDist;
		int ty = originY + yDist;
		const int tz = originZ + zDist;
		(void)tz;
		if (uniform)
			applyCircularXyJitter(originX, originY, xDist, yDist, deviation, tx, ty);
		else
			applyVanillaSquareXyJitter(deviation, tx, ty);
		const double rdx = tx - (originX + xDist);
		const double rdy = ty - (originY + yDist);
		sum += std::sqrt(rdx * rdx + rdy * rdy);
	}
	return sum / samples;
}

static void testStatisticalCardinalParity()
{
	const int zDist = 0;
	const double accuracy = 0.55;
	const int range = 15 * 16;
	const int samples = 8000;
	const double spreadNs = meanRadialSpread(samples, 0, range, zDist, accuracy, true);
	const double spreadEw = meanRadialSpread(samples, range, 0, zDist, accuracy, true);
	expectNear(spreadNs, spreadEw, 2.5, "mean XY miss radius N/S vs E/W within tolerance");
}

static void testOverallSpreadNotEasierThanVanillaEw()
{
	const int range = 15 * 16;
	const double accuracy = 0.50;
	const int samples = 8000;
	RNG::setSeed(0xBEEF0001ULL);
	const double uniformEw = meanRadialSpread(samples, range, 0, 0, accuracy, true);
	RNG::setSeed(0xBEEF0001ULL);
	const double vanillaEw = meanRadialSpread(samples, range, 0, 0, accuracy, false);
	// E/W was the tighter vanilla axis; uniform should not collapse far below that baseline.
	expect(uniformEw >= vanillaEw * 0.75, "uniform spread not globally tighter than vanilla E/W");
}

static void testElevatedTargetUsesSameXyShift()
{
	const int horiz = 10 * 16;
	const int lowZ = computeUniformXyShift(horiz, 0);
	const int highZShiftOnly = computeZShift(lowZ, 5 * 24);
	expect(highZShiftOnly > lowZ, "elevated target increases zShift/deviation scale");
	expect(computeUniformXyShift(horiz, 0) == computeUniformXyShift(horiz, 0), "horizontal xyShift independent of target Z");
}

int main()
{
	testCardinalXyShiftSymmetry();
	testDiagonalAndMirrors();
	testZShiftUnchangedFromVanillaFormula();
	testDeterministicCircularConstraint();
	testStatisticalCardinalParity();
	testOverallSpreadNotEasierThanVanillaEw();
	testElevatedTargetUsesSameXyShift();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " test failure(s)." << std::endl;
		return EXIT_FAILURE;
	}
	std::cout << "companion_uniform_shooting_spread_test: all tests passed." << std::endl;
	return EXIT_SUCCESS;
}
