/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang, Tianming Liu
 */

#ifndef AbstractAgent_CPP_
#define AbstractAgent_CPP_

#include "AbstractAgent.h"

#include "GPTAgent.h"
#include "CodeCoverageMonitor.h"
#include <utility>
#include "../model/Model.h"
#include "../desc/MergedState.h"
#include "../desc/reuse/ReuseState.h"
#include "../events/Preference.h"
#include "LLMTaskAgent.h"
#include "../llm/LlmJavaHttp.h"
#include <cmath>
#include <future>

namespace fastbotx {

    enum class LlmdroidMode { EXPLORE, NAVIGATE, TEST_FUNCTION };

    struct LlmdroidAgentOverlay {
        GraphPtr graph;
        MergedStateGraphPtr mergedStateGraph;
        std::unique_ptr<GPTAgent> gptAgent;
        int totalMergedState{0};
        double startTime{0};
        double nextStageTime{0};
        double exploreWindowMs{120000.0};
        ActivityStateActionPtr mCurrentAction;
        ReuseStatePtr mCurrentState;

        /// same window/threshold defaults as LLMDroid AbstractAgent.
        CodeCoverageMonitor cvMonitor{80, 0.05, 1.0};
        static constexpr int kRateCapacity = 80;
        std::vector<double> growthRateWindow;
        double currentThreshold{0.05};
        bool shouldWait{false};
        size_t exploreWindowStartActivityCoverage{0};

        LlmdroidMode mode{LlmdroidMode::EXPLORE};
        Path currentPath;
        std::vector<Path> paths;
        int guideTarget{-1};
        int guideTime{0};
        int successGuideTime{0};
        int totalGuideTime{0};
        int executedSteps{0};
        float currentSimilarityCheck{0.6f};
        static constexpr float kMaxSimilarity = 0.6f;
        static constexpr float kMinSimilarity = 0.49f;
        ActivityStateActionPtr actionByGpt;
        std::future<int> futureInt;
        std::future<ActivityStateActionPtr> futureAction;
    };

    namespace {

        void llmdroidResetFuture(LlmdroidAgentOverlay &L) {
            if (!L.gptAgent) {
                return;
            }
            PromiseIntPtr promInt = std::make_shared<std::promise<int>>();
            PromiseActionPtr promAction = std::make_shared<std::promise<ActivityStateActionPtr>>();
            L.futureInt = promInt->get_future();
            L.futureAction = promAction->get_future();
            L.gptAgent->resetPromise(std::move(promInt), std::move(promAction));
        }

        void llmdroidDebugMergedStates(LlmdroidAgentOverlay & /*L*/) {
            BLOG("LLMDroid: debugMergedStates (file dump skipped)");
        }

        void llmdroidPrepareBackToExplore(LlmdroidAgentOverlay &L, AbstractAgent & /*agent*/) {
            BLOG("LLMDroid: prepareBackToExplore");
            L.mode = LlmdroidMode::EXPLORE;
            L.nextStageTime = L.exploreWindowMs + currentStamp();
            if (L.graph) {
                L.exploreWindowStartActivityCoverage = L.graph->getVisitedActivities().size();
            }
            L.growthRateWindow.clear();
            L.shouldWait = false;
            L.guideTarget = -1;
            L.paths.clear();
            L.guideTime = 0;
            L.currentSimilarityCheck = LlmdroidAgentOverlay::kMaxSimilarity;
            L.executedSteps = 0;
            L.actionByGpt.reset();
            if (L.gptAgent) {
                L.gptAgent->addTestedFunction();
                L.gptAgent->clearExecutedEvents();
            }
            if (!L.mergedStateGraph || !L.gptAgent) {
                return;
            }
            for (const MergedStatePtr &ms : L.mergedStateGraph->getMergedStates()) {
                if (ms && ms->needReanalysed()) {
                    QuestionPayload qp;
                    qp.type = AskModel::REANALYSIS;
                    qp.from = ms;
                    L.gptAgent->pushStateToQueue(std::move(qp));
                }
            }
        }

        void llmdroidOnNavigationOver(LlmdroidAgentOverlay &L, bool success, AbstractAgent &agent) {
            if (success) {
                L.successGuideTime++;
                L.mode = LlmdroidMode::TEST_FUNCTION;
                BLOG("LLMDroid: navigation success -> TEST_FUNCTION");
            } else {
                llmdroidPrepareBackToExplore(L, agent);
            }
            BLOG("LLMDroid: guide stat %d/%d", L.successGuideTime, L.totalGuideTime);
            L.guideTarget = -1;
            L.paths.clear();
            L.guideTime = 0;
            L.currentSimilarityCheck = LlmdroidAgentOverlay::kMaxSimilarity;
        }

        void llmdroidPrepareForNavigation(LlmdroidAgentOverlay &L, const ModelPtr &model, AbstractAgent &agent);

        void llmdroidOnNavigationFailed(LlmdroidAgentOverlay &L, const ModelPtr &model, AbstractAgent &agent) {
            BLOG("LLMDroid: onNavigationFailed guideTime=%d", L.guideTime);
            if (L.guideTime > 1 && L.currentSimilarityCheck > LlmdroidAgentOverlay::kMinSimilarity) {
                L.currentSimilarityCheck -= 0.05f;
            }
            if (!L.paths.empty()) {
                L.currentPath = L.paths[0];
                L.paths.erase(L.paths.begin());
                return;
            }
            if (L.guideTime < 3) {
                if (L.gptAgent) {
                    L.gptAgent->addTestedFunction();
                }
                llmdroidPrepareForNavigation(L, model, agent);
                return;
            }
            llmdroidOnNavigationOver(L, false, agent);
        }

        void llmdroidPrepareForNavigation(LlmdroidAgentOverlay &L, const ModelPtr &model, AbstractAgent &agent) {
            (void)model;
            (void)agent;
            if (!L.graph || !L.gptAgent) {
                return;
            }
            L.mode = LlmdroidMode::NAVIGATE;
            L.gptAgent->waitUntilQueueEmpty();
            llmdroidDebugMergedStates(L);
            L.guideTime++;
            L.totalGuideTime++;
            llmdroidResetFuture(L);
            QuestionPayload qp;
            qp.type = AskModel::GUIDE;
            L.gptAgent->pushStateToQueue(std::move(qp));
            L.guideTarget = L.futureInt.get();
            BLOG("LLMDroid: guide target ReuseState id=%d", L.guideTarget);
            if (L.guideTarget < 0) {
                llmdroidOnNavigationFailed(L, model, agent);
                return;
            }
            L.paths = L.graph->findPath(L.guideTarget, true);
            if (L.paths.empty()) {
                BLOG("LLMDroid: no path to ReuseState %d", L.guideTarget);
                llmdroidOnNavigationFailed(L, model, agent);
            } else {
                L.currentPath = L.paths[0];
                L.paths.erase(L.paths.begin());
            }
        }

        int llmdroidGuideCheck(LlmdroidAgentOverlay &L) {
            bool isCorrect = false;
            int targetId = -1;
            ReuseStatePtr mcs = L.mCurrentState;
            if (!mcs) {
                return 3;
            }
            while (!L.currentPath.steps.empty()) {
                Step currentStep = L.currentPath.steps.front();
                targetId = currentStep.node;
                L.currentPath.steps.pop();
                if (mcs->getIdi() == targetId) {
                    isCorrect = true;
                    break;
                }
                if (currentStep.action && currentStep.action->getActionType() == ActionType::RESTART) {
                    if (L.currentPath.steps.empty()) {
                        isCorrect = true;
                        break;
                    }
                    ActionPtr replace = mcs->findSimilarAction(L.currentPath.steps.front().action);
                    if (replace) {
                        ActivityStateActionPtr tmp = std::dynamic_pointer_cast<ActivityStateAction>(replace);
                        L.currentPath.steps.front().action =
                            tmp ? std::static_pointer_cast<Action>(std::make_shared<ActivityStateAction>(*tmp)) : replace;
                        isCorrect = true;
                        break;
                    }
                } else if (L.graph) {
                    ReuseStatePtr targetState = L.graph->findReuseStateById(targetId);
                    const float sim = targetState ? mcs->computeSimilarity(targetState) : 0.f;
                    BLOG("LLMDroid guideCheck sim target R%d now R%d -> %f", targetId, mcs->getIdi(), sim);
                    if (sim > L.currentSimilarityCheck) {
                        if (L.currentPath.steps.empty()) {
                            isCorrect = true;
                            break;
                        }
                        ActionPtr replace = mcs->findSimilarAction(L.currentPath.steps.front().action);
                        if (replace) {
                            ActivityStateActionPtr tmp = std::dynamic_pointer_cast<ActivityStateAction>(replace);
                            L.currentPath.steps.front().action =
                                tmp ? std::static_pointer_cast<Action>(std::make_shared<ActivityStateAction>(*tmp))
                                    : replace;
                            isCorrect = true;
                            break;
                        }
                    }
                }
            }
            if (isCorrect) {
                return L.currentPath.steps.empty() ? 2 : 1;
            }
            BLOG("LLMDroid guideCheck failed target=%d now=%d", targetId, mcs->getIdi());
            return 3;
        }

        void llmdroidPrepareTestFunction(LlmdroidAgentOverlay &L) {
            if (!L.gptAgent || !L.mCurrentState) {
                return;
            }
            if (L.executedSteps < 5) {
                L.executedSteps++;
                llmdroidResetFuture(L);
                QuestionPayload qp;
                qp.type = AskModel::TEST_FUNCTION;
                qp.reuseState = L.mCurrentState;
                L.gptAgent->pushStateToQueue(std::move(qp));
                L.actionByGpt = L.futureAction.get();
            } else {
                L.actionByGpt.reset();
                BLOG("LLMDroid: TEST_FUNCTION step cap reached");
            }
        }

        MergedStatePtr findMostSimilarReuse(LlmdroidAgentOverlay &L, const ReuseStatePtr &state) {
            constexpr float kThreshold = 0.6f;
            MergedStatePtr origin = state->getMergedState();
            if (origin) {
                return origin;
            }
            MergedStatePtr current = L.mergedStateGraph->getCurrentNode();
            if (!current) {
                return nullptr;
            }
            ReuseStatePtr rootState = current->getRootState();
            if (!rootState) {
                return nullptr;
            }
            const float similarity = rootState->computeSimilarityForMergedState(state);
            if (similarity < kThreshold) {
                float maxS = 0.f;
                MergedStatePtr best;
                for (const MergedStatePtr &ms : L.mergedStateGraph->getMergedStates()) {
                    if (!ms) {
                        continue;
                    }
                    ReuseStatePtr r = ms->getRootState();
                    if (!r) {
                        continue;
                    }
                    const float s = r->computeSimilarityForMergedState(state);
                    if (s > kThreshold && s > maxS) {
                        maxS = s;
                        best = ms;
                    }
                }
                return best;
            }
            return current;
        }

        void llmdroidSwitchMode(LlmdroidAgentOverlay &L, const ModelPtr &model, AbstractAgent &agent) {
            const double now = currentStamp();

            if (L.mode == LlmdroidMode::EXPLORE) {
                const bool useCoverageMode = isLlmdroidExternalCoverageEnabledFromJava();
                if (useCoverageMode) {
                    const double javaCoverageMetric = getLlmdroidCoverageFromJava();
                    const auto res = L.cvMonitor.update(javaCoverageMetric);
                    L.currentThreshold = res.second;
                    L.growthRateWindow.push_back(res.first);
                    if (static_cast<int>(L.growthRateWindow.size()) > LlmdroidAgentOverlay::kRateCapacity) {
                        L.growthRateWindow.erase(L.growthRateWindow.begin());
                    }

                    if (static_cast<int>(L.growthRateWindow.size()) == LlmdroidAgentOverlay::kRateCapacity) {
                        bool pass = false;
                        for (double d : L.growthRateWindow) {
                            if (d > L.currentThreshold) {
                                pass = true;
                                break;
                            }
                        }
                        if (!pass) {
                            L.shouldWait = true;
                            BLOG("LLMDroid: stagnation by coverage window (threshold=%f)", L.currentThreshold);
                        }
                    }
                } else {
                    // Time mode: switch only when activity coverage has not increased within one explore window.
                    size_t currentActivityCoverage = 0;
                    if (L.graph) {
                        currentActivityCoverage = L.graph->getVisitedActivities().size();
                    }
                    if (currentActivityCoverage > L.exploreWindowStartActivityCoverage) {
                        BLOG("LLMDroid: time window coverage increased %zu -> %zu, extend explore window",
                             L.exploreWindowStartActivityCoverage, currentActivityCoverage);
                        L.exploreWindowStartActivityCoverage = currentActivityCoverage;
                        L.nextStageTime = now + L.exploreWindowMs;
                    } else if (now > L.nextStageTime) {
                        L.shouldWait = true;
                        BLOG("LLMDroid: switch by time window (activity coverage no increase: %zu)",
                             currentActivityCoverage);
                    }
                }

                if (L.shouldWait) {
                    L.shouldWait = false;
                    llmdroidPrepareForNavigation(L, model, agent);
                    L.nextStageTime = now + L.exploreWindowMs;
                    if (L.graph) {
                        L.exploreWindowStartActivityCoverage = L.graph->getVisitedActivities().size();
                    }
                    L.growthRateWindow.clear();
                    return;
                }
            }

            if (L.mode == LlmdroidMode::NAVIGATE) {
                const int st = llmdroidGuideCheck(L);
                if (st == 1) {
                    return;
                }
                if (st == 2) {
                    llmdroidOnNavigationOver(L, true, agent);
                    return;
                }
                llmdroidOnNavigationFailed(L, model, agent);
                return;
            }

            if (L.mode == LlmdroidMode::TEST_FUNCTION) {
                llmdroidPrepareTestFunction(L);
            }
        }

    } // namespace

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
        _llmdroid.reset();
        this->_model.reset();
        this->_lastState.reset();
        this->_currentState.reset();
        this->_newState.reset();
        this->_lastAction.reset();
        this->_currentAction.reset();
        this->_newAction.reset();
        this->_validateFilter.reset();
    }

    void AbstractAgent::ensureLlmdroidRuntime() {
        if (_llmdroid) {
            return;
        }
        const PreferencePtr pref = Preference::inst();
        if (!pref || !pref->isLlmdroidEnabled()) {
            return;
        }
        const ModelPtr model = this->_model.lock();
        if (!model) {
            return;
        }
        _llmdroid = std::make_unique<LlmdroidAgentOverlay>();
        _llmdroid->graph = model->getGraph();
        _llmdroid->mergedStateGraph = std::make_shared<MergedStateGraph>(_llmdroid->graph);
        std::shared_ptr<LlmClient> llm = model->getLlmClient();
        std::string startPrompt = "I'm testing an Android app.\n";
        _llmdroid->gptAgent =
                std::make_unique<GPTAgent>(_llmdroid->mergedStateGraph, std::move(llm), std::move(startPrompt));
        _llmdroid->startTime = currentStamp();
        const int exploreWindowSec = pref->getLlmdroidExploreWindowSec();
        _llmdroid->exploreWindowMs = static_cast<double>(exploreWindowSec) * 1000.0;
        _llmdroid->nextStageTime = _llmdroid->startTime + _llmdroid->exploreWindowMs;
        _llmdroid->exploreWindowStartActivityCoverage =
                _llmdroid->graph ? _llmdroid->graph->getVisitedActivities().size() : 0;
        BLOG("LLMDroid: mode source=%s",
             isLlmdroidExternalCoverageEnabledFromJava() ? "external-coverage(jacoco/androlog)" : "time-mode");
        BLOG("LLMDroid: time-mode explore window configured to %d sec", exploreWindowSec);
    }

    void AbstractAgent::processState(const ReuseStatePtr &state) {
        if (!state) {
            return;
        }
        const auto pref = Preference::inst();
        if (!pref || !pref->isLlmdroidEnabled()) {
            return;
        }
        const ModelPtr model = this->_model.lock();
        if (!model) {
            return;
        }

        ensureLlmdroidRuntime();
        if (!_llmdroid || !_llmdroid->mergedStateGraph || !_llmdroid->gptAgent) {
            return;
        }

        LlmdroidAgentOverlay &L = *_llmdroid;
        L.mCurrentState = state;
        L.mCurrentAction = _currentAction;

        MergedStatePtr mergedState = findMostSimilarReuse(L, state);
        if (mergedState) {
            state->setMergedState(mergedState);
            if (mergedState == L.mergedStateGraph->getCurrentNode()) {
                mergedState->addState(state, L.mCurrentAction, false, false);
            } else {
                MergedStatePtr preState = L.mergedStateGraph->getCurrentNode();
                if (preState) {
                    preState->addState(state, L.mCurrentAction, false, true);
                }
                mergedState->addState(state, L.mCurrentAction, true, false);
            }
        } else {
            MergedStatePtr preState = L.mergedStateGraph->getCurrentNode();
            if (preState) {
                preState->addState(state, L.mCurrentAction, false, true);
            }
            mergedState = std::make_shared<MergedState>(state, L.totalMergedState);
            L.totalMergedState++;
            state->setMergedState(mergedState);
            const stringPtr actStr = state->getActivityString();
            if (actStr && actStr.get() && actStr->find("com.android.") == std::string::npos) {
                QuestionPayload qp;
                qp.type = AskModel::STATE_OVERVIEW;
                qp.from = mergedState;
                L.gptAgent->pushStateToQueue(std::move(qp));
            }
        }

        L.mergedStateGraph->addNode(mergedState, L.mCurrentAction, false);
        llmdroidSwitchMode(L, model, *this);
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
        this->adjustActions();

        const PreferencePtr pref = Preference::inst();
        if (pref && pref->isLlmdroidEnabled() && _llmdroid) {
            LlmdroidAgentOverlay &L = *_llmdroid;
            if (L.mode == LlmdroidMode::NAVIGATE) {
                if (!L.currentPath.steps.empty()) {
                    ActionPtr nextAction = L.currentPath.steps.front().action;
                    ActivityStateActionPtr tmp = std::dynamic_pointer_cast<ActivityStateAction>(nextAction);
                    ActionPtr action =
                        (tmp && L.mCurrentState) ? L.mCurrentState->findSimilarAction(nextAction) : nextAction;
                    if (!action) {
                        BLOGE("LLMDroid NAVIGATE: findSimilarAction failed");
                    } else {
                        _newAction = std::dynamic_pointer_cast<ActivityStateAction>(action);
                        return action;
                    }
                }
                BLOGE("LLMDroid NAVIGATE: currentPath is empty, fallback to RL selectNewAction. "
                      "state=%d guideTarget=%d pendingPaths=%zu guideTime=%d similarity=%.3f",
                      L.mCurrentState ? L.mCurrentState->getIdi() : -1,
                      L.guideTarget,
                      L.paths.size(),
                      L.guideTime,
                      static_cast<double>(L.currentSimilarityCheck));
            } else if (L.mode == LlmdroidMode::TEST_FUNCTION) {
                if (L.actionByGpt) {
                    _newAction = L.actionByGpt;
                    return std::static_pointer_cast<Action>(L.actionByGpt);
                }
                llmdroidPrepareBackToExplore(L, *this);
            }
        }

        ActionPtr action = this->selectNewAction();
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
