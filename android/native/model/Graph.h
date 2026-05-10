/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 *
 * @file Graph.h
 * @brief State–action graph: hashed state store, action indexing, transition notifications, optional naming
 *        fingerprint index (dynamic abstraction), and optional activity-level export when enabled in preferences.
 */
#ifndef  Graph_H_
#define  Graph_H_

#include "State.h"
#include "Base.h"
#include "Action.h"
#include "GraphPath.h"
#include <map>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fastbotx {

    /** Classifies a transition for listeners: new (action, first target) vs another target vs duplicate. */
    enum class GraphTransitionVisitKind {
        NewAction,
        NewActionTarget,
        Existing,
    };

    class Activity;
    typedef std::shared_ptr<Activity> ActivityPtr;

    class ReuseState;
    typedef std::shared_ptr<ReuseState> ReuseStatePtr;

    /**
     * @brief Map from widget to set of actions that can be performed on that widget
     * Used for quick lookup of available actions for a specific widget
     */
    typedef std::map<WidgetPtr, ActivityStateActionPtrSet, Comparator<Widget>> ModelActionPtrWidgetMap;
    
    /**
     * @brief Map from activity name string to set of states in that activity
     * Used for organizing states by their activity context
     */
    typedef std::map<std::string, StatePtrSet> StatePtrStrMap;

    /**
     * @brief Counter for tracking action statistics by action type
     * 
     * Maintains counts for each action type and total action count.
     * Used for generating unique action IDs and tracking action distribution.
     */
    struct ActionCounter {
    private:
        /// Array storing count for each action type (indexed by ActionType enum)
        long actCount[ActionType::ActTypeSize];
        
        /// Total count of all actions processed
        long total;

    public:
        /**
         * @brief Constructor initializes all counters to zero
         */
        ActionCounter()
                : actCount{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, total(0) {
        }

        /**
         * @brief Increment counter for a specific action type
         * 
         * @param action The action to count
         */
        void countAction(const ActivityStateActionPtr &action) {
            actCount[action->getActionType()]++;
            total++;
        }

        /**
         * @brief Get total count of all actions processed
         * 
         * @return Total action count
         */
        long getTotal() const { return total; }
    };

    /**
     * @brief Interface for objects that want to be notified when new states are added to the graph
     * 
     * Implement this interface to receive notifications about state additions.
     * Used by agents to update their internal state when the graph changes.
     */
    class GraphListener {
    public:
        virtual ~GraphListener() = default;
        /**
         * @brief Called when a new state node is added to the graph
         * 
         * @param node The state node that was added
         */
        virtual void onAddNode(StatePtr node) = 0;
        virtual void onVisitStateTransition(const StatePtr &fromState,
                                            const ActivityStateActionPtr &action,
                                            const StatePtr &toState,
                                            GraphTransitionVisitKind visitKind =
                                                GraphTransitionVisitKind::Existing) {
            (void)fromState;
            (void)action;
            (void)toState;
            (void)visitKind;
        }
    };

    /// Smart pointer type for GraphListener
    typedef std::shared_ptr<GraphListener> GraphListenerPtr;
    
    /// Vector of graph listeners
    typedef std::vector<GraphListenerPtr> GraphListenerPtrVec;

    /**
     * @brief Graph class representing the state-action graph for reinforcement learning
     * 
     * The Graph maintains:
     * - All visited states in the application
     * - All actions (visited and unvisited) that can be performed
     * - Activity distribution statistics
     * - Listeners that need to be notified of state changes
     * 
     * States are stored in a set and deduplicated by hash. Actions are indexed
     * by their visited status for efficient lookup during action selection.
     */
    class Graph : Node {
    public:
        /**
         * @brief Constructor initializes the graph with empty state
         */
        Graph();

        /**
         * @brief Get the number of unique states in the graph
         * 
         * @return Number of states
         */
        inline size_t stateSize() const { return this->_states.size(); }

        /**
         * @brief Get the set of all states in the graph (read-only).
         * Used by agents that need the full state set (e.g. curiosity long-horizon novelty).
         * @return Const reference to the state set
         */
        const StatePtrSet &getStates() const { return this->_states; }

        /**
         * @brief Get the current timestamp of the graph
         * 
         * @return Current timestamp
         */
        time_t getTimestamp() const { return this->_timeStamp; }
        const std::string &getStructureId() const { return _structureId; }

        /** Updates structural id after a naming-epoch change and clears per-edge target novelty caches. */
        void syncApeStructuralEpoch(uint64_t epoch);

        /** For model actions with a widget target: notifies listeners and classifies repeat vs new destination. */
        void notifyVisitStateTransition(const StatePtr &fromState,
                                        const ActivityStateActionPtr &action,
                                        const StatePtr &toState);

        /**
         * @brief Add a listener to be notified when new states are added
         * 
         * @param listener The listener to register
         */
        void addListener(const GraphListenerPtr &listener);

        /**
         * @brief Add a state to the graph, or return existing state if already present
         * 
         * If the state already exists (by hash), returns the existing state.
         * Otherwise, adds the new state and updates all related statistics.
         * 
         * @param state The state to add
         * @return The state that was added or the existing matching state
         */
        StatePtr addState(StatePtr state);

        /**
         * Record a revisit to an existing graph state (same as addState tail when the state already
         * exists by widget hash): notifications, distribution, addActionFromState. Used when graph
         * identity is merged by an external key instead of widget hash alone.
         */
        void recordStateVisit(StatePtr canonical, StatePtr freshlyBuilt);

        /**
         * Remove states (and their actions) by state hash.
         * Used by dynamic state abstraction to drop stale states after Naming changes.
         * Returns the number of removed states.
         */
        size_t removeStatesByHash(const std::unordered_set<uintptr_t> &stateHashes);

        /**
         * Shortest-hop paths over `ReuseState` edges (`ReuseState::addSubSequentState`). The underlying reuse-state
         * chain is populated from `addState` / `recordStateVisit` when `Preference::isLlmdroidEnabled()` enables
         * activity-graph bookkeeping.
         */
        std::vector<Path> findPath(int dest, bool forceRestart);

        ReuseStatePtr findReuseStateById(int id);

        /** Reuse state used as the path-finding origin when the optional activity-graph chain is active. */
        ReuseStatePtr getLlmdroidGraphCursorState() const { return _llmdroidCurrentState; }

        /**
         * Activity-level graph text for debugging (e.g. Mermaid-style flow fragments). Does not affect exploration
         * scoring.
         */
        std::string generateGraphCodeForActivity();

        /** Per-activity node labels for `generateGraphCodeForActivity()` (brief widget summaries). */
        std::string generateNodeCodeForActivity();

        /**
         * @brief Get total distribution count (total number of state accesses)
         * 
         * @return Total distribution count
         */
        long getTotalDistri() const { return this->_totalDistri; }

        /**
         * @brief Get set of all visited activity names
         * 
         * @return Const reference to set of visited activity string pointers
         */
        const stringPtrSet& getVisitedActivities() const { return this->_visitedActivities; };

        /**
         * @brief Get number of states that belong to the given activity.
         * Used for dynamic state abstraction (coarsening threshold).
         */
        size_t getStateCountByActivity(const std::string &activity) const;

#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        /**
         * Maintain states grouped by naming fingerprint (StateKey string). Upsert when a state is assigned a
         * fingerprint; remove when the state is evicted from the graph.
         */
        void apeNamingIndexUpsert(const StatePtr &state, const std::string &namingFingerprint);
        void apeNamingIndexRemoveState(const StatePtr &state);
        /** Collects deduplicated states whose fingerprints appear in `fingerprints`. */
        void apeCollectStatesByNamingFingerprints(const std::unordered_set<std::string> &fingerprints,
                                                  std::vector<StatePtr> *out) const;
#endif

        /**
         * @brief Destructor clears all internal data structures
         */
        virtual ~Graph();

    protected:
        /**
         * @brief Notify all registered listeners about a new state
         * 
         * @param node The state node that was added
         */
        void notifyNewStateEvents(const StatePtr &node);

        /** Extends the optional reuse-state / activity chain used for path finding and graph export. */
        void buildStateGraph(const ReuseStatePtr &reuseState);

    private:
        Path transformPath(Path origin, int source, int dest);

        void processPaths(std::vector<Path> &paths, int source, int dest);

        std::vector<Path> dijkstra(int source, int dest);

        std::vector<std::vector<Step>> traceback(std::vector<bool> &is_used,
                                                   std::vector<std::vector<Step>> &parent,
                                                   int source,
                                                   int dest,
                                                   int layer);

        /// Head of the reuse-state chain when activity-graph mode is enabled (see `Preference::isLlmdroidEnabled()`).
        ReuseStatePtr _llmdroidFirstState;
        /// Latest reuse state in the linear exploration chain (path queries start here unless restarted).
        ReuseStatePtr _llmdroidCurrentState;
        /// Cursor into the reuse-state list for incremental graph walks.
        ReuseStatePtr _llmdroidCursor;
        ActivityPtr _firstActivity;
        ActivityPtr _currentActivity;
        std::map<std::string, ActivityPtr> _activityMap;

        /**
         * @brief Process and index all actions from a state
         * 
         * Updates action sets (visited/unvisited) and assigns IDs to new actions.
         * 
         * @param node The state node containing actions to process
         */
        void addActionFromState(const StatePtr &node);

        /** Links or updates `Activity` nodes along the exploration edge that entered `state`. */
        void buildActivityGraph(const ReuseStatePtr &state, const ActionPtr &edgeAction);

        /// Set of all unique states in the graph (deduplicated by hash)
        StatePtrSet _states;
        
        /// Set of all visited activity names (shared pointers to strings for memory efficiency)
        stringPtrSet _visitedActivities;
        
        /// Map from activity name to (visit_count, percentage) pair
        /// Used for tracking activity distribution statistics
        std::map<std::string, std::pair<int, double>> _activityDistri;
        
        /// Total count of state accesses (new states + revisits)
        /// Used for calculating activity visit percentages
        long _totalDistri;
        
        /// Map from widget to set of actions that can be performed on that widget
        /// Used for quick lookup of available actions for a specific widget
        ModelActionPtrWidgetMap _widgetActions;

        /// Set of actions that have not been visited yet
        ActivityStateActionPtrSet _unvisitedActions;
        
        /// Set of actions that have been visited at least once
        ActivityStateActionPtrSet _visitedActions;

        /// Counter for tracking action statistics by type
        ActionCounter _actionCounter;
        
        /// List of listeners to notify when new states are added
        GraphListenerPtrVec _listeners;
        
        /// Current timestamp of the graph (updated when states are added)
        time_t _timeStamp;

        std::string _structureId{"g0"};
        /// Mixed (source, action) key → destination state hashes already seen (transition novelty for listeners).
        std::unordered_map<uint64_t, std::unordered_set<uintptr_t>> _apeSrcActionToSeenTargets;

        /// Per-activity count of unique states (updated only when a new state is added)
        /// Used for dynamic state abstraction (coarsening threshold) - O(1) lookup
        std::unordered_map<std::string, size_t> _activityStateCount;

#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        /// Naming fingerprint → states currently indexed under that key.
        std::unordered_map<std::string, std::unordered_set<StatePtr>> _apeStatesByNamingFingerprint;
        /// State → naming fingerprint (inverse index for upsert/remove).
        std::unordered_map<StatePtr, std::string> _apeStateToNamingFingerprint;
#endif

        /// Default distribution pair (0, 0.0) used for initializing new activities
        const static std::pair<int, double> _defaultDistri;
    };

    typedef std::shared_ptr<Graph> GraphPtr;

}

#endif  // Graph_H_
