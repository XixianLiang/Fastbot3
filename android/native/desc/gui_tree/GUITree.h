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
 /*
 * APE GUITree — root + activity + current Naming + parallel arrays (names ↔ node groups).
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

    class GUITree {
    public:
        explicit GUITree(GUITreeNodePtr root, std::string activity_package, std::string activity_class);

        ~GUITree() = default;

        GUITree(const GUITree &) = delete;
        GUITree &operator=(const GUITree &) = delete;

        int getId() const { return id_; }

        int getTimestamp() const { return timestamp_; }
        void setTimestamp(int t) { timestamp_ = t; }

        GUITreeNode *getRootNode() { return root_.get(); }
        const GUITreeNode *getRootNode() const { return root_.get(); }

        const std::string &getActivityPackageName() const { return activity_package_; }
        const std::string &getActivityClassName() const { return activity_class_; }

        naming::NamingPtr getCurrentNaming() const { return current_naming_; }
        void setCurrentNaming(naming::NamingPtr n) { current_naming_ = std::move(n); }

        bool hasFocusedNode() const;

        /** Replace current names + node groups (Java rebuild / setCurrentNaming). */
        void rebuild(std::vector<naming::NamePtr> names, std::vector<std::vector<GUITreeNodePtr>> node_groups);

        /** Sorted names + parallel groups (same length). */
        void setCurrentNaming(naming::NamingPtr naming,
                              std::vector<naming::NamePtr> names,
                              std::vector<std::vector<GUITreeNodePtr>> node_groups);

        const std::vector<naming::NamePtr> &getCurrentNames() const { return current_names_; }

        const std::vector<std::vector<GUITreeNodePtr>> &getCurrentNodeGroups() const { return current_node_groups_; }

        /** Validate that each node's xpath Name matches the parallel Name entry (Java validate). */
        void validate() const;

    private:
        static int nextId();

        int id_{0};
        int timestamp_{0};

        GUITreeNodePtr root_{};
        std::string activity_package_;
        std::string activity_class_;

        naming::NamingPtr current_naming_{nullptr};
        std::vector<naming::NamePtr> current_names_{};
        std::vector<std::vector<GUITreeNodePtr>> current_node_groups_{};
        bool has_focused_node_{false};
    };

    using GUITreePtr = std::shared_ptr<GUITree>;

} // namespace gui_tree
} // namespace fastbotx

#endif
