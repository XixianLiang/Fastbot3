/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Widget_CPP_
#define Widget_CPP_


#include "Widget.h"
#include "../utils.hpp"
#include "Preference.h"
#include <algorithm>
#include <utility>

namespace fastbotx {

    Widget::Widget() = default;

    const auto ifCharIsDigitOrBlank = [](const char &c) -> bool {
        return c == ' ' || (c >= '0' && c <= '9');
    };


    /**
     * @brief Constructor creates a Widget from an Element
     * 
     * Initializes a Widget object from an Element, extracting relevant properties
     * and computing the widget's hash code. The text is processed to remove
     * digits and spaces, and may be truncated for Chinese text handling.
     * 
     * Performance optimizations:
     * - Uses move semantics for parent to avoid copying
     * - Efficient string processing with remove_if algorithm
     * - Hash computation only when needed (based on configuration flags)
     * 
     * @param parent Parent widget (nullptr for root widgets)
     * @param element The Element to create widget from
     */
    Widget::Widget(std::shared_ptr<Widget> parent, const ElementPtr &element) {
        // Move parent to avoid copying shared_ptr
        this->_parent = std::move(parent);
        
        // Initialize widget properties from element
        this->initFormElement(element);
        
        // Performance: Remove digits and blank spaces from text using efficient algorithm
        // remove_if moves matching elements to end, returns iterator to new end
        auto removeIterator = std::remove_if(this->_text.begin(), 
                                             this->_text.end(), 
                                             ifCharIsDigitOrBlank);
        // Erase removed elements
        this->_text.erase(removeIterator, this->_text.end());
        
        // Process text for hash computation if text-based state is enabled
        if (STATE_WITH_TEXT || Preference::inst()->isForceUseTextModel()) {
            bool overMaxLen = this->_text.size() > STATE_TEXT_MAX_LEN;
            
            // Temporarily extend text for Chinese character detection
            // (Chinese chars may span multiple bytes, need to check at boundary)
            this->_text = this->_text.substr(0, STATE_TEXT_MAX_LEN * 4);
            size_t cutLength = static_cast<size_t>(STATE_TEXT_MAX_LEN);
            
            // Handle Chinese characters: if cut point is in middle of Chinese char, adjust
            if (this->_text.length() > cutLength && isZhCn(this->_text[STATE_TEXT_MAX_LEN])) {
                size_t ci = 0;
                // Find safe cut point that doesn't split Chinese characters
                for (; ci < cutLength; ci++) {
                    if (isZhCn(this->_text[ci])) {
                        ci += 2; // Chinese chars are typically 2-3 bytes in UTF-8
                    }
                }
                cutLength = ci;
            }

            // Final text truncation
            this->_text = this->_text.substr(0, cutLength);
            
            // Only include text in hash if it wasn't truncated
            // (truncated text would make hash unstable)
            if (!overMaxLen) {
                this->_hashcode ^= (0x79b9 + (std::hash<std::string>{}(this->_text) << 5));
            }
        }

        // Include index in hash if configured
        if (STATE_WITH_INDEX) {
            this->_hashcode ^= ((0x79b9 + (std::hash<int>{}(this->_index) << 6)) << 1);
        }
    }

    void Widget::initFormElement(const ElementPtr &element) {
        if (element->getCheckable())
            enableOperate(OperateType::Checkable);
        if (element->getEnable())
            enableOperate(OperateType::Enable);
        if (element->getClickable())
            enableOperate(OperateType::Clickable);
        if (element->getScrollable())
            enableOperate(OperateType::Scrollable);
        if (element->getLongClickable()) {
            enableOperate(OperateType::LongClickable);
            this->_actions.insert(ActionType::LONG_CLICK);
        }
        if (this->hasOperate(OperateType::Checkable) ||
            this->hasOperate(OperateType::Clickable)) {
            this->_actions.insert(ActionType::CLICK);
        }

        ScrollType scrollType = element->getScrollType();
        switch (scrollType) {
            case ScrollType::NONE:
                break;
            case ScrollType::ALL:
                this->_actions.insert(ActionType::SCROLL_BOTTOM_UP);
                this->_actions.insert(ActionType::SCROLL_TOP_DOWN);
                this->_actions.insert(ActionType::SCROLL_LEFT_RIGHT);
                this->_actions.insert(ActionType::SCROLL_RIGHT_LEFT);
                break;
            case ScrollType::Horizontal:
                this->_actions.insert(ActionType::SCROLL_LEFT_RIGHT);
                this->_actions.insert(ActionType::SCROLL_RIGHT_LEFT);
                break;
            case ScrollType::Vertical:
                this->_actions.insert(ActionType::SCROLL_BOTTOM_UP);
                this->_actions.insert(ActionType::SCROLL_TOP_DOWN);
                break;
            default:
                break;
        }

        if (this->hasAction()) {
            this->_clazz = (element->getClassname());
            this->_isEditable = ("android.widget.EditText" == this->_clazz
                                 || "android.inputmethodservice.ExtractEditText" == this->_clazz
                                 || "android.widget.AutoCompleteTextView" == this->_clazz
                                 || "android.widget.MultiAutoCompleteTextView" == this->_clazz);

            if (SCROLL_BOTTOM_UP_N_ENABLE && (0 == this->_clazz.compare("android.widget.ListView")
                                              || 0 == this->_clazz.compare(
                    "android.support.v7.widget.RecyclerView")
                                              || 0 == this->_clazz.compare(
                    "androidx.recyclerview.widget.RecyclerView"))) {
                this->_actions.insert(ActionType::SCROLL_BOTTOM_UP_N);
            }
            this->_resourceID = (element->getResourceID());
        }
        if (element->getBounds()) {
            this->_bounds = element->getBounds();
        } else {
            this->_bounds = Rect::RectZero;
            BLOGE("Widget::initFormElement: element->getBounds() returned null, using RectZero");
        }
        this->_index = element->getIndex();
        this->_enabled = element->getEnable();
        this->_text = element->getText();
        this->_contextDesc = (element->getContentDesc());
        // compute for only 1 time
        uintptr_t hashcode1 = std::hash<std::string>{}(this->_clazz);
        uintptr_t hashcode2 = std::hash<std::string>{}(this->_resourceID);
        uintptr_t hashcode3 = std::hash<int>{}(this->_operateMask);
        uintptr_t hashcode4 = std::hash<int>{}(scrollType);

        this->_hashcode = ((hashcode1 ^ (hashcode2 << 4)) >> 2) ^
                          (((127U * hashcode3 << 1) ^ (256U * hashcode4 << 3)) >> 1);
    }

    bool Widget::isEditable() const {
        return this->_isEditable;
    }

    void Widget::clearDetails() {
        this->_clazz.clear();
        this->_text.clear();
        this->_contextDesc.clear();
        this->_resourceID.clear();
        this->_bounds = Rect::RectZero;
    }

    void Widget::fillDetails(const std::shared_ptr<Widget> &copy) {
        this->_text = copy->_text;
        this->_clazz = copy->_clazz;
        this->_contextDesc = copy->_contextDesc;
        this->_resourceID = copy->_resourceID;
        this->_bounds = copy->getBounds();
        this->_enabled = copy->_enabled;
    }

    std::string Widget::toString() const {
        return this->toXPath();
    }


    std::string Widget::toXPath() const {
        if (this->_text.empty() && this->_clazz.empty()
            && this->_resourceID.empty()) {
            BDLOG("widget detail has been clear");
            return "";
        }

        // Make local copies to avoid issues if strings are modified during formatting
        std::string clazzCopy = this->_clazz;
        std::string resourceIDCopy = this->_resourceID;
        std::string textCopy = this->_text;
        std::string contextDescCopy = this->_contextDesc;
        
        std::stringstream stringStream;
        std::string boundsStr = "";
        if (this->_bounds != nullptr) {
            boundsStr = this->_bounds->toString();
        } else {
            boundsStr = "[null]";
            BLOGE("Widget::toXPath: _bounds is null for widget");
        }
        stringStream << "{xpath: /*" <<
                     "[@class=\"" << clazzCopy << "\"]" <<
                     "[@resource-id=\"" << resourceIDCopy << "\"]" <<
                     "[@text=\"" << textCopy << "\"]" <<
                     "[@content-desc=\"" << contextDescCopy << "\"]" <<
                     "[@index=" << this->_index << "]" <<
                     "[@bounds=\"" << boundsStr << "\"]}";
        return stringStream.str();
    }

    std::string Widget::toJson() const {
        if (this->_text.empty() && this->_clazz.empty()
            && this->_resourceID.empty()) {
            BDLOG("widget detail has been clear");
            return "";
        }

        nlohmann::json j;
        j["index"] = this->_index;
        j["class"] = this->_clazz;
        j["resource-id"] = this->_resourceID;
        j["text"] =  this->_text;
        j["content-desc"] = this->_contextDesc;
        if (this->_bounds != nullptr) {
            j["bounds"] = this->_bounds->toString();
        } else {
            j["bounds"] = "[null]";
            BLOGE("Widget::toJson: _bounds is null for widget");
        }
        return j.dump();
    }

    std::string Widget::buildFullXpath() const {
        std::string fullXpathString = this->toXPath();
        std::shared_ptr<Widget> parent = _parent;
        while (parent) {
            std::string parentXpath = parent->toXPath();
            parentXpath.append(fullXpathString);
            fullXpathString = parentXpath;
            parent = parent->_parent;
        }
        return fullXpathString;
    }

    Widget::~Widget() {
        this->_actions.clear();
        this->_parent = nullptr;
    }

    uintptr_t Widget::hash() const {
        return _hashcode;
    }

}

#endif //Widget_CPP_
