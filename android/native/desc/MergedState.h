/**
 * Screen-level merge graph for LLMDroid GPT / navigation path planning.
 * Does not replace Graph for RL. GPT-mutating entry points (overview / reanalysis / listeners) are
 * gated by Preference::isLlmdroidEnabled();
 *
 * Threading:
 * - **Monkey / exploration thread** (LLMDroid "MAIN_THREAD"): `MergedStateGraph::addNode`, `findPaths`,
 *   `MergedState::addState`, `addPrevious`/`addNext`/`addEdge`, `getUnvisitedEdge`, `onActionExecuted`
 *   (via `ActivityStateAction::visit` after an action is chosen), `updateCompletedFunction`,
 *   `updateLaterJoinedState`.
 * - **GPT worker** (A: Java `ExecutorService` / future native worker): `getOverview`,
 *   `getFunctionList`, `updateFromStateOverview`, `updateFromReanalysis`, `updateCompletedFunctions`,
 *   `stateDescription`, `walk`, `reset`, `writeOverviewAndTop5Tojson`, `toJson`, `hasUntestedFunctions`,
 *   `updateNavigationValue`, `sortFunctionsByValue` path, `MergedStateGraph::temporalWalk`,
 *   `appendUtgString`, `getUtgString`.
 * Phase 4 should **serialize** one in-flight GPT apply with graph updates or rely on locks below.
 * `MergedState::_mergedStateMutex` is **recursive** so `walk`→`reset` and overview→`sortFunctionsByValue` nest safely.
 
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 */

#ifndef MergedState_H_
#define MergedState_H_

#include "Action.h"
#include "../model/GraphPath.h"
#include "Graph.h"
#include "json.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fastbotx {

class ReuseState;
typedef std::shared_ptr<ReuseState> ReuseStatePtr;
class MergedState;
typedef std::shared_ptr<MergedState> MergedStatePtr;
typedef std::vector<MergedStatePtr> MergedStateVec;
typedef std::shared_ptr<MergedStateVec> MergedStateVecPtr;

/** Directed edge in the merged-state timeline (`MergedStateGraph`): successor, label action, visit flags. */
class MergedStateGraphEdge {
public:
    MergedStatePtr _next;
    ActionPtr _action;
    uintptr_t _hash{};
    bool _isVisited{false};
    /** When true, exploration stopped at a time/resource budget on this transition. */
    bool _shouldStop{false};

    MergedStateGraphEdge(MergedStatePtr next, ActionPtr action, bool shouldStop);

    uintptr_t hash() const { return _hash; }

    bool operator==(const MergedStateGraphEdge &right) const { return this->hash() == right.hash(); }

    bool operator<(const MergedStateGraphEdge &right) const { return this->hash() < right.hash(); }
};

typedef std::shared_ptr<MergedStateGraphEdge> MergedStateGraphEdgePtr;

/** Associates a widget record with a named function during reanalysis (`updateFromReanalysis`). */
struct WidgetInfo {
    std::string function;
    ReuseStatePtr state;
    int importance{};
    WidgetPtr widget;
};

/** Planner prioritization: remaining importance score and which reuse state anchored the label. */
struct FunctionDetail {
    int importance{};
    ReuseStatePtr state;
};

/**
 * One merged logical screen: set of related `ReuseState` rows, mini transition edges on those rows,
 * and planner bookkeeping (`_overview`, `_functionList`).
 */
class MergedState : public HashNode,
                    public FunctionListener,
                    public std::enable_shared_from_this<MergedState> {
public:
    MergedState(ReuseStatePtr state, int id);

    ReuseStatePtr getRootState() { return _root; }

    int getId() { return _id; }

    /** Thread-safe copy of the overview paragraph (same mutex as `_functionList`). */
    std::string getOverview() const;

    /** Thread-safe snapshot map function name → detail (importance + anchor state). */
    std::map<std::string, FunctionDetail> getFunctionList() const;

    uintptr_t hash() const override { return _hashcode; }

    /** Reverse link in the merged-state DAG (`MergedStateGraph`). */
    void addPrevious(MergedStatePtr state);

    /** Forward link in the merged-state DAG. */
    void addNext(MergedStatePtr state);

    /** Stores one coarse graph hop (`MergedStateGraph` timeline). */
    void addEdge(MergedStateGraphEdgePtr edge);

    /** Extends intra-screen mini-graph and membership; see implementation for `fromOutside` / `toOutside`. */
    void addState(ReuseStatePtr state, ActionPtr action, bool fromOutside, bool toOutside);

    /** Next unvisited edge along `_edges` for temporal walks. */
    MergedStateGraphEdgePtr getUnvisitedEdge();

    /** Human-readable summary from the root reuse state (planner prompts). */
    std::string stateDescription();

    /** Renders mini-graph lines from `_starts`; resets visit bits when done. */
    std::string walk();

    /** Clears `_miniEdges` visit marks across all aggregated reuse states. */
    void reset();

    /** Applies planner JSON: overview text, function list with element ids, widget labeling, listeners. */
    void updateFromStateOverview(nlohmann::json &jsonData);

    /** Secondary labeling pass keyed by widget id strings from replanning output. */
    void updateFromReanalysis(nlohmann::json &jsonResp,
                              std::unordered_map<std::string, std::vector<int>> &uniqueWidgets,
                              std::unordered_map<int, WidgetInfo> &widgetDict);

    /** Bulk-importance update from an external completion map. */
    void updateCompletedFunctions(std::map<std::string, int> completedFunctions);

    /** Marks a single function as exercised (importance forced to zero). */
    void updateCompletedFunction(std::string func);

    std::set<ReuseStatePtr> &getReuseStates() { return _states; }

    int getNavigationValue() const;

    /** Recomputes navigation heuristic from global planner horizon `total`. */
    void updateNavigationValue(int total);

    /** `FunctionListener`: marks matching function importance zero after an executed action. */
    void onActionExecuted(ActivityStateActionPtr action) override;

    /** Writes `top5["State<id>"]` with overview plus top function keys. */
    void writeOverviewAndTop5Tojson(nlohmann::json &top5, bool ignoreImportance = false);

    /** Minimal JSON dump for persistence/debug. */
    nlohmann::json toJson();

    /** Propagates widget function labels when new reuse states join after the first overview pass. */
    void updateLaterJoinedState(ReuseStatePtr state);

    /** True while some tracked function still has positive importance. */
    bool hasUntestedFunctions() const;

    /** Pending relabel after late join; cleared when `updateFromReanalysis` succeeds. */
    bool needReanalysed();

    /** Anchor reuse state recorded for a named function (replay targeting). */
    ReuseStatePtr getTargetState(const std::string &function);

private:
    void updateNavigationCount();

    void updateCompletedFunctions2();

    void updateCompletedFunction2(int /*caller*/, ActivityStateActionPtr action);

    void setFunctionToWidget(const std::vector<std::pair<std::string, int>> &functionList);

    void filterFunctionList();

    std::vector<std::string> sortFunctionsByValue(bool ignoreImportance);

    int _id{};
    std::set<std::string> _subtasks;
    std::set<ReuseStatePtr> _states;
    ReuseStatePtr _root;
    uintptr_t _hashcode{};

    std::vector<ReuseStatePtr> _starts;
    ReuseStatePtr _cursor;
    /** Guards overview text, function map, merged navigation fields, and mini-graph mutations on this object. */
    mutable std::recursive_mutex _mergedStateMutex;

    std::set<MergedStatePtr> _previous;
    std::set<MergedStatePtr> _next;
    std::vector<MergedStateGraphEdgePtr> _edges;

    std::string _overview;
    std::map<std::string, FunctionDetail> _functionList;

    int _navigationValue = 0;
    int _navigationCount = 0;

    bool _needReanalysed = false;
};

/**
 * Outer timeline of merged screens plus optional RL path queries into underlying reuse-state ids.
 */
class MergedStateGraph {
public:
    explicit MergedStateGraph(GraphPtr graph);

    /** Appends the next merged node along exploration (`timeup` marks budget-limited hops). */
    void addNode(MergedStatePtr mergedState, ActionPtr action, bool timeup);

    MergedStatePtr getCurrentNode() { return _cursor; }

    MergedStatePtr getLastNode() { return _lastState; }

    MergedStatePtr findMergedStateById(int id);

    /** Format-only walk along unvisited merged edges for logging / UTG strings. */
    std::string temporalWalk(int transitCount);

    std::set<MergedStatePtr> &getMergedStates() { return _mergedStates; }

    void appendUtgString(std::string value);

    /** Returns accumulated transcript (mutex held during copy). */
    std::string getUtgString() const;

    /**
     * Shortest paths on the underlying `Graph` transition model (reuse-state ids, not merged-state ids).
     *
     * @param reuseStateId Target `ReuseState::getIdi()` value.
     * @param forceRestart Passed through to `Graph::findPath`.
     */
    std::vector<Path> findPaths(int reuseStateId, bool forceRestart);

private:
    /** Guards timeline mutators and readers; recursive because `findPaths` may consult merged ids internally. */
    mutable std::recursive_mutex _mergedStateGraphMutex;

    MergedStatePtr _root;
    MergedStatePtr _cursor;
    MergedStatePtr _gptCursor;
    MergedStatePtr _lastState;

    std::set<MergedStatePtr> _mergedStates;

    std::set<std::string> _allSubtasks;
    std::set<std::string> _performedSubtask;

    std::set<std::string> getPerformedSubtask() { return _performedSubtask; }

    std::string _utgString;

    GraphPtr _graph;
};

typedef std::shared_ptr<MergedStateGraph> MergedStateGraphPtr;

} // namespace fastbotx

#endif
