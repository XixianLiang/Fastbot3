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
/**
 * APE ActionPatchNamer: wraps base namer and appends enabled/clickable/checkable/long-clickable/scrollable
 * predicates to Name::toXPath (Config.usePatchNamer, default true).
 */
#ifndef FASTBOTX_DESC_NAMING_ACTIONPATCHNAMER_H_
#define FASTBOTX_DESC_NAMING_ACTIONPATCHNAMER_H_

#include "Namer.h"

#include <memory>

namespace fastbotx {
namespace naming {

    class ActionPatchName : public Name {
    public:
        ActionPatchName(NamerPtr namer, NamePtr baseName, int patch);

        std::shared_ptr<Namer> getNamer() const override { return namer_; }

        std::string toXPath() const override;

        void appendXPathLocalProperties(std::string &sb) const override;

    private:
        NamerPtr namer_{};
        NamePtr base_{};
        int patch_{0};
    };

    class ActionPatchNamer : public Namer, public std::enable_shared_from_this<ActionPatchNamer> {
    public:
        explicit ActionPatchNamer(NamerPtr base);

        std::vector<NamerType> getNamerTypes() const override;
        uint32_t typeDimensionMask() const override;

        NamePtr naming(gui_tree::GUITreeNode &node) override;

        std::string xpathKeyForNode(gui_tree::GUITreeNode &node) const override;

        bool refinesTo(const Namer &other) const override;

        const Namer &baseNamer() const { return *base_; }

    private:
        NamerPtr base_{};
    };

    std::string appendActionPatchXPathSuffix(const std::string &baseXPath, int patch);

    int actionPatchBitsForNode(const gui_tree::GUITreeNode &node);

} // namespace naming
} // namespace fastbotx

#endif
