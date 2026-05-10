/*
 * Copyright 2020 Advanced Software Technologies Lab at ETH Zurich, Switzerland
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @authors Tianxiao Gu, Zhao Zhang
 */
 
#include "GUITreeNode.h"
#include "../naming/Namelet.h"

namespace fastbotx {
namespace gui_tree {

    /** Default destructor; child nodes are owned via `children_`. */
    GUITreeNode::~GUITreeNode() = default;

    /** Stores the Namelet currently matched for this node during XPath/naming (may be null). */
    void GUITreeNode::setCurrentNamelet(std::shared_ptr<naming::Namelet> nl) {
        current_namelet_ = std::move(nl);
    }

    /** Returns the active Namelet for this node, or nullptr if none was set. */
    std::shared_ptr<naming::Namelet> GUITreeNode::getCurrentNamelet() const {
        return current_namelet_;
    }

    /**
     * Factory: allocates a node with the given parent weak ref (root passes an empty weak_ptr).
     * Must be used so nodes are always held by `shared_ptr` (`enable_shared_from_this`).
     */
    GUITreeNodePtr GUITreeNode::create(const GUITreeNodeWeakPtr &parent) {
        return GUITreeNodePtr(new GUITreeNode(parent));
    }

    /** Sets `depth_` to parent depth + 1 when a parent exists; otherwise keeps default depth. */
    GUITreeNode::GUITreeNode(GUITreeNodeWeakPtr parent)
        : parent_(std::move(parent)) {
        if (auto p = parent_.lock()) {
            depth_ = p->depth_ + 1;
        }
    }

    /** Links `child` as a direct descendant and rewires its parent weak pointer to this node. */
    void GUITreeNode::appendChild(GUITreeNodePtr child) {
        if (!child) return;
        child->parent_ = GUITreeNodeWeakPtr(shared_from_this());
        children_.push_back(std::move(child));
    }

    /** True when the widget class name is the standard Android WebView type. */
    bool GUITreeNode::isWebView() const {
        return class_name_ == "android.webkit.WebView";
    }

} // namespace gui_tree
} // namespace fastbotx
