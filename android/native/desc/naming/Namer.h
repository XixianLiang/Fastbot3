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
#ifndef FASTBOTX_DESC_NAMING_NAMER_H_
#define FASTBOTX_DESC_NAMING_NAMER_H_

#include "Name.h"
#include "NamerType.h"
#include "../gui_tree/GUITreeNode.h"

#include <string>
#include <vector>

namespace fastbotx {
namespace naming {

    class Namer {
    public:
        virtual ~Namer() = default;

        /** Which naming dimensions this policy combines (`NamerType` values). */
        virtual std::vector<NamerType> getNamerTypes() const = 0;

        /** Bit `i` is `1u << i` for each included `NamerType`; default folds `getNamerTypes()`. */
        virtual uint32_t typeDimensionMask() const;

        virtual NamePtr naming(gui_tree::GUITreeNode &node) = 0;

        /**
         * When `xpathKey` matches `xpathKeyForNode(node)` for this namer, implementations may build the `Name`
         * from the key without recomputing from the widget tree. Default delegates to `naming(node)`.
         */
        virtual NamePtr namingWithXPathKey(gui_tree::GUITreeNode &node, const std::string &xpathKey) {
            (void)xpathKey;
            return naming(node);
        }

        /**
         * Canonical XPath string used as a deduplication key before calling `naming` when the caller already
         * has a materialized path. Empty means “derive via `naming` then `toXPath`” (default).
         */
        virtual std::string xpathKeyForNode(gui_tree::GUITreeNode &node) const {
            (void)node;
            return {};
        }

        virtual bool refinesTo(const Namer &other) const = 0;
    };

    using NamerPtr = std::shared_ptr<Namer>;

    /** Three-way compare on sorted `NamerType` lists (used by `Namelet` ordering). */
    int compareNamer(const Namer &a, const Namer &b);

    /** Stable semantic key for Namer (type-set based, independent from pointer identity). */
    std::string namerSemanticKey(const Namer &n);

} // namespace naming
} // namespace fastbotx

#endif
