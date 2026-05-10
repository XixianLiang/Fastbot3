/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @file RichWidget.cpp
 *
 * Implements rich hashing for reuse states: combines structural ids with optional descendant text,
 * and supports masked hashes for dynamic state abstraction.
 *
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef RichWidget_CPP_
#define RichWidget_CPP_


#include "RichWidget.h"
#include "../utils.hpp"
#include "../events/Preference.h"
#include <algorithm>
#include <utility>

namespace fastbotx {


    /**
     * Builds `_widgetHashcodeBase` from class, resource id, and supported action kinds, then XORs in a
     * text term when `getValidTextFromWidgetAndChildren` finds displayable copy (node or descendants).
     */
    RichWidget::RichWidget(WidgetPtr parent, const ElementPtr &element)
            : Widget(std::move(parent), element) {
        uintptr_t hashcode1 = fastbotx::fastStringHash(this->_clazz);
        uintptr_t hashcode2 = fastbotx::fastStringHash(this->_resourceID);

        uintptr_t hashcode3 = 0x1;
        for (ActionType actionType: this->getActions()) {
            hashcode3 ^= (127U * std::hash<int>{}(static_cast<int>(actionType)));
        }

        this->_widgetHashcodeBase = ((hashcode1 ^ (hashcode2 << 4)) >> 2) ^ ((127U * hashcode3 << 1));
        this->_widgetHashcode = this->_widgetHashcodeBase;

        std::string elementText = this->getValidTextFromWidgetAndChildren(element);
        if (!elementText.empty()) {
            this->_widgetHashcode ^= (0x79b9 + (fastbotx::fastStringHash(elementText) << 1));
        }
    }

    /**
     * Static reuse abstraction: recursive gather that skips borrowing labels from clickable children
     * (matches the historical static-reuse text policy). Otherwise: iterative DFS over `validText` fields.
     */
    std::string RichWidget::getValidTextFromWidgetAndChildren(const ElementPtr &element) const {
        // Static reuse abstraction path (distinct rules from dynamic mode below).
        if (Preference::inst() && Preference::inst()->useStaticReuseAbstraction()) {
            std::function<std::string(const ElementPtr &)> getElementText =
                [&getElementText](const ElementPtr &elem) -> std::string {
                    std::string txt = elem->validText;
                    if (txt.empty()) {
                        bool useChildText = true;
                        for (auto &child : elem->getChildren()) {
                            if (child->getClickable() || child->getLongClickable()) {
                                useChildText = false;
                            }
                            if (txt.empty()) {
                                txt = getElementText(child);
                            }
                        }
                        if (useChildText && !txt.empty()) {
                            return txt;
                        }
                    }
                    return txt;
                };
            return getElementText(element);
        }

        if (!element->validText.empty()) {
            return element->validText;
        }

        std::vector<ElementPtr> stack;
        stack.reserve(32);

        const auto &children = element->getChildren();
        stack.insert(stack.end(), children.begin(), children.end());

        while (!stack.empty()) {
            ElementPtr current = stack.back();
            stack.pop_back();

            if (!current->validText.empty()) {
                return current->validText;
            }

            const auto &currentChildren = current->getChildren();
            stack.insert(stack.end(), currentChildren.begin(), currentChildren.end());
        }

        return "";
    }

    RichWidget::RichWidget()
            : Widget() {

    }

    uintptr_t RichWidget::hash() const {
        return getActHashCode();
    }

    uintptr_t RichWidget::hashWithMask(WidgetKeyMask mask) const {
        // Start from the structural mix (class / resource / actions); fold in base `Widget` attribute hashes per mask bit.
        uintptr_t h = _widgetHashcodeBase;
        if (mask & static_cast<WidgetKeyMask>(WidgetKeyAttr::Text)) h ^= _hashText;
        if (mask & static_cast<WidgetKeyMask>(WidgetKeyAttr::ContentDesc)) h ^= _hashContentDesc;
        if (mask & static_cast<WidgetKeyMask>(WidgetKeyAttr::Index)) h ^= _hashIndex;
        return h;
    }

}

#endif //RichWidget_CPP_
