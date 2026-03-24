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

#include "Namer.h"
#include "BitmaskNamer.h"

#include <algorithm>

namespace fastbotx {
namespace naming {

    int compareNamer(const Namer &a, const Namer &b) {
        const auto *ab = dynamic_cast<const BitmaskNamer *>(&a);
        const auto *bb = dynamic_cast<const BitmaskNamer *>(&b);
        if (ab && bb) {
            const uint32_t ma = ab->getMask();
            const uint32_t mb = bb->getMask();
            const int pa = __builtin_popcount(ma);
            const int pb = __builtin_popcount(mb);
            if (pa != pb) {
                return pa < pb ? -1 : 1;
            }
            if (ma != mb) {
                return ma < mb ? -1 : 1;
            }
            return 0;
        }
        std::vector<NamerType> ta = a.getNamerTypes();
        std::vector<NamerType> tb = b.getNamerTypes();
        std::sort(ta.begin(), ta.end(), [](NamerType x, NamerType y) {
            return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
        });
        std::sort(tb.begin(), tb.end(), [](NamerType x, NamerType y) {
            return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
        });
        if (ta.size() != tb.size()) {
            return ta.size() < tb.size() ? -1 : 1;
        }
        for (size_t i = 0; i < ta.size(); ++i) {
            if (ta[i] != tb[i]) {
                return static_cast<unsigned char>(ta[i]) < static_cast<unsigned char>(tb[i]) ? -1 : 1;
            }
        }
        return 0;
    }

} // namespace naming
} // namespace fastbotx
