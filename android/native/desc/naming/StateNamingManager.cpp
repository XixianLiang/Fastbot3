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
/**
 * Implements activity-keyed naming updates, state-keyed refinement edges, `treeToNaming` graph walks,
 * and diagnostic counters. Core lattice operations delegate to `NamingFactory` / `NamerLattice`.
 */

#include "StateNamingManager.h"
#include "NamingFactory.h"
#include "StateKey.h"
#include "NamerFactory.h"
#include "NamerLattice.h"
#include "../gui_tree/GUITree.h"
#include "../xpath/XPathNodeMapper.h"
#include "../../utils.hpp"

#include <cassert>
#include <atomic>
#include <cinttypes>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace fastbotx {
namespace naming {
namespace {
    /** When true, assert lookup invariants after edge registration (debug builds only). */
    constexpr bool kEnableStateNamingManagerDebugAssert = false;

    std::atomic<uint64_t> g_u_reject_null_new{0};
    std::atomic<uint64_t> g_u_refine_not_direct_child{0};
    std::atomic<uint64_t> g_u_refine_infer_edge_failed{0};
    std::atomic<uint64_t> g_u_abstract_sibling_no_parent{0};
    std::atomic<uint64_t> g_u_abstract_sibling_naming_to_edge_failed{0};
    std::atomic<uint64_t> g_u_abstract_sibling_infer_edge_failed{0};
    std::atomic<uint64_t> g_u_abstract_sibling_not_leaf{0};
    std::atomic<uint64_t> g_u_abstract_relation_rejected{0};
    std::atomic<uint64_t> g_sk_not_applied{0};
    std::atomic<uint64_t> g_sk_abstract_leaf_blocked{0};
    std::atomic<uint64_t> g_sh_not_applied{0};
    std::atomic<uint64_t> g_sh_abstract_leaf_blocked{0};
    std::atomic<uint64_t> g_sk_null_arg{0};
    std::atomic<uint64_t> g_sh_null_arg{0};

    std::atomic<uint64_t> g_tt_dom_entries{0};
    std::atomic<uint64_t> g_tt_const_entries{0};
    std::atomic<uint64_t> g_tt_const_shadow_multi_hop{0};
    std::atomic<uint64_t> g_tt_const_shadow_mismatch{0};
    std::atomic<uint64_t> g_tt_rebuild_failed{0};
    std::atomic<uint64_t> g_tt_zero_hash{0};
    std::atomic<uint64_t> g_tt_no_successor{0};
    std::atomic<uint64_t> g_tt_cycle{0};
    std::atomic<uint64_t> g_tt_total_hops{0};
    std::atomic<uint64_t> g_fp_null_after_ttn{0};
    std::atomic<uint64_t> g_fp_batch_refine_failed{0};

    std::atomic<uint64_t> g_chain_wsk_refine_enter{0};
    std::atomic<uint64_t> g_chain_wsh_refine_enter{0};
    std::atomic<uint64_t> g_chain_refine_struct_ok{0};
    std::atomic<uint64_t> g_chain_wsk_state_edge_ok{0};
    std::atomic<uint64_t> g_chain_wsh_hash_edge_ok{0};

    bool stateNamingWantDetailLog(uint64_t count) {
        return count == 1 || count <= 3 || (count % 128) == 0;
    }

    /** Wider sampling for high-frequency chain/enter logs. */
    bool namingChainTraceLog(uint64_t seq) {
        return seq <= 12 || (seq % 512) == 0;
    }

    void logStateNamingSample(const char *tag, uint64_t count) {
        if (stateNamingWantDetailLog(count)) {
            BLOG("state naming diag [%s] count=%llu", tag, static_cast<unsigned long long>(count));
        }
    }

    std::string namingFpSnippet(const NamingPtr &n) {
        if (!n) {
            return std::string("-");
        }
        const std::string &s = n->fingerprintString();
        if (s.size() <= 200) {
            return s;
        }
        return s.substr(0, 200) + std::string("...");
    }

    uintptr_t namingFpHash(const NamingPtr &n) {
        if (!n) {
            return 0;
        }
        return static_cast<uintptr_t>(std::hash<std::string>{}(n->fingerprintString()));
    }

    size_t namingParentHopCount(const NamingPtr &n) {
        size_t h = 0;
        for (NamingPtr x = n; x; x = x->getParent()) {
            ++h;
        }
        return h;
    }

    void logRefineNotDirectChildDetail(uint64_t seq, const std::string &activity_key,
                                       const NamingPtr &old_n, const NamingPtr &new_n) {
        if (!stateNamingWantDetailLog(seq)) {
            return;
        }
        const NamingPtr new_parent = new_n ? new_n->getParent() : nullptr;
        const NamingPtr old_parent = old_n ? old_n->getParent() : nullptr;
        const int sibling = (old_parent && new_parent && old_parent == new_parent) ? 1 : 0;
        const int direct = (old_n && new_parent && new_parent == old_n) ? 1 : 0;
        int old_strict_anc_new = 0;
        if (old_n && new_n) {
            for (NamingPtr x = new_n->getParent(); x; x = x->getParent()) {
                if (x == old_n) {
                    old_strict_anc_new = 1;
                    break;
                }
            }
        }
        const uintptr_t old_fp_h = namingFpHash(old_n);
        const uintptr_t new_fp_h = namingFpHash(new_n);
        const uintptr_t par_fp_h = namingFpHash(new_parent);
        int par_fp_eq_old_fp = 0;
        if (old_n && new_parent) {
            par_fp_eq_old_fp = (old_n->fingerprintString() == new_parent->fingerprintString()) ? 1 : 0;
        }
        BDLOG(
            "state naming refine_not_child detail seq=%llu act=%s old=%p new=%p new_parent=%p "
            "direct_old_eq_parent=%d sibling_share_parent=%d old_hops=%zu new_hops=%zu par_hops=%zu "
            "old_anc_new=%d old_fp_h=%" PRIuPTR " new_fp_h=%" PRIuPTR " par_fp_h=%" PRIuPTR " "
            "par_fp_eq_old_fp=%d parent_fp=%s old_fp=%s new_fp=%s",
            static_cast<unsigned long long>(seq), activity_key.c_str(),
            static_cast<const void *>(old_n.get()), static_cast<const void *>(new_n.get()),
            static_cast<const void *>(new_parent.get()), direct, sibling,
            namingParentHopCount(old_n), namingParentHopCount(new_n), namingParentHopCount(new_parent),
            old_strict_anc_new, old_fp_h, new_fp_h, par_fp_h, par_fp_eq_old_fp,
            namingFpSnippet(new_parent).c_str(),
            namingFpSnippet(old_n).c_str(), namingFpSnippet(new_n).c_str());
    }

    void logStateKeyNotAppliedDetail(uint64_t seq, const std::string &activity_key,
                                     NamingUpdateKind kind, const NamingPtr &old_n, const NamingPtr &new_n,
                                     const StateKey &state_key, const NamingPtr &stored_naming) {
        if (!stateNamingWantDetailLog(seq)) {
            return;
        }
        std::string sk_nf = state_key.namingFingerprint();
        if (sk_nf.size() > 160) {
            sk_nf = sk_nf.substr(0, 160) + std::string("...");
        }
        const char *kind_s = (kind == NamingUpdateKind::Refine) ? "Refine" : "Abstract";
        const NamingPtr nnpar = new_n ? new_n->getParent() : nullptr;
        const int stored_is_old = (stored_naming == old_n) ? 1 : 0;
        const int sk_act_eq = (state_key.activity() == activity_key) ? 1 : 0;
        BDLOG(
            "state naming sk_not_applied detail seq=%llu act=%s sk_hash=%" PRIuPTR
            " kind=%s sk_act_eq_key=%d old=%p new=%p stored=%p stored_is_old=%d nnpar=%p "
            "old_fp=%s new_fp=%s stored_fp=%s nnpar_fp=%s sk_nfp=%s",
            static_cast<unsigned long long>(seq), activity_key.c_str(),
            static_cast<uintptr_t>(state_key.hash()), kind_s, sk_act_eq,
            static_cast<const void *>(old_n.get()), static_cast<const void *>(new_n.get()),
            static_cast<const void *>(stored_naming.get()), stored_is_old,
            static_cast<const void *>(nnpar.get()), namingFpSnippet(old_n).c_str(),
            namingFpSnippet(new_n).c_str(), namingFpSnippet(stored_naming).c_str(),
            namingFpSnippet(nnpar).c_str(), sk_nf.c_str());
    }

    /** Canonical activity string derived from the tree’s package/class fields. */
    std::string activityKeyFromTree(const gui_tree::GUITree &tree) {
        return StateKey::activityFromPackageAndClass(tree.getActivityPackageName(), tree.getActivityClassName());
    }

    /** Infers the refinement `(fromNamelet,toNamelet)` edge by first differing namelet or appended tail. */
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
        // Lattice refinement often appends one Namelet: prefix matches `from`, `to` has +1 entry
        // (makeLatticeRefinementChild). First-differing-index loop never runs; use probe parent link.
        if (b.size() == a.size() + 1) {
            NameletPtr toNl = b.back();
            NameletPtr fromNl = toNl ? toNl->getParent() : nullptr;
            if (fromNl && toNl) {
                edge.from = fromNl;
                edge.to = toNl;
            }
        }
        return edge;
    }

    /** True if `ancestor` appears on the parent chain above `node`. */
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

    /** True when `child->getParent()` equals `parent`. */
    bool isDirectChildOf(const NamingPtr &parent, const NamingPtr &child) {
        return parent && child && child->getParent() == parent;
    }

    void logStateHashNotAppliedDetail(uint64_t seq, const std::string &activity_key,
                                      NamingUpdateKind kind, const NamingPtr &old_n, const NamingPtr &new_n,
                                      uintptr_t state_key_hash, const NamingPtr &stored_naming) {
        if (!stateNamingWantDetailLog(seq)) {
            return;
        }
        const NamingPtr nnpar = new_n ? new_n->getParent() : nullptr;
        const int stored_is_old = (stored_naming == old_n) ? 1 : 0;
        const char *kind_s = (kind == NamingUpdateKind::Refine) ? "Refine" : "Abstract";
        BDLOG(
            "state naming sh_not_applied detail seq=%llu act=%s skh=%" PRIuPTR
            " kind=%s old=%p new=%p stored=%p stored_is_old=%d nnpar=%p direct_child=%d "
            "old_fp=%s new_fp=%s nnpar_fp=%s",
            static_cast<unsigned long long>(seq), activity_key.c_str(),
            static_cast<uintptr_t>(state_key_hash), kind_s, static_cast<const void *>(old_n.get()),
            static_cast<const void *>(new_n.get()), static_cast<const void *>(stored_naming.get()),
            stored_is_old, static_cast<const void *>(nnpar.get()),
            isDirectChildOf(old_n, new_n) ? 1 : 0, namingFpSnippet(old_n).c_str(),
            namingFpSnippet(new_n).c_str(), namingFpSnippet(nnpar).c_str());
    }

    void logRefineInferEdgeFailedDetail(uint64_t seq, const std::string &activity_key,
                                        const NamingPtr &old_n, const NamingPtr &new_n) {
        if (!stateNamingWantDetailLog(seq)) {
            return;
        }
        const size_t na = old_n ? old_n->getNamelets().size() : 0;
        const size_t nb = new_n ? new_n->getNamelets().size() : 0;
        size_t same_prefix = 0;
        if (old_n && new_n) {
            const auto &a = old_n->getNamelets();
            const auto &b = new_n->getNamelets();
            const size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; ++i) {
                if (a[i] == b[i]) {
                    ++same_prefix;
                } else {
                    break;
                }
            }
        }
        size_t first_mismatch_idx = 0;
        bool has_mismatch = false;
        bool size_is_plus_1 = (new_n && old_n) ? (nb == na + 1) : false;
        int toNl_parent_exists = 0;
        bool no_mismatch = false;

        if (old_n && new_n) {
            const auto &a = old_n->getNamelets();
            const auto &b = new_n->getNamelets();
            const size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; ++i) {
                if (a[i] != b[i]) {
                    has_mismatch = true;
                    first_mismatch_idx = i;
                    break;
                }
            }
            if (!has_mismatch) {
                // No mismatch in the overlapped region: the mismatch can only come from size difference.
                no_mismatch = true;
            }
            if (size_is_plus_1) {
                // inferRefineEdge's "append one Namelet" path uses new_n's last Namelet's parent.
                const NameletPtr toNl = nb ? new_n->getNamelets().back() : nullptr;
                if (toNl) {
                    toNl_parent_exists = toNl->getParent() ? 1 : 0;
                }
            }
        }
        BDLOG(
            "state naming refine_infer_edge_fail detail seq=%llu act=%s na=%zu nb=%zu same_prefix=%zu "
            "direct_child=%d size_is_plus_1=%d no_mismatch=%d first_mismatch_idx=%zu toNl_parent_exists=%d "
            "old_fp=%s new_fp=%s",
            static_cast<unsigned long long>(seq), activity_key.c_str(), na, nb, same_prefix,
            isDirectChildOf(old_n, new_n) ? 1 : 0, size_is_plus_1 ? 1 : 0,
            no_mismatch ? 1 : 0, first_mismatch_idx, toNl_parent_exists,
            namingFpSnippet(old_n).c_str(), namingFpSnippet(new_n).c_str());
    }

} // namespace

    /** Ensures a non-null `ActivityNamingManager`, default-constructing one if missing. */
    StateNamingManager::StateNamingManager(std::shared_ptr<ActivityNamingManager> activity_mgr)
        : activity_mgr_(std::move(activity_mgr)) {
        if (!activity_mgr_) {
            activity_mgr_ = std::make_shared<ActivityNamingManager>();
        }
    }

    ActivityNamingManager &StateNamingManager::activityManager() { return *activity_mgr_; }

    const ActivityNamingManager &StateNamingManager::activityManager() const { return *activity_mgr_; }

    /** No outgoing state-key edges from `n` in either exact or hash-only maps. */
    bool StateNamingManager::isStateGraphLeaf(const NamingPtr &n) const {
        if (!n) {
            return true;
        }
        auto it = naming_to_edge_.find(n);
        if (it != naming_to_edge_.end()) {
            for (const auto &kv : it->second) {
                if (!kv.second.empty()) {
                    return false;
                }
            }
        }
        auto ith = naming_to_edge_hash_only_.find(n);
        if (ith != naming_to_edge_hash_only_.end() && !ith->second.empty()) {
            return false;
        }
        return true;
    }

    NamingPtr StateNamingManager::getNamingForActivity(const std::string &activity_key) const {
        return activity_mgr_->getNaming(activity_key);
    }

    /** Delegates to the `(old, new)` overload using the current activity naming as `old_n`. */
    void StateNamingManager::updateNaming(const std::string &activity_key, NamingUpdateKind kind, NamingPtr n) {
        NamingPtr current = activity_mgr_->getNaming(activity_key);
        updateNaming(activity_key, kind, current, std::move(n));
    }

    /**
     * Refine: require direct child, infer edge, register `addRefinementChild`. Abstract: ancestor shortcut,
     * sibling replacement with leaf checks, or reject.
     */
    void StateNamingManager::updateNaming(const std::string &activity_key, NamingUpdateKind kind,
                                          const NamingPtr &old_n, NamingPtr new_n) {
        if (!new_n) {
            const uint64_t c = ++g_u_reject_null_new;
            logStateNamingSample("update_reject_null_new", c);
            return;
        }
        if (!old_n || old_n == new_n) {
            activity_mgr_->setNaming(activity_key, std::move(new_n));
            return;
        }

        if (kind == NamingUpdateKind::Refine) {
            {
                static std::atomic<uint64_t> g_updateNaming_refine_entry_diag{0};
                const uint64_t ud = ++g_updateNaming_refine_entry_diag;
                if (ud <= 80 || (ud % 400) == 0) {
                    const NamingPtr old_par = old_n ? old_n->getParent() : nullptr;
                    const NamingPtr new_par = new_n ? new_n->getParent() : nullptr;
                    const int direct_child = isDirectChildOf(old_n, new_n) ? 1 : 0;
                    const int sibling_share_parent =
                        (old_par && new_par && old_par == new_par) ? 1 : 0;
                    BDLOG(
                        "state naming diag [updateNaming-enter Refine] seq=%llu act=%s old=%p new=%p "
                        "oldPar=%p newPar=%p direct_child=%d sibling_share_parent=%d oldFin=%d newFin=%d "
                        "old_fp_h=%" PRIuPTR " new_fp_h=%" PRIuPTR,
                        static_cast<unsigned long long>(ud), activity_key.c_str(),
                        static_cast<const void *>(old_n.get()), static_cast<const void *>(new_n.get()),
                        static_cast<const void *>(old_par.get()), static_cast<const void *>(new_par.get()),
                        direct_child, sibling_share_parent, old_n->getFineness(),
                        new_n->getFineness(), namingFpHash(old_n), namingFpHash(new_n));
                }
            }
            // Refine updates require a direct lattice parent link: `new_n`’s parent must be `old_n`.
            if (!isDirectChildOf(old_n, new_n)) {
                const uint64_t c = ++g_u_refine_not_direct_child;
                logStateNamingSample("update_refine_not_direct_child", c);
                logRefineNotDirectChildDetail(c, activity_key, old_n, new_n);
                return;
            }
            NamingEdge edge = inferRefineEdge(old_n, new_n);
            if (edge.from && edge.to) {
                old_n->addRefinementChild(edge, new_n);
            } else {
                const uint64_t c = ++g_u_refine_infer_edge_failed;
                logStateNamingSample("update_refine_infer_edge_failed", c);
                logRefineInferEdgeFailedDetail(c, activity_key, old_n, new_n);
            }
            const int newFinForLog = new_n->getFineness();
            const void *const newPtrForLog = static_cast<const void *>(new_n.get());
            activity_mgr_->setNaming(activity_key, std::move(new_n));
            {
                const uint64_t s = ++g_chain_refine_struct_ok;
                if (namingChainTraceLog(s)) {
                    BDLOG(
                        "state naming chain: updateNaming Refine applied act=%s old=%p new=%p "
                        "oldFin=%d newFin=%d",
                        activity_key.c_str(), static_cast<const void *>(old_n.get()), newPtrForLog,
                        old_n->getFineness(), newFinForLog);
                }
            }
            return;
        }

        // Abstract branch order: strict ancestor shortcut, then sibling abstract with leaf checks.
        if (isAncestorNaming(new_n, old_n)) {
            activity_mgr_->setNaming(activity_key, std::move(new_n));
            return;
        }
        if (new_n->getParent() == old_n->getParent()) {
            NamingPtr parent = new_n->getParent();
            if (!parent) {
                const uint64_t c = ++g_u_abstract_sibling_no_parent;
                logStateNamingSample("update_abstract_sibling_no_parent", c);
                return;
            }
            if (!isStateGraphLeaf(old_n) || !isStateGraphLeaf(new_n)) {
                const uint64_t c = ++g_u_abstract_sibling_not_leaf;
                logStateNamingSample("update_abstract_sibling_not_leaf", c);
                return;
            }
            NamingEdge oldEdge{}, newEdge{};
            if (!namingToEdge(parent, old_n, &oldEdge)) {
                const uint64_t c = ++g_u_abstract_sibling_naming_to_edge_failed;
                logStateNamingSample("update_abstract_sibling_naming_to_edge_failed", c);
            }
            newEdge = inferRefineEdge(parent, new_n);
            if (newEdge.from && newEdge.to) {
                parent->addRefinementChild(newEdge, new_n);
            } else {
                const uint64_t c = ++g_u_abstract_sibling_infer_edge_failed;
                logStateNamingSample("update_abstract_sibling_infer_edge_failed", c);
            }
            activity_mgr_->setNaming(activity_key, std::move(new_n));
            return;
        }

        const uint64_t c = ++g_u_abstract_relation_rejected;
        logStateNamingSample("update_abstract_relation_rejected", c);
    }

    /** Runs `updateNaming` then, if applied, upserts or erases exact `StateKey` edges on the refinement graph. */
    void StateNamingManager::updateNamingWithStateKey(const std::string &activity_key, NamingUpdateKind kind,
                                                      const NamingPtr &old_n, NamingPtr new_n,
                                                      const StateKey &state_key) {
        if (!old_n || !new_n) {
            const uint64_t c = ++g_sk_null_arg;
            logStateNamingSample("statekey_skipped_null_arg", c);
            return;
        }
        if (kind == NamingUpdateKind::Refine) {
            {
                static std::atomic<uint64_t> g_updateNamingWithStateKey_refine_entry_diag{0};
                const uint64_t ud = ++g_updateNamingWithStateKey_refine_entry_diag;
                if (ud <= 80 || (ud % 400) == 0) {
                    const NamingPtr old_par = old_n ? old_n->getParent() : nullptr;
                    const NamingPtr new_par = new_n ? new_n->getParent() : nullptr;
                    const int direct_child = isDirectChildOf(old_n, new_n) ? 1 : 0;
                    const int sibling_share_parent =
                        (old_par && new_par && old_par == new_par) ? 1 : 0;
                    BDLOG(
                        "state naming diag [updateNamingWithStateKey-enter Refine] seq=%llu act=%s sk_h=%"
                        PRIuPTR " old=%p new=%p oldPar=%p newPar=%p direct_child=%d sibling_share_parent=%d "
                        "oldFin=%d newFin=%d old_fp_h=%" PRIuPTR " new_fp_h=%" PRIuPTR,
                        static_cast<unsigned long long>(ud), activity_key.c_str(),
                        static_cast<uintptr_t>(state_key.hash()),
                        static_cast<const void *>(old_n.get()), static_cast<const void *>(new_n.get()),
                        static_cast<const void *>(old_par.get()), static_cast<const void *>(new_par.get()),
                        direct_child, sibling_share_parent, old_n->getFineness(),
                        new_n->getFineness(), namingFpHash(old_n), namingFpHash(new_n));
                }
            }
            const uint64_t s = ++g_chain_wsk_refine_enter;
            if (namingChainTraceLog(s)) {
                const NamingPtr nnpar = new_n->getParent();
                BDLOG(
                    "state naming chain: updateWithStateKey enter Refine act=%s sk_h=%" PRIuPTR
                    " old=%p new=%p new_par=%p direct_child=%d",
                    activity_key.c_str(), static_cast<uintptr_t>(state_key.hash()),
                    static_cast<const void *>(old_n.get()), static_cast<const void *>(new_n.get()),
                    static_cast<const void *>(nnpar.get()),
                    isDirectChildOf(old_n, new_n) ? 1 : 0);
            }
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
        NamingPtr stored_after = activity_mgr_->getNaming(activity_key);
        if (stored_after != new_n) {
            const uint64_t c = ++g_sk_not_applied;
            logStateNamingSample("statekey_not_applied_after_update", c);
            logStateKeyNotAppliedDetail(c, activity_key, kind, old_n, new_n, state_key, stored_after);
            return;
        }
        if (kind == NamingUpdateKind::Refine && isDirectChildOf(old_n, new_n)) {
            upsertExactStateEdge(old_n, state_key, new_n);
            {
                const uint64_t u = ++g_chain_wsk_state_edge_ok;
                if (namingChainTraceLog(u)) {
                    BDLOG(
                        "state naming chain: updateWithStateKey state edge upsert Refine act=%s sk_h=%" PRIuPTR
                        " src=%p tgt=%p",
                        activity_key.c_str(), static_cast<uintptr_t>(state_key.hash()),
                        static_cast<const void *>(old_n.get()), static_cast<const void *>(new_n.get()));
                }
            }
            if (kEnableStateNamingManagerDebugAssert) {
                assert(getNamingByStateKey(old_n, state_key) == new_n);
            }
            return;
        }
        if (kind == NamingUpdateKind::Abstract) {
            if (new_n->getParent() == old_n->getParent()) {
                NamingPtr parent = new_n->getParent();
                if (parent) {
                    upsertExactStateEdge(parent, state_key, new_n);
                    {
                        const uint64_t u = ++g_chain_wsk_state_edge_ok;
                        if (namingChainTraceLog(u)) {
                            BDLOG(
                                "state naming chain: updateWithStateKey state edge upsert Abstract(sibling) "
                                "act=%s sk_h=%" PRIuPTR " parent=%p tgt=%p",
                                activity_key.c_str(), static_cast<uintptr_t>(state_key.hash()),
                                static_cast<const void *>(parent.get()),
                                static_cast<const void *>(new_n.get()));
                        }
                    }
                }
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

    /** Same structural rules as `updateNamingWithStateKey` but stores hash-only buckets when full keys are absent. */
    void StateNamingManager::updateNamingWithStateHash(const std::string &activity_key, NamingUpdateKind kind,
                                                       const NamingPtr &old_n, NamingPtr new_n,
                                                       uintptr_t state_key_hash) {
        if (!old_n || !new_n) {
            const uint64_t c = ++g_sh_null_arg;
            logStateNamingSample("statehash_skipped_null_arg", c);
            return;
        }
        if (kind == NamingUpdateKind::Refine) {
            const uint64_t s = ++g_chain_wsh_refine_enter;
            if (namingChainTraceLog(s)) {
                const NamingPtr nnpar = new_n->getParent();
                BDLOG(
                    "state naming chain: updateWithStateHash enter Refine act=%s skh=%" PRIuPTR
                    " old=%p new=%p new_par=%p direct_child=%d",
                    activity_key.c_str(), static_cast<uintptr_t>(state_key_hash),
                    static_cast<const void *>(old_n.get()), static_cast<const void *>(new_n.get()),
                    static_cast<const void *>(nnpar.get()),
                    isDirectChildOf(old_n, new_n) ? 1 : 0);
            }
        }
        // Keep structural constraints in one place.
        updateNaming(activity_key, kind, old_n, new_n);
        NamingPtr stored_after_sh = activity_mgr_->getNaming(activity_key);
        if (stored_after_sh != new_n) {
            const uint64_t c = ++g_sh_not_applied;
            logStateNamingSample("statehash_not_applied_after_update", c);
            logStateHashNotAppliedDetail(c, activity_key, kind, old_n, new_n, state_key_hash,
                                         stored_after_sh);
            return;
        }
        // Fallback mode: when state hash is absent, keep only structural naming update.
        if (state_key_hash == 0) {
            return;
        }

        if (kind == NamingUpdateKind::Refine && isDirectChildOf(old_n, new_n)) {
            naming_to_edge_hash_only_[old_n][state_key_hash] = new_n;
            {
                const uint64_t u = ++g_chain_wsh_hash_edge_ok;
                if (namingChainTraceLog(u)) {
                    BDLOG(
                        "state naming chain: updateWithStateHash hash edge set Refine act=%s skh=%" PRIuPTR
                        " src=%p tgt=%p",
                        activity_key.c_str(), static_cast<uintptr_t>(state_key_hash),
                        static_cast<const void *>(old_n.get()), static_cast<const void *>(new_n.get()));
                }
            }
            if (kEnableStateNamingManagerDebugAssert) {
                assert(getNamingByStateHash(old_n, state_key_hash) == new_n);
            }
            return;
        }
        if (kind == NamingUpdateKind::Abstract) {
            if (new_n->getParent() == old_n->getParent()) {
                NamingPtr parent = new_n->getParent();
                if (parent) {
                    naming_to_edge_hash_only_[parent][state_key_hash] = new_n;
                    if (kEnableStateNamingManagerDebugAssert) {
                        assert(getNamingByStateHash(parent, state_key_hash) == new_n);
                    }
                }
                return;
            }

            // Remove state edges along old -> ... -> new path.
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

    /** Resolves the `NamingEdge` connecting `from` to `to` via stored state maps or direct refinement children. */
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

    /** Hash-bucket lookup: prefers a unique exact entry, else the hash-only map (updates `lookup_stats_`). */
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

    /** Exact `StateKey` match inside the hash bucket; does not fall back to hash-only edges. */
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
        // Correctness constraint: when a full StateKey is available, do NOT fall back to hash-only edges.
        // Hash-only mapping may collide and must not decide the refinement path.
        const_cast<StateNamingManager *>(this)->lookup_stats_.miss++;
        return nullptr;
    }

    /** Const overload: single hop along exact state key (diagnostic shadow compares multi-hop reachability). */
    NamingPtr StateNamingManager::treeToNaming(const gui_tree::GUITree &tree) {
        const uint64_t entrySeq = ++g_tt_const_entries;
        NamingPtr source = activity_mgr_->getNaming(activityKeyFromTree(tree));
        if (!source) {
            return NamingFactory::defaultRootNaming();
        }
        // Without a DOM we cannot rebuild the tree under each successor naming, so only resolve
        // the edge represented by the tree's current StateKey. The DOM overload handles multi-hop.
        const uintptr_t h = StateKey::hashFromGUITree(tree);
        if (h == 0) {
            return source;
        }
        StateKey state = StateKey::fromGUITree(tree);
        NamingPtr next = getNamingByStateKey(source, state);
        const NamingPtr oneHop = next ? next : source;

        // Diagnostic-only shadow walk: keep previous multi-hop behavior as an observer so we can
        // verify whether one-hop resolution truncates reachable successors.
        NamingPtr shadow = source;
        std::set<NamingPtr, std::owner_less<NamingPtr>> visited;
        visited.insert(shadow);
        uint64_t shadowHops = 0;
        while (true) {
            NamingPtr shadowNext = getNamingByStateKey(shadow, state);
            if (!shadowNext || visited.find(shadowNext) != visited.end()) {
                break;
            }
            visited.insert(shadowNext);
            shadow = shadowNext;
            ++shadowHops;
        }
        if (shadowHops > 1) {
            ++g_tt_const_shadow_multi_hop;
        }
        if (shadow != oneHop) {
            ++g_tt_const_shadow_mismatch;
        }
        if ((shadowHops > 1 || shadow != oneHop) &&
            (entrySeq <= 40 || (entrySeq % 400) == 0)) {
            const uint64_t mismatchCount = g_tt_const_shadow_mismatch.load(std::memory_order_relaxed);
            const uint64_t multiHopCount =
                g_tt_const_shadow_multi_hop.load(std::memory_order_relaxed);
            BDLOG("state naming diag [tree_to_naming_const_shadow] seq=%llu activity=%s "
                  "stateHash=%" PRIuPTR " oneHop=%p shadow=%p shadowHops=%llu mismatch=%d "
                  "mismatchCount=%llu multiHopCount=%llu sourceFp=%s oneHopFp=%s shadowFp=%s",
                  static_cast<unsigned long long>(entrySeq), activityKeyFromTree(tree).c_str(),
                  static_cast<uintptr_t>(h), static_cast<const void *>(oneHop.get()),
                  static_cast<const void *>(shadow.get()),
                  static_cast<unsigned long long>(shadowHops), (shadow != oneHop) ? 1 : 0,
                  static_cast<unsigned long long>(mismatchCount),
                  static_cast<unsigned long long>(multiHopCount), namingFpSnippet(source).c_str(),
                  namingFpSnippet(oneHop).c_str(), namingFpSnippet(shadow).c_str());
        }
        if (shadow != oneHop) {
            BDLOG("state naming BUG_PROBE [treeToNaming_const_truncated_to_onehop] seq=%llu "
                  "activity=%s stateHash=%" PRIuPTR " source=%p oneHop=%p shadow=%p "
                  "shadowHops=%llu will_return_oneHop=1 sourceFp=%s oneHopFp=%s shadowFp=%s",
                  static_cast<unsigned long long>(entrySeq), activityKeyFromTree(tree).c_str(),
                  static_cast<uintptr_t>(h), static_cast<const void *>(source.get()),
                  static_cast<const void *>(oneHop.get()), static_cast<const void *>(shadow.get()),
                  static_cast<unsigned long long>(shadowHops), namingFpSnippet(source).c_str(),
                  namingFpSnippet(oneHop).c_str(), namingFpSnippet(shadow).c_str());
        }

        return oneHop;
    }

    /** DOM overload: rebuild each hop, follow state edges until stop condition; returns final naming. */
    NamingPtr StateNamingManager::treeToNaming(gui_tree::GUITree &tree,
                                               const std::shared_ptr<gui_tree::XPathNodeMapper> &dom) {
        if (!dom) {
            return treeToNaming(static_cast<const gui_tree::GUITree &>(tree));
        }
        ++g_tt_dom_entries;
        NamingPtr source = activity_mgr_->getNaming(activityKeyFromTree(tree));
        if (!source) {
            source = NamingFactory::defaultRootNaming();
            if (!source) {
                return nullptr;
            }
        }
        std::set<NamingPtr, std::owner_less<NamingPtr>> visited;
        visited.insert(source);
        uint64_t hops = 0;
        while (true) {
            if (!NamingFactory::rebuildTree(source, tree, dom)) {
                const uint64_t c = ++g_tt_rebuild_failed;
                logStateNamingSample("tree_to_naming_rebuild_failed", c);
                break;
            }
            const uintptr_t h = StateKey::hashFromGUITree(tree);
            if (h == 0) {
                const uint64_t c = ++g_tt_zero_hash;
                logStateNamingSample("tree_to_naming_zero_state_hash", c);
                break;
            }
            StateKey state = StateKey::fromGUITree(tree);
            NamingPtr next = getNamingByStateKey(source, state);
            if (!next) {
                const uint64_t c = ++g_tt_no_successor;
                logStateNamingSample("tree_to_naming_no_successor", c);
                break;
            }
            if (visited.find(next) != visited.end()) {
                const uint64_t c = ++g_tt_cycle;
                logStateNamingSample("tree_to_naming_cycle_break", c);
                break;
            }
            visited.insert(next);
            source = next;
            ++hops;
        }
        g_tt_total_hops.fetch_add(hops, std::memory_order_relaxed);
        return source;
    }

    /** Without DOM: returns stored naming or default root when `max_iter > 0` and nothing cached. */
    NamingPtr StateNamingManager::getNamingFixedPoint(const std::string &activity_key,
                                                      const gui_tree::GUITree & /*tree*/, int max_iter) {
        NamingPtr n = activity_mgr_->getNaming(activity_key);
        if (!n && max_iter > 0) {
            return NamingFactory::defaultRootNaming();
        }
        return n;
    }

    /** `treeToNaming` then `batchRefineWithRebuildFixedPoint`; persists the result under `activity_key`. */
    NamingPtr StateNamingManager::getNamingFixedPoint(const std::string &activity_key, gui_tree::GUITree &tree,
                                                      const std::shared_ptr<gui_tree::XPathNodeMapper> &dom,
                                                      int max_iter) {
        if (!dom) {
            return getNamingFixedPoint(activity_key, tree, max_iter);
        }
        NamingPtr n = treeToNaming(tree, dom);
        if (!n) {
            const uint64_t c = ++g_fp_null_after_ttn;
            logStateNamingSample("fixed_point_null_after_tree_to_naming", c);
            return nullptr;
        }
        const int steps = max_iter > 0 ? max_iter : 64;
        NamerLattice lat(NamerFactory::current());
        n = NamingFactory::batchRefineWithRebuildFixedPoint(n, lat, tree, dom, steps);
        if (!n) {
            const uint64_t c = ++g_fp_batch_refine_failed;
            logStateNamingSample("fixed_point_batch_refine_failed", c);
            return nullptr;
        }
        activity_mgr_->setNaming(activity_key, n);
        return n;
    }

    /** Clears and returns edge lookup hit/miss counters for this manager instance. */
    StateNamingManager::EdgeLookupStats StateNamingManager::consumeEdgeLookupStats() {
        EdgeLookupStats out = lookup_stats_;
        lookup_stats_ = EdgeLookupStats{};
        return out;
    }

    /** Walks every reachable `Naming` graph under all activity roots and clears per-tree caches. */
    void StateNamingManager::releaseTreeCache(const gui_tree::GUITree &tree) {
        std::vector<NamingPtr> roots = activity_mgr_->getAllNamings();
        std::set<const Naming *> visited;
        std::vector<NamingPtr> stack;
        stack.reserve(64);
        for (const NamingPtr &root : roots) {
            if (!root) {
                continue;
            }
            stack.push_back(root);
            while (!stack.empty()) {
                NamingPtr cur = stack.back();
                stack.pop_back();
                if (!cur) {
                    continue;
                }
                const Naming *raw = cur.get();
                if (visited.count(raw) != 0) {
                    continue;
                }
                visited.insert(raw);
                cur->releaseTreeCache(tree);
                for (const auto &kv : cur->getRefinementChildren()) {
                    if (kv.second) {
                        stack.push_back(kv.second);
                    }
                }
            }
        }
    }

    /** Atomically reads and clears global update-reject diagnostics. */
    StateNamingUpdateRejectDiagStats StateNamingManager::consumeUpdateRejectDiagStats() {
        StateNamingUpdateRejectDiagStats s;
        s.update_reject_null_new = g_u_reject_null_new.exchange(0);
        s.update_refine_not_direct_child = g_u_refine_not_direct_child.exchange(0);
        s.update_refine_infer_edge_failed = g_u_refine_infer_edge_failed.exchange(0);
        s.update_abstract_sibling_no_parent = g_u_abstract_sibling_no_parent.exchange(0);
        s.update_abstract_sibling_naming_to_edge_failed =
            g_u_abstract_sibling_naming_to_edge_failed.exchange(0);
        s.update_abstract_sibling_infer_edge_failed = g_u_abstract_sibling_infer_edge_failed.exchange(0);
        s.update_abstract_sibling_not_leaf = g_u_abstract_sibling_not_leaf.exchange(0);
        s.update_abstract_relation_rejected = g_u_abstract_relation_rejected.exchange(0);
        s.statekey_not_applied_after_update = g_sk_not_applied.exchange(0);
        s.statekey_abstract_leaf_blocked = g_sk_abstract_leaf_blocked.exchange(0);
        s.statehash_not_applied_after_update = g_sh_not_applied.exchange(0);
        s.statehash_abstract_leaf_blocked = g_sh_abstract_leaf_blocked.exchange(0);
        s.statekey_skipped_null_arg = g_sk_null_arg.exchange(0);
        s.statehash_skipped_null_arg = g_sh_null_arg.exchange(0);
        if (s.any()) {
            BLOG("state naming update reject (window): null_new=%llu refine_not_child=%llu "
                 "refine_bad_edge=%llu abs_sib_noparent=%llu abs_sib_noedge=%llu "
                 "abs_sib_infer=%llu abs_sib_not_leaf=%llu abs_relation=%llu sk_not_applied=%llu "
                 "sk_leaf_blk=%llu sh_not_applied=%llu sh_leaf_blk=%llu "
                 "sk_null_arg=%llu sh_null_arg=%llu",
                 static_cast<unsigned long long>(s.update_reject_null_new),
                 static_cast<unsigned long long>(s.update_refine_not_direct_child),
                 static_cast<unsigned long long>(s.update_refine_infer_edge_failed),
                 static_cast<unsigned long long>(s.update_abstract_sibling_no_parent),
                 static_cast<unsigned long long>(s.update_abstract_sibling_naming_to_edge_failed),
                 static_cast<unsigned long long>(s.update_abstract_sibling_infer_edge_failed),
                 static_cast<unsigned long long>(s.update_abstract_sibling_not_leaf),
                 static_cast<unsigned long long>(s.update_abstract_relation_rejected),
                 static_cast<unsigned long long>(s.statekey_not_applied_after_update),
                 static_cast<unsigned long long>(s.statekey_abstract_leaf_blocked),
                 static_cast<unsigned long long>(s.statehash_not_applied_after_update),
                 static_cast<unsigned long long>(s.statehash_abstract_leaf_blocked),
                 static_cast<unsigned long long>(s.statekey_skipped_null_arg),
                 static_cast<unsigned long long>(s.statehash_skipped_null_arg));
        }
        return s;
    }

    /** Atomically reads and clears global tree-walk / fixed-point diagnostics. */
    StateNamingTreeWalkDiagStats StateNamingManager::consumeTreeWalkDiagStats() {
        StateNamingTreeWalkDiagStats s;
        s.tree_to_naming_dom_entries = g_tt_dom_entries.exchange(0);
        s.tree_to_naming_rebuild_failed = g_tt_rebuild_failed.exchange(0);
        s.tree_to_naming_zero_state_hash = g_tt_zero_hash.exchange(0);
        s.tree_to_naming_no_successor = g_tt_no_successor.exchange(0);
        s.tree_to_naming_cycle_break = g_tt_cycle.exchange(0);
        s.tree_to_naming_total_hops = g_tt_total_hops.exchange(0);
        s.fixed_point_null_after_tree_to_naming = g_fp_null_after_ttn.exchange(0);
        s.fixed_point_batch_refine_failed = g_fp_batch_refine_failed.exchange(0);
        if (s.tree_to_naming_dom_entries > 0 || s.any()) {
            BLOG("state naming tree walk (window): ttn_entries=%llu ttn_hops_total=%llu "
                 "ttn_rebuild_fail=%llu ttn_zero_hash=%llu ttn_no_next=%llu "
                 "ttn_cycle=%llu fp_null_after_ttn=%llu fp_batch_refine_fail=%llu",
                 static_cast<unsigned long long>(s.tree_to_naming_dom_entries),
                 static_cast<unsigned long long>(s.tree_to_naming_total_hops),
                 static_cast<unsigned long long>(s.tree_to_naming_rebuild_failed),
                 static_cast<unsigned long long>(s.tree_to_naming_zero_state_hash),
                 static_cast<unsigned long long>(s.tree_to_naming_no_successor),
                 static_cast<unsigned long long>(s.tree_to_naming_cycle_break),
                 static_cast<unsigned long long>(s.fixed_point_null_after_tree_to_naming),
                 static_cast<unsigned long long>(s.fixed_point_batch_refine_failed));
        }
        return s;
    }

} // namespace naming
} // namespace fastbotx
