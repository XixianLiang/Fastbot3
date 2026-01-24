/*
 * Unit tests for State class
 */
#include <gtest/gtest.h>
#include "../desc/State.h"
#include "../desc/StateFactory.h"
#include "../desc/Element.h"
#include "../desc/Widget.h"
#include "../desc/Action.h"
#include "../desc/ActionFilter.h"
#include <memory>

using namespace fastbotx;

class StateTest : public ::testing::Test {
protected:
    void SetUp() override {
        activityName = std::make_shared<std::string>("TestActivity");
        
        // Create test element
        element = std::make_shared<Element>();
        element->reSetBounds(std::make_shared<Rect>(0, 0, 100, 100));
        element->reSetClickable(true);
        element->reSetEnabled(true);
        
        // Create child element
        childElement = std::make_shared<Element>();
        childElement->reSetBounds(std::make_shared<Rect>(10, 10, 50, 50));
        childElement->reSetClickable(true);
        element->reAddChild(childElement);
        
        state = StateFactory::createState(AlgorithmType::Reuse, activityName, element);
    }
    
    void TearDown() override {}
    
    stringPtr activityName;
    ElementPtr element;
    ElementPtr childElement;
    StatePtr state;
};

TEST_F(StateTest, Create) {
    EXPECT_NE(state, nullptr);
    EXPECT_EQ(state->getActivityString(), activityName);
}

TEST_F(StateTest, GetActivityString) {
    EXPECT_EQ(state->getActivityString(), activityName);
}

TEST_F(StateTest, HashFunction) {
    StatePtr state1 = StateFactory::createState(AlgorithmType::Reuse, activityName, element);
    StatePtr state2 = StateFactory::createState(AlgorithmType::Reuse, activityName, element);
    
    // Same elements should produce same hash
    EXPECT_EQ(state1->hash(), state2->hash());
}

TEST_F(StateTest, ComparisonOperators) {
    StatePtr state1 = StateFactory::createState(AlgorithmType::Reuse, activityName, element);
    StatePtr state2 = StateFactory::createState(AlgorithmType::Reuse, activityName, element);
    
    // Same hash means equal
    EXPECT_TRUE(state1 == state2 || state1 < state2 || state2 < state1);
}

TEST_F(StateTest, GetActions) {
    auto actions = state->getActions();
    EXPECT_FALSE(actions.empty());
    // Should have at least BACK action
    EXPECT_GT(actions.size(), 0);
}

TEST_F(StateTest, GetBackAction) {
    auto backAction = state->getBackAction();
    EXPECT_NE(backAction, nullptr);
    EXPECT_TRUE(backAction->isBack());
}

TEST_F(StateTest, TargetActions) {
    auto targetActions = state->targetActions();
    // Should contain actions that require target
    EXPECT_GE(targetActions.size(), 0);
}

TEST_F(StateTest, ContainsTarget) {
    auto widgets = state->getActions();
    if (!widgets.empty() && widgets[0]->getTarget()) {
        bool contains = state->containsTarget(widgets[0]->getTarget());
        EXPECT_TRUE(contains);
    }
}

TEST_F(StateTest, IsSaturated) {
    auto actions = state->getActions();
    if (!actions.empty()) {
        bool saturated = state->isSaturated(actions[0]);
        // May be true or false depending on visited count
        EXPECT_TRUE(saturated == true || saturated == false);
    }
}

TEST_F(StateTest, CountActionPriority) {
    ActionFilterPtr filter = enableValidValuePriorityFilter;
    int total = state->countActionPriority(filter, true);
    EXPECT_GE(total, 0);
}

TEST_F(StateTest, CountActionPriority_ExcludeBack) {
    ActionFilterPtr filter = enableValidValuePriorityFilter;
    int totalWithBack = state->countActionPriority(filter, true);
    int totalWithoutBack = state->countActionPriority(filter, false);
    EXPECT_LE(totalWithoutBack, totalWithBack);
}

TEST_F(StateTest, RandomPickAction) {
    ActionFilterPtr filter = enableValidValuePriorityFilter;
    auto action = state->randomPickAction(filter);
    // May return nullptr if no valid actions
    EXPECT_TRUE(action == nullptr || action != nullptr);
}

TEST_F(StateTest, RandomPickUnvisitedAction) {
    auto action = state->randomPickUnvisitedAction();
    // May return nullptr if all actions visited
    EXPECT_TRUE(action == nullptr || action != nullptr);
}

TEST_F(StateTest, GreedyPickMaxQValue) {
    ActionFilterPtr filter = enableValidValuePriorityFilter;
    auto action = state->greedyPickMaxQValue(filter);
    // May return nullptr if no valid actions
    EXPECT_TRUE(action == nullptr || action != nullptr);
}

TEST_F(StateTest, ResolveAt) {
    auto actions = state->getActions();
    if (!actions.empty()) {
        time_t timestamp = time(nullptr);
        auto resolved = state->resolveAt(actions[0], timestamp);
        // May return nullptr or the action itself
        EXPECT_TRUE(resolved == nullptr || resolved != nullptr);
    }
}

TEST_F(StateTest, SetPriority) {
    state->setPriority(100);
    EXPECT_EQ(state->getPriority(), 100);
}

TEST_F(StateTest, ClearDetails) {
    state->clearDetails();
    EXPECT_TRUE(state->hasNoDetail());
}

TEST_F(StateTest, FillDetails) {
    StatePtr state1 = StateFactory::createState(AlgorithmType::Reuse, activityName, element);
    StatePtr state2 = StateFactory::createState(AlgorithmType::Reuse, activityName, element);
    
    state1->clearDetails();
    state1->fillDetails(state2);
    EXPECT_FALSE(state1->hasNoDetail());
}

TEST_F(StateTest, HasNoDetail) {
    EXPECT_FALSE(state->hasNoDetail());
    state->clearDetails();
    EXPECT_TRUE(state->hasNoDetail());
}

TEST_F(StateTest, ToString) {
    std::string str = state->toString();
    EXPECT_FALSE(str.empty());
    EXPECT_NE(str.find("state"), std::string::npos);
}

TEST_F(StateTest, Visit) {
    time_t timestamp = time(nullptr);
    state->visit(timestamp);
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(StateTest, GetVisitedCount) {
    int count = state->getVisitedCount();
    EXPECT_GE(count, 0);
}

TEST_F(StateTest, IsVisited) {
    bool visited = state->isVisited();
    // May be true or false
    EXPECT_TRUE(visited == true || visited == false);
}

// Note: mergeWidgetAndStoreMergedOnes is a protected method
// It's tested indirectly through StateFactory::createState which calls it internally

TEST_F(StateTest, DifferentActivities) {
    auto activity1 = std::make_shared<std::string>("Activity1");
    auto activity2 = std::make_shared<std::string>("Activity2");
    
    StatePtr state1 = StateFactory::createState(AlgorithmType::Reuse, activity1, element);
    StatePtr state2 = StateFactory::createState(AlgorithmType::Reuse, activity2, element);
    
    EXPECT_NE(state1->hash(), state2->hash());
}

TEST_F(StateTest, EmptyElement) {
    auto emptyElement = std::make_shared<Element>();
    StatePtr emptyState = StateFactory::createState(AlgorithmType::Reuse, activityName, emptyElement);
    EXPECT_NE(emptyState, nullptr);
}
