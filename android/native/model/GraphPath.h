/*
 * Navigation path segments for graph shortest-path and replay helpers.
 */
/**
 * @authors Zhao Zhang
 *
 * @file GraphPath.h
 * @brief Lightweight path representation: a sequence of state ids, actions, and timestamps.
 */

#ifndef FASTBOTX_GRAPH_PATH_H_
#define FASTBOTX_GRAPH_PATH_H_

#include "../desc/Action.h"

#include <cstddef>
#include <queue>
#include <string>

namespace fastbotx {

/** One hop along a path: source state id, action taken, and edge metadata time (e.g. creation time). */
struct Step {
    int node{};
    ActionPtr action;
    double time{};
};

/** Ordered queue of steps plus aggregate length and latest time (used by `Graph::dijkstra` / path utilities). */
struct Path {
    size_t length{};
    double time{};
    std::queue<Step> steps;
};

/** Human-readable linear summary: `State<id>-- <action> -->` segments without the final destination id. */
std::string pathToString(const Path &path);

} // namespace fastbotx

#endif
