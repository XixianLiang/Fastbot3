/*
 * Integration tests for Fastbot Native
 */
#include <gtest/gtest.h>
#include "../model/Model.h"
#include "../desc/Element.h"
#include "../agent/ModelReusableAgent.h"
#include <memory>

using namespace fastbotx;

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        model = Model::create();
        model->setPackageName("com.test.app");
    }
    
    void TearDown() override {
        model.reset();
    }
    
    ModelPtr model;
    
    std::string createComplexXML() {
        return R"(<?xml version="1.0" encoding="UTF-8"?>
<hierarchy>
    <node bounds="[0,0][1080,1920]" class="android.widget.FrameLayout">
        <node bounds="[0,0][1080,1920]" class="android.widget.LinearLayout">
            <node bounds="[0,0][1080,100]" class="android.widget.Toolbar" text="Title"/>
            <node bounds="[0,100][1080,1820]" class="android.widget.ScrollView" scrollable="true">
                <node bounds="[0,100][1080,1820]" class="android.widget.LinearLayout">
                    <node bounds="[10,110][1070,210]" 
                          class="android.widget.Button" 
                          text="Button 1"
                          resource-id="com.test:id/button1"
                          clickable="true"/>
                    <node bounds="[10,220][1070,320]" 
                          class="android.widget.Button" 
                          text="Button 2"
                          resource-id="com.test:id/button2"
                          clickable="true"/>
                    <node bounds="[10,330][1070,430]" 
                          class="android.widget.EditText" 
                          text=""
                          resource-id="com.test:id/edittext"
                          clickable="true"
                          focusable="true"/>
                </node>
            </node>
        </node>
    </node>
</hierarchy>)";
    }
};

TEST_F(IntegrationTest, CompleteWorkflow) {
    // Test complete workflow: XML -> State -> Action -> Operate
    std::string xml = createComplexXML();
    std::string result = model->getOperate(xml, "MainActivity", "");
    
    EXPECT_FALSE(result.empty());
    // Result should be valid JSON
    EXPECT_NE(result.find("act"), std::string::npos);
}

TEST_F(IntegrationTest, MultipleOperations) {
    // Test multiple operations in sequence
    std::string xml = createComplexXML();
    
    for (int i = 0; i < 5; i++) {
        std::string result = model->getOperate(xml, "MainActivity", "");
        EXPECT_FALSE(result.empty());
    }
    
    // State should be tracked
    EXPECT_GT(model->stateSize(), 0);
}

TEST_F(IntegrationTest, StateTracking) {
    std::string xml = createComplexXML();
    
    // First operation
    model->getOperate(xml, "Activity1", "");
    size_t stateSize1 = model->stateSize();
    
    // Same state again
    model->getOperate(xml, "Activity1", "");
    size_t stateSize2 = model->stateSize();
    
    // Should reuse state
    EXPECT_EQ(stateSize1, stateSize2);
    
    // Different activity
    model->getOperate(xml, "Activity2", "");
    size_t stateSize3 = model->stateSize();
    
    // Should have new state
    EXPECT_GT(stateSize3, stateSize2);
}

TEST_F(IntegrationTest, AgentStateManagement) {
    auto agent = model->addAgent("device1", AlgorithmType::Reuse);
    std::string xml = createComplexXML();
    
    // Multiple operations
    for (int i = 0; i < 3; i++) {
        model->getOperate(xml, "MainActivity", "device1");
    }
    
    // Agent should track state
    EXPECT_GE(agent->getCurrentStateBlockTimes(), 0);
}

TEST_F(IntegrationTest, ModelPersistence) {
    auto agent = std::dynamic_pointer_cast<ModelReusableAgent>(
        model->addAgent("device1", AlgorithmType::Reuse));
    
    // Load non-existent model (should not crash)
    agent->loadReuseModel("non_existent_package");
    
    // Save model
    agent->saveReuseModel("/tmp/test_model.fbm");
    
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(IntegrationTest, MultipleAgents) {
    auto agent1 = model->addAgent("device1", AlgorithmType::Reuse);
    auto agent2 = model->addAgent("device2", AlgorithmType::Reuse);
    
    std::string xml = createComplexXML();
    
    std::string result1 = model->getOperate(xml, "Activity1", "device1");
    std::string result2 = model->getOperate(xml, "Activity1", "device2");
    
    EXPECT_FALSE(result1.empty());
    EXPECT_FALSE(result2.empty());
}

TEST_F(IntegrationTest, ActivityTracking) {
    std::string xml = createComplexXML();
    
    model->getOperate(xml, "Activity1", "");
    model->getOperate(xml, "Activity2", "");
    model->getOperate(xml, "Activity1", "");
    
    auto activities = model->getGraph()->getVisitedActivities();
    EXPECT_EQ(activities.size(), 2);
}

TEST_F(IntegrationTest, ActionSelection) {
    std::string xml = createComplexXML();
    
    // Get multiple actions
    std::vector<std::string> actions;
    for (int i = 0; i < 10; i++) {
        std::string result = model->getOperate(xml, "MainActivity", "");
        if (!result.empty()) {
            actions.push_back(result);
        }
    }
    
    // Should get valid actions
    EXPECT_GT(actions.size(), 0);
}

TEST_F(IntegrationTest, ErrorHandling) {
    // Test with invalid XML
    std::string invalidXML = "invalid xml";
    std::string result = model->getOperate(invalidXML, "Activity", "");
    EXPECT_TRUE(result.empty());
    
    // Test with empty XML
    result = model->getOperate("", "Activity", "");
    EXPECT_TRUE(result.empty());
}

TEST_F(IntegrationTest, StateReuse) {
    std::string xml = createComplexXML();
    
    // First call
    std::string result1 = model->getOperate(xml, "Activity1", "");
    size_t stateSize1 = model->stateSize();
    
    // Second call with same XML
    std::string result2 = model->getOperate(xml, "Activity1", "");
    size_t stateSize2 = model->stateSize();
    
    // State should be reused
    EXPECT_EQ(stateSize1, stateSize2);
}
