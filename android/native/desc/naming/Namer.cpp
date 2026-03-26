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
        const uint32_t ma = a.typeDimensionMask();
        const uint32_t mb = b.typeDimensionMask();
        const int pa = __builtin_popcount(ma);
        const int pb = __builtin_popcount(mb);
        if (pa != pb) {
            return pa < pb ? -1 : 1;
        }
        if (ma != mb) {
            return ma < mb ? -1 : 1;
        }
        if (&a == &b) {
            return 0;
        }
        /* Same lattice mask (e.g. wrap vs base): tie-break by object address for strict weak ordering. */
        return &a < &b ? -1 : 1;
    }

} // namespace naming
} // namespace fastbotx
