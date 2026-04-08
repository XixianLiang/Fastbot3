/*
 * Sliding-window growth-rate monitor for LLMDroid stagnation.
 * Ported from LLMDroid-Fastbot native/agent/CodeCoverageMonitor.{h,cpp}.
 */
 /**
 * @authors Zhao Zhang, Tianming Liu
 */
 
#ifndef FASTBOTX_CODE_COVERAGE_MONITOR_H_
#define FASTBOTX_CODE_COVERAGE_MONITOR_H_

#include <utility>
#include <vector>

namespace fastbotx {

class CodeCoverageMonitor {
public:
    explicit CodeCoverageMonitor(int windowSize = 80, double minGrowthRate = 0.05, double adjustmentFactor = 1.0);

    /**
     * @param currentCoverage monotone-ish metric (e.g. graph state count + activity diversity).
     * @return first: per-step growth rate; second: adaptive threshold (minGrowthRate until window full).
     */
    std::pair<double, double> update(double currentCoverage);

private:
    std::vector<double> coverageHistory;
    const int windowSize;
    const double minGrowthRate;
    const double adjustmentFactor;
    const double _minThreshold = 0.01;
    double _adjustedThreshold;
    double _growthRateSum = 0.0;
};

} // namespace fastbotx

#endif
