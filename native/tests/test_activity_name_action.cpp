/*
 * Unit tests for ActivityNameAction class
 */
#include <gtest/gtest.h>
#include "../desc/reuse/ActivityNameAction.h"
#include "../desc/State.h"
#include "../desc/StateFactory.h"
#include "../desc/Element.h"
#include "../desc/Widget.h"
#include <memory>

using namespace fastbotx;

class ActivityNameActionTest : public ::testing::Test {
protected:
    void SetUp() override {
        activityName = std::make_shared<std::string>("TestActivity");
        auto elem = std::make_shared<Element>();
        elem->reSetBounds(std::make_shared<Rect>(0, 0, 100, 100));
        elem->reSetClickable(true);
        state = StateFactory::createState(AlgorithmType::Reuse, activityName, elem);
        widget = std::make_shared<Widget>(nullptr, elem);
    }
    
    void TearDown() override {}
    
    stringPtr activityName;
    StatePtr state;
    WidgetPtr widget;
};

TEST_F(ActivityNameActionTest, Constructor) {
    ActivityNameActionPtr action = std::make_shared<ActivityNameAction>(
        activityName, widget, ActionType::CLICK);
    EXPECT_NE(action, nullptr);
    EXPECT_EQ(action->getActionType(), ActionType::CLICK);
}

TEST_F(ActivityNameActionTest, GetActivity) {
    ActivityNameActionPtr action = std::make_shared<ActivityNameAction>(
        activityName, widget, ActionType::CLICK);
    EXPECT_EQ(action->getActivity(), activityName);
}

TEST_F(ActivityNameActionTest, GetTarget) {
    ActivityNameActionPtr action = std::make_shared<ActivityNameAction>(
        activityName, widget, ActionType::CLICK);
    EXPECT_EQ(action->getTarget(), widget);
}

TEST_F(ActivityNameActionTest, HashFunction) {
    ActivityNameActionPtr action1 = std::make_shared<ActivityNameAction>(
        activityName, widget, ActionType::CLICK);
    ActivityNameActionPtr action2 = std::make_shared<ActivityNameAction>(
        activityName, widget, ActionType::CLICK);
    
    EXPECT_EQ(action1->hash(), action2->hash());
}

TEST_F(ActivityNameActionTest, ToOperate) {
    ActivityNameActionPtr action = std::make_shared<ActivityNameAction>(
        activityName, widget, ActionType::CLICK);
    OperatePtr operate = action->toOperate();
    EXPECT_NE(operate, nullptr);
    EXPECT_EQ(operate->act, ActionType::CLICK);
}
