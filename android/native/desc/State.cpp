/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef State_CPP_
#define State_CPP_

#include "../Base.h"
#include  "State.h"
#include "../utils.hpp"
#include "ActionFilter.h"
#include <regex>
#include <map>
#include <algorithm>
#include <utility>
#include <cmath>
#include <sstream>

namespace fastbotx {

    State::State()
            : Node(), _hasNoDetail(false) {
    }

    State::State(stringPtr activityName)
            : Node(), _activity(std::move(activityName)), _hasNoDetail(false) {
        BLOG("create state");
    }

    /**
     * @brief Merge duplicate widgets and store merged ones
     * 
     * Identifies duplicate widgets (by hash) and stores them in the merged widgets map.
     * This is used for widget deduplication to optimize state comparison and action selection.
     * 
     * Performance optimization:
     * - Uses set for O(log n) duplicate detection
     * - Only processes if merge is enabled and widgets exist
     * 
     * @param mergeWidgets Set to track unique widgets (output parameter)
     * @return Number of widgets that were merged (duplicates found)
     */
    int State::mergeWidgetAndStoreMergedOnes(WidgetPtrSet &mergeWidgets) {
        int mergedWidgetCount = 0;
        if (STATE_MERGE_DETAIL_TEXT && !this->_widgets.empty()) {
            for (const auto &widgetPtr: this->_widgets) {
                // Try to insert widget into set (returns false if duplicate)
                auto noMerged = mergeWidgets.emplace(widgetPtr).second;
                if (!noMerged) {
                    // Widget is a duplicate, store in merged widgets map
                    uintptr_t h = widgetPtr->hash();
                    mergedWidgetCount++;
                    
                    // Performance: Use find instead of count + at to avoid double lookup
                    auto mergedIt = this->_mergedWidgets.find(h);
                    if (mergedIt == this->_mergedWidgets.end()) {
                        // First duplicate for this hash, create new vector
                        WidgetPtrVec tempWidgetVector;
                        tempWidgetVector.emplace_back(widgetPtr);
                        this->_mergedWidgets.emplace(h, std::move(tempWidgetVector));
                    } else {
                        // Additional duplicate, add to existing vector
                        mergedIt->second.emplace_back(widgetPtr);
                    }
                }
            }
        }
        return mergedWidgetCount;
    }

    /**
     * @brief Factory method to create a State from Element and activity name
     * 
     * Creates a new State object by:
     * 1. Building widget tree from Element
     * 2. Merging duplicate widgets
     * 3. Computing state hash
     * 4. Creating actions for all widgets
     * 5. Adding back action
     * 
     * Performance optimizations:
     * - Uses move semantics for activity name
     * - Efficient widget merging
     * - Pre-allocates action vector capacity if possible
     * 
     * @param elem Root Element of the UI hierarchy
     * @param activityName Activity name string pointer
     * @return Shared pointer to created State
     */
    StatePtr State::create(ElementPtr elem, stringPtr activityName) {
        // Use new + shared_ptr instead of make_shared because constructor is protected
        // and make_shared cannot access protected constructors from outside the class
        StatePtr sharedPtr = std::shared_ptr<State>(new State(std::move(activityName)));
        sharedPtr->buildFromElement(nullptr, std::move(elem));
        
        // Compute hash based on activity name
        uintptr_t activityHash =
                (std::hash<std::string>{}(*(sharedPtr->_activity.get())) * 31U) << 5;
        
        // Merge duplicate widgets for optimization
        WidgetPtrSet mergedWidgets;
        int mergedWidgetCount = sharedPtr->mergeWidgetAndStoreMergedOnes(mergedWidgets);
        if (mergedWidgetCount != 0) {
            BDLOG("build state merged  %d widget", mergedWidgetCount);
            // Use merged widgets (deduplicated) instead of original widgets
            sharedPtr->_widgets.assign(mergedWidgets.begin(), mergedWidgets.end());
        }
        
        // Combine activity hash with widget hash
        activityHash ^= (combineHash<Widget>(sharedPtr->_widgets, STATE_WITH_WIDGET_ORDER) << 1);
        sharedPtr->_hashcode = activityHash;
        
        // Build actions for all widgets
        // Performance: Could pre-allocate capacity if we can estimate action count
        for (const auto &w: sharedPtr->_widgets) {
            if (w->getBounds() == nullptr) {
                BLOGE("NULL Bounds happened");
                continue;
            }
            // Create action for each action type supported by this widget
            for (ActionType act: w->getActions()) {
                ActivityStateActionPtr modelAction = std::make_shared<ActivityStateAction>(
                        sharedPtr, w, act);
                sharedPtr->_actions.emplace_back(modelAction);
            }
        }
        
        // Always add back action for navigation
        sharedPtr->_backAction = std::make_shared<ActivityStateAction>(sharedPtr, nullptr,
                                                                       ActionType::BACK);
        sharedPtr->_actions.emplace_back(sharedPtr->_backAction);

        return sharedPtr;
    }

    /**
     * @brief Check if an action is saturated (visited too many times)
     * 
     * An action is considered saturated if:
     * - For actions without targets: visited at least once
     * - For actions with targets: visited more times than the number of merged widgets
     *   with the same hash (to account for duplicate widgets)
     * 
     * Performance optimization:
     * - Uses find instead of count + at to avoid double lookup
     * 
     * @param action Action to check
     * @return true if action is saturated (should be avoided)
     */
    bool State::isSaturated(const ActivityStateActionPtr &action) const {
        if (!action->requireTarget()) {
            // Actions without targets are saturated if visited at least once
            return action->isVisited();
        }
        if (nullptr != action->getTarget()) {
            uintptr_t h = action->getTarget()->hash();
            // Performance: Use find instead of count + at
            auto mergedIt = this->_mergedWidgets.find(h);
            if (mergedIt != this->_mergedWidgets.end()) {
                // Action is saturated if visited more times than merged widget count
                return action->getVisitedCount() > static_cast<int>(mergedIt->second.size());
            }
        }
        // Default: saturated if visited at least once
        return action->getVisitedCount() >= 1;
    }

    RectPtr State::_sameRootBounds = std::make_shared<Rect>();

    void State::buildFromElement(WidgetPtr parentWidget, ElementPtr elem) {
        if (elem->getParent().expired() && !(elem->getBounds()->isEmpty())) {
            if (_sameRootBounds.get()->isEmpty() && elem) {
                _sameRootBounds = elem->getBounds();
            }
            if (equals(_sameRootBounds, elem->getBounds())) {
                this->_rootBounds = _sameRootBounds;
            } else
                this->_rootBounds = elem->getBounds();
        }
        WidgetPtr widget = nullptr;
        widget = std::make_shared<Widget>(parentWidget, elem);
        this->_widgets.emplace_back(widget);
        for (const auto &childElement: elem->getChildren()) {
            buildFromElement(widget, childElement);
        }
    }

    uintptr_t State::hash() const {
        return this->_hashcode;
    }

    bool State::operator<(const State &state) const {
        return this->hash() < state.hash();
    }

    State::~State() {
        this->_activity.reset();
        this->_actions.clear();
        this->_backAction = nullptr;
        this->_widgets.clear();

        this->_mergedWidgets.clear();
    }


    void State::clearDetails() {
        for (auto const &widget: this->_widgets) {
            widget->clearDetails();
        }
        this->_mergedWidgets.clear();
        _hasNoDetail = true;
    }

    void State::fillDetails(const std::shared_ptr<State> &copy) {
        for (auto widgetPtr: this->_widgets) {
            auto widgetIterator = std::find_if(copy->_widgets.begin(), copy->_widgets.end(),
                                               [&widgetPtr](const WidgetPtr &cw) {
                                                   return *(cw.get()) == *widgetPtr;
                                               });
            if (widgetIterator != copy->_widgets.end()) {
                widgetPtr->fillDetails(*widgetIterator);
            } else {
                LOGE("ERROR can not refill widget");
            }
        }
        for (const auto &miter: this->_mergedWidgets) {
            auto mkw = copy->_mergedWidgets.find(miter.first);
            if (mkw == copy->_mergedWidgets.end())
                continue;
            for (auto widgetPtr: miter.second) {
                auto widgetIterator = std::find_if((*mkw).second.begin(), (*mkw).second.end(),
                                                   [&widgetPtr](const WidgetPtr &cw) {
                                                       return *(cw.get()) == *widgetPtr;
                                                   });
                if (widgetIterator != (*mkw).second.end()) {
                    widgetPtr->fillDetails(*widgetIterator);
                }
            }

        }
        _hasNoDetail = false;
    }

    std::string State::toString() const {
        std::ostringstream oss;
        oss << "{state: " << this->hash() << "\n    widgets: \n";
        for (auto const &widget: this->_widgets) {
            oss << "   " << widget->toString() << "\n";
        }
        oss << "action: \n";
        for (auto const &action: this->_actions) {
            oss << "   " << action->toString() << "\n";
        }
        oss << "\n}";
        return oss.str();
    }


    // for algorithm
    int State::countActionPriority(const ActionFilterPtr &filter, bool includeBack) const {
        int totalP = 0;
        for (const auto &action: this->_actions) {
            if (!includeBack && action->isBack()) {
                continue;
            }
            if (filter->include(action)) {
                int fp = filter->getPriority(action);
                if (fp <= 0) {
                    BDLOG("Error: Action should has a positive priority, but we get %d", fp);
                    return -1;
                }
                totalP += fp;
            }
        }
        return totalP;
    }

    ActivityStateActionPtrVec State::targetActions() const {
        ActivityStateActionPtrVec retV;
        ActionFilterPtr filter = targetFilter; //(ActionFilterPtr(new ActionFilterTarget());)
        for (const auto &a: this->_actions) {
            if (filter->include(a))
                retV.emplace_back(a);
        }
        return retV;
    }

    ActivityStateActionPtr State::greedyPickMaxQValue(const ActionFilterPtr &filter) const {
        ActivityStateActionPtr retA;
        long maxvalue = 0;
        for (const auto &m: this->_actions) {
            if (!filter->include(m))
                continue;
            if (filter->getPriority(m) > maxvalue) {
                maxvalue = filter->getPriority(m);
                retA = m;
            }
        }
        return retA;
    }

    ActivityStateActionPtr State::randomPickAction(const ActionFilterPtr &filter) const {
        return this->randomPickAction(filter, true);
    }

    ActivityStateActionPtr
    State::randomPickAction(const ActionFilterPtr &filter, bool includeBack) const {
        int total = this->countActionPriority(filter, includeBack);
        if (total == 0)
            return nullptr;
        // Use thread-local random number generator for better performance
        int index = randomInt(0, total);
        return pickAction(filter, includeBack, index);
    }

    ActivityStateActionPtr
    State::pickAction(const ActionFilterPtr &filter, bool includeBack, int index) const {
        int ii = index;
        for (auto action: this->_actions) {
            if (!includeBack && action->isBack())
                continue;
            if (filter->include(action)) {
                int p = filter->getPriority(action);
                if (p > ii)
                    return action;
                else
                    ii = ii - p;
            }
        }
        BDLOG("%s", "ERROR: action filter is unstable");
        return nullptr;
    }

    ActivityStateActionPtr State::randomPickUnvisitedAction() const {
        ActivityStateActionPtr action = this->randomPickAction(enableValidUnvisitedFilter, false);
        if (action == nullptr && enableValidUnvisitedFilter->include(getBackAction())) {
            action = getBackAction();
        }
        return action;
    }


    ActivityStateActionPtr State::resolveAt(ActivityStateActionPtr action, time_t /*t*/) {
        if (action->getTarget() == nullptr)
            return action;
        uintptr_t h = action->getTarget()->hash();
        auto targetWidgets = this->_mergedWidgets.find(h);
        if (targetWidgets == this->_mergedWidgets.end()) {
            return action;
        }
        int total = (int) (this->_mergedWidgets.at(h).size());
        int index = action->getVisitedCount() % total;
        BLOG("resolve a merged widget %d/%d for action %s", index, total, action->getId().c_str());
        action->setTarget(this->_mergedWidgets.at(h)[index]);
        return action;
    }

    bool State::containsTarget(const WidgetPtr &widget) const {
        for (const auto &w: this->_widgets) {
            if (equals(w, widget))
                return true;
        }
        return false;
    }

    PropertyIDPrefixImpl(State, "g0s");

    bool State::operator==(const State &state) const {
        return this->hash() == state.hash();
    }


} // namespace fastbot


#endif // State_CPP_
