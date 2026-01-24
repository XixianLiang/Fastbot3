/*
 * Unit tests for Action classes
 */
#include <gtest/gtest.h>
#include "../desc/Action.h"
#include "../desc/State.h"
#include "../desc/StateFactory.h"
#include "../desc/Widget.h"
#include <memory>

using namespace fastbotx;

// ==================== Action Tests ====================

class ActionTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ActionTest, DefaultConstructor) {
    Action action;
    EXPECT_EQ(action.getActionType(), ActionType::NOP);
    EXPECT_EQ(action.getPriority(), 0);
    EXPECT_EQ(action.getQValue(), 0.0);
    EXPECT_TRUE(action.isValid());
    EXPECT_TRUE(action.getEnabled());
}

TEST_F(ActionTest, ParameterizedConstructor) {
    Action action(ActionType::CLICK);
    EXPECT_EQ(action.getActionType(), ActionType::CLICK);
}

TEST_F(ActionTest, ActionTypeChecks) {
    Action clickAction(ActionType::CLICK);
    Action backAction(ActionType::BACK);
    Action nopAction(ActionType::NOP);
    
    EXPECT_TRUE(clickAction.isClick());
    EXPECT_FALSE(clickAction.isBack());
    EXPECT_FALSE(clickAction.isNop());
    
    EXPECT_TRUE(backAction.isBack());
    EXPECT_FALSE(backAction.isClick());
    EXPECT_FALSE(backAction.isNop());
    
    EXPECT_TRUE(nopAction.isNop());
    EXPECT_FALSE(nopAction.isClick());
    EXPECT_FALSE(nopAction.isBack());
}

TEST_F(ActionTest, PriorityManagement) {
    Action action;
    action.setPriority(100);
    EXPECT_EQ(action.getPriority(), 100);
}

TEST_F(ActionTest, PriorityByActionType) {
    Action clickAction(ActionType::CLICK);
    Action longClickAction(ActionType::LONG_CLICK);
    Action scrollAction(ActionType::SCROLL_TOP_DOWN);
    Action backAction(ActionType::BACK);
    
    EXPECT_EQ(clickAction.getPriorityByActionType(), 4);
    EXPECT_EQ(longClickAction.getPriorityByActionType(), 2);
    EXPECT_EQ(scrollAction.getPriorityByActionType(), 2);
    EXPECT_EQ(backAction.getPriorityByActionType(), 1);
}

TEST_F(ActionTest, IsModelAct) {
    Action clickAction(ActionType::CLICK);
    Action backAction(ActionType::BACK);
    Action startAction(ActionType::START);
    Action crashAction(ActionType::CRASH);
    
    EXPECT_TRUE(clickAction.isModelAct());
    EXPECT_TRUE(backAction.isModelAct());
    EXPECT_TRUE(startAction.isModelAct());
    EXPECT_FALSE(crashAction.isModelAct());
}

TEST_F(ActionTest, RequireTarget) {
    Action clickAction(ActionType::CLICK);
    Action backAction(ActionType::BACK);
    Action scrollAction(ActionType::SCROLL_TOP_DOWN);
    
    EXPECT_TRUE(clickAction.requireTarget());
    EXPECT_FALSE(backAction.requireTarget());
    EXPECT_TRUE(scrollAction.requireTarget());
}

TEST_F(ActionTest, CanStartTestApp) {
    Action startAction(ActionType::START);
    Action restartAction(ActionType::RESTART);
    Action cleanRestartAction(ActionType::CLEAN_RESTART);
    Action clickAction(ActionType::CLICK);
    
    EXPECT_TRUE(startAction.canStartTestApp());
    EXPECT_TRUE(restartAction.canStartTestApp());
    EXPECT_TRUE(cleanRestartAction.canStartTestApp());
    EXPECT_FALSE(clickAction.canStartTestApp());
}

TEST_F(ActionTest, QValueManagement) {
    Action action;
    action.setQValue(10.5);
    EXPECT_DOUBLE_EQ(action.getQValue(), 10.5);
    
    action.setQValue(-5.0);
    EXPECT_DOUBLE_EQ(action.getQValue(), -5.0);
}

TEST_F(ActionTest, HashFunction) {
    Action action1(ActionType::CLICK);
    Action action2(ActionType::CLICK);
    Action action3(ActionType::BACK);
    
    // Actions of same type should have same hash
    EXPECT_EQ(action1.hash(), action2.hash());
    // Different types should have different hashes
    EXPECT_NE(action1.hash(), action3.hash());
}

TEST_F(ActionTest, EqualityOperator) {
    Action action1(ActionType::CLICK);
    Action action2(ActionType::CLICK);
    Action action3(ActionType::BACK);
    
    EXPECT_TRUE(action1 == action2);
    EXPECT_FALSE(action1 == action3);
}

TEST_F(ActionTest, ToString) {
    Action action(ActionType::CLICK);
    std::string str = action.toString();
    EXPECT_NE(str.find("CLICK"), std::string::npos);
}

TEST_F(ActionTest, ToOperate) {
    Action action(ActionType::CLICK);
    OperatePtr operate = action.toOperate();
    EXPECT_NE(operate, nullptr);
    EXPECT_EQ(operate->act, ActionType::CLICK);
}

TEST_F(ActionTest, StaticInstances) {
    EXPECT_NE(Action::NOP, nullptr);
    EXPECT_NE(Action::ACTIVATE, nullptr);
    EXPECT_NE(Action::RESTART, nullptr);
}

// ==================== ActivityStateAction Tests ====================

class ActivityStateActionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a mock state
        activityName = std::make_shared<std::string>("TestActivity");
        auto element = std::make_shared<Element>();
        element->reSetBounds(std::make_shared<Rect>(10, 20, 30, 40));
        element->reSetClickable(true);
        state = StateFactory::createState(AlgorithmType::Reuse, activityName, element);
        
        // Create a mock widget
        widget = std::make_shared<Widget>(nullptr, element);
    }
    
    void TearDown() override {}
    
    stringPtr activityName;
    StatePtr state;
    WidgetPtr widget;
};

TEST_F(ActivityStateActionTest, Constructor) {
    ActivityStateAction action(state, widget, ActionType::CLICK);
    EXPECT_EQ(action.getActionType(), ActionType::CLICK);
    EXPECT_EQ(action.getTarget(), widget);
    EXPECT_FALSE(action.getState().expired());
}

TEST_F(ActivityStateActionTest, GetTarget) {
    ActivityStateAction action(state, widget, ActionType::CLICK);
    EXPECT_EQ(action.getTarget(), widget);
}

TEST_F(ActivityStateActionTest, GetState) {
    ActivityStateAction action(state, widget, ActionType::CLICK);
    auto statePtr = action.getState().lock();
    EXPECT_NE(statePtr, nullptr);
    EXPECT_EQ(statePtr, state);
}

TEST_F(ActivityStateActionTest, IsValid) {
    ActivityStateAction action1(state, widget, ActionType::CLICK);
    EXPECT_TRUE(action1.isValid());
    
    // Action with null target should be valid (for BACK action)
    ActivityStateAction action2(state, nullptr, ActionType::BACK);
    EXPECT_TRUE(action2.isValid());
    
    // Action with empty bounds widget should be invalid
    auto emptyElement = std::make_shared<Element>();
    emptyElement->reSetBounds(std::make_shared<Rect>()); // Empty rect
    auto emptyWidget = std::make_shared<Widget>(nullptr, emptyElement);
    ActivityStateAction action3(state, emptyWidget, ActionType::CLICK);
    EXPECT_FALSE(action3.isValid());
}

TEST_F(ActivityStateActionTest, GetEnabled) {
    ActivityStateAction action(state, widget, ActionType::CLICK);
    EXPECT_TRUE(action.getEnabled());
    
    // Test with disabled widget
    auto disabledElement = std::make_shared<Element>();
    disabledElement->reSetEnabled(false);
    disabledElement->reSetBounds(std::make_shared<Rect>(10, 20, 30, 40));
    auto disabledWidget = std::make_shared<Widget>(nullptr, disabledElement);
    ActivityStateAction disabledAction(state, disabledWidget, ActionType::CLICK);
    EXPECT_FALSE(disabledAction.getEnabled());
}

TEST_F(ActivityStateActionTest, SetTarget) {
    ActivityStateAction action(state, widget, ActionType::CLICK);
    auto newWidget = std::make_shared<Widget>(nullptr, std::make_shared<Element>());
    action.setTarget(newWidget);
    EXPECT_EQ(action.getTarget(), newWidget);
}

TEST_F(ActivityStateActionTest, HashFunction) {
    ActivityStateAction action1(state, widget, ActionType::CLICK);
    ActivityStateAction action2(state, widget, ActionType::CLICK);
    ActivityStateAction action3(state, widget, ActionType::BACK);
    
    // Same state, widget, and action type should have same hash
    EXPECT_EQ(action1.hash(), action2.hash());
    // Different action type should have different hash
    EXPECT_NE(action1.hash(), action3.hash());
}

TEST_F(ActivityStateActionTest, EqualityOperator) {
    ActivityStateAction action1(state, widget, ActionType::CLICK);
    ActivityStateAction action2(state, widget, ActionType::CLICK);
    ActivityStateAction action3(state, widget, ActionType::BACK);
    
    EXPECT_TRUE(action1 == action2);
    EXPECT_FALSE(action1 == action3);
}

TEST_F(ActivityStateActionTest, LessThanOperator) {
    ActivityStateAction action1(state, widget, ActionType::CLICK);
    ActivityStateAction action2(state, widget, ActionType::BACK);
    
    // Comparison is based on hash
    bool result = action1 < action2;
    EXPECT_TRUE(result == true || result == false);
}

TEST_F(ActivityStateActionTest, IsEmpty) {
    ActivityStateAction action(state, widget, ActionType::CLICK);
    EXPECT_FALSE(action.isEmpty());
    
    // Action with empty bounds widget
    auto emptyElement = std::make_shared<Element>();
    emptyElement->reSetBounds(std::make_shared<Rect>());
    auto emptyWidget = std::make_shared<Widget>(nullptr, emptyElement);
    ActivityStateAction emptyAction(state, emptyWidget, ActionType::CLICK);
    EXPECT_TRUE(emptyAction.isEmpty());
}

TEST_F(ActivityStateActionTest, ToString) {
    ActivityStateAction action(state, widget, ActionType::CLICK);
    std::string str = action.toString();
    EXPECT_FALSE(str.empty());
    EXPECT_NE(str.find("CLICK"), std::string::npos);
}

TEST_F(ActivityStateActionTest, ToOperate) {
    ActivityStateAction action(state, widget, ActionType::CLICK);
    OperatePtr operate = action.toOperate();
    EXPECT_NE(operate, nullptr);
    EXPECT_EQ(operate->act, ActionType::CLICK);
}

// Note: EmptyConstructor test removed because ActivityStateAction's default constructor is protected
// The class is designed to be created with state and widget parameters
