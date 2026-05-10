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

#ifndef FASTBOTX_DESC_GUI_TREE_GUITREE_H_
#define FASTBOTX_DESC_GUI_TREE_GUITREE_H_

#include "../naming/Name.h"
#include "../naming/Naming.h"
#include "GUITreeNode.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace fastbotx {
namespace gui_tree {

    class GUITree;
    /** Shared ownership handle for passing GUI trees across layers (e.g. refine, snapshot). */
    using GUITreePtr = std::shared_ptr<GUITree>;

    /**
     * Snapshot of an accessibility-style widget tree for one Activity, plus parallel naming data:
     * XPath names, sorted cached strings, and node groups aligned by index.
     */
    class GUITree {
    public:
        /** Builds a tree rooted at `root` and records the owning Activity package and class name. */
        explicit GUITree(GUITreeNodePtr root, std::string activity_package, std::string activity_class);

        ~GUITree() = default;

        /** Non-copyable; use cloneDeep for an independent duplicate. */
        GUITree(const GUITree &) = delete;
        /** Non-copy-assignable. */
        GUITree &operator=(const GUITree &) = delete;

        /** Unique id assigned at construction (monotonic via nextId). */
        int getId() const { return id_; }

        /** Arbitrary capture time or version stamp set by the caller. */
        int getTimestamp() const { return timestamp_; }
        void setTimestamp(int t) { timestamp_ = t; }

        /** Non-owning pointer to the root widget node. */
        GUITreeNode *getRootNode() { return root_.get(); }
        /** Non-owning pointer to the root widget node. */
        const GUITreeNode *getRootNode() const { return root_.get(); }
        /** Shared owner for DOM-bridge registration and lifetime tied to the tree. */
        const GUITreeNodePtr &getRootNodePtr() const { return root_; }

        /** Android package name of the Activity that owns this snapshot. */
        const std::string &getActivityPackageName() const { return activity_package_; }
        /** Fully qualified Activity class name for this snapshot. */
        const std::string &getActivityClassName() const { return activity_class_; }

        /** Current naming policy object (may be null). */
        naming::NamingPtr getCurrentNaming() const { return current_naming_; }
        /** Replaces only the naming policy; does not rebuild name/node parallel arrays (use the overload with vectors). */
        void setCurrentNaming(naming::NamingPtr n) { current_naming_ = std::move(n); }

        /** True if any node in the current groups was focused when names/groups were last rebuilt. */
        bool hasFocusedNode() const;

        /**
         * Replaces parallel `names` and `node_groups` (same length), sorts by XPath for stable ordering,
         * refreshes cached XPath strings and focus summary. Mirrors Java rebuild / setCurrentNaming flow.
         */
        void rebuild(std::vector<naming::NamePtr> names, std::vector<std::vector<GUITreeNodePtr>> node_groups);

        /**
         * Sets naming policy and replaces names plus groups in one step (same length as `names`);
         * internally calls rebuild. Names and groups stay index-aligned after sorting.
         */
        void setCurrentNaming(naming::NamingPtr naming,
                              std::vector<naming::NamePtr> names,
                              std::vector<std::vector<GUITreeNodePtr>> node_groups);

        /** Sorted XPath name objects, parallel to getCurrentNodeGroups() and getCurrentXPaths(). */
        const std::vector<naming::NamePtr> &getCurrentNames() const { return current_names_; }

        /** For each name index, the widget nodes that share that XPath name (may be empty). */
        const std::vector<std::vector<GUITreeNodePtr>> &getCurrentNodeGroups() const { return current_node_groups_; }

        /** Precomputed XPath strings in the same order as current_names_ / current_node_groups_. */
        const std::vector<std::string> &getCurrentXPaths() const { return current_xpaths_; }

        /** Throws if any non-null node’s XPath name disagrees with the parallel Name row (Java validate). */
        void validate() const;

        /**
         * Deep-copy node tree plus current naming/name groups.
         * Returned tree is independent — safe to retain across refine/rebuild on another instance.
         */
        static GUITreePtr cloneDeep(const GUITree &src);

    private:
        /** Next global tree id (thread-safe counter). */
        static int nextId();

        /** Unique id for this instance. */
        int id_{0};
        /** Caller-defined snapshot time or sequence. */
        int timestamp_{0};

        /** Root of the widget hierarchy. */
        GUITreeNodePtr root_{};
        /** Owning Activity package name. */
        std::string activity_package_;
        /** Owning Activity class name. */
        std::string activity_class_;

        /** Active naming scheme used when resolving or comparing names. */
        naming::NamingPtr current_naming_{nullptr};
        /** Parallel to current_node_groups_ and current_xpaths_; sorted by XPath key after rebuild. */
        std::vector<naming::NamePtr> current_names_{};
        /** Nodes grouped by shared XPath name; index matches current_names_. */
        std::vector<std::vector<GUITreeNodePtr>> current_node_groups_{};
        /** Cached `toXPath()` strings for ordering and fast lookup. */
        std::vector<std::string> current_xpaths_{};
        /** Whether rebuild observed any focused node in the groups above. */
        bool has_focused_node_{false};
    };

} // namespace gui_tree
} // namespace fastbotx

#endif
