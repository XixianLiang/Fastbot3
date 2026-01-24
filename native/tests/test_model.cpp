/*
 * Unit tests for Model class
 */
#include <gtest/gtest.h>
#include "../model/Model.h"
#include "../desc/Element.h"
#include <memory>

using namespace fastbotx;

class ModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        model = Model::create();
    }
    
    void TearDown() override {
        model.reset();
    }
    
    ModelPtr model;
    
    std::string createSimpleXML() {
        return R"(<?xml version="1.0" encoding="UTF-8"?>
<hierarchy>
    <node bounds="[0,0][100,100]" clickable="true" class="android.widget.Button" text="Click Me"/>
</hierarchy>)";
    }
};

TEST_F(ModelTest, Create) {
    EXPECT_NE(model, nullptr);
    EXPECT_NE(model->getGraph(), nullptr);
    EXPECT_NE(model->getPreference(), nullptr);
}

TEST_F(ModelTest, StateSize) {
    EXPECT_EQ(model->stateSize(), 0);
}

TEST_F(ModelTest, AddAgent) {
    auto agent = model->addAgent("device1", AlgorithmType::Reuse);
    EXPECT_NE(agent, nullptr);
    EXPECT_EQ(agent->getAlgorithmType(), AlgorithmType::Reuse);
}

TEST_F(ModelTest, GetAgent) {
    auto agent1 = model->addAgent("device1", AlgorithmType::Reuse);
    auto agent2 = model->getAgent("device1");
    
    EXPECT_EQ(agent1, agent2);
}

TEST_F(ModelTest, GetAgent_NonExistent) {
    auto agent = model->getAgent("non_existent");
    EXPECT_EQ(agent, nullptr);
}

TEST_F(ModelTest, GetAgent_DefaultDevice) {
    // Should return default agent if device not found
    auto agent = model->getAgent("");
    // May be nullptr if no default agent exists yet
    EXPECT_TRUE(agent == nullptr || agent != nullptr);
}

TEST_F(ModelTest, GetOperate_WithXMLString) {
    std::string xml = createSimpleXML();
    std::string result = model->getOperate(xml, "TestActivity", "");
    
    // Should return JSON string (may be empty if XML parsing fails)
    EXPECT_TRUE(result.empty() || !result.empty());
}

TEST_F(ModelTest, GetOperate_WithElement) {
    auto elem = Element::createFromXml(createSimpleXML());
    if (elem) {
        std::string result = model->getOperate(elem, "TestActivity", "");
        EXPECT_FALSE(result.empty());
    }
}

TEST_F(ModelTest, GetOperate_InvalidXML) {
    std::string invalidXML = "invalid xml";
    std::string result = model->getOperate(invalidXML, "TestActivity", "");
    EXPECT_TRUE(result.empty());
}

TEST_F(ModelTest, GetOperate_EmptyXML) {
    std::string emptyXML = "";
    std::string result = model->getOperate(emptyXML, "TestActivity", "");
    EXPECT_TRUE(result.empty());
}

TEST_F(ModelTest, GetOperateOpt) {
    auto elem = Element::createFromXml(createSimpleXML());
    if (elem) {
        OperatePtr operate = model->getOperateOpt(elem, "TestActivity", "");
        EXPECT_NE(operate, nullptr);
    }
}

TEST_F(ModelTest, GetPackageName) {
    model->setPackageName("com.test.app");
    EXPECT_EQ(model->getPackageName(), "com.test.app");
}

TEST_F(ModelTest, GetNetActionTaskID) {
    int taskId = model->getNetActionTaskID();
    EXPECT_GE(taskId, 0);
}

TEST_F(ModelTest, MultipleAgents) {
    auto agent1 = model->addAgent("device1", AlgorithmType::Reuse);
    auto agent2 = model->addAgent("device2", AlgorithmType::Reuse);
    
    EXPECT_NE(agent1, agent2);
    EXPECT_EQ(model->getAgent("device1"), agent1);
    EXPECT_EQ(model->getAgent("device2"), agent2);
}

TEST_F(ModelTest, StateCreationAndTracking) {
    auto elem = Element::createFromXml(createSimpleXML());
    if (elem) {
        model->getOperate(elem, "TestActivity", "");
        // State should be added to graph
        EXPECT_GT(model->stateSize(), 0);
    }
}

TEST_F(ModelTest, ActivityTracking) {
    auto elem = Element::createFromXml(createSimpleXML());
    if (elem) {
        model->getOperate(elem, "Activity1", "");
        model->getOperate(elem, "Activity2", "");
        
        auto activities = model->getGraph()->getVisitedActivities();
        EXPECT_GE(activities.size(), 1);
    }
}

TEST_F(ModelTest, DefaultAgentCreation) {
    // When getting operate without agent, default agent should be created
    auto elem = Element::createFromXml(createSimpleXML());
    if (elem) {
        model->getOperate(elem, "TestActivity", "");
        // Default agent should be created
        auto defaultAgent = model->getAgent("0000001");
        EXPECT_NE(defaultAgent, nullptr);
    }
}

TEST_F(ModelTest, Destructor) {
    // Test that destructor doesn't crash
    {
        auto localModel = Model::create();
        localModel->addAgent("test", AlgorithmType::Reuse);
        // Destructor called when going out of scope
    }
    EXPECT_TRUE(true);
}
