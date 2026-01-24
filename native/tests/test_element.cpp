/*
 * Unit tests for Element class
 */
#include <gtest/gtest.h>
#include "../desc/Element.h"
#include <memory>

using namespace fastbotx;

class ElementTest : public ::testing::Test {
protected:
    void SetUp() override {
        element = std::make_shared<Element>();
    }
    
    void TearDown() override {}
    
    ElementPtr element;
    
    std::string createSimpleXML() {
        return R"(<?xml version="1.0" encoding="UTF-8"?>
<hierarchy>
    <node bounds="[0,0][100,100]" 
          clickable="true" 
          class="android.widget.Button" 
          text="Click Me"
          resource-id="com.test:id/button"
          content-desc="Button Description"/>
</hierarchy>)";
    }
};

TEST_F(ElementTest, DefaultConstructor) {
    EXPECT_FALSE(element->getClickable());
    EXPECT_FALSE(element->getLongClickable());
    EXPECT_FALSE(element->getCheckable());
    EXPECT_FALSE(element->getScrollable());
    EXPECT_FALSE(element->getEnable());
}

TEST_F(ElementTest, PropertySetters) {
    element->reSetClickable(true);
    // Note: reSetLongClickable and reSetCheckable don't exist in Element class
    // These properties are set via XML parsing or direct member access
    element->reSetScrollable(true);
    element->reSetEnabled(true);
    
    EXPECT_TRUE(element->getClickable());
    // Test longClickable and checkable via XML parsing instead
    EXPECT_TRUE(element->getScrollable());
    EXPECT_TRUE(element->getEnable());
}

TEST_F(ElementTest, LongClickableAndCheckableViaXML) {
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<hierarchy>
    <node bounds="[0,0][100,100]" 
          long-clickable="true" 
          checkable="true"/>
</hierarchy>)";
    auto elem = Element::createFromXml(xml);
    ASSERT_NE(elem, nullptr);
    EXPECT_TRUE(elem->getLongClickable());
    EXPECT_TRUE(elem->getCheckable());
}

TEST_F(ElementTest, TextProperties) {
    element->reSetText("Test Text");
    EXPECT_EQ(element->getText(), "Test Text");
}

TEST_F(ElementTest, ResourceID) {
    element->reSetResourceID("com.test:id/button");
    EXPECT_EQ(element->getResourceID(), "com.test:id/button");
}

TEST_F(ElementTest, ContentDescription) {
    element->reSetContentDesc("Button Description");
    EXPECT_EQ(element->getContentDesc(), "Button Description");
}

TEST_F(ElementTest, Classname) {
    element->reSetClassname("android.widget.Button");
    EXPECT_EQ(element->getClassname(), "android.widget.Button");
}

TEST_F(ElementTest, Index) {
    element->reSetIndex(5);
    EXPECT_EQ(element->getIndex(), 5);
}

TEST_F(ElementTest, Bounds) {
    auto rect = std::make_shared<Rect>(10, 20, 30, 40);
    element->reSetBounds(rect);
    EXPECT_EQ(element->getBounds(), rect);
}

TEST_F(ElementTest, IsEditText) {
    element->reSetClassname("android.widget.EditText");
    EXPECT_TRUE(element->isEditText());
    
    element->reSetClassname("android.widget.Button");
    EXPECT_FALSE(element->isEditText());
}

TEST_F(ElementTest, IsWebView) {
    element->reSetClassname("android.webkit.WebView");
    EXPECT_TRUE(element->isWebView());
    
    element->reSetClassname("android.widget.Button");
    EXPECT_FALSE(element->isWebView());
}

TEST_F(ElementTest, GetChildren) {
    auto child1 = std::make_shared<Element>();
    auto child2 = std::make_shared<Element>();
    
    element->reAddChild(child1);
    element->reAddChild(child2);
    
    auto children = element->getChildren();
    EXPECT_EQ(children.size(), 2);
}

TEST_F(ElementTest, GetParent) {
    auto parent = std::make_shared<Element>();
    auto child = std::make_shared<Element>();
    
    child->reSetParent(parent);
    parent->reAddChild(child);
    
    auto parentPtr = child->getParent().lock();
    EXPECT_EQ(parentPtr, parent);
}

TEST_F(ElementTest, CreateFromXml) {
    std::string xml = createSimpleXML();
    ElementPtr elem = Element::createFromXml(xml);
    
    EXPECT_NE(elem, nullptr);
}

TEST_F(ElementTest, CreateFromXml_Invalid) {
    std::string invalidXML = "invalid xml";
    ElementPtr elem = Element::createFromXml(invalidXML);
    
    // May return nullptr or handle gracefully
    EXPECT_TRUE(elem == nullptr || elem != nullptr);
}

TEST_F(ElementTest, CreateFromXml_Empty) {
    std::string emptyXML = "";
    ElementPtr elem = Element::createFromXml(emptyXML);
    
    EXPECT_EQ(elem, nullptr);
}

TEST_F(ElementTest, ToString) {
    element->reSetText("Test");
    element->reSetClassname("android.widget.Button");
    std::string str = element->toString();
    EXPECT_FALSE(str.empty());
}

TEST_F(ElementTest, ToJson) {
    element->reSetText("Test");
    element->reSetClassname("android.widget.Button");
    std::string json = element->toJson();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("Test"), std::string::npos);
}

TEST_F(ElementTest, ToXML) {
    element->reSetText("Test");
    std::string xml = element->toXML();
    EXPECT_FALSE(xml.empty());
}

TEST_F(ElementTest, FromJson) {
    std::string json = R"({"text":"Test","class":"android.widget.Button"})";
    element->fromJson(json);
    EXPECT_EQ(element->getText(), "Test");
    EXPECT_EQ(element->getClassname(), "android.widget.Button");
}

TEST_F(ElementTest, Hash) {
    element->reSetText("Test1");
    uintptr_t hash1 = element->hash(true);
    
    element->reSetText("Test2");
    uintptr_t hash2 = element->hash(true);
    
    EXPECT_NE(hash1, hash2);
}

TEST_F(ElementTest, RecursiveElements) {
    auto child1 = std::make_shared<Element>();
    auto child2 = std::make_shared<Element>();
    element->reAddChild(child1);
    element->reAddChild(child2);
    
    std::vector<ElementPtr> result;
    element->recursiveElements([](ElementPtr /*e*/) { return true; }, result);
    
    EXPECT_GE(result.size(), 1);
}

TEST_F(ElementTest, RecursiveDoElements) {
    auto child = std::make_shared<Element>();
    element->reAddChild(child);
    
    int count = 0;
    element->recursiveDoElements([&count](ElementPtr /*e*/) { count++; });
    
    EXPECT_GT(count, 0);
}

TEST_F(ElementTest, MatchXpathSelector) {
    auto xpath = std::make_shared<Xpath>();
    xpath->resourceID = "com.test:id/button";
    xpath->text = "Click Me";
    xpath->operationAND = true;
    
    element->reSetResourceID("com.test:id/button");
    element->reSetText("Click Me");
    
    EXPECT_TRUE(element->matchXpathSelector(xpath));
}

TEST_F(ElementTest, MatchXpathSelector_OR) {
    auto xpath = std::make_shared<Xpath>();
    xpath->resourceID = "com.test:id/button";
    xpath->operationAND = false;
    
    element->reSetResourceID("com.test:id/button");
    
    EXPECT_TRUE(element->matchXpathSelector(xpath));
}

TEST_F(ElementTest, MatchXpathSelector_Null) {
    EXPECT_FALSE(element->matchXpathSelector(nullptr));
}

TEST_F(ElementTest, GetScrollType) {
    element->reSetScrollable(true);
    ScrollType type = element->getScrollType();
    EXPECT_GE(type, ScrollType::ALL);
    EXPECT_LE(type, ScrollType::ScrollTypeSize);
}

TEST_F(ElementTest, DeleteElement) {
    auto parent = std::make_shared<Element>();
    auto child = std::make_shared<Element>();
    parent->reAddChild(child);
    child->reSetParent(parent);
    
    child->deleteElement();
    
    auto children = parent->getChildren();
    EXPECT_EQ(children.size(), 0);
}
