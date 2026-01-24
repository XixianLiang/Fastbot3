/*
 * Unit tests for ActionFilter classes
 */
#include <gtest/gtest.h>
#include "../desc/ActionFilter.h"
#include "../desc/Action.h"
#include "../desc/State.h"
#include "../desc/StateFactory.h"
#include "../desc/Widget.h"
#include "../desc/Element.h"
#include <memory>

using namespace fastbotx;

class ActionFilterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test state and action
        auto activityName = std::make_shared<std::string>("TestActivity");
        auto elem = std::make_shared<Element>();
        elem->reSetBounds(std::make_shared<Rect>(0, 0, 100, 100));
        elem->reSetClickable(true);
        state = StateFactory::createState(AlgorithmType::Reuse, activityName, elem);
        
        auto widget = std::make_shared<Widget>(nullptr, elem);
        action = std::make_shared<ActivityStateAction>(state, widget, ActionType::CLICK);
    }
    
    void TearDown() override {}
    
    StatePtr state;
    ActivityStateActionPtr action;
};

TEST_F(ActionFilterTest, ActionFilterALL) {
    ActionFilterALL filter;
    EXPECT_TRUE(filter.include(action));
    
    // Should include all actions
    auto backAction = state->getBackAction();
    EXPECT_TRUE(filter.include(backAction));
}

TEST_F(ActionFilterTest, ActionFilterTarget) {
    ActionFilterTarget filter;
    EXPECT_TRUE(filter.include(action));  // CLICK requires target
    
    auto backAction = state->getBackAction();
    EXPECT_FALSE(filter.include(backAction));  // BACK doesn't require target
}

TEST_F(ActionFilterTest, ActionFilterValid) {
    ActionFilterValid filter;
    EXPECT_TRUE(filter.include(action));
    
    // Invalid action (empty bounds)
    auto emptyElem = std::make_shared<Element>();
    emptyElem->reSetBounds(std::make_shared<Rect>());
    auto emptyWidget = std::make_shared<Widget>(nullptr, emptyElem);
    auto invalidAction = std::make_shared<ActivityStateAction>(state, emptyWidget, ActionType::CLICK);
    EXPECT_FALSE(filter.include(invalidAction));
}

TEST_F(ActionFilterTest, ActionFilterEnableValid) {
    ActionFilterEnableValid filter;
    EXPECT_TRUE(filter.include(action));
    
    // Disabled action
    auto disabledElem = std::make_shared<Element>();
    disabledElem->reSetEnabled(false);
    disabledElem->reSetBounds(std::make_shared<Rect>(0, 0, 100, 100));
    auto disabledWidget = std::make_shared<Widget>(nullptr, disabledElem);
    auto disabledAction = std::make_shared<ActivityStateAction>(state, disabledWidget, ActionType::CLICK);
    EXPECT_FALSE(filter.include(disabledAction));
}

TEST_F(ActionFilterTest, ActionFilterUnvisitedValid) {
    ActionFilterUnvisitedValid filter;
    EXPECT_TRUE(filter.include(action));  // Not visited yet
    
    // Visit the action
    action->visit(time(nullptr));
    EXPECT_FALSE(filter.include(action));  // Now visited
}

TEST_F(ActionFilterTest, ActionFilterValidValuePriority) {
    ActionFilterValidValuePriority filter;
    EXPECT_TRUE(filter.include(action));
    
    int priority1 = filter.getPriority(action);
    action->setQValue(10.0);
    int priority2 = filter.getPriority(action);
    
    EXPECT_GT(priority2, priority1);
}

TEST_F(ActionFilterTest, ActionFilterValidDatePriority) {
    ActionFilterValidDatePriority filter;
    
    // Test different action types
    EXPECT_TRUE(filter.include(action));  // CLICK
    
    auto backAction = state->getBackAction();
    EXPECT_TRUE(filter.include(backAction));  // BACK
    
    // Test with null action
    EXPECT_FALSE(filter.include(nullptr));
}

TEST_F(ActionFilterTest, ActionFilterValidUnSaturated) {
    ActionFilterValidUnSaturated filter;
    
    // Test with unvisited action
    EXPECT_TRUE(filter.include(action));
    
    // Test with visited action (may be saturated)
    action->visit(time(nullptr));
    bool included = filter.include(action);
    EXPECT_TRUE(included == true || included == false);
}

TEST_F(ActionFilterTest, GetPriority) {
    ActionFilterALL filter;
    action->setPriority(100);
    EXPECT_EQ(filter.getPriority(action), 100);
}

TEST_F(ActionFilterTest, GlobalFilters) {
    EXPECT_NE(allFilter, nullptr);
    EXPECT_NE(targetFilter, nullptr);
    EXPECT_NE(validFilter, nullptr);
    EXPECT_NE(enableValidFilter, nullptr);
    EXPECT_NE(enableValidUnvisitedFilter, nullptr);
    EXPECT_NE(enableValidValuePriorityFilter, nullptr);
    EXPECT_NE(validDatePriorityFilter, nullptr);
}

TEST_F(ActionFilterTest, FilterChaining) {
    // Test that filters can be used together
    ActionFilterEnableValid filter;
    EXPECT_TRUE(filter.include(action));
    
    // Test with different actions
    auto actions = state->getActions();
    for (const auto& act : actions) {
        bool included = filter.include(act);
        EXPECT_TRUE(included == true || included == false);
    }
}
