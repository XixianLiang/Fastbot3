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

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace fastbotx {
namespace naming {
namespace {
    ApeBaseNamingMode g_default_root_naming_mode = ApeBaseNamingMode::ActionType;

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

    bool nameletSelectLess(const NameletPtr &a, const NameletPtr &b) {
        if (!a || !b) return a.get() < b.get();
        if (a->getDepth() != b->getDepth()) return a->getDepth() < b->getDepth();
        const int c = a->getExprString().compare(b->getExprString());
        if (c != 0) return c < 0;
        return a.get() < b.get();
    }

    NameletPtr selectNameletForNode(const std::vector<NameletPtr> &candidates) {
        if (candidates.empty()) {
            return nullptr;
        }
        if (candidates.size() == 1) {
            return (candidates[0] && candidates[0]->isBase()) ? candidates[0] : nullptr;
        }

        // Only copy when we actually need to sort/iterate multiple candidates.
        std::vector<NameletPtr> sorted = candidates;
        std::sort(sorted.begin(), sorted.end(), nameletSelectLess);

        // Parent-chain membership check: compare by pointer identity only.
        std::unordered_set<const Namelet *> selectedSet;
        selectedSet.reserve(sorted.size() * 2 + 1);
        for (const auto &nl : sorted) {
            selectedSet.insert(nl.get());
        }
        for (size_t i = sorted.size(); i > 0; --i) {
            NameletPtr cur = sorted[i - 1];
            NameletPtr p = cur ? cur->getParent() : nullptr;
            while (p) {
                if (selectedSet.find(p.get()) == selectedSet.end()) {
                    break;
                }
                p = p->getParent();
            }
            if (!p) {
                return cur;
            }
        }
        return sorted.back();
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
            if (ch->getExprString() == expr && ch->getNamerPtr() == finer) {
                return kv.second;
            }
        }
        return nullptr;
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

    struct NamingEvalGuard {
        explicit NamingEvalGuard(const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *m) {
            namingEvalSetNodeToNamer(m);
        }
        ~NamingEvalGuard() { namingEvalClear(); }
        NamingEvalGuard(const NamingEvalGuard &) = delete;
        NamingEvalGuard &operator=(const NamingEvalGuard &) = delete;
    };

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

    void NamingFactory::setDefaultRootNamingMode(ApeBaseNamingMode mode) {
        g_default_root_naming_mode = mode;
    }

    ApeBaseNamingMode NamingFactory::getDefaultRootNamingMode() {
        return g_default_root_naming_mode;
    }

    Naming::NamingResult NamingFactory::evaluateNaming(const NamingPtr &naming, gui_tree::GUITree &tree,
                                                       const std::shared_ptr<gui_tree::XPathNodeMapper> &dom) {
        Naming::NamingResult out;
        if (!naming || !dom) {
            return out;
        }

        std::unordered_map<std::string, size_t> key_to_idx;
        std::unordered_map<gui_tree::GUITreeNode *, std::vector<NameletPtr>> node_to_namelets;
        std::unordered_map<gui_tree::GUITreeNode *, gui_tree::GUITreeNodePtr> node_ref;
        std::vector<gui_tree::GUITreeNode *> node_order;
        node_order.reserve(256);

        for (const auto &nl : naming->getNamelets()) {
            if (!nl) {
                continue;
            }
            std::vector<gui_tree::GUITreeNodePtr> matched = dom->nodesForXPath(nl->getExprString());
            for (const auto &nptr : matched) {
                if (!nptr) {
                    continue;
                }
                gui_tree::GUITreeNode *raw = nptr.get();
                if (node_ref.find(raw) == node_ref.end()) {
                    node_ref.emplace(raw, nptr);
                    node_order.push_back(raw);
                }
                node_to_namelets[raw].push_back(nl);
            }
        }

        // APE-compatible guard: every GUI tree node must have at least one matched namelet.
        std::vector<gui_tree::GUITreeNode *> all_nodes;
        all_nodes.reserve(node_order.size() > 0 ? node_order.size() : 64);
        std::function<void(gui_tree::GUITreeNode *)> collect = [&](gui_tree::GUITreeNode *n) {
            if (!n) return;
            all_nodes.push_back(n);
            for (const auto &ch : n->getChildren()) {
                collect(ch.get());
            }
        };
        collect(tree.getRootNode());
        for (gui_tree::GUITreeNode *n : all_nodes) {
            if (node_to_namelets.find(n) == node_to_namelets.end()) {
                // Force rebuildTree() to fail via resolveNonDeterminism size guard.
                out.names.push_back(nullptr);
                return out;
            }
        }

        std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> node_to_namer;
        node_to_namer.reserve(all_nodes.size());
        for (gui_tree::GUITreeNode *n : all_nodes) {
            auto itNl = node_to_namelets.find(n);
            if (itNl == node_to_namelets.end()) {
                continue;
            }
            NameletPtr sel = selectNameletForNode(itNl->second);
            if (sel && sel->getNamerPtr()) {
                node_to_namer[n] = &sel->getNamer();
            }
        }
        NamingEvalGuard namingEvalGuard(&node_to_namer);

        for (gui_tree::GUITreeNode *raw : node_order) {
            auto itNode = node_ref.find(raw);
            if (itNode == node_ref.end()) {
                continue;
            }
            auto itNamelets = node_to_namelets.find(raw);
            if (itNamelets == node_to_namelets.end() || itNamelets->second.empty()) {
                continue;
            }
            gui_tree::GUITreeNodePtr nptr = itNode->second;
            NameletPtr selected = selectNameletForNode(itNamelets->second);
            if (!selected || !selected->getNamerPtr()) {
                // Force rebuildTree() to fail via resolveNonDeterminism size guard.
                out.names.push_back(nullptr);
                return out;
            }

            Namer &namer = selected->getNamer();
            // Contract: when non-empty, xpathKeyForNode(node) must equal naming(node)->toXPath().
            // So we can reuse the precomputed key to avoid calling name->toXPath() again.
            std::string kPrecheck = namer.xpathKeyForNode(*nptr);
            if (!kPrecheck.empty()) {
                auto itHit = key_to_idx.find(kPrecheck);
                if (itHit != key_to_idx.end()) {
                    const size_t idx = itHit->second;
                    out.node_groups[idx].push_back(nptr);
                    out.namelet_groups[idx].push_back(selected);
                    continue;
                }
            }

            NamePtr name = kPrecheck.empty() ? namer.naming(*nptr) : namer.namingWithXPathKey(*nptr, kPrecheck);
            if (!name) {
                continue;
            }

            std::string k;
            if (!kPrecheck.empty()) {
                k = std::move(kPrecheck);
            } else {
                k = name->toXPath();
            }

            auto itDup = key_to_idx.find(k);
            if (itDup != key_to_idx.end()) {
                const size_t idx = itDup->second;
                out.node_groups[idx].push_back(nptr);
                out.namelet_groups[idx].push_back(selected);
                continue;
            }

            const size_t idx = out.names.size();
            key_to_idx[k] = idx;
            out.names.push_back(std::move(name));
            out.node_groups.emplace_back();
            out.namelet_groups.emplace_back();
            out.node_groups[idx].push_back(nptr);
            out.namelet_groups[idx].push_back(selected);
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

        auto createTypeOnlyBaseNaming = [&]() -> NamingPtr {
            const NamerPtr typeNamer = getNamer(typeMask);
            if (!typeNamer) {
                return nullptr;
            }
            std::vector<NameletPtr> v;
            v.reserve(1);
            v.push_back(std::make_shared<Namelet>(Namelet::Type::BASE, "//*", typeNamer));
            return std::make_shared<Naming>(std::move(v));
        };

        auto createActionTypeBaseNaming = [&]() -> NamingPtr {
            const NamerPtr typeNamer = getNamer(typeMask);
            const NamerPtr bottomNamer = factory.empty();
            if (!typeNamer || !bottomNamer) {
                return nullptr;
            }
            std::vector<NameletPtr> v;
            v.reserve(2);
            v.push_back(std::make_shared<Namelet>(
                Namelet::Type::BASE,
                "//*[@clickable='true' or @long-clickable='true' or @checkable='true' or @scrollable='true']",
                typeNamer));
            v.push_back(std::make_shared<Namelet>(
                Namelet::Type::BASE,
                "//*[@clickable='false' and @long-clickable='false' and @checkable='false' and @scrollable='false']",
                bottomNamer));
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
            return createTypeOnlyBaseNaming();
        case ApeBaseNamingMode::ResourceID:
            return createResourceIDBaseNaming();
        case ApeBaseNamingMode::ParentIndex:
            return createParentIndexBaseNaming();
        case ApeBaseNamingMode::Boosted:
            return createBoostedBaseNaming();
        case ApeBaseNamingMode::Stoat:
            return createStoatBaseNaming();
        case ApeBaseNamingMode::ActionType:
        default:
            return createActionTypeBaseNaming();
        }
    }

    NamingPtr NamingFactory::refineNaming(const NamingPtr &naming, const NamerLattice &lattice) {
        if (!naming || naming->getNamelets().empty()) {
            return nullptr;
        }
        // Prefer structural rollback in the refinement lattice (APE batchAbstract semantics).
        if (auto parent = naming->getParent()) {
            return parent;
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
            std::vector<NamerPtr> refs = lattice.immediateRefinements(namer);
            if (refs.empty()) {
                continue;
            }
            NamerPtr finer = refs[0];
            if (NamingPtr childNaming = makeLatticeRefinementChild(naming, i, namelets, finer)) {
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
        return out;
    }

} // namespace naming
} // namespace fastbotx
