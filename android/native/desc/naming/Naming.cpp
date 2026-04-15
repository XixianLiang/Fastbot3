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

#include "Naming.h"
#include "BitmaskNamer.h"
#include "NamingRuntime.h"
#include "NamerType.h"
#include "../gui_tree/GUITree.h"
#include "../xpath/XPathNodeMapper.h"

#include <algorithm>
#include <deque>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <string>

namespace fastbotx {
namespace naming {
namespace {

    int bitCount(uint32_t x) {
        return __builtin_popcount(x);
    }

    std::string computeFingerprintString(
        const std::vector<std::shared_ptr<Namelet>> &namelets) {
        auto appendHex32 = [](std::string &dst, uint32_t v) {
            static const char kHex[] = "0123456789abcdef";
            for (int shift = 28; shift >= 0; shift -= 4) {
                dst.push_back(kHex[(v >> shift) & 0xF]);
            }
        };
        // Collision-resistant fingerprint for Naming identity / blacklist keys. Ordering of namelets
        // matters for StateKey identity; do not canonicalize by sorting.
        // Keep it printable (no control chars) so it stays stable across logs/JSON/persistence.
        std::string out;
        out.reserve(namelets.size() * 48);
        out.append("v3");
        for (size_t idx = 0; idx < namelets.size(); ++idx) {
            const auto &nl = namelets[idx];
            if (!nl) {
                continue;
            }
            out.push_back('|');
            // Prefix with positional index to make the fingerprint order-sensitive and easy to diff.
            out.append(std::to_string(idx));
            out.push_back(':');
            out.push_back(nl->isBase() ? 'B' : 'R');
            out.push_back(':');
            out.append(nl->getExprString());
            out.push_back('#');
            appendHex32(out, nl->getNamer().typeDimensionMask());
        }
        return out;
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
        std::vector<NameletPtr> sorted = candidates;
        std::sort(sorted.begin(), sorted.end(), nameletSelectLess);
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

    struct NamingEvalGuard {
        explicit NamingEvalGuard(const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *m) {
            namingEvalSetNodeToNamer(m);
        }
        ~NamingEvalGuard() { namingEvalClear(); }
        NamingEvalGuard(const NamingEvalGuard &) = delete;
        NamingEvalGuard &operator=(const NamingEvalGuard &) = delete;
    };

}

    std::atomic<int> Naming::naming_counter_{0};

    Naming::Naming(std::vector<std::shared_ptr<Namelet>> namelets)
        : Naming(nullptr, std::move(namelets)) {}

    std::shared_ptr<Naming> Naming::createChild(std::shared_ptr<Naming> parent,
                                                 std::vector<std::shared_ptr<Namelet>> namelets) {
        return std::shared_ptr<Naming>(new Naming(std::move(parent), std::move(namelets)));
    }

    void Naming::addRefinementChild(const NamingEdge &edge, std::shared_ptr<Naming> child) {
        if (!child) {
            return;
        }
        children_[edge] = std::move(child);
    }

    Naming::Naming(std::shared_ptr<Naming> parent, std::vector<std::shared_ptr<Namelet>> namelets)
        : parent_(parent),
          namelets_(std::move(namelets)) {
        naming_name_ = "Naming[" + std::to_string(naming_counter_.fetch_add(1, std::memory_order_relaxed)) + "]";
        fineness_ = -1;
        for (const auto &nl : namelets_) {
            if (!nl) continue;
            int f = 0;
            const auto *bn = dynamic_cast<const BitmaskNamer *>(nl->getNamerPtr().get());
            if (bn) {
                f = bitCount(bn->getMask());
            } else {
                f = static_cast<int>(nl->getNamerPtr()->getNamerTypes().size());
            }
            if (fineness_ < 0 || f > fineness_) {
                fineness_ = f;
            }
        }
        if (fineness_ < 0) {
            fineness_ = 0;
        }
        fingerprint_cached_ = computeFingerprintString(namelets_);
    }

    size_t Naming::NamingResult::getNodeSize() const {
        size_t s = 0;
        for (const auto &g : node_groups) {
            s += g.size();
        }
        return s;
    }

    void Naming::NamingResult::updateNames() {
        for (size_t i = 0; i < names.size(); ++i) {
            if (i >= node_groups.size()) break;
            for (size_t j = 0; j < node_groups[i].size(); ++j) {
                gui_tree::GUITreeNodePtr &node = node_groups[i][j];
                if (!node) continue;
                node->setXPathName(names[i]);
                if (i < namelet_groups.size() && j < namelet_groups[i].size()) {
                    node->setCurrentNamelet(namelet_groups[i][j]);
                }
            }
        }
    }

    const std::string &Naming::fingerprintString() const { return fingerprint_cached_; }

    std::shared_ptr<Namelet> Naming::getLastNamelet() const {
        if (namelets_.empty()) {
            return nullptr;
        }
        return namelets_.back();
    }

    bool Naming::isReplaceable(const std::shared_ptr<Namelet> &namelet) const {
        if (!namelet || namelets_.empty()) {
            return false;
        }
        if (!namelet->isRefine()) {
            return false;
        }
        return namelets_.back() == namelet;
    }

    Naming::NamingResult Naming::namingInternal(
        gui_tree::GUITree &tree, const std::shared_ptr<gui_tree::XPathNodeMapper> &dom) const {
        NamingResult out;
        if (!dom) {
            return out;
        }
        std::unordered_map<gui_tree::GUITreeNode *, std::vector<NameletPtr>> node_to_namelets;
        std::unordered_map<gui_tree::GUITreeNode *, gui_tree::GUITreeNodePtr> node_ref;
        node_to_namelets.reserve(256);
        node_ref.reserve(256);

        for (const auto &nl : namelets_) {
            if (!nl) continue;
            std::vector<gui_tree::GUITreeNodePtr> matched = dom->nodesForXPath(nl->getExprString());
            for (const auto &nptr : matched) {
                if (!nptr) continue;
                gui_tree::GUITreeNode *raw = nptr.get();
                node_ref.emplace(raw, nptr);
                node_to_namelets[raw].push_back(nl);
            }
        }

        std::vector<gui_tree::GUITreeNode *> bfs_nodes;
        bfs_nodes.reserve(256);
        std::deque<gui_tree::GUITreeNode *> q;
        if (tree.getRootNode()) q.push_back(tree.getRootNode());
        while (!q.empty()) {
            gui_tree::GUITreeNode *cur = q.front();
            q.pop_front();
            if (!cur) continue;
            bfs_nodes.push_back(cur);
            for (const auto &ch : cur->getChildren()) {
                if (ch) q.push_back(ch.get());
            }
        }

        std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> node_to_namer;
        std::unordered_map<gui_tree::GUITreeNode *, NameletPtr> node_to_selected;
        node_to_namer.reserve(bfs_nodes.size());
        node_to_selected.reserve(bfs_nodes.size());
        for (gui_tree::GUITreeNode *n : bfs_nodes) {
            auto itNl = node_to_namelets.find(n);
            if (itNl == node_to_namelets.end()) {
                out.names.push_back(nullptr);
                return out;
            }
            NameletPtr sel = selectNameletForNode(itNl->second);
            node_to_selected[n] = sel;
            if (sel && sel->getNamerPtr()) {
                node_to_namer[n] = &sel->getNamer();
            }
        }
        NamingEvalGuard namingEvalGuard(&node_to_namer);

        struct Group {
            NamePtr name;
            std::vector<gui_tree::GUITreeNodePtr> nodes;
            std::vector<NameletPtr> namelets;
        };
        std::unordered_map<std::string, Group> groups;
        groups.reserve(bfs_nodes.empty() ? 64 : bfs_nodes.size());
        for (gui_tree::GUITreeNode *raw : bfs_nodes) {
            auto itNode = node_ref.find(raw);
            if (itNode == node_ref.end()) {
                out.names.push_back(nullptr);
                return out;
            }
            gui_tree::GUITreeNodePtr nptr = itNode->second;
            auto itSel = node_to_selected.find(raw);
            NameletPtr selected = (itSel != node_to_selected.end()) ? itSel->second : nullptr;
            if (!selected || !selected->getNamerPtr()) {
                out.names.push_back(nullptr);
                return out;
            }
            NamePtr name = selected->getNamer().naming(*nptr);
            if (!name) {
                out.names.push_back(nullptr);
                return out;
            }
            const std::string key = name->toXPath();
            Group &g = groups[key];
            if (!g.name) g.name = std::move(name);
            g.nodes.push_back(nptr);
            g.namelets.push_back(selected);
        }
        std::vector<std::string> sorted_keys;
        sorted_keys.reserve(groups.size());
        for (const auto &kv : groups) sorted_keys.push_back(kv.first);
        std::sort(sorted_keys.begin(), sorted_keys.end());
        for (const auto &k : sorted_keys) {
            auto it = groups.find(k);
            if (it == groups.end() || !it->second.name) continue;
            out.names.push_back(it->second.name);
            out.node_groups.push_back(std::move(it->second.nodes));
            out.namelet_groups.push_back(std::move(it->second.namelets));
        }
        return out;
    }

} // namespace naming
} // namespace fastbotx
