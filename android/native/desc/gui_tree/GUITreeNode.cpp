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

    GUITreeNode::~GUITreeNode() = default;

    void GUITreeNode::setCurrentNamelet(std::shared_ptr<naming::Namelet> nl) {
        current_namelet_ = std::move(nl);
    }

    std::shared_ptr<naming::Namelet> GUITreeNode::getCurrentNamelet() const {
        return current_namelet_;
    }

    GUITreeNodePtr GUITreeNode::create(const GUITreeNodeWeakPtr &parent) {
        return GUITreeNodePtr(new GUITreeNode(parent));
    }

    GUITreeNode::GUITreeNode(GUITreeNodeWeakPtr parent)
        : parent_(std::move(parent)) {
        if (auto p = parent_.lock()) {
            depth_ = p->depth_ + 1;
        }
    }

    void GUITreeNode::appendChild(GUITreeNodePtr child) {
        if (!child) return;
        child->parent_ = GUITreeNodeWeakPtr(shared_from_this());
        children_.push_back(std::move(child));
    }

    bool GUITreeNode::isWebView() const {
        return class_name_ == "android.webkit.WebView";
    }

} // namespace gui_tree
} // namespace fastbotx
