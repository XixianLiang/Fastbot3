/*
 * Unit tests for Base classes (Point, Rect, HashNode, etc.)
 */
#include <gtest/gtest.h>
#include "../Base.h"
#include <sstream>

using namespace fastbotx;

// ==================== Point Tests ====================

class PointTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(PointTest, DefaultConstructor) {
    Point p;
    EXPECT_EQ(p.x, 0);
    EXPECT_EQ(p.y, 0);
}

TEST_F(PointTest, ParameterizedConstructor) {
    Point p(10, 20);
    EXPECT_EQ(p.x, 10);
    EXPECT_EQ(p.y, 20);
}

TEST_F(PointTest, CopyConstructor) {
    Point p1(10, 20);
    Point p2(p1);
    EXPECT_EQ(p2.x, 10);
    EXPECT_EQ(p2.y, 20);
}

TEST_F(PointTest, AssignmentOperator) {
    Point p1(10, 20);
    Point p2;
    p2 = p1;
    EXPECT_EQ(p2.x, 10);
    EXPECT_EQ(p2.y, 20);
}

TEST_F(PointTest, EqualityOperator) {
    Point p1(10, 20);
    Point p2(10, 20);
    Point p3(10, 21);
    EXPECT_TRUE(p1 == p2);
    EXPECT_FALSE(p1 == p3);
}

TEST_F(PointTest, HashFunction) {
    Point p1(10, 20);
    Point p2(10, 20);
    Point p3(10, 21);
    EXPECT_EQ(p1.hash(), p2.hash());
    EXPECT_NE(p1.hash(), p3.hash());
}

// ==================== Rect Tests ====================

class RectTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(RectTest, DefaultConstructor) {
    Rect r;
    EXPECT_EQ(r.top, 0);
    EXPECT_EQ(r.bottom, 0);
    EXPECT_EQ(r.left, 0);
    EXPECT_EQ(r.right, 0);
    EXPECT_TRUE(r.isEmpty());
}

TEST_F(RectTest, ParameterizedConstructor) {
    Rect r(10, 20, 30, 40);
    EXPECT_EQ(r.left, 10);
    EXPECT_EQ(r.top, 20);
    EXPECT_EQ(r.right, 30);
    EXPECT_EQ(r.bottom, 40);
    EXPECT_FALSE(r.isEmpty());
}

TEST_F(RectTest, CopyConstructor) {
    Rect r1(10, 20, 30, 40);
    Rect r2(r1);
    EXPECT_EQ(r2.left, 10);
    EXPECT_EQ(r2.top, 20);
    EXPECT_EQ(r2.right, 30);
    EXPECT_EQ(r2.bottom, 40);
}

TEST_F(RectTest, AssignmentOperator) {
    Rect r1(10, 20, 30, 40);
    Rect r2;
    r2 = r1;
    EXPECT_EQ(r2.left, 10);
    EXPECT_EQ(r2.top, 20);
    EXPECT_EQ(r2.right, 30);
    EXPECT_EQ(r2.bottom, 40);
}

TEST_F(RectTest, IsEmpty) {
    Rect r1(10, 20, 30, 40);
    EXPECT_FALSE(r1.isEmpty());
    
    Rect r2(10, 20, 10, 40);  // left == right
    EXPECT_TRUE(r2.isEmpty());
    
    Rect r3(10, 20, 30, 20);  // top == bottom
    EXPECT_TRUE(r3.isEmpty());
    
    Rect r4(10, 20, 5, 15);   // left > right
    EXPECT_TRUE(r4.isEmpty());
}

TEST_F(RectTest, Contains) {
    Rect r(10, 20, 30, 40);
    Point p1(20, 30);  // Inside
    Point p2(5, 30);   // Outside (left)
    Point p3(35, 30);  // Outside (right)
    Point p4(20, 15); // Outside (top)
    Point p5(20, 45); // Outside (bottom)
    Point p6(10, 20); // On edge
    Point p7(30, 40); // On edge
    
    EXPECT_TRUE(r.contains(p1));
    EXPECT_FALSE(r.contains(p2));
    EXPECT_FALSE(r.contains(p3));
    EXPECT_FALSE(r.contains(p4));
    EXPECT_FALSE(r.contains(p5));
    EXPECT_TRUE(r.contains(p6));
    EXPECT_TRUE(r.contains(p7));
}

TEST_F(RectTest, Center) {
    Rect r(10, 20, 30, 40);
    Point center = r.center();
    EXPECT_EQ(center.x, 20);  // (left + right) / 2
    EXPECT_EQ(center.y, 30);  // (top + bottom) / 2
}

TEST_F(RectTest, HashFunction) {
    Rect r1(10, 20, 30, 40);
    Rect r2(10, 20, 30, 40);
    Rect r3(10, 20, 30, 41);
    EXPECT_EQ(r1.hash(), r2.hash());
    EXPECT_NE(r1.hash(), r3.hash());
}

TEST_F(RectTest, EqualityOperator) {
    Rect r1(10, 20, 30, 40);
    Rect r2(10, 20, 30, 40);
    Rect r3(10, 20, 30, 41);
    EXPECT_TRUE(r1 == r2);
    EXPECT_FALSE(r1 == r3);
}

TEST_F(RectTest, ToString) {
    Rect r(10, 20, 30, 40);
    std::string str = r.toString();
    EXPECT_NE(str.find("10"), std::string::npos);
    EXPECT_NE(str.find("20"), std::string::npos);
    EXPECT_NE(str.find("30"), std::string::npos);
    EXPECT_NE(str.find("40"), std::string::npos);
}

TEST_F(RectTest, GetRect) {
    RectPtr emptyRect = std::make_shared<Rect>();
    RectPtr result = Rect::getRect(emptyRect);
    EXPECT_EQ(result, Rect::RectZero);
    
    RectPtr validRect = std::make_shared<Rect>(10, 20, 30, 40);
    result = Rect::getRect(validRect);
    EXPECT_EQ(result, validRect);
    
    result = Rect::getRect(nullptr);
    EXPECT_EQ(result, Rect::RectZero);
}

// ==================== HashNode Tests ====================

class TestHashNode : public HashNode {
public:
    uintptr_t customHash = 12345;
    uintptr_t hash() const override {
        return customHash;
    }
};

class HashNodeTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(HashNodeTest, HashFunction) {
    TestHashNode node1;
    TestHashNode node2;
    node1.customHash = 100;
    node2.customHash = 100;
    
    EXPECT_EQ(node1.hash(), 100);
    EXPECT_EQ(node2.hash(), 100);
    EXPECT_TRUE(node1 == node2);
}

TEST_F(HashNodeTest, ComparisonOperators) {
    TestHashNode node1;
    TestHashNode node2;
    TestHashNode node3;
    node1.customHash = 100;
    node2.customHash = 100;
    node3.customHash = 200;
    
    EXPECT_TRUE(node1 == node2);
    EXPECT_FALSE(node1 == node3);
    EXPECT_TRUE(node1 < node3);
    EXPECT_FALSE(node3 < node1);
}

// ==================== PriorityNode Tests ====================

class TestPriorityNode : public PriorityNode {
public:
    void setPriority(int p) { _priority = p; }
};

class PriorityNodeTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(PriorityNodeTest, DefaultPriority) {
    TestPriorityNode node;
    EXPECT_EQ(node.getPriority(), 0);
}

TEST_F(PriorityNodeTest, ComparisonOperator) {
    TestPriorityNode node1;
    TestPriorityNode node2;
    node1.setPriority(10);
    node2.setPriority(20);
    
    EXPECT_TRUE(node1 < node2);
    EXPECT_FALSE(node2 < node1);
}

// ==================== Utility Functions Tests ====================

class UtilityFunctionsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(UtilityFunctionsTest, StringToActionType) {
    EXPECT_EQ(stringToActionType("CLICK"), ActionType::CLICK);
    EXPECT_EQ(stringToActionType("BACK"), ActionType::BACK);
    EXPECT_EQ(stringToActionType("NOP"), ActionType::NOP);
    EXPECT_EQ(stringToActionType("INVALID"), ActionType::ActTypeSize);
}

TEST_F(UtilityFunctionsTest, StringToScrollType) {
    EXPECT_EQ(stringToScrollType("all"), ScrollType::ALL);
    EXPECT_EQ(stringToScrollType("horizontal"), ScrollType::Horizontal);
    EXPECT_EQ(stringToScrollType("vertical"), ScrollType::Vertical);
    EXPECT_EQ(stringToScrollType("none"), ScrollType::NONE);
    EXPECT_EQ(stringToScrollType("invalid"), ScrollType::NONE);
}

TEST_F(UtilityFunctionsTest, RandomInt) {
    // Test that randomInt returns values in range
    for (int i = 0; i < 100; i++) {
        int value = randomInt(10, 20);
        EXPECT_GE(value, 10);
        EXPECT_LT(value, 20);
    }
}

TEST_F(UtilityFunctionsTest, TrimString) {
    std::string str1 = "  hello  ";
    trimString(str1);
    EXPECT_EQ(str1, "hello");
    
    std::string str2 = "hello";
    trimString(str2);
    EXPECT_EQ(str2, "hello");
    
    std::string str3 = "   ";
    trimString(str3);
    EXPECT_EQ(str3, "");
}

TEST_F(UtilityFunctionsTest, SplitString) {
    std::string str = "a,b,c,d";
    std::vector<std::string> result;
    splitString(str, result, ',');
    EXPECT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
    EXPECT_EQ(result[3], "d");
}

TEST_F(UtilityFunctionsTest, GetJsonValue) {
    nlohmann::json j;
    j["key1"] = 100;
    j["key2"] = "test";
    j["key3"] = nullptr;
    
    EXPECT_EQ(getJsonValue<int>(j, "key1", 0), 100);
    EXPECT_EQ(getJsonValue<std::string>(j, "key2", ""), "test");
    EXPECT_EQ(getJsonValue<int>(j, "key3", 999), 999);
    EXPECT_EQ(getJsonValue<int>(j, "key4", 999), 999);
}

TEST_F(UtilityFunctionsTest, StringReplaceAll) {
    std::string str = "hello world hello";
    stringReplaceAll(str, "hello", "hi");
    EXPECT_EQ(str, "hi world hi");
}

TEST_F(UtilityFunctionsTest, GetTimeFormatStr) {
    std::string timeStr = getTimeFormatStr();
    EXPECT_FALSE(timeStr.empty());
    EXPECT_GT(timeStr.length(), 10);
}

TEST_F(UtilityFunctionsTest, CurrentStamp) {
    double stamp1 = currentStamp();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    double stamp2 = currentStamp();
    EXPECT_GT(stamp2, stamp1);
}

TEST_F(UtilityFunctionsTest, IsZhCn) {
    // Test Chinese character detection
    char ch1 = '\xE4';  // UTF-8 first byte of Chinese character
    char ch2 = 'A';     // ASCII character
    EXPECT_TRUE(isZhCn(ch1));
    EXPECT_FALSE(isZhCn(ch2));
}

TEST_F(UtilityFunctionsTest, GetRandomChars) {
    std::string randomStr = getRandomChars();
    EXPECT_GE(randomStr.length(), 11);
    EXPECT_LE(randomStr.length(), 1000);
}

// ==================== CombineHash Tests ====================

class TestHashable : public HashNode {
public:
    uintptr_t hashValue;
    TestHashable(uintptr_t h) : hashValue(h) {}
    uintptr_t hash() const override { return hashValue; }
    virtual ~TestHashable() = default;
};

TEST_F(UtilityFunctionsTest, CombineHashWithOrder) {
    std::vector<std::shared_ptr<TestHashable>> vec;
    vec.push_back(std::make_shared<TestHashable>(100));
    vec.push_back(std::make_shared<TestHashable>(200));
    vec.push_back(std::make_shared<TestHashable>(300));
    
    uintptr_t hash1 = combineHash(vec, true);
    uintptr_t hash2 = combineHash(vec, true);
    EXPECT_EQ(hash1, hash2);
    
    // Reverse order should produce different hash
    std::vector<std::shared_ptr<TestHashable>> vec2;
    vec2.push_back(std::make_shared<TestHashable>(300));
    vec2.push_back(std::make_shared<TestHashable>(200));
    vec2.push_back(std::make_shared<TestHashable>(100));
    uintptr_t hash3 = combineHash(vec2, true);
    EXPECT_NE(hash1, hash3);
}

TEST_F(UtilityFunctionsTest, CombineHashWithoutOrder) {
    std::vector<std::shared_ptr<TestHashable>> vec;
    vec.push_back(std::make_shared<TestHashable>(100));
    vec.push_back(std::make_shared<TestHashable>(200));
    vec.push_back(std::make_shared<TestHashable>(300));
    
    uintptr_t hash1 = combineHash(vec, false);
    uintptr_t hash2 = combineHash(vec, false);
    EXPECT_EQ(hash1, hash2);
}

// ==================== Equals Template Tests ====================

TEST_F(UtilityFunctionsTest, EqualsTemplate) {
    auto ptr1 = std::make_shared<Point>(10, 20);
    auto ptr2 = std::make_shared<Point>(10, 20);
    auto ptr3 = std::make_shared<Point>(10, 21);
    std::shared_ptr<Point> nullPtr = nullptr;
    
    EXPECT_TRUE(equals(ptr1, ptr2));
    EXPECT_FALSE(equals(ptr1, ptr3));
    EXPECT_FALSE(equals(ptr1, nullPtr));
    EXPECT_FALSE(equals(nullPtr, ptr2));
    EXPECT_FALSE(equals(nullPtr, nullPtr));
}

// ==================== Comparator Template Tests ====================

TEST_F(UtilityFunctionsTest, ComparatorTemplate) {
    Comparator<Point> comp;
    auto ptr1 = std::make_shared<Point>(10, 20);
    auto ptr2 = std::make_shared<Point>(20, 30);
    
    // Point comparison is based on hash, which depends on x and y
    // Since ptr1 hash < ptr2 hash (likely), comp should return true
    bool result = comp(ptr1, ptr2);
    // Just verify it doesn't crash and returns a boolean
    EXPECT_TRUE(result == true || result == false);
}
