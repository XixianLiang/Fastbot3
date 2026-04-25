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

#include <algorithm>
#include <vector>

namespace fastbotx {
namespace naming {

    uint32_t Namer::typeDimensionMask() const {
        uint32_t m = 0;
        for (NamerType t : getNamerTypes()) {
            m |= 1u << static_cast<unsigned>(t);
        }
        return m;
    }

    int compareNamer(const Namer &a, const Namer &b) {
        std::vector<NamerType> ta = a.getNamerTypes();
        std::vector<NamerType> tb = b.getNamerTypes();
        auto ord = [](NamerType t) -> unsigned { return static_cast<unsigned>(t); };
        std::sort(ta.begin(), ta.end(), [&](NamerType x, NamerType y) { return ord(x) < ord(y); });
        std::sort(tb.begin(), tb.end(), [&](NamerType x, NamerType y) { return ord(x) < ord(y); });
        if (ta.size() != tb.size()) {
            return ta.size() < tb.size() ? -1 : 1;
        }
        for (size_t i = 0; i < ta.size(); ++i) {
            const unsigned oa = ord(ta[i]);
            const unsigned ob = ord(tb[i]);
            if (oa != ob) {
                return oa < ob ? -1 : 1;
            }
        }
        return 0;
    }

} // namespace naming
} // namespace fastbotx
