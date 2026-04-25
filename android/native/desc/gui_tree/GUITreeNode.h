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
#ifndef FASTBOTX_DESC_GUI_TREE_GUITREENODE_H_
#define FASTBOTX_DESC_GUI_TREE_GUITREENODE_H_

#include "../ApeStringCache.h"
#include "../naming/Name.h"
#include "../../Base.h"

#include <memory>
#include <string>
#include <vector>

namespace fastbotx {
namespace naming {
    class Namelet;
}
namespace gui_tree {

    class GUITreeNode;
    using GUITreeNodePtr = std::shared_ptr<GUITreeNode>;
    using GUITreeNodeWeakPtr = std::weak_ptr<GUITreeNode>;

    class GUITreeNode : public std::enable_shared_from_this<GUITreeNode> {
    public:
        static GUITreeNodePtr create(const GUITreeNodeWeakPtr &parent);

        ~GUITreeNode();

        GUITreeNode(const GUITreeNode &) = delete;
        GUITreeNode &operator=(const GUITreeNode &) = delete;

        GUITreeNodePtr getParent() const { return parent_.lock(); }

        const std::vector<GUITreeNodePtr> &getChildren() const { return children_; }

        /** Append child (Java addChild — order preserved). */
        void appendChild(GUITreeNodePtr child);

        int getIndex() const { return index_; }
        void setIndex(int v) { index_ = v; }

        int getDepth() const { return depth_; }
        int getDescendantCount() const { return descendant_count_; }
        void setDescendantCount(int v) { descendant_count_ = v; }

        int getHeight() const { return height_; }
        void setHeight(int v) { height_ = v; }

        const std::string &getResourceId() const { return resource_id_; }
        void setResourceId(std::string v) { resource_id_ = std::move(v); }

        const std::string &getClassName() const { return class_name_; }
        void setClassName(std::string v) { class_name_ = std::move(v); }

        const std::string &getPackageName() const { return package_name_; }
        void setPackageName(std::string v) { package_name_ = std::move(v); }

        const std::string &getText() const {
            return text_ ? *text_ : ApeStringCache::empty();
        }
        void setText(std::string v) {
            text_ = &ApeStringCache::cacheStringEmptyOnNull(std::move(v), true);
        }

        const std::string &getContentDesc() const {
            return content_desc_ ? *content_desc_ : ApeStringCache::empty();
        }
        void setContentDesc(std::string v) {
            content_desc_ = &ApeStringCache::cacheStringEmptyOnNull(std::move(v), true);
        }

        bool isEnabled() const { return enabled_; }
        void setEnabled(bool v) { enabled_ = v; }

        bool isClickable() const { return clickable_; }
        void setClickable(bool v) { clickable_ = v; }
        bool isLongClickable() const { return long_clickable_; }
        void setLongClickable(bool v) { long_clickable_ = v; }
        bool isCheckable() const { return checkable_; }
        void setCheckable(bool v) { checkable_ = v; }
        bool isChecked() const { return checked_; }
        void setChecked(bool v) { checked_ = v; }
        bool isFocusable() const { return focusable_; }
        void setFocusable(bool v) { focusable_ = v; }
        int getScrollable() const { return scrollable_; }
        void setScrollable(int v) { scrollable_ = v; }
        bool isPassword() const { return password_; }
        void setPassword(bool v) { password_ = v; }
        bool isFocused() const { return focused_; }
        void setFocused(bool v) { focused_ = v; }

        const Rect &getBounds() const { return bounds_; }
        void setBounds(const Rect &r) { bounds_ = r; }

        naming::NamePtr getXPathName() const { return xpath_name_; }
        void setXPathName(naming::NamePtr n) { xpath_name_ = std::move(n); }

        void setCurrentNamelet(std::shared_ptr<naming::Namelet> nl);
        std::shared_ptr<naming::Namelet> getCurrentNamelet() const;

        bool isWebView() const;

    private:
        explicit GUITreeNode(GUITreeNodeWeakPtr parent);

        GUITreeNodeWeakPtr parent_{};
        std::vector<GUITreeNodePtr> children_{};

        std::string resource_id_;
        std::string class_name_;
        std::string package_name_;
        const std::string *text_{nullptr};
        const std::string *content_desc_{nullptr};

        Rect bounds_{};

        bool enabled_{false};
        bool checked_{false};
        bool checkable_{false};
        bool clickable_{false};
        bool focusable_{false};
        int scrollable_{0};
        bool long_clickable_{false};
        bool password_{false};
        bool focused_{false};

        int index_{0};
        int depth_{1};
        int descendant_count_{1};
        int height_{1};

        naming::NamePtr xpath_name_{nullptr};
        std::shared_ptr<naming::Namelet> current_namelet_{nullptr};
    };

} // namespace gui_tree
} // namespace fastbotx

#endif
