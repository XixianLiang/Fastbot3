/**
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 */

#include "MergedState.h"

#include "ReuseState.h"
#include "../events/Preference.h"
#include "../utils.hpp"

#include <algorithm>
#include <exception>
#include <sstream>

namespace fastbotx {

MergedStateGraphEdge::MergedStateGraphEdge(MergedStatePtr next, ActionPtr action, bool shouldStop) {
    _next = std::move(next);
    _action = std::move(action);
    _hash = (_action ? _action->hash() : 0) | (_next ? _next->hash() : 0);
    _isVisited = false;
    _shouldStop = shouldStop;
}

MergedState::MergedState(ReuseStatePtr state, int id) {
    _states.insert(state);
    _root = std::move(state);
    _cursor = _root;
    _id = id;
    _starts.push_back(_cursor);
    _hashcode = _root->hash();
}

std::string MergedState::getOverview() const {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    return _overview;
}

std::map<std::string, FunctionDetail> MergedState::getFunctionList() const {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    return _functionList;
}

void MergedState::addState(ReuseStatePtr state, ActionPtr action, bool fromOutside, bool toOutside) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);

    if (fromOutside) {
        _starts.push_back(state);
    }

    _cursor->_miniEdges.push_back({state, action, false});

    if (!toOutside) {
        auto success = _states.insert(state);
        _cursor = state;
        if (success.second && !_functionList.empty()) {
            _needReanalysed = true;
            updateLaterJoinedState(state);
        }
    }
}

void MergedState::addEdge(MergedStateGraphEdgePtr edge) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    _edges.push_back(std::move(edge));
}

void MergedState::addPrevious(MergedStatePtr state) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    _previous.insert(std::move(state));
}

void MergedState::addNext(MergedStatePtr state) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    _next.insert(std::move(state));
}

std::string MergedState::stateDescription() {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    return _root->getStateDescriptionForMergedState();
}

std::string MergedState::walk() {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);

    std::stringstream graphStream;
    for (ReuseStatePtr cursor : _starts) {
        graphStream << "State" << cursor->getIdi();
        MiniGraphEdge *edge = cursor->getUnvisitedMiniEdge();
        while (edge && cursor->getMergedState() && cursor->getMergedState()->getId() == _id) {
            std::string actionstr = edge->action ? edge->action->toDescription() : "null";
            graphStream << " -- " << actionstr << " --> ";
            graphStream << "State" << edge->next->getIdi();
            cursor = edge->next;
            edge->isVisited = true;
            edge = cursor->getUnvisitedMiniEdge();
        }
        graphStream << "\n";
    }
    reset();
    return graphStream.str();
}

void MergedState::reset() {
    for (ReuseStatePtr state : _states) {
        for (MiniGraphEdge &edge : state->_miniEdges) {
            edge.isVisited = false;
        }
    }
}

void MergedState::updateFromStateOverview(nlohmann::json &jsonData) {
    const PreferencePtr pref = Preference::inst();
    if (!pref || !pref->isLlmdroidEnabled()) {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);

    std::string overview = jsonData["Overview"];
    std::vector<std::pair<std::string, int>> functionList;
    nlohmann::json jsonFunctionList = jsonData["Function List"];
    for (auto it = jsonFunctionList.begin(); it != jsonFunctionList.end(); ++it) {
        if (it.value().is_number()) {
            functionList.push_back(std::make_pair(it.key(), it.value()));
            BLOG("[Number]%s", it.key().c_str());
        } else if (it.value().is_array()) {
            functionList.push_back(std::make_pair(it.key(), it.value().front()));
            BLOG("[Vector]%s", it.key().c_str());
        } else {
            BLOG("[ErrorType]%s", it.key().c_str());
        }
    }

    _overview = overview;

    int size = static_cast<int>(functionList.size());
    for (int i = 0; i < size; i++) {
        auto it = _functionList.find(functionList[i].first);
        if (it == _functionList.end()) {
            _functionList.insert(std::make_pair(functionList[i].first,
                                                FunctionDetail{size - i, _root}));
        }
    }

    filterFunctionList();

    updateNavigationCount();

    setFunctionToWidget(functionList);
    BLOG("setFunctionToWidget complete!");

    updateCompletedFunctions2();
    BLOG("updateFromStateOverview complete!");
}

void MergedState::updateCompletedFunctions(std::map<std::string, int> completedFunctions) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    for (auto it : completedFunctions) {
        auto found = _functionList.find(it.first);
        if (found != _functionList.end()) {
            found->second.importance = it.second;
        } else {
            _functionList.insert(std::make_pair(it.first, FunctionDetail{it.second, _root}));
        }
    }
}

MergedStateGraphEdgePtr MergedState::getUnvisitedEdge() {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    for (const auto &edge : _edges) {
        if (!edge->_isVisited) {
            return edge;
        }
    }
    return nullptr;
}

void MergedState::updateCompletedFunction(std::string func) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    auto it = _functionList.find(func);
    if (it == _functionList.end()) {
        _functionList.insert(std::make_pair(func, FunctionDetail{0, _root}));
    } else {
        it->second.importance = 0;
    }
}

int MergedState::getNavigationValue() const {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    return _navigationValue;
}

void MergedState::updateNavigationValue(int total) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    int weight = total - _id;
    _navigationValue = weight * _navigationCount;
}

void MergedState::filterFunctionList() {
    std::set<std::string> keyToDelete;

    for (const auto &pair : _functionList) {
        size_t startPos = pair.first.find("**");
        if (startPos != std::string::npos) {
            size_t endPos = pair.first.rfind("**");
            if (endPos != std::string::npos && endPos > startPos) {
                std::string strippedKey = pair.first.substr(startPos + 2, endPos - startPos - 2);
                if (_functionList.find(strippedKey) != _functionList.end()) {
                    keyToDelete.insert(strippedKey);
                }
            }
        }
    }

    for (const auto &k : keyToDelete) {
        _functionList.erase(k);
    }
}

void MergedState::updateNavigationCount() {
    int navigateFunctionNum = 0;
    for (const auto &it : _functionList) {
        if (it.first.find("navigate") != std::string::npos) {
            navigateFunctionNum++;
        }
    }
    _navigationCount = navigateFunctionNum;
}

void MergedState::updateCompletedFunctions2() {
    const PreferencePtr pref = Preference::inst();
    if (!pref || !pref->isLlmdroidEnabled()) {
        return;
    }
    for (ReuseStatePtr state : _states) {
        for (ActivityStateActionPtr action : state->getActions()) {
            action->setListener(shared_from_this());
            updateCompletedFunction2(0, action);
        }
    }
}

void MergedState::setFunctionToWidget(const std::vector<std::pair<std::string, int>> &functionList) {
    for (size_t i = 0; i < functionList.size(); i++) {
        if (_functionList.find(functionList[i].first) == _functionList.end()) {
            continue;
        }
        ElementPtr element = _root->findElementById(functionList[i].second);
        if (!element) {
            BLOG("can't find element%d for function%s", functionList[i].second, functionList[i].first.c_str());
            continue;
        }
        WidgetPtr widget = _root->getWidgetForElement(element);
        if (!widget) {
            BLOG("can't map element%d to widget for function%s", functionList[i].second,
                 functionList[i].first.c_str());
            continue;
        }
        std::string function = functionList[i].first;
        widget->setFunctionLabel(function);
        BLOG("successfully set function: %s to root's widget", function.c_str());

        int whichWidget = _root->findWhichWidget(widget);
        if (whichWidget < -1) {
            BLOG("[setFunctionToWidget] can't find widget in root%d", _root->getIdi());
            continue;
        }

        for (ReuseStatePtr state : _states) {
            if (state == _root) {
                continue;
            }
            WidgetPtr similarWidget = state->findWidgetByHashAndLocation(widget->hash(), whichWidget);
            if (similarWidget) {
                similarWidget->setFunctionLabel(function);
                BLOG("successfully set function: %s to R%d's widget", function.c_str(), state->getIdi());
            } else {
                BLOG("widget:%s doesn't have similar one in R%d", widget->toHTML().c_str(), state->getIdi());
            }
        }
    }
}

void MergedState::onActionExecuted(ActivityStateActionPtr action) {
    updateCompletedFunction2(0, action);
}

void MergedState::updateCompletedFunction2(int /*caller*/, ActivityStateActionPtr action) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    if (action->getVisitedCount() > 0) {
        WidgetPtr widget = action->getTarget();
        if (!widget) {
            return;
        }
        std::string function = widget->getFunctionLabel();
        if (!function.empty()) {
            auto it = _functionList.find(function);
            if (it != _functionList.end()) {
                it->second.importance = 0;
                BLOG("Function:%s is tested by perform: %s", function.c_str(),
                     action->toDescription().c_str());
            }
        }
    }
}

void MergedState::writeOverviewAndTop5Tojson(nlohmann::json &top5, bool ignoreImportance) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    std::string key = "State" + std::to_string(_id);
    top5[key]["Overview"] = _overview;
    auto sortedFunctions = sortFunctionsByValue(ignoreImportance);
    if (sortedFunctions.size() > 5) {
        sortedFunctions.resize(5);
    }
    top5[key]["FunctionList"] = sortedFunctions;
}

nlohmann::json MergedState::toJson() {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    nlohmann::json data;
    data["Overview"] = _overview;
    std::vector<std::string> functions;
    for (auto &it : _functionList) {
        functions.push_back(it.first);
    }
    data["Function List"] = functions;
    return data;
}

std::vector<std::string> MergedState::sortFunctionsByValue(bool ignoreImportance) {
    std::vector<std::pair<int, std::string>> pairs;

    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    for (const auto &kvp : _functionList) {
        if (kvp.second.importance > 0 || ignoreImportance) {
            pairs.push_back(std::make_pair(kvp.second.importance, kvp.first));
        }
    }

    std::sort(pairs.begin(), pairs.end(),
              [](const std::pair<int, std::string> &a, const std::pair<int, std::string> &b) {
                  return a.first > b.first;
              });

    std::vector<std::string> sortedKeys;
    for (const auto &pair : pairs) {
        sortedKeys.push_back(pair.second);
    }

    return sortedKeys;
}

void MergedState::updateLaterJoinedState(ReuseStatePtr state) {
    const PreferencePtr pref = Preference::inst();
    if (!pref || !pref->isLlmdroidEnabled()) {
        return;
    }
    for (WidgetPtr rootWidget : _root->getAllWidgets()) {
        std::string function = rootWidget->getFunctionLabel();
        if (function.empty()) {
            continue;
        }
        int whichWidget = _root->findWhichWidget(rootWidget);
        if (whichWidget < -1) {
            BLOG("[updateLaterJoinedState] can't find widget in root%d", _root->getIdi());
            continue;
        }
        WidgetPtr similarWidget = state->findWidgetByHashAndLocation(rootWidget->hash(), whichWidget);
        if (similarWidget) {
            similarWidget->setFunctionLabel(function);
            BLOG("successfully set function: %s to widget: %s", function.c_str(),
                 similarWidget->toHTML().c_str());
        } else {
            BLOG("widget:%s doesn't have similar one in R%d", rootWidget->toHTML().c_str(), state->getIdi());
        }
    }

    for (ActivityStateActionPtr action : state->getActions()) {
        action->setListener(shared_from_this());
    }
}

bool MergedState::hasUntestedFunctions() const {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    bool flag = false;
    for (const auto &it : _functionList) {
        if (it.second.importance > 0) {
            flag = true;
            break;
        }
    }
    return flag;
}

void MergedState::updateFromReanalysis(nlohmann::json &jsonResp,
                                        std::unordered_map<std::string, std::vector<int>> &uniqueWidgets,
                                        std::unordered_map<int, WidgetInfo> &widgetDict) {
    const PreferencePtr pref = Preference::inst();
    if (!pref || !pref->isLlmdroidEnabled()) {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);

    for (auto &it : jsonResp.items()) {
        try {
            int id = std::stoi(it.key());
            std::string function = it.value();
            if (_functionList.find(function) == _functionList.end()) {
                _functionList.insert(std::make_pair(function, FunctionDetail{1, widgetDict.at(id).state}));
            }
            std::vector<int> widgetIds = uniqueWidgets[widgetDict.at(id).widget->toHTML({}, false, 0)];
            for (int widgetId : widgetIds) {
                WidgetPtr widget = widgetDict[widgetId].widget;
                widget->setFunctionLabel(function);

                for (auto &action : widgetDict[widgetId].state->findActionsByWidget(widget)) {
                    updateCompletedFunction2(0, action);
                }
            }
        } catch (const std::exception &e) {
            BLOG("[Exception]: %s, skip this kv-pair", e.what());
        }
    }
    _needReanalysed = false;
}

bool MergedState::needReanalysed() {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    return _needReanalysed;
}

MergedStateGraph::MergedStateGraph(GraphPtr graph) {
    BLOG("MergedStateGraph init");
    _root = nullptr;
    _cursor = nullptr;
    _gptCursor = nullptr;
    _lastState = nullptr;
    _graph = std::move(graph);
}

void MergedStateGraph::addNode(MergedStatePtr mergedState, ActionPtr action, bool timeup) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);
    if (mergedState == _cursor) {
        return;
    }

    _mergedStates.insert(mergedState);
    if (_root == nullptr) {
        _root = _cursor = mergedState;
        _gptCursor = mergedState;
        BLOG("***first add node: MergedState%d", _gptCursor->getId());
    } else {
        _cursor->addNext(mergedState);
        mergedState->addPrevious(_cursor);
        _cursor->addEdge(std::make_shared<MergedStateGraphEdge>(mergedState, action, timeup));
        BLOG("add edge from MergedState%d to MergedState%d timeup %d", _cursor->getId(), mergedState->getId(),
             timeup);
        _lastState = _cursor;
        _cursor = mergedState;
    }
}

MergedStatePtr MergedStateGraph::findMergedStateById(int id) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);

    auto it = std::find_if(_mergedStates.begin(), _mergedStates.end(),
                           [id](const MergedStatePtr &state) { return id == state->getId(); });
    if (it != _mergedStates.end()) {
        return *it;
    }
    BLOG("[THREAD] findMergedStateById: can't find id %d", id);
    return nullptr;
}

std::string MergedStateGraph::temporalWalk(int transitCount) {
    if (transitCount == 0) {
        return "No transition during this period.";
    }
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);
    std::string ret;
    ret.append("State").append(std::to_string(_gptCursor->getId()));

    BLOG("[THREAD] temporal walk begin %d", transitCount);
    MergedStateGraphEdgePtr edge;
    int count = 0;
    do {
        edge = _gptCursor->getUnvisitedEdge();

        if (edge) {
            int nid = edge->_next->getId();
            ret.append(" -- ").append(edge->_action->toDescription());
            ret.append(" --> State");
            ret.append(std::to_string(nid));

            edge->_isVisited = true;
            _gptCursor = edge->_next;
            ++count;
        }
    } while (edge && (count < transitCount));

    ret.append("\n");
    return ret;
}

void MergedStateGraph::appendUtgString(std::string value) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);
    _utgString.append(std::move(value)).append("\n");
}

std::string MergedStateGraph::getUtgString() const {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);
    return _utgString;
}

std::vector<Path> MergedStateGraph::findPaths(int reuseStateId, bool forceRestart) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateGraphMutex);
    if (!_graph) {
        return {};
    }
    return _graph->findPath(reuseStateId, forceRestart);
}

ReuseStatePtr MergedState::getTargetState(const std::string &function) {
    std::lock_guard<std::recursive_mutex> lock(_mergedStateMutex);
    auto it = _functionList.find(function);
    if (it != _functionList.end()) {
        return it->second.state;
    }
    BLOG("function{%s} doesn't belong to any state in MergedState{%d}", function.c_str(), _id);
    return nullptr;
}

} // namespace fastbotx
