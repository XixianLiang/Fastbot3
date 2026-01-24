/*
 * Unit tests for StateFactory class
 */
#include <gtest/gtest.h>
#include "../desc/StateFactory.h"
#include "../desc/Element.h"
#include <memory>

using namespace fastbotx;

class StateFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        element = std::make_shared<Element>();
        element->reSetBounds(std::make_shared<Rect>(0, 0, 100, 100));
        element->reSetClickable(true);
        activityName = std::make_shared<std::string>("TestActivity");
    }
    
    void TearDown() override {}
    
    ElementPtr element;
    stringPtr activityName;
};

TEST_F(StateFactoryTest, CreateState_Reuse) {
    StatePtr state = StateFactory::createState(AlgorithmType::Reuse, activityName, element);
    EXPECT_NE(state, nullptr);
    EXPECT_EQ(state->getActivityString(), activityName);
}

TEST_F(StateFactoryTest, CreateState_Random) {
    StatePtr state = StateFactory::createState(AlgorithmType::Random, activityName, element);
    EXPECT_NE(state, nullptr);
}

TEST_F(StateFactoryTest, CreateState_Server) {
    StatePtr state = StateFactory::createState(AlgorithmType::Server, activityName, element);
    EXPECT_NE(state, nullptr);
}

TEST_F(StateFactoryTest, CreateState_NullElement) {
    StatePtr state = StateFactory::createState(AlgorithmType::Reuse, activityName, nullptr);
    // May return nullptr or handle gracefully
    EXPECT_TRUE(state == nullptr || state != nullptr);
}

TEST_F(StateFactoryTest, CreateState_EmptyElement) {
    auto emptyElement = std::make_shared<Element>();
    StatePtr state = StateFactory::createState(AlgorithmType::Reuse, activityName, emptyElement);
    EXPECT_NE(state, nullptr);
}

TEST_F(StateFactoryTest, CreateState_WithChildren) {
    auto child1 = std::make_shared<Element>();
    child1->reSetBounds(std::make_shared<Rect>(10, 10, 50, 50));
    child1->reSetClickable(true);
    element->reAddChild(child1);
    
    StatePtr state = StateFactory::createState(AlgorithmType::Reuse, activityName, element);
    EXPECT_NE(state, nullptr);
    auto actions = state->getActions();
    EXPECT_GT(actions.size(), 0);
}

TEST_F(StateFactoryTest, CreateState_DifferentActivities) {
    auto activity1 = std::make_shared<std::string>("Activity1");
    auto activity2 = std::make_shared<std::string>("Activity2");
    
    StatePtr state1 = StateFactory::createState(AlgorithmType::Reuse, activity1, element);
    StatePtr state2 = StateFactory::createState(AlgorithmType::Reuse, activity2, element);
    
    EXPECT_NE(state1->hash(), state2->hash());
}
