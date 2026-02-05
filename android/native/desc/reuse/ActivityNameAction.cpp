/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef ActivityNameAction_CPP_
#define ActivityNameAction_CPP_

#include  "ActivityNameAction.h"

#include <utility>
#include "../utils.hpp"
#include "ActionFilter.h"

namespace fastbotx {

    ActivityNameAction::ActivityNameAction()
            : ActivityStateAction(), _activity(nullptr) {

    }

    /**
     * @brief Constructor creates ActivityNameAction with activity name
     * 
     * Creates an action that includes activity name in its hash computation.
     * This allows actions to be uniquely identified by activity + widget + action type.
     * 
     * Hash computation:
     * - Combines activity hash, action type hash, and target widget hash
     * - Uses bit shifting for better hash distribution
     * 
     * @param activity Activity name string pointer (moved to avoid copy)
     * @param widget Target widget (nullptr for actions without targets)
     * @param act Action type
     */
    ActivityNameAction::ActivityNameAction(stringPtr activity, const WidgetPtr &widget,
                                           ActionType act)
            : ActivityStateAction(nullptr, widget, act), _activity(std::move(activity)) {
        // Compute hash components
        // Performance optimization: Use fast string hash instead of std::hash
        uintptr_t activityHashCode = fastbotx::fastStringHash(*(_activity.get()));
        uintptr_t actionHashCode = std::hash<int>{}(static_cast<int>(this->getActionType()));
        uintptr_t targetHash = (widget != nullptr) ? widget->hash() : 0x1;

        // Combine hashes with bit shifting for better distribution
        // Magic number 0x9e3779b9 is a common hash mixing constant
        this->_hashcode = 0x9e3779b9 + (activityHashCode << 2) ^
                          (((actionHashCode << 6) ^ (targetHash << 1)) << 1);
    }

    ActivityNameAction::~ActivityNameAction()
    = default;

} // namespace fastbot


#endif // ActivityNameAction_CPP_
