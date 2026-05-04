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
 * Naming: ordered Namelets + refinement child map.
 * extend / replaceLast — lattice ops; factory helpers on NamingFactory.
 */
#ifndef FASTBOTX_DESC_NAMING_NAMING_H_
#define FASTBOTX_DESC_NAMING_NAMING_H_

#include "Namelet.h"
#include "../gui_tree/GUITreeNode.h"

#include <atomic>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fastbotx {
namespace gui_tree {
    class GUITree;
    class XPathNodeMapper;
}
namespace naming {

    static bool nameletLess(const std::shared_ptr<Namelet> &a, const std::shared_ptr<Namelet> &b) {
            if (!a || !b) {
                return a.get() < b.get();
            }
            return *a < *b;
    }

    struct NamingEdge {
        std::shared_ptr<Namelet> from;
        std::shared_ptr<Namelet> to;

        bool operator<(const NamingEdge &o) const {
            if (nameletLess(from, o.from)) return true;
            if (nameletLess(o.from, from)) return false;
            return nameletLess(to, o.to);
        }
    };

    class Naming : public std::enable_shared_from_this<Naming> {
    public:
        /** Result of evaluating a Naming on a GUITree (Java inner class NamingResult). */
        struct NamingResult {
            std::vector<NamePtr> names;
            std::vector<std::vector<gui_tree::GUITreeNodePtr>> node_groups;
            std::vector<std::vector<NameletPtr>> namelet_groups;

            size_t getNameSize() const { return names.size(); }

            size_t getNodeSize() const;

            /** Propagate Name / Namelet to nodes (Java NamingResult.updateNames). */
            void updateNames();
        };

        /** Naming.namingInternal equivalent on a concrete tree+dom. */
        NamingResult namingInternal(gui_tree::GUITree &tree,
                                    const std::shared_ptr<gui_tree::XPathNodeMapper> &dom) const;

        explicit Naming(std::vector<std::shared_ptr<Namelet>> namelets);

        /** Child Naming in the refinement lattice (Java: children map). */
        static std::shared_ptr<Naming> createChild(std::shared_ptr<Naming> parent,
                                                   std::vector<std::shared_ptr<Namelet>> namelets);

        std::shared_ptr<Naming> getParent() const { return parent_.lock(); }

        size_t size() const { return namelets_.size(); }

        const std::vector<std::shared_ptr<Namelet>> &getNamelets() const { return namelets_; }

        /** Last namelet in this naming sequence (Java Naming.getLastNamelet). */
        std::shared_ptr<Namelet> getLastNamelet() const;

        /**
         * Java Naming.isReplaceable: must be REFINE and equal to the last namelet.
         */
        bool isReplaceable(const std::shared_ptr<Namelet> &namelet) const;

        /** True when this naming has no refinement lattice children (Java Naming.hasChild / NamingManager.isLeaf). */
        bool hasChild() const { return children_.empty(); }

        int getFineness() const { return fineness_; }

        const std::string &getNamingName() const { return naming_name_; }

        const std::map<NamingEdge, std::shared_ptr<Naming>> &getRefinementChildren() const { return children_; }

        void addRefinementChild(const NamingEdge &edge, std::shared_ptr<Naming> child);

        std::shared_ptr<Naming> getRefinementChild(const std::shared_ptr<Namelet> &from,
                                                   const std::shared_ptr<Namelet> &to) const;

        /** Stable fingerprint for StateKey / lattice (same serialization as StateKey::fromParts). */
        const std::string &fingerprintString() const;

        /** drop cached NamingResult for this GUITree. */
        void releaseTreeCache(const gui_tree::GUITree &tree) const;

        virtual ~Naming() = default;

    private:
        Naming(std::shared_ptr<Naming> parent, std::vector<std::shared_ptr<Namelet>> namelets);

        static std::atomic<int> naming_counter_;

        std::string naming_name_;
        std::weak_ptr<Naming> parent_{};
        std::vector<std::shared_ptr<Namelet>> namelets_{};
        int fineness_{-1};

        std::map<NamingEdge, std::shared_ptr<Naming>> children_{};
        std::string fingerprint_cached_{};
        // Cache NamingResult by stable GUITree instance id (not pointer address).
        mutable std::mutex naming_cache_mu_;
        mutable std::unordered_map<int, NamingResult> tree_to_naming_result_;
    };

    using NamingPtr = std::shared_ptr<Naming>;

} // namespace naming
} // namespace fastbotx

#endif
