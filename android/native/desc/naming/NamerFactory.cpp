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

#include "NamerFactory.h"
#include "ActionPatchNamer.h"
#include "BitmaskNamer.h"
#include "NamerType.h"

#include <algorithm>
#include <memory>

namespace fastbotx {
namespace naming {
namespace {

    int popcount32(uint32_t x) {
        return __builtin_popcount(x);
    }

} // namespace

    NamerFactory::NamerFactory() {
        const auto &used = namerTypesUsed();
        const size_t n = used.size();
        const uint32_t limit = n >= 32 ? 0u : (1u << static_cast<unsigned>(n));

        for (uint32_t subset = 0; subset < limit; ++subset) {
            uint32_t mask = 0;
            for (size_t i = 0; i < n; ++i) {
                if (subset & (1u << static_cast<unsigned>(i))) {
                    const int b = static_cast<int>(used[i]);
                    mask |= (1u << static_cast<unsigned>(b));
                }
            }
            auto bn = BitmaskNamer::create(mask);
            NamerPtr pub = usePatchNamer() ? std::static_pointer_cast<Namer>(std::make_shared<ActionPatchNamer>(
                                                 std::static_pointer_cast<Namer>(bn)))
                                           : std::static_pointer_cast<Namer>(bn);
            by_mask_[mask] = pub;
            ordered_.push_back(std::move(pub));
        }

        std::sort(ordered_.begin(), ordered_.end(),
                  [](const NamerPtr &a, const NamerPtr &b) {
                      if (!a || !b) {
                          return a.get() < b.get();
                      }
                      const uint32_t ma = a->typeDimensionMask();
                      const uint32_t mb = b->typeDimensionMask();
                      const int pa = popcount32(ma);
                      const int pb = popcount32(mb);
                      if (pa != pb) {
                          return pa < pb;
                      }
                      return ma < mb;
                  });

        auto it = by_mask_.find(0);
        if (it != by_mask_.end()) {
            empty_ = it->second;
        }
    }

    const NamerFactory &NamerFactory::current() {
        static std::unique_ptr<NamerFactory> inst;
        static bool have = false;
        static bool snap_a = false;
        static bool snap_p = false;
        const bool a = useAncestorNamer();
        const bool p = usePatchNamer();
        if (!have || snap_a != a || snap_p != p) {
            inst = std::make_unique<NamerFactory>();
            snap_a = a;
            snap_p = p;
            have = true;
        }
        return *inst;
    }

    NamerPtr NamerFactory::getByMask(uint32_t mask) const {
        auto it = by_mask_.find(mask);
        if (it == by_mask_.end()) {
            return nullptr;
        }
        return it->second;
    }

} // namespace naming
} // namespace fastbotx
