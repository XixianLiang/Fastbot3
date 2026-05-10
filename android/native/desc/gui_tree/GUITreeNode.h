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
    /** Shared ownership for tree nodes (required for `enable_shared_from_this` and factories). */
    using GUITreeNodePtr = std::shared_ptr<GUITreeNode>;
    /** Parent links without extending lifetime (cycle-safe against strong parent→child edges). */
    using GUITreeNodeWeakPtr = std::weak_ptr<GUITreeNode>;

    /**
     * One widget/view in a snapshot hierarchy: accessibility-like fields, cached stats for planning,
     * and naming hooks (`xpath_name_`, current Namelet). Built by `GUITreeFactory` from XML or Element trees.
     */
    class GUITreeNode : public std::enable_shared_from_this<GUITreeNode> {
    public:
        /** Allocates a node; pass an empty weak_ptr for the tree root (see `.cpp`). */
        static GUITreeNodePtr create(const GUITreeNodeWeakPtr &parent);

        ~GUITreeNode();

        /** Non-copyable; mirror Java-side nodes by reconstruction or factory parse. */
        GUITreeNode(const GUITreeNode &) = delete;
        /** Non-copy-assignable. */
        GUITreeNode &operator=(const GUITreeNode &) = delete;

        /** Parent node, or nullptr when this is the root or the parent has expired. */
        GUITreeNodePtr getParent() const { return parent_.lock(); }

        /** Direct children in DOM/XML traversal order (same as Java `addChild`). */
        const std::vector<GUITreeNodePtr> &getChildren() const { return children_; }

        /** Inserts `child` at the end of `children_` and sets its parent weak ref to this node. */
        void appendChild(GUITreeNodePtr child);

        /** Index among siblings in the dumped hierarchy (uiautomator `index`). */
        int getIndex() const { return index_; }
        void setIndex(int v) { index_ = v; }

        /** Depth from root (root depth is 1). */
        int getDepth() const { return depth_; }
        /** Total nodes in this subtree including self; maintained by post-order passes (e.g. factory). */
        int getDescendantCount() const { return descendant_count_; }
        void setDescendantCount(int v) { descendant_count_ = v; }

        /** Height of subtree in edges (leaf height 1); maintained with descendant_count_. */
        int getHeight() const { return height_; }
        void setHeight(int v) { height_ = v; }

        /** Android `resource-id` string (may be empty). */
        const std::string &getResourceId() const { return resource_id_; }
        void setResourceId(std::string v) { resource_id_ = std::move(v); }

        /** Fully qualified view class name, e.g. `android.widget.Button`. */
        const std::string &getClassName() const { return class_name_; }
        void setClassName(std::string v) { class_name_ = std::move(v); }

        /** Application package owning this view. */
        const std::string &getPackageName() const { return package_name_; }
        void setPackageName(std::string v) { package_name_ = std::move(v); }

        /** Visible text after APE normalization; stored in `ApeStringCache` for deduplication. */
        const std::string &getText() const {
            return text_ ? *text_ : ApeStringCache::empty();
        }
        void setText(std::string v) {
            text_ = &ApeStringCache::cacheStringEmptyOnNull(std::move(v), true);
        }

        /** Content description after APE normalization; cached like `text_`. */
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
        /** Scroll capability bitmask (0–3), same encoding as factory XML parsing. */
        int getScrollable() const { return scrollable_; }
        void setScrollable(int v) { scrollable_ = v; }
        bool isPassword() const { return password_; }
        void setPassword(bool v) { password_ = v; }
        bool isFocused() const { return focused_; }
        void setFocused(bool v) { focused_ = v; }

        /** Screen bounds in pixels (`[l,t][r,b]` space). */
        const Rect &getBounds() const { return bounds_; }
        void setBounds(const Rect &r) { bounds_ = r; }

        /** Resolved XPath `Name` for this widget after naming; shared across siblings with same path. */
        naming::NamePtr getXPathName() const { return xpath_name_; }
        void setXPathName(naming::NamePtr n) { xpath_name_ = std::move(n); }

        /** Namelet selected for this node during the latest naming pass (nullable). */
        void setCurrentNamelet(std::shared_ptr<naming::Namelet> nl);
        std::shared_ptr<naming::Namelet> getCurrentNamelet() const;

        /** True if class name is `android.webkit.WebView`. */
        bool isWebView() const;

    private:
        /** Internal constructor; use `create` so `shared_from_this` is valid for children. */
        explicit GUITreeNode(GUITreeNodeWeakPtr parent);

        /** Weak link upward; strong refs flow root→leaf via `children_`. */
        GUITreeNodeWeakPtr parent_{};
        /** Owned child pointers in insertion order. */
        std::vector<GUITreeNodePtr> children_{};

        std::string resource_id_;
        std::string class_name_;
        std::string package_name_;
        /** Interned pointer into `ApeStringCache` for displayed text. */
        const std::string *text_{nullptr};
        /** Interned pointer into `ApeStringCache` for content description. */
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
