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

#include "NamerLattice.h"

#include <algorithm>

namespace fastbotx {
namespace naming {
namespace {

    int popcount32(uint32_t x) {
        return __builtin_popcount(x);
    }

    uint32_t maskOf(const Namer &n) { return n.typeDimensionMask(); }

} // namespace

    NamerLattice::NamerLattice(const NamerFactory &factory) : factory_(&factory) {}

    std::vector<NamerPtr> NamerLattice::immediateRefinements(const NamerPtr &coarse) const {
        std::vector<NamerPtr> out;
        if (!coarse) {
            return out;
        }
        const uint32_t cm = maskOf(*coarse);

        auto itCached = refinements_cache_.find(cm);
        if (itCached != refinements_cache_.end()) {
            return itCached->second;
        }

        out.reserve(factory_->all().size());
        const int cpc = popcount32(cm);
        for (const auto &n : factory_->all()) {
            if (!n) {
                continue;
            }
            const uint32_t fm = maskOf(*n);
            if (fm == cm) {
                continue;
            }
            if (popcount32(fm) != cpc + 1) {
                continue;
            }
            if ((fm & cm) != cm) {
                continue;
            }
            if (!n->refinesTo(*coarse)) {
                continue;
            }
            out.push_back(n);
        }
        std::sort(out.begin(), out.end(), [](const NamerPtr &a, const NamerPtr &b) {
            if (!a || !b) {
                return a.get() < b.get();
            }
            const uint32_t ma = maskOf(*a);
            const uint32_t mb = maskOf(*b);
            if (ma != mb) {
                return ma < mb;
            }
            return a.get() < b.get();
        });
        refinements_cache_.emplace(cm, out);
        return out;
    }

    std::vector<NamerPtr> NamerLattice::immediateAbstractions(const NamerPtr &fine) const {
        std::vector<NamerPtr> out;
        if (!fine) {
            return out;
        }
        const uint32_t fm = maskOf(*fine);

        auto itCached = abstractions_cache_.find(fm);
        if (itCached != abstractions_cache_.end()) {
            return itCached->second;
        }

        out.reserve(factory_->all().size());
        const int fpc = popcount32(fm);
        if (fpc == 0) {
            return out;
        }
        for (const auto &n : factory_->all()) {
            if (!n) {
                continue;
            }
            const uint32_t cm = maskOf(*n);
            if (cm == fm) {
                continue;
            }
            if (popcount32(cm) != fpc - 1) {
                continue;
            }
            if ((fm & cm) != cm) {
                continue;
            }
            if (!fine->refinesTo(*n)) {
                continue;
            }
            out.push_back(n);
        }
        std::sort(out.begin(), out.end(), [](const NamerPtr &a, const NamerPtr &b) {
            if (!a || !b) {
                return a.get() < b.get();
            }
            const uint32_t ma = maskOf(*a);
            const uint32_t mb = maskOf(*b);
            if (ma != mb) {
                return ma < mb;
            }
            return a.get() < b.get();
        });
        abstractions_cache_.emplace(fm, out);
        return out;
    }

    std::vector<NamerPtr> NamerLattice::sortedAbove(const NamerPtr &coarse) const {
        std::vector<NamerPtr> out;
        if (!coarse) {
            return out;
        }
        const uint32_t cm = maskOf(*coarse);
        auto itCached = sorted_above_cache_.find(cm);
        if (itCached != sorted_above_cache_.end()) {
            return itCached->second;
        }

        auto ordinalSum = [](uint32_t mask) -> unsigned {
            unsigned s = 0;
            while (mask != 0) {
                const unsigned tz = static_cast<unsigned>(__builtin_ctz(mask));
                s += tz;
                mask ^= (mask & (~mask + 1u));
            }
            return s;
        };

        const int cpc = popcount32(cm);
        out.reserve(factory_->all().size());
        for (const auto &n : factory_->all()) {
            if (!n) {
                continue;
            }
            const uint32_t fm = maskOf(*n);
            if (fm == cm) {
                continue;
            }
            if (!n->refinesTo(*coarse)) {
                continue;
            }
            if (popcount32(fm) <= cpc) {
                continue;
            }
            out.push_back(n);
        }
        std::sort(out.begin(), out.end(), [&](const NamerPtr &a, const NamerPtr &b) {
            if (!a || !b) {
                return a.get() < b.get();
            }
            const uint32_t ma = maskOf(*a);
            const uint32_t mb = maskOf(*b);
            const int pa = popcount32(ma);
            const int pb = popcount32(mb);
            if (pa != pb) {
                return pa < pb;
            }
            const unsigned sa = ordinalSum(ma);
            const unsigned sb = ordinalSum(mb);
            if (sa != sb) {
                return sa < sb;
            }
            return ma < mb;
        });
        sorted_above_cache_.emplace(cm, out);
        return out;
    }

} // namespace naming
} // namespace fastbotx
