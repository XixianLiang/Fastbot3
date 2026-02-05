/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef  Graph_CPP_
#define  Graph_CPP_


#include "Graph.h"
#include "../utils.hpp"
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
        
        return state;
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
