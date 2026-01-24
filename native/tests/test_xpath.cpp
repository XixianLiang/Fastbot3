/*
 * Unit tests for Xpath class
 */
#include <gtest/gtest.h>
#include "../desc/Element.h"
#include <memory>

using namespace fastbotx;

class XpathTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(XpathTest, DefaultConstructor) {
    Xpath xpath;
    EXPECT_TRUE(xpath.clazz.empty());
    EXPECT_TRUE(xpath.resourceID.empty());
    EXPECT_TRUE(xpath.text.empty());
    EXPECT_TRUE(xpath.contentDescription.empty());
    EXPECT_EQ(xpath.index, 0);
    EXPECT_TRUE(xpath.operationAND);
}

TEST_F(XpathTest, ParameterizedConstructor) {
    std::string xpathStr = "//node[@resource-id='com.test:id/button']";
    Xpath xpath(xpathStr);
    EXPECT_EQ(xpath.toString(), xpathStr);
}

TEST_F(XpathTest, ToString) {
    Xpath xpath;
    xpath.resourceID = "com.test:id/button";
    std::string str = xpath.toString();
    EXPECT_FALSE(str.empty());
}

TEST_F(XpathTest, Properties) {
    Xpath xpath;
    xpath.clazz = "android.widget.Button";
    xpath.resourceID = "com.test:id/button";
    xpath.text = "Click Me";
    xpath.contentDescription = "Button Description";
    xpath.index = 5;
    xpath.operationAND = false;
    
    EXPECT_EQ(xpath.clazz, "android.widget.Button");
    EXPECT_EQ(xpath.resourceID, "com.test:id/button");
    EXPECT_EQ(xpath.text, "Click Me");
    EXPECT_EQ(xpath.contentDescription, "Button Description");
    EXPECT_EQ(xpath.index, 5);
    EXPECT_FALSE(xpath.operationAND);
}
