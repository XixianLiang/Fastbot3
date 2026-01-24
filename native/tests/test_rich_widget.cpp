/*
 * Unit tests for RichWidget class
 */
#include <gtest/gtest.h>
#include "../desc/reuse/RichWidget.h"
#include "../desc/Element.h"
#include <memory>

using namespace fastbotx;

typedef std::shared_ptr<RichWidget> RichWidgetPtr;

class RichWidgetTest : public ::testing::Test {
protected:
    void SetUp() override {
        element = std::make_shared<Element>();
        element->reSetBounds(std::make_shared<Rect>(0, 0, 100, 100));
        element->reSetClickable(true);
        element->reSetText("Test Button");
        element->reSetResourceID("com.test:id/button");
        element->reSetClassname("android.widget.Button");
    }
    
    void TearDown() override {}
    
    ElementPtr element;
};

TEST_F(RichWidgetTest, Constructor) {
    RichWidgetPtr widget = std::make_shared<RichWidget>(nullptr, element);
    EXPECT_NE(widget, nullptr);
}

TEST_F(RichWidgetTest, HashFunction) {
    RichWidgetPtr widget1 = std::make_shared<RichWidget>(nullptr, element);
    RichWidgetPtr widget2 = std::make_shared<RichWidget>(nullptr, element);
    
    // Same element should produce same hash
    EXPECT_EQ(widget1->hash(), widget2->hash());
}

TEST_F(RichWidgetTest, GetActHashCode) {
    RichWidgetPtr widget = std::make_shared<RichWidget>(nullptr, element);
    uintptr_t hash = widget->getActHashCode();
    EXPECT_NE(hash, 0);
}

TEST_F(RichWidgetTest, GetText) {
    RichWidgetPtr widget = std::make_shared<RichWidget>(nullptr, element);
    std::string text = widget->getText();
    EXPECT_FALSE(text.empty());
}

TEST_F(RichWidgetTest, GetBounds) {
    RichWidgetPtr widget = std::make_shared<RichWidget>(nullptr, element);
    auto bounds = widget->getBounds();
    EXPECT_NE(bounds, nullptr);
}

TEST_F(RichWidgetTest, GetActions) {
    RichWidgetPtr widget = std::make_shared<RichWidget>(nullptr, element);
    auto actions = widget->getActions();
    EXPECT_FALSE(actions.empty());
}

TEST_F(RichWidgetTest, HasAction) {
    RichWidgetPtr widget = std::make_shared<RichWidget>(nullptr, element);
    EXPECT_TRUE(widget->hasAction());
}

TEST_F(RichWidgetTest, HashWithChildren) {
    // Create element with children
    auto child = std::make_shared<Element>();
    child->reSetText("Child Text");
    element->reAddChild(child);
    
    RichWidgetPtr widget = std::make_shared<RichWidget>(nullptr, element);
    uintptr_t hash = widget->hash();
    EXPECT_NE(hash, 0);
}
