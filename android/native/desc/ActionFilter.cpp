/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef ActionFilter_CPP_
#define ActionFilter_CPP_

#include "ActionFilter.h"
#include "State.h"

namespace fastbotx {
    /**
     * @brief Check if action is enabled, valid, and not saturated
     * 
     * An action is included if:
     * 1. It is enabled
     * 2. It is valid
     * 3. Its state is still valid (not expired)
     * 4. It is not saturated (hasn't been visited too many times)
     * 
     * Performance optimization:
     * - Checks state expiration before locking to avoid unnecessary lock
     * 
     * @param action Action to check
     * @return true if action should be included
     */
    bool ActionFilterValidUnSaturated::include(ActivityStateActionPtr action) const {
        // Check basic validity first (fast checks)
        bool ret = action->getEnabled() && action->isValid();
        
        // Check state is still valid (weak_ptr not expired)
        ret = ret && !action->getState().expired();
        
        // Check action is not saturated (more expensive check, requires state lock)
        if (ret) {
            ret = ret && !action->getState().lock()->isSaturated(action);
        }
        
        return ret;
    }

    ActionFilterPtr allFilter = ActionFilterPtr(new ActionFilterALL());
    ActionFilterPtr targetFilter = ActionFilterPtr(new ActionFilterTarget());
    ActionFilterPtr validFilter = ActionFilterPtr(new ActionFilterValid());
    ActionFilterPtr enableValidFilter = ActionFilterPtr(new ActionFilterEnableValid());
    ActionFilterPtr enableValidUnvisitedFilter = ActionFilterPtr(new ActionFilterUnvisitedValid());
    ActionFilterPtr enableValidUnSaturatedFilter = ActionFilterPtr(
            new ActionFilterValidUnSaturated());
    ActionFilterPtr enableValidValuePriorityFilter = ActionFilterPtr(
            new ActionFilterValidValuePriority());
    ActionFilterPtr validDatePriorityFilter = ActionFilterPtr(new ActionFilterValidDatePriority());

}
#endif
