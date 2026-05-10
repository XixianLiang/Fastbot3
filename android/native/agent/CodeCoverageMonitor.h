/**
 * @authors Zhao Zhang, Tianming Liu
 */

/**
 * @file CodeCoverageMonitor.h
 *
 * Incremental monitor over a scalar coverage signal (instrumentation or heuristic aggregate).
 * After enough samples (`windowSize`), adapts a stagnation threshold from per-step growth vs the
 * historical mean growth rate. Used by LLMDroid alongside the caller's own finite rolling window.
 */

#ifndef FASTBOTX_CODE_COVERAGE_MONITOR_H_
#define FASTBOTX_CODE_COVERAGE_MONITOR_H_

#include <utility>
#include <vector>

namespace fastbotx {

/**
 * Tracks sequential coverage samples and yields an adaptive minimum growth-rate threshold.
 *
 * Each `update` appends one scalar (units are defined by the caller; values should be comparable across steps).
 * Growth rate is `(current - previous) / max(previous, epsilon)`. After at least `windowSize` samples,
 * the returned threshold is adjusted with `exp(adjustmentFactor * (lastGrowth - meanGrowth))` and clamped
 * to `_minThreshold`. Mean growth uses every step observed so far (history is not truncated in memory).
 */
class CodeCoverageMonitor {
public:
    /**
     * @param windowSize Minimum number of `update` calls before adaptive thresholds apply (warm-up length).
     * @param minGrowthRate Threshold returned during warm-up and initial value for `_adjustedThreshold`.
     * @param adjustmentFactor Strength of exponential threshold moves (`exp(factor * delta)`).
     */
    explicit CodeCoverageMonitor(int windowSize = 80, double minGrowthRate = 0.05, double adjustmentFactor = 1.0);

    /**
     * Records `currentCoverage`, updates cumulative growth statistics, and revises the adaptive threshold once warm-up completes.
     *
     * @param currentCoverage Latest coverage scalar from the caller (must be comparable step-to-step).
     * @return `first`: growth rate vs previous sample (`0` until two samples exist).
     *         `second`: threshold — `minGrowthRate` until `windowSize` samples exist; then the adaptive value.
     */
    std::pair<double, double> update(double currentCoverage);

private:
    /** Every coverage sample passed to `update` (grows with run length). */
    std::vector<double> coverageHistory;
    const int windowSize;
    const double minGrowthRate;
    /** Positive factor scaling how aggressively `_adjustedThreshold` reacts to growth deltas. */
    const double adjustmentFactor;
    /** Floor applied after each exponential update so the threshold cannot collapse below this bound. */
    const double _minThreshold = 0.01;
    /** Running adaptive threshold (starts at `minGrowthRate`). */
    double _adjustedThreshold;
    /** Sum of per-step growth rates seen so far (used with `n-1` to build a dynamic baseline). */
    double _growthRateSum = 0.0;
};

} // namespace fastbotx

#endif // FASTBOTX_CODE_COVERAGE_MONITOR_H_
