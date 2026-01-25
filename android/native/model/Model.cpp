/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Model_CPP_
#define Model_CPP_

#include "Model.h"
#include "StateFactory.h"
#include "../utils.hpp"
#include <ctime>
#include <iostream>

namespace fastbotx {

    /**
     * @brief Log state information with each widget and action on a separate line
     * 
     * This helper function formats state information for debugging/logging purposes.
     * It prints the state hash, all widgets, and all actions in a readable format.
     * Long strings (>3000 chars) are split across multiple log lines.
     * 
     * @param state The state to log (nullptr is handled gracefully)
     */
    inline void logStatePerLine(const StatePtr &state) {
        if (state == nullptr) {
            BDLOGE("State is null, cannot log state information");
            return;
        }
        
        // Print state header with hash code
        BDLOG("{state: %lu", static_cast<unsigned long>(state->hash()));
        
        // Print each widget on a separate line for better readability
        BDLOG("widgets:");
        const auto &widgets = state->getWidgets();
        for (const auto &widget : widgets) {
            std::string widgetStr = widget->toString();
            // If widget string is too long, split it across multiple log lines
            if (widgetStr.length() > 3000) {
                logLongStringInfo("   " + widgetStr);
            } else {
                BDLOG("   %s", widgetStr.c_str());
            }
        }
        
        // Print each action on a separate line for better readability
        BDLOG("action:");
        const auto &actions = state->getActions();
        for (const auto &action : actions) {
            std::string actionStr = action->toString();
            // If action string is too long, split it across multiple log lines
            if (actionStr.length() > 3000) {
                logLongStringInfo("   " + actionStr);
            } else {
                BDLOG("   %s", actionStr.c_str());
            }
        }
        
        BDLOG("}");
    }

    /**
     * @brief Factory method to create a new Model instance
     * 
     * Uses new + shared_ptr instead of make_shared because the constructor is protected
     * and make_shared cannot access protected constructors from outside the class.
     * 
     * @return Shared pointer to a new Model instance
     */
    std::shared_ptr<Model> Model::create() {
        return std::shared_ptr<Model>(new Model());
    }

    /**
     * @brief Constructor for Model class
     * 
     * Initializes the model with:
     * - A new Graph instance for state management
     * - Preference singleton instance
     * - Network action parameters set to default values
     */
    Model::Model() {
#ifndef FASTBOT_VERSION
    // Use build timestamp if available, otherwise use compile-time date/time
    #ifdef FASTBOT_BUILD_TIMESTAMP
        #define FASTBOT_VERSION FASTBOT_BUILD_TIMESTAMP
    #else
        // Fallback to compiler's __DATE__ and __TIME__ macros
        #define FASTBOT_VERSION __DATE__ " " __TIME__
    #endif
#endif
        BLOG("----Fastbot native version " FASTBOT_VERSION "----\n");
        this->_graph = std::make_shared<Graph>();
        this->_preference = Preference::inst();
        this->_netActionParam.netActionTaskid = 0;
    }


    /**
     * @brief General entry point for getting next operation step according to RL model
     * 
     * This is the main entry point that accepts XML content as a string.
     * It parses the XML string into an Element object and delegates to the
     * ElementPtr-based version of getOperate().
     * 
     * @param descContent XML content of the current page as a string
     * @param activity Activity name string
     * @param deviceID Device ID string (default: empty string uses default device)
     * @return Next operation step in JSON format, or empty string if parsing fails
     */
    std::string Model::getOperate(const std::string &descContent, const std::string &activity,
                                  const std::string &deviceID) {
        // Parse XML string into Element object using tinyxml2
        ElementPtr elem = Element::createFromXml(descContent);
        if (nullptr == elem) {
            return "";
        }
        // Delegate to ElementPtr-based version
        return this->getOperate(elem, activity, deviceID);
    }

    /**
     * @brief Create and add an agent to the model for a specific device
     * 
     * Creates a new agent using the AgentFactory, adds it to the device-agent map,
     * and registers it as a listener to the graph for state change notifications.
     * 
     * @param deviceIDString Device ID string (empty string uses default device ID)
     * @param agentType The type of algorithm/agent to create
     * @param deviceType The type of device (default: Normal)
     * @return Shared pointer to the newly created agent
     */
    AbstractAgentPtr Model::addAgent(const std::string &deviceIDString, AlgorithmType agentType,
                                     DeviceType deviceType) {
        // Create agent using factory pattern
        auto agent = AgentFactory::create(agentType, shared_from_this(), deviceType);
        
        // Use default device ID if empty string provided
        const std::string &deviceID = deviceIDString.empty() ? ModelConstants::DefaultDeviceID
                                                             : deviceIDString;
        
        // Add the device-agent pair to the map
        this->_deviceIDAgentMap.emplace(deviceID, agent);
        
        // Register agent as a listener to graph updates
        // This allows the agent to be notified when new states are added
        this->_graph->addListener(agent);
        
        return agent;
    }

    /**
     * @brief Get the agent for a specific device ID
     * 
     * @param deviceID Device ID string (empty string uses default device ID)
     * @return Shared pointer to the agent, or nullptr if not found
     */
    AbstractAgentPtr Model::getAgent(const std::string &deviceID) const {
        const std::string &d = deviceID.empty() ? ModelConstants::DefaultDeviceID : deviceID;
        auto iter = this->_deviceIDAgentMap.find(d);
        if (iter != this->_deviceIDAgentMap.end()) {
            return iter->second;
        }
        return nullptr;
    }


    /**
     * @brief Get next operation step from Element object, returning JSON string
     * 
     * This method wraps the core getOperateOpt() method and converts the result
     * to a JSON string format.
     * 
     * @param element XML Element object of the current page
     * @param activity Activity name string
     * @param deviceID Device ID string (default: empty string uses default device)
     * @return Next operation step in JSON format
     */
    std::string Model::getOperate(const ElementPtr &element, const std::string &activity,
                                  const std::string &deviceID) {
        OperatePtr operate = getOperateOpt(element, activity, deviceID);
        // Convert operation object to JSON string
        std::string operateString = operate->toString();
        return operateString;
    }


    /**
     * @brief Get custom action from preference if one exists for this page
     * 
     * Checks if the user has specified a custom action for this activity/page
     * in the preference settings. Returns nullptr if no custom action is defined.
     * 
     * @param activity Activity name string
     * @param element XML Element object of the current page
     * @return Custom action if exists, nullptr otherwise
     */
    ActionPtr Model::getCustomActionIfExists(const std::string &activity, const ElementPtr &element) const {
        if (this->_preference) {
            BLOG("try get custom action from preference");
            return this->_preference->resolvePageAndGetSpecifiedAction(activity, element);
        }
        return nullptr;
    }

    /**
     * @brief Get or create an activity string pointer
     * 
     * This method optimizes memory usage by reusing existing activity string pointers
     * from the graph's visited activities set. If the activity already exists,
     * returns the cached shared pointer. Otherwise, creates a new one.
     * 
     * Performance optimization:
     * - Reuses existing string pointers to avoid duplicate string storage
     * - Uses hash-based set lookup for O(log n) complexity
     * 
     * @param activity The activity name string
     * @return Shared pointer to the activity string (cached or newly created)
     * 
     * @note The returned pointer may be from the cache or newly created.
     *       Newly created pointers will be added to the graph's visited activities
     *       when the state is added via createAndAddState().
     */
    stringPtr Model::getOrCreateActivityPtr(const std::string &activity) {
        // Get the set of visited activities (returns by value, but set is typically small)
        const stringPtrSet& activityStringPtrSet = this->_graph->getVisitedActivities();
        
        // Create temporary shared_ptr for lookup
        // Note: This creates a temporary object for comparison only
        // If not found, we'll return this pointer; if found, we'll return the cached one
        stringPtr tempActivityPtr = std::make_shared<std::string>(activity);
        
        // Try to find existing activity pointer in the set
        auto founded = activityStringPtrSet.find(tempActivityPtr);
        
        if (founded == activityStringPtrSet.end()) {
            // This is a new activity, return the newly created pointer
            return tempActivityPtr;
        } else {
            // Activity already exists, return the cached pointer to avoid duplication
            return *founded;
        }
    }

    /**
     * @brief Get or create an agent for the given device ID
     * 
     * This method retrieves an agent for the specified device ID. If no agent exists
     * for the device ID, returns the default agent. If no agents exist at all,
     * creates a default reuse agent.
     * 
     * Performance optimization:
     * - Uses find() instead of [] operator to avoid creating unnecessary map entries
     * - Falls back to default device ID if device ID is not found
     * 
     * @param deviceID The device ID string (empty string uses default device ID)
     * @return Shared pointer to the agent for the device
     * 
     * @note If the device ID is not found, returns the default agent instead of
     *       creating a new one. This ensures all devices have an agent to use.
     */
    AbstractAgentPtr Model::getOrCreateAgent(const std::string &deviceID) {
        // Create a default agent if map is empty
        if (this->_deviceIDAgentMap.empty()) {
            BLOG("%s", "use reuseAgent as the default agent");
            this->addAgent(ModelConstants::DefaultDeviceID, AlgorithmType::Reuse);
        }
        
        // Use find() instead of [] to avoid creating unnecessary map entries
        // Performance: O(log n) lookup without side effects
        auto agentIterator = this->_deviceIDAgentMap.find(deviceID);
        
        if (agentIterator == this->_deviceIDAgentMap.end()) {
            // Device ID not found, return the default agent
            // Use find() again to avoid [] operator side effects
            auto defaultIterator = this->_deviceIDAgentMap.find(ModelConstants::DefaultDeviceID);
            if (defaultIterator != this->_deviceIDAgentMap.end()) {
                return defaultIterator->second;
            }
            // Should not reach here if addAgent worked correctly, but handle gracefully
            return nullptr;
        } else {
            // Found the agent for this device ID
            return agentIterator->second;
        }
    }

    /**
     * @brief Create a new state from element and add it to the graph
     * 
     * Creates a state object based on the agent's algorithm type, then adds it
     * to the graph. The graph will deduplicate if a similar state already exists.
     * Marks the state as visited with the current graph timestamp.
     * 
     * @param element XML Element object of the current page (must not be nullptr)
     * @param agent The agent to use for state creation (determines state type)
     * @param activityPtr Shared pointer to activity name string
     * @return Shared pointer to the created/existing state, or nullptr if element is null
     */
    StatePtr Model::createAndAddState(const ElementPtr &element, const AbstractAgentPtr &agent,
                                      const stringPtr &activityPtr) {
        // Validate input
        if (nullptr == element) {
            return nullptr;
        }
        
        // Create state according to the agent's algorithm type
        // The state includes all possible actions based on widgets in the element
        StatePtr state = StateFactory::createState(agent->getAlgorithmType(), activityPtr, element);
        
        // Add state to graph (may return existing state if duplicate)
        // The graph handles deduplication based on state hash
        state = this->_graph->addState(state);
        
        // Mark state as visited with current graph timestamp
        state->visit(this->_graph->getTimestamp());
        
        return state;
    }

    /**
     * @brief Select an action based on state, agent, and custom preferences
     * 
     * This method implements the action selection logic:
     * 1. Uses custom action from preference if available
     * 2. Checks for blocked state and returns RESTART if needed
     * 3. Otherwise, asks the agent to resolve a new action
     * 4. Updates agent strategy and marks action as visited if it's a model action
     * 
     * @param state The current state (may be modified)
     * @param agent The agent to use for action selection (may be modified)
     * @param customAction Custom action from preference, if any
     * @param actionCost Output parameter: time cost for action generation in seconds
     * @return Selected action, or nullptr if selection failed
     */
    ActionPtr Model::selectAction(StatePtr &state, AbstractAgentPtr &agent, ActionPtr customAction, double &actionCost) {
        double startGeneratingActionTimestamp = currentStamp();
        actionCost = 0.0;
        ActionPtr action = customAction; // Use custom action if provided

        // Log state information for debugging
        logStatePerLine(state);

        // Check if preference indicates we should skip model actions (listen mode)
        bool shouldSkipActionsFromModel = this->_preference ? this->_preference->skipAllActionsFromModel() : false;
        if (shouldSkipActionsFromModel) {
            LOGI("listen mode skip get action from model");
        }

        // If no custom action specified and not in listen mode, get action from agent
        if (nullptr == customAction && !shouldSkipActionsFromModel) {
            // Check if we're in a blocked state and should restart
            if (-1 != BLOCK_STATE_TIME_RESTART &&
                -1 != Preference::inst()->getForceMaxBlockStateTimes() &&
                agent->getCurrentStateBlockTimes() > BLOCK_STATE_TIME_RESTART) {
                // Force restart action when stuck in blocked state
                action = Action::RESTART;
                BLOG("Ran into a block state %s", state ? state->getId().c_str() : "");
            } else {
                // Ask agent to resolve a new action (this is the main RL model entry point)
                auto resolvedAction = agent->resolveNewAction();
                action = std::dynamic_pointer_cast<Action>(resolvedAction);
                
                // Update agent's strategy based on the new action
                agent->updateStrategy();
                
                if (nullptr == action) {
                    BDLOGE("get null action!!!!");
                    return nullptr; // Handle null action gracefully
                }
            }
            
            // Calculate action generation time cost
            double endGeneratingActionTimestamp = currentStamp();
            actionCost = endGeneratingActionTimestamp - startGeneratingActionTimestamp;
            
            // If this is a model action and state exists, mark it as visited and update agent
            if (action->isModelAct() && state) {
                action->visit(this->_graph->getTimestamp());
                // Update agent's current state/action with new state/action
                agent->moveForward(state);
            }
        }
        
        return action;
    }

    /**
     * @brief Convert an action to an operate object and apply patches
     * 
     * Converts an Action object to a DeviceOperateWrapper (OperatePtr) that can be
     * executed. If the action requires a target widget, extracts widget information.
     * Applies preference patches and optionally clears state details for memory optimization.
     * 
     * @param action The action to convert (nullptr returns NOP operation)
     * @param state The current state (used for detail clearing optimization)
     * @return OperatePtr The operation object ready for execution
     */
    OperatePtr Model::convertActionToOperate(ActionPtr action, StatePtr state) {
        if (action == nullptr) {
            // Return no-operation if action is null
            return DeviceOperateWrapper::OperateNop;
        }

        BLOG("selected action %s", action->toString().c_str());
        
        // Convert action to operation object
        OperatePtr opt = action->toOperate();

        // If action requires a target widget, extract widget information
        if (action->requireTarget()) {
            if (auto stateAction = std::dynamic_pointer_cast<fastbotx::ActivityStateAction>(action)) {
                std::shared_ptr<Widget> widget = stateAction->getTarget();
                if (widget) {
                    // Serialize widget to JSON and attach to operation
                    std::string widget_str = widget->toJson();
                    opt->widget = widget_str;
                    BLOG("stateAction Widget: %s", widget_str.c_str());
                }
            }
        }

        // Apply preference patches to the operation (e.g., custom modifications)
        if (this->_preference) {
            this->_preference->patchOperate(opt);
        }

        // Memory optimization: clear state details after use if enabled
        // This reduces memory usage for states that are no longer needed in detail
        if (DROP_DETAIL_AFTER_SATE && state && !state->hasNoDetail()) {
            state->clearDetails();
        }

        return opt;
    }

    /**
     * @brief Core method for getting next operation step and updating RL model
     * 
     * This is the main orchestration method that:
     * 1. Gets custom action from preference if available
     * 2. Gets or creates activity pointer (memory optimization)
     * 3. Gets or creates agent for the device
     * 4. Creates and adds state to the graph
     * 5. Selects an action using the agent or custom action
     * 6. Converts action to operation object
     * 7. Logs performance metrics
     * 
     * @param element XML Element object of the current page
     * @param activity Activity name string
     * @param deviceID Device ID string (default: empty string uses default device)
     * @return DeviceOperateWrapper object containing the next operation to perform
     * 
     * @note This method updates the RL model by adding states and actions to the graph
     */
    OperatePtr Model::getOperateOpt(const ElementPtr &element, const std::string &activity,
                                    const std::string &deviceID) {
        // Record method start time for performance tracking
        double methodStartTimestamp = currentStamp();
        
        // Step 1: Get custom action from preference if user specified one
        ActionPtr customAction = getCustomActionIfExists(activity, element);
        
        // Step 2: Get or create activity pointer (reuses existing pointers for memory efficiency)
        stringPtr activityPtr = getOrCreateActivityPtr(activity);
        
        // Step 3: Get or create agent for this device (creates default if needed)
        AbstractAgentPtr agent = getOrCreateAgent(deviceID);
        
        // Step 4: Create state from element and add to graph
        // The graph handles deduplication if a similar state already exists
        StatePtr state = createAndAddState(element, agent, activityPtr);
        
        // Record state generation time for performance tracking
        double stateGeneratedTimestamp = currentStamp();
        
        // Step 5: Select action (either custom, restart, or from agent)
        double actionCost = 0.0;
        ActionPtr action = selectAction(state, agent, customAction, actionCost);
        
        // Handle null action gracefully
        if (nullptr == action) {
            return DeviceOperateWrapper::OperateNop;
        }
        
        // Step 6: Convert action to operation object and apply patches
        OperatePtr opt = convertActionToOperate(action, state);
        
        // Record end time and log performance metrics
        double methodEndTimestamp = currentStamp();
        BLOG("build state cost: %.3fs action cost: %.3fs total cost %.3fs",
             stateGeneratedTimestamp - methodStartTimestamp,
             actionCost,
             methodEndTimestamp - methodStartTimestamp);
        
        return opt;
    }

    /**
     * @brief Destructor for Model class
     * 
     * Clears the device-agent map to release all agent resources.
     * The graph and preference are shared pointers and will be automatically
     * cleaned up when the last reference is released.
     */
    Model::~Model() {
        this->_deviceIDAgentMap.clear();
    }

}
#endif //Model_CPP_
