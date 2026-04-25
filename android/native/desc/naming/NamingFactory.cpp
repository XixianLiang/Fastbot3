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

#include "NamingFactory.h"
#include "Namelet.h"
#include "NamerFactory.h"
#include "NamerType.h"
#include "NamingRuntime.h"
#include "StateKey.h"
#include "../gui_tree/GUITree.h"
#include "../../utils.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace fastbotx {
namespace naming {
namespace {
    ApeBaseNamingMode g_default_root_naming_mode = ApeBaseNamingMode::ActionType;
    /** CreateActionTypeBaseNaming interactive partition (explicit true flags). */
    static const char kActionTypeInteractiveExpr[] =
        "//*[@clickable='true' or @long-clickable='true' or @checkable='true' or @scrollable='true']";
    /** ActionType non-interactive partition (strict false flags). */
    static const char kActionTypeNonInteractiveExpr[] =
        "//*[@clickable='false' and @long-clickable='false' and @checkable='false' and "
        "@scrollable='false']";

    std::atomic<uint64_t> g_eval_fail_node_without_namelet{0};
    std::atomic<uint64_t> g_eval_fail_actiontype_orphan{0};
    std::atomic<uint64_t> g_eval_fail_select_namelet_output{0};
    std::atomic<uint64_t> g_select_namelet_empty{0};
    std::atomic<uint64_t> g_select_namelet_single_non_base{0};
    std::atomic<uint64_t> g_rebuild_resolve_nondet{0};
    std::atomic<uint64_t> g_rebuild_evaluate_sentinel_null{0};

    bool isActionTypeDefaultRootShaped(const NamingPtr &naming) {
        if (!naming) {
            return false;
        }
        const auto &v = naming->getNamelets();
        if (v.size() != 2 || !v[0] || !v[1]) {
            return false;
        }
        return v[0]->isBase() && v[1]->isBase() && v[0]->getExprString() == kActionTypeInteractiveExpr &&
               v[1]->getExprString() == kActionTypeNonInteractiveExpr;
    }

    void logEvalDiagSample(const char *tag, uint64_t count) {
        if (count == 1 || count <= 3 || (count % 128) == 0) {
            BLOG("naming eval diag [%s] count=%llu", tag, static_cast<unsigned long long>(count));
        }
    }

    void syncNodesAfterRebuild(const gui_tree::GUITree &tree,
                               const std::unordered_map<gui_tree::GUITreeNode *, NameletPtr> &node_to_namelet) {
        const auto &names = tree.getCurrentNames();
        const auto &groups = tree.getCurrentNodeGroups();
        for (size_t i = 0; i < names.size(); ++i) {
            if (i >= groups.size()) break;
            for (const auto &node : groups[i]) {
                if (!node) continue;
                node->setXPathName(names[i]);
                auto it = node_to_namelet.find(node.get());
                if (it != node_to_namelet.end()) {
                    node->setCurrentNamelet(it->second);
                }
            }
        }
    }

    NamingPtr findExistingChildNaming(const NameletPtr &parentNamelet, const std::string &expr,
                                      const NamerPtr &finer) {
        if (!parentNamelet || !finer) {
            return nullptr;
        }
        for (const auto &kv : parentNamelet->getChildNamings()) {
            const NameletPtr &ch = kv.first;
            if (!ch) {
                continue;
            }
            const NamerPtr chNamer = ch->getNamerPtr();
            if (!chNamer) {
                continue;
            }
            // child reuse is keyed by equivalent namer behavior, not pointer identity.
            if (ch->getExprString() == expr && compareNamer(*chNamer, *finer) == 0) {
                return kv.second;
            }
        }
        return nullptr;
    }

    /**
     * Mirrors Java Naming.extend(parent, namelet).ensureRefine contract: finer must strictly refine
     * the parent namer. Under normal callers (lattice.immediateRefinements / sortedAbove) the
     * invariant holds by construction; this log gives us a breadcrumb if a future caller violates it.
     */
    void debugAssertRefinesTo(const NameletPtr &parentNamelet, const NamerPtr &finer) {
#ifndef NDEBUG
        if (!parentNamelet || !finer) {
            return;
        }
        const NamerPtr parentNamer = parentNamelet->getNamerPtr();
        if (!parentNamer) {
            return;
        }
        if (!finer->refinesTo(*parentNamer)) {
            BLOG("naming ensureRefine violated: finerMask=%u parentMask=%u parentExpr=%s",
                 static_cast<unsigned>(finer->typeDimensionMask()),
                 static_cast<unsigned>(parentNamer->typeDimensionMask()),
                 parentNamelet->getExprString().c_str());
        }
#else
        (void)parentNamelet;
        (void)finer;
#endif
    }

    /** Append a refinement child namelet (lattice edge) and register parent/child Naming links. */
    NamingPtr makeLatticeRefinementChild(const NamingPtr &base, size_t i,
                                         const std::vector<NameletPtr> &namelets,
                                         const NamerPtr &finer) {
        if (!base || !finer) {
            return nullptr;
        }
        NameletPtr parentNamelet = namelets[i];
        if (!parentNamelet) {
            return nullptr;
        }
        debugAssertRefinesTo(parentNamelet, finer);
        const std::string &expr = parentNamelet->getExprString();
        if (NamingPtr cached = findExistingChildNaming(parentNamelet, expr, finer)) {
            return cached;
        }
        NameletPtr probe = std::make_shared<Namelet>(expr, finer);
        probe->setParent(parentNamelet);
        std::vector<NameletPtr> newlets = namelets;
        newlets.push_back(probe);
        NamingPtr childNaming = Naming::createChild(base, std::move(newlets));
        parentNamelet->addChildNaming(probe, childNaming);
        base->addRefinementChild(NamingEdge{parentNamelet, probe}, childNaming);
        return childNaming;
    }

    NamingPtr makeWidgetLatticeRefinementChild(const NamingPtr &base, size_t parentIndex,
                                               const std::vector<NameletPtr> &namelets,
                                               const NamerPtr &finer, const std::string &widgetExpr) {
        if (!base || !finer || widgetExpr.empty()) {
            return nullptr;
        }
        NameletPtr parentNamelet = namelets[parentIndex];
        if (!parentNamelet) {
            return nullptr;
        }
        debugAssertRefinesTo(parentNamelet, finer);
        if (NamingPtr cached = findExistingChildNaming(parentNamelet, widgetExpr, finer)) {
            return cached;
        }
        NameletPtr probe = std::make_shared<Namelet>(widgetExpr, finer);
        probe->setParent(parentNamelet);
        std::vector<NameletPtr> newlets = namelets;
        newlets.push_back(probe);
        NamingPtr childNaming = Naming::createChild(base, std::move(newlets));
        parentNamelet->addChildNaming(probe, childNaming);
        base->addRefinementChild(NamingEdge{parentNamelet, probe}, childNaming);
        return childNaming;
    }

    NamingPtr latticeRefinementChildAtNamelet_impl(const NamingPtr &base, size_t nameletIndex,
                                                    const NamerPtr &finer) {
        if (!base || !finer || nameletIndex >= base->getNamelets().size()) {
            return nullptr;
        }
        return makeLatticeRefinementChild(base, nameletIndex, base->getNamelets(), finer);
    }

    NamingPtr extendUnderNamelet_impl(const NamingPtr &base, size_t parentNameletIndex,
                                      const std::string &widgetXPathExpr, const NamerPtr &finer) {
        if (!base || !finer || widgetXPathExpr.empty() || parentNameletIndex >= base->getNamelets().size()) {
            return nullptr;
        }
        return makeWidgetLatticeRefinementChild(base, parentNameletIndex, base->getNamelets(), finer,
                                                widgetXPathExpr);
    }

    NamingPtr actionRefinementSearch(const NamingPtr &naming, const NamerLattice &lattice,
                                     int max_steps,
                                     const std::function<bool(const NamingPtr &)> &accept,
                                     bool choose_deepest_acceptable,
                                     bool evaluate_all_immediate_candidates) {
        if (!naming || max_steps <= 0 || !accept) {
            return nullptr;
        }
        auto immediateCandidates = [&](const NamingPtr &base) -> std::vector<NamingPtr> {
            std::vector<NamingPtr> out;
            if (!base) {
                return out;
            }
            const auto &namelets = base->getNamelets();
            for (size_t i = 0; i < namelets.size(); ++i) {
                const auto &nl = namelets[i];
                if (!nl || !nl->getNamerPtr()) {
                    continue;
                }
                std::vector<NamerPtr> refs = lattice.immediateRefinements(nl->getNamerPtr());
                for (const auto &finer : refs) {
                    if (!finer) {
                        continue;
                    }
                    if (NamingPtr c = makeLatticeRefinementChild(base, i, namelets, finer)) {
                        out.push_back(std::move(c));
                    }
                }
            }
            return out;
        };
        NamingPtr cur = naming;
        NamingPtr best = nullptr;
        for (int s = 0; s < max_steps; ++s) {
            std::vector<NamingPtr> candidates;
            if (evaluate_all_immediate_candidates) {
                candidates = immediateCandidates(cur);
            } else {
                NamingPtr next = NamingFactory::refineNaming(cur, lattice);
                if (next) {
                    candidates.push_back(std::move(next));
                }
            }
            if (candidates.empty()) {
                break;
            }
            NamingPtr firstAccepted = nullptr;
            NamingPtr lastAccepted = nullptr;
            for (const auto &cand : candidates) {
                if (accept(cand)) {
                    if (!firstAccepted) {
                        firstAccepted = cand;
                    }
                    lastAccepted = cand;
                }
            }
            if (firstAccepted) {
                if (!choose_deepest_acceptable) {
                    return firstAccepted;
                }
                best = lastAccepted;
                cur = lastAccepted;
            } else {
                cur = candidates[0];
            }
        }
        return best;
    }

} // namespace

    NamingPtr NamingFactory::latticeRefinementChildAtNamelet(const NamingPtr &base, size_t nameletIndex,
                                                             const NamerPtr &finer) {
        return latticeRefinementChildAtNamelet_impl(base, nameletIndex, finer);
    }

    NamingPtr NamingFactory::extendUnderNamelet(const NamingPtr &base, size_t parentNameletIndex,
                                                const std::string &widgetXPathExpr, const NamerPtr &finer) {
        return extendUnderNamelet_impl(base, parentNameletIndex, widgetXPathExpr, finer);
    }

    NamingPtr NamingFactory::replaceLast(const NamingPtr &naming, const NameletPtr &replaced, const NamerPtr &finer) {
        if (!naming || !replaced || !finer) {
            return nullptr;
        }
        if (!naming->isReplaceable(replaced)) {
            return nullptr;
        }
        NamingPtr parentNaming = naming->getParent();
        if (!parentNaming) {
            return nullptr;
        }
        NameletPtr pOf = replaced->getParent();
        if (!pOf) {
            return nullptr;
        }
        const auto &pv = parentNaming->getNamelets();
        size_t idx = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < pv.size(); ++i) {
            if (pv[i] == pOf) {
                idx = i;
                break;
            }
        }
        if (idx >= pv.size()) {
            for (size_t i = 0; i < pv.size(); ++i) {
                const NameletPtr &a = pv[i];
                if (!a) {
                    continue;
                }
                if (a->getExprString() == pOf->getExprString() &&
                    compareNamer(a->getNamer(), pOf->getNamer()) == 0) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx >= pv.size()) {
            return nullptr;
        }
        return extendUnderNamelet(parentNaming, idx, replaced->getExprString(), finer);
    }

    void NamingFactory::setDefaultRootNamingMode(ApeBaseNamingMode mode) {
        g_default_root_naming_mode = mode;
    }

    NamingPtr NamingFactory::getBaseNaming() {
        return defaultRootNaming();
    }

    ApeBaseNamingMode NamingFactory::getDefaultRootNamingMode() {
        return g_default_root_naming_mode;
    }

    Naming::NamingResult NamingFactory::evaluateNaming(const NamingPtr &naming, gui_tree::GUITree &tree,
                                                       const std::shared_ptr<gui_tree::XPathNodeMapper> &dom) {
        Naming::NamingResult out;
        if (!naming) {
            return out;
        }
        out = naming->namingInternal(tree, dom);
        if (!out.names.empty() && !out.names[0]) {
            const uint64_t c = ++g_eval_fail_node_without_namelet;
            logEvalDiagSample("fail_node_without_namelet", c);
            if (isActionTypeDefaultRootShaped(naming)) {
                const uint64_t ac = ++g_eval_fail_actiontype_orphan;
                logEvalDiagSample("fail_actiontype_default_orphan_node", ac);
            }
        }
        return out;
    }

    bool NamingFactory::rebuildTree(const NamingPtr &naming, gui_tree::GUITree &tree,
                                    const std::shared_ptr<gui_tree::XPathNodeMapper> &dom) {
        if (!naming || !dom) {
            return false;
        }
        Naming::NamingResult r = evaluateNaming(naming, tree, dom);
        if (resolveNonDeterminism(r)) {
            const uint64_t c = ++g_rebuild_resolve_nondet;
            logEvalDiagSample("rebuild_resolve_nondeterminism", c);
            if (!r.names.empty() && !r.names[0]) {
                const uint64_t s = ++g_rebuild_evaluate_sentinel_null;
                logEvalDiagSample("rebuild_after_evaluate_sentinel_null", s);
            }
            return false;
        }
        std::unordered_map<gui_tree::GUITreeNode *, NameletPtr> node_to_namelet;
        for (size_t i = 0; i < r.node_groups.size(); ++i) {
            for (size_t j = 0; j < r.node_groups[i].size(); ++j) {
                gui_tree::GUITreeNodePtr &np = r.node_groups[i][j];
                if (!np) {
                    continue;
                }
                if (i < r.namelet_groups.size() && j < r.namelet_groups[i].size()) {
                    node_to_namelet[np.get()] = r.namelet_groups[i][j];
                }
            }
        }

        tree.setCurrentNaming(naming, std::move(r.names), std::move(r.node_groups));
        syncNodesAfterRebuild(tree, node_to_namelet);
        return true;
    }

    NamingPtr NamingFactory::defaultRootNaming() {
        const NamerFactory &factory = NamerFactory::current();
        auto getNamer = [&](uint32_t mask) -> NamerPtr { return factory.getByMask(mask); };
        const uint32_t typeMask = 1u << static_cast<unsigned>(NamerType::TYPE);

        auto createActionTypeBaseNaming = [&]() -> NamingPtr {
            const NamerPtr typeNamer = getNamer(typeMask);
            const NamerPtr bottomNamer = factory.empty();
            if (!typeNamer || !bottomNamer) {
                return nullptr;
            }
            std::vector<NameletPtr> v;
            v.reserve(2);
            v.push_back(
                std::make_shared<Namelet>(Namelet::Type::BASE, kActionTypeInteractiveExpr, typeNamer));
            v.push_back(
                std::make_shared<Namelet>(Namelet::Type::BASE, kActionTypeNonInteractiveExpr, bottomNamer));
            return std::make_shared<Naming>(std::move(v));
        };

        auto createResourceIDBaseNaming = [&]() -> NamingPtr {
            const NamerPtr typeNamer = getNamer(typeMask);
            if (!typeNamer) {
                return nullptr;
            }
            std::vector<NameletPtr> v;
            v.reserve(2);
            v.push_back(std::make_shared<Namelet>(Namelet::Type::BASE, "//*[@resource-id!='']", typeNamer));
            v.push_back(std::make_shared<Namelet>(Namelet::Type::BASE, "//*[@resource-id='']", typeNamer));
            return std::make_shared<Naming>(std::move(v));
        };

        auto createParentIndexBaseNaming = [&]() -> NamingPtr {
            const uint32_t parentMask = 1u << static_cast<unsigned>(NamerType::PARENT);
            const uint32_t indexMask = 1u << static_cast<unsigned>(NamerType::INDEX);
            const NamerPtr parentIndexNamer = getNamer(parentMask | indexMask);
            if (!parentIndexNamer) {
                return nullptr;
            }
            std::vector<NameletPtr> v;
            v.reserve(1);
            v.push_back(std::make_shared<Namelet>(Namelet::Type::BASE, "//*", parentIndexNamer));
            return std::make_shared<Naming>(std::move(v));
        };

        auto createBoostedBaseNaming = [&]() -> NamingPtr {
            NamingPtr base = createActionTypeBaseNaming();
            if (!base) {
                return nullptr;
            }
            const auto &namelets = base->getNamelets();
            if (namelets.size() < 2 || !namelets[1]) {
                return nullptr;
            }
            const uint32_t parentMask = 1u << static_cast<unsigned>(NamerType::PARENT);
            const uint32_t indexMask = 1u << static_cast<unsigned>(NamerType::INDEX);
            const NamerPtr refined = getNamer(parentMask | typeMask | indexMask);
            if (!refined) {
                return nullptr;
            }
            static const char kBoostedExpr[] =
                "//*[@class!='android.widget.ListView' and @class!='android.widget.GridView' and "
                "@class!='android.support.v7.widget.RecyclerView' and "
                "@class!='android.support.v17.leanback.widget.VerticalGridView' and "
                "@class!='android.support.v17.leanback.widget.HorizontalGridView' and "
                "@class!='android.widget.ExpandableListView']/*";
            std::vector<NameletPtr> v = namelets;
            NameletPtr parentNamelet = namelets[1];
            NameletPtr childNamelet =
                std::make_shared<Namelet>(Namelet::Type::BASE, kBoostedExpr, refined);
            childNamelet->setParent(parentNamelet);
            v.push_back(std::move(childNamelet));
            return std::make_shared<Naming>(std::move(v));
        };

        auto createStoatBaseNaming = [&]() -> NamingPtr {
            const uint32_t parentMask = 1u << static_cast<unsigned>(NamerType::PARENT);
            const uint32_t indexMask = 1u << static_cast<unsigned>(NamerType::INDEX);
            const NamerPtr rootNamer = getNamer(parentMask | typeMask | indexMask);
            const NamerPtr parentTypeNamer = getNamer(parentMask | typeMask);
            if (!rootNamer || !parentTypeNamer) {
                return nullptr;
            }
            static const char kStoatListExpr[] =
                "//*[@class='android.widget.ListView' or @class='android.widget.GridView' or "
                "@class='android.support.v7.widget.RecyclerView' or "
                "@class='android.support.v17.leanback.widget.VerticalGridView' or "
                "@class='android.support.v17.leanback.widget.HorizontalGridView' or "
                "@class='android.widget.ExpandableListView']/*";
            std::vector<NameletPtr> v;
            v.reserve(2);
            NameletPtr root = std::make_shared<Namelet>(Namelet::Type::BASE, "//*", rootNamer);
            v.push_back(root);
            NameletPtr listChild =
                std::make_shared<Namelet>(Namelet::Type::BASE, kStoatListExpr, parentTypeNamer);
            listChild->setParent(root);
            v.push_back(std::move(listChild));
            return std::make_shared<Naming>(std::move(v));
        };

        switch (g_default_root_naming_mode) {
        case ApeBaseNamingMode::TypeOnly:
        case ApeBaseNamingMode::ActionType:
            return createActionTypeBaseNaming();
        case ApeBaseNamingMode::ResourceID:
            return createResourceIDBaseNaming();
        case ApeBaseNamingMode::ParentIndex:
            return createParentIndexBaseNaming();
        case ApeBaseNamingMode::Boosted:
            return createBoostedBaseNaming();
        case ApeBaseNamingMode::Stoat:
            return createStoatBaseNaming();
        default:
            return createBoostedBaseNaming();
        }
    }

    NamingPtr NamingFactory::refineNaming(const NamingPtr &naming, const NamerLattice &lattice) {
        if (!naming || naming->getNamelets().empty()) {
            return nullptr;
        }
        // Only walk downward on the namer lattice. Returning the structural parent here is
        // coarser (abstract direction) and polluted actionRefinementCandidatesWithOptions: the
        // multi-hop gather would re-insert the activity's current naming with finenessGain==0,
        // which Model then sorted ahead of real refinements.
        const auto &namelets = naming->getNamelets();
        for (size_t i = 0; i < namelets.size(); ++i) {
            const auto &nl = namelets[i];
            if (!nl) {
                continue;
            }
            NamerPtr namer = nl->getNamerPtr();
            if (!namer) {
                continue;
            }
            std::vector<NamerPtr> refs = lattice.immediateRefinements(namer);
            if (refs.empty()) {
                continue;
            }
            NamerPtr finer = refs[0];
            if (NamingPtr childNaming = makeLatticeRefinementChild(naming, i, namelets, finer)) {
                static std::atomic<uint64_t> g_refine_child_seq{0};
                const uint64_t n = ++g_refine_child_seq;
                if (n <= 24 || (n % 768) == 0) {
                    const std::string fromFp = naming->fingerprintString();
                    const std::string toFp = childNaming ? childNaming->fingerprintString() : std::string("-");
                    const unsigned finerMask = finer ? finer->typeDimensionMask() : 0u;
                    BDLOG("naming refineNaming: lattice step idx=%zu naming=%p child_fp=%s finerMask=%u from_fp=%s",
                          i, static_cast<const void *>(naming.get()), toFp.c_str(), finerMask, fromFp.c_str());
                }
                return childNaming;
            }
        }
        return nullptr;
    }

    NamingPtr NamingFactory::abstractNaming(const NamingPtr &naming, const NamerLattice &lattice) {
        if (!naming || naming->getNamelets().empty()) {
            return nullptr;
        }
        const auto &namelets = naming->getNamelets();
        for (size_t i = 0; i < namelets.size(); ++i) {
            const auto &nl = namelets[i];
            if (!nl) {
                continue;
            }
            NamerPtr namer = nl->getNamerPtr();
            if (!namer) {
                continue;
            }
            std::vector<NamerPtr> abs = lattice.immediateAbstractions(namer);
            if (abs.empty()) {
                continue;
            }
            NamerPtr coarser = abs[0];
            std::vector<NameletPtr> newlets;
            newlets.reserve(namelets.size());
            for (size_t j = 0; j < namelets.size(); ++j) {
                if (j == i) {
                    newlets.push_back(std::make_shared<Namelet>(namelets[j]->getExprString(), coarser));
                } else {
                    newlets.push_back(
                        std::make_shared<Namelet>(namelets[j]->getExprString(), namelets[j]->getNamerPtr()));
                }
            }
            return std::make_shared<Naming>(std::move(newlets));
        }
        return nullptr;
    }

    bool NamingFactory::resolveNonDeterminism(Naming::NamingResult &result) {
        const size_t n = result.names.size();
        if (n != result.node_groups.size() || n != result.namelet_groups.size()) {
            return true;
        }
        std::unordered_set<gui_tree::GUITreeNode *> seen;
        for (size_t i = 0; i < n; ++i) {
            if (result.node_groups[i].size() != result.namelet_groups[i].size()) {
                return true;
            }
            for (const auto &np : result.node_groups[i]) {
                if (!np) {
                    continue;
                }
                gui_tree::GUITreeNode *raw = np.get();
                if (seen.count(raw) != 0) {
                    return true;
                }
                seen.insert(raw);
            }
        }
        return false;
    }

    NamingPtr NamingFactory::batchRefine(const NamingPtr &naming, const NamerLattice &lattice, int max_steps) {
        if (!naming || max_steps <= 0) {
            return naming;
        }
        NamingPtr cur = naming;
        for (int s = 0; s < max_steps; ++s) {
            NamingPtr next = refineNaming(cur, lattice);
            if (!next) {
                break;
            }
            cur = std::move(next);
        }
        return cur;
    }

    NamingPtr NamingFactory::batchRefineWithRebuildFixedPoint(const NamingPtr &naming, const NamerLattice &lattice,
                                                              gui_tree::GUITree &tree,
                                                              const std::shared_ptr<gui_tree::XPathNodeMapper> &dom,
                                                              int max_steps) {
        if (!naming || !dom) {
            return nullptr;
        }
        NamingPtr cur = naming;
        if (!rebuildTree(cur, tree, dom)) {
            return nullptr;
        }
        if (max_steps <= 0) {
            return cur;
        }
        uintptr_t prevHash = StateKey::hashFromGUITree(tree);
        for (int s = 0; s < max_steps; ++s) {
            NamingPtr next = refineNaming(cur, lattice);
            if (!next) {
                break;
            }
            if (!rebuildTree(next, tree, dom)) {
                return nullptr;
            }
            const uintptr_t h = StateKey::hashFromGUITree(tree);
            if (h == prevHash) {
                if (!rebuildTree(cur, tree, dom)) {
                    return nullptr;
                }
                break;
            }
            prevHash = h;
            cur = std::move(next);
        }
        return cur;
    }

    NamingPtr NamingFactory::batchAbstract(const NamingPtr &naming, const NamerLattice &lattice, int max_steps) {
        if (!naming || max_steps <= 0) {
            return naming;
        }
        NamingPtr cur = naming;
        for (int s = 0; s < max_steps; ++s) {
            NamingPtr next = abstractNaming(cur, lattice);
            if (!next) {
                break;
            }
            cur = std::move(next);
        }
        return cur;
    }

    NamingPtr NamingFactory::actionRefinement(const NamingPtr &naming, const NamerLattice &lattice, int max_steps) {
        if (!naming || max_steps <= 0) {
            return naming;
        }
        NamingPtr cur = naming;
        ActionRefinementOptions options;
        options.max_steps = 1;
        for (int s = 0; s < max_steps; ++s) {
            NamingPtr next = actionRefinementWithOptions(cur, lattice, options);
            if (!next) {
                break;
            }
            cur = std::move(next);
        }
        return cur;
    }

    NamingPtr NamingFactory::actionRefinementWithBlacklist(const NamingPtr &naming, const NamerLattice &lattice,
                                                           int max_steps, const std::set<std::string> &blacklist) {
        ActionRefinementOptions options;
        options.max_steps = max_steps;
        options.blacklist = &blacklist;
        return actionRefinementWithOptions(naming, lattice, options);
    }

    NamingPtr NamingFactory::actionRefinementWithOptions(const NamingPtr &naming, const NamerLattice &lattice,
                                                         const ActionRefinementOptions &options) {
        auto accept = [&](const NamingPtr &candidate) -> bool {
            if (!candidate) {
                return false;
            }
            if (options.blacklist &&
                options.blacklist->count(candidate->fingerprintString()) != 0) {
                return false;
            }
            if (options.accept_predicate) {
                return options.accept_predicate(candidate);
            }
            return true;
        };
        return actionRefinementSearch(naming, lattice, options.max_steps, accept,
                                      options.choose_deepest_acceptable,
                                      options.evaluate_all_immediate_candidates);
    }

    std::vector<NamingPtr> NamingFactory::actionRefinementCandidatesWithOptions(
        const NamingPtr &naming, const NamerLattice &lattice, const ActionRefinementOptions &options) {
        std::vector<NamingPtr> out;
        if (!naming || options.max_steps <= 0) {
            return out;
        }
        auto accept = [&](const NamingPtr &candidate) -> bool {
            if (!candidate) {
                return false;
            }
            if (options.blacklist &&
                options.blacklist->count(candidate->fingerprintString()) != 0) {
                return false;
            }
            if (options.accept_predicate) {
                return options.accept_predicate(candidate);
            }
            return true;
        };
        auto immediateCandidates = [&](const NamingPtr &base) -> std::vector<NamingPtr> {
            std::vector<NamingPtr> cands;
            if (!base) {
                return cands;
            }
            const auto &namelets = base->getNamelets();
            for (size_t i = 0; i < namelets.size(); ++i) {
                const auto &nl = namelets[i];
                if (!nl || !nl->getNamerPtr()) {
                    continue;
                }
                std::vector<NamerPtr> refs = lattice.immediateRefinements(nl->getNamerPtr());
                for (const auto &finer : refs) {
                    if (!finer) {
                        continue;
                    }
                    if (NamingPtr c = makeLatticeRefinementChild(base, i, namelets, finer)) {
                        cands.push_back(std::move(c));
                    }
                }
            }
            return cands;
        };

        std::unordered_set<std::string> seen;
        NamingPtr cur = naming;
        for (int step = 0; step < options.max_steps; ++step) {
            std::vector<NamingPtr> candidates;
            if (options.evaluate_all_immediate_candidates) {
                candidates = immediateCandidates(cur);
            } else {
                NamingPtr next = NamingFactory::refineNaming(cur, lattice);
                if (next) {
                    candidates.push_back(next);
                }
            }
            if (candidates.empty()) {
                break;
            }
            NamingPtr firstAccepted = nullptr;
            NamingPtr lastAccepted = nullptr;
            for (const auto &cand : candidates) {
                if (!cand) {
                    continue;
                }
                if (!accept(cand)) {
                    continue;
                }
                const std::string fp = cand->fingerprintString();
                if (seen.insert(fp).second) {
                    out.push_back(cand);
                    if (!firstAccepted) {
                        firstAccepted = cand;
                    }
                    lastAccepted = cand;
                }
            }
            if (firstAccepted) {
                cur = options.choose_deepest_acceptable ? lastAccepted : firstAccepted;
            } else {
                cur = candidates[0];
            }
        }
        {
            static std::atomic<uint64_t> g_arc_chain{0};
            const uint64_t t = ++g_arc_chain;
            if (!out.empty() && (t <= 14 || (t % 320) == 0)) {
                const size_t nshow = std::min<size_t>(out.size(), 4);
                for (size_t i = 0; i < nshow; ++i) {
                    const NamingPtr c = out[i];
                    const NamingPtr p = c ? c->getParent() : nullptr;
                    BDLOG(
                        "naming chain: actionRefineCands out[%zu/%zu] cand=%p parent=%p base_in=%p "
                        "parent_eq_base=%d",
                        i, out.size(), static_cast<const void *>(c.get()),
                        static_cast<const void *>(p.get()), static_cast<const void *>(naming.get()),
                        (p.get() == naming.get()) ? 1 : 0);
                }
            }
        }
        return out;
    }

    std::vector<NamingPtr> NamingFactory::widgetXPathRefinementCandidatesWithOptions(
        const NamingPtr &naming, const NamerLattice &lattice, const ActionRefinementOptions &options,
        const NameletPtr &widget_parent, const std::string &widget_xpath_expr) {
        std::vector<NamingPtr> out;
        if (!naming || options.max_steps <= 0 || !widget_parent || widget_xpath_expr.empty()) {
            return out;
        }

        auto accept = [&](const NamingPtr &candidate) -> bool {
            if (!candidate) {
                return false;
            }
            if (options.blacklist &&
                options.blacklist->count(candidate->fingerprintString()) != 0) {
                return false;
            }
            if (options.accept_predicate) {
                return options.accept_predicate(candidate);
            }
            return true;
        };

        auto immediateWidgetCandidates = [&](const NamingPtr &base) -> std::vector<NamingPtr> {
            std::vector<NamingPtr> cands;
            if (!base) {
                return cands;
            }
            const auto &namelets = base->getNamelets();
            bool foundAnchor = false;
            size_t anchorIdx = 0;
            for (size_t i = namelets.size(); i > 0; --i) {
                const NameletPtr &nl = namelets[i - 1];
                if (!nl) {
                    continue;
                }
                for (NameletPtr w = widget_parent; w; w = w->getParent()) {
                    if (w.get() == nl.get()) {
                        anchorIdx = i - 1;
                        foundAnchor = true;
                        break;
                    }
                }
                if (foundAnchor) {
                    break;
                }
            }
            if (!foundAnchor) {
                return cands;
            }
            const NameletPtr anchor = namelets[anchorIdx];
            if (!anchor || !anchor->getNamerPtr()) {
                return cands;
            }
            std::vector<NamerPtr> aboves = lattice.sortedAbove(anchor->getNamerPtr());
            for (const NamerPtr &finer : aboves) {
                if (!finer) {
                    continue;
                }
                if (NamingPtr c =
                        makeWidgetLatticeRefinementChild(base, anchorIdx, namelets, finer, widget_xpath_expr)) {
                    cands.push_back(std::move(c));
                }
            }
            return cands;
        };

        std::unordered_set<std::string> seen;
        NamingPtr cur = naming;
        for (int step = 0; step < options.max_steps; ++step) {
            std::vector<NamingPtr> candidates;
            if (options.evaluate_all_immediate_candidates) {
                candidates = immediateWidgetCandidates(cur);
            } else {
                std::vector<NamingPtr> all = immediateWidgetCandidates(cur);
                if (!all.empty()) {
                    candidates.push_back(std::move(all[0]));
                }
            }
            if (candidates.empty()) {
                break;
            }
            NamingPtr firstAccepted = nullptr;
            NamingPtr lastAccepted = nullptr;
            for (const auto &cand : candidates) {
                if (!cand) {
                    continue;
                }
                if (!accept(cand)) {
                    continue;
                }
                const std::string fp = cand->fingerprintString();
                if (seen.insert(fp).second) {
                    out.push_back(cand);
                    if (!firstAccepted) {
                        firstAccepted = cand;
                    }
                    lastAccepted = cand;
                }
            }
            if (firstAccepted) {
                cur = options.choose_deepest_acceptable ? lastAccepted : firstAccepted;
            } else {
                cur = candidates[0];
            }
        }
        {
            static std::atomic<uint64_t> g_wx_chain{0};
            const uint64_t t = ++g_wx_chain;
            if (!out.empty() && (t <= 12 || (t % 320) == 0)) {
                const size_t nshow = std::min<size_t>(out.size(), 3);
                for (size_t i = 0; i < nshow; ++i) {
                    const NamingPtr c = out[i];
                    const NamingPtr p = c ? c->getParent() : nullptr;
                    BDLOG(
                        "naming chain: widgetXPathRefineCands out[%zu/%zu] cand=%p parent=%p base_in=%p "
                        "parent_eq_base=%d xpath_len=%zu",
                        i, out.size(), static_cast<const void *>(c.get()),
                        static_cast<const void *>(p.get()), static_cast<const void *>(naming.get()),
                        (p.get() == naming.get()) ? 1 : 0, widget_xpath_expr.size());
                }
            }
        }
        return out;
    }

    NamingEvaluateDiagStats NamingFactory::consumeNamingEvaluateDiagStats() {
        NamingEvaluateDiagStats s;
        s.fail_node_without_namelet = g_eval_fail_node_without_namelet.exchange(0);
        s.fail_actiontype_default_orphan_node = g_eval_fail_actiontype_orphan.exchange(0);
        s.fail_select_namelet_for_output = g_eval_fail_select_namelet_output.exchange(0);
        s.select_namelet_empty_candidates = g_select_namelet_empty.exchange(0);
        s.select_namelet_single_non_base = g_select_namelet_single_non_base.exchange(0);
        s.rebuild_resolve_nondeterminism = g_rebuild_resolve_nondet.exchange(0);
        s.rebuild_after_evaluate_sentinel_null = g_rebuild_evaluate_sentinel_null.exchange(0);
        if (s.any()) {
            BLOG("naming evaluate diag (window): no_namelet=%llu actiontype_orphan=%llu "
                 "select_out=%llu sel_empty=%llu sel_single_nonbase=%llu "
                 "rebuild_nondet=%llu rebuild_sentinel_null=%llu",
                 static_cast<unsigned long long>(s.fail_node_without_namelet),
                 static_cast<unsigned long long>(s.fail_actiontype_default_orphan_node),
                 static_cast<unsigned long long>(s.fail_select_namelet_for_output),
                 static_cast<unsigned long long>(s.select_namelet_empty_candidates),
                 static_cast<unsigned long long>(s.select_namelet_single_non_base),
                 static_cast<unsigned long long>(s.rebuild_resolve_nondeterminism),
                 static_cast<unsigned long long>(s.rebuild_after_evaluate_sentinel_null));
        }
        return s;
    }

} // namespace naming
} // namespace fastbotx
