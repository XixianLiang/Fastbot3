/*
 * Unit tests for DeviceOperateWrapper class
 */
#include <gtest/gtest.h>
#include "../desc/DeviceOperateWrapper.h"
#include "../Base.h"

using namespace fastbotx;

class DeviceOperateWrapperTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(DeviceOperateWrapperTest, DefaultConstructor) {
    DeviceOperateWrapper opt;
    EXPECT_EQ(opt.act, ActionType::NOP);
    EXPECT_EQ(opt.throttle, 0);
    EXPECT_EQ(opt.waitTime, 0);
    EXPECT_FALSE(opt.editable);
    EXPECT_FALSE(opt.clear);
    EXPECT_FALSE(opt.adbInput);
    EXPECT_TRUE(opt.allowFuzzing);
}

TEST_F(DeviceOperateWrapperTest, CopyConstructor) {
    DeviceOperateWrapper opt1;
    opt1.act = ActionType::CLICK;
    opt1.throttle = 100.0f;
    opt1.waitTime = 500;
    opt1.setText("test");
    
    DeviceOperateWrapper opt2(opt1);
    EXPECT_EQ(opt2.act, ActionType::CLICK);
    EXPECT_EQ(opt2.throttle, 100.0f);
    EXPECT_EQ(opt2.waitTime, 500);
    EXPECT_EQ(opt2.getText(), "test");
}

TEST_F(DeviceOperateWrapperTest, AssignmentOperator) {
    DeviceOperateWrapper opt1;
    opt1.act = ActionType::CLICK;
    opt1.setText("test1");
    
    DeviceOperateWrapper opt2;
    opt2 = opt1;
    EXPECT_EQ(opt2.act, ActionType::CLICK);
    EXPECT_EQ(opt2.getText(), "test1");
}

TEST_F(DeviceOperateWrapperTest, SetText) {
    DeviceOperateWrapper opt;
    opt.editable = true;
    
    std::string result = opt.setText("Hello World");
    EXPECT_EQ(result, "Hello World");
    EXPECT_EQ(opt.getText(), "Hello World");
}

TEST_F(DeviceOperateWrapperTest, SetText_TooLong) {
    DeviceOperateWrapper opt;
    opt.editable = true;
    
    std::string longText(2000, 'a');
    std::string result = opt.setText(longText);
    EXPECT_EQ(result.length(), 999);
    EXPECT_EQ(opt.getText().length(), 999);
}

TEST_F(DeviceOperateWrapperTest, GetText) {
    DeviceOperateWrapper opt;
    opt.setText("test");
    EXPECT_EQ(opt.getText(), "test");
}

TEST_F(DeviceOperateWrapperTest, ToString) {
    DeviceOperateWrapper opt;
    opt.act = ActionType::CLICK;
    opt.pos = Rect(10, 20, 30, 40);
    opt.setText("test");
    
    std::string str = opt.toString();
    EXPECT_FALSE(str.empty());
    EXPECT_NE(str.find("CLICK"), std::string::npos);
}

TEST_F(DeviceOperateWrapperTest, ConstructorFromJson) {
    std::string json = R"({
        "act": "CLICK",
        "pos": [10, 20, 30, 40],
        "throttle": 100,
        "wait_time": 500,
        "adb_input": true
    })";
    
    DeviceOperateWrapper opt(json);
    EXPECT_EQ(opt.act, ActionType::CLICK);
    EXPECT_EQ(opt.pos.left, 10);
    EXPECT_EQ(opt.pos.top, 20);
    EXPECT_EQ(opt.pos.right, 30);
    EXPECT_EQ(opt.pos.bottom, 40);
    EXPECT_EQ(opt.throttle, 100.0f);
    EXPECT_EQ(opt.waitTime, 500);
    EXPECT_TRUE(opt.adbInput);
}

TEST_F(DeviceOperateWrapperTest, ConstructorFromJson_Invalid) {
    std::string invalidJson = "invalid json";
    DeviceOperateWrapper opt(invalidJson);
    // Should handle gracefully, default to NOP
    EXPECT_EQ(opt.act, ActionType::NOP);
}

TEST_F(DeviceOperateWrapperTest, ConstructorFromJson_InvalidAction) {
    std::string json = R"({"act": "INVALID_ACTION"})";
    DeviceOperateWrapper opt(json);
    // Should default to NOP
    EXPECT_EQ(opt.act, ActionType::NOP);
}

TEST_F(DeviceOperateWrapperTest, ConstructorFromJson_NoPos) {
    std::string json = R"({"act": "CLICK"})";
    DeviceOperateWrapper opt(json);
    EXPECT_EQ(opt.act, ActionType::CLICK);
}

TEST_F(DeviceOperateWrapperTest, OperateNop) {
    EXPECT_NE(DeviceOperateWrapper::OperateNop, nullptr);
    EXPECT_EQ(DeviceOperateWrapper::OperateNop->act, ActionType::NOP);
}
