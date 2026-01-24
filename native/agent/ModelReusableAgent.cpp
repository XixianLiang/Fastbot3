/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Zhao Zhang, Zhengwei Lv, Jianqiang Guo, Yuhui Su
 */
#ifndef fastbotx_ModelReusableAgent_CPP_
#define fastbotx_ModelReusableAgent_CPP_

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

    ModelReusableAgent::ModelReusableAgent(const ModelPtr &model)
            : AbstractAgent(model), 
              _alpha(SarsaRLConstants::DefaultAlpha), 
              _epsilon(SarsaRLConstants::DefaultEpsilon),
              _rng(std::random_device{}()),
              _modelSavePath(DefaultModelSavePath), 
              _defaultModelSavePath(DefaultModelSavePath) {
        this->_algorithmType = AlgorithmType::Reuse;
    }

    ModelReusableAgent::~ModelReusableAgent() {
        // Note: Model should have been saved by the periodic save thread (threadModelStorage)
        // If the thread is not running or save failed, we still try to save here as a fallback
        // However, this may block the destructor, so it's better to ensure saves happen during lifetime
        BLOG("save model in destruct");
        this->saveReuseModel(this->_modelSavePath);
        this->_reuseModel.clear();
    }

    void ModelReusableAgent::computeAlphaValue() {
        if (nullptr != this->_newState) {
            auto modelPtr = this->_model.lock();
            if (!modelPtr) {
                BLOGE("Model has been destroyed, cannot compute alpha value");
                return;
            }
            const GraphPtr &graphRef = modelPtr->getGraph();
            long totalVisitCount = graphRef->getTotalDistri();
            this->_alpha = calculateAlphaByVisitCount(totalVisitCount);
        }
    }
    
    double ModelReusableAgent::calculateAlphaByVisitCount(long visitCount) const {
        using namespace SarsaRLConstants;
        
        // Use lookup table for better maintainability
        static const std::vector<std::pair<long, double>> alphaThresholds = {
            {AlphaThreshold4, AlphaDecrement},
            {AlphaThreshold3, AlphaDecrement},
            {AlphaThreshold2, AlphaDecrement},
            {AlphaThreshold1, AlphaDecrement}
        };
        
        double movingAlpha = InitialMovingAlpha;
        for (const auto& pair : alphaThresholds) {
            int threshold = pair.first;
            double decrement = pair.second;
            if (visitCount > threshold) {
                movingAlpha -= decrement;
            }
        }
        // The minimal alpha is DefaultAlpha (0.25), not 0.1
        return std::max(DefaultAlpha, movingAlpha);
    }

    /// Based on the lastSelectedAction (newly selected action), compute its reward value
    /// \return the reward value
    double ModelReusableAgent::computeRewardOfLatestAction() {
        double rewardValue = 0.0;
        if (nullptr != this->_newState) {
            this->computeAlphaValue();
            auto modelPtr = this->_model.lock();
            if (!modelPtr) {
                BLOGE("Model has been destroyed, cannot compute reward");
                return rewardValue;
            }
            const GraphPtr &graphRef = modelPtr->getGraph();
            auto visitedActivities = graphRef->getVisitedActivities(); // get the set of visited activities
            // get the last, or previous, action in the vector containing previous actions.
            if (auto lastSelectedAction = std::dynamic_pointer_cast<ActivityStateAction>(
                    this->_previousActions.back())) {
                // Get the expectation of this action for accessing unvisited new activity.
                rewardValue = this->probabilityOfVisitingNewActivities(lastSelectedAction,
                                                                       visitedActivities);
                // If this is an action not in reuse model, this action is new and should definitely be used
                if (std::abs(rewardValue - 0.0) < SarsaRLConstants::RewardEpsilon)
                    rewardValue = SarsaRLConstants::NewActionReward;
                rewardValue = (rewardValue / sqrt(lastSelectedAction->getVisitedCount() + 1.0));
            }
            rewardValue = rewardValue + (this->getStateActionExpectationValue(this->_newState,
                                                                              visitedActivities) /
                                         sqrt(this->_newState->getVisitedCount() + 1.0));
            BLOG("total visited " ACTIVITY_VC_STR " count is %zu", visitedActivities.size());
        }
        BDLOG("reuse-cov-opti action reward=%f", rewardValue);
        this->_rewardCache.emplace_back(rewardValue);
        // Make sure the length of reward cache is not over NStep
        if (this->_rewardCache.size() > SarsaRLConstants::NStep) {
            this->_rewardCache.erase(this->_rewardCache.begin());
        }
        return rewardValue;// + this->_newState->getTheta();
    }

    /// Based on the reuse model, compute the probability of this current action visiting a unvisited activity,
    /// which not in visitedActivities set. This value is the percentage of count of
    /// activities that this state has not reached compared with the visitedActivities set.
    /// \param action The chosen action in this state.
    /// \param visitedActivities A string set, containing already visited activities.
    /// \return percentage of count of activities that this state has not reached compared with the visitedActivities set.
    double
    ModelReusableAgent::probabilityOfVisitingNewActivities(const ActivityStateActionPtr &action,
                                                           const stringPtrSet &visitedActivities) const {
        double value = .0;
        int total = 0;
        int unvisited = 0;
        // find this action in this model according to its int hash
        // according to the given action, get the activities that this action could reach in reuse model.
        std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
        auto actionMapIterator = this->_reuseModel.find(action->hash());
        if (actionMapIterator != this->_reuseModel.end()) {
            // Iterate the map containing entry of activity name and visited count
            // to ascertain the unvisited activity count according to the pre-saved reuse model
            for (const auto &activityCountMapIterator: (*actionMapIterator).second) {
                total += activityCountMapIterator.second;
                stringPtr activity = activityCountMapIterator.first;
                if (visitedActivities.count(activity) == 0) {
                    unvisited += activityCountMapIterator.second;
                }
            }
            if (total > 0 && unvisited > 0) {
                value = static_cast<double>(unvisited) / total;
            }
        }
        return value;
    }

    /// Return the expectation of reaching an unvisited activity after executing one of the action
    /// from this state. It estimate the expectation from the perspective of the whole state.
    /// @param state the newly reached state
    /// @param visitedActivities the visited activity set AFTER reaching this state(the activity of this
    ///         state is included)
    /// @return the expectation of this state reaching an unvisited activity after executing one of the action
    bool ModelReusableAgent::isActionInReuseModel(uintptr_t actionHash) const {
        std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
        return this->_reuseModel.count(actionHash) > 0;
    }

    double ModelReusableAgent::getStateActionExpectationValue(const StatePtr &state,
                                                              const stringPtrSet &visitedActivities) const {
        double value = 0.0;
        for (const auto &action: state->getActions()) {
            uintptr_t actionHash = action->hash();
            // if this action is new, increment the value by 1, else by 0.5
            // If this action has not been visited yet.
            if (!this->isActionInReuseModel(actionHash)) {
                value += SarsaRLConstants::NewActionInStateReward;
            }
                // If this action is been performed in current testing.
            else if (action->getVisitedCount() >= 1) {
                value += SarsaRLConstants::VisitedActionReward;
            }

            // regardless of the back action
            // Expectation of reaching an unvisited activity.
            if (action->getTarget() != nullptr) {
                value += probabilityOfVisitingNewActivities(action, visitedActivities);
            }
        }
        return value;
    }

    double ModelReusableAgent::getQValue(const ActionPtr &action) {
        return action->getQValue();
    }

    void ModelReusableAgent::setQValue(const ActionPtr &action, double qValue) {
        action->setQValue(qValue);
    }

/// If the new action is generated,
    void ModelReusableAgent::updateStrategy() {
        if (nullptr == this->_newAction) // need to call resolveNewAction to update _newAction
            return;
        // _previousActions is a vector storing certain amount of actions, of which length equals to NStep.
        if (!this->_previousActions.empty()) {
            this->computeRewardOfLatestAction();
            this->updateReuseModel();
            this->updateQValues();
        } else {
            BDLOG("%s", "get action value failed!");
        }
        // add the new action to the back of the cache.
        this->_previousActions.emplace_back(this->_newAction);
        if (this->_previousActions.size() > SarsaRLConstants::NStep) {
            // if the cached length is over NStep, erase the first action from cache.
            this->_previousActions.erase(this->_previousActions.begin());
        }
    }
    
    void ModelReusableAgent::updateQValues() {
        using namespace SarsaRLConstants;
        double value = getQValue(_newAction);
        for (int i = static_cast<int>(this->_previousActions.size()) - 1; i >= 0; i--) {
            double currentQValue = getQValue(_previousActions[i]);
            double currentRewardValue = this->_rewardCache[i];
            // accumulated reward from the newest actions
            value = currentRewardValue + DefaultGamma * value;
            // Should not update the q value during step (action edge) between i+1 to i+n-1
            // The following statement is slightly different from the original sarsa RL paper.
            // Since only the oldest action should be updated.
            if (i == 0)
                setQValue(this->_previousActions[i],
                          currentQValue + this->_alpha * (value - currentQValue));
        }
    }

    void ModelReusableAgent::updateReuseModel() {
        if (this->_previousActions.empty())
            return;
        ActionPtr lastAction = this->_previousActions.back();
        if (auto modelAction = std::dynamic_pointer_cast<ActivityNameAction>(lastAction)) {
            if (nullptr == this->_newState)
                return;
            auto hash = (uint64_t) modelAction->hash();
            stringPtr activity = this->_newState->getActivityString(); // mark: use the _newstate as last selected action's target
            if (activity == nullptr)
                return;
            {
                std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
                auto iter = this->_reuseModel.find(hash);
                if (iter == this->_reuseModel.end()) {
                    BDLOG("can not find action %s in reuse map", modelAction->getId().c_str());
                    ReuseEntryM entryMap;
                    entryMap.emplace(activity, 1);
                    this->_reuseModel[hash] = entryMap;
                } else {
                    ((*iter).second)[activity] += 1;
                }
                this->_reuseQValue[hash] = modelAction->getQValue();
            }
        }
    }

    ActivityStateActionPtr ModelReusableAgent::selectNewActionEpsilonGreedyRandomly() const {
        if (this->eGreedy()) {
            BDLOG("%s", "Try to select the max value action");
            return this->_newState->greedyPickMaxQValue(enableValidValuePriorityFilter);
        }
        BDLOG("%s", "Try to randomly select a value action.");
        return this->_newState->randomPickAction(enableValidValuePriorityFilter);
    }


    bool ModelReusableAgent::eGreedy() const {
        // Use member random number generator for better performance and thread safety
        return _uniformDist(_rng) >= this->_epsilon;
    }


    ActionPtr ModelReusableAgent::selectNewAction() {
        ActionPtr action = nullptr;
        action = this->selectUnperformedActionNotInReuseModel();
        if (nullptr != action) {
            BLOG("%s", "select action not in reuse model");
            return action;
        }

        action = this->selectUnperformedActionInReuseModel();
        if (nullptr != action) {
            BLOG("%s", "select action in reuse model");
            return action;
        }

        action = this->_newState->randomPickUnvisitedAction();
        if (nullptr != action) {
            BLOG("%s", "select action in unvisited action");
            return action;
        }

        // if all the actions are explored, use those two methods to generate new action based on q value.
        // there are two methods to choose from.
        // based on q value and a uniform distribution, select an action with the highest value.
        action = this->selectActionByQValue();
        if (nullptr != action) {
            BLOG("%s", "select action by qvalue");
            return action;
        }

        // use the traditional epsilon greedy strategy to choose the next action.
        action = this->selectNewActionEpsilonGreedyRandomly();
        if (nullptr != action) {
            BLOG("%s", "select action by EpsilonGreedyRandom");
            return action;
        }
        BLOGE("null action happened , handle null action");
        return handleNullAction();
    }

    /// Randomly choose an action that is not been visited before, which belongs to the following type:
    ///        BACK,
    ///        FEED,
    ///        CLICK,
    ///        LONG_CLICK,
    ///        SCROLL_TOP_DOWN,
    ///        SCROLL_BOTTOM_UP,
    ///        SCROLL_LEFT_RIGHT,
    ///        SCROLL_RIGHT_LEFT,
    ///        SCROLL_BOTTOM_UP_N
    /// \return An action in this new state but not been performed before nor been recorded by Reuse Model
    ActionPtr ModelReusableAgent::selectUnperformedActionNotInReuseModel() const {
        std::vector<ActionPtr> actionsNotInModel;
        for (const auto &action: this->_newState->getActions()) {
            bool matched = action->isModelAct() // should be one of aforementioned actions.
                           && !this->isActionInReuseModel(action->hash()) // this action should not be in reuse model
                           && action->getVisitedCount() <=
                              0; // find the action that not been explored before
            if (matched) {
                actionsNotInModel.emplace_back(action);
            }
        }
        // random by priority - use cumulative distribution function (CDF) and binary search for O(log n) complexity
        if (actionsNotInModel.empty()) {
            BDLOGE("%s", " no actions not in model");
            return nullptr;
        }
        
        // Build cumulative weights for binary search
        std::vector<int> cumulativeWeights;
        int totalWeight = 0;
        for (const auto &action: actionsNotInModel) {
            totalWeight += action->getPriority();
            cumulativeWeights.push_back(totalWeight);
        }
        
        if (totalWeight <= 0) {
            BDLOGE("%s", " total weights is 0");
            return nullptr;
        }
        
        int randI = randomInt(0, totalWeight);
        auto it = std::lower_bound(cumulativeWeights.begin(), 
                                  cumulativeWeights.end(), randI);
        size_t index = std::distance(cumulativeWeights.begin(), it);
        
        if (index < actionsNotInModel.size()) {
            return actionsNotInModel[index];
        }
        
        BDLOGE("%s", " rand a null action");
        return nullptr;
    }

    ActionPtr ModelReusableAgent::selectUnperformedActionInReuseModel() const {
        float maxValue = -MAXFLOAT;
        ActionPtr nextAction = nullptr;
        
        // Cache visitedActivities to avoid repeated calls in the loop
        auto modelPointer = this->_model.lock();
        stringPtrSet visitedActivities;
        if (modelPointer) {
            const GraphPtr &graphRef = modelPointer->getGraph();
            visitedActivities = graphRef->getVisitedActivities();
        } else {
            return nullptr;
        }
        
        // use humble gumbel(http://amid.fish/humble-gumbel) to affect the sampling of actions from reuseModel
        for (const auto &action: this->_newState->targetActions())  // except BACK/FEED/EVENT_SHELL actions. Only actions from  ActionType::CLICK to ActionType::SCROLL_BOTTOM_UP_N are allowed
        {
            uintptr_t actionHash = action->hash();
            if (this->isActionInReuseModel(actionHash)) // found this action in reuse model
            {
                if (action->getVisitedCount() >
                    0) // In this state, this action has just been performed in this round.
                {
                    BDLOG("%s", "action has been visited");
                    continue;
                }
                auto qualityValue = static_cast<float>(this->probabilityOfVisitingNewActivities(
                        action,
                        visitedActivities));
                if (qualityValue > SarsaRLConstants::QualityValueThreshold)
                {
                    // following code is for generating a random value to slight affect the quality value
                    qualityValue = SarsaRLConstants::QualityValueMultiplier * qualityValue;
                    // Use member random number generator for better performance
                    auto uniform = _uniformFloatDist(_rng);
                    // random value from uniform distribution should not be 0, or log function will return INF
                    if (uniform < std::numeric_limits<float>::min())
                        uniform = std::numeric_limits<float>::min();
                    // add this random factor to quality value
                    qualityValue -= log(-log(uniform));

                    // choose the action with the maximum quality value
                    if (qualityValue > maxValue) {
                        maxValue = qualityValue;
                        nextAction = action;
                    }
                }
            }
        }
        return nextAction;
    }

    /// Select an action with the largest quality value based on
    /// its quality value and the uniform distribution
    /// \return the selected action with the highest quality value
    ActionPtr ModelReusableAgent::selectActionByQValue() {
        ActionPtr returnAction = nullptr;
        float maxQ = -MAXFLOAT;
        auto modelPtr = this->_model.lock();
        if (!modelPtr) {
            BLOGE("Model has been destroyed, cannot select action by Q value");
            return nullptr;
        }
        const GraphPtr &graphRef = modelPtr->getGraph();
        auto visitedActivities = graphRef->getVisitedActivities();
        for (const auto &action: this->_newState->getActions()) {
            double qv = 0.0;
            uintptr_t actionHash = action->hash();
            // it won't happen, since if there is am unvisited action in state, it will be
            // visited before this method is called.
            if (action->getVisitedCount() <= 0) {
                if (this->isActionInReuseModel(actionHash)) {
                    qv += this->probabilityOfVisitingNewActivities(action, visitedActivities);
                } else {
                    BDLOG("qvalue pick return a action: %s", action->toString().c_str());
                    return action;
                }
            }
            qv += getQValue(action);
            qv /= SarsaRLConstants::EntropyAlpha;
            // Use member random number generator for better performance
            float uniform = _uniformFloatDist(_rng); // with this uniform distribution, add a little disturbance to the qv value

            // use the uniform distribution and humble gumbel to add some randomness to the qv value
            if (uniform < std::numeric_limits<float>::min())
                uniform = std::numeric_limits<float>::min();
            qv -= log(-log(uniform));
            // choose the action with the highest qv value
            if (qv > maxQ) {
                maxQ = static_cast<float >(qv);
                returnAction = action;
            }
        }
        return returnAction; // return the action with the largest qv value
    }

    void ModelReusableAgent::adjustActions() {
        AbstractAgent::adjustActions();
    }

    void ModelReusableAgent::threadModelStorage(const std::weak_ptr<ModelReusableAgent> &agent) {
        constexpr int saveInterval = SarsaRLConstants::ModelSaveIntervalMs;
        constexpr auto interval = std::chrono::milliseconds(saveInterval);
        
        while (true) {
            auto agentPtr = agent.lock();  // 只lock一次
            if (!agentPtr) {
                break;
            }
            
            std::string savePath = agentPtr->_modelSavePath;  // 复制路径
            agentPtr.reset();  // 释放锁，避免长时间持有
            
            // 在锁外保存模型
            if (auto locked = agent.lock()) {
                locked->saveReuseModel(savePath);
            }
            
            std::this_thread::sleep_for(interval);
        }
    }

    /// According to the given package name, deserialize
    /// the serialized model file with the ReuseModel.fbs
    /// by FlatBuffers
    /// \param packageName The package name of the tested application
    void ModelReusableAgent::loadReuseModel(const std::string &packageName) {
        std::string modelFilePath = std::string(ModelStorageConstants::StoragePrefix) + 
                                    packageName + ModelStorageConstants::ModelFileExtension;

        this->_modelSavePath = modelFilePath;
        if (!this->_modelSavePath.empty()) {
            this->_defaultModelSavePath = std::string(ModelStorageConstants::StoragePrefix) + 
                                         packageName + ModelStorageConstants::TempModelFileExtension;
        }
        BLOG("begin load model: %s", this->_modelSavePath.c_str());

        std::ifstream modelFile(modelFilePath, std::ios::binary | std::ios::in);
        if (!modelFile.is_open()) {
            BLOGE("Failed to open model file: %s", modelFilePath.c_str());
            return;
        }

        // load  model file to struct
        std::filebuf *fileBuffer = modelFile.rdbuf();
        // returns the new position of the altered control stream, which is the end of the file
        std::size_t filesize = fileBuffer->pubseekoff(0, modelFile.end, modelFile.in);
        // reset the position of the controlled stream
        fileBuffer->pubseekpos(0, modelFile.in);
        
        // Check file size validity
        if (filesize <= 0 || filesize > SarsaRLConstants::MaxModelFileSize) {
            BLOGE("Invalid model file size: %zu", filesize);
            return;
        }
        
        // Use smart pointer to manage memory, exception safe
        std::unique_ptr<char[]> modelFileData(new char[filesize]);
        //Reads count characters from the input sequence and stores them into a character array.
        std::streamsize bytesRead = fileBuffer->sgetn(modelFileData.get(), static_cast<int>(filesize));
        
        // Check if read was successful
        if (bytesRead != static_cast<std::streamsize>(filesize)) {
            BLOGE("Failed to read complete model file: read %lld bytes, expected %zu bytes", 
                  static_cast<long long>(bytesRead), filesize);
            return;
        }
        auto reuseFBModel = GetReuseModel(modelFileData.get());

        // to std::map
        {
            std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
            this->_reuseModel.clear();
            this->_reuseQValue.clear();
        }
        auto reusedModelDataPtr = reuseFBModel->model();
        if (!reusedModelDataPtr) {
            BLOG("%s", "model data is null");
            return;
        }
        for (flatbuffers::uoffset_t entryIndex = 0; entryIndex < reusedModelDataPtr->size(); entryIndex++) {
            auto reuseEntryInReuseModel = reusedModelDataPtr->Get(entryIndex);
            uint64_t actionHash = reuseEntryInReuseModel->action();
            auto activityEntry = reuseEntryInReuseModel->targets();
            ReuseEntryM entryPtr;
            for (flatbuffers::uoffset_t targetIndex = 0; targetIndex < activityEntry->size(); targetIndex++) {
                auto targetEntry = activityEntry->Get(targetIndex);
                BDLOG("load model hash: %llu %s %d", actionHash,
                      targetEntry->activity()->str().c_str(), (int) targetEntry->times());
                entryPtr.emplace(
                        std::make_shared<std::string>(targetEntry->activity()->str()),
                        (int) targetEntry->times());
            }
            if (!entryPtr.empty()) {
                std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
//            this->_reuseQValue.insert(std::make_pair(actionHash, reuseEntryInReuseModel->quality()));
                this->_reuseModel.emplace(actionHash, entryPtr);
            }
        }
        BLOG("loaded model contains actions: %zu", this->_reuseModel.size());
        // modelFileData will be automatically freed by unique_ptr
    }

    std::string ModelReusableAgent::DefaultModelSavePath = "/sdcard/fastbot.model.fbm";

    /// With the FlatBuffer library, serialize the ReuseModel according to ReuseModel.fbs,
    /// and save the data to modelFilePath.
    /// \param modelFilepath the path to save this serialized model.
    void ModelReusableAgent::saveReuseModel(const std::string &modelFilepath) {
        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<fastbotx::ReuseEntry>> actionActivityVector;
        // loaded, but not visited
        {
            std::lock_guard<std::mutex> reuseGuard(this->_reuseModelLock);
            for (const auto &actionIterator: this->_reuseModel) {
                uint64_t actionHash = actionIterator.first;
                ReuseEntryM activityCountEntryMap = actionIterator.second;
                std::vector<flatbuffers::Offset<fastbotx::ActivityTimes>> activityCountEntryVector; // flat buffer needs vector rather than map
                for (const auto &activityCountEntry: activityCountEntryMap) {
                    auto sentryActT = CreateActivityTimes(builder, builder.CreateString(
                            *(activityCountEntry.first)), activityCountEntry.second);
                    activityCountEntryVector.push_back(sentryActT);
                }
                auto savedActivityCountEntries = CreateReuseEntry(builder, actionHash,
                                                                  builder.CreateVector(
                                                                          activityCountEntryVector.data(),
                                                                          activityCountEntryVector.size()));
                actionActivityVector.push_back(savedActivityCountEntries);
            }
        }
        auto savedActionActivityEntries = CreateReuseModel(builder, builder.CreateVector(
                actionActivityVector.data(), actionActivityVector.size()));
        builder.Finish(savedActionActivityEntries);

        //save to local file - use temporary file first, then atomically replace
        std::string outputFilePath = modelFilepath;
        if (outputFilePath.empty()) // if the passed argument modelFilepath is "", use the tmpSavePath
            outputFilePath = this->_defaultModelSavePath;
        
        if (outputFilePath.empty()) {
            BLOGE("Cannot save model: output file path is empty");
            return;
        }
        
        // Write to temporary file first
        std::string tempFilePath = outputFilePath + ".tmp";
        BLOG("save model to temporary path: %s", tempFilePath.c_str());
        
        std::ofstream outputFile(tempFilePath, std::ios::binary);
        if (!outputFile.is_open()) {
            BLOGE("Failed to open temporary file for writing: %s", tempFilePath.c_str());
            return;
        }
        
        outputFile.write((char *) builder.GetBufferPointer(), static_cast<int>(builder.GetSize()));
        outputFile.close();
        
        // Check if write was successful
        if (outputFile.fail()) {
            BLOGE("Failed to write model to temporary file: %s", tempFilePath.c_str());
            std::remove(tempFilePath.c_str());  // Clean up temporary file
            return;
        }
        
        // Atomically replace the original file with the temporary file
        if (std::rename(tempFilePath.c_str(), outputFilePath.c_str()) != 0) {
            BLOGE("Failed to rename temporary file to final file: %s -> %s", 
                  tempFilePath.c_str(), outputFilePath.c_str());
            std::remove(tempFilePath.c_str());  // Clean up temporary file
            return;
        }
        
        BLOG("Model saved successfully to: %s", outputFilePath.c_str());
    }

}  // namespace fastbotx

#endif
