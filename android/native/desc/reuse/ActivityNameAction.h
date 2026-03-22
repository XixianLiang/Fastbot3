/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef ActivityNameAction_H_
#define ActivityNameAction_H_

#include "Action.h"
#include "RichWidget.h"
#include <vector>


namespace fastbotx {

    /**
     * @brief ActivityNameAction extends ActivityStateAction with activity name
     * 
     * ActivityNameAction includes the activity name in addition to state, widget, and action type.
     * This is used in reuse-based algorithms where actions are identified by
     * activity name + widget + action type combination.
     */
    class ActivityNameAction : public ActivityStateAction {
    public:
        /**
         * @brief Constructor creates ActivityNameAction with activity name
         * 
         * @param activity Activity name string pointer
         * @param widget Target widget (nullptr for actions without targets)
         * @param act Action type
         */
        ActivityNameAction(stringPtr activity, const WidgetPtr &widget, ActionType act);

        /**
         * Recompute hash for dynamic (non-static) APE alignment: activity term uses the same string as
         * StateKey::activity() (canonical pkg/cls), target term uses abstract widget id (e.g. fastStringHash(Name::toXPath)).
         * Mixing formula matches the constructor. Static reuse mode keeps the original constructor hash.
         */
        void applyApeDynamicRlIdentity(uintptr_t activityCanonicalHash, uintptr_t abstractTargetHash);

        /**
         * @brief Get the activity name
         * 
         * @return Shared pointer to activity name string
         */
        stringPtr getActivity() const { return this->_activity; }

        /**
         * @brief Destructor
         */
        ~ActivityNameAction() override;

    protected:
        ActivityNameAction();

        stringPtr _activity;

    };

    typedef std::shared_ptr<ActivityNameAction> ActivityNameActionPtr;
    typedef std::vector<ActivityNameActionPtr> ActivityNameActionPtrVec;
    typedef std::set<ActivityNameActionPtr, Comparator<ActivityNameAction>> ActivityNameActionPtrSet;

}

#endif //ActivityNameAction_H_
