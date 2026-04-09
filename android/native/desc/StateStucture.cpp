/**
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 */

#include "StateStructure.h"
#include <algorithm>
#include <sstream>

namespace fastbotx {

const ElementPtr StateStructure::getFirst() {
    // Initialize stack
    // Loop through elements
    while (!this->_stack.empty()) {
        this->_stack.pop();
    }
    // Depth-first traversal: add all child nodes to the stack
    for (ElementPtr child : this->_rootElement->getChildren()) {
        this->_stack.push(child);
    }
    return this->_rootElement;
}

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

void StateStructure::addChildElement(ElementPtr child) {
    if (!_rootElement || !child) {
        return;
    }
    _rootElement->reAddChild(child);
}

const ElementPtr StateStructure::findElement(const uintptr_t target) {
    auto result = this->_elementMap.find(target);
    if (result != this->_elementMap.end()) {
        return result->second;
    } else {
        return nullptr;
    }
}

void StateStructure::addTab() {
    for (int i = 0; i < this->tabCount; i++) {
        this->_stateDescription.append("\t");
    }
}

bool StateStructure::shouldMerge(const ElementPtr father, const ElementPtr child) {
    (void)father;
    (void)child;
    return false;
}

bool StateStructure::insertElement(ElementPtr element) {
    auto res = this->_elements.insert(element);
    return res.second;
}

ElementPtr StateStructure::findElementById(int id) {
    auto found = std::find_if(_elements.begin(), _elements.end(), [id](const ElementPtr &ptr) {
        return ptr && ptr->getStableElementId() == id;
    });
    return (found != _elements.end()) ? *found : nullptr;
}

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

void StateStructure::generateElementDescription(const ElementPtr target, int &actionId, int depth,
                                                int &count) {
    (void)actionId;
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
    if (!target->getText().empty()) {
        line << " text=\"" << target->getText() << "\"";
    }
    _stateDescription.append(line.str()).append("\n");
    for (const auto &child : target->getChildren()) {
        generateElementDescription(child, actionId, depth + 1, count);
    }
}

std::string StateStructure::generateActionList(const ElementPtr target) {
    (void)target;
    return "\n";
}
} // namespace fastbotx
