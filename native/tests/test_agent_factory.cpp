/*
 * Unit tests for AgentFactory class
 */
#include <gtest/gtest.h>
#include "../agent/AgentFactory.h"
#include "../model/Model.h"
#include <memory>

using namespace fastbotx;

class AgentFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        model = Model::create();
    }
    
    void TearDown() override {
        model.reset();
    }
    
    ModelPtr model;
};

TEST_F(AgentFactoryTest, CreateReuseAgent) {
    auto agent = AgentFactory::create(AlgorithmType::Reuse, model);
    EXPECT_NE(agent, nullptr);
    EXPECT_EQ(agent->getAlgorithmType(), AlgorithmType::Reuse);
}

TEST_F(AgentFactoryTest, CreateRandomAgent) {
    auto agent = AgentFactory::create(AlgorithmType::Random, model);
    EXPECT_NE(agent, nullptr);
    // Currently all algorithms return ReuseAgent
    EXPECT_EQ(agent->getAlgorithmType(), AlgorithmType::Reuse);
}

TEST_F(AgentFactoryTest, CreateServerAgent) {
    auto agent = AgentFactory::create(AlgorithmType::Server, model);
    EXPECT_NE(agent, nullptr);
    // Currently all algorithms return ReuseAgent
    EXPECT_EQ(agent->getAlgorithmType(), AlgorithmType::Reuse);
}

TEST_F(AgentFactoryTest, CreateWithDeviceType) {
    auto agent = AgentFactory::create(AlgorithmType::Reuse, model, DeviceType::Normal);
    EXPECT_NE(agent, nullptr);
}

TEST_F(AgentFactoryTest, MultipleAgents) {
    auto agent1 = AgentFactory::create(AlgorithmType::Reuse, model);
    auto agent2 = AgentFactory::create(AlgorithmType::Reuse, model);
    
    // Should create different instances
    EXPECT_NE(agent1, agent2);
}
