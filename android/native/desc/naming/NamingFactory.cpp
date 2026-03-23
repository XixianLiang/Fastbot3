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
#include "StateKey.h"
#include "../gui_tree/GUITree.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace fastbotx {
namespace naming {
namespace {

    bool namePtrLess(const NamePtr &a, const NamePtr &b) {
        if (!a || !b) return a.get() < b.get();
        return a->toXPath() < b->toXPath();
    }

    void sortNamingResultLikeGUITree(Naming::NamingResult &r) {
        if (r.names.size() != r.node_groups.size() || r.names.size() != r.namelet_groups.size()) {
            return;
        }
        const size_t n = r.names.size();
        std::vector<size_t> perm(n);
        for (size_t i = 0; i < n; ++i) perm[i] = i;
        std::sort(perm.begin(), perm.end(), [&](size_t a, size_t b) { return namePtrLess(r.names[a], r.names[b]); });

        std::vector<NamePtr> names;
        std::vector<std::vector<gui_tree::GUITreeNodePtr>> node_groups;
        std::vector<std::vector<NameletPtr>> namelet_groups;
        names.reserve(n);
        node_groups.reserve(n);
        namelet_groups.reserve(n);
        for (size_t k = 0; k < n; ++k) {
            const size_t i = perm[k];
            names.push_back(r.names[i]);
            node_groups.push_back(std::move(r.node_groups[i]));
            namelet_groups.push_back(std::move(r.namelet_groups[i]));
        }
        r.names = std::move(names);
        r.node_groups = std::move(node_groups);
        r.namelet_groups = std::move(namelet_groups);
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
                    std::vector<NameletPtr> newlets;
                    newlets.reserve(namelets.size());
                    for (size_t j = 0; j < namelets.size(); ++j) {
                        if (!namelets[j]) {
                            newlets.push_back(nullptr);
                        } else if (j == i) {
                            newlets.push_back(std::make_shared<Namelet>(namelets[j]->getExprString(), finer));
                        } else {
                            newlets.push_back(
                                std::make_shared<Namelet>(namelets[j]->getExprString(), namelets[j]->getNamerPtr()));
                        }
                    }
                    out.push_back(std::make_shared<Naming>(std::move(newlets)));
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

    Naming::NamingResult NamingFactory::evaluateNaming(const NamingPtr &naming, gui_tree::GUITree & /*tree*/,
                                                       const std::shared_ptr<gui_tree::XPathNodeMapper> &dom) {
        Naming::NamingResult out;
        if (!naming || !dom) {
            return out;
        }

        std::unordered_map<std::string, size_t> key_to_idx;
        std::unordered_set<gui_tree::GUITreeNode *> visited;

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
                if (visited.count(raw) != 0) {
                    continue;
                }
                visited.insert(raw);
                Namer &namer = nl->getNamer();
                const std::string kPrecheck = namer.xpathKeyForNode(*nptr);
                if (!kPrecheck.empty()) {
                    auto itHit = key_to_idx.find(kPrecheck);
                    if (itHit != key_to_idx.end()) {
                        const size_t idx = itHit->second;
                        out.node_groups[idx].push_back(nptr);
                        out.namelet_groups[idx].push_back(nl);
                        continue;
                    }
                }
                NamePtr name = namer.naming(*nptr);
                if (!name) {
                    continue;
                }
                const std::string k = name->toXPath();
                auto itDup = key_to_idx.find(k);
                if (itDup != key_to_idx.end()) {
                    const size_t idx = itDup->second;
                    out.node_groups[idx].push_back(nptr);
                    out.namelet_groups[idx].push_back(nl);
                    continue;
                }
                const size_t idx = out.names.size();
                key_to_idx[k] = idx;
                out.names.push_back(std::move(name));
                out.node_groups.emplace_back();
                out.namelet_groups.emplace_back();
                out.node_groups[idx].push_back(nptr);
                out.namelet_groups[idx].push_back(nl);
            }
        }

        sortNamingResultLikeGUITree(out);
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
        const uint32_t m = 1u << static_cast<unsigned>(NamerType::TYPE);
        NamerPtr namer = NamerFactory::CURRENT.getByMask(m);
        if (!namer) {
            return nullptr;
        }
        std::vector<NameletPtr> v;
        v.push_back(std::make_shared<Namelet>("//*", std::move(namer)));
        return std::make_shared<Naming>(std::move(v));
    }

    NamingPtr NamingFactory::refineNaming(const NamingPtr &naming, const NamerLattice &lattice) {
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
            std::vector<NamerPtr> refs = lattice.immediateRefinements(namer);
            if (refs.empty()) {
                continue;
            }
            NamerPtr finer = refs[0];
            std::vector<NameletPtr> newlets;
            newlets.reserve(namelets.size());
            for (size_t j = 0; j < namelets.size(); ++j) {
                if (j == i) {
                    newlets.push_back(std::make_shared<Namelet>(namelets[j]->getExprString(), finer));
                } else {
                    newlets.push_back(
                        std::make_shared<Namelet>(namelets[j]->getExprString(), namelets[j]->getNamerPtr()));
                }
            }
            return std::make_shared<Naming>(std::move(newlets));
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
        uintptr_t prevHash = StateKey::fromGUITree(tree).hash();
        for (int s = 0; s < max_steps; ++s) {
            NamingPtr next = refineNaming(cur, lattice);
            if (!next) {
                break;
            }
            if (!rebuildTree(next, tree, dom)) {
                return nullptr;
            }
            const uintptr_t h = StateKey::fromGUITree(tree).hash();
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

} // namespace naming
} // namespace fastbotx
