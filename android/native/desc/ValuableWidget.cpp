/**
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 *
 * @file ValuableWidget.cpp
 * @brief Implements aggregation and line-oriented text formatting for merged widgets in activity briefs.
 */

#include "ValuableWidget.h"

namespace fastbotx {

/** Builds the initial bucket from a single widget (details, top edge, bounds hash). */
ValuableWidget::ValuableWidget(WidgetPtr widget) {
    fillDetails(widget);

    RectPtr rect = widget ? widget->getBounds() : nullptr;
    _top = rect ? rect->top : 0;

    // Bounds identity (`Rect::hash2`) aligns with merging widgets by region in activity summaries.
    computeHash(rect);
}

/** Adds `widget` to `_widgets`, merges class names, actions, or informational text depending on `hasAction()`. */
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

/** Sets `_hashcode` from `Rect::hash2()` when bounds exist; otherwise clears the hash. */
void ValuableWidget::computeHash(RectPtr rect) {
    if (!rect) {
        this->_hashcode = 0;
        return;
    }
    // 64-bit packed bounds hash (same semantics as Rect::hash2()).
    this->_hashcode = rect->hash2();
}

/**
 * Concatenates class names, widget count, optional resource id, optional serialized widgets plus action phrase,
 * or parenthetical info lines for widgets without actions.
 */
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

/** Space-separated unique class strings from `_classes`. */
std::string ValuableWidget::generateClass() {
    std::string str;
    for (auto clazz : _classes) {
        str.append(clazz).append(" ");
    }
    return str;
}

/** Builds `" which can "` followed by global `actName` labels for each merged action type. */
std::string ValuableWidget::generateAction() {
    std::string str = " which can ";
    for (auto action : _actions) {
        str.append(actName[action]).append(" ");
    }
    return str;
}

/** Returns `(resource-id:…)` from the first widget in `_widgets`, or empty when missing (caller must ensure non-empty set). */
std::string ValuableWidget::generateResId() {
    std::string str;
    std::string res_id = (*(_widgets.begin()))->getResourceID();
    if (res_id.empty()) {
        return str;
    }
    str.append("(resource-id:").append(res_id).append(")");
    return str;
}

/** No extra teardown; smart pointers own widget data. */
ValuableWidget::~ValuableWidget() {
    return;
}

} // namespace fastbotx
