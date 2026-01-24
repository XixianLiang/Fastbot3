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

    // SARSA Reinforcement Learning Constants
    namespace SarsaRLConstants {
        constexpr double DefaultAlpha = 0.25;
        constexpr double DefaultEpsilon = 0.05;
        constexpr double DefaultGamma = 0.8;
        constexpr int NStep = 5;
        
        // Alpha calculation constants
        constexpr double InitialMovingAlpha = 0.5;
        constexpr double AlphaDecrement = 0.1;
        constexpr long AlphaThreshold1 = 20000;
        constexpr long AlphaThreshold2 = 50000;
        constexpr long AlphaThreshold3 = 100000;
        constexpr long AlphaThreshold4 = 250000;
        
        // Reward calculation constants
        constexpr double RewardEpsilon = 0.0001;
        constexpr double NewActionReward = 1.0;
        constexpr double VisitedActionReward = 0.5;
        constexpr double NewActionInStateReward = 1.0;
        
        // Action selection constants
        constexpr double EntropyAlpha = 0.1;
        constexpr float QualityValueMultiplier = 10.0f;
        constexpr float QualityValueThreshold = 1e-4f;
        
        // Model storage constants
        constexpr int ModelSaveIntervalMs = 1000 * 60 * 10; // 10 minutes
        constexpr size_t MaxModelFileSize = 100 * 1024 * 1024; // 100MB
    }
    
    // Backward compatibility macros
    #define SarsaRLDefaultAlpha   SarsaRLConstants::DefaultAlpha
    #define SarsaRLDefaultEpsilon SarsaRLConstants::DefaultEpsilon
    #define SarsaRLDefaultGamma   SarsaRLConstants::DefaultGamma
    #define SarsaNStep            SarsaRLConstants::NStep

    typedef std::map<stringPtr, int> ReuseEntryM;
    typedef std::map<uint64_t, ReuseEntryM> ReuseEntryIntMap;
    typedef std::map<uint64_t, double> ReuseEntryQValueMap;

    class ModelReusableAgent : public AbstractAgent {

    public:
        explicit ModelReusableAgent(const ModelPtr &model);

        // load & save will be automatically called in construct & dealloc
        virtual void loadReuseModel(const std::string &packageName);

        // @param model filepath is "" then save to _defaultModelSavePath
        void saveReuseModel(const std::string &modelFilepath);

        static void threadModelStorage(const std::weak_ptr<ModelReusableAgent> &agent);

        ~ModelReusableAgent() override;

    protected:
        virtual double computeRewardOfLatestAction();

        void updateStrategy() override;

        virtual ActivityStateActionPtr selectNewActionEpsilonGreedyRandomly() const;

        virtual bool eGreedy() const;

        ActionPtr selectNewAction() override;

        double probabilityOfVisitingNewActivities(const ActivityStateActionPtr &action,
                                                  const stringPtrSet &visitedActivities) const;

        double getStateActionExpectationValue(const StatePtr &state,
                                              const stringPtrSet &visitedActivities) const;

        virtual void updateReuseModel();

        void adjustActions() override;

        ActionPtr selectUnperformedActionNotInReuseModel() const;

        /// Choose an unused(unvisited) action with quality value greater than zero
        /// under the influence of humble-gumbel distribution,
        /// \return The chosen action
        ActionPtr selectUnperformedActionInReuseModel() const;

        ActionPtr selectActionByQValue();


    protected:
        double _alpha{};
        double _epsilon{};

        // _rewardCache[i] is the reward value of _previousActions[i+1]
        std::vector<double> _rewardCache;
        std::vector<ActionPtr> _previousActions;

    private:
        // Random number generator for better performance and thread safety
        mutable std::mt19937 _rng;
        mutable std::uniform_real_distribution<double> _uniformDist{0.0, 1.0};
        // A map containing entry of hash code of Action and map, which containing entry of name of activity that this
        // action goes to and the count of this very activity being visited.
        ReuseEntryIntMap _reuseModel;
        ReuseEntryQValueMap _reuseQValue;
        std::string _modelSavePath;
        std::string _defaultModelSavePath;
        static std::string DefaultModelSavePath; // if the saved path is not specified, use this as the default.
        std::mutex _reuseModelLock;

        void computeAlphaValue();
        
        /// Calculate alpha value based on visit count
        /// \param visitCount Total number of state visits
        /// \return Calculated alpha value
        double calculateAlphaByVisitCount(long visitCount) const;

        double getQValue(const ActionPtr &action);

        void setQValue(const ActionPtr &action, double qValue);
        
        /// Update Q values for previous actions using SARSA algorithm
        void updateQValues();
    };

    typedef std::shared_ptr<ModelReusableAgent> ReuseAgentPtr;

}


#endif /* ReuseAgent_H_ */
