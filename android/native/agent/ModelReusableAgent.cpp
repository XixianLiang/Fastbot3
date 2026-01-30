/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Zhao Zhang, Zhengwei Lv, Jianqiang Guo, Yuhui Su
 */
#ifndef fastbotx_ModelReusableAgent_CPP_
#define fastbotx_ModelReusableAgent_CPP_

#if 0  // ModelReusableAgent temporarily disabled, use DoubleSarsaAgent

#include "ModelReusableAgent.h"
#include "Model.h"
#include <cmath>
#include "ActivityNameAction.h"
#include "../storage/ReuseModel_generated.h"
#include <iostream>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cinttypes>

namespace ModelStorageConstants {
#ifdef __ANDROID__
    constexpr const char* StoragePrefix = "/sdcard/fastbot_";
#else
    constexpr const char* StoragePrefix = "";
#endif
    constexpr const char* ModelFileExtension = ".fbm";
    constexpr const char* TempModelFileExtension = ".tmp.fbm";
}  // namespace ModelStorageConstants

namespace fastbotx {

    /**
     * @brief Constructor
     * 
     * Initializes SARSA parameters and reuse model related variables.
     * 
     * @param model Model pointer for accessing state graph info
     */
    ModelReusableAgent::ModelReusableAgent(const ModelPtr &model)
            : AbstractAgent(model), 
              _alpha(SarsaRLConstants::DefaultAlpha),  // Initial learning rate 0.25
              _epsilon(SarsaRLConstants::DefaultEpsilon),  // Initial exploration rate 0.05
              _rng(std::random_device{}()),  // Initialize random number generator with random device
              _modelSavePath(DefaultModelSavePath),  // Set model save path
              _defaultModelSavePath(DefaultModelSavePath) {  // Set default save path
        this->_algorithmType = AlgorithmType::Reuse;  // Set algorithm type to Reuse
    }

    /**
     * @brief Destructor
     * 
     * Saves reuse model and cleans up resources.
     * 
     * Note:
     * - Model should have been saved by periodic save thread (threadModelStorage)
     * - If thread not running or save failed, attempts save here as fallback
     * - Save operation may block destructor, so best to ensure periodic saves during lifetime
     */
    ModelReusableAgent::~ModelReusableAgent() {
        BLOG("save model in destruct");
        this->saveReuseModel(this->_modelSavePath);
        this->_reuseModel.clear();
    }

    /**
     * @brief Compute alpha value
     * 
     * Dynamically adjusts learning rate alpha based on current state graph's visit count.
     * More visits result in smaller learning rate (learning rate decay strategy).
     * 
     * Performance optimization:
     * - Uses weak_ptr to avoid circular references
     * - Only computes when there's a new state, avoids unnecessary computation
     */
    void ModelReusableAgent::computeAlphaValue() {
        if (nullptr != this->_newState) {
            // Get model pointer (use weak_ptr to avoid circular references)
            auto modelPtr = this->_model.lock();
            if (!modelPtr) {
                BLOGE("Model has been destroyed, cannot compute alpha value");
                return;
            }
            
            // Get state graph and read total visit count
            const GraphPtr &graphRef = modelPtr->getGraph();
            long totalVisitCount = graphRef->getTotalDistri();
            
            // Calculate alpha value based on visit count
            this->_alpha = calculateAlphaByVisitCount(totalVisitCount);
        }
    }
    
    /**
     * @brief Calculate alpha value based on visit count
     * 
     * Implements learning rate decay strategy: learning rate gradually decreases as visit count increases.
     * 
     * Decay rules (from high to low):
     * - visitCount > 250000: alpha = 0.1
     * - visitCount > 100000: alpha = 0.2
     * - visitCount > 50000:  alpha = 0.3
     * - visitCount > 20000: alpha = 0.4
     * - Otherwise: alpha = 0.5
     * 
     * Minimum alpha value is DefaultAlpha (0.25), ensures it doesn't decay to 0.
     * 
     * Performance optimization:
     * - Uses static lookup table to avoid repeated creation
     * - Uses max to ensure alpha doesn't go below minimum
     * 
     * @param visitCount Total visit count
     * @return Calculated alpha value, range [DefaultAlpha, InitialMovingAlpha]
     */
    double ModelReusableAgent::calculateAlphaByVisitCount(long visitCount) const {
        using namespace SarsaRLConstants;
        
        // Use static lookup table for better maintainability and performance
        static const std::vector<std::pair<long, double>> alphaThresholds = {
            {AlphaThreshold4, AlphaDecrement},  // 250000 -> -0.1
            {AlphaThreshold3, AlphaDecrement},  // 100000 -> -0.1
            {AlphaThreshold2, AlphaDecrement},  // 50000  -> -0.1
            {AlphaThreshold1, AlphaDecrement}  // 20000  -> -0.1
        };
        
        // Start from initial value
        double movingAlpha = InitialMovingAlpha;  // 0.5
        
        // Decrement alpha based on visit count
        for (const auto& pair : alphaThresholds) {
            long threshold = pair.first;
            double decrement = pair.second;
            if (visitCount > threshold) {
                movingAlpha -= decrement;
            }
        }
        
        // Ensure alpha is not less than minimum (DefaultAlpha = 0.25)
        return std::max(DefaultAlpha, movingAlpha);
    }

    /**
     * @brief Compute reward value of latest executed action
     * 
     * Computes reward for the most recently executed action (_previousActions.back()).
     * The reward is based on the probability that the action can reach unvisited activities
     * and the expectation value of the resulting state.
     * 
     * Reward calculation formula:
     * reward = [probabilityOfVisitingNewActivities / sqrt(visitedCount + 1)]
     *        + [getStateActionExpectationValue / sqrt(stateVisitedCount + 1)]
     * 
     * Where:
     * - probabilityOfVisitingNewActivities: Probability that action can reach unvisited activities
     *   (based on reuse model). If action not in reuse model, uses NewActionReward (1.0)
     * - getStateActionExpectationValue: Expected value of reaching unvisited activities from new state
     * - sqrt(visitedCount + 1): Square root normalization to decay reward for frequently visited actions
     * - sqrt(stateVisitedCount + 1): Square root normalization to decay reward for frequently visited states
     * 
     * Reward timing:
     * - This reward is for the action that was executed and led to _newState
     * - The action is _previousActions.back() (last executed action)
     * - This reward will be used in N-step SARSA Q-value updates
     * 
     * Performance optimization:
     * - Uses weak_ptr to avoid circular references
     * - Caches reward values to _rewardCache to avoid repeated computation
     * - Limits cache size to NStep to avoid unlimited memory growth
     * 
     * @return Reward value for the latest executed action
     */
    double ModelReusableAgent::computeRewardOfLatestAction() {
        double rewardValue = 0.0;
        
        if (nullptr != this->_newState) {
            // Update alpha value (dynamically adjusted based on visit count)
            this->computeAlphaValue();
            
            // Get model pointer
            auto modelPtr = this->_model.lock();
            if (!modelPtr) {
                BLOGE("Model has been destroyed, cannot compute reward");
                return rewardValue;
            }
            
            // Get set of visited activities (after reaching _newState)
            const GraphPtr &graphRef = modelPtr->getGraph();
            auto visitedActivities = graphRef->getVisitedActivities();
            
            // Get latest executed action (last in action history)
            // This is the action that was executed and led to _newState
            if (auto lastSelectedAction = std::dynamic_pointer_cast<ActivityStateAction>(
                    this->_previousActions.back())) {
                
                // Compute probability that action can reach unvisited activities
                rewardValue = this->probabilityOfVisitingNewActivities(lastSelectedAction,
                                                                       visitedActivities);
                
                // If action not in reuse model (new action), directly give reward
                // This encourages exploration of new actions
                if (std::abs(rewardValue - 0.0) < SarsaRLConstants::RewardEpsilon) {
                    rewardValue = SarsaRLConstants::NewActionReward;
                }
                
                // Normalize: divide by square root of visit count (more visits, more reward decay)
                // This normalization applies to all rewards to encourage exploration of less-visited actions
                rewardValue = (rewardValue / sqrt(lastSelectedAction->getVisitedCount() + 1.0));
            }
            
            // Add state expectation value (normalized)
            // This adds bonus for states that have high potential to reach unvisited activities
            rewardValue = rewardValue + (this->getStateActionExpectationValue(this->_newState,
                                                                              visitedActivities) /
                                         sqrt(this->_newState->getVisitedCount() + 1.0));
            
            BLOG("total visited " ACTIVITY_VC_STR " count is %zu", visitedActivities.size());
        }
        
        BDLOG("reuse-cov-opti action reward=%f", rewardValue);
        
        // Add reward value to cache
        // This reward corresponds to _previousActions.back() (the action that was just executed)
        // When updateQValues() is called, _rewardCache[i] will correspond to _previousActions[i]
        this->_rewardCache.emplace_back(rewardValue);
        
        // Ensure cache size doesn't exceed NStep (avoid unlimited memory growth)
        // This keeps the cache aligned with _previousActions size
        if (this->_rewardCache.size() > SarsaRLConstants::NStep) {
            this->_rewardCache.erase(this->_rewardCache.begin());
        }
        
        return rewardValue;
    }

    /**
     * @brief Compute probability that action can reach unvisited activities
     * 
     * Based on reuse model, computes the ratio of unvisited activity visit counts
     * to total visit counts among activities that this action can reach.
     * 
     * Calculation process:
     * 1. Find action in reuse model (by hash)
     * 2. Iterate through all activities this action can reach and their visit counts
     * 3. Count total visit counts and unvisited activity visit counts
     * 4. Calculate probability = unvisited activity visit counts / total visit counts
     * 
     * Performance optimization:
     * - Uses mutex to protect concurrent access to reuse model
     * - Uses map's find operation (O(log n)) for fast lookup
     * - Uses set's count operation (O(log n)) for fast check if visited
     * 
     * @param action Action to evaluate
     * @param visitedActivities Set of visited activities
     * @return Probability value, range [0,1]. Returns 0 if action not in reuse model or no unvisited activities
     */
    double
    ModelReusableAgent::probabilityOfVisitingNewActivities(const ActivityStateActionPtr &action,
                                                           const stringPtrSet &visitedActivities) const {
        double value = 0.0;
        int total = 0;
        int unvisited = 0;
        
        // Lock to protect concurrent access to reuse model
        std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
        
        // Find action in reuse model (by hash value)
        auto actionMapIterator = this->_reuseModel.find(action->hash());
        
        if (actionMapIterator != this->_reuseModel.end()) {
            // Iterate through all activities this action can reach and their visit counts
            for (const auto &activityCountMapIterator: actionMapIterator->second) {
                total += activityCountMapIterator.second;  // Accumulate total visit counts
                stringPtr activity = activityCountMapIterator.first;
                
                // Check if this activity is unvisited
                if (visitedActivities.count(activity) == 0) {
                    unvisited += activityCountMapIterator.second;  // Accumulate unvisited activity visit counts
                }
            }
            
            // Calculate probability: unvisited activity visit counts / total visit counts
            if (total > 0 && unvisited > 0) {
                value = static_cast<double>(unvisited) / total;
            }
        }
        
        return value;
    }

    /**
     * @brief Check if action is in reuse model
     * 
     * Performance optimization:
     * - Uses mutex to protect concurrent access
     * - Uses map's count operation (O(log n)) for fast lookup
     * 
     * @param actionHash Action's hash value
     * @return true if in reuse model, false otherwise
     */
    bool ModelReusableAgent::isActionInReuseModel(uintptr_t actionHash) const {
        std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
        return this->_reuseModel.count(actionHash) > 0;
    }

    /**
     * @brief Compute state action expectation value
     * 
     * Evaluates expected value of reaching unvisited activities after executing actions from this state.
     * 
     * Calculation rules:
     * 1. For new actions (not in reuse model): add NewActionInStateReward (1.0)
     * 2. For visited actions (visitedCount >= 1): add VisitedActionReward (0.5)
     * 3. For target actions: add probabilityOfVisitingNewActivities (probability of reaching unvisited activities)
     * 
     * Performance optimization:
     * - Caches isActionInReuseModel results to avoid repeated lookups
     * - Early check of getTarget() to avoid unnecessary computation
     * 
     * @param state State to evaluate
     * @param visitedActivities Set of visited activities (after reaching this state)
     * @return Expectation value, larger value means state is more likely to reach unvisited activities
     */
    double ModelReusableAgent::getStateActionExpectationValue(const StatePtr &state,
                                                              const stringPtrSet &visitedActivities) const {
        double value = 0.0;
        
        // Iterate through all actions in state
        for (const auto &action: state->getActions()) {
            uintptr_t actionHash = action->hash();
            
            // If action not in reuse model (new action), add reward
            if (!this->isActionInReuseModel(actionHash)) {
                value += SarsaRLConstants::NewActionInStateReward;  // +1.0
            }
            // If action already visited (executed in current test), add smaller reward
            else if (action->getVisitedCount() >= 1) {
                value += SarsaRLConstants::VisitedActionReward;  // +0.5
            }

            // For target actions, add probability of reaching unvisited activities
            // Note: Does not consider no-target actions like BACK
            if (action->getTarget() != nullptr) {
                value += probabilityOfVisitingNewActivities(action, visitedActivities);
            }
        }
        
        return value;
    }

    /**
     * @brief Get action's Q-value
     * 
     * @param action Action pointer
     * @return Q-value (quality value)
     */
    double ModelReusableAgent::getQValue(const ActionPtr &action) {
        return action->getQValue();
    }

    /**
     * @brief Set action's Q-value
     * 
     * @param action Action pointer
     * @param qValue Q-value (quality value)
     */
    void ModelReusableAgent::setQValue(const ActionPtr &action, double qValue) {
        action->setQValue(qValue);
    }

    /**
     * @brief Update strategy
     * 
     * Implements AbstractAgent's pure virtual function.
     * Called after action execution, performs the following operations:
     * 1. Compute reward value of latest executed action (_previousActions.back())
     * 2. Update reuse model (record action-to-activity mapping)
     * 3. Update Q-values using modified N-step SARSA algorithm
     * 4. Add new action (_newAction) to action history cache
     * 
     * Execution flow:
     * - At start: _previousActions contains executed actions, _rewardCache contains their rewards
     * - Step 1: Compute reward for _previousActions.back(), add to _rewardCache
     * - Step 2: Update reuse model with latest action-activity mapping
     * - Step 3: Update Q-values (only oldest action is updated, see updateQValues() for details)
     * - Step 4: Add _newAction to _previousActions (it will be executed next)
     * 
     * Note: Only executes update when there's a new action (_newAction != nullptr).
     */
    void ModelReusableAgent::updateStrategy() {
        // If no new action, return directly (need to call resolveNewAction first)
        if (nullptr == this->_newAction) {
            return;
        }
        
        // If action history is not empty, execute update operations
        // _previousActions stores recent NStep executed actions
        if (!this->_previousActions.empty()) {
            // 1. Compute reward value of latest executed action (_previousActions.back())
            // This reward is for the action that was just executed and led to _newState
            this->computeRewardOfLatestAction();
            
            // 2. Update reuse model (record action-to-activity mapping)
            // Records that the latest executed action led to the activity in _newState
            this->updateReuseModel();
            
            // 3. Update Q-values using modified N-step SARSA algorithm
            // Only updates the oldest action in the history (see updateQValues() for details)
            this->updateQValues();
        } else {
            BDLOG("%s", "get action value failed!");
        }
        
        // 4. Add new action to end of action history cache
        // This action will be executed in the next step
        this->_previousActions.emplace_back(this->_newAction);
        
        // Ensure cache size doesn't exceed NStep (avoid unlimited memory growth)
        if (this->_previousActions.size() > SarsaRLConstants::NStep) {
            // If exceeds NStep, remove oldest action (first one)
            // Also need to remove corresponding reward (handled in computeRewardOfLatestAction)
            this->_previousActions.erase(this->_previousActions.begin());
        }
    }
    
    /**
     * @brief Update Q-values using standard N-step SARSA algorithm
     * 
     * Implements standard N-step SARSA algorithm that updates ALL actions within the N-step window.
     * 
     * Standard N-step SARSA formula:
     *   For each action a_τ in the window:
     *     G_τ^(n) = R_{τ+1} + γR_{τ+2} + γ²R_{τ+3} + ... + γ^(n-1)R_{τ+n} + γ^n Q(S_{τ+n}, A_{τ+n})
     *     Q(s_τ, a_τ) ← Q(s_τ, a_τ) + α(G_τ^(n) - Q(s_τ, a_τ))
     * 
     * Algorithm description:
     * - Starts from latest action (_newAction), accumulates rewards and Q-values backward
     * - For each action i in the window, computes n-step return from that action
     * - Updates Q-value for ALL actions in the window (standard N-step SARSA behavior)
     * 
     * Implementation details:
     * - Traverses from back to front to efficiently compute n-step returns
     * - For action i: n-step return = R_i + γR_{i+1} + γ²R_{i+2} + ... + γ^(n-1)R_{i+n-1} + γ^n Q(s_{i+n}, a_{i+n})
     * - If window size < NStep, uses available steps (partial n-step return)
     * - _newAction provides Q(s_{i+n}, a_{i+n}) for bootstrapping when i+n is beyond window
     * 
     * Reward-Action Mapping:
     * - _rewardCache[i] stores reward for _previousActions[i] (when arrays are aligned)
     * - At updateQValues() call time:
     *   * _previousActions contains executed actions (size = n, where n <= NStep)
     *   * _rewardCache contains rewards for those actions (size = n, after computeRewardOfLatestAction adds one)
     *   * _newAction is the newly selected action (not yet executed, provides Q(s_n, a_n))
     * 
     * Where:
     * - R: Reward received after executing action
     * - gamma: Discount factor (DefaultGamma = 0.8)
     * - alpha: Learning rate (dynamically adjusted)
     * - n: Window size (number of actions in _previousActions, up to NStep)
     * 
     * Performance optimization:
     * - Traverses from back to front to avoid repeated computation of accumulated rewards
     * - Each n-step return is computed incrementally using previously computed values
     * 
     * @note This implementation follows standard N-step SARSA by updating all actions in the window,
     *       which provides better learning efficiency compared to only updating the oldest action.
     */
    void ModelReusableAgent::updateQValues() {
        using namespace SarsaRLConstants;
        
        // Validate array sizes match
        if (this->_previousActions.empty()) {
            return;
        }
        
        // Check if reward cache has enough entries
        // After computeRewardOfLatestAction(), _rewardCache should have same size as _previousActions
        if (this->_rewardCache.size() < this->_previousActions.size()) {
            BLOGE("Reward cache size (%zu) is smaller than action history size (%zu)", 
                  this->_rewardCache.size(), this->_previousActions.size());
            return;
        }
        
        // Start from latest action's Q-value (_newAction is the next action to be executed)
        // This represents Q(s_{n}, a_{n}) in the n-step return formula, where n is window size
        // For standard N-step SARSA, this provides the bootstrapped Q-value for n steps ahead
        double value = getQValue(_newAction);
        
        // Get window size (number of actions in history, up to NStep)
        int windowSize = static_cast<int>(this->_previousActions.size());
        
        // Traverse action history from back to front (from latest to oldest)
        // For each action i, compute n-step return: R_i + γR_{i+1} + ... + γ^(n-1)R_{i+n-1} + γ^n Q(s_{i+n}, a_{i+n})
        // where n is the number of steps from i to the end of window (or NStep, whichever is smaller)
        for (int i = windowSize - 1; i >= 0; i--) {
            // Safety check: ensure index is valid
            if (i >= static_cast<int>(this->_rewardCache.size())) {
                BLOGE("Index %d out of bounds for reward cache (size %zu)", i, this->_rewardCache.size());
                break;
            }
            
            double currentQValue = getQValue(_previousActions[i]);
            // _rewardCache[i] stores reward for _previousActions[i]
            double currentRewardValue = this->_rewardCache[i];
            
            // Accumulated n-step return: current reward + discount factor * future return
            // This builds: R_i + γ * (R_{i+1} + γ * (R_{i+2} + ... + γ^(k-1)R_{i+k-1} + γ^k Q(s_{i+k}, a_{i+k})))
            // where k is the number of steps from i+1 to the end of window
            // At this point, 'value' contains the return from i+1 to the end of window
            // Adding R_i and multiplying by gamma gives the return from i to the end
            value = currentRewardValue + DefaultGamma * value;
            
            // Update Q-value for this action using n-step return
            // Standard N-step SARSA updates ALL actions in the window (not just the oldest)
            // The n-step return 'value' represents: R_i + γR_{i+1} + ... + γ^(k-1)R_{i+k-1} + γ^k Q(s_{i+k}, a_{i+k})
            // where k is the number of steps from i+1 to the end of window
            // If window size < NStep, this is a partial n-step return (still valid for learning)
            // Q-value update: Q(s,a) = Q(s,a) + alpha * (target value - Q(s,a))
            // where target value is the n-step return from this action
            setQValue(this->_previousActions[i],
                      currentQValue + this->_alpha * (value - currentQValue));
        }
    }

    /**
     * @brief Update reuse model
     * 
     * Records latest executed action and the activity it reached.
     * 
     * Update logic:
     * 1. Get latest executed action (last in action history)
     * 2. Get activity reached by this action (from _newState)
     * 3. Find action in reuse model:
     *    - If not exists, create new entry: action hash -> (Activity -> 1)
     *    - If exists, increment this activity's visit count
     * 4. Update action's Q-value
     * 
     * Performance optimization:
     * - Uses mutex to protect concurrent access to reuse model
     * - Uses map's find operation (O(log n)) for fast lookup
     * - Uses emplace to avoid unnecessary copies
     */
    void ModelReusableAgent::updateReuseModel() {
        // If action history is empty, return directly
        if (this->_previousActions.empty()) {
            return;
        }
        
        // Get latest executed action
        ActionPtr lastAction = this->_previousActions.back();
        
        // Only process ActivityNameAction type actions
        if (auto modelAction = std::dynamic_pointer_cast<ActivityNameAction>(lastAction)) {
            // If new state is null, cannot get activity info
            if (nullptr == this->_newState) {
                return;
            }
            
            // Get action hash and reached activity
            auto hash = static_cast<uint64_t>(modelAction->hash());
            stringPtr activity = this->_newState->getActivityString();  // Use _newState as action's target
            
            if (activity == nullptr) {
                return;
            }
            
            // Lock to protect concurrent access to reuse model
            {
                std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
                
                // Find action in reuse model
                auto iter = this->_reuseModel.find(hash);
                
                if (iter == this->_reuseModel.end()) {
                    // Action not in reuse model, create new entry
                    BDLOG("can not find action %s in reuse map", modelAction->getId().c_str());
                    ReuseEntryM entryMap;
                    entryMap.emplace(activity, 1);  // Initialize activity visit count to 1
                    this->_reuseModel[hash] = entryMap;
                } else {
                    // Action in reuse model, increment this activity's visit count
                    iter->second[activity] += 1;
                }
                
                // Update action's Q-value
                this->_reuseQValue[hash] = modelAction->getQValue();
            }
        }
    }

    /**
     * @brief Select action using epsilon-greedy strategy
     * 
     * With (1-epsilon) probability selects action with highest Q-value (greedy strategy),
     * with epsilon probability randomly selects action (exploration strategy).
     * 
     * Strategy description:
     * - Greedy strategy: Select action with highest Q-value, exploit existing knowledge
     * - Random strategy: Randomly select action, explore unknown areas
     * 
     * @return Selected action
     */
    ActivityStateActionPtr ModelReusableAgent::selectNewActionEpsilonGreedyRandomly() const {
        if (this->eGreedy()) {
            // Use greedy strategy: select action with highest Q-value
            BDLOG("%s", "Try to select the max value action");
            return this->_newState->greedyPickMaxQValue(enableValidValuePriorityFilter);
        }
        
        // Use random strategy: randomly select action
        BDLOG("%s", "Try to randomly select a value action.");
        return this->_newState->randomPickAction(enableValidValuePriorityFilter);
    }

    /**
     * @brief Determine whether to use greedy strategy
     * 
     * Generates random number in range [0,1), if >= epsilon uses greedy strategy, otherwise uses random strategy.
     * 
     * Performance optimization:
     * - Uses member random number generator to avoid creating new generator each time
     * - Uses uniform_real_distribution, performant and thread-safe
     * 
     * @return true means use greedy strategy (select action with highest Q-value), false means use random strategy
     */
    bool ModelReusableAgent::eGreedy() const {
        // Use member random number generator for better performance and thread safety
        return _uniformDist(_rng) >= this->_epsilon;
    }


    /**
     * @brief Select new action (implements AbstractAgent's pure virtual function)
     * 
     * Attempts to select action in priority order, priority from high to low:
     * 
     * 1. **Select unexecuted actions not in reuse model**
     *    - Prioritize exploring new actions that may reach unvisited activities
     * 
     * 2. **Select unvisited unexecuted actions in reuse model**
     *    - Based on humble-gumbel distribution, select action with maximum quality value
     *    - Quality value = probability of reaching unvisited activities
     * 
     * 3. **Select unvisited actions**
     *    - If previous two steps fail, try to select any unvisited action
     * 
     * 4. **Select actions based on Q-values**
     *    - If all actions are visited, use Q-values and humble-gumbel distribution
     *    - Add randomness to avoid always selecting same action
     * 
     * 5. **Epsilon-greedy strategy**
     *    - Final fallback, use traditional epsilon-greedy strategy
     * 
     * 6. **Handle null action**
     *    - If all methods fail, call handleNullAction() to attempt recovery
     * 
     * @return Selected action, or nullptr on failure
     */
    ActionPtr ModelReusableAgent::selectNewAction() {
        ActionPtr action = nullptr;
        
        // Strategy 1: Select unexecuted actions not in reuse model (highest priority)
        action = this->selectUnperformedActionNotInReuseModel();
        if (nullptr != action) {
            BLOG("%s", "select action not in reuse model");
            return action;
        }

        // Strategy 2: Select unvisited unexecuted actions in reuse model
        action = this->selectUnperformedActionInReuseModel();
        if (nullptr != action) {
            BLOG("%s", "select action in reuse model");
            return action;
        }

        // Strategy 3: Select unvisited actions
        action = this->_newState->randomPickUnvisitedAction();
        if (nullptr != action) {
            BLOG("%s", "select action in unvisited action");
            return action;
        }

        // Strategy 4: If all actions are visited, select actions based on Q-values
        // Use humble-gumbel distribution to add randomness, avoid always selecting same action
        action = this->selectActionByQValue();
        if (nullptr != action) {
            BLOG("%s", "select action by qvalue");
            return action;
        }

        // Strategy 5: Use traditional epsilon-greedy strategy
        action = this->selectNewActionEpsilonGreedyRandomly();
        if (nullptr != action) {
            BLOG("%s", "select action by EpsilonGreedyRandom");
            return action;
        }
        
        // Strategy 6: All methods failed, attempt to handle null action
        BLOGE("null action happened , handle null action");
        return handleNullAction();
    }

    /**
     * @brief Select unexecuted action not in reuse model
     * 
     * Selects actions from current state that satisfy the following conditions:
     * 1. Is a model action (CLICK, LONG_CLICK, SCROLL, etc., excluding BACK, FEED)
     * 2. Not in reuse model (new action)
     * 3. Not executed (visitedCount <= 0)
     * 
     * Uses weighted random selection, weights are action priorities.
     * 
     * Performance optimization:
     * - Uses cumulative distribution function (CDF) and binary search, time complexity O(n log n)
     * - First collects candidate actions, then selects once, avoids repeated traversal
     * 
     * @return Selected action, or nullptr if none
     */
    ActionPtr ModelReusableAgent::selectUnperformedActionNotInReuseModel() const {
        // Step 1: Collect all unexecuted actions not in reuse model
        std::vector<ActionPtr> actionsNotInModel;
        for (const auto &action: this->_newState->getActions()) {
            // Check if action satisfies conditions:
            // 1. Is a model action (CLICK, SCROLL, etc.)
            // 2. Not in reuse model
            // 3. Not executed
            bool matched = action->isModelAct()  // Must be model action
                           && !this->isActionInReuseModel(action->hash())  // Not in reuse model
                           && action->getVisitedCount() <= 0;  // Not executed
            
            if (matched) {
                actionsNotInModel.emplace_back(action);
            }
        }
        
        // If no candidate actions, return nullptr
        if (actionsNotInModel.empty()) {
            BDLOGE("%s", " no actions not in model");
            return nullptr;
        }
        
        // Step 2: Build cumulative weight array (for weighted random selection)
        std::vector<int> cumulativeWeights;
        int totalWeight = 0;
        for (const auto &action: actionsNotInModel) {
            totalWeight += action->getPriority();
            cumulativeWeights.push_back(totalWeight);
        }
        
        // If total weight is 0, return nullptr
        if (totalWeight <= 0) {
            BDLOGE("%s", " total weights is 0");
            return nullptr;
        }
        
        // Step 3: Use binary search to select action (O(log n))
        int randI = randomInt(0, totalWeight);  // Generate random number in [0, totalWeight)
        auto it = std::lower_bound(cumulativeWeights.begin(), 
                                  cumulativeWeights.end(), randI);
        size_t index = std::distance(cumulativeWeights.begin(), it);
        
        // Return selected action
        if (index < actionsNotInModel.size()) {
            return actionsNotInModel[index];
        }
        
        BDLOGE("%s", " rand a null action");
        return nullptr;
    }

    /**
     * @brief Select unvisited unexecuted action in reuse model
     * 
     * Uses humble-gumbel distribution to influence sampling, selects action with maximum quality value.
     * 
     * Quality value calculation:
     * qualityValue = probabilityOfVisitingNewActivities * QualityValueMultiplier - log(-log(uniform))
     * 
     * Where:
     * - probabilityOfVisitingNewActivities: Probability that action can reach unvisited activities
     * - QualityValueMultiplier: Quality value multiplier (10.0)
     * - log(-log(uniform)): Humble-gumbel distribution random term, adds exploration
     * 
     * Selection conditions:
     * 1. Action is in reuse model
     * 2. Action not executed (visitedCount <= 0)
     * 3. Quality value greater than threshold (QualityValueThreshold = 1e-4)
     * 
     * Performance optimization:
     * - Caches visitedActivities to avoid repeated queries in loop
     * - Uses member random number generator for better performance
     * - Only processes target actions (targetActions), excludes BACK/FEED, etc.
     * 
     * @return Selected action, or nullptr if none
     */
    ActionPtr ModelReusableAgent::selectUnperformedActionInReuseModel() const {
        float maxValue = -MAXFLOAT;
        ActionPtr nextAction = nullptr;
        
        // Cache visitedActivities to avoid repeated queries in loop (performance optimization)
        auto modelPointer = this->_model.lock();
        stringPtrSet visitedActivities;
        if (modelPointer) {
            const GraphPtr &graphRef = modelPointer->getGraph();
            visitedActivities = graphRef->getVisitedActivities();
        } else {
            return nullptr;
        }
        
        // Use humble-gumbel distribution to influence sampling
        // Only process target actions (CLICK, SCROLL, etc.), exclude BACK/FEED/EVENT_SHELL
        for (const auto &action: this->_newState->targetActions()) {
            uintptr_t actionHash = action->hash();
            
            // Check if action is in reuse model
            if (this->isActionInReuseModel(actionHash)) {
                // If action already executed, skip
                if (action->getVisitedCount() > 0) {
                    BDLOG("%s", "action has been visited");
                    continue;
                }
                
                // Compute probability that action can reach unvisited activities (quality value)
                auto qualityValue = static_cast<float>(this->probabilityOfVisitingNewActivities(
                        action,
                        visitedActivities));
                
                // Only consider if quality value greater than threshold
                if (qualityValue > SarsaRLConstants::QualityValueThreshold) {
                    // Amplify quality value
                    qualityValue = SarsaRLConstants::QualityValueMultiplier * qualityValue;
                    
                    // Use member random number generator to generate random number (performance optimization)
                    auto uniform = _uniformFloatDist(_rng);
                    
                    // Ensure random number is not 0 to avoid log(0) causing INF
                    if (uniform < std::numeric_limits<float>::min()) {
                        uniform = std::numeric_limits<float>::min();
                    }
                    
                    // Add humble-gumbel distribution random term: -log(-log(uniform))
                    // This adds exploration to quality value, avoids always selecting same action
                    qualityValue -= log(-log(uniform));

                    // Select action with maximum quality value
                    if (qualityValue > maxValue) {
                        maxValue = qualityValue;
                        nextAction = action;
                    }
                }
            }
        }
        
        return nextAction;
    }

    /**
     * @brief Select action based on Q-value
     * 
     * Uses humble-gumbel distribution to add randomness, selects action with maximum adjusted Q-value.
     * 
     * Adjusted Q-value calculation:
     * adjustedQ = (Q + probabilityOfVisitingNewActivities) / EntropyAlpha - log(-log(uniform))
     * 
     * Where:
     * - Q: Action's Q-value (quality value)
     * - probabilityOfVisitingNewActivities: Probability that action can reach unvisited activities
     * - EntropyAlpha: Entropy alpha value (0.1), used for normalization
     * - log(-log(uniform)): Humble-gumbel distribution random term, adds exploration
     * 
     * Special handling:
     * - If action unvisited and not in reuse model, directly return this action (highest priority)
     * - If action unvisited but in reuse model, add probabilityOfVisitingNewActivities
     * 
     * Performance optimization:
     * - Caches visitedActivities to avoid repeated queries
     * - Uses member random number generator for better performance
     * 
     * @return Selected action, or nullptr if none
     */
    ActionPtr ModelReusableAgent::selectActionByQValue() {
        ActionPtr returnAction = nullptr;
        float maxQ = -MAXFLOAT;
        
        // Get model pointer and set of visited activities
        auto modelPtr = this->_model.lock();
        if (!modelPtr) {
            BLOGE("Model has been destroyed, cannot select action by Q value");
            return nullptr;
        }
        
        const GraphPtr &graphRef = modelPtr->getGraph();
        auto visitedActivities = graphRef->getVisitedActivities();
        
        // Iterate through all actions, select action with maximum adjusted Q-value
        for (const auto &action: this->_newState->getActions()) {
            double qv = 0.0;
            uintptr_t actionHash = action->hash();
            
            // If action unvisited
            if (action->getVisitedCount() <= 0) {
                if (this->isActionInReuseModel(actionHash)) {
                    // In reuse model, add probability of reaching unvisited activities
                    qv += this->probabilityOfVisitingNewActivities(action, visitedActivities);
                } else {
                    // Not in reuse model (new action), directly return (highest priority)
                    BDLOG("qvalue pick return a action: %s", action->toString().c_str());
                    return action;
                }
            }
            
            // Add action's Q-value
            qv += getQValue(action);
            
            // Divide by EntropyAlpha for normalization
            qv /= SarsaRLConstants::EntropyAlpha;
            
            // Use member random number generator to generate random number (performance optimization)
            float uniform = _uniformFloatDist(_rng);
            
            // Ensure random number is not 0 to avoid log(0) causing INF
            if (uniform < std::numeric_limits<float>::min()) {
                uniform = std::numeric_limits<float>::min();
            }
            
            // Add humble-gumbel distribution random term: -log(-log(uniform))
            // This adds exploration to Q-value, avoids always selecting same action
            qv -= log(-log(uniform));
            
            // Select action with maximum adjusted Q-value
            if (qv > maxQ) {
                maxQ = static_cast<float>(qv);
                returnAction = action;
            }
        }
        
        return returnAction;
    }

    /**
     * @brief Adjust action priorities (overrides parent class method)
     * 
     * First calls parent class's adjustActions() for basic priority adjustment,
     * then can add additional priority adjustment logic (currently not implemented).
     */
    void ModelReusableAgent::adjustActions() {
        AbstractAgent::adjustActions();
    }

    /**
     * @brief Model storage background thread function
     * 
     * Periodically saves reuse model to file system to prevent data loss.
     * 
     * Work flow:
     * 1. Saves model every 10 minutes (ModelSaveIntervalMs)
     * 2. Uses weak_ptr to avoid circular references, automatically exits when Agent is destructed
     * 3. Briefly locks Agent before saving to get save path, then releases lock
     * 4. Executes save operation outside lock to avoid holding lock for long time
     * 
     * Performance optimization:
     * - Uses weak_ptr to avoid circular references and resource leaks
     * - Briefly locks to get path then immediately releases, reduces lock hold time
     * - Executes IO operations outside lock to avoid blocking other threads
     * 
     * @param agent Agent's weak_ptr to avoid circular references
     */
    void ModelReusableAgent::threadModelStorage(const std::weak_ptr<ModelReusableAgent> &agent) {
        constexpr int saveInterval = SarsaRLConstants::ModelSaveIntervalMs;  // 10 minutes
        constexpr auto interval = std::chrono::milliseconds(saveInterval);
        
        // Loop to save until Agent is destructed (weak_ptr becomes invalid)
        while (true) {
            // Briefly lock Agent to get save path
            auto agentPtr = agent.lock();
            if (!agentPtr) {
                // Agent has been destructed, exit thread
                break;
            }
            
            // Copy save path
            std::string savePath = agentPtr->_modelSavePath;
            
            // Immediately release lock to avoid holding for long time
            agentPtr.reset();
            
            // Save model outside lock (IO operations may be slow)
            if (auto locked = agent.lock()) {
                locked->saveReuseModel(savePath);
            }
            
            // Wait for specified interval before saving again
            std::this_thread::sleep_for(interval);
        }
    }

    /**
     * @brief Load reuse model
     * 
     * Loads previously saved reuse model from file system.
     * Model file uses FlatBuffers serialization, file format: /sdcard/fastbot_{packageName}.fbm
     * 
     * Loading flow:
     * 1. Build model file path
     * 2. Open file and check file size
     * 3. Read file content to memory
     * 4. Deserialize using FlatBuffers
     * 5. Convert data to std::map format and load to memory
     * 
     * Performance optimization:
     * - Uses smart pointers to manage memory, exception safe
     * - Checks file size to prevent loading overly large files (max 100MB)
     * - Uses mutex to protect concurrent access to reuse model
     * - Uses emplace to avoid unnecessary copies
     * 
     * Error handling:
     * - File not exists: Log error and return (normal for first run)
     * - Invalid file size: Log error and return
     * - Read failure: Log error and return
     * 
     * @param packageName Application package name, used to construct model file path
     */
    void ModelReusableAgent::loadReuseModel(const std::string &packageName) {
        // Build model file path: /sdcard/fastbot_{packageName}.fbm
        std::string modelFilePath = std::string(ModelStorageConstants::StoragePrefix) + 
                                    packageName + ModelStorageConstants::ModelFileExtension;

        // Set model save path
        this->_modelSavePath = modelFilePath;
        
        // Set default save path (temporary file)
        if (!this->_modelSavePath.empty()) {
            this->_defaultModelSavePath = std::string(ModelStorageConstants::StoragePrefix) + 
                                         packageName + ModelStorageConstants::TempModelFileExtension;
        }
        
        BLOG("begin load model: %s", this->_modelSavePath.c_str());

        // Open model file (binary mode)
        std::ifstream modelFile(modelFilePath, std::ios::binary | std::ios::in);
        if (!modelFile.is_open()) {
            BLOGE("Failed to open model file: %s", modelFilePath.c_str());
            return;  // File not exists is normal (first run), return directly
        }

        // Get file size
        std::filebuf *fileBuffer = modelFile.rdbuf();
        // Move file pointer to end to get file size
        std::size_t filesize = fileBuffer->pubseekoff(0, modelFile.end, modelFile.in);
        // Reset file pointer to beginning
        fileBuffer->pubseekpos(0, modelFile.in);
        
        // Check if file size is valid (prevent loading overly large files)
        if (filesize <= 0 || filesize > SarsaRLConstants::MaxModelFileSize) {
            BLOGE("Invalid model file size: %zu", filesize);
            return;
        }
        
        // Use smart pointer to manage memory, exception safe
        std::unique_ptr<char[]> modelFileData(new char[filesize]);
        
        // Read file content to memory
        std::streamsize bytesRead = fileBuffer->sgetn(modelFileData.get(), static_cast<int>(filesize));
        
        // Check if successfully read complete file
        if (bytesRead != static_cast<std::streamsize>(filesize)) {
            BLOGE("Failed to read complete model file: read %lld bytes, expected %zu bytes", 
                  static_cast<long long>(bytesRead), filesize);
            return;
        }
        
        // Deserialize model data using FlatBuffers
        auto reuseFBModel = GetReuseModel(modelFileData.get());

        // Clear existing reuse model (lock protected)
        {
            std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
            this->_reuseModel.clear();
            this->_reuseQValue.clear();
        }
        
        // Get model data pointer
        auto reusedModelDataPtr = reuseFBModel->model();
        if (!reusedModelDataPtr) {
            BLOG("%s", "model data is null");
            return;
        }
        
        // Iterate through all reuse entries, load to memory
        for (flatbuffers::uoffset_t entryIndex = 0; entryIndex < reusedModelDataPtr->size(); entryIndex++) {
            auto reuseEntryInReuseModel = reusedModelDataPtr->Get(entryIndex);
            uint64_t actionHash = reuseEntryInReuseModel->action();
            auto activityEntry = reuseEntryInReuseModel->targets();
            
            // Build activity mapping: Activity name -> visit count
            ReuseEntryM entryPtr;
            for (flatbuffers::uoffset_t targetIndex = 0; targetIndex < activityEntry->size(); targetIndex++) {
                auto targetEntry = activityEntry->Get(targetIndex);
                BDLOG("load model hash: %" PRIu64 " %s %d", actionHash,
                      targetEntry->activity()->str().c_str(), static_cast<int>(targetEntry->times()));
                
                // Use emplace to avoid unnecessary copies
                entryPtr.emplace(
                        std::make_shared<std::string>(targetEntry->activity()->str()),
                        static_cast<int>(targetEntry->times()));
            }
            
            // If entry not empty, add to reuse model (lock protected)
            if (!entryPtr.empty()) {
                std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
                // Note: Q-values currently not loaded from file (commented code)
                // this->_reuseQValue.insert(std::make_pair(actionHash, reuseEntryInReuseModel->quality()));
                this->_reuseModel.emplace(actionHash, entryPtr);
            }
        }
        
        BLOG("loaded model contains actions: %zu", this->_reuseModel.size());
        // modelFileData will be automatically freed when unique_ptr is destructed
    }

    /// Static default model save path
    std::string ModelReusableAgent::DefaultModelSavePath = "/sdcard/fastbot.model.fbm";

    /**
     * @brief Save reuse model
     * 
     * Serializes and saves current reuse model to file system.
     * Uses FlatBuffers for serialization, uses temporary file + atomic replace to ensure data integrity.
     * 
     * Saving flow:
     * 1. Lock to protect reuse model, build FlatBuffers data structure
     * 2. Serialize data
     * 3. Write to temporary file
     * 4. Atomically replace original file (using rename operation)
     * 
     * Data integrity guarantee:
     * - First write to temporary file, then atomically replace original file after success
     * - If write fails, delete temporary file, does not affect original file
     * - Rename operation is atomic, avoids file corruption
     * 
     * Performance optimization:
     * - Uses mutex to protect concurrent access to reuse model
     * - Uses FlatBuffers zero-copy serialization, performant
     * - Uses temporary file + atomic replace to avoid file corruption during write
     * 
     * Error handling:
     * - Path empty: Log error and return
     * - File open failure: Log error and return
     * - Write failure: Delete temporary file and return
     * - Rename failure: Delete temporary file and return
     * 
     * @param modelFilepath Model file path, uses _defaultModelSavePath if empty
     */
    void ModelReusableAgent::saveReuseModel(const std::string &modelFilepath) {
        // Create FlatBuffers builder
        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<fastbotx::ReuseEntry>> actionActivityVector;
        
        // Lock to protect concurrent access to reuse model
        {
            std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
            
            // Iterate through reuse model, build FlatBuffers data structure
            for (const auto &actionIterator: this->_reuseModel) {
                uint64_t actionHash = actionIterator.first;
                ReuseEntryM activityCountEntryMap = actionIterator.second;
                
                // FlatBuffers needs vector instead of map, so need to convert
                std::vector<flatbuffers::Offset<fastbotx::ActivityTimes>> activityCountEntryVector;
                
                // Iterate through activity mapping, build ActivityTimes objects
                for (const auto &activityCountEntry: activityCountEntryMap) {
                    auto sentryActT = CreateActivityTimes(
                            builder, 
                            builder.CreateString(*(activityCountEntry.first)),  // Activity name
                            activityCountEntry.second);  // Visit count
                    activityCountEntryVector.push_back(sentryActT);
                }
                
                // Create ReuseEntry object
                auto savedActivityCountEntries = CreateReuseEntry(
                        builder, 
                        actionHash,
                        builder.CreateVector(activityCountEntryVector.data(),
                                          activityCountEntryVector.size()));
                actionActivityVector.push_back(savedActivityCountEntries);
            }
        }  // Release lock
        
        // Create ReuseModel root object and complete serialization
        auto savedActionActivityEntries = CreateReuseModel(
                builder, 
                builder.CreateVector(actionActivityVector.data(), actionActivityVector.size()));
        builder.Finish(savedActionActivityEntries);

        // Determine output file path
        std::string outputFilePath = modelFilepath;
        if (outputFilePath.empty()) {
            // If passed path is empty, use default save path
            outputFilePath = this->_defaultModelSavePath;
        }
        
        if (outputFilePath.empty()) {
            BLOGE("Cannot save model: output file path is empty");
            return;
        }
        
        // First write to temporary file (ensure data integrity)
        std::string tempFilePath = outputFilePath + ".tmp";
        BLOG("save model to temporary path: %s", tempFilePath.c_str());
        
        std::ofstream outputFile(tempFilePath, std::ios::binary);
        if (!outputFile.is_open()) {
            BLOGE("Failed to open temporary file for writing: %s", tempFilePath.c_str());
            return;
        }
        
        // Write serialized data
        outputFile.write(reinterpret_cast<const char*>(builder.GetBufferPointer()), 
                        static_cast<int>(builder.GetSize()));
        outputFile.close();
        
        // Check if write was successful
        if (outputFile.fail()) {
            BLOGE("Failed to write model to temporary file: %s", tempFilePath.c_str());
            std::remove(tempFilePath.c_str());  // Clean up temporary file
            return;
        }
        
        // Atomically replace original file (using rename operation, which is atomic)
        if (std::rename(tempFilePath.c_str(), outputFilePath.c_str()) != 0) {
            BLOGE("Failed to rename temporary file to final file: %s -> %s", 
                  tempFilePath.c_str(), outputFilePath.c_str());
            std::remove(tempFilePath.c_str());  // Clean up temporary file
            return;
        }
        
        BLOG("Model saved successfully to: %s", outputFilePath.c_str());
    }

}  // namespace fastbotx

#endif /* #if 0 ModelReusableAgent temporarily disabled */

namespace fastbotx { void _model_reusable_agent_disabled_placeholder() { (void)0; } }

#endif
