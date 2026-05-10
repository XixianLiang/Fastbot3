/**
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 *
 * @file StateStucture.cpp
 * @brief Implements `StateStructure`: depth-first traversal over an `Element` tree, optional
 *        human-readable state description (class, resource id, text/description, stable id,
 *        actionable capabilities), and lookups by pointer identity or stable element id.
 */

#include "StateStructure.h"
#include <algorithm>
#include <sstream>

namespace fastbotx {

/** Clears the traversal stack, seeds it with root children, and returns the root (or nullptr). */
const ElementPtr StateStructure::getFirst() {
    while (!this->_stack.empty()) {
        this->_stack.pop();
    }
    if (!this->_rootElement) {
        return nullptr;
    }
    for (ElementPtr child : this->_rootElement->getChildren()) {
        this->_stack.push(child);
    }
    return this->_rootElement;
}

/** Pops the next node from the DFS stack, pushes its children, and returns the popped node. */
const ElementPtr StateStructure::getNext() {
    if (this->_stack.empty()) {
        return nullptr;
    }
    ElementPtr target = this->_stack.top();
    this->_stack.pop();
    for (ElementPtr child : target->getChildren()) {
        this->_stack.push(child);
    }
    return target;
}

/** Re-attaches `child` under `_rootElement` when both exist (see `Element::reAddChild`). */
void StateStructure::addChildElement(ElementPtr child) {
    if (!_rootElement || !child) {
        return;
    }
    _rootElement->reAddChild(child);
}

/** Lookup in `_elementMap` by raw pointer address cast to `uintptr_t`. */
const ElementPtr StateStructure::findElement(const uintptr_t target) {
    auto result = this->_elementMap.find(target);
    if (result != this->_elementMap.end()) {
        return result->second;
    } else {
        return nullptr;
    }
}

/** Appends `tabCount` tab characters to `_stateDescription` for indentation. */
void StateStructure::addTab() {
    for (int i = 0; i < this->tabCount; i++) {
        this->_stateDescription.append("\t");
    }
}

/** Reserved hook for subtree merge rules; currently always returns false. */
bool StateStructure::shouldMerge(const ElementPtr father, const ElementPtr child) {
    (void)father;
    (void)child;
    return false;
}

/** Inserts `element` into `_elements`; returns true if it was newly inserted. */
bool StateStructure::insertElement(ElementPtr element) {
    auto res = this->_elements.insert(element);
    return res.second;
}

/** Linear scan of `_elements` for the given stable element id (may run off-thread per header). */
ElementPtr StateStructure::findElementById(int id) {
    auto found = std::find_if(_elements.begin(), _elements.end(), [id](const ElementPtr &ptr) {
        return ptr && ptr->getStableElementId() == id;
    });
    return (found != _elements.end()) ? *found : nullptr;
}

/**
 * Builds or returns a cached multi-line description of the UI subtree under `_rootElement`.
 * Each line: class, optional resource id, optional text/content description, stable id (`sid=`),
 * and a plain-language list of supported interactions when applicable.
 *
 * @param id Unused; kept for API compatibility.
 */
std::string StateStructure::generateStateDescription(int id) {
    (void)id;
    if (!_stateDescription.empty()) {
        return _stateDescription;
    }
    int actionId = 0;
    this->tabCount = 0;
    this->_stateDescription = "";
    if (!_rootElement) {
        return _stateDescription;
    }
    int depth = 0;
    int count = 0;
    generateElementDescription(_rootElement, actionId, depth, count);
    return this->_stateDescription;
}

/**
 * Depth-first append of one element line and recursion into children.
 * Caps traversal at depth 25 and 100 nodes to bound output size.
 */
void StateStructure::generateElementDescription(const ElementPtr target, int &actionId, int depth,
                                                int &count) {
    if (!target || depth >= 25 || count >= 100) {
        return;
    }
    count++;
    tabCount = depth;
    addTab();
    std::ostringstream line;
    line << target->getClassname();
    if (!target->getResourceID().empty()) {
        line << " id=" << target->getResourceID();
    }
    const std::string textual = !target->getText().empty()
        ? target->getText()
        : target->getContentDesc();
    if (!textual.empty()) {
        line << " text=\"" << textual << "\"";
    }
    line << " sid=" << target->getStableElementId();
    const std::string actionList = generateActionList(target);
    if (!actionList.empty()) {
        line << actionList;
    }
    _stateDescription.append(line.str()).append("\n");
    for (const auto &child : target->getChildren()) {
        generateElementDescription(child, actionId, depth + 1, count);
    }
}

/** Returns a short phrase listing actionable capabilities (click, long_click, scroll) when any apply. */
std::string StateStructure::generateActionList(const ElementPtr target) {
    if (!target) {
        return "";
    }
    std::vector<std::string> actions;
    if (target->getClickable() || target->getCheckable()) {
        actions.emplace_back("click");
    }
    if (target->getLongClickable()) {
        actions.emplace_back("long_click");
    }
    if (target->getScrollable()) {
        actions.emplace_back("scroll");
    }
    if (actions.empty()) {
        return "";
    }
    std::ostringstream oss;
    oss << " which can ";
    for (size_t i = 0; i < actions.size(); ++i) {
        if (i > 0) {
            oss << " or ";
        }
        oss << actions[i];
    }
    return oss.str();
}
} // namespace fastbotx
