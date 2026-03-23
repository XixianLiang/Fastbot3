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
#include "NamerType.h"

#include <algorithm>

namespace fastbotx {
namespace naming {
namespace {

    int popcount32(uint32_t x) {
        int c = 0;
        while (x) {
            c++;
            x &= x - 1u;
        }
        return c;
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
            by_mask_[mask] = bn;
            ordered_.push_back(bn);
        }

        std::sort(ordered_.begin(), ordered_.end(),
                  [](const std::shared_ptr<BitmaskNamer> &a, const std::shared_ptr<BitmaskNamer> &b) {
                      const uint32_t ma = a->getMask();
                      const uint32_t mb = b->getMask();
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

    const NamerFactory NamerFactory::CURRENT{};

    std::shared_ptr<BitmaskNamer> NamerFactory::getByMask(uint32_t mask) const {
        auto it = by_mask_.find(mask);
        if (it == by_mask_.end()) {
            return nullptr;
        }
        return it->second;
    }

} // namespace naming
} // namespace fastbotx
