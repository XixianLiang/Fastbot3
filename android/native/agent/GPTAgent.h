/*
 * LLMDroid GPT worker: dedicated consumer thread, dual priority queues, HTTP via `LlmClient`.
 * Prompt types (`llmdroid_*`) and JSON payloads must match the configured LLM backend / runner.
 */
/**
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 */

/**
 * @file GPTAgent.h
 *
 * Background worker that turns merged-state questions into `predictWithPayload` calls and applies JSON results
 * back into `MergedState` / navigation promises. The UI thread only enqueues `QuestionPayload` items.
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

/** Kind of LLM request; drives JSON shape and `promptTypeForAsk` string sent to `LlmClient`. */
enum class AskModel { STATE_OVERVIEW, GRAPH_OVERVIEW, GUIDE, TEST_FUNCTION, GUIDE_FAILURE, REANALYSIS };

/** Work item for the worker thread; only fields relevant to `type` are populated. */
struct QuestionPayload {
    AskModel type{AskModel::STATE_OVERVIEW};
    MergedStatePtr from;
    std::map<MergedStatePtr, std::set<ActionPtr>> stateMap;
    int transitCount{0};
    ReuseStatePtr reuseState;
    bool flag{false};
};

/**
 * Single-consumer queue processor for LLMDroid LLM round-trips.
 *
 * High-priority queue (`_stateQueue`): overview, guide, test-function payloads.
 * Low-priority queue (`_lowQueue`): reanalysis for merged states that still rank in the top slice.
 * `_questionRemained` tracks outstanding items for `waitUntilQueueEmpty`.
 */
class GPTAgent {
public:
    GPTAgent(MergedStateGraphPtr graph, std::shared_ptr<LlmClient> llm, std::string startPrompt);

    ~GPTAgent();

    GPTAgent(const GPTAgent &) = delete;
    GPTAgent &operator=(const GPTAgent &) = delete;

    /** Install fresh promises before enqueueing GUIDE / TEST_FUNCTION (caller waits on futures). */
    void resetPromise(PromiseIntPtr promInt, PromiseActionPtr promAction);

    void pushStateToQueue(QuestionPayload payload);

    /** Blocks until all pushed items have been processed (polls `_questionRemained`). */
    void waitUntilQueueEmpty();

    std::string getFunctionToTest() const { return _targetFunction; }

    /** Record that the guided function under test has been exercised (updates merged state bookkeeping). */
    void addTestedFunction();

    void clearExecutedEvents();

private:
    void pageAnalysisLoop();

    void askForStateOverview(QuestionPayload &payload);

    void askForReanalysis(QuestionPayload &payload);

    void askForGuiding(QuestionPayload &payload);

    void askForTestFunction(QuestionPayload &payload);

    /** POST payload to LLM; extracts first `{...}` JSON object from text responses when needed. */
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

    /** Top merged states considered high-value for overview / guide context (see `_P2` window). */
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

#endif // FASTBOTX_GPT_AGENT_H_
