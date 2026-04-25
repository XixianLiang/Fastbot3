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
 * NamerType.
 */
#ifndef FASTBOTX_DESC_NAMING_NAMERTYPE_H_
#define FASTBOTX_DESC_NAMING_NAMERTYPE_H_

#include <vector>

namespace fastbotx {
namespace naming {

    enum class NamerType : unsigned char {
        TYPE = 0,
        INDEX,
        PARENT,
        TEXT,
        ANCESTOR
    };

    /** Ordered list used in lattice (Config.useAncestorNamer in Java). */
    const std::vector<NamerType> &namerTypesUsed();
    /** Runtime switch for Config.useAncestorNamer-style behavior. */
    void setUseAncestorNamer(bool enabled);
    bool useAncestorNamer();

    /** Runtime switch for Config.usePatchNamer. */
    void setUsePatchNamer(bool enabled);
    bool usePatchNamer();

} // namespace naming
} // namespace fastbotx

#endif
