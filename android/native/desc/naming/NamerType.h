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
 * Naming dimension tags for bitmask namers and the lattice (`TYPE`, `TEXT`, hierarchy modes, etc.).
 */
#ifndef FASTBOTX_DESC_NAMING_NAMERTYPE_H_
#define FASTBOTX_DESC_NAMING_NAMERTYPE_H_

#include <vector>

namespace fastbotx {
namespace naming {

    /** Axes that can be combined in a `BitmaskNamer`; numeric values define bit positions in masks. */
    enum class NamerType : unsigned char {
        TYPE = 0,
        INDEX,
        PARENT,
        TEXT,
        ANCESTOR
    };

    /** Canonical ordered list of dimensions included in `NamerFactory` subsets (ancestor optional). */
    const std::vector<NamerType> &namerTypesUsed();

    /** Turns ancestor XPath naming on or off for subsequent factory cube builds. */
    void setUseAncestorNamer(bool enabled);
    bool useAncestorNamer();

    /** Turns `ActionPatchNamer` wrapping on or off for bitmask namers from the factory. */
    void setUsePatchNamer(bool enabled);
    bool usePatchNamer();

} // namespace naming
} // namespace fastbotx

#endif
