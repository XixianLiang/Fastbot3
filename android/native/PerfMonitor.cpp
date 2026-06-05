/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
#include "PerfMonitor.h"

#ifdef __ANDROID__

#include <android/log.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace fastbotx {

namespace {

constexpr const char *kPerfLogTag = "FastbotPerf";

static std::atomic<bool> g_running{false};
static std::thread g_worker;

bool readProcessCpuJiffies(unsigned long long &out) {
    std::ifstream stat("/proc/self/stat");
    if (!stat.is_open()) {
        return false;
    }
    std::string line;
    if (!std::getline(stat, line)) {
        return false;
    }
    const size_t rparen = line.rfind(')');
    if (rparen == std::string::npos || rparen + 2 >= line.size()) {
        return false;
    }
    std::istringstream iss(line.substr(rparen + 2));
    std::string skip;
    unsigned long utime = 0;
    unsigned long stime = 0;
    // Fields after comm: state(3) .. utime(14) stime(15)
    for (int field = 3; field < 14; ++field) {
        if (!(iss >> skip)) {
            return false;
        }
    }
    if (!(iss >> utime >> stime)) {
        return false;
    }
    out = static_cast<unsigned long long>(utime) + static_cast<unsigned long long>(stime);
    return true;
}

bool readMemoryKb(long &rssKb, long &vssKb) {
    std::ifstream status("/proc/self/status");
    if (!status.is_open()) {
        return false;
    }
    rssKb = 0;
    vssKb = 0;
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::sscanf(line.c_str() + 6, "%ld", &rssKb);
        } else if (line.compare(0, 7, "VmSize:") == 0) {
            std::sscanf(line.c_str() + 7, "%ld", &vssKb);
        }
    }
    return true;
}

void perfMonitorLoop() {
    const long clkTck = sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 100;
    const int pid = static_cast<int>(getpid());
    bool hasPrev = false;
    unsigned long long prevJiffies = 0;

    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!g_running.load(std::memory_order_relaxed)) {
            break;
        }

        unsigned long long jiffies = 0;
        long rssKb = 0;
        long vssKb = 0;
        const bool cpuOk = readProcessCpuJiffies(jiffies);
        const bool memOk = readMemoryKb(rssKb, vssKb);

        double cpuPct = 0.0;
        if (hasPrev && cpuOk && prevJiffies <= jiffies) {
            const unsigned long long delta = jiffies - prevJiffies;
            cpuPct = 100.0 * static_cast<double>(delta) / static_cast<double>(clkTck);
        }
        if (cpuOk) {
            prevJiffies = jiffies;
            hasPrev = true;
        }

        if (cpuOk || memOk) {
            __android_log_print(ANDROID_LOG_INFO, kPerfLogTag,
                                "pid=%d, cpu=%.1f%%, rss=%ldKB, vss=%ldKB",
                                pid, cpuPct, rssKb, vssKb);
        }
    }
}

} // namespace

void PerfMonitor::start() {
    bool expected = false;
    if (!g_running.compare_exchange_strong(expected, true)) {
        return;
    }
    if (g_worker.joinable()) {
        g_worker.join();
    }
    g_worker = std::thread(perfMonitorLoop);
}

void PerfMonitor::stop() {
    if (!g_running.exchange(false)) {
        return;
    }
    if (g_worker.joinable()) {
        g_worker.join();
    }
}

} // namespace fastbotx

#else

namespace fastbotx {

void PerfMonitor::start() {}
void PerfMonitor::stop() {}

} // namespace fastbotx

#endif // __ANDROID__
