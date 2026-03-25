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
 * Coordinates Naming lattice navigation per activity.
 * treeToNaming / refine fixed-point — full logic in NamingFactory.
 */
#ifndef FASTBOTX_DESC_NAMING_STATENAMINGMANAGER_H_
#define FASTBOTX_DESC_NAMING_STATENAMINGMANAGER_H_

#include "ActivityNamingManager.h"
#include "Naming.h"
#include "StateKey.h"

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

        NamingPtr getNamingForActivity(const std::string &activity_key) const;

        /** APE-style update from current(activity) naming to n with relation checks by kind. */
        void updateNaming(const std::string &activity_key, NamingUpdateKind kind, NamingPtr n);
        /** APE-style explicit transition update (old -> new) with strict relation checks. */
        void updateNaming(const std::string &activity_key, NamingUpdateKind kind, const NamingPtr &old_n, NamingPtr new_n);
        /** APE-style state-scoped update edge: source naming + state key -> target naming. */
        void updateNamingWithStateKey(const std::string &activity_key, NamingUpdateKind kind,
                                      const NamingPtr &old_n, NamingPtr new_n, const StateKey &state_key);
        /** Same as updateNamingWithStateKey but keyed by precomputed StateKey hash. */
        void updateNamingWithStateHash(const std::string &activity_key, NamingUpdateKind kind,
                                       const NamingPtr &old_n, NamingPtr new_n, uintptr_t state_key_hash);

        /** True if to is a direct refinement child of from (edge in from's refinement map). */
        bool namingToEdge(const NamingPtr &from, const NamingPtr &to, NamingEdge *out_edge) const;

        /** Current naming for the tree's activity, or {@link NamingFactory::defaultRootNaming} if unset. */
        NamingPtr treeToNaming(const gui_tree::GUITree &tree);
        /** APE-style lookup with per-hop rebuild and state-key recomputation. */
        NamingPtr treeToNaming(gui_tree::GUITree &tree, const std::shared_ptr<gui_tree::XPathNodeMapper> &dom);
        /** State-hash scoped lookup from a source naming (APE namingToEdge-style). */
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

    private:
        struct StateEdgeEntry {
            StateKey state_key;
            NamingPtr target;
        };
        using StateEdgeBucket = std::vector<StateEdgeEntry>;
        using StateEdgeMap = std::map<uintptr_t, StateEdgeBucket>;
        std::map<NamingPtr, StateEdgeMap, std::owner_less<NamingPtr>> naming_to_edge_;
        // Compatibility path for callers that only provide hash (no full StateKey available).
        std::map<NamingPtr, std::map<uintptr_t, NamingPtr>, std::owner_less<NamingPtr>> naming_to_edge_hash_only_;
        EdgeLookupStats lookup_stats_{};
        std::shared_ptr<ActivityNamingManager> activity_mgr_;
    };

} // namespace naming
} // namespace fastbotx

#endif
