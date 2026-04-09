/*
 * LLMDroid-style GPT worker: high/low priority queues, single consumer thread.
 * HTTP via LlmClient::predictWithPayload (Java path: promptType llmdroid_* + payload JSON).
 */
/**
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 */

#ifndef FASTBOTX_GPT_AGENT_H_
#define FASTBOTX_GPT_AGENT_H_

#include "Base.h"
#include "MergedState.h"

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace fastbotx {

class LlmClient;

typedef std::shared_ptr<std::promise<int>> PromiseIntPtr;
typedef std::shared_ptr<std::promise<ActivityStateActionPtr>> PromiseActionPtr;

enum class AskModel { STATE_OVERVIEW, GRAPH_OVERVIEW, GUIDE, TEST_FUNCTION, GUIDE_FAILURE, REANALYSIS };

struct QuestionPayload {
    AskModel type{AskModel::STATE_OVERVIEW};
    MergedStatePtr from;
    std::map<MergedStatePtr, std::set<ActionPtr>> stateMap;
    int transitCount{0};
    ReuseStatePtr reuseState;
    bool flag{false};
};

/**
 * Worker owns LLM calls; monkey thread only pushes payloads (overview / reanalysis / guide / test).
 */
class GPTAgent {
public:
    GPTAgent(MergedStateGraphPtr graph, std::shared_ptr<LlmClient> llm, std::string startPrompt);

    ~GPTAgent();

    GPTAgent(const GPTAgent &) = delete;
    GPTAgent &operator=(const GPTAgent &) = delete;

    void resetPromise(PromiseIntPtr promInt, PromiseActionPtr promAction);

    void pushStateToQueue(QuestionPayload payload);

    void waitUntilQueueEmpty();

    std::string getFunctionToTest() const { return _targetFunction; }

    /** Main thread: mark guided function tested (stub for full guide flow). */
    void addTestedFunction();

    void clearExecutedEvents();

private:
    void pageAnalysisLoop();

    void askForStateOverview(QuestionPayload &payload);

    void askForReanalysis(QuestionPayload &payload);

    void askForGuiding(QuestionPayload &payload);

    void askForTestFunction(QuestionPayload &payload);

    /** Calls LlmClient; parses JSON object from assistant content. */
    nlohmann::json getResponseJson(const std::string &prompt, AskModel type);
    nlohmann::json getResponseJson(const nlohmann::json &payload, AskModel type);

    static std::string promptTypeForAsk(AskModel type);

    MergedStateGraphPtr _mergedStateGraph;
    std::shared_ptr<LlmClient> _llm;
    std::string _startPrompt;

    std::queue<QuestionPayload> _stateQueue;
    std::queue<QuestionPayload> _lowQueue;
    std::mutex _mtx;
    std::condition_variable _cv;
    std::atomic<bool> _stop{false};
    std::thread _worker;

    std::mutex _questionMtx;
    int _questionRemained{0};

    static constexpr unsigned long _P2 = 10;
    MergedStateVecPtr _topValuedMergedState = std::make_shared<MergedStateVec>();

    std::string _targetFunction;
    int _targetMergedStateId{-1};
    std::set<std::string> _testedFunctions;
    std::vector<std::string> _executedFunctions;

    PromiseIntPtr _promiseInt;
    PromiseActionPtr _promiseAction;

    void addExecutedEvent(const std::string &html, int widget_id, const ActivityStateActionPtr &act);
};

} // namespace fastbotx

#endif
