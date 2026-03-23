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
 * Pre-registered BitmaskNamers for every subset of namerTypesUsed() (APE NamerFactory).
 */
#ifndef FASTBOTX_DESC_NAMING_NAMERFACTORY_H_
#define FASTBOTX_DESC_NAMING_NAMERFACTORY_H_

#include "BitmaskNamer.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fastbotx {
namespace naming {

    class NamerFactory {
    public:
        /** Singleton: all non-empty subsets of {@link namerTypesUsed()} plus mask 0 (empty namer). */
        static const NamerFactory CURRENT;

        /** Builds the full bitmask cube for {@link namerTypesUsed()}; prefer {@link CURRENT}. */
        NamerFactory();

        NamerFactory(const NamerFactory &) = delete;
        NamerFactory &operator=(const NamerFactory &) = delete;

        /** Bitmask uses {@code 1u << static_cast<unsigned>(NamerType)}. */
        std::shared_ptr<BitmaskNamer> getByMask(uint32_t mask) const;

        const std::vector<std::shared_ptr<BitmaskNamer>> &all() const { return ordered_; }

        std::shared_ptr<BitmaskNamer> empty() const { return empty_; }

    private:

        std::unordered_map<uint32_t, std::shared_ptr<BitmaskNamer>> by_mask_;
        std::vector<std::shared_ptr<BitmaskNamer>> ordered_;
        std::shared_ptr<BitmaskNamer> empty_{};
    };

} // namespace naming
} // namespace fastbotx

#endif
