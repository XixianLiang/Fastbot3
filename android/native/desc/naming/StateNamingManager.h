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
 * Per-activity `Naming` selection and refinement graph edges keyed by `StateKey` / state hash.
 * Bridges `ActivityNamingManager`, lattice refinements (`NamingFactory`), and tree walks for fixed points.
 */
#ifndef FASTBOTX_DESC_NAMING_STATENAMINGMANAGER_H_
#define FASTBOTX_DESC_NAMING_STATENAMINGMANAGER_H_

#include "ActivityNamingManager.h"
#include "Naming.h"
#include "StateKey.h"

#include <cstdint>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fastbotx {
namespace gui_tree {
    class GUITree;
    class XPathNodeMapper;
}
namespace naming {

    enum class NamingUpdateKind : unsigned char { Refine, Abstract };

    /** Counters for `updateNaming*` paths that bail out (structural checks, state-key guards). */
    struct StateNamingUpdateRejectDiagStats {
        uint64_t update_reject_null_new{0};
        uint64_t update_refine_not_direct_child{0};
        uint64_t update_refine_infer_edge_failed{0};
        uint64_t update_abstract_sibling_no_parent{0};
        uint64_t update_abstract_sibling_naming_to_edge_failed{0};
        uint64_t update_abstract_sibling_infer_edge_failed{0};
        /** Abstract sibling replace rejected: old/new not both leaves in the stored refinement graph. */
        uint64_t update_abstract_sibling_not_leaf{0};
        uint64_t update_abstract_relation_rejected{0};
        uint64_t statekey_not_applied_after_update{0};
        uint64_t statekey_abstract_leaf_blocked{0};
        uint64_t statehash_not_applied_after_update{0};
        uint64_t statehash_abstract_leaf_blocked{0};
        uint64_t statekey_skipped_null_arg{0};
        uint64_t statehash_skipped_null_arg{0};

        bool any() const {
            return update_reject_null_new || update_refine_not_direct_child ||
                   update_refine_infer_edge_failed || update_abstract_sibling_no_parent ||
                   update_abstract_sibling_naming_to_edge_failed ||
                   update_abstract_sibling_infer_edge_failed || update_abstract_sibling_not_leaf ||
                   update_abstract_relation_rejected ||
                   statekey_not_applied_after_update || statekey_abstract_leaf_blocked ||
                   statehash_not_applied_after_update || statehash_abstract_leaf_blocked ||
                   statekey_skipped_null_arg || statehash_skipped_null_arg;
        }
    };

    /** Counters for `treeToNaming` / fixed-point failures (rebuild, hash, cycles). */
    struct StateNamingTreeWalkDiagStats {
        uint64_t tree_to_naming_dom_entries{0};
        uint64_t tree_to_naming_rebuild_failed{0};
        uint64_t tree_to_naming_zero_state_hash{0};
        uint64_t tree_to_naming_no_successor{0};
        uint64_t tree_to_naming_cycle_break{0};
        uint64_t tree_to_naming_total_hops{0};
        uint64_t fixed_point_null_after_tree_to_naming{0};
        uint64_t fixed_point_batch_refine_failed{0};

        bool any() const {
            return tree_to_naming_rebuild_failed || tree_to_naming_zero_state_hash ||
                   tree_to_naming_no_successor || tree_to_naming_cycle_break ||
                   fixed_point_null_after_tree_to_naming || fixed_point_batch_refine_failed;
        }
    };

    /**
     * Owns activity-scoped naming roots and maps `(source naming, state key)` to successor namings after
     * refine/abstract updates. Exposes `treeToNaming` walks and bitmask fixed-point refinement.
     */
    class StateNamingManager {
    public:
        struct EdgeLookupStats {
            uint64_t exact_hit{0};
            uint64_t hash_only_hit{0};
            uint64_t miss{0};
        };

        explicit StateNamingManager(std::shared_ptr<ActivityNamingManager> activity_mgr);

        ActivityNamingManager &activityManager();
        const ActivityNamingManager &activityManager() const;

        /** Cached `Naming` root for `activity_key` (may be lazily inserted by `ActivityNamingManager`). */
        NamingPtr getNamingForActivity(const std::string &activity_key) const;

        /** Updates from current activity naming to `n`, dispatching on `kind` (see overload for rules). */
        void updateNaming(const std::string &activity_key, NamingUpdateKind kind, NamingPtr n);
        /** Validates `old_n -> new_n` by kind, registers refinement edges, then stores `new_n` for the activity. */
        void updateNaming(const std::string &activity_key, NamingUpdateKind kind, const NamingPtr &old_n, NamingPtr new_n);
        /** After a successful structural update, records `state_key -> new_n` from `old_n` when applicable. */
        void updateNamingWithStateKey(const std::string &activity_key, NamingUpdateKind kind,
                                      const NamingPtr &old_n, NamingPtr new_n, const StateKey &state_key);
        /** Like `updateNamingWithStateKey` but stores hash-only edges when only the uintptr key is available. */
        void updateNamingWithStateHash(const std::string &activity_key, NamingUpdateKind kind,
                                       const NamingPtr &old_n, NamingPtr new_n, uintptr_t state_key_hash);

        /** True if to is a direct refinement child of from (edge in from's refinement map). */
        bool namingToEdge(const NamingPtr &from, const NamingPtr &to, NamingEdge *out_edge) const;

        /** One-hop resolve: activity naming + current tree `StateKey`; falls back to `NamingFactory::defaultRootNaming`. */
        NamingPtr treeToNaming(const gui_tree::GUITree &tree);
        /** Follows state edges with `rebuildTree` each hop until no successor or cycle (multi-hop). */
        NamingPtr treeToNaming(gui_tree::GUITree &tree, const std::shared_ptr<gui_tree::XPathNodeMapper> &dom);
        /** Looks up successor naming by hash bucket (exact bucket first, then hash-only fallback). */
        NamingPtr getNamingByStateHash(const NamingPtr &source, uintptr_t state_key_hash) const;
        /** State-key exact lookup with hash-bucket + full-key verification. */
        NamingPtr getNamingByStateKey(const NamingPtr &source, const StateKey &state_key) const;

        /**
         * Without dom: returns stored naming (or default root) — no rebuild.
         */
        NamingPtr getNamingFixedPoint(const std::string &activity_key, const gui_tree::GUITree &tree, int max_iter);

        /**
         * Bitmask refinement with rebuild after each step: refineNaming → rebuildTree → compare StateKey::hash();
         * stops when refineNaming has no step, or StateKey unchanged (fixed point), or max_iter steps.
         * Persists result in ActivityNamingManager.
         */
        NamingPtr getNamingFixedPoint(const std::string &activity_key, gui_tree::GUITree &tree,
                                      const std::shared_ptr<gui_tree::XPathNodeMapper> &dom, int max_iter);

        /** Return and reset lookup window stats for exact-vs-fallback hit-rate analysis. */
        EdgeLookupStats consumeEdgeLookupStats();

        void releaseTreeCache(const gui_tree::GUITree &tree);

        /** Global (process-wide) counters; not tied to manager instance. */
        static StateNamingUpdateRejectDiagStats consumeUpdateRejectDiagStats();
        static StateNamingTreeWalkDiagStats consumeTreeWalkDiagStats();

    private:
        struct StateEdgeEntry {
            StateKey state_key;
            NamingPtr target;
        };
        using StateEdgeBucket = std::vector<StateEdgeEntry>;
        using StateEdgeMap = std::map<uintptr_t, StateEdgeBucket>;
        /** Source naming → hash bucket → full `StateKey` entries with successor namings. */
        std::map<NamingPtr, StateEdgeMap, std::owner_less<NamingPtr>> naming_to_edge_;
        /** Fallback edges keyed only by `StateKey::hash()` when callers lack full keys. */
        std::map<NamingPtr, std::map<uintptr_t, NamingPtr>, std::owner_less<NamingPtr>> naming_to_edge_hash_only_;
        EdgeLookupStats lookup_stats_{};
        std::shared_ptr<ActivityNamingManager> activity_mgr_;
        bool isStateGraphLeaf(const NamingPtr &n) const;
    };

} // namespace naming
} // namespace fastbotx

#endif
