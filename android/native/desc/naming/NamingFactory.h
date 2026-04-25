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
 * Evaluate Naming on GUITree via XPath (XPathNodeMapper) + Namer.
 * refine / batchAbstract / blacklist — extended in later passes.
 */
#ifndef FASTBOTX_DESC_NAMING_NAMINGFACTORY_H_
#define FASTBOTX_DESC_NAMING_NAMINGFACTORY_H_

#include "Naming.h"
#include "NamerLattice.h"
#include "../xpath/XPathNodeMapper.h"

#include <cstdint>

#include <functional>
#include <memory>
#include <set>

namespace fastbotx {
namespace gui_tree {
    class GUITree;
}
namespace naming {

    enum class ApeBaseNamingMode : unsigned char {
        ActionType = 0,
        TypeOnly = 1,
        ResourceID = 2,
        ParentIndex = 3,
        Boosted = 4,
        Stoat = 5,
    };

    /** Counters for review items 1.1–1.2: default ActionType XPath gaps + evaluateNaming failures. */
    struct NamingEvaluateDiagStats {
        uint64_t fail_node_without_namelet{0};
        /** Subset of the above when naming matches ActionType default-root shape (two partition XPaths). */
        uint64_t fail_actiontype_default_orphan_node{0};
        /** Non-ignored node lacks selected namelet / namer in output phase. */
        uint64_t fail_select_namelet_for_output{0};
        uint64_t select_namelet_empty_candidates{0};
        uint64_t select_namelet_single_non_base{0};
        /** rebuildTree: resolveNonDeterminism returned true. */
        uint64_t rebuild_resolve_nondeterminism{0};
        /** Subset: evaluateNaming aborted early (single nullptr sentinel in names). */
        uint64_t rebuild_after_evaluate_sentinel_null{0};

        bool any() const {
            return fail_node_without_namelet || fail_actiontype_default_orphan_node ||
                   fail_select_namelet_for_output || select_namelet_empty_candidates ||
                   select_namelet_single_non_base || rebuild_resolve_nondeterminism ||
                   rebuild_after_evaluate_sentinel_null;
        }
    };

    class NamingFactory {
    public:
        struct ActionRefinementOptions {
            int max_steps{1};
            const std::set<std::string> *blacklist{nullptr};
            std::function<bool(const NamingPtr &)> accept_predicate{};
            bool choose_deepest_acceptable{false};
            bool evaluate_all_immediate_candidates{false};
        };

        /**
         * For each Namelet in order: XPath on dom → nodes not yet assigned; namer produces Name.
         * Groups by Name::toXPath(); sorts groups like GUITree::rebuild.
         */
        static Naming::NamingResult evaluateNaming(const NamingPtr &naming, gui_tree::GUITree &tree,
                                                   const std::shared_ptr<gui_tree::XPathNodeMapper> &dom);

        /** evaluateNaming + setCurrentNaming + sync xpath/namelet on nodes. */
        static bool rebuildTree(const NamingPtr &naming, gui_tree::GUITree &tree,
                                const std::shared_ptr<gui_tree::XPathNodeMapper> &dom);

        /** One Namelet (root XPath for all nodes) with TYPE-only BitmaskNamer (coarsest non-empty namer in the cube). */
        static NamingPtr defaultRootNaming();
        static NamingPtr getBaseNaming();
        static void setDefaultRootNamingMode(ApeBaseNamingMode mode);
        static ApeBaseNamingMode getDefaultRootNamingMode();

        /**
         * Append one REFINE namelet under namelet {@code nameletIndex}
         * with namer {@code finer} (same XPath as that namelet).
         */
        static NamingPtr latticeRefinementChildAtNamelet(const NamingPtr &base, size_t nameletIndex,
                                                         const NamerPtr &finer);

        /**
         * Java Naming.extend(parentNamelet, newNamelet): append a REFINE namelet with
         * {@code widgetXPathExpr} and {@code finer} under the existing lattice namelet at
         * {@code parentNameletIndex}.
         */
        static NamingPtr extendUnderNamelet(const NamingPtr &base, size_t parentNameletIndex,
                                            const std::string &widgetXPathExpr, const NamerPtr &finer);

        /**
         * Java Naming.replaceLast(replaced, namelet): parentNaming.extend(replaced.getParent(), newNamelet)
         * with same XPath expr as {@code replaced} and finer namer.
         */
        static NamingPtr replaceLast(const NamingPtr &naming, const NameletPtr &replaced, const NamerPtr &finer);

        /** First applicable immediate refinement on the bitmask lattice (one Namelet step). */
        static NamingPtr refineNaming(const NamingPtr &naming, const NamerLattice &lattice);

        /** First applicable immediate abstraction on the bitmask lattice (one Namelet step). */
        static NamingPtr abstractNaming(const NamingPtr &naming, const NamerLattice &lattice);

        /** Repeated refineNaming (greedy) until no finer step or max_steps. */
        static NamingPtr batchRefine(const NamingPtr &naming, const NamerLattice &lattice, int max_steps);

        /**
         * Observable fixed point on a concrete tree: rebuild current naming, then for each refine step:
         * refineNaming -> rebuildTree -> compare StateKey::hash(). Stops when hash no longer changes,
         * no finer step exists, or max_steps reached. Returns nullptr if any rebuild fails.
         */
        static NamingPtr batchRefineWithRebuildFixedPoint(const NamingPtr &naming, const NamerLattice &lattice,
                                                          gui_tree::GUITree &tree,
                                                          const std::shared_ptr<gui_tree::XPathNodeMapper> &dom,
                                                          int max_steps);

        /** Repeated abstractNaming (greedy) until no coarser step or max_steps. */
        static NamingPtr batchAbstract(const NamingPtr &naming, const NamerLattice &lattice, int max_steps);

        /**
         * Greedy refine hops along the bitmask lattice.
         * Implemented via actionRefinementWithOptions with default acceptance.
         */
        static NamingPtr actionRefinement(const NamingPtr &naming, const NamerLattice &lattice, int max_steps);

        /**
         * Like actionRefinement but skips blacklisted fingerprints and returns the first non-blacklisted
         * refinement reachable within max_steps (or nullptr if none).
         */
        static NamingPtr actionRefinementWithBlacklist(const NamingPtr &naming, const NamerLattice &lattice,
                                                       int max_steps, const std::set<std::string> &blacklist);

        /** Action refinement with pluggable acceptance predicate / blacklist skipping. */
        static NamingPtr actionRefinementWithOptions(const NamingPtr &naming, const NamerLattice &lattice,
                                                     const ActionRefinementOptions &options);

        /**
         * Enumerate refinement candidates within max_steps using the same blacklist/predicate options.
         * Used by Model-level "resolve -> filter -> pick best" flow.
         */
        static std::vector<NamingPtr> actionRefinementCandidatesWithOptions(
            const NamingPtr &naming, const NamerLattice &lattice, const ActionRefinementOptions &options);

        /**
         * Refinement candidates anchored at {@code widget_parent} using {@code widget_xpath_expr} and
         * lattice.sortedAbove (Java widget XPath refinement analogue).
         */
        static std::vector<NamingPtr> widgetXPathRefinementCandidatesWithOptions(
            const NamingPtr &naming, const NamerLattice &lattice, const ActionRefinementOptions &options,
            const NameletPtr &widget_parent, const std::string &widget_xpath_expr);

        /**
         * True when NamingResult is structurally inconsistent (parallel arrays, per-group
         * namelet/node count mismatch, or the same GUI node in two groups). Does not mutate.
         * evaluateNaming normally produces a consistent result; this guards rebuildTree.
         */
        static bool resolveNonDeterminism(Naming::NamingResult &result);

        /** Atomically read and zero naming-evaluate / rebuild diagnostics (see NamingEvaluateDiagStats). */
        static NamingEvaluateDiagStats consumeNamingEvaluateDiagStats();
    };

} // namespace naming
} // namespace fastbotx

#endif
