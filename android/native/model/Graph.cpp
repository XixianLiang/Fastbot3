/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang, Tianming Liu
 */
#ifndef  Graph_CPP_
#define  Graph_CPP_


#include "Graph.h"
#include "../desc/reuse/ReuseState.h"
#include "../events/Preference.h"
#include "../utils.hpp"
#include <algorithm>
#include <deque>
#include <limits>
#include <queue>
#include <sstream>
#include <unordered_set>
#include <vector>


namespace fastbotx {


    /**
     * @brief Constructor for Graph class
     * Initializes the graph with zero distribution count and zero timestamp
     */
    Graph::Graph()
            : _totalDistri(0), _timeStamp(0) {

    }

    /**
     * @brief Default distribution pair for activity statistics
     * First value: count of how many times the state is visited
     * Second value: percentage of times that this state been accessed over all states
     */
    const std::pair<int, double> Graph::_defaultDistri = std::make_pair(0, 0.0);

    /**
     * @brief Add a state to the graph, or return existing state if already present
     * 
     * This method performs the following operations:
     * 1. Checks if the state already exists in the graph (by hash comparison)
     * 2. If new: assigns an ID and adds to the state set
     * 3. If existing: fills details if the existing state has no details
     * 4. Notifies all listeners about the new/existing state
     * 5. Updates activity statistics (visit count and percentage)
     * 6. Processes and indexes all actions from this state
     * 
     * @param state The state to add to the graph
     * @return StatePtr The state that was added or the existing matching state
     * 
     * @note Performance: O(log n) for state lookup, O(m log k) for action processing
     *       where n is number of states, m is number of actions, k is number of unique actions
     */
    StatePtr Graph::addState(StatePtr state) {
        // Get the activity name (activity class name) of this new state
        auto activity = state->getActivityString();
        static const std::string kEmptyActivityStr;
        const std::string& activityStrForCount = (activity && activity.get()) ? *activity : kEmptyActivityStr;

        // Try to find state in state cache using hash-based comparison
        auto ifStateExists = this->_states.find(state);
        
        if (ifStateExists == this->_states.end()) {
            // This is a brand-new state, add it to the state cache
            state->setId(static_cast<int>(this->_states.size()));
            this->_states.emplace(state);
            this->_activityStateCount[activityStrForCount]++;
        } else {
            // State already exists, fill details if needed
            if ((*ifStateExists)->hasNoDetail()) {
                (*ifStateExists)->fillDetails(state);
            }
            // Use the existing state instead of the new one
            state = *ifStateExists;
        }

        // Notify all registered listeners about the new/existing state
        this->notifyNewStateEvents(state);

        // Add this activity name to the visited activities set (every name is unique)
        if (activity && activity.get()) {
            this->_visitedActivities.emplace(activity);
        }

        // Update total distribution count
        this->_totalDistri++;
        
        // Update activity distribution statistics
        // Performance optimization: avoid creating string copy, use reference to shared_ptr's string
        const std::string& activityStr = activityStrForCount;
        auto distriIt = this->_activityDistri.find(activityStr);
        
        if (distriIt == this->_activityDistri.end()) {
            // First time seeing this activity, initialize with default values
            this->_activityDistri[activityStr] = _defaultDistri;
            distriIt = this->_activityDistri.find(activityStr);
        }
        
        // Update visit count and percentage
        distriIt->second.first++;
        distriIt->second.second = 1.0 * distriIt->second.first / this->_totalDistri;
        
        // Process and index all actions from this state
        addActionFromState(state);

        buildStateGraph(std::dynamic_pointer_cast<ReuseState>(state));

        return state;
    }

    void Graph::recordStateVisit(StatePtr canonical, StatePtr freshlyBuilt) {
        if (!canonical) {
            return;
        }
        if (freshlyBuilt && canonical->hasNoDetail() && !freshlyBuilt->hasNoDetail()) {
            canonical->fillDetails(freshlyBuilt);
        }
        auto activity = canonical->getActivityString();
        static const std::string kEmptyActivityStr;
        const std::string &activityStrForCount = (activity && activity.get()) ? *activity : kEmptyActivityStr;

        this->notifyNewStateEvents(canonical);

        if (activity && activity.get()) {
            this->_visitedActivities.emplace(activity);
        }

        this->_totalDistri++;

        const std::string &activityStr = activityStrForCount;
        auto distriIt = this->_activityDistri.find(activityStr);

        if (distriIt == this->_activityDistri.end()) {
            this->_activityDistri[activityStr] = _defaultDistri;
            distriIt = this->_activityDistri.find(activityStr);
        }

        distriIt->second.first++;
        distriIt->second.second = 1.0 * distriIt->second.first / this->_totalDistri;

        addActionFromState(canonical);

        buildStateGraph(std::dynamic_pointer_cast<ReuseState>(canonical));
    }

    size_t Graph::removeStatesByHash(const std::unordered_set<uintptr_t> &stateHashes) {
        if (stateHashes.empty()) {
            return 0;
        }
        size_t removed = 0;
        for (auto it = this->_states.begin(); it != this->_states.end();) {
            const StatePtr &s = *it;
            if (!s || stateHashes.count(s->hash()) == 0) {
                ++it;
                continue;
            }
            for (const auto &a : s->getActions()) {
                this->_visitedActions.erase(a);
                this->_unvisitedActions.erase(a);
            }
            auto activity = s->getActivityString();
            static const std::string kEmptyActivityStr;
            const std::string &activityStrForCount =
                (activity && activity.get()) ? *activity : kEmptyActivityStr;
            auto itCnt = this->_activityStateCount.find(activityStrForCount);
            if (itCnt != this->_activityStateCount.end() && itCnt->second > 0) {
                itCnt->second--;
                if (itCnt->second == 0) {
                    this->_activityStateCount.erase(itCnt);
                }
            }
            it = this->_states.erase(it);
            ++removed;
        }
        return removed;
    }

    /**
     * @brief Notify all registered listeners about a new state being added
     * 
     * @param node The state node that was added to the graph
     */
    void Graph::notifyNewStateEvents(const StatePtr &node) {
        for (const auto &listener: this->_listeners) {
            listener->onAddNode(node);
        }
    }

    /**
     * @brief Add a listener to be notified when new states are added to the graph
     * 
     * @param listener The listener to register
     */
    void Graph::addListener(const GraphListenerPtr &listener) {
        this->_listeners.emplace_back(listener);
    }

    size_t Graph::getStateCountByActivity(const std::string &activity) const {
        auto it = this->_activityStateCount.find(activity);
        return it != this->_activityStateCount.end() ? it->second : 0;
    }

    /**
     * @brief Process and index all actions from a state
     * 
     * This method performs the following operations for each action in the state:
     * 1. Checks if the action already exists in visited actions set
     * 2. If not visited, checks if it exists in unvisited actions set
     * 3. If new action, assigns a new ID and updates action counter
     * 4. Updates the appropriate set (visited/unvisited) based on action status
     * 
     * Performance optimization:
     * - Checks visited set first (typically smaller and more frequently accessed)
     * - Only checks unvisited set if action is not found in visited set
     * - Uses hash-based set lookup for O(log n) complexity
     * 
     * @param node The state node containing actions to process
     * 
     * @note Time complexity: O(m log k) where m is number of actions, k is number of unique actions
     */
    void Graph::addActionFromState(const StatePtr &node) {
        auto nodeActions = node->getActions();
        
        for (const auto &action: nodeActions) {
            // Performance optimization: check visited set first (typically smaller)
            auto itervisted = this->_visitedActions.find(action);
            if (itervisted != this->_visitedActions.end()) {
                // Action already exists in visited set, reuse its ID
                action->setId((*itervisted)->getIdi());
                // No need to check unvisited set
            } else {
                // Action not in visited set, check unvisited set
                auto iterunvisited = this->_unvisitedActions.find(action);
                if (iterunvisited != this->_unvisitedActions.end()) {
                    // Action exists in unvisited set, reuse its ID
                    action->setId((*iterunvisited)->getIdi());
                } else {
                    // New action, not in either set
                    // Assign new ID based on total action count
                    action->setId(static_cast<int>(this->_actionCounter.getTotal()));
                    // Update action counter statistics
                    this->_actionCounter.countAction(action);
                }
                
                // Update sets based on visited status
                // Move action to appropriate set if its visited status changed
                if (action->isVisited()) {
                    this->_visitedActions.emplace(action);
                    // Remove from unvisited if it was there (shouldn't happen, but safe)
                    this->_unvisitedActions.erase(action);
                } else {
                    this->_unvisitedActions.emplace(action);
                }
            }
        }
        
        BDLOG("unvisited action: %zu, visited action %zu", this->_unvisitedActions.size(),
              this->_visitedActions.size());
    }

    void Graph::buildStateGraph(const ReuseStatePtr &reuseState) {
        const PreferencePtr pref = Preference::inst();
        if (!pref || !pref->isLlmdroidEnabled() || !reuseState) {
            return;
        }
        if (!_llmdroidFirstState) {
            _llmdroidFirstState = reuseState;
            _llmdroidCurrentState = reuseState;
            _llmdroidCursor = _llmdroidFirstState;
            return;
        }
        _llmdroidCurrentState->addSubSequentState(reuseState);
        _llmdroidCurrentState = reuseState;
    }

    ReuseStatePtr Graph::findReuseStateById(int id) {
        auto result = std::find_if(_states.begin(), _states.end(), [id](const StatePtr &s) {
            return s && s->getIdi() == id;
        });
        if (result != _states.end()) {
            return std::dynamic_pointer_cast<ReuseState>(*result);
        }
        BLOG("Graph::findReuseStateById: no state id=%d", id);
        return nullptr;
    }

    void Graph::processPaths(std::vector<Path> &paths, int source, int dest) {
        std::sort(paths.begin(), paths.end(), [](const Path &a, const Path &b) { return a.length < b.length; });
        if (paths.size() >= 2) {
            std::sort(paths.begin() + 1, paths.end(), [](const Path &a, const Path &b) { return a.time > b.time; });
        }
        if (paths.size() > 3) {
            paths.resize(3);
        }
        for (size_t i = 0; i < paths.size(); i++) {
            BLOG("[GRAPH] PATH %zu time %f length %zu: %s", i, paths[i].time, paths[i].length,
                 pathToString(paths[i]).c_str());
            paths[i] = transformPath(std::move(paths[i]), source, dest);
        }
    }

    Path Graph::transformPath(Path origin, int source, int dest) {
        Path res;
        std::stringstream ss;
        const int curId = _llmdroidCurrentState ? _llmdroidCurrentState->getIdi() : -1;
        ss << "State" << curId;
        if (curId != 0 && source == 0) {
            res.steps.push(Step{0, Action::RESTART, 0.0});
            ss << "-- RESTART -->State0";
        } else if (curId == 0 && source == 0) {
            res.steps.push(Step{0, Action::NOP, 0.0});
            ss << "-- NOP -->State0";
        }

        while (!origin.steps.empty()) {
            Step step = origin.steps.front();
            origin.steps.pop();
            if (step.action && step.action->getActionType() == ActionType::RESTART) {
                while (!res.steps.empty()) {
                    res.steps.pop();
                }
                ss.str("");
                ss << "State" << curId;
            }
            step.node = (!origin.steps.empty()) ? origin.steps.front().node : dest;
            res.steps.push(step);
            ss << "-- " << (step.action ? step.action->toDescription() : "") << " -->State" << step.node;
        }

        BLOG("[GRAPH] transformed path\n%s", ss.str().c_str());
        res.length = res.steps.size();
        res.time = origin.time;
        return res;
    }

    std::vector<std::vector<Step>> Graph::traceback(std::vector<bool> &is_used,
                                                    std::vector<std::vector<Step>> &parent,
                                                    int source,
                                                    int dest,
                                                    int layer) {
        if (source == dest) {
            return std::vector<std::vector<Step>>(1);
        }
        if (layer > 10) {
            return std::vector<std::vector<Step>>();
        }
        if (static_cast<size_t>(dest) >= is_used.size()) {
            return std::vector<std::vector<Step>>();
        }
        if (is_used[static_cast<size_t>(dest)]) {
            return std::vector<std::vector<Step>>();
        }
        is_used[static_cast<size_t>(dest)] = true;
        std::vector<std::vector<Step>> out;
        const std::vector<Step> &precursors = parent[static_cast<size_t>(dest)];
        for (const Step &precursor : precursors) {
            std::vector<std::vector<Step>> sub = traceback(is_used, parent, source, precursor.node, layer + 1);
            for (auto &current_path : sub) {
                current_path.push_back(precursor);
                out.push_back(std::move(current_path));
            }
        }
        is_used[static_cast<size_t>(dest)] = false;
        return out;
    }

    std::vector<Path> Graph::dijkstra(int source, int dest) {
        const int stateNum = static_cast<int>(_states.size());
        if (stateNum <= 0 || source < 0 || dest < 0 || source >= stateNum || dest >= stateNum) {
            return {};
        }
        std::vector<int> dist(static_cast<size_t>(stateNum), std::numeric_limits<int>::max());
        std::vector<std::vector<Step>> parent(static_cast<size_t>(stateNum));
        dist[static_cast<size_t>(source)] = 0;
        using P = std::pair<int, int>;
        std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
        pq.push({0, source});
        BLOG("[GRAPH] Dijkstra source=%d dest=%d", source, dest);

        while (!pq.empty()) {
            const int u = pq.top().second;
            pq.pop();
            ReuseStatePtr u_state = findReuseStateById(u);
            if (!u_state) {
                continue;
            }
            for (const StateGraphEdge &v_edge : u_state->getEdges()) {
                if (!v_edge.nextState) {
                    continue;
                }
                const int v = v_edge.nextState->getIdi();
                if (v < 0 || v >= stateNum) {
                    continue;
                }
                ActivityStateActionPtr tmp = std::dynamic_pointer_cast<ActivityStateAction>(v_edge.action);
                ActivityStateActionPtr action_copy =
                    tmp ? std::make_shared<ActivityStateAction>(*tmp) : nullptr;
                if (tmp && action_copy && tmp->getTarget()) {
                    const int currentWidget = tmp->getWhichWidget();
                    if (v_edge.whichWidget != currentWidget) {
                        WidgetPtr realTarget =
                            u_state->findWidgetByHashAndLocation(tmp->getTarget()->hash(), v_edge.whichWidget);
                        if (realTarget) {
                            action_copy->setWhichWidget(v_edge.whichWidget);
                            action_copy->setTarget(realTarget);
                        }
                    }
                }
                bool loose = false;
                if (dist[static_cast<size_t>(u)] + 1 < dist[static_cast<size_t>(v)]) {
                    dist[static_cast<size_t>(v)] = dist[static_cast<size_t>(u)] + 1;
                    pq.push({dist[static_cast<size_t>(v)], v});
                    loose = true;
                }
                if (u != v) {
                    auto &pv = parent[static_cast<size_t>(v)];
                    auto found = std::find_if(pv.begin(), pv.end(),
                                              [u](const Step &s) { return s.node == u; });
                    if (found == pv.end()) {
                        if (loose) {
                            if (action_copy) {
                                pv.insert(pv.begin(), Step{u, action_copy, v_edge.createdTime});
                            } else {
                                pv.insert(pv.begin(), Step{u, v_edge.action, v_edge.createdTime});
                            }
                        } else {
                            if (action_copy) {
                                pv.push_back(Step{u, action_copy, v_edge.createdTime});
                            } else {
                                pv.push_back(Step{u, v_edge.action, v_edge.createdTime});
                            }
                        }
                    } else if (loose) {
                        std::rotate(pv.begin(), found, found + 1);
                    }
                }
            }
        }

        std::vector<bool> is_used(static_cast<size_t>(stateNum), false);
        std::vector<std::vector<Step>> all_paths = traceback(is_used, parent, source, dest, 0);
        std::vector<Path> ret;
        int num = 0;
        for (const auto &path : all_paths) {
            if (path.empty()) {
                continue;
            }
            auto latest = std::max_element(path.begin(), path.end(),
                                         [](const Step &a, const Step &b) { return a.time < b.time; });
            double latest_time = 0.0;
            if (latest != path.end()) {
                latest_time = latest->time;
            }
            ret.push_back(Path{path.size(), latest_time,
                               std::queue<Step>(std::deque<Step>(path.begin(), path.end()))});
            if (++num >= 100) {
                break;
            }
        }
        return ret;
    }

    std::vector<Path> Graph::findPath(int dest, bool forceRestart) {
        ReuseStatePtr destination = findReuseStateById(dest);
        if (!destination || !_llmdroidCurrentState) {
            return {};
        }
        const int source_id = _llmdroidCurrentState->getIdi();
        BLOG("[GRAPH] findPath from ReuseState%d to ReuseState%d (forceRestart=%d)", source_id, dest,
             forceRestart ? 1 : 0);

        if (!forceRestart) {
            std::vector<Path> forwardPath = dijkstra(source_id, dest);
            if (!forwardPath.empty()) {
                processPaths(forwardPath, source_id, dest);
                return forwardPath;
            }
        } else {
            BLOG("[GRAPH] findPath from R0 to ReuseState%d", dest);
            std::vector<Path> originPath = dijkstra(0, dest);
            if (!originPath.empty()) {
                processPaths(originPath, 0, dest);
                return originPath;
            }
        }
        BLOG("[GRAPH] no path found to ReuseState%d", dest);
        return {};
    }

    /**
     * @brief Destructor for Graph class
     * Clears all internal data structures to free memory
     */
    Graph::~Graph() {
        this->_states.clear();
        this->_unvisitedActions.clear();
        this->_visitedActions.clear();
        this->_widgetActions.clear();
        this->_listeners.clear();
        this->_activityStateCount.clear();
    }

}

#endif //Graph_CPP_
