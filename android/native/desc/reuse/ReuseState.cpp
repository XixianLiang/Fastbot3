/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef ReuseState_CPP_
#define ReuseState_CPP_

#include "ReuseState.h"

#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <functional>
#include <sstream>
#include "RichWidget.h"
#include "ActivityNameAction.h"
#include "Action.h"
#include "../utils.hpp"
#include "ActionFilter.h"
#include "../events/Preference.h"

namespace fastbotx {


    ReuseState::ReuseState()
    = default;

    ReuseState::ReuseState(stringPtr activityName)
            : ReuseState() {
        this->_activity = std::move(activityName);
        this->_hasNoDetail = false;
    }

    /**
     * @brief Build bounding box for root element
     * 
     * Sets the root bounds for this state. If the element is the root (no parent)
     * and has valid bounds, stores them. Uses shared root bounds if available.
     * 
     * Performance optimization:
     * - Checks bounds validity before accessing
     * - Avoids multiple getBounds() calls
     * 
     * @param element Element to get bounds from
     */
    void ReuseState::buildBoundingBox(const ElementPtr &element) {
        // Check if this is the root element (no parent)
        if (element->getParent().expired()) {
            RectPtr bounds = element->getBounds();
            // Check bounds validity before using
            if (bounds != nullptr && !bounds->isEmpty()) {
                // Initialize shared root bounds if empty
                if (_sameRootBounds->isEmpty() && element) {
                    _sameRootBounds = bounds;
                }
                // Use shared bounds if they match, otherwise use element's bounds
                if (equals(_sameRootBounds, bounds)) {
                    this->_rootBounds = _sameRootBounds;
                } else {
                    this->_rootBounds = bounds;
                }
            }
        }
    }

    void ReuseState::buildStateFromElement(WidgetPtr parentWidget, ElementPtr element) {
        buildBoundingBox(element);
        WidgetPtr widget = std::make_shared<RichWidget>(parentWidget, element);
        _elementPtrToWidget[element.get()] = widget;
        this->_widgets.emplace_back(widget);
        for (const auto &childElement: element->getChildren()) {
            buildFromElement(widget, childElement);
        }
    }

    /**
     * @brief Build widget tree from Element (using regular Widget for children)
     * 
     * This method is used for building child widgets after the root uses RichWidget.
     * It uses regular Widget instead of RichWidget for performance optimization.
     * 
     * Performance optimization:
     * - Removed unnecessary dynamic_pointer_cast (elem is already ElementPtr)
     * - Direct use of elem parameter
     * 
     * @param parentWidget Parent widget
     * @param elem Element to build widget from (already ElementPtr, no cast needed)
     */
    void ReuseState::buildFromElement(WidgetPtr parentWidget, ElementPtr elem) {
        buildBoundingBox(elem);
        WidgetPtr widget;
        if (Preference::inst() && Preference::inst()->useStaticReuseAbstraction()) {
            // Legacy static reuse mode: use RichWidget for all nodes so hash matches legacy ReuseWidget style
            widget = std::make_shared<RichWidget>(parentWidget, elem);
        } else {
            // Dynamic abstraction mode: still use lightweight Widget for non-root nodes
            widget = std::make_shared<Widget>(parentWidget, elem);
        }
        this->_widgets.emplace_back(widget);
        _elementPtrToWidget[elem.get()] = widget;
        for (const auto &childElement: elem->getChildren()) {
            buildFromElement(widget, childElement);
        }
    }

    /**
     * @brief Factory method to create a ReuseState from Element and activity name
     * 
     * Creates a new ReuseState object by:
     * 1. Building widget tree from Element (using RichWidget)
     * 2. Merging duplicate widgets
     * 3. Computing state hash
     * 4. Creating actions for all widgets
     * 
     * @param element Root Element of the UI hierarchy (XML of this page)
     * @param activityName Activity name string pointer
     * @return Shared pointer to newly created ReuseState
     */
    ReuseStatePtr ReuseState::create(const ElementPtr &element, const stringPtr &activityName,
                                     WidgetKeyMask mask) {
        // Use new + shared_ptr instead of make_shared because constructor is protected
        ReuseStatePtr statePointer = std::shared_ptr<ReuseState>(new ReuseState(activityName));
        statePointer->_widgetKeyMask = mask;
        statePointer->buildState(element);
        return statePointer;
    }

    void ReuseState::buildState(const ElementPtr &element) {
        _rootElement = element;
        _elementByStableId.clear();
        _elementPtrToWidget.clear();
        _widgetPtrToStableElementId.clear();
        buildStateFromElement(nullptr, element);
        mergeWidgetsInState();
        rebuildElementIdMaps(element);
        buildHashForState();
        buildActionForState();
    }

    /**
     * @brief Build hash code for this state
     * 
     * Computes hash code based on activity name and widget collection.
     * The hash is used for state comparison and deduplication.
     * 
     * Performance optimization:
     * - Uses efficient hash combination with bit shifting
     * - Combines activity hash with widget hash
     */
    void ReuseState::buildHashForState() {
        // Build hash from activity name (guard against null _activity)
        std::string activityString = (_activity && _activity.get()) ? *_activity : "";
        uintptr_t activityHash = (fastbotx::fastStringHash(activityString) * 31U) << 5;

#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        // In static reuse abstraction mode, ignore WidgetKeyMask and fall back to legacy hash
        if (Preference::inst() && Preference::inst()->useStaticReuseAbstraction()) {
            activityHash ^= (combineHash<Widget>(_widgets, STATE_WITH_WIDGET_ORDER) << 1);
        } else {
            uintptr_t widgetsHash = 0x1;
            for (const auto &w : _widgets) {
                if (w) {
                    widgetsHash ^= w->hashWithMask(_widgetKeyMask);
                }
            }
            activityHash ^= (widgetsHash << 1);
        }
#else
        // Combine with widget hash (may include order if STATE_WITH_WIDGET_ORDER is enabled)
        activityHash ^= (combineHash<Widget>(_widgets, STATE_WITH_WIDGET_ORDER) << 1);
#endif
        _hashcode = activityHash;
    }

    /**
     * @brief Build actions for all widgets in this state
     * 
     * Creates ActivityNameAction objects for each action type supported by each widget.
     * ActivityNameAction includes the activity name for reuse-based algorithms.
     * Also creates and adds a back action for navigation.
     * 
     * Performance optimizations:
     * - Pre-allocates _actions vector capacity to avoid reallocations
     * - Uses make_shared instead of new + shared_ptr (single memory allocation)
     * - Uses emplace_back() to construct objects in-place (avoids copy)
     * - Skips widgets with null bounds
     */
    void ReuseState::buildActionForState() {
        // Performance: Cache activity string (shared_ptr copy) once instead of per action
        stringPtr activityStr = getActivityString();
        // Performance: Heuristic reserve to avoid first-pass count; typical ~2–4 actions per widget
        _actions.reserve(_widgets.size() * 4 + 1);

        for (const auto &widget : _widgets) {
            RectPtr bounds = widget->getBounds();
            if (bounds == nullptr) {
                BLOGE("NULL Bounds happened");
                continue;
            }
            // getActions() returns const ref — no set copy per widget
            const std::set<ActionType> &actions = widget->getActions();
            for (ActionType action : actions) {
                _actions.emplace_back(std::make_shared<ActivityNameAction>(activityStr, widget, action));
            }
        }

        _backAction = std::make_shared<ActivityNameAction>(activityStr, nullptr, ActionType::BACK);
        _actions.emplace_back(_backAction);
    }

    /**
     * @brief Merge duplicate widgets in this state
     * 
     * Identifies and merges duplicate widgets (by hash) to optimize state comparison.
     * Replaces the widget vector with deduplicated widgets.
     * 
     * Performance optimization:
     * - Reduces widget count by removing duplicates
     * - Improves hash computation and state comparison speed
     */
    void ReuseState::mergeWidgetsInState() {
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        if (Preference::inst() && Preference::inst()->useStaticReuseAbstraction()) {
            // Static reuse abstraction: fall back to legacy merge-by-Widget::hash behavior
            WidgetPtrSet mergedWidgets;
            int mergedCount = mergeWidgetAndStoreMergedOnes(mergedWidgets);
            if (mergedCount != 0) {
                BDLOG("build state merged  %d widget", mergedCount);
                _widgets.assign(mergedWidgets.begin(), mergedWidgets.end());
            }
        } else {
            std::unordered_map<uintptr_t, WidgetPtr> uniqueByMaskHash;
            WidgetPtrVec uniqueWidgets;
            int mergedCount = 0;
            for (const auto &w : _widgets) {
                if (!w) continue;
                uintptr_t keyMask = w->hashWithMask(_widgetKeyMask);
                auto it = uniqueByMaskHash.find(keyMask);
                if (it == uniqueByMaskHash.end()) {
                    uniqueByMaskHash[keyMask] = w;
                    uniqueWidgets.push_back(w);
                } else {
                    mergedCount++;
                    WidgetPtr representative = it->second;
                    uintptr_t repHash = representative->hash();
                    auto mergedIt = _mergedWidgets.find(repHash);
                    if (mergedIt == _mergedWidgets.end()) {
                        WidgetPtrVec vec;
                        vec.push_back(representative);
                        vec.push_back(w);
                        _mergedWidgets[repHash] = std::move(vec);
                    } else {
                        mergedIt->second.push_back(w);
                    }
                }
            }
            if (mergedCount != 0) {
                BDLOG("build state merged  %d widget", mergedCount);
                _widgets = std::move(uniqueWidgets);
            }
        }
#else
        WidgetPtrSet mergedWidgets;
        int mergedCount = mergeWidgetAndStoreMergedOnes(mergedWidgets);
        if (mergedCount != 0) {
            BDLOG("build state merged  %d widget", mergedCount);
            _widgets.assign(mergedWidgets.begin(), mergedWidgets.end());
        }
#endif
    }

    size_t ReuseState::getMaxWidgetsPerModelAction() const {
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        size_t maxCount = 1;
        for (const auto &p : _mergedWidgets) {
            if (p.second.size() > maxCount) {
                maxCount = p.second.size();
            }
        }
        return maxCount;
#else
        return State::getMaxWidgetsPerModelAction();
#endif
    }

#if DYNAMIC_STATE_ABSTRACTION_ENABLED
    uintptr_t ReuseState::getHashUnderMask(WidgetKeyMask mask) const {
        if (usesDynamicAbstractionIdentityHash()) {
            return hash();
        }
        std::string activityString = (_activity && _activity.get()) ? *_activity : "";
        // Performance optimization: Use fast string hash instead of std::hash
        uintptr_t activityHash = (fastbotx::fastStringHash(activityString) * 31U) << 5;
        uintptr_t widgetsHash = 0x1;
        for (const auto &w : _widgets) {
            if (w) {
                widgetsHash ^= w->hashWithMask(mask);
            }
        }
        return activityHash ^ (widgetsHash << 1);
    }

    size_t ReuseState::getUniqueWidgetCountUnderMask(WidgetKeyMask mask) const {
        std::unordered_set<uintptr_t> seen;
        for (const auto &w : _widgets) {
            if (w) seen.insert(w->hashWithMask(mask));
        }
        return seen.size();
    }
#endif

    void ReuseState::rebuildElementIdMaps(const ElementPtr &root) {
        _elementByStableId.clear();
        _widgetPtrToStableElementId.clear();
        if (!root) {
            return;
        }
        int next = 0;
        const std::function<void(const ElementPtr &)> dfs = [&](const ElementPtr &e) {
            if (!e) {
                return;
            }
            e->setStableElementId(next);
            _elementByStableId[next] = e;
            next++;
            for (const auto &c : e->getChildren()) {
                dfs(c);
            }
        };
        dfs(root);
        for (const auto &kv : _elementByStableId) {
            const int stableId = kv.first;
            const ElementPtr &elem = kv.second;
            if (!elem) {
                continue;
            }
            auto wit = _elementPtrToWidget.find(elem.get());
            if (wit == _elementPtrToWidget.end() || !wit->second) {
                continue;
            }
            _widgetPtrToStableElementId[wit->second.get()] = stableId;
        }
    }

    void ReuseState::addMiniEdge(MiniGraphEdge edge) {
        _miniEdges.push_back(std::move(edge));
    }

    MiniGraphEdge *ReuseState::getUnvisitedMiniEdge() {
        for (size_t i = 0; i < _miniEdges.size(); i++) {
            if (!_miniEdges[i].isVisited) {
                return &(_miniEdges[i]);
            }
        }
        return nullptr;
    }

    std::vector<WidgetPtr> ReuseState::diffWidgets(const ReuseStatePtr &target) {
        std::vector<WidgetPtr> ret;
        if (!target) {
            return ret;
        }
        for (const WidgetPtr &widget : _widgets) {
            if (!widget) {
                continue;
            }
            const uintptr_t h = widget->hash();
            const auto found = std::find_if(target->_widgets.begin(), target->_widgets.end(),
                                            [h](const WidgetPtr &w) { return w && w->hash() == h; });
            if (found == target->_widgets.end()) {
                ret.push_back(widget);
            }
        }
        return ret;
    }

    std::string ReuseState::getStateDescriptionForMergedState() const {
        std::ostringstream ss;
        if (getActivityString() && getActivityString().get()) {
            ss << "[Activity: " << *getActivityString() << "]\n";
        }
        ss << "[State" << getIdi() << "]\n";
        for (const auto &w : _widgets) {
            if (!w) {
                continue;
            }
            ss << "- " << w->getClassname() << " id=" << w->getResourceID()
               << " text=\"" << w->getText() << "\"\n";
        }
        return ss.str();
    }

    float ReuseState::computeSimilarityForMergedState(const ReuseStatePtr &target) const {
        if (!target) {
            return 0.f;
        }
        size_t matchedCount = 0;
        const bool bigger = target->_widgets.size() > _widgets.size();
        const WidgetPtrVec &toCompare = bigger ? target->_widgets : _widgets;
        const WidgetPtrVec &candidates = bigger ? _widgets : target->_widgets;
        const WidgetPtrVecMap &toCompareMap = bigger ? target->_mergedWidgets : _mergedWidgets;

        for (WidgetPtr candidate : candidates) {
            if (!candidate) {
                continue;
            }
            const uintptr_t ch = candidate->hash();
            const bool inWidgets =
                    std::any_of(toCompare.begin(), toCompare.end(),
                                [ch](const WidgetPtr &ptr) { return ptr && ptr->hash() == ch; });
            bool inMergedWidgets = false;
            auto mit = toCompareMap.find(ch);
            if (mit != toCompareMap.end()) {
                for (const WidgetPtr &ptr : mit->second) {
                    if (ptr && ptr->hash() == ch) {
                        inMergedWidgets = true;
                        break;
                    }
                }
            }
            if (inWidgets || inMergedWidgets) {
                matchedCount++;
            }
        }
        const size_t denom = toCompare.size() + candidates.size();
        return denom == 0 ? 0.f
                          : static_cast<float>(matchedCount * 2) / static_cast<float>(denom);
    }

    ElementPtr ReuseState::findElementById(int id) const {
        auto it = _elementByStableId.find(id);
        return it == _elementByStableId.end() ? ElementPtr() : it->second;
    }

    WidgetPtr ReuseState::getWidgetForElement(const ElementPtr &element) const {
        if (!element) {
            return nullptr;
        }
        auto it = _elementPtrToWidget.find(element.get());
        return it == _elementPtrToWidget.end() ? nullptr : it->second;
    }

    int ReuseState::getStableElementIdForWidget(const WidgetPtr &widget) const {
        if (!widget) {
            return -1;
        }
        auto it = _widgetPtrToStableElementId.find(widget.get());
        return it == _widgetPtrToStableElementId.end() ? -1 : it->second;
    }

    int ReuseState::findWhichWidget(WidgetPtr target) const {
        if (!target) {
            return -3;
        }
        auto found = std::find_if(_widgets.begin(), _widgets.end(),
                                  [target](const WidgetPtr &ptr) { return ptr.get() == target.get(); });
        if (found != _widgets.end()) {
            return -1;
        }
        const uintptr_t h = target->hash();
        if (_mergedWidgets.find(h) == _mergedWidgets.end()) {
            return -2;
        }
        WidgetPtrVec mergedOnes = _mergedWidgets.at(h);
        found = std::find_if(mergedOnes.begin(), mergedOnes.end(),
                             [target](const WidgetPtr &ptr) { return ptr.get() == target.get(); });
        if (found == mergedOnes.end()) {
            return -3;
        }
        return static_cast<int>(found - mergedOnes.begin());
    }

    WidgetPtr ReuseState::findWidgetByHashAndLocation(uintptr_t hash, int location) const {
        auto found = std::find_if(_widgets.begin(), _widgets.end(),
                                  [hash](const WidgetPtr &ptr) { return ptr && ptr->hash() == hash; });
        if (found == _widgets.end()) {
            return nullptr;
        }
        if (location == -1) {
            return *found;
        }
        auto mit = _mergedWidgets.find(hash);
        if (mit == _mergedWidgets.end()) {
            return nullptr;
        }
        const WidgetPtrVec &vec = mit->second;
        if (location >= static_cast<int>(vec.size())) {
            return vec.back();
        }
        return vec[static_cast<size_t>(location)];
    }

    std::vector<WidgetPtr> ReuseState::getAllWidgets() const {
        std::vector<WidgetPtr> ret(_widgets);
        for (const WidgetPtr &widget : _widgets) {
            if (!widget) {
                continue;
            }
            auto mit = _mergedWidgets.find(widget->hash());
            if (mit != _mergedWidgets.end()) {
                ret.insert(ret.end(), mit->second.begin(), mit->second.end());
            }
        }
        return ret;
    }

    std::vector<ActivityStateActionPtr> ReuseState::findActionsByWidget(WidgetPtr widget) const {
        std::vector<ActivityStateActionPtr> ret;
        if (!widget) {
            return ret;
        }
        const uintptr_t h = widget->hash();
        for (const auto &it : _actions) {
            if (it->getTarget() && it->getTarget()->hash() == h) {
                ret.push_back(it);
            }
        }
        return ret;
    }

    int ReuseState::findActionByElementId(int elementId, int actionType) {
        ElementPtr element = findElementById(elementId);
        if (!element) {
            BLOG("ReuseState::findActionByElementId: no element id=%d in state%d", elementId, getIdi());
            return -1;
        }
        WidgetPtr widget = getWidgetForElement(element);
        if (!widget) {
            BLOGE("ReuseState::findActionByElementId: no widget for element id=%d", elementId);
            return -1;
        }
        const int whichWidget = findWhichWidget(widget);
        if (whichWidget < -1) {
            BLOGE("ReuseState::findActionByElementId: whichWidget=%d", whichWidget);
            return -1;
        }
        auto action = std::find_if(_actions.begin(), _actions.end(),
                                   [widget, actionType](const ActivityStateActionPtr &ptr) {
                                       if (!ptr->getTarget()) {
                                           return false;
                                       }
                                       return widget->hash() == ptr->getTarget()->hash() &&
                                              ptr->getActionType() == static_cast<ActionType>(actionType);
                                   });
        if (action == _actions.end()) {
            return -1;
        }
        (*action)->setWhichWidget(whichWidget);
        (*action)->setTarget(widget);
        return static_cast<int>(action - _actions.begin());
    }

    void ReuseState::addSubSequentState(const ReuseStatePtr &state) {
        if (!state) {
            return;
        }
        ActionPtr action = this->_actionToPerform;
        if (!action) {
            action = Action::NOP;
            BLOG("ReuseState::addSubSequentState: _actionToPerform null, using NOP (state id=%d)", getIdi());
        }
        const uintptr_t edgeHash = action->hash() + state->hash();
        const auto itSet = this->_existedStateGraphEdges.find(edgeHash);
        if (itSet != this->_existedStateGraphEdges.end()) {
            auto edge = std::find_if(_edges.begin(), _edges.end(),
                                     [edgeHash](const StateGraphEdge &e) { return e.hash == edgeHash; });
            if (edge != _edges.end()) {
                edge->remainTimes++;
            }
            return;
        }
        ActivityStateActionPtr tmp = std::dynamic_pointer_cast<ActivityStateAction>(action);
        int whichWidget = -1;
        if (tmp) {
            whichWidget = tmp->getWhichWidget();
        }
        this->_edges.push_back(StateGraphEdge{action, state, 1, false, edgeHash, whichWidget, currentStamp()});
        this->_existedStateGraphEdges.insert(edgeHash);
    }

    float ReuseState::computeSimilarity(const ReuseStatePtr &target) const {
        if (!target) {
            return 0.f;
        }
        size_t matchedCount = 0;
        const bool bigger = target->_widgets.size() > _widgets.size();
        const WidgetPtrVec &toCompare = bigger ? target->_widgets : _widgets;
        const WidgetPtrVecMap &toCompareMap = bigger ? target->_mergedWidgets : _mergedWidgets;
        const WidgetPtrVec &candidates = bigger ? _widgets : target->_widgets;

        for (const WidgetPtr &candidate : candidates) {
            if (!candidate) {
                continue;
            }
            const uintptr_t ch = candidate->hash();
            auto inWidgets = std::find_if(toCompare.begin(), toCompare.end(),
                                          [ch](const WidgetPtr &ptr) { return ptr && ptr->hash() == ch; });
            bool inMergedWidgets = false;
            auto mit = toCompareMap.find(ch);
            if (mit != toCompareMap.end()) {
                for (const WidgetPtr &ptr : mit->second) {
                    if (ptr && ptr->hash() == ch) {
                        inMergedWidgets = true;
                        break;
                    }
                }
            }
            if (inWidgets != toCompare.end() || inMergedWidgets) {
                matchedCount++;
            }
        }
        const size_t denom = toCompare.size() + candidates.size();
        return denom == 0 ? 0.f
                          : static_cast<float>(matchedCount * 2) / static_cast<float>(denom);
    }

    ActivityStateActionPtr ReuseState::findActionByWidgetHash(uintptr_t h, ActionType actionType) const {
        for (const ActivityStateActionPtr &a : _actions) {
            if (!a || !a->getTarget()) {
                continue;
            }
            if (a->getTarget()->hash() == h && a->getActionType() == actionType) {
                return a;
            }
        }
        return nullptr;
    }

    ActionPtr ReuseState::findSimilarAction(const ActionPtr &origin) {
        if (!origin) {
            return nullptr;
        }
        if (origin->getActionType() == ActionType::BACK) {
            auto found = std::find_if(_actions.begin(), _actions.end(), [](const ActivityStateActionPtr &elem) {
                return elem && elem->getActionType() == ActionType::BACK;
            });
            return found != _actions.end() ? *found : nullptr;
        }
        if (!origin->requireTarget()) {
            return origin;
        }
        ActivityStateActionPtr action = std::dynamic_pointer_cast<ActivityStateAction>(origin);
        if (!action || !action->getTarget()) {
            return nullptr;
        }
        const uintptr_t h = action->getTarget()->hash();
        auto found = std::find_if(_widgets.begin(), _widgets.end(),
                                  [h](const WidgetPtr &widget) { return widget && widget->hash() == h; });
        if (found == _widgets.end()) {
            return nullptr;
        }
        ActivityStateActionPtr ret = findActionByWidgetHash(h, origin->getActionType());
        if (!ret) {
            return nullptr;
        }
        if (action->hasInput()) {
            ret->setInputText(action->getInputText());
        }
        const int originIndex = action->getWhichWidget();
        auto targetWidgets = this->_mergedWidgets.find(h);
        if (targetWidgets == this->_mergedWidgets.end()) {
            ret->setTarget(*found);
            ret->setWhichWidget(originIndex);
            return ret;
        }
        const int total = static_cast<int>(this->_mergedWidgets.at(h).size());
        if (originIndex == -1) {
            ret->setTarget(*found);
            ret->setWhichWidget(originIndex);
            return ret;
        }
        if (originIndex < total) {
            ret->setTarget(this->_mergedWidgets.at(h)[static_cast<size_t>(originIndex)]);
            ret->setWhichWidget(originIndex);
            return ret;
        }
        ret->setTarget(this->_mergedWidgets.at(h)[static_cast<size_t>(total - 1)]);
        ret->setWhichWidget(total - 1);
        return ret;
    }

} // namespace fastbotx


#endif // ReuseState_CPP_
