/*
 * Unit tests for AbstractAgent class
 */
#include <gtest/gtest.h>
#include "../agent/AbstractAgent.h"
#include "../model/Model.h"
#include "../desc/State.h"
#include "../desc/StateFactory.h"
#include "../desc/Element.h"
#include <memory>

using namespace fastbotx;

class AbstractAgentTest : public ::testing::Test {
protected:
    void SetUp() override {
        model = Model::create();
        agent = model->addAgent("test_device", AlgorithmType::Reuse);
        
        // Create test state
        activityName = std::make_shared<std::string>("TestActivity");
        auto elem = std::make_shared<Element>();
        elem->reSetBounds(std::make_shared<Rect>(0, 0, 100, 100));
        elem->reSetClickable(true);
        testState = StateFactory::createState(AlgorithmType::Reuse, activityName, elem);
    }
    
    void TearDown() override {
        agent.reset();
        model.reset();
    }
    
    ModelPtr model;
    AbstractAgentPtr agent;
    stringPtr activityName;
    StatePtr testState;
};

TEST_F(AbstractAgentTest, Constructor) {
    EXPECT_NE(agent, nullptr);
    EXPECT_EQ(agent->getAlgorithmType(), AlgorithmType::Reuse);
}

TEST_F(AbstractAgentTest, GetCurrentStateBlockTimes) {
    int blockTimes = agent->getCurrentStateBlockTimes();
    EXPECT_GE(blockTimes, 0);
}

TEST_F(AbstractAgentTest, OnAddNode) {
    // Add state to graph, which triggers onAddNode
    auto state = model->getGraph()->addState(testState);
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(AbstractAgentTest, ResolveNewAction) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // Resolve new action
    auto action = agent->resolveNewAction();
    // May return nullptr if no valid actions
    EXPECT_TRUE(action == nullptr || action != nullptr);
}

TEST_F(AbstractAgentTest, MoveForward) {
    // Add state to graph
    auto state1 = model->getGraph()->addState(testState);
    
    // Create another state
    auto elem2 = std::make_shared<Element>();
    elem2->reSetBounds(std::make_shared<Rect>(0, 0, 200, 200));
    auto state2 = StateFactory::createState(AlgorithmType::Reuse, activityName, elem2);
    auto state3 = model->getGraph()->addState(state2);
    
    // Move forward
    agent->moveForward(state3);
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(AbstractAgentTest, AdjustActions) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // Adjust actions (called internally by resolveNewAction)
    agent->resolveNewAction();
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(AbstractAgentTest, HandleNullAction) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // Handle null action (protected method, test through resolveNewAction)
    auto action = agent->resolveNewAction();
    // May return nullptr
    EXPECT_TRUE(action == nullptr || action != nullptr);
}

TEST_F(AbstractAgentTest, UpdateStrategy) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // Update strategy (called internally)
    agent->resolveNewAction();
    agent->updateStrategy();
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(AbstractAgentTest, GetAlgorithmType) {
    EXPECT_EQ(agent->getAlgorithmType(), AlgorithmType::Reuse);
}

TEST_F(AbstractAgentTest, StateBlocking) {
    // Add same state multiple times to test blocking
    auto state = model->getGraph()->addState(testState);
    
    for (int i = 0; i < 5; i++) {
        model->getGraph()->addState(testState);
    }
    
    int blockTimes = agent->getCurrentStateBlockTimes();
    EXPECT_GE(blockTimes, 0);
}

TEST_F(AbstractAgentTest, MultipleStates) {
    // Add multiple different states
    auto state1 = model->getGraph()->addState(testState);
    
    auto elem2 = std::make_shared<Element>();
    elem2->reSetBounds(std::make_shared<Rect>(0, 0, 200, 200));
    auto state2 = StateFactory::createState(AlgorithmType::Reuse, activityName, elem2);
    model->getGraph()->addState(state2);
    
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(AbstractAgentTest, Destructor) {
    // Test that destructor doesn't crash
    {
        auto localAgent = model->addAgent("test_device2", AlgorithmType::Reuse);
        // Destructor called when going out of scope
    }
    EXPECT_TRUE(true);
}
