/**
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 */

#include "ValuableWidget.h"

namespace fastbotx {

ValuableWidget::ValuableWidget(WidgetPtr widget) {
    fillDetails(widget);

    // get top value of bounds
    RectPtr rect = widget ? widget->getBounds() : nullptr;
    _top = rect ? rect->top : 0;

    // Bounds-based identity hash (LLMDroid-compatible semantics)
    computeHash(rect);
}

void ValuableWidget::fillDetails(WidgetPtr widget) {
    _widgets.insert(widget);
    _classes.insert(widget->getClassname());
    if (widget->hasAction()) {
        std::set<ActionType> tmp = widget->getActions();
        _actions.insert(tmp.begin(), tmp.end());
    } else {
        const std::string textual = widget->getText() + " " + widget->getContentDesc();
        if (!textual.empty()) {
            _info.insert(textual);
        }
    }
}

void ValuableWidget::computeHash(RectPtr rect) {
    if (!rect) {
        this->_hashcode = 0;
        return;
    }
    // 64-bit packed bounds hash (same semantics as Rect::hash2()).
    this->_hashcode = rect->hash2();
}

std::string ValuableWidget::toDescription() {
    std::string desc = generateClass();
    desc.append(std::to_string(_widgets.size()));
    std::string res_id = generateResId();
    if (!res_id.empty()) {
        desc.append(res_id);
    }
    if (!_actions.empty()) {
        desc.append("(");
        for (auto widget : _widgets) {
            std::string tmp = widget->toHTML();
            if (!tmp.empty()) {
                desc.append(tmp).append(" ");
            }
        }
        desc.append(")");
        desc.append(generateAction());
    }
    if (!_info.empty()) {
        desc.append("(");
        for (auto info : _info) {
            desc.append(info).append(" ");
        }
        desc.append(")");
    }
    desc.append("\n");
    return desc;
}

std::string ValuableWidget::generateClass() {
    std::string str;
    for (auto clazz : _classes) {
        str.append(clazz).append(" ");
    }
    return str;
}

std::string ValuableWidget::generateAction() {
    std::string str = " which can ";
    for (auto action : _actions) {
        str.append(actName[action]).append(" ");
    }
    return str;
}

std::string ValuableWidget::generateResId() {
    std::string str;
    std::string res_id = (*(_widgets.begin()))->getResourceID();
    if (res_id.empty()) {
        return str;
    }
    str.append("(resource-id:").append(res_id).append(")");
    return str;
}

ValuableWidget::~ValuableWidget() {
    return;
}

} // namespace fastbotx
