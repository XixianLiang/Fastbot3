/**
 * @authors Zhao Zhang, Tianming Liu
 */

#include "GPTAgent.h"

#include "LLMTaskAgent.h"
#include "ReuseState.h"
#include "../desc/Action.h"
#include "../utils.hpp"
#include "../thirdpart/json/json.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <sstream>
#include <thread>

namespace fastbotx {

namespace {

std::string trimStateDescription(std::string s, size_t maxLen) {
    if (s.size() > maxLen) {
        s.resize(maxLen);
    }
    return s;
}

} // namespace

GPTAgent::GPTAgent(MergedStateGraphPtr graph, std::shared_ptr<LlmClient> llm, std::string startPrompt)
    : _mergedStateGraph(std::move(graph)), _llm(std::move(llm)), _startPrompt(std::move(startPrompt)) {
    _worker = std::thread(&GPTAgent::pageAnalysisLoop, this);
}

GPTAgent::~GPTAgent() {
    _stop.store(true);
    _cv.notify_all();
    if (_worker.joinable()) {
        _worker.join();
    }
}

void GPTAgent::resetPromise(PromiseIntPtr promInt, PromiseActionPtr promAction) {
    _promiseInt = std::move(promInt);
    _promiseAction = std::move(promAction);
}

std::string GPTAgent::promptTypeForAsk(AskModel type) {
    switch (type) {
        case AskModel::REANALYSIS:
            return "llmdroid_reanalysis";
        case AskModel::GUIDE:
            return "llmdroid_guide";
        case AskModel::TEST_FUNCTION:
            return "llmdroid_test_function";
        case AskModel::STATE_OVERVIEW:
        default:
            return "llmdroid_state_overview";
    }
}

void GPTAgent::pushStateToQueue(QuestionPayload payload) {
    if (!_llm) {
        BLOG("GPTAgent: no LlmClient, drop payload (enable max.llm.* HTTP for LLMDroid overview)");
        return;
    }

    if (payload.type == AskModel::REANALYSIS) {
        std::unique_lock<std::mutex> lock(_mtx);
        if (!payload.from) {
            return;
        }
        const int targetId = payload.from->getId();
        const auto end = _topValuedMergedState->begin() +
                           static_cast<std::ptrdiff_t>(std::min<size_t>(static_cast<size_t>(_P2 + 1UL),
                                                                       _topValuedMergedState->size()));
        const auto found = std::find_if(_topValuedMergedState->begin(), end,
                                        [targetId](const MergedStatePtr &ms) { return ms && ms->getId() == targetId; });
        if (found != end) {
            {
                std::lock_guard<std::mutex> qc(_questionMtx);
                _questionRemained++;
                _lowQueue.push(std::move(payload));
            }
            lock.unlock();
            _cv.notify_one();
            BLOG("[LLMDroid] push M%d to low queue", targetId);
        }
    } else if (payload.type == AskModel::GUIDE || payload.type == AskModel::TEST_FUNCTION) {
        {
            std::lock_guard<std::mutex> qc(_questionMtx);
            _questionRemained++;
            std::lock_guard<std::mutex> lock(_mtx);
            _stateQueue.push(std::move(payload));
        }
        _cv.notify_one();
        BLOG("[LLMDroid] push GUIDE/TEST_FUNCTION to high queue");
    } else {
        {
            std::lock_guard<std::mutex> qc(_questionMtx);
            _questionRemained++;
            std::lock_guard<std::mutex> lock(_mtx);
            _stateQueue.push(std::move(payload));
        }
        _cv.notify_one();
        BLOG("[LLMDroid] push to high queue");
    }
}

void GPTAgent::waitUntilQueueEmpty() {
    BLOG("[LLMDroid] waitUntilQueueEmpty");
    for (;;) {
        int rem = 0;
        {
            std::lock_guard<std::mutex> qc(_questionMtx);
            rem = _questionRemained;
        }
        if (rem == 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

void GPTAgent::addTestedFunction() {
    _testedFunctions.insert(_targetFunction);
    MergedStatePtr ms = _mergedStateGraph ? _mergedStateGraph->findMergedStateById(_targetMergedStateId) : nullptr;
    if (ms) {
        ms->updateCompletedFunction(_targetFunction);
    }
}

void GPTAgent::clearExecutedEvents() { _executedFunctions.clear(); }

void GPTAgent::pageAnalysisLoop() {
    while (!_stop.load()) {
        QuestionPayload payload;
        {
            std::unique_lock<std::mutex> lock(_mtx);
            if (!_cv.wait_for(lock, std::chrono::seconds(1),
                              [this] { return _stop.load() || !_stateQueue.empty() || !_lowQueue.empty(); })) {
                continue;
            }
            if (_stop.load()) {
                break;
            }
            if (!_stateQueue.empty()) {
                payload = _stateQueue.front();
                _stateQueue.pop();
            } else if (!_lowQueue.empty()) {
                payload = _lowQueue.front();
                _lowQueue.pop();
            } else {
                continue;
            }
        }

        switch (payload.type) {
            case AskModel::STATE_OVERVIEW:
                askForStateOverview(payload);
                break;
            case AskModel::REANALYSIS:
                askForReanalysis(payload);
                break;
            case AskModel::GUIDE:
                askForGuiding(payload);
                break;
            case AskModel::TEST_FUNCTION:
                askForTestFunction(payload);
                break;
            default:
                break;
        }

        std::lock_guard<std::mutex> qc(_questionMtx);
        _questionRemained--;
    }
}

nlohmann::json GPTAgent::getResponseJson(const std::string &prompt, AskModel type) {
    if (!_llm || prompt.empty()) {
        return {};
    }
    nlohmann::json wrapper = nlohmann::json::object();
    wrapper["prompt"] = prompt;
    return getResponseJson(wrapper, type);
}

nlohmann::json GPTAgent::getResponseJson(const nlohmann::json &payload, AskModel type) {
    if (!_llm || payload.is_null()) {
        return {};
    }
    constexpr int kMaxAttempts = 3;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        std::string raw;
        if (!_llm->predictWithPayload(promptTypeForAsk(type), payload.dump(), {}, raw)) {
            BLOGE("GPTAgent: predictWithPayload failed (attempt %d/%d)", attempt, kMaxAttempts);
            continue;
        }
        std::string s = raw;
        const size_t l = s.find('{');
        const size_t r = s.rfind('}');
        if (l != std::string::npos && r != std::string::npos && r > l) {
            s = s.substr(l, r - l + 1);
        }
        try {
            return nlohmann::json::parse(s);
        } catch (const std::exception &e) {
            BLOGE("GPTAgent: JSON parse error (attempt %d/%d): %s", attempt, kMaxAttempts, e.what());
            BLOGE("GPTAgent: JSON parse raw response (attempt %d/%d): %s", attempt, kMaxAttempts, raw.c_str());
        }
    }
    return {};
}

void GPTAgent::askForStateOverview(QuestionPayload &payload) {
    if (!payload.from) {
        return;
    }
    BLOG("[LLMDroid] askForStateOverview MergedState%d", payload.from->getId());

    std::string stateDesc = payload.from->stateDescription();
    stateDesc = trimStateDescription(std::move(stateDesc), 7000);

    nlohmann::json req = nlohmann::json::object();
    req["start_prompt"] = _startPrompt;
    req["state_desc"] = stateDesc;
    req["use_top5"] = false;

    const bool useTop5 = _topValuedMergedState->size() >= 5;
    if (useTop5) {
        nlohmann::json top5 = nlohmann::json::object();
        int count = 0;
        for (size_t i = 0; i < _topValuedMergedState->size() && count < 5; ++i) {
            MergedStatePtr ms = (*_topValuedMergedState)[i];
            if (ms && ms->hasUntestedFunctions()) {
                ms->writeOverviewAndTop5Tojson(top5);
                count++;
            }
        }
        req["use_top5"] = true;
        req["current_state_id"] = payload.from->getId();
        req["top_pages"] = top5;
    }

    nlohmann::json jsonResponse = getResponseJson(req, AskModel::STATE_OVERVIEW);
    if (jsonResponse.empty()) {
        return;
    }

    payload.from->updateFromStateOverview(jsonResponse);

    if (useTop5) {
        std::vector<int> topList;
        const char *topKey = jsonResponse.contains("Top5") ? "Top5" : (jsonResponse.contains("Top 5") ? "Top 5" : nullptr);
        if (topKey) {
            try {
                topList = jsonResponse[topKey].get<std::vector<int>>();
            } catch (const std::exception &) {
                try {
                    for (const auto &el : jsonResponse[topKey]) {
                        if (el.is_string()) {
                            const std::string str = el.get<std::string>();
                            if (str.size() > 5 && str.compare(0, 5, "State") == 0) {
                                topList.push_back(std::stoi(str.substr(5)));
                            }
                        }
                    }
                } catch (const std::exception &) {
                    BLOG("[LLMDroid] Top5 parse failed, skip reorder");
                }
            }
        }
        if (topList.size() > 0 && _topValuedMergedState->size() >= 5) {
            std::vector<MergedStatePtr> originalFirstFive(_topValuedMergedState->begin(),
                                                          _topValuedMergedState->begin() + 5);
            for (size_t i = 0; i < topList.size() && i < 5; i++) {
                MergedStatePtr mergedState =
                        _mergedStateGraph ? _mergedStateGraph->findMergedStateById(topList[i]) : nullptr;
                if (mergedState) {
                    (*_topValuedMergedState)[i] = mergedState;
                }
            }
            std::vector<MergedStatePtr> elementsToInsert;
            for (const auto &elem : originalFirstFive) {
                if (!elem) {
                    continue;
                }
                const int id = elem->getId();
                if (std::find(topList.begin(), topList.end(), id) == topList.end()) {
                    elementsToInsert.push_back(elem);
                }
            }
            _topValuedMergedState->insert(_topValuedMergedState->begin() + 5, elementsToInsert.begin(),
                                          elementsToInsert.end());
        }
    } else {
        _topValuedMergedState->push_back(payload.from);
    }
}

void GPTAgent::askForReanalysis(QuestionPayload &payload) {
    if (!payload.from || !_mergedStateGraph) {
        return;
    }
    BLOG("[LLMDroid] askForReanalysis MergedState%d", payload.from->getId());

    std::unordered_map<int, WidgetInfo> widgetsDict;
    int id = 1;
    ReuseStatePtr rootState = payload.from->getRootState();
    if (!rootState) {
        return;
    }
    for (const auto &state : payload.from->getReuseStates()) {
        if (!state) {
            continue;
        }
        for (const auto &widget : state->diffWidgets(rootState)) {
            widgetsDict[id] = WidgetInfo{"", state, -1, widget};
            id++;
        }
    }

    if (widgetsDict.empty()) {
        BLOG("[LLMDroid] reanalysis: no diff widgets vs root");
        return;
    }

    std::unordered_map<std::string, std::vector<int>> uniqueWidgets;
    for (const auto &widgetPair : widgetsDict) {
        const int wid = widgetPair.first;
        const auto &widgetInfo = widgetPair.second;
        if (!widgetInfo.widget) {
            continue;
        }
        std::string html = widgetInfo.widget->toHTML({}, false, 0);
        uniqueWidgets[html].push_back(wid);
    }

    std::ostringstream controlsHtml;
    for (const auto &item : uniqueWidgets) {
        const int widgetId = item.second[0];
        auto it = widgetsDict.find(widgetId);
        if (it != widgetsDict.end() && it->second.widget) {
            controlsHtml << it->second.widget->toHTML({}, true, widgetId);
        }
    }

    nlohmann::json req = nlohmann::json::object();
    req["start_prompt"] = _startPrompt;
    req["overview_and_function_list"] = payload.from->toJson();
    req["controls_html"] = controlsHtml.str();

    nlohmann::json jsonResp = getResponseJson(req, AskModel::REANALYSIS);
    if (jsonResp.empty()) {
        return;
    }
    payload.from->updateFromReanalysis(jsonResp, uniqueWidgets, widgetsDict);
}

void GPTAgent::addExecutedEvent(const std::string &html, int widget_id, const ActivityStateActionPtr &act) {
    if (!act) {
        return;
    }
    std::istringstream stream(html);
    std::string line;
    const std::string target = "id=" + std::to_string(widget_id);
    while (std::getline(stream, line)) {
        if (line.find(target) != std::string::npos) {
            std::istringstream line_stream(line);
            std::string cell;
            std::string last_cell;
            while (std::getline(line_stream, cell, '\t')) {
                last_cell = std::move(cell);
            }
            _executedFunctions.push_back(act->toDescription() + " " + last_cell);
            break;
        }
    }
}

void GPTAgent::askForGuiding(QuestionPayload & /*payload*/) {
    if (!_promiseInt) {
        return;
    }
    BLOG("[LLMDroid] askForGuiding (worker)");
    nlohmann::json jsonData = nlohmann::json::object();
    const int end = static_cast<int>(std::min<size_t>(static_cast<size_t>(_P2), _topValuedMergedState->size()));
    int count = 0;
    for (size_t i = 0; i < _topValuedMergedState->size(); i++) {
        MergedStatePtr ms = (*_topValuedMergedState)[i];
        if (ms && ms->hasUntestedFunctions()) {
            ms->writeOverviewAndTop5Tojson(jsonData);
            count++;
        }
        if (count >= end) {
            break;
        }
    }
    count = 0;
    if (jsonData.empty()) {
        for (size_t i = 0; i < _topValuedMergedState->size(); i++) {
            MergedStatePtr ms = (*_topValuedMergedState)[i];
            if (ms) {
                ms->writeOverviewAndTop5Tojson(jsonData, true);
                count++;
            }
            if (count >= end) {
                break;
            }
        }
    }
    nlohmann::json tested = nlohmann::json::array();
    for (const auto &tf : _testedFunctions) {
        tested.push_back(tf);
    }
    nlohmann::json req = nlohmann::json::object();
    req["start_prompt"] = _startPrompt;
    req["state_informations"] = jsonData;
    req["tested_functions"] = tested;

    nlohmann::json jsonResponse = getResponseJson(req, AskModel::GUIDE);
    if (jsonResponse.empty() || !jsonResponse.contains("Target State") || !jsonResponse.contains("Target Function")) {
        _promiseInt->set_value(-1);
        return;
    }
    try {
        if (jsonResponse["Target State"].is_number_integer()) {
            _targetMergedStateId = jsonResponse["Target State"].get<int>();
        } else {
            const std::string targetState = jsonResponse["Target State"].get<std::string>();
            if (targetState.size() > 5 && targetState.compare(0, 5, "State") == 0) {
                _targetMergedStateId = std::stoi(targetState.substr(5));
            } else {
                _targetMergedStateId = -1;
            }
        }
        _targetFunction = jsonResponse["Target Function"].is_string()
                              ? jsonResponse["Target Function"].get<std::string>()
                              : std::string();
    } catch (const std::exception &) {
        _promiseInt->set_value(-1);
        return;
    }
    if (_targetMergedStateId < 0 || _targetFunction.empty()) {
        _promiseInt->set_value(-1);
        return;
    }
    MergedStatePtr destination =
            _mergedStateGraph ? _mergedStateGraph->findMergedStateById(_targetMergedStateId) : nullptr;
    if (!destination) {
        _promiseInt->set_value(-1);
        return;
    }
    ReuseStatePtr rs = destination->getTargetState(_targetFunction);
    _promiseInt->set_value(rs ? rs->getIdi() : -1);
}

void GPTAgent::askForTestFunction(QuestionPayload &payload) {
    if (!_promiseAction || !payload.reuseState) {
        return;
    }
    BLOG("[LLMDroid] askForTestFunction (worker)");
    const std::string html = payload.reuseState->getStateDescriptionForMergedState();
    nlohmann::json executed = nlohmann::json::array();
    for (const auto &f : _executedFunctions) {
        executed.push_back(f);
    }
    nlohmann::json req = nlohmann::json::object();
    req["start_prompt"] = _startPrompt;
    req["page_desc"] = html;
    req["target_function"] = _targetFunction;
    req["executed_functions"] = executed;

    nlohmann::json jsonResponse = getResponseJson(req, AskModel::TEST_FUNCTION);
    if (jsonResponse.empty() || !jsonResponse.contains("Element Id") || !jsonResponse.contains("Action Type")) {
        _promiseAction->set_value(nullptr);
        return;
    }
    int elementId = -1;
    int actionTypeRaw = 0;
    try {
        if (jsonResponse["Element Id"].is_string()) {
            elementId = std::stoi(jsonResponse["Element Id"].get<std::string>());
        } else {
            elementId = jsonResponse["Element Id"].get<int>();
        }
        if (jsonResponse["Action Type"].is_string()) {
            actionTypeRaw = std::stoi(jsonResponse["Action Type"].get<std::string>());
        } else {
            actionTypeRaw = jsonResponse["Action Type"].get<int>();
        }
    } catch (const std::exception &) {
        _promiseAction->set_value(nullptr);
        return;
    }
    if (actionTypeRaw < 0 || actionTypeRaw > 6) {
        BLOG("LLMDroid TEST_FUNCTION: invalid action type %d", actionTypeRaw);
        _promiseAction->set_value(nullptr);
        return;
    }
    int actionType = static_cast<int>(ActionType::CLICK) + actionTypeRaw;
    if (actionTypeRaw == 6) {
        actionType = static_cast<int>(ActionType::CLICK);
    }
    if (elementId == -1) {
        _promiseAction->set_value(nullptr);
        return;
    }
    const int actionIdx = payload.reuseState->findActionByElementId(elementId, actionType);
    ActivityStateActionPtr ret = nullptr;
    if (actionIdx == -1) {
        BLOG("LLMDroid TEST_FUNCTION: no action for elementId=%d type=%d", elementId, actionType);
    } else {
        ret = std::dynamic_pointer_cast<ActivityStateAction>(payload.reuseState->getActions()[static_cast<size_t>(actionIdx)]);
        if (jsonResponse.contains("Input") && ret) {
            try {
                ret->setInputText(jsonResponse["Input"].get<std::string>());
            } catch (const std::exception &) {
            }
        }
        if (ret) {
            addExecutedEvent(html, elementId, ret);
        }
    }
    _promiseAction->set_value(ret);
}

} // namespace fastbotx
