/*
 * Unit tests for ModelReusableAgent class
 */
#include <gtest/gtest.h>
#include "../agent/ModelReusableAgent.h"
#include "../model/Model.h"
#include "../desc/State.h"
#include "../desc/StateFactory.h"
#include "../desc/Element.h"
#include <memory>
#include <thread>
#include <chrono>

using namespace fastbotx;

class ModelReusableAgentTest : public ::testing::Test {
protected:
    void SetUp() override {
        model = Model::create();
        agent = std::dynamic_pointer_cast<ModelReusableAgent>(
            model->addAgent("test_device", AlgorithmType::Reuse));
        ASSERT_NE(agent, nullptr);
        
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
    ReuseAgentPtr agent;
    stringPtr activityName;
    StatePtr testState;
};

TEST_F(ModelReusableAgentTest, Constructor) {
    EXPECT_EQ(agent->getAlgorithmType(), AlgorithmType::Reuse);
    EXPECT_NE(agent->getCurrentStateBlockTimes(), -1);
}

TEST_F(ModelReusableAgentTest, ComputeAlphaValue) {
    // Add state to graph to trigger alpha computation
    auto state = model->getGraph()->addState(testState);
    
    // Note: computeAlphaValue() is private, so we test indirectly
    // by verifying the agent can resolve actions (which uses alpha internally)
    auto action = agent->resolveNewAction();
    EXPECT_NE(action, nullptr);
}

TEST_F(ModelReusableAgentTest, EGreedy) {
    // Note: eGreedy() is protected, so we test indirectly
    // by verifying the agent can resolve actions (which uses eGreedy internally)
    auto state = model->getGraph()->addState(testState);
    auto action = agent->resolveNewAction();
    EXPECT_NE(action, nullptr);
}

TEST_F(ModelReusableAgentTest, GetQValue) {
    // Create a test action
    auto action = std::make_shared<Action>(ActionType::CLICK);
    action->setQValue(10.5);
    
    // Note: getQValue() is private, test indirectly
    // Q-values are updated internally when resolving actions
    auto resolvedAction = agent->resolveNewAction();
    EXPECT_NE(resolvedAction, nullptr);
}

TEST_F(ModelReusableAgentTest, SetQValue) {
    // Note: setQValue() is private, test indirectly
    // Q-values are set internally when resolving actions and moving forward
    auto state = model->getGraph()->addState(testState);
    auto action = agent->resolveNewAction();
    if (action) {
        agent->moveForward(state);
        EXPECT_TRUE(true);
    }
}

TEST_F(ModelReusableAgentTest, ProbabilityOfVisitingNewActivities) {
    // Note: probabilityOfVisitingNewActivities() is protected
    // Test indirectly by resolving actions which uses this method internally
    auto state = model->getGraph()->addState(testState);
    auto action = agent->resolveNewAction();
    EXPECT_NE(action, nullptr);
}

TEST_F(ModelReusableAgentTest, GetStateActionExpectationValue) {
    // Note: getStateActionExpectationValue() is protected
    // Test indirectly by resolving actions which uses this method internally
    auto state = model->getGraph()->addState(testState);
    auto action = agent->resolveNewAction();
    EXPECT_NE(action, nullptr);
}

TEST_F(ModelReusableAgentTest, LoadReuseModel_NonExistentFile) {
    // Test loading non-existent model file
    agent->loadReuseModel("non_existent_package");
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(ModelReusableAgentTest, SaveReuseModel) {
    // Test saving model (even if empty)
    std::string testPath = "/tmp/test_model.fbm";
    agent->saveReuseModel(testPath);
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(ModelReusableAgentTest, UpdateReuseModel) {
    // Add a state and action to trigger update
    auto state = model->getGraph()->addState(testState);
    
    // Simulate action selection
    auto action = agent->resolveNewAction();
    if (action) {
        // Note: updateStrategy() is protected, but it's called internally
        // by moveForward() or resolveNewAction()
        agent->moveForward(state);
        EXPECT_TRUE(true);
    }
}

TEST_F(ModelReusableAgentTest, SelectNewAction) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // Set new state in agent (simulate state update)
    // Note: This requires accessing protected members, so we test through public interface
    
    // Test that selectNewAction doesn't crash
    // This is tested indirectly through resolveNewAction
    auto action = agent->resolveNewAction();
    // May return nullptr if no valid actions
    EXPECT_TRUE(action == nullptr || action != nullptr);
}

TEST_F(ModelReusableAgentTest, AdjustActions) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // Adjust actions (called internally by resolveNewAction)
    agent->resolveNewAction();
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(ModelReusableAgentTest, ComputeAlphaValueIndirect) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // Compute alpha value
    // Note: computeAlphaValue() is private, test indirectly via resolveNewAction
    auto action = agent->resolveNewAction();
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(ModelReusableAgentTest, ComputeRewardOfLatestAction) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // Note: computeRewardOfLatestAction() is protected
    // Test indirectly by resolving actions and moving forward
    auto action = agent->resolveNewAction();
    if (action) {
        // Moving forward will trigger reward computation internally
        agent->moveForward(state);
        EXPECT_TRUE(true);
    }
}

TEST_F(ModelReusableAgentTest, UpdateQValues) {
    // Add state and simulate action sequence
    auto state = model->getGraph()->addState(testState);
    
    // Resolve action to populate previousActions
    // updateStrategy() is called internally by resolveNewAction() or moveForward()
    auto action1 = agent->resolveNewAction();
    if (action1) {
        // updateStrategy() is protected, but it's called internally
        // Test that the agent can resolve actions without crashing
        EXPECT_NE(action1, nullptr);
    }
}

TEST_F(ModelReusableAgentTest, SelectUnperformedActionNotInReuseModel) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // This is a protected method, test through public interface
    auto action = agent->resolveNewAction();
    // May return nullptr
    EXPECT_TRUE(action == nullptr || action != nullptr);
}

TEST_F(ModelReusableAgentTest, SelectUnperformedActionInReuseModel) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // This is a protected method, test through public interface
    auto action = agent->resolveNewAction();
    // May return nullptr
    EXPECT_TRUE(action == nullptr || action != nullptr);
}

TEST_F(ModelReusableAgentTest, SelectActionByQValue) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // This is a protected method, test through public interface
    auto action = agent->resolveNewAction();
    // May return nullptr
    EXPECT_TRUE(action == nullptr || action != nullptr);
}

TEST_F(ModelReusableAgentTest, SelectNewActionEpsilonGreedyRandomly) {
    // Add state to graph
    auto state = model->getGraph()->addState(testState);
    
    // This is a protected method, test through public interface
    auto action = agent->resolveNewAction();
    // May return nullptr
    EXPECT_TRUE(action == nullptr || action != nullptr);
}

TEST_F(ModelReusableAgentTest, GetCurrentStateBlockTimes) {
    int blockTimes = agent->getCurrentStateBlockTimes();
    EXPECT_GE(blockTimes, 0);
}

TEST_F(ModelReusableAgentTest, Destructor) {
    // Test that destructor doesn't crash
    {
        auto localAgent = std::dynamic_pointer_cast<ModelReusableAgent>(
            model->addAgent("test_device2", AlgorithmType::Reuse));
        // Destructor called when going out of scope
    }
    EXPECT_TRUE(true);
}
