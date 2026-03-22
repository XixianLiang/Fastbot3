/*
 * Evaluate Naming on GUITree via XPath (GUITreeDomBridge) + Namer (Java NamingFactory core).
 * refine / batchAbstract / blacklist — extended in later passes.
 */
#ifndef FASTBOTX_DESC_NAMING_NAMINGFACTORY_H_
#define FASTBOTX_DESC_NAMING_NAMINGFACTORY_H_

#include "Naming.h"
#include "NamerLattice.h"
#include "../xpath/GUITreeDomBridge.h"

#include <functional>
#include <memory>
#include <set>

namespace fastbotx {
namespace gui_tree {
    class GUITree;
}
namespace naming {

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
                                                   const std::shared_ptr<gui_tree::GUITreeDomBridge> &dom);

        /** evaluateNaming + setCurrentNaming + sync xpath/namelet on nodes. */
        static bool rebuildTree(const NamingPtr &naming, gui_tree::GUITree &tree,
                                const std::shared_ptr<gui_tree::GUITreeDomBridge> &dom);

        /** One Namelet (root XPath for all nodes) with TYPE-only BitmaskNamer (coarsest non-empty namer in the cube). */
        static NamingPtr defaultRootNaming();

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
                                                          const std::shared_ptr<gui_tree::GUITreeDomBridge> &dom,
                                                          int max_steps);

        /** Repeated abstractNaming (greedy) until no coarser step or max_steps. */
        static NamingPtr batchAbstract(const NamingPtr &naming, const NamerLattice &lattice, int max_steps);

        /**
         * Java APE "action refinement" (α) analogue: greedy refine hops along the bitmask lattice.
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
         * True when NamingResult is structurally inconsistent (parallel arrays, per-group
         * namelet/node count mismatch, or the same GUI node in two groups). Does not mutate.
         * evaluateNaming normally produces a consistent result; this guards rebuildTree.
         */
        static bool resolveNonDeterminism(Naming::NamingResult &result);
    };

} // namespace naming
} // namespace fastbotx

#endif
