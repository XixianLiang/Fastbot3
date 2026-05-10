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


#include "GUITree.h"

#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace fastbotx {
namespace gui_tree {

    namespace {
        struct NamingGroupEntry {
            naming::NamePtr name;
            std::string xpathKey;
            const void *ptrKey{nullptr};
            std::vector<GUITreeNodePtr> nodes;
        };
    } // namespace

    static std::atomic<int> g_tree_id{0};

    /** Returns the next monotonically increasing tree id (thread-safe). */
    int GUITree::nextId() {
        return g_tree_id.fetch_add(1, std::memory_order_relaxed);
    }

    /** Constructs a GUI tree with root node and owning activity package/class names. */
    GUITree::GUITree(GUITreeNodePtr root, std::string activity_package, std::string activity_class)
        : id_(nextId()),
          root_(std::move(root)),
          activity_package_(std::move(activity_package)),
          activity_class_(std::move(activity_class)) {}

    /** Returns whether any node in the current naming groups reported focused state at last rebuild. */
    bool GUITree::hasFocusedNode() const {
        return has_focused_node_;
    }

    /**
     * Replaces parallel name and node-group vectors: sorts by XPath key, refreshes caches,
     * and recomputes whether any node is focused.
     */
    void GUITree::rebuild(std::vector<naming::NamePtr> names, std::vector<std::vector<GUITreeNodePtr>> node_groups) {
        if (names.size() != node_groups.size()) {
            throw std::invalid_argument("GUITree::rebuild: names and node_groups size mismatch");
        }
        std::vector<NamingGroupEntry> entries;
        entries.reserve(names.size());
        for (size_t i = 0; i < names.size(); ++i) {
            NamingGroupEntry e;
            e.name = std::move(names[i]);
            e.nodes = std::move(node_groups[i]);
            e.ptrKey = e.name.get();
            if (e.name) {
                // Precompute XPath string once to avoid repeated allocations in sort comparisons.
                e.xpathKey = e.name->toXPath();
            }
            entries.emplace_back(std::move(e));
        }
        std::sort(entries.begin(), entries.end(),
                  [](const NamingGroupEntry &a, const NamingGroupEntry &b) {
                      if (!a.name || !b.name) {
                          return a.ptrKey < b.ptrKey;
                      }
                      return a.xpathKey < b.xpathKey;
                  });
        current_names_.clear();
        current_node_groups_.clear();
        current_xpaths_.clear();
        current_names_.reserve(entries.size());
        current_node_groups_.reserve(entries.size());
        current_xpaths_.reserve(entries.size());
        has_focused_node_ = false;
        for (auto &e : entries) {
            current_names_.push_back(std::move(e.name));
            current_xpaths_.push_back(std::move(e.xpathKey));
            // Update focus cache while we still have the nodes in hand.
            bool anyFocused = false;
            for (const auto &n : e.nodes) {
                if (n && n->isFocused()) {
                    anyFocused = true;
                    break;
                }
            }
            has_focused_node_ = has_focused_node_ || anyFocused;
            current_node_groups_.push_back(std::move(e.nodes));
        }
    }

    /** Stores the naming context and rebuilds sorted names, XPath strings, and node groups. */
    void GUITree::setCurrentNaming(naming::NamingPtr naming,
                                   std::vector<naming::NamePtr> names,
                                   std::vector<std::vector<GUITreeNodePtr>> node_groups) {
        current_naming_ = std::move(naming);
        rebuild(std::move(names), std::move(node_groups));
    }

    namespace {

        /** Deep-clones a subtree under parentWeak; omap maps source pointers to cloned nodes for remap. */
        GUITreeNodePtr cloneSubtree(const GUITreeNode *src,
                                    std::unordered_map<const GUITreeNode *, GUITreeNodePtr> &omap,
                                    const GUITreeNodeWeakPtr &parentWeak) {
            if (!src) {
                return nullptr;
            }
            GUITreeNodePtr n = GUITreeNode::create(parentWeak);
            omap[src] = n;
            n->setIndex(src->getIndex());
            n->setDescendantCount(src->getDescendantCount());
            n->setHeight(src->getHeight());
            n->setResourceId(src->getResourceId());
            n->setClassName(src->getClassName());
            n->setPackageName(src->getPackageName());
            n->setText(std::string(src->getText()));
            n->setContentDesc(std::string(src->getContentDesc()));
            n->setEnabled(src->isEnabled());
            n->setClickable(src->isClickable());
            n->setLongClickable(src->isLongClickable());
            n->setCheckable(src->isCheckable());
            n->setChecked(src->isChecked());
            n->setFocusable(src->isFocusable());
            n->setScrollable(src->getScrollable());
            n->setPassword(src->isPassword());
            n->setFocused(src->isFocused());
            n->setBounds(src->getBounds());
            naming::NamePtr xn = src->getXPathName();
            if (xn) {
                n->setXPathName(xn);
            }
            std::shared_ptr<naming::Namelet> nl = src->getCurrentNamelet();
            if (nl) {
                n->setCurrentNamelet(nl);
            }
            for (const auto &ch : src->getChildren()) {
                GUITreeNodePtr c = cloneSubtree(ch.get(), omap, GUITreeNodeWeakPtr(n));
                if (c) {
                    n->appendChild(std::move(c));
                }
            }
            return n;
        }

    } // namespace

    /** Deep-copies the tree, timestamp, and naming; node groups point into the cloned nodes via omap. */
    GUITreePtr GUITree::cloneDeep(const GUITree &src) {
        const GUITreeNode *root = src.getRootNode();
        if (!root) {
            return nullptr;
        }
        std::unordered_map<const GUITreeNode *, GUITreeNodePtr> omap;
        GUITreeNodePtr newRoot = cloneSubtree(root, omap, GUITreeNodeWeakPtr());
        GUITreePtr out = std::make_shared<GUITree>(
            std::move(newRoot), src.getActivityPackageName(), src.getActivityClassName());
        out->setTimestamp(src.getTimestamp());
        naming::NamingPtr naming = src.getCurrentNaming();
        std::vector<naming::NamePtr> names = src.getCurrentNames();
        const auto &ogr = src.getCurrentNodeGroups();
        std::vector<std::vector<GUITreeNodePtr>> ngroups;
        ngroups.reserve(ogr.size());
        for (const auto &g : ogr) {
            std::vector<GUITreeNodePtr> ng;
            ng.reserve(g.size());
            for (const auto &node : g) {
                if (!node) {
                    ng.push_back(nullptr);
                    continue;
                }
                auto it = omap.find(node.get());
                if (it != omap.end()) {
                    ng.push_back(it->second);
                } else {
                    ng.push_back(nullptr);
                }
            }
            ngroups.push_back(std::move(ng));
        }
        out->setCurrentNaming(std::move(naming), std::move(names), std::move(ngroups));
        return out;
    }

    /** Ensures each non-null node’s XPath name matches the parallel Name entry; throws on mismatch. */
    void GUITree::validate() const {
        const size_t n = current_names_.size();
        if (current_node_groups_.size() != n) {
            throw std::runtime_error("GUITree::validate: names / node_groups size mismatch");
        }
        for (size_t i = 0; i < n; ++i) {
            const naming::NamePtr &w = current_names_[i];
            if (!w) {
                continue;
            }
            const auto &group = current_node_groups_[i];
            for (const auto &node : group) {
                if (!node) continue;
                naming::NamePtr xn = node->getXPathName();
                // Fast path: same shared_ptr instance is guaranteed equal.
                if (!xn || (xn.get() != w.get() && !(*xn == *w))) {
                    throw std::runtime_error("GUITree::validate: mismatched node and name");
                }
            }
        }
    }

} // namespace gui_tree
} // namespace fastbotx
