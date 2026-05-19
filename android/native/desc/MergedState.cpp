/**
 * @file MergedState.cpp
 *
 * Implements merged logical screens: aggregates multiple `ReuseState` snapshots, attaches GPT-derived function
 * labels to widgets, tracks completion via `FunctionListener`, and maintains a higher-level `MergedStateGraph`
 * for scripted walks and path lookup. GPT-facing updates run under `_mergedStateMutex`; exploration callbacks
 * must coordinate using the same locks documented in `MergedState.h`.
 *
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 */

#include "MergedState.h"

#include "ReuseState.h"
#include "../events/Preference.h"
#include "../utils.hpp"

#include <algorithm>
#include <exception>
#include <sstream>

namespace fastbotx {

/** Records one transition between merged screens; `_hash` combines action and successor for ordering. */
MergedStateGraphEdge::MergedStateGraphEdge(MergedStatePtr next, ActionPtr action, bool shouldStop) {
    // Cheap identity mix for edge dedup/set ordering (not a cryptographic hash).
    _next = std::move(next);
    _action = std::move(action);
    _hash = (_action ? _action->hash() : 0) | (_next ? _next->hash() : 0);
    _isVisited = false;
    _shouldStop = shouldStop;
}

/** Seeds the cluster with one `ReuseState`, copies its hash into `_hashcode`, and queues `_cursor` as a walk start. */
MergedState::MergedState(ReuseStatePtr state, int id) {
    // Single-screen cluster initially; `_starts` seeds linear walks from entry reuse states.
    _states.insert(state);
    _root = std::move(state);
    _cursor = _root;
    _id = id;
    _starts.push_back(_cursor);
    _hashcode = _root->hash();
}

/** Thread-safe read of the GPT-generated overview paragraph cached on this merged state. */
std::string MergedState::getOverview() const {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    return _overview;
}

/** Returns a snapshot copy of function-name → importance / anchor state (same mutex as overview). */
std::map<std::string, FunctionDetail> MergedState::getFunctionList() const {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    return _functionList;
}

/**
 * Appends a mini-edge from the current cursor and optionally adopts `state` as the new cursor.
 * `toOutside` stops membership growth when the transition leaves this merged state's subgraph.
 */
void MergedState::addState(ReuseStatePtr state, ActionPtr action, bool fromOutside, bool toOutside) {
    // Extends the mini transition graph on `_cursor`; `fromOutside` records alternate entry states.
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);

    if (fromOutside) {
        _starts.push_back(state);
    }

    _cursor->_miniEdges.push_back({state, action, false});

    if (!toOutside) {
        auto success = _states.insert(state);
        _cursor = state;
        if (success.second && !_functionList.empty()) {
            _needReanalysed = true;
            updateLaterJoinedState(state); // propagate function labels from `_root` onto the new snapshot
        }
    }
}

/** Stores a coarse merged-graph edge (parallel timeline) alongside mini-edges on reuse states. */
void MergedState::addEdge(MergedStateGraphEdgePtr edge) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    _edges.push_back(std::move(edge));
}

/** Registers another merged node that can transition into this one (reverse link set). */
void MergedState::addPrevious(MergedStatePtr state) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    _previous.insert(std::move(state));
}

/** Registers a successor merged node reachable from this one. */
void MergedState::addNext(MergedStatePtr state) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    _next.insert(std::move(state));
}

/** Delegates to the root reuse state's textual summary for planner prompts. */
std::string MergedState::stateDescription() {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    return _root->getStateDescriptionForMergedState();
}

/** DFS-style string of one mini-walk per start state; clears visit flags via `reset()` after building text. */
std::string MergedState::walk() {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);

    std::stringstream graphStream;
    for (ReuseStatePtr cursor : _starts) {
        graphStream << "State" << cursor->getIdi();
        MiniGraphEdge *edge = cursor->getUnvisitedMiniEdge();
        while (edge && cursor->getMergedState() && cursor->getMergedState()->getId() == _id) {
            std::string actionstr = edge->action ? edge->action->toDescription() : "null";
            graphStream << " -- " << actionstr << " --> ";
            graphStream << "State" << edge->next->getIdi();
            cursor = edge->next;
            edge->isVisited = true;
            edge = cursor->getUnvisitedMiniEdge();
        }
        graphStream << "\n";
    }
    reset();
    return graphStream.str();
}

/** Clears visit marks on all `_miniEdges` under every aggregated reuse state so `walk()` can rerun. */
void MergedState::reset() {
    for (ReuseStatePtr state : _states) {
        for (MiniGraphEdge &edge : state->_miniEdges) {
            edge.isVisited = false;
        }
    }
}

/**
 * Ingests planner JSON (`Overview`, `Function List`), merges new functions with importance ordering,
 * dedupes keys, refreshes navigation weights, pushes labels to widgets, and re-hooks action listeners.
 */
void MergedState::updateFromStateOverview(nlohmann::json &jsonData) {
    const PreferencePtr pref = Preference::inst();
    if (!pref || !pref->isLlmdroidEnabled()) {
        return; // merged-state / planner payloads disabled
    }

    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);

    std::string overview = jsonData["Overview"];
    std::vector<std::pair<std::string, int>> functionList;
    nlohmann::json jsonFunctionList = jsonData["Function List"];
    for (auto it = jsonFunctionList.begin(); it != jsonFunctionList.end(); ++it) {
        if (it.value().is_number()) {
            functionList.push_back(std::make_pair(it.key(), it.value()));
            BLOG("[Number]%s", it.key().c_str());
        } else if (it.value().is_array()) {
            functionList.push_back(std::make_pair(it.key(), it.value().front()));
            BLOG("[Vector]%s", it.key().c_str());
        } else {
            BLOG("[ErrorType]%s", it.key().c_str());
        }
    }

    _overview = overview;

    int size = static_cast<int>(functionList.size());
    for (int i = 0; i < size; i++) {
        auto it = _functionList.find(functionList[i].first);
        if (it == _functionList.end()) {
            _functionList.insert(std::make_pair(functionList[i].first,
                                                FunctionDetail{size - i, _root}));
        }
    }

    filterFunctionList();

    updateNavigationCount();

    setFunctionToWidget(functionList);
    BLOG("setFunctionToWidget complete!");

    updateCompletedFunctions2();
    BLOG("updateFromStateOverview complete!");
}

/** Bulk-updates importance scores from an external completion map (creates entries when missing). */
void MergedState::updateCompletedFunctions(std::map<std::string, int> completedFunctions) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    for (auto it : completedFunctions) {
        auto found = _functionList.find(it.first);
        if (found != _functionList.end()) {
            found->second.importance = it.second;
        } else {
            _functionList.insert(std::make_pair(it.first, FunctionDetail{it.second, _root}));
        }
    }
}

/** Linear scan of `_edges` for temporal walks (`MergedStateGraph::temporalWalk`). */
MergedStateGraphEdgePtr MergedState::getUnvisitedEdge() {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    for (const auto &edge : _edges) {
        if (!edge->_isVisited) {
            return edge;
        }
    }
    return nullptr;
}

/** Marks one function as satisfied (importance 0) or seeds it with zero importance if newly named. */
void MergedState::updateCompletedFunction(std::string func) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    auto it = _functionList.find(func);
    if (it == _functionList.end()) {
        _functionList.insert(std::make_pair(func, FunctionDetail{0, _root}));
    } else {
        it->second.importance = 0;
    }
}

/** Cached scalar combining navigation-function count and distance from the newest merged id. */
int MergedState::getNavigationValue() const {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    return _navigationValue;
}

/** Recomputes `_navigationValue` from planner horizon `total` and internal `_navigationCount`. */
void MergedState::updateNavigationValue(int total) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    int weight = total - _id;
    _navigationValue = weight * _navigationCount; // scales "navigate-*" richness by distance from newest merged id
}

/** Removes undecorated duplicate keys when a `**name**` pair exists in `_functionList`. */
void MergedState::filterFunctionList() {
    // Drop bare keys when a `**key**` decorated variant exists (duplicate labeling from the overview parser).
    std::set<std::string> keyToDelete;

    for (const auto &pair : _functionList) {
        size_t startPos = pair.first.find("**");
        if (startPos != std::string::npos) {
            size_t endPos = pair.first.rfind("**");
            if (endPos != std::string::npos && endPos > startPos) {
                std::string strippedKey = pair.first.substr(startPos + 2, endPos - startPos - 2);
                if (_functionList.find(strippedKey) != _functionList.end()) {
                    keyToDelete.insert(strippedKey);
                }
            }
        }
    }

    for (const auto &k : keyToDelete) {
        _functionList.erase(k);
    }
}

/** Counts function entries whose name contains "navigate" to feed `updateNavigationValue`. */
void MergedState::updateNavigationCount() {
    // Heuristic: count GPT functions whose name mentions "navigate" for weighting `updateNavigationValue`.
    int navigateFunctionNum = 0;
    for (const auto &it : _functionList) {
        if (it.first.find("navigate") != std::string::npos) {
            navigateFunctionNum++;
        }
    }
    _navigationCount = navigateFunctionNum;
}

/** Attaches `this` as `FunctionListener` on every action under all member states and syncs completion flags. */
void MergedState::updateCompletedFunctions2() {
    const PreferencePtr pref = Preference::inst();
    if (!pref || !pref->isLlmdroidEnabled()) {
        return; // listener wiring only needed when merged-state tooling is on
    }
    for (ReuseStatePtr state : _states) {
        for (ActivityStateActionPtr action : state->getActions()) {
            action->setListener(shared_from_this());
            updateCompletedFunction2(0, action);
        }
    }
}

/** Maps planner element ids to widgets on `_root`, then copies labels onto matching widgets in sibling reuse states. */
void MergedState::setFunctionToWidget(const std::vector<std::pair<std::string, int>> &functionList) {
    for (size_t i = 0; i < functionList.size(); i++) {
        if (_functionList.find(functionList[i].first) == _functionList.end()) {
            continue;
        }
        ElementPtr element = _root->findElementById(functionList[i].second);
        if (!element) {
            BLOG("can't find element%d for function%s", functionList[i].second, functionList[i].first.c_str());
            continue;
        }
        WidgetPtr widget = _root->getWidgetForElement(element);
        if (!widget) {
            BLOG("can't map element%d to widget for function%s", functionList[i].second,
                 functionList[i].first.c_str());
            continue;
        }
        std::string function = functionList[i].first;
        widget->setFunctionLabel(function);
        BLOG("successfully set function: %s to root's widget", function.c_str());
        auto anchorIt = _functionList.find(function);
        if (anchorIt != _functionList.end()) {
            anchorIt->second.state = _root;
        }

        int whichWidget = _root->findWhichWidget(widget);
        if (whichWidget < -1) {
            BLOG("[setFunctionToWidget] can't find widget in root%d", _root->getIdi());
            continue;
        }

        for (ReuseStatePtr state : _states) {
            if (state == _root) {
                continue;
            }
            WidgetPtr similarWidget = state->findWidgetByHashAndLocation(widget->hash(), whichWidget);
            if (similarWidget) {
                similarWidget->setFunctionLabel(function);
                BLOG("successfully set function: %s to R%d's widget", function.c_str(), state->getIdi());
                if (anchorIt != _functionList.end()) {
                    anchorIt->second.state = state;
                }
            } else {
                BLOG("widget:%s doesn't have similar one in R%d", widget->toHTML().c_str(), state->getIdi());
            }
        }
    }
}

/** `FunctionListener` entry: forwards to `updateCompletedFunction2` after each executed action. */
void MergedState::onActionExecuted(ActivityStateActionPtr action) {
    updateCompletedFunction2(0, action); // invoked from `ActivityStateAction::visit` after visit count updates
}

/** When an action targeting a labeled widget runs, drop that function's importance to zero (treated as covered). */
void MergedState::updateCompletedFunction2(int /*caller*/, ActivityStateActionPtr action) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    if (action->getVisitedCount() > 0) {
        WidgetPtr widget = action->getTarget();
        if (!widget) {
            return;
        }
        std::string function = widget->getFunctionLabel();
        if (!function.empty()) {
            auto it = _functionList.find(function);
            if (it != _functionList.end()) {
                it->second.importance = 0;
                BLOG("Function:%s is tested by perform: %s", function.c_str(),
                     action->toDescription().c_str());
            }
        }
    }
}

/** Embeds overview text plus up to five highest-importance function names into `top5["State<id>"]`. */
void MergedState::writeOverviewAndTop5Tojson(nlohmann::json &top5, bool ignoreImportance) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    std::string key = "State" + std::to_string(_id);
    top5[key]["Overview"] = _overview;
    auto sortedFunctions = sortFunctionsByValue(ignoreImportance);
    if (sortedFunctions.size() > 5) {
        sortedFunctions.resize(5);
    }
    top5[key]["FunctionList"] = sortedFunctions;
}

/** Minimal JSON export: overview string plus ordered function names (no importance values). */
nlohmann::json MergedState::toJson() {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    nlohmann::json data;
    data["Overview"] = _overview;
    std::vector<std::string> functions;
    for (auto &it : _functionList) {
        functions.push_back(it.first);
    }
    data["Function List"] = functions;
    return data;
}

/** Sorts functions by descending importance; skips zero-importance rows unless `ignoreImportance` is true. */
std::vector<std::string> MergedState::sortFunctionsByValue(bool ignoreImportance) {
    std::vector<std::pair<int, std::string>> pairs;

    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    for (const auto &kvp : _functionList) {
        if (kvp.second.importance > 0 || ignoreImportance) {
            pairs.push_back(std::make_pair(kvp.second.importance, kvp.first));
        }
    }

    std::sort(pairs.begin(), pairs.end(),
              [](const std::pair<int, std::string> &a, const std::pair<int, std::string> &b) {
                  return a.first > b.first;
              });

    std::vector<std::string> sortedKeys;
    for (const auto &pair : pairs) {
        sortedKeys.push_back(pair.second);
    }

    return sortedKeys;
}

/** Propagates widget function labels from `_root` onto a newly joined `state` and registers listeners there. */
void MergedState::updateLaterJoinedState(ReuseStatePtr state) {
    const PreferencePtr pref = Preference::inst();
    if (!pref || !pref->isLlmdroidEnabled()) {
        return;
    }
    for (WidgetPtr rootWidget : _root->getAllWidgets()) {
        std::string function = rootWidget->getFunctionLabel();
        if (function.empty()) {
            continue;
        }
        int whichWidget = _root->findWhichWidget(rootWidget);
        if (whichWidget < -1) {
            BLOG("[updateLaterJoinedState] can't find widget in root%d", _root->getIdi());
            continue;
        }
        WidgetPtr similarWidget = state->findWidgetByHashAndLocation(rootWidget->hash(), whichWidget);
        if (similarWidget) {
            similarWidget->setFunctionLabel(function);
            BLOG("successfully set function: %s to widget: %s", function.c_str(),
                 similarWidget->toHTML().c_str());
        } else {
            BLOG("widget:%s doesn't have similar one in R%d", rootWidget->toHTML().c_str(), state->getIdi());
        }
    }

    for (ActivityStateActionPtr action : state->getActions()) {
        action->setListener(shared_from_this());
    }
}

/** True while at least one tracked function still has positive importance (not marked exercised). */
bool MergedState::hasUntestedFunctions() const {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    bool flag = false;
    for (const auto &it : _functionList) {
        if (it.second.importance > 0) {
            flag = true;
            break;
        }
    }
    return flag;
}

/** Applies a secondary labeling pass keyed by widget id string → function name; clears `_needReanalysed` on success. */
void MergedState::updateFromReanalysis(nlohmann::json &jsonResp,
                                        std::unordered_map<std::string, std::vector<int>> &uniqueWidgets,
                                        std::unordered_map<int, WidgetInfo> &widgetDict) {
    const PreferencePtr pref = Preference::inst();
    if (!pref || !pref->isLlmdroidEnabled()) {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);

    // Keys are numeric widget ids as strings; values are canonical function labels from re-analysis.
    for (auto &it : jsonResp.items()) {
        try {
            int id = std::stoi(it.key());
            std::string function = it.value();
            if (_functionList.find(function) == _functionList.end()) {
                _functionList.insert(std::make_pair(function, FunctionDetail{1, widgetDict.at(id).state}));
            }
            std::vector<int> widgetIds = uniqueWidgets[widgetDict.at(id).widget->toHTML({}, false, 0)];
            for (int widgetId : widgetIds) {
                WidgetPtr widget = widgetDict[widgetId].widget;
                widget->setFunctionLabel(function);

                for (auto &action : widgetDict[widgetId].state->findActionsByWidget(widget)) {
                    updateCompletedFunction2(0, action);
                }
            }
        } catch (const std::exception &e) {
            BLOG("[Exception]: %s, skip this kv-pair", e.what());
        }
    }
    _needReanalysed = false;
}

/** True after a late-joined state triggers relabeling; cleared when `updateFromReanalysis` finishes. */
bool MergedState::needReanalysed() {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    return _needReanalysed;
}

/** Owns the timeline of merged logical screens plus an RL `Graph` used for concrete replay paths. */
MergedStateGraph::MergedStateGraph(GraphPtr graph) {
    // `_graph` supplies concrete RL paths; merged nodes layer GPT/temporal navigation on top.
    BLOG("MergedStateGraph init");
    _root = nullptr;
    _cursor = nullptr;
    _gptCursor = nullptr;
    _lastState = nullptr;
    _graph = std::move(graph);
}

/** Appends the temporal chain `_root → … → _cursor`; `timeup` marks edges where exploration hit a time budget. */
void MergedStateGraph::addNode(MergedStatePtr mergedState, ActionPtr action, bool timeup) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);
    if (mergedState == _cursor) {
        return;
    }

    _mergedStates.insert(mergedState);
    if (_root == nullptr) {
        _root = _cursor = mergedState;
        _gptCursor = mergedState;
        BLOG("***first add node: MergedState%d", _gptCursor->getId());
    } else {
        _cursor->addNext(mergedState);
        mergedState->addPrevious(_cursor);
        _cursor->addEdge(std::make_shared<MergedStateGraphEdge>(mergedState, action, timeup));
        BLOG("add edge from MergedState%d to MergedState%d timeup %d", _cursor->getId(), mergedState->getId(),
             timeup);
        _lastState = _cursor;
        _cursor = mergedState;
    }
}

/** Looks up a merged state in the set gathered by `addNode`; logs on miss. */
MergedStatePtr MergedStateGraph::findMergedStateById(int id) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);

    auto it = std::find_if(_mergedStates.begin(), _mergedStates.end(),
                           [id](const MergedStatePtr &state) { return id == state->getId(); });
    if (it != _mergedStates.end()) {
        return *it;
    }
    BLOG("[THREAD] findMergedStateById: can't find id %d", id);
    return nullptr;
}

/** Greedy walk from `_gptCursor` along unvisited merged edges, up to `transitCount` hops (for UTG / logs). */
std::string MergedStateGraph::temporalWalk(int transitCount) {
    if (transitCount == 0) {
        return "No transition during this period.";
    }
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);
    std::string ret;
    ret.append("State").append(std::to_string(_gptCursor->getId()));

    BLOG("[THREAD] temporal walk begin %d", transitCount);
    MergedStateGraphEdgePtr edge;
    int count = 0;
    do {
        edge = _gptCursor->getUnvisitedEdge();

        if (edge) {
            int nid = edge->_next->getId();
            ret.append(" -- ").append(edge->_action->toDescription());
            ret.append(" --> State");
            ret.append(std::to_string(nid));

            edge->_isVisited = true;
            _gptCursor = edge->_next;
            ++count;
        }
    } while (edge && (count < transitCount));

    ret.append("\n");
    return ret;
}

/** Accumulates optional UTG / walk transcript lines (newline-terminated) for external reporting. */
void MergedStateGraph::appendUtgString(std::string value) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);
    _utgString.append(std::move(value)).append("\n");
}

/** Returns the concatenated UTG transcript built via `appendUtgString`. */
std::string MergedStateGraph::getUtgString() const {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);
    return _utgString;
}

/** Delegates to underlying RL `Graph` for concrete replay paths into `reuseStateId`. */
std::vector<Path> MergedStateGraph::findPaths(int reuseStateId, bool forceRestart) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);
    if (!_graph) {
        return {};
    }
    return _graph->findPath(reuseStateId, forceRestart);
}

/** Resolves which reuse snapshot anchored a function label (for replay targeting). */
ReuseStatePtr MergedState::getTargetState(const std::string &function) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    auto it = _functionList.find(function);
    if (it == _functionList.end()) {
        BLOG("function{%s} doesn't belong to any state in MergedState{%d}", function.c_str(), _id);
        return nullptr;
    }
    for (const ReuseStatePtr &state : _states) {
        if (!state) {
            continue;
        }
        for (const WidgetPtr &widget : state->getAllWidgets()) {
            if (widget && widget->getFunctionLabel() == function) {
                return state;
            }
        }
    }
    if (it->second.state) {
        return it->second.state;
    }
    return _root;
}

} // namespace fastbotx
