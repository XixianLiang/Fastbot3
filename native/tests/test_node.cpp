/*
 * Unit tests for Node class
 */
#include <gtest/gtest.h>
#include "../desc/Node.h"
#include <memory>
#include <ctime>

using namespace fastbotx;

class TestNode : public Node {
public:
    TestNode() : Node() {}
    std::string toString() const override {
        return "TestNode";
    }
    virtual ~TestNode() = default;
};

class NodeTest : public ::testing::Test {
protected:
    void SetUp() override {
        node = std::make_shared<TestNode>();
    }
    
    void TearDown() override {}
    
    std::shared_ptr<TestNode> node;
};

TEST_F(NodeTest, Constructor) {
    EXPECT_EQ(node->getVisitedCount(), 0);
    EXPECT_FALSE(node->isVisited());
}

TEST_F(NodeTest, Visit) {
    time_t timestamp = time(nullptr);
    node->visit(timestamp);
    EXPECT_EQ(node->getVisitedCount(), 1);
    EXPECT_TRUE(node->isVisited());
}

TEST_F(NodeTest, MultipleVisits) {
    time_t timestamp = time(nullptr);
    node->visit(timestamp);
    node->visit(timestamp);
    node->visit(timestamp);
    EXPECT_EQ(node->getVisitedCount(), 3);
}

TEST_F(NodeTest, IsVisited) {
    EXPECT_FALSE(node->isVisited());
    node->visit(time(nullptr));
    EXPECT_TRUE(node->isVisited());
}

TEST_F(NodeTest, GetVisitedCount) {
    EXPECT_EQ(node->getVisitedCount(), 0);
    node->visit(time(nullptr));
    EXPECT_EQ(node->getVisitedCount(), 1);
}

TEST_F(NodeTest, SetId) {
    node->setId(100);
    EXPECT_EQ(node->getIdi(), 100);
}

TEST_F(NodeTest, GetId) {
    node->setId(50);
    std::string id = node->getId();
    EXPECT_NE(id.find("50"), std::string::npos);
}

TEST_F(NodeTest, GetIdi) {
    node->setId(75);
    EXPECT_EQ(node->getIdi(), 75);
}

TEST_F(NodeTest, ToString) {
    std::string str = node->toString();
    EXPECT_EQ(str, "TestNode");
}
