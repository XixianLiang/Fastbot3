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

/*
 * Refinement graph over the pre-built bitmask namers from `NamerFactory`: one-bit up/down neighbors and the sorted finer set.
 */
#ifndef FASTBOTX_DESC_NAMING_NAMERLATTICE_H_
#define FASTBOTX_DESC_NAMING_NAMERLATTICE_H_

#include "Namer.h"
#include "NamerFactory.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace fastbotx {
namespace naming {

    class NamerLattice {
    public:
        explicit NamerLattice(const NamerFactory &factory = NamerFactory::current());

        /** One-bit refinements of `coarse` (see `NamerLattice.cpp`); deterministic order by mask. */
        std::vector<NamerPtr> immediateRefinements(const NamerPtr &coarse) const;

        /** One-bit abstractions of `fine`; deterministic order by mask. */
        std::vector<NamerPtr> immediateAbstractions(const NamerPtr &fine) const;

        /** All strictly finer namers, sorted by popcount then sum of set-bit indices then mask. */
        std::vector<NamerPtr> sortedAbove(const NamerPtr &coarse) const;

    private:
        const NamerFactory *factory_{nullptr};
        mutable std::unordered_map<uint32_t, std::vector<NamerPtr>> refinements_cache_;
        mutable std::unordered_map<uint32_t, std::vector<NamerPtr>> abstractions_cache_;
        mutable std::unordered_map<uint32_t, std::vector<NamerPtr>> sorted_above_cache_;
    };

} // namespace naming
} // namespace fastbotx

#endif
