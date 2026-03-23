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

#include "NamerType.h"

namespace fastbotx {
namespace naming {

#ifndef FASTBOTX_USE_ANCESTOR_NAMER
#define FASTBOTX_USE_ANCESTOR_NAMER 0
#endif

    const std::vector<NamerType> &namerTypesUsed() {
#if FASTBOTX_USE_ANCESTOR_NAMER
        static const std::vector<NamerType> kUsed = {
            NamerType::TYPE, NamerType::INDEX, NamerType::PARENT,
            NamerType::TEXT, NamerType::ANCESTOR};
#else
        static const std::vector<NamerType> kUsed = {
            NamerType::TYPE, NamerType::INDEX, NamerType::PARENT, NamerType::TEXT};
#endif
        return kUsed;
    }

} // namespace naming
} // namespace fastbotx
