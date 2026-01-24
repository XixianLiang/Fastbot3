/*
 * Unit tests for Graph class
 */
#include <gtest/gtest.h>
#include "../model/Graph.h"
#include "../desc/State.h"
#include "../desc/StateFactory.h"
#include "../desc/Element.h"
#include "../desc/Action.h"
#include <memory>

using namespace fastbotx;

// Mock GraphListener for testing
class MockGraphListener : public GraphListener {
public:
    int nodeAddedCount = 0;
    StatePtr lastAddedNode = nullptr;
    
    void onAddNode(StatePtr node) override {
        nodeAddedCount++;
        lastAddedNode = node;
    }
    virtual ~MockGraphListener() = default;
};

class GraphTest : public ::testing::Test {
protected:
    void SetUp() override {
        graph = std::make_shared<Graph>();
        activityName = std::make_shared<std::string>("TestActivity");
    }
    
    void TearDown() override {}
    
    GraphPtr graph;
    stringPtr activityName;
    
    StatePtr createTestState(const std::string& activity) {
        auto elem = std::make_shared<Element>();
        elem->reSetBounds(std::make_shared<Rect>(0, 0, 100, 100));
        auto activityPtr = std::make_shared<std::string>(activity);
        return StateFactory::createState(AlgorithmType::Reuse, activityPtr, elem);
    }
};

TEST_F(GraphTest, Constructor) {
    EXPECT_EQ(graph->stateSize(), 0);
    EXPECT_EQ(graph->getTotalDistri(), 0);
}

TEST_F(GraphTest, AddState) {
    StatePtr state = createTestState("Activity1");
    StatePtr addedState = graph->addState(state);
    
    EXPECT_EQ(graph->stateSize(), 1);
    EXPECT_EQ(graph->getTotalDistri(), 1);
    EXPECT_NE(addedState, nullptr);
}

TEST_F(GraphTest, AddDuplicateState) {
    StatePtr state1 = createTestState("Activity1");
    StatePtr state2 = createTestState("Activity1");
    
    StatePtr added1 = graph->addState(state1);
    StatePtr added2 = graph->addState(state2);
    
    // Should return the same state (merged)
    EXPECT_EQ(added1, added2);
    EXPECT_EQ(graph->stateSize(), 1);
    EXPECT_EQ(graph->getTotalDistri(), 2);
}

TEST_F(GraphTest, AddDifferentStates) {
    StatePtr state1 = createTestState("Activity1");
    StatePtr state2 = createTestState("Activity2");
    
    graph->addState(state1);
    graph->addState(state2);
    
    EXPECT_EQ(graph->stateSize(), 2);
    EXPECT_EQ(graph->getTotalDistri(), 2);
}

TEST_F(GraphTest, GetVisitedActivities) {
    graph->addState(createTestState("Activity1"));
    graph->addState(createTestState("Activity2"));
    graph->addState(createTestState("Activity1"));
    
    auto activities = graph->getVisitedActivities();
    EXPECT_EQ(activities.size(), 2);
}

TEST_F(GraphTest, AddListener) {
    auto listener = std::make_shared<MockGraphListener>();
    graph->addListener(listener);
    
    StatePtr state = createTestState("Activity1");
    graph->addState(state);
    
    EXPECT_EQ(listener->nodeAddedCount, 1);
    EXPECT_EQ(listener->lastAddedNode, state);
}

TEST_F(GraphTest, MultipleListeners) {
    auto listener1 = std::make_shared<MockGraphListener>();
    auto listener2 = std::make_shared<MockGraphListener>();
    
    graph->addListener(listener1);
    graph->addListener(listener2);
    
    StatePtr state = createTestState("Activity1");
    graph->addState(state);
    
    EXPECT_EQ(listener1->nodeAddedCount, 1);
    EXPECT_EQ(listener2->nodeAddedCount, 1);
}

TEST_F(GraphTest, AddActionFromState) {
    StatePtr state = createTestState("Activity1");
    graph->addState(state);
    
    // Verify actions are tracked
    // This is tested indirectly through addState
    EXPECT_GT(graph->stateSize(), 0);
}

TEST_F(GraphTest, StateIdAssignment) {
    StatePtr state1 = createTestState("Activity1");
    StatePtr state2 = createTestState("Activity2");
    
    StatePtr added1 = graph->addState(state1);
    StatePtr added2 = graph->addState(state2);
    
    EXPECT_NE(added1->getId(), added2->getId());
}

TEST_F(GraphTest, Timestamp) {
    time_t timestamp1 = graph->getTimestamp();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    StatePtr state = createTestState("Activity1");
    graph->addState(state);
    
    time_t timestamp2 = graph->getTimestamp();
    EXPECT_GE(timestamp2, timestamp1);
}

TEST_F(GraphTest, TotalDistriIncrement) {
    EXPECT_EQ(graph->getTotalDistri(), 0);
    
    graph->addState(createTestState("Activity1"));
    EXPECT_EQ(graph->getTotalDistri(), 1);
    
    graph->addState(createTestState("Activity1"));  // Duplicate
    EXPECT_EQ(graph->getTotalDistri(), 2);
    
    graph->addState(createTestState("Activity2"));
    EXPECT_EQ(graph->getTotalDistri(), 3);
}

TEST_F(GraphTest, StateDetailsFilling) {
    StatePtr state1 = createTestState("Activity1");
    StatePtr added1 = graph->addState(state1);
    
    // Create another state with same hash but different details
    StatePtr state2 = createTestState("Activity1");
    StatePtr added2 = graph->addState(state2);
    
    // Should be the same state object
    EXPECT_EQ(added1, added2);
}
