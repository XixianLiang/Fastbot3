/*
 * Unit tests for Widget class
 */
#include <gtest/gtest.h>
#include "../desc/Widget.h"
#include "../desc/Element.h"
#include <memory>

using namespace fastbotx;

class WidgetTest : public ::testing::Test {
protected:
    void SetUp() override {
        element = std::make_shared<Element>();
        element->reSetBounds(std::make_shared<Rect>(10, 20, 30, 40));
        element->reSetText("Test Button");
        element->reSetClickable(true);
        element->reSetEnabled(true);
        element->reSetClassname("android.widget.Button");
        element->reSetResourceID("com.test:id/button");
    }
    
    void TearDown() override {}
    
    ElementPtr element;
};

TEST_F(WidgetTest, Constructor) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    EXPECT_NE(widget, nullptr);
}

TEST_F(WidgetTest, GetParent) {
    auto parent = std::make_shared<Widget>(nullptr, std::make_shared<Element>());
    auto child = std::make_shared<Widget>(parent, element);
    
    EXPECT_EQ(child->getParent(), parent);
}

TEST_F(WidgetTest, GetBounds) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    auto bounds = widget->getBounds();
    EXPECT_NE(bounds, nullptr);
    EXPECT_FALSE(bounds->isEmpty());
}

TEST_F(WidgetTest, GetText) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    std::string text = widget->getText();
    EXPECT_FALSE(text.empty());
}

TEST_F(WidgetTest, GetEnabled) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    EXPECT_TRUE(widget->getEnabled());
    
    element->reSetEnabled(false);
    WidgetPtr disabledWidget = std::make_shared<Widget>(nullptr, element);
    EXPECT_FALSE(disabledWidget->getEnabled());
}

TEST_F(WidgetTest, GetActions) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    auto actions = widget->getActions();
    EXPECT_FALSE(actions.empty());
    // Should contain CLICK action
    EXPECT_NE(actions.find(ActionType::CLICK), actions.end());
}

TEST_F(WidgetTest, HasAction) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    EXPECT_TRUE(widget->hasAction());
}

TEST_F(WidgetTest, HasOperate) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    EXPECT_TRUE(widget->hasOperate(OperateType::Clickable));
    EXPECT_TRUE(widget->hasOperate(OperateType::Enable));
}

TEST_F(WidgetTest, IsEditable) {
    element->reSetClassname("android.widget.EditText");
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    EXPECT_TRUE(widget->isEditable());
    
    element->reSetClassname("android.widget.Button");
    WidgetPtr buttonWidget = std::make_shared<Widget>(nullptr, element);
    EXPECT_FALSE(buttonWidget->isEditable());
}

TEST_F(WidgetTest, HashFunction) {
    WidgetPtr widget1 = std::make_shared<Widget>(nullptr, element);
    WidgetPtr widget2 = std::make_shared<Widget>(nullptr, element);
    
    // Same element should produce same hash
    EXPECT_EQ(widget1->hash(), widget2->hash());
}

TEST_F(WidgetTest, ToString) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    std::string str = widget->toString();
    EXPECT_FALSE(str.empty());
}

TEST_F(WidgetTest, ToJson) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    std::string json = widget->toJson();
    EXPECT_FALSE(json.empty());
}

TEST_F(WidgetTest, BuildFullXpath) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    std::string xpath = widget->buildFullXpath();
    EXPECT_FALSE(xpath.empty());
}

TEST_F(WidgetTest, ClearDetails) {
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    widget->clearDetails();
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(WidgetTest, FillDetails) {
    WidgetPtr widget1 = std::make_shared<Widget>(nullptr, element);
    WidgetPtr widget2 = std::make_shared<Widget>(nullptr, element);
    
    widget1->clearDetails();
    widget1->fillDetails(widget2);
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(WidgetTest, LongClickable) {
    // Create element with long-clickable via XML
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<hierarchy>
    <node bounds="[0,0][100,100]" long-clickable="true"/>
</hierarchy>)";
    auto elem = Element::createFromXml(xml);
    ASSERT_NE(elem, nullptr);
    WidgetPtr widget = std::make_shared<Widget>(nullptr, elem);
    auto actions = widget->getActions();
    EXPECT_NE(actions.find(ActionType::LONG_CLICK), actions.end());
}

TEST_F(WidgetTest, Scrollable) {
    element->reSetScrollable(true);
    WidgetPtr widget = std::make_shared<Widget>(nullptr, element);
    auto actions = widget->getActions();
    // Should contain scroll actions
    EXPECT_GT(actions.size(), 0);
}

TEST_F(WidgetTest, Checkable) {
    // Create element with checkable via XML
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<hierarchy>
    <node bounds="[0,0][100,100]" checkable="true"/>
</hierarchy>)";
    auto elem = Element::createFromXml(xml);
    ASSERT_NE(elem, nullptr);
    WidgetPtr widget = std::make_shared<Widget>(nullptr, elem);
    EXPECT_TRUE(widget->hasOperate(OperateType::Checkable));
}
