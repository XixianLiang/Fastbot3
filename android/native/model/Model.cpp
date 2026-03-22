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
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
#include "../desc/gui_tree/GUITreeNode.h"
#include "../desc/reuse/ActivityNameAction.h"
#endif
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
#include "../desc/xpath/GUITreeBuilder.h"
#include "../desc/gui_tree/GUITree.h"
#include "../desc/naming/NamingFactory.h"
#endif
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
namespace {
using namespace fastbotx;

struct ApeNonDetPairStat {
    std::string sourceActivity;
    uintptr_t sourceKeyHash{0};
    uintptr_t actionHash{0};
    std::unordered_set<uintptr_t> targetKeyHashes;
    size_t targetCount{0};
};

void collectGUITreeNodesPreOrder(const gui_tree::GUITreeNode *node, std::vector<const gui_tree::GUITreeNode *> *out) {
    if (!node || !out) {
        return;
    }
    out->push_back(node);
    for (const auto &ch : node->getChildren()) {
        collectGUITreeNodesPreOrder(ch.get(), out);
    }
}

void applyApeDynamicActionHashesToReuseState(const StatePtr &state,
                                             const std::vector<const gui_tree::GUITreeNode *> &nodesPreOrder,
                                             const naming::StateKey &apeKey) {
    if (!state) {
        return;
    }
    const uintptr_t activityH = fastStringHash(apeKey.activity());
    const WidgetPtrVec &ws = state->getWidgets();
    const bool indexAligned = (ws.size() == nodesPreOrder.size());
    for (const auto &a : state->getActions()) {
        auto ana = std::dynamic_pointer_cast<ActivityNameAction>(a);
        if (!ana) {
            continue;
        }
        WidgetPtr w = ana->getTarget();
        if (!w) {
            ana->applyApeDynamicRlIdentity(activityH, 0x1);
            continue;
        }
        uintptr_t th = w->hash();
        if (indexAligned) {
            size_t idx = SIZE_MAX;
            for (size_t i = 0; i < ws.size(); ++i) {
                if (ws[i] == w) {
                    idx = i;
                    break;
                }
            }
            if (idx != SIZE_MAX) {
                const gui_tree::GUITreeNode *n = nodesPreOrder[idx];
                if (n) {
                    naming::NamePtr nxp = n->getXPathName();
                    if (nxp) {
                        th = fastStringHash(nxp->toXPath());
                    }
                }
            }
        }
        ana->applyApeDynamicRlIdentity(activityH, th);
    }
}
} // namespace

#include "../desc/naming/NamingFactory.h"
#include "../desc/naming/NamerLattice.h"
#include "../desc/naming/NamerFactory.h"
#endif
#include "../Base.h"
#include "../utils.hpp"
#include "../thirdpart/json/json.hpp"
#include "../llm/HttpLlmClient.h"
#include <algorithm>
#include <ctime>
#include <iostream>
#include <map>
#include <fstream>
#include <sstream>
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
#include <unordered_map>
#include <utility>

namespace {
    /// Convert WidgetKeyMask to human-readable dimension list for logging (e.g. "Clazz|ResourceID|ContentDesc").
    std::string maskToDimensionString(fastbotx::WidgetKeyMask m) {
        std::ostringstream os;
        const char *sep = "";
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::Clazz)) { os << sep << "Clazz"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::ResourceID)) { os << sep << "ResourceID"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::OperateMask)) { os << sep << "OperateMask"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::ScrollType)) { os << sep << "ScrollType"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::Text)) { os << sep << "Text"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::ContentDesc)) { os << sep << "ContentDesc"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::Index)) { os << sep << "Index"; sep = "|"; }
        return os.str().empty() ? "(none)" : os.str();
    }
}
#endif

namespace fastbotx {

    WidgetKeyMask Model::getActivityKeyMask(const std::string &activity) const {
        auto it = _activityKeyMask.find(activity);
        if (it != _activityKeyMask.end()) {
            return it->second;
        }
        return DefaultWidgetKeyMask;
    }

    std::shared_ptr<LlmClient> Model::getLlmClient() const {
        return _llmTaskAgent ? _llmTaskAgent->getLlmClient() : nullptr;
    }

    void Model::setActivityKeyMask(const std::string &activity, WidgetKeyMask mask) {
        _activityKeyMask[activity] = mask;
    }

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
        
        // Print each widget on a separate line for better readability; skip empty (e.g. toXPath returns "" when details cleared)
        BDLOG("widgets:");
        const auto &widgets = state->getWidgets();
        for (const auto &widget : widgets) {
            std::string widgetStr = widget->toString();
            if (widgetStr.empty()) continue;
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
        BLOG("----Fastbot native build version: " FASTBOT_VERSION "----\n");
        this->_graph = std::make_shared<Graph>();
        this->_preference = Preference::inst();
        this->_netActionParam.netActionTaskid = 0;

        // Initialize LLMTaskAgent with HTTP LLM client if LLM is enabled in config.
        LlmRuntimeConfig llmCfg;
        if (this->_preference) {
            llmCfg = this->_preference->getLlmRuntimeConfig();
        }
        std::shared_ptr<LlmClient> client = nullptr;
        if (llmCfg.enabled) {
            client = std::make_shared<HttpLlmClient>(llmCfg);
            BLOG("LLMTaskAgent: HTTP LLM client initialized with model %s", llmCfg.model.c_str());
        } else {
            BLOG("LLMTaskAgent: LLM is disabled in config");
        }
        this->_llmTaskAgent = std::make_shared<LLMTaskAgent>(this->_preference, client);
        this->_apeStateNamingManager = std::make_shared<naming::StateNamingManager>(nullptr);
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        this->_apeTransitionLog.resize(MaxTransitionLogSize);
        BLOG("state abstraction: enabled (check interval=%d, batch every %d steps)",
             (int)RefinementCheckInterval, (int)RefinementCheckInterval);
#endif
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
            BLOG("%s", "use DoubleSarsaAgent as the default agent");
            this->addAgent(ModelConstants::DefaultDeviceID, AlgorithmType::DoubleSarsa);
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
    StatePtr Model::buildStateOnly(const ElementPtr &element, const AbstractAgentPtr &agent,
                                   const stringPtr &activityPtr) {
        if (nullptr == element) {
            return nullptr;
        }
        std::string activityStr = activityPtr ? *activityPtr : "";
        WidgetKeyMask mask = getActivityKeyMask(activityStr);
        StatePtr state = StateFactory::createState(agent->getAlgorithmType(), activityPtr, element, mask);
        return state;
    }

    StatePtr Model::createAndAddState(const ElementPtr &element, const AbstractAgentPtr &agent,
                                      const stringPtr &activityPtr) {
        StatePtr state = buildStateOnly(element, agent, activityPtr);
        if (!state) return nullptr;
        state = this->_graph->addState(state);
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
            
            // moveForward is now called at the start of the next getOperateOpt (before addState),
            // so (fromState, actionTaken, nextState) is correct for AIG/updateKnowledge.
            if (state && action->isModelAct()) {
                action->visit(this->_graph->getTimestamp());
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
        
        // Step 0: Match LLM task on raw tree (before resolvePage) so checkpoint matches unmodified UI.
        LlmTaskConfigPtr preMatchedLlmTask = nullptr;
        if (this->_preference && element) {
            preMatchedLlmTask = this->_preference->matchLlmTask(activity, element);
        }
        
        // Step 1: Resolve page (black widgets, tree pruning, valid texts) before using element.
        if (this->_preference && element) {
            this->_preference->resolvePage(activity, element);
        }
        // Step 2: Custom action from max.xpath.actions (if any) for this activity and page.
        ActionPtr customAction = (this->_preference && element)
            ? this->_preference->getCustomActionFromXpath(activity, element)
            : nullptr;
        
        // Step 3: Get or create activity pointer (reuses existing pointers for memory efficiency)
        stringPtr activityPtr = getOrCreateActivityPtr(activity);
        
        // Step 4: Get or create agent for this device (creates default if needed)
        AbstractAgentPtr agent = getOrCreateAgent(deviceID);
        
        // Step 5: Build state, notify agent of transition (moveForward) before adding to graph, then add state
        // moveForward(currentState) must run before addState so agent still has previous _newState/_newAction
        // for (fromState, actionTaken, nextState) → updateKnowledge / AIG edges (see FIND_NAVIGATE_PATH_CODE_REVIEW §7).
        double buildStateStartTimestamp = currentStamp();
        StatePtr built = buildStateOnly(element, agent, activityPtr);
        StatePtr state = built;
        if (state) {
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
            naming::StateKey apeKey = naming::StateKey::fromParts(
                naming::StateKey::canonicalActivityString(activity), nullptr, {});
            bool haveApeKey = false;
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
            const bool wantApeRlIdentity = !_preference || !_preference->useStaticReuseAbstraction();
            haveApeKey = buildApeStateKeyFromElementTree(
                element, activity, &apeKey, wantApeRlIdentity ? built : StatePtr());
            if (wantApeRlIdentity) {
                if (haveApeKey) {
                    built->applyDynamicAbstractionIdentityHash(apeKey.hash());
                } else {
                    const uintptr_t xmlH = fastStringHash(element->toXML());
                    apeKey = naming::StateKey::fromFallbackXmlStringHash(activity, xmlH);
                    built->applyDynamicAbstractionIdentityHash(apeKey.hash());
                    applyApeDynamicActionHashesToReuseState(built, {}, apeKey);
                    haveApeKey = true;
                }
            }
#else
            haveApeKey = buildApeStateKeyFromElementTree(element, activity, &apeKey);
#endif
            if (haveApeKey && _preference && _preference->useApeGraphDedupByStateKey()) {
                const uintptr_t kh = apeKey.hash();
                auto itDedup = _ape_graph_state_by_key.find(kh);
                if (itDedup != _ape_graph_state_by_key.end()) {
                    agent->moveForward(itDedup->second);
                    _graph->recordStateVisit(itDedup->second, built);
                    state = itDedup->second;
                } else {
                    agent->moveForward(built);
                    state = _graph->addState(built);
                    _ape_graph_state_by_key.emplace(kh, state);
                }
            } else {
                agent->moveForward(built);
                state = _graph->addState(built);
            }
            state->visit(this->_graph->getTimestamp());
            if (haveApeKey) {
                recordApeStateKey(state, apeKey);
                logApeStateKeySnapshot(activity, state, apeKey, _graph);
            }
#elif DYNAMIC_STATE_ABSTRACTION_ENABLED
            // No pugixml in this build: XPath/GUITree unavailable — use XML-digest StateKey only (still APE-style id).
            naming::StateKey apeKey = naming::StateKey::fromParts(
                naming::StateKey::canonicalActivityString(activity), nullptr, {});
            bool haveApeKey = false;
            const bool wantApeRlIdentity = !_preference || !_preference->useStaticReuseAbstraction();
            std::vector<const gui_tree::GUITreeNode *> guiPreOrder;
            if (wantApeRlIdentity) {
                const uintptr_t xmlH = fastStringHash(element->toXML());
                apeKey = naming::StateKey::fromFallbackXmlStringHash(activity, xmlH);
                built->applyDynamicAbstractionIdentityHash(apeKey.hash());
                applyApeDynamicActionHashesToReuseState(built, guiPreOrder, apeKey);
                haveApeKey = true;
            }
            if (haveApeKey && _preference && _preference->useApeGraphDedupByStateKey()) {
                const uintptr_t kh = apeKey.hash();
                auto itDedup = _ape_graph_state_by_key.find(kh);
                if (itDedup != _ape_graph_state_by_key.end()) {
                    agent->moveForward(itDedup->second);
                    _graph->recordStateVisit(itDedup->second, built);
                    state = itDedup->second;
                } else {
                    agent->moveForward(built);
                    state = _graph->addState(built);
                    _ape_graph_state_by_key.emplace(kh, state);
                }
            } else {
                agent->moveForward(built);
                state = _graph->addState(built);
            }
            state->visit(this->_graph->getTimestamp());
            if (haveApeKey) {
                recordApeStateKey(state, apeKey);
                logApeStateKeySnapshot(activity, state, apeKey, _graph);
            }
#else
            agent->moveForward(state);
            state = this->_graph->addState(state);
            state->visit(this->_graph->getTimestamp());
#endif
        }
        double buildStateEndTimestamp = currentStamp();
        bool fromLlm = (_llmTaskAgent && _llmTaskAgent->inSession());
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        if (!fromLlm) {
            recordTransition(agent, state);
        }
#endif
        // Step 5b: Removed — image now stays in Java (setLastScreenshotForLlm + doLlmHttpPostFromPrompt).
        // Native no longer returns NOP when screenshotBytes is empty; Java always has the image when needed.
        // Step 6: Optionally delegate to LLMTaskAgent before RL (pass pre-matched task from raw tree).
        ActionPtr llmAction = nullptr;
        if (this->_llmTaskAgent) {
            llmAction = this->_llmTaskAgent->selectNextAction(element, activity, deviceID, preMatchedLlmTask);
        }

        // Step 7: Select action (either LLM, custom, restart, or from agent)
        double actionCost = 0.0;
        ActionPtr action;
        if (llmAction) {
            // When LLMTaskAgent returns an action, we bypass RL for this step.
            action = llmAction;
        } else {
            action = selectAction(state, agent, customAction, actionCost);
        }
        
        // Handle null action gracefully
        if (nullptr == action) {
            return DeviceOperateWrapper::OperateNop;
        }

        // Resolve merged widgets: when multiple concrete nodes share the same abstract widget,
        // set action target to the next concrete node (visitCount % total) so each selection hits a different node (e.g. 特价→首页→秒送→新品).
        if (state && action && action->requireTarget()) {
            if (auto stateAction = std::dynamic_pointer_cast<ActivityStateAction>(action)) {
                state->resolveAt(stateAction, _graph->getTimestamp());
            }
        }

        // Step 8: Convert action to operation object and apply patches
        OperatePtr opt = convertActionToOperate(action, state);
        if (llmAction) {
            opt->allowFuzzing = false;
        }
        // Optional: agent-provided LLM-generated input text (e.g. LLMExplorerAgent content-aware input)
        if (agent) {
            std::string agentInputText = agent->getInputTextForAction(state, action);
            if (!agentInputText.empty()) {
                opt->setText(agentInputText);
            }
        }

        // Record end time and log performance metrics (currentStamp returns ms, keep ms for log)
        double methodEndTimestamp = currentStamp();
        double buildStateCostMs = buildStateEndTimestamp - buildStateStartTimestamp;
        double actionCostMs = actionCost;
        double totalCostMs = methodEndTimestamp - methodStartTimestamp;
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        if (state && state->usesDynamicAbstractionIdentityHash()) {
            BLOG("build state cost: %.3fms action cost: %.3fms total cost: %.3fms dims=[APE]",
                 buildStateCostMs,
                 actionCostMs,
                 totalCostMs);
        } else {
            BLOG("build state cost: %.3fms action cost: %.3fms total cost: %.3fms dims=[%s]",
                 buildStateCostMs,
                 actionCostMs,
                 totalCostMs,
                 maskToDimensionString(getActivityKeyMask(activity)).c_str());
        }
#else
        BLOG("build state cost: %.3fms action cost: %.3fms total cost: %.3fms",
             buildStateCostMs,
             actionCostMs,
             totalCostMs);
#endif
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        if (!fromLlm) {
            _stepCountSinceLastCheck++;
            if (_stepCountSinceLastCheck >= RefinementCheckInterval) {
                runRefinementAndCoarseningIfScheduled();
                _stepCountSinceLastCheck = 0;
            }
        }
#endif
        return opt;
    }

#if DYNAMIC_STATE_ABSTRACTION_ENABLED
    void Model::recordTransition(const AbstractAgentPtr &agent, const StatePtr &targetState) {
        if (!agent || !targetState) return;
        StatePtr srcState = agent->getCurrentState();
        ActivityStateActionPtr act = agent->getCurrentAction();
        if (!srcState || !act || !act->isModelAct() || !act->requireTarget()) return;
        recordApeTransitionForAbstraction(srcState, targetState, act);
    }

    void Model::recordApeTransitionForAbstraction(const StatePtr &src, const StatePtr &tgt,
                                                  const ActivityStateActionPtr &act) {
        if (!src || !tgt || !act || _apeTransitionLog.empty()) {
            return;
        }
        auto itS = _ape_state_keys_by_hash.find(src->hash());
        auto itT = _ape_state_keys_by_hash.find(tgt->hash());
        if (itS == _ape_state_keys_by_hash.end() || itT == _ape_state_keys_by_hash.end()) {
            return;
        }
        ApeTransitionEntry e;
        e.sourceKeyHash = itS->second.hash();
        e.actionHash = act->hash();
        e.targetKeyHash = itT->second.hash();
        {
            auto actPtr = src->getActivityString();
            e.sourceActivity = naming::StateKey::canonicalActivityString(
                (actPtr && actPtr.get()) ? *actPtr : "");
        }
        e.valid = true;
        BDLOG("ape naming: transition srcKey=%lu act=%lu tgtKey=%lu activity=%s",
              (unsigned long)e.sourceKeyHash, (unsigned long)e.actionHash, (unsigned long)e.targetKeyHash,
              e.sourceActivity.c_str());
        ApeTransitionEntry &aSlot = _apeTransitionLog[_apeTransitionLogWriteIndex];
        if (aSlot.valid) {
            apePairAggRemove(aSlot);
        }
        aSlot = std::move(e);
        apePairAggAdd(aSlot);
        _apeTransitionLogWriteIndex = (_apeTransitionLogWriteIndex + 1) % _apeTransitionLog.size();
    }

    void Model::apePairAggRemove(const ApeTransitionEntry &e) {
        if (!e.valid || e.sourceKeyHash == e.targetKeyHash) {
            return;
        }
        ApePairKey pk{e.sourceKeyHash, e.actionHash};
        auto it = _apePairAgg.find(pk);
        if (it == _apePairAgg.end()) {
            return;
        }
        auto &tm = it->second.first;
        auto itT = tm.find(e.targetKeyHash);
        if (itT == tm.end()) {
            return;
        }
        if (--(itT->second) <= 0) {
            tm.erase(itT);
        }
        if (tm.empty()) {
            _apePairAgg.erase(it);
        }
    }

    void Model::apePairAggAdd(const ApeTransitionEntry &e) {
        if (!e.valid || e.sourceKeyHash == e.targetKeyHash) {
            return;
        }
        ApePairKey pk{e.sourceKeyHash, e.actionHash};
        auto &slot = _apePairAgg[pk];
        slot.first[e.targetKeyHash]++;
        slot.second = e.sourceActivity;
    }

    std::vector<std::string> Model::detectNonDeterminismApe() const {
        const int minTargets = (_preference ? _preference->getApeNamingMinNonDetTargets()
                                            : MinNonDeterminismCount);
        std::set<std::string> activitiesSet;
        for (const auto &kv : _apePairAgg) {
            if (kv.second.first.size() >= static_cast<size_t>(minTargets)) {
                activitiesSet.insert(kv.second.second);
            }
        }
        return std::vector<std::string>(activitiesSet.begin(), activitiesSet.end());
    }

    bool Model::refineActivityApeNaming(const std::string &activity) {
        return refineActivityApeNaming(activity, nullptr, -1);
    }

    bool Model::refineActivityApeNaming(const std::string &activity, const ApeRefinePair *pair,
                                        int precomputedActivityNonDetPairCount) {
        const std::string actKey = naming::StateKey::canonicalActivityString(activity);
        const int minTargets = (_preference ? _preference->getApeNamingMinNonDetTargets()
                                            : MinNonDeterminismCount);
        int nonDetPairs = 0;
        size_t dominantPairTargets = 0;
        uintptr_t dominantSourceKeyHash = 0;
        uintptr_t dominantActionHash = 0;
        std::unordered_set<uintptr_t> dominantTargetKeyHashes;
        const bool useBatchNonDet =
            precomputedActivityNonDetPairCount >= 0 && pair && pair->sourceKeyHash != 0 &&
            pair->actionHash != 0 && pair->targetCount >= static_cast<size_t>(minTargets);
        if (useBatchNonDet) {
            nonDetPairs = precomputedActivityNonDetPairCount;
            dominantPairTargets = pair->targetCount;
            dominantSourceKeyHash = pair->sourceKeyHash;
            dominantActionHash = pair->actionHash;
            dominantTargetKeyHashes = pair->targetKeyHashes;
        } else {
            for (const auto &kv : _apePairAgg) {
                if (kv.second.second != actKey) {
                    continue;
                }
                const auto &tm = kv.second.first;
                if (tm.size() < static_cast<size_t>(minTargets)) {
                    continue;
                }
                nonDetPairs++;
                if (tm.size() > dominantPairTargets) {
                    dominantPairTargets = tm.size();
                    dominantSourceKeyHash = kv.first.sourceKeyHash;
                    dominantActionHash = kv.first.actionHash;
                    dominantTargetKeyHashes.clear();
                    for (const auto &te : tm) {
                        dominantTargetKeyHashes.insert(te.first);
                    }
                }
            }
            // Batch (`runApeNamingAbstractionBatch`) passes the exact non-det pair for this attempt; use it as
            // the trigger for blacklist/admissibility and `ctx.trigger*` so logs and behavior match `refine-attempt`.
            // With `pair == nullptr` (one-arg API), keep scan-only dominant = max target fan-out for this activity.
            if (pair && pair->sourceKeyHash != 0 && pair->actionHash != 0 &&
                pair->targetCount >= static_cast<size_t>(minTargets)) {
                dominantPairTargets = pair->targetCount;
                dominantSourceKeyHash = pair->sourceKeyHash;
                dominantActionHash = pair->actionHash;
                dominantTargetKeyHashes = pair->targetKeyHashes;
            }
        }
        if (dominantSourceKeyHash != 0 || dominantActionHash != 0) {
            ApePairKey pairKey{dominantSourceKeyHash, dominantActionHash};
            auto itBlk = _apeRefinePairBlacklist.find(actKey);
            if (itBlk != _apeRefinePairBlacklist.end() && itBlk->second.count(pairKey) != 0) {
                BDLOG("ape naming: skip refine activity=%s reason=trigger pair blacklisted srcKey=%lu act=%lu",
                      activity.c_str(), (unsigned long)dominantSourceKeyHash, (unsigned long)dominantActionHash);
                return false;
            }
        }
        const int minNonDetPairs = (_preference ? _preference->getApeNamingActionRefineMinNonDetPairs() : 1);
        if (nonDetPairs < minNonDetPairs) {
            BDLOG("ape naming: skip refine activity=%s reason=nonDetPairs<%d (%d)",
                  activity.c_str(), minNonDetPairs, nonDetPairs);
            return false;
        }
        const int minNonDetPairDelta =
            (_preference ? _preference->getApeNamingActionRefineMinNonDetPairDelta() : 0);
        auto itCtx = _apeNamingContext.find(actKey);
        if (itCtx != _apeNamingContext.end()) {
            const int lastPairs = itCtx->second.nonDetPairsAtLastNamingRefinement;
            if (nonDetPairs < lastPairs + minNonDetPairDelta) {
                BDLOG("ape naming: skip refine activity=%s reason=nonDetPairDelta<%d (now=%d,last=%d)",
                      activity.c_str(), minNonDetPairDelta, nonDetPairs, lastPairs);
                return false;
            }
        }
        const size_t activityStateCount = getGraph()->getStateCountByActivity(activity);
        const int minStates = (_preference ? _preference->getApeNamingActionRefineMinActivityStates() : 2);
        if (activityStateCount < static_cast<size_t>(minStates)) {
            BDLOG("ape naming: skip refine activity=%s reason=stateCount<%d (%zu)",
                  activity.c_str(), minStates, activityStateCount);
            return false;
        }
        const int minStateDelta = (_preference ? _preference->getApeNamingActionRefineMinStateDelta() : 1);
        if (itCtx != _apeNamingContext.end()) {
            const size_t lastCount = itCtx->second.stateCountAtLastNamingRefinement;
            if (activityStateCount < lastCount + static_cast<size_t>(minStateDelta)) {
                BDLOG("ape naming: skip refine activity=%s reason=stateDelta<%d (now=%zu,last=%zu)",
                      activity.c_str(), minStateDelta, activityStateCount, lastCount);
                return false;
            }
        }
        naming::ActivityNamingManager &mgr = _apeStateNamingManager->activityManager();
        naming::NamingPtr cur = mgr.getNaming(actKey);
        if (!cur) {
            cur = naming::NamingFactory::defaultRootNaming();
            if (!cur) {
                return false;
            }
            mgr.setNaming(actKey, cur);
        }
        naming::NamerLattice lat(naming::NamerFactory::CURRENT);
        std::set<std::string> blk;
        for (const auto &p : _apeNamingCoarseningBlacklist) {
            if (p.first == actKey) {
                blk.insert(p.second);
            }
        }
        naming::NamingFactory::ActionRefinementOptions opts;
        opts.max_steps = (_preference ? _preference->getApeNamingActionRefineHops() : 8);
        opts.blacklist = &blk;
        // TODO(ape-alpha): map Java action-set α predicates into this accept_predicate hook.
        // Current native baseline supports configurable predicate modes.
        const std::string predicateMode =
            (_preference ? _preference->getApeNamingActionRefinePredicateMode() : "fingerprint_change");
        const std::string selectionMode =
            (_preference ? _preference->getApeNamingActionRefineSelectionMode() : "first_accept");
        const std::string ruleProfile =
            (_preference ? _preference->getApeNamingActionRefineRuleProfile() : "baseline");
        opts.choose_deepest_acceptable = (selectionMode == "deepest_accept");
        // ALPHA_BASE_PREDICATE: configurable baseline predicate family before Java-specific α rules are ported.
        if (ruleProfile == "java_rule_01_preview") {
            // Preview profile for first Java-rule mapping experiments:
            // stricter predicate + deeper acceptable-hop preference.
            opts.choose_deepest_acceptable = true;
            const std::string curFp = cur->fingerprintString();
            const int curFineness = cur->getFineness();
            opts.accept_predicate = [curFp, curFineness](const naming::NamingPtr &candidate) {
                return candidate && candidate->fingerprintString() != curFp &&
                       candidate->getFineness() > curFineness;
            };
        } else if (ruleProfile == "java_rule_03_preview") {
            // Preview profile for branch/topology experiments:
            // evaluate all immediate refinements at each depth, not only greedy refs[0] chain.
            opts.choose_deepest_acceptable = true;
            opts.evaluate_all_immediate_candidates = true;
            const std::string curFp = cur->fingerprintString();
            opts.accept_predicate = [curFp](const naming::NamingPtr &candidate) {
                return candidate && candidate->fingerprintString() != curFp;
            };
        } else if (ruleProfile == "strict_baseline") {
            const std::string curFp = cur->fingerprintString();
            const int curFineness = cur->getFineness();
            opts.accept_predicate = [curFp, curFineness](const naming::NamingPtr &candidate) {
                return candidate && candidate->fingerprintString() != curFp &&
                       candidate->getFineness() > curFineness;
            };
        } else if (predicateMode == "fingerprint_change") {
            const std::string curFp = cur->fingerprintString();
            opts.accept_predicate = [curFp](const naming::NamingPtr &candidate) {
                return candidate && candidate->fingerprintString() != curFp;
            };
        } else if (predicateMode == "fineness_increase") {
            const int curFineness = cur->getFineness();
            opts.accept_predicate = [curFineness](const naming::NamingPtr &candidate) {
                return candidate && candidate->getFineness() > curFineness;
            };
        } else {
            opts.accept_predicate = {};
        }
        naming::NamingPtr next = naming::NamingFactory::actionRefinementWithOptions(cur, lat, opts);
        if (!next) {
            BDLOG("ape naming: skip refine activity=%s reason=no non-blacklisted finer namer", activity.c_str());
            return false;
        }
        // Transition-level admissibility baseline:
        // on concrete non-deterministic (sourceKey, action) pairs, require strict refinement.
        if (dominantPairTargets >= static_cast<size_t>(minTargets) &&
            next->getFineness() <= cur->getFineness()) {
            BDLOG("ape naming: skip refine activity=%s reason=admissibility(nonDetPair strict-fineness) "
                  "dominantPairTargets=%zu curFine=%d nextFine=%d",
                  activity.c_str(), dominantPairTargets, cur->getFineness(), next->getFineness());
            return false;
        }
        if (dominantPairTargets >= static_cast<size_t>(minTargets) &&
            next->fingerprintString() == cur->fingerprintString()) {
            BDLOG("ape naming: skip refine activity=%s reason=admissibility(nonDetPair no-fingerprint-change) "
                  "dominantPairTargets=%zu",
                  activity.c_str(), dominantPairTargets);
            return false;
        }
        ApeNamingAbstractionContext &ctx = _apeNamingContext[actKey];
        ctx.previousNamingBeforeRefine = cur;
        ctx.previousNamingFingerprintBeforeRefine = cur->fingerprintString();
        ctx.oldKeyHashToNewKeyHashes.clear();
        ctx.stateCountAtLastNamingRefinement = getGraph()->getStateCountByActivity(activity);
        ctx.nonDetPairsAtLastNamingRefinement = nonDetPairs;
        ctx.triggerSourceKeyHash = dominantSourceKeyHash;
        ctx.triggerActionHash = dominantActionHash;
        ctx.triggerTargetKeyHashes = std::move(dominantTargetKeyHashes);
        ctx.triggerTargetCountAtRefine = dominantPairTargets;
        mgr.setNaming(actKey, next);
        invalidateApeGraphStateKeyDedupMap();
        BLOG("ape naming: refine activity=%s", activity.c_str());
        return true;
    }

    void Model::invalidateApeGraphStateKeyDedupMap() {
        _ape_graph_state_by_key.clear();
    }

    void Model::coarsenActivityApeNamingIfNeeded(const std::string &activity) {
        const std::string actKey = naming::StateKey::canonicalActivityString(activity);
        auto it = _apeNamingContext.find(actKey);
        if (it == _apeNamingContext.end()) {
            return;
        }
        ApeNamingAbstractionContext &ctx = it->second;
        naming::ActivityNamingManager &mgr2 = _apeStateNamingManager->activityManager();
        naming::NamingPtr cur = mgr2.getNaming(actKey);
        naming::NamingPtr prev = ctx.previousNamingBeforeRefine;
        if (!cur || !prev) {
            return;
        }
        const size_t affectedOldKeys = ctx.oldKeyHashToNewKeyHashes.size();
        std::unordered_set<uintptr_t> totalNewKeys;
        bool overSplit = false;
        for (const auto &p : ctx.oldKeyHashToNewKeyHashes) {
            totalNewKeys.insert(p.second.begin(), p.second.end());
            if (p.second.size() > static_cast<size_t>(BetaMaxSplitCount)) {
                overSplit = true;
                break;
            }
        }
        // Java batchAbstract-inspired global rollback gates:
        // 1) affected old states threshold; 2) resulting refined targets threshold by fineness.
        const int affectedThreshold = 8;
        const int totalTypes = static_cast<int>(naming::namerTypesUsed().size());
        const int fineness = cur->getFineness();
        const int shift = std::max(0, totalTypes - fineness);
        const int targetThreshold = std::min(8, std::max(1, 2 << shift));
        const bool overAffected = affectedOldKeys > static_cast<size_t>(affectedThreshold);
        const bool overTargets = totalNewKeys.size() > static_cast<size_t>(targetThreshold);
        // Java batchAbstract filterTargets-like gate: focus on trigger source-key bucket.
        const uintptr_t triggerSource = ctx.triggerSourceKeyHash;
        size_t filteredAffected = 0;
        size_t filteredTargets = 0;
        auto itFiltered = ctx.oldKeyHashToNewKeyHashes.find(triggerSource);
        if (itFiltered != ctx.oldKeyHashToNewKeyHashes.end()) {
            filteredAffected = 1;
            filteredTargets = itFiltered->second.size();
        }
        const bool overFilteredAffected =
            (filteredAffected > 0 && filteredAffected > static_cast<size_t>(affectedThreshold));
        const bool overFilteredTargets =
            (filteredTargets > 0 && filteredTargets > static_cast<size_t>(targetThreshold));

        // Pair-driven effectiveness check: if trigger source is observed but still unsplit
        // and trigger targets remain divergent after refinement, rollback.
        bool unresolvedTriggerPair = false;
        size_t postRefineMaxFanoutForAction = 0;
        if (triggerSource != 0 && itFiltered != ctx.oldKeyHashToNewKeyHashes.end()) {
            const bool sourceSplit = itFiltered->second.size() > 1;
            std::unordered_set<uintptr_t> mappedTargetUnion;
            size_t coveredOldTargets = 0;
            for (auto oldT : ctx.triggerTargetKeyHashes) {
                auto itOld = ctx.oldKeyHashToNewKeyHashes.find(oldT);
                if (itOld == ctx.oldKeyHashToNewKeyHashes.end()) {
                    continue;
                }
                coveredOldTargets++;
                mappedTargetUnion.insert(itOld->second.begin(), itOld->second.end());
            }
            const size_t minEvidence = std::min<size_t>(ctx.triggerTargetCountAtRefine, 2);
            if (!sourceSplit && coveredOldTargets >= minEvidence && mappedTargetUnion.size() >= minEvidence) {
                unresolvedTriggerPair = true;
            }
        }
        // Action-level transition-sample check (Java checkActionRefinement approximation):
        // estimate post-refine fan-out via old->new key mapping evidence.
        if (ctx.triggerActionHash != 0 && ctx.triggerSourceKeyHash != 0) {
            auto itSrcMap = ctx.oldKeyHashToNewKeyHashes.find(ctx.triggerSourceKeyHash);
            if (itSrcMap != ctx.oldKeyHashToNewKeyHashes.end() && !itSrcMap->second.empty()) {
                std::unordered_set<uintptr_t> mappedTargetUnion;
                size_t coveredOldTargets = 0;
                for (auto oldT : ctx.triggerTargetKeyHashes) {
                    auto itOld = ctx.oldKeyHashToNewKeyHashes.find(oldT);
                    if (itOld == ctx.oldKeyHashToNewKeyHashes.end()) {
                        continue;
                    }
                    coveredOldTargets++;
                    mappedTargetUnion.insert(itOld->second.begin(), itOld->second.end());
                }
                postRefineMaxFanoutForAction = mappedTargetUnion.size();
                const size_t minEvidence = std::min<size_t>(ctx.triggerTargetCountAtRefine, 2);
                // Evidence guard: only fail when source and targets both have enough remap evidence.
                if (ctx.triggerTargetCountAtRefine > 0 &&
                    itSrcMap->second.size() >= minEvidence &&
                    coveredOldTargets >= minEvidence &&
                    postRefineMaxFanoutForAction >= ctx.triggerTargetCountAtRefine) {
                    unresolvedTriggerPair = true;
                }
            }
        }
        if (overSplit || overAffected || overTargets ||
            overFilteredAffected || overFilteredTargets || unresolvedTriggerPair) {
            std::string fpFiner = cur->fingerprintString();
            mgr2.setNaming(actKey, prev);
            invalidateApeGraphStateKeyDedupMap();
            _apeNamingCoarseningBlacklist.insert(std::make_pair(actKey, fpFiner));
            if (ctx.triggerSourceKeyHash != 0 || ctx.triggerActionHash != 0) {
                _apeRefinePairBlacklist[actKey].insert(
                    ApePairKey{ctx.triggerSourceKeyHash, ctx.triggerActionHash});
            }
            ctx.oldKeyHashToNewKeyHashes.clear();
            ctx.previousNamingBeforeRefine = nullptr;
            ctx.previousNamingFingerprintBeforeRefine.clear();
            ctx.triggerSourceKeyHash = 0;
            ctx.triggerActionHash = 0;
            ctx.triggerTargetKeyHashes.clear();
            ctx.triggerTargetCountAtRefine = 0;
            ctx.stateCountAtLastNamingRefinement = getGraph()->getStateCountByActivity(activity);
            BLOG("ape naming: coarsen activity=%s rollback split=%d overAffected=%d overTargets=%d "
                 "overFilteredAffected=%d overFilteredTargets=%d unresolvedTriggerPair=%d "
                 "affectedOld=%zu totalNew=%zu filteredTargets=%zu triggerTargets=%zu postFanout=%zu "
                 "targetThreshold=%d fp=%s",
                 activity.c_str(), overSplit ? 1 : 0, overAffected ? 1 : 0, overTargets ? 1 : 0,
                 overFilteredAffected ? 1 : 0, overFilteredTargets ? 1 : 0, unresolvedTriggerPair ? 1 : 0,
                 affectedOldKeys, totalNewKeys.size(), filteredTargets, ctx.triggerTargetCountAtRefine,
                 postRefineMaxFanoutForAction, targetThreshold, fpFiner.c_str());
            return;
        }
    }

    void Model::runApeNamingAbstractionBatch() {
        if (Preference::inst() && Preference::inst()->useStaticReuseAbstraction()) {
            return;
        }
        const int minTargets = (_preference ? _preference->getApeNamingMinNonDetTargets()
                                            : MinNonDeterminismCount);
        for (const auto &kv : _apeNamingContext) {
            const std::string &activity = kv.first;
            const ApeNamingAbstractionContext &ctx = kv.second;
            if (!ctx.previousNamingBeforeRefine) {
                continue;
            }
            naming::NamingPtr n = _apeStateNamingManager->activityManager().getNaming(activity);
            if (!n) {
                continue;
            }
            if (ctx.previousNamingFingerprintBeforeRefine != n->fingerprintString()) {
                coarsenActivityApeNamingIfNeeded(activity);
            }
        }
        if (Preference::inst() && !Preference::inst()->useApeNamingPeriodicRefinement()) {
            return;
        }
        const std::string ruleProfile =
            (_preference ? _preference->getApeNamingActionRefineRuleProfile() : "baseline");
        auto collectNonDetPairs = [&]() -> std::vector<ApeNonDetPairStat> {
            std::vector<ApeNonDetPairStat> out;
            out.reserve(_apePairAgg.size());
            for (const auto &kv : _apePairAgg) {
                const auto &tm = kv.second.first;
                if (tm.size() < static_cast<size_t>(minTargets)) {
                    continue;
                }
                ApeNonDetPairStat s;
                s.sourceKeyHash = kv.first.sourceKeyHash;
                s.actionHash = kv.first.actionHash;
                s.sourceActivity = kv.second.second;
                for (const auto &te : tm) {
                    s.targetKeyHashes.insert(te.first);
                }
                s.targetCount = s.targetKeyHashes.size();
                out.push_back(std::move(s));
            }
            std::sort(out.begin(), out.end(), [](const ApeNonDetPairStat &a, const ApeNonDetPairStat &b) {
                if (a.targetCount != b.targetCount) return a.targetCount > b.targetCount;
                if (a.sourceActivity != b.sourceActivity) return a.sourceActivity < b.sourceActivity;
                if (a.sourceKeyHash != b.sourceKeyHash) return a.sourceKeyHash < b.sourceKeyHash;
                return a.actionHash < b.actionHash;
            });
            return out;
        };
        auto toRefinePair = [](const ApeNonDetPairStat &p) -> ApeRefinePair {
            ApeRefinePair out;
            out.sourceKeyHash = p.sourceKeyHash;
            out.actionHash = p.actionHash;
            out.targetKeyHashes = p.targetKeyHashes;
            out.targetCount = p.targetCount;
            return out;
        };
        auto countNonDetPairsPerActivity = [](const std::vector<ApeNonDetPairStat> &v) {
            std::unordered_map<std::string, int> m;
            m.reserve(std::max<size_t>(v.size(), 8) * 2);
            for (const auto &p : v) {
                const std::string k = naming::StateKey::canonicalActivityString(p.sourceActivity);
                m[k]++;
            }
            return m;
        };
        if (UsePaperRefinementOrder) {
            std::vector<ApeNonDetPairStat> nonDetPairs = collectNonDetPairs();
            const std::unordered_map<std::string, int> nonDetCountByAct = countNonDetPairsPerActivity(nonDetPairs);
            if (ruleProfile == "java_rule_02_preview" && nonDetPairs.size() > 1) {
                nonDetPairs.resize(1);
            }
            BLOG("ape naming: paper order nonDetPairs=%zu", nonDetPairs.size());
            std::unordered_set<std::string> refinedActivities;
            for (const auto &p : nonDetPairs) {
                if (refinedActivities.count(p.sourceActivity) != 0) {
                    continue;
                }
                ApeRefinePair rp = toRefinePair(p);
                BLOG("ape naming: refine-attempt activity=%s srcKey=%lu act=%lu targets=%zu",
                     p.sourceActivity.c_str(), (unsigned long)p.sourceKeyHash,
                     (unsigned long)p.actionHash, p.targetCount);
                const int preN = nonDetCountByAct.at(
                    naming::StateKey::canonicalActivityString(p.sourceActivity));
                if (refineActivityApeNaming(p.sourceActivity, &rp, preN)) {
                    coarsenActivityApeNamingIfNeeded(p.sourceActivity);
                    refinedActivities.insert(p.sourceActivity);
                }
            }
        } else {
            std::vector<ApeNonDetPairStat> nonDetPairs = collectNonDetPairs();
            const std::unordered_map<std::string, int> nonDetCountByAct = countNonDetPairsPerActivity(nonDetPairs);
            if (ruleProfile == "java_rule_02_preview" && nonDetPairs.size() > 1) {
                nonDetPairs.resize(1);
            }
            BLOG("ape naming: batch nonDetPairs=%zu", nonDetPairs.size());
            std::vector<std::string> refinedActs;
            std::unordered_set<std::string> refinedActivities;
            for (const auto &p : nonDetPairs) {
                if (refinedActivities.count(p.sourceActivity) != 0) {
                    continue;
                }
                ApeRefinePair rp = toRefinePair(p);
                BLOG("ape naming: refine-attempt activity=%s srcKey=%lu act=%lu targets=%zu",
                     p.sourceActivity.c_str(), (unsigned long)p.sourceKeyHash,
                     (unsigned long)p.actionHash, p.targetCount);
                const int preN = nonDetCountByAct.at(
                    naming::StateKey::canonicalActivityString(p.sourceActivity));
                if (refineActivityApeNaming(p.sourceActivity, &rp, preN)) {
                    refinedActs.push_back(p.sourceActivity);
                    refinedActivities.insert(p.sourceActivity);
                }
            }
            for (const auto &a : refinedActs) {
                coarsenActivityApeNamingIfNeeded(a);
            }
        }
    }

    void Model::runRefinementAndCoarseningIfScheduled() {
        if (Preference::inst() && Preference::inst()->useStaticReuseAbstraction()) {
            return;
        }
        if (_stepCountSinceLastCheck < static_cast<size_t>(RefinementCheckInterval)) return;
        BLOG("state abstraction: ape-only batch at step %zu (interval=%d)",
             _stepCountSinceLastCheck, (int)RefinementCheckInterval);
        runApeNamingAbstractionBatch();
        for (const auto &kv : _deviceIDAgentMap) {
            if (kv.second) kv.second->onStateAbstractionChanged();
        }
    }
#endif

    void Model::reportActivity(const std::string &activity) {
        if (activity.empty()) return;
        std::lock_guard<std::mutex> lock(_coverageMutex);
        _visitedActivities.insert(activity);
        _coverageStepCount++;
    }

    std::string Model::getCoverageJson() const {
        std::lock_guard<std::mutex> lock(_coverageMutex);
        nlohmann::json j;
        j["stepsCount"] = _coverageStepCount;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &a : _visitedActivities) {
            arr.push_back(a);
        }
        j["testedActivities"] = arr;
        return j.dump();
    }

    void Model::loadStateAbstractionPolicy() {
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        auto pref = Preference::inst();
        // Only meaningful when dynamic abstraction is actually used and policy persistence is enabled.
        if (!pref || pref->useStaticReuseAbstraction() || !pref->isStateAbstractionPolicyEnabled()) {
            return;
        }

        const std::string &pkg = getPackageName();
        if (pkg.empty()) {
            return;
        }

        std::string path = "/sdcard/fastbot_" + pkg + ".statekey.json";
        BLOG("state abstraction: try load policy from %s", path.c_str());
        std::ifstream in(path);
        if (!in.is_open()) {
            return;
        }

        try {
            // Basic size guard to avoid attempting to parse extremely large / corrupted files.
            in.seekg(0, std::ios::end);
            std::streamoff sz = in.tellg();
            if (sz <= 0 || sz > static_cast<std::streamoff>(1024 * 1024)) { // 1MB hard upper bound
                BLOGE("state abstraction: skip loading %s (size=%lld bytes out of bounds)", path.c_str(),
                      static_cast<long long>(sz));
                return;
            }
            in.seekg(0, std::ios::beg);

            nlohmann::json j;
            in >> j;

            if (!j.is_object()) {
                BLOGE("state abstraction: policy file %s is not a JSON object", path.c_str());
                return;
            }

            // v1 files may contain widget-key masks and coarseningBlacklist (legacy); do not apply — dynamic
            // identity is APE StateKey-only; keeping old entries would confuse debugging.
            auto itActs = j.find("activities");
            if (itActs != j.end() && itActs->is_array() && !itActs->empty()) {
                BLOG("state abstraction: %s contains legacy activities[]; ignored", path.c_str());
            }
            auto itBlk = j.find("coarseningBlacklist");
            if (itBlk != j.end() && itBlk->is_array() && !itBlk->empty()) {
                BLOG("state abstraction: %s contains legacy coarseningBlacklist; ignored", path.c_str());
            }

            BLOG("state abstraction: loaded policy metadata from %s (no widget-mask state applied)", path.c_str());
        } catch (const std::exception &ex) {
            BLOGE("state abstraction: failed to load policy from %s: %s", path.c_str(), ex.what());
        }
#else
        (void)this;
#endif
    }

    void Model::saveStateAbstractionPolicy() const {
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        auto pref = Preference::inst();
        // Only meaningful when dynamic abstraction is actually used and policy persistence is enabled.
        if (!pref || pref->useStaticReuseAbstraction() || !pref->isStateAbstractionPolicyEnabled()) {
            return;
        }

        const std::string &pkg = getPackageName();
        if (pkg.empty()) {
            return;
        }

        std::string path = "/sdcard/fastbot_" + pkg + ".statekey.json";
        std::string tmpPath = path + ".tmp";

        nlohmann::json j;
        j["version"] = 2;
        j["activities"] = nlohmann::json::array();

        try {
            std::ofstream out(tmpPath, std::ios::trunc);
            if (!out.is_open()) {
                BLOGE("state abstraction: cannot open temp policy file %s for writing", tmpPath.c_str());
                return;
            }
            out << j.dump();
            out.close();
            if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
                BLOGE("state abstraction: failed to rename policy file %s -> %s", tmpPath.c_str(), path.c_str());
            } else {
                BLOG("state abstraction: policy saved to %s", path.c_str());
            }
        } catch (const std::exception &ex) {
            BLOGE("state abstraction: failed to save policy to %s: %s", path.c_str(), ex.what());
        }
#else
        (void)this;
#endif
    }

#if DYNAMIC_STATE_ABSTRACTION_ENABLED
    void Model::logApeStateKeySnapshot(const std::string &rawActivity, const StatePtr &state,
                                       const naming::StateKey &key, const GraphPtr &graph) {
        std::ostringstream head;
        head << "ape-key: activityRaw=" << rawActivity
             << " activityKey=" << key.activity()
             << " stateHash=" << static_cast<unsigned long>(state ? state->hash() : 0U)
             << " stateKeyHash=" << static_cast<unsigned long>(key.hash())
             << " graphSize=" << static_cast<unsigned long>(graph ? graph->stateSize() : 0U)
             << " names=" << key.sortedXPaths().size();
        BDLOG("%s", head.str().c_str());
        std::ostringstream sample;
        const auto &xs = key.sortedXPaths();
        const size_t k = std::min<size_t>(3, xs.size());
        for (size_t i = 0; i < k; ++i) {
            if (i != 0) {
                sample << " | ";
            }
            sample << xs[i];
        }
        BDLOG("ape-key: sample=%s", sample.str().c_str());
    }

#endif

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML

    bool Model::buildApeStateKeyFromElementTree(const ElementPtr &element, const std::string &activity,
                                               naming::StateKey *outKey,
                                               const StatePtr &stateForDynamicApply) {
        if (!element || !outKey) {
            return false;
        }
        std::string pkg;
        std::string cls;
        naming::StateKey::splitActivityPackageClass(activity, &pkg, &cls);
        const std::string actKey = naming::StateKey::canonicalActivityString(activity);
        gui_tree::GUITreeBuildResult built = gui_tree::GUITreeBuilder::buildFromElement(element, pkg, cls);
        if (!built.tree || !built.dom) {
            const std::string xml = element->toXML();
            built = gui_tree::GUITreeBuilder::buildFromXml(xml, pkg, cls);
        }
        if (!built.tree || !built.dom) {
            return false;
        }
        naming::ActivityNamingManager &mgr = _apeStateNamingManager->activityManager();
        const int fpSteps = _preference ? _preference->getApeNamingFixedPointMaxIter() : 0;
        naming::NamingPtr naming;
        if (fpSteps > 0) {
            naming = _apeStateNamingManager->getNamingFixedPoint(actKey, *built.tree, built.dom, fpSteps);
            if (!naming) {
                return false;
            }
        } else {
            naming = mgr.getNaming(actKey);
            if (!naming) {
                naming = naming::NamingFactory::defaultRootNaming();
                if (naming) {
                    mgr.setNaming(actKey, naming);
                }
            }
            if (!naming) {
                return false;
            }
            if (!naming::NamingFactory::rebuildTree(naming, *built.tree, built.dom)) {
                return false;
            }
        }
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        auto itCtx = _apeNamingContext.find(actKey);
        if (itCtx != _apeNamingContext.end() && itCtx->second.previousNamingBeforeRefine) {
            naming::NamingPtr prevN = itCtx->second.previousNamingBeforeRefine;
            if (!naming::NamingFactory::rebuildTree(prevN, *built.tree, built.dom)) {
                return false;
            }
            naming::StateKey kOld = naming::StateKey::fromGUITree(*built.tree);
            if (!naming::NamingFactory::rebuildTree(naming, *built.tree, built.dom)) {
                return false;
            }
            naming::StateKey kNewAfter = naming::StateKey::fromGUITree(*built.tree);
            uintptr_t oldH = kOld.hash();
            uintptr_t newH = kNewAfter.hash();
            if (oldH != newH) {
                itCtx->second.oldKeyHashToNewKeyHashes[oldH].insert(newH);
            }
        }
#endif
        naming::StateKey kNew = naming::StateKey::fromGUITree(*built.tree);
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        if (stateForDynamicApply) {
            std::vector<const gui_tree::GUITreeNode *> guiPreOrder;
            collectGUITreeNodesPreOrder(built.tree->getRootNode(), &guiPreOrder);
            applyApeDynamicActionHashesToReuseState(stateForDynamicApply, guiPreOrder, kNew);
        }
#endif
        *outKey = std::move(kNew);
        return true;
    }
#endif

    void Model::recordApeStateKey(const StatePtr &state, const naming::StateKey &key) {
        if (!state) {
            return;
        }
        const uintptr_t h = state->hash();
        auto it = _ape_state_keys_by_hash.find(h);
        if (it != _ape_state_keys_by_hash.end()) {
            it->second = key;
        } else {
            _ape_state_keys_by_hash.emplace(h, key);
        }
    }

    bool Model::tryGetApeStateKey(uintptr_t stateHash, naming::StateKey *out) const {
        auto it = _ape_state_keys_by_hash.find(stateHash);
        if (it == _ape_state_keys_by_hash.end()) {
            return false;
        }
        if (out != nullptr) {
            *out = it->second;
        }
        return true;
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

}  // namespace fastbotx

#endif  // Model_CPP_