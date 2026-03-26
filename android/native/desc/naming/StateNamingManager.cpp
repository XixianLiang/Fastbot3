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

#include "StateNamingManager.h"
#include "NamingFactory.h"
#include "StateKey.h"
#include "NamerFactory.h"
#include "NamerLattice.h"
#include "../gui_tree/GUITree.h"
#include "../xpath/XPathNodeMapper.h"

#include <cassert>
#include <set>

namespace fastbotx {
namespace naming {
namespace {
    constexpr bool kEnableApeNamingManagerDebugAssert = false;

    std::string activityKeyFromTree(const gui_tree::GUITree &tree) {
        return StateKey::activityFromPackageAndClass(tree.getActivityPackageName(), tree.getActivityClassName());
    }

    NamingEdge inferRefineEdge(const NamingPtr &from, const NamingPtr &to) {
        NamingEdge edge{};
        if (!from || !to) {
            return edge;
        }
        const auto &a = from->getNamelets();
        const auto &b = to->getNamelets();
        const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i) {
            if (a[i] != b[i]) {
                edge.from = a[i];
                edge.to = b[i];
                return edge;
            }
        }
        return edge;
    }

    bool isAncestorNaming(const NamingPtr &ancestor, const NamingPtr &node) {
        if (!ancestor || !node) {
            return false;
        }
        NamingPtr cur = node->getParent();
        while (cur) {
            if (cur == ancestor) {
                return true;
            }
            cur = cur->getParent();
        }
        return false;
    }

    bool isDirectChildOf(const NamingPtr &parent, const NamingPtr &child) {
        return parent && child && child->getParent() == parent;
    }

} // namespace

    StateNamingManager::StateNamingManager(std::shared_ptr<ActivityNamingManager> activity_mgr)
        : activity_mgr_(std::move(activity_mgr)) {
        if (!activity_mgr_) {
            activity_mgr_ = std::make_shared<ActivityNamingManager>();
        }
    }

    ActivityNamingManager &StateNamingManager::activityManager() { return *activity_mgr_; }

    const ActivityNamingManager &StateNamingManager::activityManager() const { return *activity_mgr_; }

    NamingPtr StateNamingManager::getNamingForActivity(const std::string &activity_key) const {
        return activity_mgr_->getNaming(activity_key);
    }

    void StateNamingManager::updateNaming(const std::string &activity_key, NamingUpdateKind kind, NamingPtr n) {
        NamingPtr current = activity_mgr_->getNaming(activity_key);
        updateNaming(activity_key, kind, current, std::move(n));
    }

    void StateNamingManager::updateNaming(const std::string &activity_key, NamingUpdateKind kind,
                                          const NamingPtr &old_n, NamingPtr new_n) {
        if (!new_n) {
            return;
        }
        if (!old_n || old_n == new_n) {
            activity_mgr_->setNaming(activity_key, std::move(new_n));
            return;
        }

        if (kind == NamingUpdateKind::Refine) {
            if (!isDirectChildOf(old_n, new_n)) {
                return;
            }
            NamingEdge edge = inferRefineEdge(old_n, new_n);
            if (!edge.from || !edge.to) {
                return;
            }
            old_n->addRefinementChild(edge, new_n);
            activity_mgr_->setNaming(activity_key, std::move(new_n));
            return;
        }

        // NamingUpdateKind::Abstract
        if (new_n->getParent() == old_n->getParent()) {
            // sibling replace
            NamingPtr parent = new_n->getParent();
            if (!parent) {
                return;
            }
            NamingEdge oldEdge{}, newEdge{};
            if (!namingToEdge(parent, old_n, &oldEdge)) {
                return;
            }
            newEdge = inferRefineEdge(parent, new_n);
            if (!newEdge.from || !newEdge.to) {
                return;
            }
            parent->addRefinementChild(newEdge, new_n);
            activity_mgr_->setNaming(activity_key, std::move(new_n));
            return;
        }

        if (!isAncestorNaming(new_n, old_n) && old_n->getParent() != new_n) {
            return;
        }
        activity_mgr_->setNaming(activity_key, std::move(new_n));
    }

    void StateNamingManager::updateNamingWithStateKey(const std::string &activity_key, NamingUpdateKind kind,
                                                      const NamingPtr &old_n, NamingPtr new_n,
                                                      const StateKey &state_key) {
        if (!old_n || !new_n) {
            return;
        }
        auto upsertExactStateEdge = [this](const NamingPtr &source, const StateKey &key,
                                           const NamingPtr &target) {
            if (!source || !target) {
                return;
            }
            auto &bucket = naming_to_edge_[source][key.hash()];
            for (auto &entry : bucket) {
                if (entry.state_key == key) {
                    entry.target = target;
                    return;
                }
            }
            bucket.push_back(StateEdgeEntry{key, target});
        };
        auto eraseExactStateEdge = [this](const NamingPtr &source, const StateKey &key,
                                          const NamingPtr &expected_target) {
            auto itSrc = naming_to_edge_.find(source);
            if (itSrc == naming_to_edge_.end()) {
                return;
            }
            auto itHash = itSrc->second.find(key.hash());
            if (itHash == itSrc->second.end()) {
                return;
            }
            auto &bucket = itHash->second;
            bucket.erase(std::remove_if(bucket.begin(), bucket.end(), [&](const StateEdgeEntry &e) {
                if (!(e.state_key == key)) {
                    return false;
                }
                return !expected_target || e.target == expected_target;
            }), bucket.end());
            if (bucket.empty()) {
                itSrc->second.erase(itHash);
            }
            if (itSrc->second.empty()) {
                naming_to_edge_.erase(itSrc);
            }
        };
        updateNaming(activity_key, kind, old_n, new_n);
        if (activity_mgr_->getNaming(activity_key) != new_n) {
            return;
        }
        if (kind == NamingUpdateKind::Refine && isDirectChildOf(old_n, new_n)) {
            upsertExactStateEdge(old_n, state_key, new_n);
            if (kEnableApeNamingManagerDebugAssert) {
                assert(getNamingByStateKey(old_n, state_key) == new_n);
            }
            return;
        }
        if (kind == NamingUpdateKind::Abstract) {
            if (new_n->getParent() == old_n->getParent()) {
                auto isLeafByStateEdges = [this](const NamingPtr &n) {
                    if (!n) return false;
                    auto it = naming_to_edge_.find(n);
                    return it == naming_to_edge_.end() || it->second.empty();
                };
                if (!isLeafByStateEdges(old_n) || !isLeafByStateEdges(new_n)) {
                    return;
                }
                NamingPtr parent = new_n->getParent();
                if (!parent) {
                    return;
                }
                upsertExactStateEdge(parent, state_key, new_n);
                return;
            }
            NamingPtr child = old_n;
            NamingPtr parent = child->getParent();
            while (parent) {
                eraseExactStateEdge(parent, state_key, child);
                if (parent == new_n) {
                    break;
                }
                child = parent;
                parent = child->getParent();
            }
        }
    }

    void StateNamingManager::updateNamingWithStateHash(const std::string &activity_key, NamingUpdateKind kind,
                                                       const NamingPtr &old_n, NamingPtr new_n,
                                                       uintptr_t state_key_hash) {
        if (!old_n || !new_n) {
            return;
        }
        // Keep structural constraints in one place.
        updateNaming(activity_key, kind, old_n, new_n);
        if (activity_mgr_->getNaming(activity_key) != new_n) {
            // Structural update rejected by relation constraints.
            return;
        }
        // Fallback mode: when state hash is absent, keep only structural naming update.
        if (state_key_hash == 0) {
            return;
        }

        if (kind == NamingUpdateKind::Refine && isDirectChildOf(old_n, new_n)) {
            naming_to_edge_hash_only_[old_n][state_key_hash] = new_n;
            if (kEnableApeNamingManagerDebugAssert) {
                assert(getNamingByStateHash(old_n, state_key_hash) == new_n);
            }
            return;
        }
        if (kind == NamingUpdateKind::Abstract) {
            if (new_n->getParent() == old_n->getParent()) {
                // APE sibling replace: only leaf supports replacement.
                auto isLeafByStateEdges = [this](const NamingPtr &n) {
                    if (!n) {
                        return false;
                    }
                    auto it = naming_to_edge_.find(n);
                    return it == naming_to_edge_.end() || it->second.empty();
                };
                if (!isLeafByStateEdges(old_n) || !isLeafByStateEdges(new_n)) {
                    return;
                }
                NamingPtr parent = new_n->getParent();
                if (!parent) {
                    return;
                }
                naming_to_edge_hash_only_[parent][state_key_hash] = new_n;
                if (kEnableApeNamingManagerDebugAssert) {
                    assert(getNamingByStateHash(parent, state_key_hash) == new_n);
                }
                return;
            }

            // APE ancestor abstraction: remove state edges along old -> ... -> new path.
            NamingPtr child = old_n;
            NamingPtr parent = child->getParent();
            while (parent) {
                auto it = naming_to_edge_hash_only_.find(parent);
                if (it != naming_to_edge_hash_only_.end()) {
                    auto jt = it->second.find(state_key_hash);
                    if (jt != it->second.end() && jt->second == child) {
                        it->second.erase(jt);
                    }
                    if (it->second.empty()) {
                        naming_to_edge_hash_only_.erase(it);
                    }
                }
                if (parent == new_n) {
                    break;
                }
                child = parent;
                parent = child->getParent();
            }
        }
    }

    bool StateNamingManager::namingToEdge(const NamingPtr &from, const NamingPtr &to, NamingEdge *out_edge) const {
        if (!from || !to || !out_edge) {
            return false;
        }
        auto itStored = naming_to_edge_.find(from);
        if (itStored != naming_to_edge_.end()) {
            for (const auto &kv : itStored->second) {
                for (const auto &entry : kv.second) {
                    if (entry.target == to) {
                        *out_edge = inferRefineEdge(from, to);
                        return out_edge->from && out_edge->to;
                    }
                }
            }
        }
        for (const auto &p : from->getRefinementChildren()) {
            if (p.second == to) {
                *out_edge = p.first;
                return true;
            }
        }
        return false;
    }

    NamingPtr StateNamingManager::getNamingByStateHash(const NamingPtr &source, uintptr_t state_key_hash) const {
        if (!source || state_key_hash == 0) {
            return nullptr;
        }
        auto it = naming_to_edge_.find(source);
        if (it != naming_to_edge_.end()) {
            auto jt = it->second.find(state_key_hash);
            if (jt != it->second.end() && jt->second.size() == 1) {
                const_cast<StateNamingManager *>(this)->lookup_stats_.exact_hit++;
                return jt->second.front().target;
            }
        }
        auto itFallback = naming_to_edge_hash_only_.find(source);
        if (itFallback == naming_to_edge_hash_only_.end()) {
            const_cast<StateNamingManager *>(this)->lookup_stats_.miss++;
            return nullptr;
        }
        auto jtFallback = itFallback->second.find(state_key_hash);
        if (jtFallback == itFallback->second.end()) {
            const_cast<StateNamingManager *>(this)->lookup_stats_.miss++;
            return nullptr;
        }
        const_cast<StateNamingManager *>(this)->lookup_stats_.hash_only_hit++;
        return jtFallback->second;
    }

    NamingPtr StateNamingManager::getNamingByStateKey(const NamingPtr &source, const StateKey &state_key) const {
        if (!source) {
            return nullptr;
        }
        auto it = naming_to_edge_.find(source);
        if (it != naming_to_edge_.end()) {
            auto jt = it->second.find(state_key.hash());
            if (jt != it->second.end()) {
                for (const auto &entry : jt->second) {
                    if (entry.state_key == state_key) {
                        const_cast<StateNamingManager *>(this)->lookup_stats_.exact_hit++;
                        return entry.target;
                    }
                }
            }
        }
        // Compatibility fallback when only hash was available at update time.
        auto itFallback = naming_to_edge_hash_only_.find(source);
        if (itFallback == naming_to_edge_hash_only_.end()) {
            const_cast<StateNamingManager *>(this)->lookup_stats_.miss++;
            return nullptr;
        }
        auto jtFallback = itFallback->second.find(state_key.hash());
        if (jtFallback == itFallback->second.end()) {
            const_cast<StateNamingManager *>(this)->lookup_stats_.miss++;
            return nullptr;
        }
        const_cast<StateNamingManager *>(this)->lookup_stats_.hash_only_hit++;
        return jtFallback->second;
    }

    NamingPtr StateNamingManager::treeToNaming(const gui_tree::GUITree &tree) {
        NamingPtr source = activity_mgr_->getNaming(activityKeyFromTree(tree));
        if (!source) {
            return NamingFactory::defaultRootNaming();
        }
        // APE-style iterative edge walk with loop guard.
        StateKey state = StateKey::fromGUITree(tree);
        if (state.hash() == 0) {
            return source;
        }
        std::set<NamingPtr, std::owner_less<NamingPtr>> visited;
        visited.insert(source);
        while (true) {
            NamingPtr next = getNamingByStateKey(source, state);
            if (!next) {
                break;
            }
            if (visited.find(next) != visited.end()) {
                break;
            }
            visited.insert(next);
            source = next;
        }
        return source;
    }

    NamingPtr StateNamingManager::treeToNaming(gui_tree::GUITree &tree,
                                               const std::shared_ptr<gui_tree::XPathNodeMapper> &dom) {
        if (!dom) {
            return treeToNaming(static_cast<const gui_tree::GUITree &>(tree));
        }
        NamingPtr source = activity_mgr_->getNaming(activityKeyFromTree(tree));
        if (!source) {
            source = NamingFactory::defaultRootNaming();
            if (!source) {
                return nullptr;
            }
        }
        std::set<NamingPtr, std::owner_less<NamingPtr>> visited;
        visited.insert(source);
        while (true) {
            if (!NamingFactory::rebuildTree(source, tree, dom)) {
                break;
            }
            StateKey state = StateKey::fromGUITree(tree);
            if (state.hash() == 0) {
                break;
            }
            NamingPtr next = getNamingByStateKey(source, state);
            if (!next || visited.find(next) != visited.end()) {
                break;
            }
            visited.insert(next);
            source = next;
        }
        return source;
    }

    NamingPtr StateNamingManager::getNamingFixedPoint(const std::string &activity_key,
                                                      const gui_tree::GUITree & /*tree*/, int max_iter) {
        NamingPtr n = activity_mgr_->getNaming(activity_key);
        if (!n && max_iter > 0) {
            return NamingFactory::defaultRootNaming();
        }
        return n;
    }

    NamingPtr StateNamingManager::getNamingFixedPoint(const std::string &activity_key, gui_tree::GUITree &tree,
                                                      const std::shared_ptr<gui_tree::XPathNodeMapper> &dom,
                                                      int max_iter) {
        if (!dom) {
            return getNamingFixedPoint(activity_key, tree, max_iter);
        }
        NamingPtr n = treeToNaming(tree, dom);
        if (!n) return nullptr;
        const int steps = max_iter > 0 ? max_iter : 64;
        NamerLattice lat(NamerFactory::current());
        n = NamingFactory::batchRefineWithRebuildFixedPoint(n, lat, tree, dom, steps);
        if (!n) {
            return nullptr;
        }
        activity_mgr_->setNaming(activity_key, n);
        return n;
    }

    StateNamingManager::EdgeLookupStats StateNamingManager::consumeEdgeLookupStats() {
        EdgeLookupStats out = lookup_stats_;
        lookup_stats_ = EdgeLookupStats{};
        return out;
    }

} // namespace naming
} // namespace fastbotx
