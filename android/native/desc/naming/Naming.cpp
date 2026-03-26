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
#include "NamerType.h"

#include <algorithm>
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
        // Same logic as the former Naming::fingerprintString() implementation,
        // but computed once in Naming construction.
        std::vector<std::string> parts;
        parts.reserve(namelets.size());
        for (const auto &nl : namelets) {
            if (!nl) {
                continue;
            }
            std::string s = nl->getExprString();
            s.push_back('\x1e');
            // Serialize NamerType set in bit order.
            // typeDimensionMask() is derived from getNamerTypes() for non-bitmask namers,
            // so this matches the prior two-branch logic.
            const uint32_t mask = nl->getNamer().typeDimensionMask();
            for (unsigned i = 0; i < 32; ++i) {
                if ((mask & (1u << i)) != 0u) {
                    s.push_back(static_cast<char>(i));
                }
            }
            parts.push_back(std::move(s));
        }
        std::sort(parts.begin(), parts.end());
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                out.push_back('|');
            }
            out.append(parts[i]);
        }
        return out;
    }

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

} // namespace naming
} // namespace fastbotx
