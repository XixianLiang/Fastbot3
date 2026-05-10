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
 * ActionPatchNamer: wraps a base `Namer` and appends interactive predicates (`enabled`, `clickable`, …)
 * to the XPath returned by `Name::toXPath`.
 *
 * Predicate sets and scroll-type inclusion are controlled by `ActionPatchTokenConfig` / `setActionPatchProfile`.
 * Optional decoding of allowed concrete actions from an `ActionPatchName` is toggled via
 * `setActionPatchDeriveActionsFromName`.
 */
#ifndef FASTBOTX_DESC_NAMING_ACTIONPATCHNAMER_H_
#define FASTBOTX_DESC_NAMING_ACTIONPATCHNAMER_H_

#include "Namer.h"

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

namespace fastbotx {
namespace naming {

/** High-level category bits for decoded derived-action metadata (click / long-click / scroll groups). */
constexpr uint32_t kDerivedKindClick = 1u << 0;
constexpr uint32_t kDerivedKindLongClick = 1u << 1;
constexpr uint32_t kDerivedKindScroll = 1u << 2;

/**
 * Global settings for how interactive properties are encoded into XPath tokens.
 * Bit *i* selects whether the *i*-th predicate in the fixed order enabled→scrollable appears in the suffix.
 */
struct ActionPatchTokenConfig {
    uint8_t include_bool_props_mask{0x1Fu};
    /** When true, append `[@scroll-type='…']` after the boolean predicates. */
    bool include_scroll_type{false};
    /** When true, downstream logic may derive concrete actions from `ActionPatchName` metadata. */
    bool derive_actions_from_name{false};
    /** Last profile string consumed by `setActionPatchProfile`. */
    std::string profile{"default"};
};

/**
 * `Name` implementation that adds a packed “action patch” suffix on top of an optional base name:
 * predicates mirror widget interactivity and scroll axis for XPath matching and planning.
 */
class ActionPatchName : public Name {
public:
    ActionPatchName(NamerPtr namer, NamePtr baseName, int patch, ScrollType scrollType,
                    uint8_t includeBoolPropsMask, bool includeScrollType,
                    std::string xpath_full_override = {});

    std::shared_ptr<Namer> getNamer() const override { return namer_; }

    const std::string &toXPath() const override;
    std::string cacheKeyString() const override {
        std::ostringstream oss;
        oss << "ActionPatchName{" << toXPath() << "|patch=" << patch_ << "|scroll="
            << static_cast<int>(scroll_type_) << "|mask=" << static_cast<unsigned>(include_bool_props_mask_)
            << "|incScroll=" << (include_scroll_type_ ? 1 : 0) << "}";
        return oss.str();
    }

    void appendXPathLocalProperties(std::string &sb) const override;

    /** Bitmask of concrete `ActionType` values allowed by this token’s predicates and scroll class. */
    uint32_t derivedActionMask() const { return derived_action_mask_; }
    /** Subset of `kDerivedKind*` flags describing which coarse action families are encoded. */
    uint32_t derivedActionKnownKinds() const { return derived_action_known_kinds_; }

private:
    NamerPtr namer_{};
    NamePtr base_{};
    int patch_{0};
    ScrollType scroll_type_{ScrollType::NONE};
    uint8_t include_bool_props_mask_{0};
    bool include_scroll_type_{false};
    uint32_t derived_action_mask_{0};
    uint32_t derived_action_known_kinds_{0};
    /** Materialized full XPath; avoids recomputing `base_->toXPath()` plus suffix on every query. */
    std::string xpath_full_;
};

/**
 * Decorator namer: delegates identity to `base` and wraps results in `ActionPatchName` using each node’s state.
 */
class ActionPatchNamer : public Namer, public std::enable_shared_from_this<ActionPatchNamer> {
public:
    explicit ActionPatchNamer(NamerPtr base);

    std::vector<NamerType> getNamerTypes() const override;
    uint32_t typeDimensionMask() const override;

    NamePtr naming(gui_tree::GUITreeNode &node) override;

    /** Builds from a precomputed base XPath key (no duplicate interactive suffix); see implementation. */
    NamePtr namingWithXPathKey(gui_tree::GUITreeNode &node, const std::string &xpathKey) override;

    /** Base namer key only—must not already contain interactive predicates from this layer. */
    std::string xpathKeyForNode(gui_tree::GUITreeNode &node) const override;

    bool refinesTo(const Namer &other) const override;

    const Namer &baseNamer() const { return *base_; }

private:
    NamerPtr base_{};
};

/** Appends interactive (and optionally scroll-type) XPath predicates to `baseXPath`. */
std::string appendActionPatchXPathSuffix(std::string baseXPath, int patch, ScrollType scrollType,
                                         uint8_t includeBoolPropsMask, bool includeScrollType);

/** Packs the five interactive flags from `node` into the patch integer used by tokens. */
int actionPatchBitsForNode(const gui_tree::GUITreeNode &node);

/** Scroll classification for tokens, from node scroll bits and view class name. */
ScrollType actionPatchScrollTypeForNode(const gui_tree::GUITreeNode &node);

/** Current global `ActionPatchTokenConfig`. */
ActionPatchTokenConfig getActionPatchTokenConfig();

/** Parses a profile label or bitmask string; updates `g_cfg`. */
void setActionPatchProfile(const std::string &value);
/** Active profile string after the last `setActionPatchProfile` call. */
const std::string &getActionPatchProfile();

/** Whether derived-action decoding from `ActionPatchName` is enabled. */
bool useActionPatchDeriveActionsFromName();
/** Enables or disables `derive_actions_from_name` in the global token config. */
void setActionPatchDeriveActionsFromName(bool enabled);

/**
 * If `name` points to `ActionPatchName`, fills allowed-action and known-kind masks; otherwise returns false.
 * The exported symbol retains a legacy identifier; behavior is generic decoding of action-patch metadata.
 */
bool decodeApeDerivedActionsFromName(const NamePtr &name, uint32_t *outAllowedActionMask,
                                     uint32_t *outKnownActionKinds);

} // namespace naming
} // namespace fastbotx

#endif
