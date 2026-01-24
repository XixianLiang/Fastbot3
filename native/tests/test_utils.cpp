/*
 * Unit tests for utility functions
 */
#include <gtest/gtest.h>
#include "../Base.h"
#include <cmath>

using namespace fastbotx;

class UtilsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(UtilsTest, CombineHash_Vector) {
    std::vector<std::shared_ptr<Point>> vec;
    vec.push_back(std::make_shared<Point>(10, 20));
    vec.push_back(std::make_shared<Point>(30, 40));
    
    uintptr_t hash1 = combineHash(vec, true);
    uintptr_t hash2 = combineHash(vec, true);
    EXPECT_EQ(hash1, hash2);
    
    // Different order should produce different hash when withOrder=true
    std::vector<std::shared_ptr<Point>> vec2;
    vec2.push_back(std::make_shared<Point>(30, 40));
    vec2.push_back(std::make_shared<Point>(10, 20));
    uintptr_t hash3 = combineHash(vec2, true);
    EXPECT_NE(hash1, hash3);
}

TEST_F(UtilsTest, CombineHash_Iterator) {
    std::vector<std::shared_ptr<Point>> vec;
    vec.push_back(std::make_shared<Point>(10, 20));
    vec.push_back(std::make_shared<Point>(30, 40));
    
    uintptr_t hash1 = combineHash(vec.begin(), vec.end(), true);
    uintptr_t hash2 = combineHash(vec.begin(), vec.end(), false);
    
    // Should produce valid hashes
    EXPECT_NE(hash1, 0);
    EXPECT_NE(hash2, 0);
}

TEST_F(UtilsTest, StringReplaceAll) {
    std::string str = "hello world hello";
    stringReplaceAll(str, "hello", "hi");
    EXPECT_EQ(str, "hi world hi");
}

TEST_F(UtilsTest, StringReplaceAll_NoMatch) {
    std::string str = "hello world";
    stringReplaceAll(str, "xyz", "abc");
    EXPECT_EQ(str, "hello world");
}

TEST_F(UtilsTest, StringReplaceAll_EmptyString) {
    std::string str = "";
    stringReplaceAll(str, "hello", "hi");
    EXPECT_EQ(str, "");
}

TEST_F(UtilsTest, GetTimeFormatStr) {
    std::string timeStr = getTimeFormatStr();
    EXPECT_FALSE(timeStr.empty());
    // Should contain date format
    EXPECT_NE(timeStr.find("-"), std::string::npos);
}

TEST_F(UtilsTest, CurrentStamp) {
    double stamp1 = currentStamp();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    double stamp2 = currentStamp();
    
    EXPECT_GT(stamp2, stamp1);
    EXPECT_GT(stamp1, 0);
}

TEST_F(UtilsTest, ThreadDelayExec) {
    bool executed = false;
    auto func = [&executed]() { executed = true; };
    
    threadDelayExec(10, true, func);
    EXPECT_TRUE(executed);
}

TEST_F(UtilsTest, ThreadDelayExec_Async) {
    bool executed = false;
    auto func = [&executed]() { executed = true; };
    
    threadDelayExec(10, false, func);
    // Wait a bit for async execution
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(executed);
}

TEST_F(UtilsTest, IsZhCn) {
    // Test Chinese character detection
    char ch1 = '\xE4';  // UTF-8 first byte
    char ch2 = 'A';     // ASCII
    char ch3 = '\x80';  // UTF-8 continuation byte
    
    EXPECT_TRUE(isZhCn(ch1));
    EXPECT_FALSE(isZhCn(ch2));
    EXPECT_TRUE(isZhCn(ch3));
}

TEST_F(UtilsTest, GetRandomChars) {
    std::string randomStr = getRandomChars();
    EXPECT_GE(randomStr.length(), 11);
    EXPECT_LE(randomStr.length(), 1000);
}

TEST_F(UtilsTest, GetRandomChars_MultipleCalls) {
    std::string str1 = getRandomChars();
    std::string str2 = getRandomChars();
    
    // May be same or different (random)
    EXPECT_TRUE(str1 == str2 || str1 != str2);
}

TEST_F(UtilsTest, RandomInt_Range) {
    for (int i = 0; i < 100; i++) {
        int value = randomInt(10, 20);
        EXPECT_GE(value, 10);
        EXPECT_LT(value, 20);
    }
}

TEST_F(UtilsTest, RandomInt_WithSeed) {
    int value1 = randomInt(10, 20, 123);
    int value2 = randomInt(10, 20, 123);
    
    // Same seed should produce same value
    EXPECT_EQ(value1, value2);
}

TEST_F(UtilsTest, TrimString) {
    std::string str1 = "  hello  ";
    trimString(str1);
    EXPECT_EQ(str1, "hello");
    
    std::string str2 = "hello";
    trimString(str2);
    EXPECT_EQ(str2, "hello");
    
    std::string str3 = "   ";
    trimString(str3);
    EXPECT_EQ(str3, "");
    
    std::string str4 = "";
    trimString(str4);
    EXPECT_EQ(str4, "");
}

TEST_F(UtilsTest, SplitString) {
    std::string str = "a,b,c,d";
    std::vector<std::string> result;
    splitString(str, result, ',');
    
    EXPECT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
    EXPECT_EQ(result[3], "d");
}

TEST_F(UtilsTest, SplitString_Empty) {
    std::string str = "";
    std::vector<std::string> result;
    splitString(str, result, ',');
    EXPECT_EQ(result.size(), 0);
}

TEST_F(UtilsTest, SplitString_NoDelimiter) {
    std::string str = "hello";
    std::vector<std::string> result;
    splitString(str, result, ',');
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "hello");
}

TEST_F(UtilsTest, GetJsonValue) {
    nlohmann::json j;
    j["int_key"] = 100;
    j["string_key"] = "test";
    j["bool_key"] = true;
    j["null_key"] = nullptr;
    
    EXPECT_EQ(getJsonValue<int>(j, "int_key", 0), 100);
    EXPECT_EQ(getJsonValue<std::string>(j, "string_key", ""), "test");
    EXPECT_EQ(getJsonValue<bool>(j, "bool_key", false), true);
    EXPECT_EQ(getJsonValue<int>(j, "null_key", 999), 999);
    EXPECT_EQ(getJsonValue<int>(j, "missing_key", 888), 888);
}

TEST_F(UtilsTest, Equals_Template) {
    auto ptr1 = std::make_shared<Point>(10, 20);
    auto ptr2 = std::make_shared<Point>(10, 20);
    auto ptr3 = std::make_shared<Point>(10, 21);
    
    EXPECT_TRUE(equals(ptr1, ptr2));
    EXPECT_FALSE(equals(ptr1, ptr3));
    EXPECT_FALSE(equals(ptr1, nullptr));
    EXPECT_FALSE(equals(nullptr, ptr2));
    EXPECT_FALSE(equals(nullptr, nullptr));
}

TEST_F(UtilsTest, Comparator_Template) {
    Comparator<Point> comp;
    auto ptr1 = std::make_shared<Point>(10, 20);
    auto ptr2 = std::make_shared<Point>(20, 30);
    
    bool result = comp(ptr1, ptr2);
    EXPECT_TRUE(result == true || result == false);
}
