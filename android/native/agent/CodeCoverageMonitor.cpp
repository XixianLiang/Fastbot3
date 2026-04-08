/**
 * @authors Zhao Zhang, Tianming Liu
 */

#include "CodeCoverageMonitor.h"

#include "../utils.hpp"

#include <cmath>

namespace fastbotx {

CodeCoverageMonitor::CodeCoverageMonitor(int windowSize, double minGrowthRate, double adjustmentFactor)
    : windowSize(windowSize), minGrowthRate(minGrowthRate), adjustmentFactor(adjustmentFactor) {
    _adjustedThreshold = minGrowthRate;
}

std::pair<double, double> CodeCoverageMonitor::update(double currentCoverage) {
    coverageHistory.push_back(currentCoverage);
    const int n = static_cast<int>(coverageHistory.size());
    double currentGrowthRate = 0.0;
    if (n >= 2) {
        const double prev = coverageHistory[static_cast<size_t>(n - 2)];
        const double denom = (prev > 1e-12) ? prev : 1e-12;
        currentGrowthRate = (currentCoverage - prev) / denom;
        _growthRateSum += currentGrowthRate;
        BLOG("[CV_Monitor] growth rate: %f, growthRateSum: %f", currentGrowthRate, _growthRateSum);
    }

    if (n >= windowSize) {
        const double dynamicBaselineGrowthRate = _growthRateSum / static_cast<double>(n - 1);
        const double growthRateDifference = currentGrowthRate - dynamicBaselineGrowthRate;
        BLOG("[CV_Monitor] dynamic baseline: %f, delta_g: %f", dynamicBaselineGrowthRate, growthRateDifference);

        double adjustedThreshold = _adjustedThreshold * std::exp(adjustmentFactor * growthRateDifference);
        _adjustedThreshold = adjustedThreshold > _minThreshold ? adjustedThreshold : _minThreshold;
        return std::make_pair(currentGrowthRate, _adjustedThreshold);
    }

    return std::make_pair(currentGrowthRate, minGrowthRate);
}

} // namespace fastbotx
