/**
 * @authors Zhao Zhang
 *
 * @file GraphPath.cpp
 * @brief Implements `pathToString` formatting for debugging and logs.
 */

#include "GraphPath.h"

#include <sstream>

namespace fastbotx {

std::string pathToString(const Path &path) {
    std::stringstream ss;
    Path p = path;
    while (!p.steps.empty()) {
        Step s = p.steps.front();
        p.steps.pop();
        ss << "State" << s.node;
        ss << "-- ";
        if (s.action) {
            ss << s.action->toDescription();
        }
        ss << " -->";
    }
    return ss.str();
}

} // namespace fastbotx
