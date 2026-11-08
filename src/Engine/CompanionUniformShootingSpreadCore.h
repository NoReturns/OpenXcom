#pragma once
/*
 * Open X-Com Companion — OXCE uniform shooting spread (Meridian 9f0c13c lineage).
 * Companion enables uniform spread by default for human and AI combatants.
 */
#include <cmath>

namespace OpenXcom
{
namespace CompanionUniformShootingSpreadCore
{

/** OXCE calibration: average xyShift scale ≈ vanilla aggregate (Meridian upstream). */
constexpr double kUniformXyShiftCalibration = 0.839;
constexpr double kOverallSpreadCoeff = 1.0;
/** Meridian SECONDARY_SPREAD_COEFF / INNER_SPREAD_COEFF for circle reject retries. */
constexpr double kInnerSpreadCoeff = 0.85;
constexpr int kCircularSpreadMaxAttempts = 10;

/** Symmetric xyShift (OXCE uniform branch). */
int computeUniformXyShift(int xDist, int yDist);

/** Vanilla OpenXcom "Commandment" xyShift (regression reference only). */
int computeVanillaXyShift(int xDist, int yDist);

/** Shared zShift from horizontal and vertical separation (unchanged vanilla). */
int computeZShift(int xyShift, int zDist);

/**
 * Circular XY jitter with bounded reject/resample (OXCE uniform spread).
 * Mutates targetX/targetY in place; uses engine RNG.
 */
void applyCircularXyJitter(int originX, int originY, int xDist, int yDist, int deviation, int &targetX, int &targetY);

/** Vanilla independent square XY jitter (regression reference only). */
void applyVanillaSquareXyJitter(int deviation, int &targetX, int &targetY);

}
}
