/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
#ifndef PERF_MONITOR_H_
#define PERF_MONITOR_H_

namespace fastbotx {

/// Background sampler: logs process pid/cpu/rss/vss to logcat tag "FastbotPerf" once per second.
class PerfMonitor {
public:
    static void start();
    static void stop();
};

} // namespace fastbotx

#endif // PERF_MONITOR_H_
