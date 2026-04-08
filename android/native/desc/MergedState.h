/**
 * @authors Zhao Zhang, Tianming Liu
 */

#ifndef MergedState_H_
#define MergedState_H_

/**
 * Screen-level merge graph for LLMDroid GPT / navigation path planning.
 * Does not replace Graph for RL. GPT-mutating entry points (overview / reanalysis / listeners) are
 * gated by Preference::isLlmdroidEnabled(); must not write APE StateKey or action RL hashes.
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
 */

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

class MergedStateGraphEdge {
public:
    MergedStatePtr _next;
    ActionPtr _action;
    uintptr_t _hash{};
    bool _isVisited{false};
    bool _shouldStop{false};

    MergedStateGraphEdge(MergedStatePtr next, ActionPtr action, bool shouldStop);

    uintptr_t hash() const { return _hash; }

    bool operator==(const MergedStateGraphEdge &right) const { return this->hash() == right.hash(); }

    bool operator<(const MergedStateGraphEdge &right) const { return this->hash() < right.hash(); }
};

typedef std::shared_ptr<MergedStateGraphEdge> MergedStateGraphEdgePtr;

struct WidgetInfo {
    std::string function;
    ReuseStatePtr state;
    int importance{};
    WidgetPtr widget;
};

struct FunctionDetail {
    int importance{};
    ReuseStatePtr state;
};

class MergedState : public HashNode,
                    public FunctionListener,
                    public std::enable_shared_from_this<MergedState> {
public:
    MergedState(ReuseStatePtr state, int id);

    ReuseStatePtr getRootState() { return _root; }

    int getId() { return _id; }

    /** GPT worker: snapshot of overview (locked). */
    std::string getOverview() const;

    /** GPT worker: copy of function list (locked, same mutex as overview mutations). */
    std::map<std::string, FunctionDetail> getFunctionList() const;

    uintptr_t hash() const override { return _hashcode; }

    /** Monkey / exploration thread. */
    void addPrevious(MergedStatePtr state);

    /** Monkey / exploration thread. */
    void addNext(MergedStatePtr state);

    /** Monkey / exploration thread. */
    void addEdge(MergedStateGraphEdgePtr edge);

    /** Monkey / exploration thread. */
    void addState(ReuseStatePtr state, ActionPtr action, bool fromOutside, bool toOutside);

    /** Monkey / exploration thread. */
    MergedStateGraphEdgePtr getUnvisitedEdge();

    /** GPT worker. */
    std::string stateDescription();

    /** GPT worker (calls reset() with mutex held). */
    std::string walk();

    /** GPT worker; or called from walk() with mutex already held. */
    void reset();

    /** GPT worker: writes _overview / _functionList / widgets. */
    void updateFromStateOverview(nlohmann::json &jsonData);

    /** GPT worker. */
    void updateFromReanalysis(nlohmann::json &jsonResp,
                              std::unordered_map<std::string, std::vector<int>> &uniqueWidgets,
                              std::unordered_map<int, WidgetInfo> &widgetDict);

    /** GPT worker. */
    void updateCompletedFunctions(std::map<std::string, int> completedFunctions);

    /** Monkey / exploration thread (FunctionListener path). */
    void updateCompletedFunction(std::string func);

    std::set<ReuseStatePtr> &getReuseStates() { return _states; }

    int getNavigationValue() const;

    /** GPT worker. */
    void updateNavigationValue(int total);

    /** Monkey / exploration thread (via Action::visit). */
    void onActionExecuted(ActivityStateActionPtr action) override;

    /** GPT worker. */
    void writeOverviewAndTop5Tojson(nlohmann::json &top5, bool ignoreImportance = false);

    /** GPT worker. */
    nlohmann::json toJson();

    /** Monkey / exploration thread (often from addState with mutex held). */
    void updateLaterJoinedState(ReuseStatePtr state);

    /** GPT worker. */
    bool hasUntestedFunctions() const;

    bool needReanalysed();

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
    /// Protects _overview, _functionList, mini/course graph fields, _navigation* , _needReanalysed.
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

class MergedStateGraph {
public:
    explicit MergedStateGraph(GraphPtr graph);

    /** Monkey / exploration thread. */
    void addNode(MergedStatePtr mergedState, ActionPtr action, bool timeup);

    MergedStatePtr getCurrentNode() { return _cursor; }

    MergedStatePtr getLastNode() { return _lastState; }

    MergedStatePtr findMergedStateById(int id);

    /** GPT worker. */
    std::string temporalWalk(int transitCount);

    std::set<MergedStatePtr> &getMergedStates() { return _mergedStates; }

    /** GPT worker. */
    void appendUtgString(std::string value);

    /** GPT worker: returns a copy under lock. */
    std::string getUtgString() const;

    /**
     * Shortest paths on the underlying {@link Graph} reuse-state transition chain.
     * @param reuseStateId target {@link ReuseState} id (not MergedState id).
     */
    std::vector<Path> findPaths(int reuseStateId, bool forceRestart);

private:
    /// Recursive: findPaths may call findMergedStateById while holding this lock.
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
