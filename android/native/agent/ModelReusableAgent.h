/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef ReuseAgent_H_
#define ReuseAgent_H_

#include "AbstractAgent.h"
#include "State.h"
#include "Action.h"
#include <vector>
#include <map>
#include <random>

namespace fastbotx {

    /**
     * @brief SARSA reinforcement learning algorithm constants namespace
     * 
     * Defines all constants used by ModelReusableAgent for SARSA (State-Action-Reward-State-Action)
     * reinforcement learning algorithm. SARSA is an on-policy temporal difference learning algorithm.
     */
    namespace SarsaRLConstants {
        // ========== Basic Learning Parameters ==========
        /// Default learning rate (alpha), controls Q-value update step size, range [0,1]
        constexpr double DefaultAlpha = 0.25;
        /// Default exploration rate (epsilon), controls random exploration probability, range [0,1]
        constexpr double DefaultEpsilon = 0.05;
        /// Default discount factor (gamma), controls importance of future rewards, range [0,1]
        constexpr double DefaultGamma = 0.8;
        /// N-step SARSA step count, uses N-step returns to update Q-values
        constexpr int NStep = 5;
        
        // ========== Alpha Dynamic Adjustment Parameters ==========
        /// Initial moving average alpha value
        constexpr double InitialMovingAlpha = 0.5;
        /// Alpha decrement step, alpha gradually decreases as visit count increases
        constexpr double AlphaDecrement = 0.1;
        /// Alpha adjustment threshold 1: when visit count exceeds this, alpha decreases by 0.1
        constexpr long AlphaThreshold1 = 20000;
        /// Alpha adjustment threshold 2: when visit count exceeds this, alpha decreases by 0.1 again
        constexpr long AlphaThreshold2 = 50000;
        /// Alpha adjustment threshold 3: when visit count exceeds this, alpha decreases by 0.1 again
        constexpr long AlphaThreshold3 = 100000;
        /// Alpha adjustment threshold 4: when visit count exceeds this, alpha decreases by 0.1 again
        constexpr long AlphaThreshold4 = 250000;
        
        // ========== Reward Calculation Constants ==========
        /// Reward epsilon threshold, used to determine if reward value is close to 0
        constexpr double RewardEpsilon = 0.0001;
        /// Reward value for new actions (actions not in reuse model)
        constexpr double NewActionReward = 1.0;
        /// Reward value for visited actions (actions in reuse model and already visited)
        constexpr double VisitedActionReward = 0.5;
        /// Reward value for new actions in state (state contains new actions)
        constexpr double NewActionInStateReward = 1.0;
        
        // ========== Action Selection Constants ==========
        /// Entropy alpha value, used to add randomness in Q-value selection
        constexpr double EntropyAlpha = 0.1;
        /// Quality value multiplier, used to amplify action quality values
        constexpr float QualityValueMultiplier = 10.0f;
        /// Quality value threshold, only actions with quality value above this are considered
        constexpr float QualityValueThreshold = 1e-4f;
        
        // ========== Model Storage Constants ==========
        /// Model save interval (milliseconds), background thread saves model every 10 minutes
        constexpr int ModelSaveIntervalMs = 1000 * 60 * 10; // 10 minutes
        /// Maximum model file size (100MB), prevents loading overly large model files
        constexpr size_t MaxModelFileSize = 100 * 1024 * 1024; // 100MB
    }
    
    // Backward compatibility macros
    #define SarsaRLDefaultAlpha   SarsaRLConstants::DefaultAlpha
    #define SarsaRLDefaultEpsilon SarsaRLConstants::DefaultEpsilon
    #define SarsaRLDefaultGamma   SarsaRLConstants::DefaultGamma
    #define SarsaNStep            SarsaRLConstants::NStep

    // ========== Reuse Model Data Structure Type Definitions ==========
    /// Reuse entry mapping: Activity name -> visit count
    typedef std::map<stringPtr, int> ReuseEntryM;
    /// Reuse model mapping: action hash -> (Activity name -> visit count)
    typedef std::map<uint64_t, ReuseEntryM> ReuseEntryIntMap;
    /// Q-value mapping: action hash -> Q-value
    typedef std::map<uint64_t, double> ReuseEntryQValueMap;

    /**
     * @brief Reusable model agent based on SARSA reinforcement learning
     * 
     * ModelReusableAgent implements a test agent based on SARSA (State-Action-Reward-State-Action)
     * reinforcement learning algorithm. It maintains a reuse model that records which activities
     * each action can reach and their visit counts, and uses Q-values to evaluate action quality,
     * thereby selecting optimal action sequences.
     * 
     * Core features:
     * 1. **Action Selection**: Selects actions based on reuse model and Q-values, prioritizing
     *    actions that can reach unvisited activities
     * 2. **Q-value Updates**: Uses N-step SARSA algorithm to update Q-values
     * 3. **Reuse Model Updates**: Records action-to-activity mapping relationships
     * 4. **Model Persistence**: Periodically saves and loads reuse model
     * 
     * Action selection strategy (priority from high to low):
     * 1. Select unexecuted actions not in reuse model (explore new actions)
     * 2. Select unvisited unexecuted actions in reuse model (based on humble-gumbel distribution)
     * 3. Select unvisited actions
     * 4. Select actions based on Q-values (with added randomness)
     * 5. Epsilon-greedy strategy (random selection with epsilon probability, otherwise select
     *    action with highest Q-value)
     * 
     * Performance optimizations:
     * - Uses member random number generator to avoid creating new generators each time
     * - Uses mutex to protect concurrent access to reuse model
     * - Uses binary search to optimize action selection
     * - Caches visitedActivities to avoid repeated queries
     */
    class ModelReusableAgent : public AbstractAgent {

    public:
        /**
         * @brief Constructor
         * 
         * Initializes SARSA parameters and reuse model.
         * 
         * @param model Model pointer
         */
        explicit ModelReusableAgent(const ModelPtr &model);

        /**
         * @brief Load reuse model
         * 
         * Loads previously saved reuse model from file system.
         * Model file path: /sdcard/fastbot_{packageName}.fbm
         * 
         * @param packageName Application package name, used to construct model file path
         */
        virtual void loadReuseModel(const std::string &packageName);

        /**
         * @brief Save reuse model
         * 
         * Serializes and saves current reuse model to file system.
         * Uses FlatBuffers for serialization, uses temporary file + atomic replace to ensure data integrity.
         * 
         * @param modelFilepath Model file path, uses _defaultModelSavePath if empty
         */
        void saveReuseModel(const std::string &modelFilepath);

        /**
         * @brief Model storage background thread function
         * 
         * Periodically saves reuse model to file system to prevent data loss.
         * Saves every 10 minutes until Agent is destructed.
         * 
         * @param agent Agent's weak_ptr to avoid circular references
         */
        static void threadModelStorage(const std::weak_ptr<ModelReusableAgent> &agent);

        /**
         * @brief Destructor
         * 
         * Saves reuse model and cleans up resources.
         */
        ~ModelReusableAgent() override;

    protected:
        /**
         * @brief Compute reward value of latest action
         * 
         * Computes reward based on probability that action can reach unvisited activities.
         * Reward calculation formula:
         * reward = probabilityOfVisitingNewActivities / sqrt(visitedCount + 1)
         *        + getStateActionExpectationValue / sqrt(stateVisitedCount + 1)
         * 
         * @return Reward value
         */
        virtual double computeRewardOfLatestAction();

        /**
         * @brief Update strategy
         * 
         * Implements AbstractAgent's pure virtual function.
         * Called after action execution, performs the following operations:
         * 1. Compute reward value of latest action
         * 2. Update reuse model (record action-to-activity mapping)
         * 3. Update Q-values using SARSA algorithm
         * 4. Add new action to action history cache
         */
        void updateStrategy() override;

        /**
         * @brief Select action using epsilon-greedy strategy
         * 
         * With (1-epsilon) probability selects action with highest Q-value (greedy),
         * with epsilon probability randomly selects action (exploration).
         * 
         * @return Selected action
         */
        virtual ActivityStateActionPtr selectNewActionEpsilonGreedyRandomly() const;

        /**
         * @brief Determine whether to use greedy strategy
         * 
         * Generates random number, if >= epsilon uses greedy strategy, otherwise uses random strategy.
         * 
         * @return true means use greedy strategy, false means use random strategy
         */
        virtual bool eGreedy() const;

        /**
         * @brief Select new action (implements AbstractAgent's pure virtual function)
         * 
         * Attempts to select action in priority order:
         * 1. Select unexecuted actions not in reuse model
         * 2. Select unvisited unexecuted actions in reuse model
         * 3. Select unvisited actions
         * 4. Select actions based on Q-values
         * 5. Epsilon-greedy strategy
         * 6. If all fail, call handleNullAction()
         * 
         * @return Selected action, or nullptr on failure
         */
        ActionPtr selectNewAction() override;

        /**
         * @brief Compute probability that action can reach unvisited activities
         * 
         * Based on reuse model, computes the ratio of unvisited activity visit counts
         * to total visit counts among activities that this action can reach.
         * 
         * @param action Action to evaluate
         * @param visitedActivities Set of visited activities
         * @return Probability value, range [0,1]
         */
        double probabilityOfVisitingNewActivities(const ActivityStateActionPtr &action,
                                                  const stringPtrSet &visitedActivities) const;

        /**
         * @brief Compute state action expectation value
         * 
         * Evaluates expected value of reaching unvisited activities after executing actions from this state.
         * Considered factors:
         * 1. Number of new actions in state
         * 2. Number of visited actions in state
         * 3. Probability that each action can reach unvisited activities
         * 
         * @param state State to evaluate
         * @param visitedActivities Set of visited activities
         * @return Expectation value
         */
        double getStateActionExpectationValue(const StatePtr &state,
                                              const stringPtrSet &visitedActivities) const;

        /**
         * @brief Update reuse model
         * 
         * Records latest executed action and the activity it reached.
         * If action not in reuse model, creates new entry; otherwise updates visit count.
         * Also updates action's Q-value.
         */
        virtual void updateReuseModel();

        /**
         * @brief Adjust action priorities (overrides parent class method)
         * 
         * First calls parent class's adjustActions(), then can add additional priority adjustment logic.
         */
        void adjustActions() override;

        /**
         * @brief Select unexecuted action not in reuse model
         * 
         * Selects actions from current state that satisfy the following conditions:
         * 1. Is a model action (CLICK, SCROLL, etc., excluding BACK, FEED)
         * 2. Not in reuse model
         * 3. Not executed (visitedCount <= 0)
         * 
         * Uses weighted random selection, weights are action priorities.
         * 
         * @return Selected action, or nullptr if none
         */
        ActionPtr selectUnperformedActionNotInReuseModel() const;

        /**
         * @brief Select unvisited unexecuted action in reuse model
         * 
         * Uses humble-gumbel distribution to influence sampling, selects action with maximum quality value.
         * Quality value = probabilityOfVisitingNewActivities * QualityValueMultiplier - log(-log(uniform))
         * 
         * @return Selected action, or nullptr if none
         */
        ActionPtr selectUnperformedActionInReuseModel() const;

        /**
         * @brief Select action based on Q-value
         * 
         * Uses humble-gumbel distribution to add randomness, selects action with highest Q-value.
         * Adjusted Q-value = (Q-value + probabilityOfVisitingNewActivities) / EntropyAlpha - log(-log(uniform))
         * 
         * @return Selected action, or nullptr if none
         */
        ActionPtr selectActionByQValue();


    protected:
        /// Learning rate (alpha), controls Q-value update step size, dynamically adjusted based on visit count
        double _alpha{};
        
        /// Exploration rate (epsilon), controls random exploration probability
        double _epsilon{};

        /**
         * @brief Reward cache
         * 
         * _rewardCache[i] stores reward value for _previousActions[i].
         * Used for N-step SARSA algorithm, caches rewards of recent N steps.
         * 
         * IMPORTANT: The mapping is _rewardCache[i] -> _previousActions[i]
         * When updateQValues() is called, both arrays should have the same size
         * (after computeRewardOfLatestAction() adds the latest reward).
         */
        std::vector<double> _rewardCache;
        
        /**
         * @brief Action history cache
         * 
         * Stores recently executed actions of last N steps, used for N-step SARSA algorithm to update Q-values.
         * Length does not exceed NStep (5).
         */
        std::vector<ActionPtr> _previousActions;

    private:
        // ========== Random Number Generators (Performance Optimization) ==========
        /// Mersenne Twister random number generator, thread-safe and performant
        mutable std::mt19937 _rng;
        /// Uniform distribution for double in range [0,1)
        mutable std::uniform_real_distribution<double> _uniformDist{0.0, 1.0};
        /// Uniform distribution for float in range [0,1)
        mutable std::uniform_real_distribution<float> _uniformFloatDist{0.0f, 1.0f};
        
        // ========== Reuse Model Data ==========
        /**
         * @brief Reuse model
         * 
         * Records activities that each action (identified by hash) can reach and their visit counts.
         * Structure: action hash -> (Activity name -> visit count)
         * 
         * Used to evaluate probability that action can reach unvisited activities.
         */
        ReuseEntryIntMap _reuseModel;
        
        /**
         * @brief Q-value mapping
         * 
         * Records Q-value (quality value) of each action.
         * Structure: action hash -> Q-value
         * 
         * Note: In current implementation, Q-values are mainly stored in Action objects,
         * this mapping may not be fully used.
         */
        ReuseEntryQValueMap _reuseQValue;
        
        // ========== Model File Paths ==========
        /// Model save path (main path)
        std::string _modelSavePath;
        /// Default model save path (temporary path, used when main path is empty)
        std::string _defaultModelSavePath;
        /// Static default model save path
        static std::string DefaultModelSavePath;
        
        // ========== Thread Safety ==========
        /// Reuse model mutex, protects concurrent access to _reuseModel and _reuseQValue
        /// Uses mutable to allow locking in const methods
        mutable std::mutex _reuseModelLock;

        /**
         * @brief Compute alpha value
         * 
         * Dynamically adjusts learning rate alpha based on current state's visit count.
         * More visits result in smaller alpha (learning rate decay).
         */
        void computeAlphaValue();
        
        /**
         * @brief Calculate alpha value based on visit count
         * 
         * Alpha value decreases in segments based on visit count:
         * - visitCount > 250000: alpha = 0.1
         * - visitCount > 100000: alpha = 0.2
         * - visitCount > 50000:  alpha = 0.3
         * - visitCount > 20000: alpha = 0.4
         * - Otherwise: alpha = 0.5
         * 
         * Minimum alpha value is DefaultAlpha (0.25).
         * 
         * @param visitCount Total visit count
         * @return Calculated alpha value
         */
        double calculateAlphaByVisitCount(long visitCount) const;

        /**
         * @brief Get action's Q-value
         * 
         * @param action Action pointer
         * @return Q-value
         */
        double getQValue(const ActionPtr &action);

        /**
         * @brief Set action's Q-value
         * 
         * @param action Action pointer
         * @param qValue Q-value
         */
        void setQValue(const ActionPtr &action, double qValue);
        
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
         * Updates all actions in the window, not just the oldest one.
         * This provides better learning efficiency compared to selective updating.
         */
        void updateQValues();
        
        /**
         * @brief Check if action is in reuse model
         * 
         * @param actionHash Action's hash value
         * @return true if in reuse model, false otherwise
         */
        bool isActionInReuseModel(uintptr_t actionHash) const;
    };

    typedef std::shared_ptr<ModelReusableAgent> ReuseAgentPtr;

}


#endif /* ReuseAgent_H_ */
