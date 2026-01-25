/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */

#ifndef AbstractAgent_CPP_
#define AbstractAgent_CPP_

#include "AbstractAgent.h"

#include <utility>
#include "../model/Model.h"

namespace fastbotx {

    /**
     * @brief Default constructor
     * 
     * Initializes all member variables to default values:
     * - Uses validDatePriorityFilter as default filter
     * - All counters initialized to 0
     * - Boolean flags initialized to false
     * - Algorithm type defaults to Random
     */
    AbstractAgent::AbstractAgent()
            : _validateFilter(validDatePriorityFilter), _graphStableCounter(0),
              _stateStableCounter(0), _activityStableCounter(0), _disableFuzz(false),
              _requestRestart(false), _currentStateBlockTimes(0),
              _algorithmType(AlgorithmType::Random) {

    }

    /**
     * @brief Constructor with model parameter
     * 
     * First calls default constructor for initialization, then sets model pointer.
     * 
     * @param model Model smart pointer
     */
    AbstractAgent::AbstractAgent(const ModelPtr &model)
            : AbstractAgent() {
        this->_model = model;
    }

    /**
     * @brief Destructor
     * 
     * Explicitly resets all smart pointers to ensure proper resource cleanup.
     * Note: Smart pointers automatically manage memory, explicit reset here is for code clarity.
     */
    AbstractAgent::~AbstractAgent() {
        this->_model.reset();
        this->_lastState.reset();
        this->_currentState.reset();
        this->_newState.reset();
        this->_lastAction.reset();
        this->_currentAction.reset();
        this->_newAction.reset();
        this->_validateFilter.reset();
    }

    /**
     * @brief Callback when a new node is added to the state graph
     * 
     * Implements GraphListener interface. Called when Graph adds a new state.
     * 
     * Functionality:
     * 1. Updates _newState to the newly added node
     * 2. If state blocking detection is enabled (BLOCK_STATE_TIME_RESTART != -1),
     *    detects if same state is reached consecutively, increments block counter if so
     * 
     * @param node Newly added state node
     */
    void AbstractAgent::onAddNode(StatePtr node) {
        _newState = node;

        // If state blocking detection is enabled, check if stuck in loop
        if(BLOCK_STATE_TIME_RESTART != -1)
        {
            if (equals(_newState, _currentState)) {
                // Consecutively reached same state, increment block count
                this->_currentStateBlockTimes++;
            } else {
                // Reached new state, reset block count
                this->_currentStateBlockTimes = 0;
            }
        }
    }

    /**
     * @brief Move forward in state machine
     * 
     * Updates state and action history, implementing state machine state transition.
     * 
     * State update flow:
     * - _lastState = _currentState (save previous state)
     * - _currentState = _newState (current state updated to new state)
     * - _newState = nextState (new state updated to next state)
     * 
     * Action update flow:
     * - _lastAction = _currentAction (save previous action)
     * - _currentAction = _newAction (current action updated to newly selected action)
     * - _newAction = nullptr (clear new action, wait for next selection)
     * 
     * @param nextState Next state, uses move semantics to avoid unnecessary copies
     */
    void AbstractAgent::moveForward(StatePtr nextState) {
        // Update state history
        _lastState = _currentState;
        _currentState = _newState;
        _newState = std::move(nextState);  // Use move to avoid copy
        
        // Update action history
        _lastAction = _currentAction;
        _currentAction = _newAction;
        _newAction = nullptr;  // Clear new action, wait for next selection
    }

    /**
     * @brief Adjust action priorities
     * 
     * Dynamically adjusts priority of each action based on visit status, type,
     * saturation status, etc.
     * 
     * Priority adjustment rules:
     * 
     * 1. Base priority: Get base priority from action type
     * 
     * 2. No-target actions:
     *    - If unvisited, add NoTargetUnvisitedBonus (5)
     *    - Skip subsequent processing
     * 
     * 3. Target actions:
     *    - If invalid, skip (no priority adjustment)
     *    - If unvisited, add UnvisitedActionBonus (20)
     *    - If new action (state not saturated), add NewActionMultiplier (5) * base priority
     *    - Ensure priority is not less than 0
     * 
     * 4. Calculate total state priority:
     *    - Accumulate (adjusted priority - base priority) for all actions
     *    - Set state priority to total priority
     * 
     * Performance optimization:
     * - Uses references to avoid unnecessary copies
     * - Early continue to skip invalid actions
     * 
     * @note Time complexity: O(n), where n is the number of actions in the state
     */
    void AbstractAgent::adjustActions() {
        using namespace ActionPriorityConstants;
        
        // Accumulate priority increments for all actions (for calculating total state priority)
        double totalPriority = 0;
        
        // Iterate through all actions in state and adjust priorities
        for (const ActivityStateActionPtr &action: _newState->getActions()) {
            // Get and set base priority
            int basePriority = action->getPriorityByActionType();
            action->setPriority(basePriority);
            
            // Handle no-target actions (e.g., BACK, FEED system actions)
            if (!action->requireTarget()) {
                if (!action->isVisited()) {
                    // Unvisited no-target action, add bonus
                    int priority = action->getPriority();
                    priority += NoTargetUnvisitedBonus;
                    action->setPriority(priority);
                }
                continue;  // No-target action processing complete, skip subsequent logic
            }
            
            // Target actions must be valid
            if (!action->isValid()) {
                continue;  // Skip invalid actions
            }
            
            // Calculate priority for target actions
            int priority = action->getPriority();
            
            // Unvisited actions get bonus, encouraging exploration
            if (!action->isVisited()) {
                priority += UnvisitedActionBonus;
            }
            
            // If new action (state not saturated), significantly increase priority
            if (!this->_newState->isSaturated(action)) {
                priority += NewActionMultiplier * action->getPriorityByActionType();
            }

            // Ensure priority is not negative
            if (priority <= 0) {
                priority = 0;
            }

            // Set adjusted priority
            action->setPriority(priority);
            
            // Accumulate priority increment (for calculating total state priority)
            totalPriority += (priority - basePriority);
        }
        
        // Set total state priority
        _newState->setPriority(static_cast<int>(totalPriority));
    }

    /**
     * @brief Resolve and select a new action
     * 
     * Main entry point for action selection. Execution flow:
     * 1. Call adjustActions() to adjust priorities of all candidate actions
     * 2. Call subclass's selectNewAction() to select specific action (strategy pattern)
     * 3. Convert selected action to ActivityStateAction type and save to _newAction
     * 
     * @return Pointer to selected action, or nullptr if selection fails
     */
    ActionPtr AbstractAgent::resolveNewAction() {
        // Step 1: Adjust priorities of all actions
        this->adjustActions();
        
        // Step 2: Call subclass implementation of action selection strategy
        ActionPtr action = this->selectNewAction();
        
        // Step 3: Save selected action to _newAction (convert to ActivityStateAction type)
        _newAction = std::dynamic_pointer_cast<ActivityStateAction>(action);
        
        return action;
    }

    /**
     * @brief Handle null action situation
     * 
     * When no valid action can be selected (selectNewAction returns nullptr),
     * attempts to randomly select a valid action from current state as fallback.
     * 
     * Handling flow:
     * 1. Randomly select an action from _newState that passes validation filter
     * 2. If action found, attempt to resolve it (resolveAt)
     * 3. If resolution succeeds, return resolved action
     * 4. If all steps fail, log error and return nullptr
     * 
     * @return Pointer to handled action, or nullptr on failure
     */
    ActivityStateActionPtr AbstractAgent::handleNullAction() const {
        // Attempt to randomly select a valid action
        ActivityStateActionPtr action = this->_newState->randomPickAction(this->_validateFilter);
        
        if (nullptr != action) {
            // Get model pointer (use weak_ptr to avoid circular references)
            auto modelPtr = this->_model.lock();
            if (!modelPtr) {
                BDLOGE("Model has been destroyed, cannot handle null action");
                return nullptr;
            }
            
            // Resolve action (resolve action target, etc. based on timestamp)
            ActivityStateActionPtr resolved = this->_newState->resolveAt(action,
                                                                         modelPtr->getGraph()->getTimestamp());
            if (nullptr != resolved) {
                return resolved;
            }
        }
        
        // All attempts failed, log error
        BDLOGE("handle null action error!!!!!");
        return nullptr;
    }
}

#endif //AbstractAgent_CPP_
