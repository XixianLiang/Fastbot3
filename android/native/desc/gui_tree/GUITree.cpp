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

    int GUITree::nextId() {
        return g_tree_id.fetch_add(1, std::memory_order_relaxed);
    }

    GUITree::GUITree(GUITreeNodePtr root, std::string activity_package, std::string activity_class)
        : id_(nextId()),
          root_(std::move(root)),
          activity_package_(std::move(activity_package)),
          activity_class_(std::move(activity_class)) {}

    bool GUITree::hasFocusedNode() const {
        return has_focused_node_;
    }

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

    void GUITree::setCurrentNaming(naming::NamingPtr naming,
                                   std::vector<naming::NamePtr> names,
                                   std::vector<std::vector<GUITreeNodePtr>> node_groups) {
        current_naming_ = std::move(naming);
        rebuild(std::move(names), std::move(node_groups));
    }

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
