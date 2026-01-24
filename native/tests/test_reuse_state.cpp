/*
 * Unit tests for ReuseState class
 */
#include <gtest/gtest.h>
#include "../desc/reuse/ReuseState.h"
#include "../desc/Element.h"
#include <memory>

using namespace fastbotx;

class ReuseStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        activityName = std::make_shared<std::string>("TestActivity");
        element = std::make_shared<Element>();
        element->reSetBounds(std::make_shared<Rect>(0, 0, 100, 100));
        element->reSetClickable(true);
        element->reSetText("Test Button");
        element->reSetResourceID("com.test:id/button");
    }
    
    void TearDown() override {}
    
    stringPtr activityName;
    ElementPtr element;
};

TEST_F(ReuseStateTest, Create) {
    ReuseStatePtr state = ReuseState::create(element, activityName);
    EXPECT_NE(state, nullptr);
    EXPECT_EQ(state->getActivityString(), activityName);
}

TEST_F(ReuseStateTest, HashFunction) {
    ReuseStatePtr state1 = ReuseState::create(element, activityName);
    ReuseStatePtr state2 = ReuseState::create(element, activityName);
    
    // Same elements should produce same hash
    EXPECT_EQ(state1->hash(), state2->hash());
}

TEST_F(ReuseStateTest, GetActions) {
    ReuseStatePtr state = ReuseState::create(element, activityName);
    auto actions = state->getActions();
    EXPECT_FALSE(actions.empty());
}

TEST_F(ReuseStateTest, ClearDetails) {
    ReuseStatePtr state = ReuseState::create(element, activityName);
    state->clearDetails();
    EXPECT_TRUE(state->hasNoDetail());
}

TEST_F(ReuseStateTest, FillDetails) {
    ReuseStatePtr state1 = ReuseState::create(element, activityName);
    ReuseStatePtr state2 = ReuseState::create(element, activityName);
    
    state1->clearDetails();
    state1->fillDetails(state2);
    EXPECT_FALSE(state1->hasNoDetail());
}
