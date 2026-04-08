/*
 * RL navigation path step (LLMDroid Graph / AbstractAgent).
 */
/**
 * @authors Zhao Zhang
 */

#ifndef FASTBOTX_GRAPH_PATH_H_
#define FASTBOTX_GRAPH_PATH_H_

#include "../desc/Action.h"

#include <cstddef>
#include <queue>
#include <string>

namespace fastbotx {

struct Step {
    int node{};
    ActionPtr action;
    double time{};
};

struct Path {
    size_t length{};
    double time{};
    std::queue<Step> steps;
};

std::string pathToString(const Path &path);

} // namespace fastbotx

#endif
