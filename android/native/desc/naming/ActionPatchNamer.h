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
 * APE ActionPatchNamer: wraps a base namer and appends interactive predicates to `Name::toXPath`.
 *
 * Java `ActionPatchName` carries `ScrollType` and participates in `NamerFactory.decodeActions(name)`.
 * Native Fastbot mirrors that so executable action-sets can be aligned with the abstract name token.
 *
 * Token composition is configurable (`max.apeActionPatchProfile`); optional scroll-type in XPath;
 * optional filtering of RL actions from the decoded token (`max.apeActionPatchDeriveActions`).
 */
#ifndef FASTBOTX_DESC_NAMING_ACTIONPATCHNAMER_H_
#define FASTBOTX_DESC_NAMING_ACTIONPATCHNAMER_H_

#include "Namer.h"

#include <cstdint>
#include <memory>
#include <string>

namespace fastbotx {
namespace naming {

/** Category bits returned by `decodeApeDerivedActionsFromName()`. */
constexpr uint32_t kDerivedKindClick = 1u << 0;
constexpr uint32_t kDerivedKindLongClick = 1u << 1;
constexpr uint32_t kDerivedKindScroll = 1u << 2;

struct ActionPatchTokenConfig {
    /// Bit i corresponds to `kInteractiveProps[i]` in the XPath token (enabled…scrollable).
    uint8_t include_bool_props_mask{0x1Fu};
    bool include_scroll_type{true};
    bool derive_actions_from_name{false};
    std::string profile{"default"};
};

class ActionPatchName : public Name {
public:
    ActionPatchName(NamerPtr namer, NamePtr baseName, int patch, ScrollType scrollType,
                    uint8_t includeBoolPropsMask, bool includeScrollType,
                    std::string xpath_full_override = {});

    std::shared_ptr<Namer> getNamer() const override { return namer_; }

    std::string toXPath() const override;

    void appendXPathLocalProperties(std::string &sb) const override;

    uint32_t derivedActionMask() const { return derived_action_mask_; }
    uint32_t derivedActionKnownKinds() const { return derived_action_known_kinds_; }

private:
    NamerPtr namer_{};
    NamePtr base_{};
    int patch_{0};
    ScrollType scroll_type_{ScrollType::NONE};
    uint8_t include_bool_props_mask_{0};
    bool include_scroll_type_{false};
    // Derived action-set (APE NamerFactory.decodeActions(ActionPatchName)).
    uint32_t derived_action_mask_{0};
    uint32_t derived_action_known_kinds_{0};
    /// Cache full XPath to avoid repeated base_->toXPath() + suffix construction.
    std::string xpath_full_;
};

class ActionPatchNamer : public Namer, public std::enable_shared_from_this<ActionPatchNamer> {
public:
    explicit ActionPatchNamer(NamerPtr base);

    std::vector<NamerType> getNamerTypes() const override;
    uint32_t typeDimensionMask() const override;

    NamePtr naming(gui_tree::GUITreeNode &node) override;

    NamePtr namingWithXPathKey(gui_tree::GUITreeNode &node, const std::string &xpathKey) override;

    std::string xpathKeyForNode(gui_tree::GUITreeNode &node) const override;

    bool refinesTo(const Namer &other) const override;

    const Namer &baseNamer() const { return *base_; }

private:
    NamerPtr base_{};
};

std::string appendActionPatchXPathSuffix(std::string baseXPath, int patch, ScrollType scrollType,
                                         uint8_t includeBoolPropsMask, bool includeScrollType);

int actionPatchBitsForNode(const gui_tree::GUITreeNode &node);

ScrollType actionPatchScrollTypeForNode(const gui_tree::GUITreeNode &node);

ActionPatchTokenConfig getActionPatchTokenConfig();

void setActionPatchProfile(const std::string &value);
const std::string &getActionPatchProfile();

bool useActionPatchDeriveActionsFromName();
void setActionPatchDeriveActionsFromName(bool enabled);

bool decodeApeDerivedActionsFromName(const NamePtr &name, uint32_t *outAllowedActionMask,
                                     uint32_t *outKnownActionKinds);

} // namespace naming
} // namespace fastbotx

#endif
