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
#include "ActionPatchNamer.h"
#include "NameManager.h"
#include "../gui_tree/GUITreeNode.h"
#include "../../Base.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>

namespace fastbotx {
namespace naming {
namespace {

    /**
     * Interactive XPath predicate names in bit order from the least significant bit:
     * enabled, clickable, checkable, long-clickable, scrollable.
     */
    constexpr const char *kInteractiveProps[5] = {"enabled", "clickable", "checkable", "long-clickable",
                                                  "scrollable"};

    /** Process-global token settings for action-patch profiles and derived-action flags. */
    ActionPatchTokenConfig g_cfg;

    /** Returns an ASCII lowercased copy of `s`. */
    inline std::string toLowerCopy(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    /** Returns a copy of `s` with leading and trailing ASCII whitespace removed. */
    inline std::string trimCopy(std::string s) {
        size_t l = 0;
        while (l < s.size() && (s[l] == ' ' || s[l] == '\t' || s[l] == '\r' || s[l] == '\n')) {
            ++l;
        }
        size_t r = s.size();
        while (r > l && (s[r - 1] == ' ' || s[r - 1] == '\t' || s[r - 1] == '\r' || s[r - 1] == '\n')) {
            --r;
        }
        return s.substr(l, r - l);
    }

    /** Single-bit mask for one `ActionType` enumerator value. */
    inline uint32_t actionTypeBit(ActionType t) {
        return 1u << static_cast<unsigned>(t);
    }

    /** String placed in `[@scroll-type='…']` for the given scroll classification. */
    const char *scrollTypeAttrValue(ScrollType st) {
        switch (st) {
        case ScrollType::NONE:
            return "none";
        case ScrollType::Horizontal:
            return "horizontal";
        case ScrollType::Vertical:
            return "vertical";
        case ScrollType::ALL:
            return "all";
        case ScrollType::VerticalSeries:
            return "vertical";
        default:
            return "none";
        }
    }

    /**
     * Classifies scroll axes from the scrollable bitmask and widget class name (same heuristics as the
     * GUI tree factory’s scroll-type helper).
     */
    ScrollType scrollTypeFromNodeImpl(int scrollableBits, const char *className) {
        if (scrollableBits == 0) {
            return ScrollType::NONE;
        }
        if (!className || !*className) {
            return ScrollType::ALL;
        }
        if (std::strcmp(className, "android.widget.ScrollView") == 0 ||
            std::strcmp(className, "android.widget.ListView") == 0 ||
            std::strcmp(className, "android.widget.ExpandableListView") == 0 ||
            std::strcmp(className, "android.support.v17.leanback.widget.VerticalGridView") == 0) {
            return ScrollType::Vertical;
        }
        if (std::strcmp(className, "android.widget.HorizontalScrollView") == 0 ||
            std::strcmp(className, "android.support.v17.leanback.widget.HorizontalGridView") == 0 ||
            std::strcmp(className, "android.support.v4.view.ViewPager") == 0) {
            return ScrollType::Horizontal;
        }
        if (scrollableBits == 1) {
            return ScrollType::Vertical;
        }
        if (scrollableBits == 2) {
            return ScrollType::Horizontal;
        }
        return ScrollType::ALL;
    }

    /** Currently passes `st` through unchanged (reserved for profile-dependent normalization). */
    ScrollType normalizeScrollTypeForToken(ScrollType st, bool /*includeScrollType*/,
                                           uint8_t /*includeBoolPropsMask*/) {
        return st;
    }

    /**
     * Bitmask of high-level action categories encoded by the current token (click / long-click / scroll),
     * based on which interactive properties participate in the patch.
     */
    uint32_t buildDerivedActionKnownKinds(uint8_t includeBoolPropsMask, bool /*includeScrollType*/) {
        uint32_t known = 0;
        // CLICK is derivable when either clickable or checkable participates, matching `buildDerivedActionMask`.
        if ((includeBoolPropsMask & (1u << 1)) != 0 || (includeBoolPropsMask & (1u << 2)) != 0) {
            known |= kDerivedKindClick;
        }
        if ((includeBoolPropsMask & (1u << 3)) != 0) {
            known |= kDerivedKindLongClick;
        }
        if ((includeBoolPropsMask & (1u << 4)) != 0) {
            known |= kDerivedKindScroll;
        }
        return known;
    }

    /**
     * Builds the allowed concrete action mask from packed predicate bits in `patch`, scroll axis,
     * and which properties are included in the token; scroll actions attach only when scrollable is present.
     */
    uint32_t buildDerivedActionMask(int patch, ScrollType scrollType, uint8_t includeBoolPropsMask,
                                    bool /*includeScrollType*/) {
        uint32_t mask = 0;
        const bool clickable = ((patch >> 1) & 0x1) != 0;
        const bool checkable = ((patch >> 2) & 0x1) != 0;
        const bool longClickable = ((patch >> 3) & 0x1) != 0;

        if (clickable || checkable) {
            mask |= actionTypeBit(ActionType::CLICK);
        }
        if (longClickable) {
            mask |= actionTypeBit(ActionType::LONG_CLICK);
        }

        const bool tokenHasScrollable = (includeBoolPropsMask & (1u << 4)) != 0;
        if (!tokenHasScrollable) {
            return mask;
        }

        const bool scrollable = ((patch >> 4) & 0x1) != 0;
        if (!scrollable) {
            return mask;
        }

        switch (scrollType) {
        case ScrollType::Vertical:
        case ScrollType::VerticalSeries:
            mask |= actionTypeBit(ActionType::SCROLL_BOTTOM_UP);
            mask |= actionTypeBit(ActionType::SCROLL_TOP_DOWN);
            break;
        case ScrollType::Horizontal:
            mask |= actionTypeBit(ActionType::SCROLL_LEFT_RIGHT);
            mask |= actionTypeBit(ActionType::SCROLL_RIGHT_LEFT);
            break;
        case ScrollType::ALL:
            mask |= actionTypeBit(ActionType::SCROLL_TOP_DOWN);
            mask |= actionTypeBit(ActionType::SCROLL_BOTTOM_UP);
            mask |= actionTypeBit(ActionType::SCROLL_LEFT_RIGHT);
            mask |= actionTypeBit(ActionType::SCROLL_RIGHT_LEFT);
            break;
        case ScrollType::NONE:
        default:
            break;
        }
        return mask;
    }

    /** Appends `[@prop='true'|'false']` predicates (and optionally scroll-type) to `baseXPath`. */
    void appendInteractiveTokens(std::string &baseXPath, int patch, ScrollType scrollType,
                                 uint8_t includeBoolPropsMask, bool includeScrollType) {
        baseXPath.reserve(baseXPath.size() + 160);
        for (size_t iph = 0; iph < 5; ++iph) {
            if ((includeBoolPropsMask & (1u << static_cast<unsigned>(iph))) == 0) {
                continue;
            }
            const char *prop = kInteractiveProps[iph];
            const int bit = (patch >> static_cast<int>(iph)) & 1;
            baseXPath.append("[@");
            baseXPath.append(prop);
            baseXPath.append("=");
            baseXPath.append(bit ? "'true'" : "'false'");
            baseXPath.append("]");
        }
        if (includeScrollType) {
            baseXPath.append("[@scroll-type='");
            baseXPath.append(scrollTypeAttrValue(scrollType));
            baseXPath.append("']");
        }
    }

} // namespace

/** Returns `baseXPath` with interactive and optional scroll-type predicates appended. */
std::string appendActionPatchXPathSuffix(std::string baseXPath, int patch, ScrollType scrollType,
                                         uint8_t includeBoolPropsMask, bool includeScrollType) {
    appendInteractiveTokens(baseXPath, patch, scrollType, includeBoolPropsMask, includeScrollType);
    return baseXPath;
}

/** Packs the five interactive flags from `node` into an integer (same bit order as `kInteractiveProps`). */
int actionPatchBitsForNode(const gui_tree::GUITreeNode &node) {
    int flag = 0;
    for (int i = 4; i >= 0; --i) {
        flag <<= 1;
        const char *prop = kInteractiveProps[static_cast<size_t>(i)];
        bool v = false;
        if (std::strcmp(prop, "enabled") == 0) {
            v = node.isEnabled();
        } else if (std::strcmp(prop, "clickable") == 0) {
            v = node.isClickable();
        } else if (std::strcmp(prop, "checkable") == 0) {
            v = node.isCheckable();
        } else if (std::strcmp(prop, "long-clickable") == 0) {
            v = node.isLongClickable();
        } else if (std::strcmp(prop, "scrollable") == 0) {
            v = (node.getScrollable() != 0);
        }
        if (v) {
            flag |= 1;
        }
    }
    return flag;
}

/** Scroll classification for XPath tokens derived from the node’s scroll bits and class name. */
ScrollType actionPatchScrollTypeForNode(const gui_tree::GUITreeNode &node) {
    return scrollTypeFromNodeImpl(node.getScrollable(), node.getClassName().c_str());
}

/** Snapshot of the current global action-patch token configuration. */
ActionPatchTokenConfig getActionPatchTokenConfig() {
    return g_cfg;
}

/**
 * Parses a profile string (`default`, `full`, `no_scroll_type`, `no_enabled`, hex masks, etc.)
 * and updates which predicates appear in tokens and whether scroll-type is included.
 */
void setActionPatchProfile(const std::string &value) {
    const std::string trimmed = trimCopy(value);
    g_cfg.profile = trimmed;
    const std::string lower = toLowerCopy(trimmed);
    if (lower.empty() || lower == "default" || lower == "full" || lower == "ape" || lower == "all") {
        g_cfg.include_bool_props_mask = 0x1Fu;
        g_cfg.include_scroll_type = false;
        return;
    }
    if (lower == "no_scroll_type" || lower == "noscrolltype" || lower == "no_scroll") {
        g_cfg.include_bool_props_mask = 0x1Fu;
        g_cfg.include_scroll_type = false;
        return;
    }
    if (lower == "no_enabled" || lower == "interaction") {
        // All flags except `enabled` (bit 0).
        g_cfg.include_bool_props_mask = 0x1Eu;
        g_cfg.include_scroll_type = false;
        return;
    }
    if (lower.rfind("0x", 0) == 0 || lower.rfind("0X", 0) == 0) {
        char *end = nullptr;
        unsigned long m = std::strtoul(lower.c_str(), &end, 0);
        g_cfg.include_bool_props_mask = static_cast<uint8_t>(m & 0xFFu);
        g_cfg.include_scroll_type = false;
        if (end && std::strstr(end, "noscroll") != nullptr) {
            g_cfg.include_scroll_type = false;
        }
        return;
    }
    g_cfg.include_bool_props_mask = 0x1Fu;
    g_cfg.include_scroll_type = false;
}

/** Last profile string passed to `setActionPatchProfile`. */
const std::string &getActionPatchProfile() {
    return g_cfg.profile;
}

/** Whether planners should derive concrete actions from encoded action-patch names. */
bool useActionPatchDeriveActionsFromName() {
    return g_cfg.derive_actions_from_name;
}

/** Enables or disables deriving actions from action-patch name metadata. */
void setActionPatchDeriveActionsFromName(bool enabled) {
    g_cfg.derive_actions_from_name = enabled;
}

/**
 * If `name` is an `ActionPatchName`, writes derived allowed and known-kind masks and returns true;
 * otherwise clears outputs and returns false.
 */
bool decodeApeDerivedActionsFromName(const NamePtr &name, uint32_t *outAllowedActionMask,
                                     uint32_t *outKnownActionKinds) {
    if (!outAllowedActionMask || !outKnownActionKinds) {
        return false;
    }
    *outAllowedActionMask = 0;
    *outKnownActionKinds = 0;
    const auto ap = std::dynamic_pointer_cast<ActionPatchName>(name);
    if (!ap) {
        return false;
    }
    *outAllowedActionMask = ap->derivedActionMask();
    *outKnownActionKinds = ap->derivedActionKnownKinds();
    return true;
}

/** Builds cached XPath text and derived-action bitmasks from base name, patch bits, and inclusion flags. */
ActionPatchName::ActionPatchName(NamerPtr namer, NamePtr baseName, int patch, ScrollType scrollType,
                                 uint8_t includeBoolPropsMask, bool includeScrollType,
                                 std::string xpath_full_override)
    : namer_(std::move(namer)),
      base_(std::move(baseName)),
      patch_(patch),
      scroll_type_(scrollType),
      include_bool_props_mask_(includeBoolPropsMask),
      include_scroll_type_(includeScrollType) {
    derived_action_mask_ =
        buildDerivedActionMask(patch_, scroll_type_, include_bool_props_mask_, include_scroll_type_);
    derived_action_known_kinds_ = buildDerivedActionKnownKinds(include_bool_props_mask_, include_scroll_type_);
    if (!xpath_full_override.empty()) {
        xpath_full_ = std::move(xpath_full_override);
        return;
    }
    if (!base_) {
        xpath_full_ =
            appendActionPatchXPathSuffix(std::string(), patch_, scroll_type_, include_bool_props_mask_,
                                         include_scroll_type_);
    } else {
        xpath_full_ = appendActionPatchXPathSuffix(base_->toXPath(), patch_, scroll_type_, include_bool_props_mask_,
                                                   include_scroll_type_);
    }
}

/** Returns the fully materialized XPath string including interactive predicates. */
const std::string &ActionPatchName::toXPath() const {
    return xpath_full_;
}

/** Appends only this layer’s predicates to `sb`, after any base name’s local properties. */
void ActionPatchName::appendXPathLocalProperties(std::string &sb) const {
    if (base_) {
        base_->appendXPathLocalProperties(sb);
    }
    appendInteractiveTokens(sb, patch_, scroll_type_, include_bool_props_mask_, include_scroll_type_);
}

/** Wraps `base` to append interactive predicates on top of its names. */
ActionPatchNamer::ActionPatchNamer(NamerPtr base) : base_(std::move(base)) {}

/** Delegates to the underlying namer, or returns an empty vector if there is no base. */
std::vector<NamerType> ActionPatchNamer::getNamerTypes() const {
    return base_ ? base_->getNamerTypes() : std::vector<NamerType>{};
}

/** Dimension mask forwarded from the base namer. */
uint32_t ActionPatchNamer::typeDimensionMask() const {
    return base_ ? base_->typeDimensionMask() : 0u;
}

/** Names the node with the base namer, then wraps the result in an `ActionPatchName` for the node state. */
NamePtr ActionPatchNamer::naming(gui_tree::GUITreeNode &node) {
    NamePtr baseName = base_ ? base_->naming(node) : nullptr;
    const int p = actionPatchBitsForNode(node);
    const ActionPatchTokenConfig cfg = getActionPatchTokenConfig();
    const ScrollType st =
        normalizeScrollTypeForToken(actionPatchScrollTypeForNode(node), cfg.include_scroll_type,
                                    cfg.include_bool_props_mask);
    return cacheName(std::make_shared<ActionPatchName>(
        shared_from_this(), std::move(baseName), p, st, cfg.include_bool_props_mask, cfg.include_scroll_type));
}

/**
 * Like `naming`, but builds XPath from `xpathKey` (must be the base key without action-patch suffix);
 * appends predicates exactly once.
 */
NamePtr ActionPatchNamer::namingWithXPathKey(gui_tree::GUITreeNode &node, const std::string &xpathKey) {
    const int p = actionPatchBitsForNode(node);
    const ActionPatchTokenConfig cfg = getActionPatchTokenConfig();
    const ScrollType st =
        normalizeScrollTypeForToken(actionPatchScrollTypeForNode(node), cfg.include_scroll_type,
                                    cfg.include_bool_props_mask);
    // `xpathKey` must be the base key only so predicates are not duplicated during rebuild.
    std::string full = appendActionPatchXPathSuffix(std::string(xpathKey), p, st, cfg.include_bool_props_mask,
                                                    cfg.include_scroll_type);
    return cacheName(std::make_shared<ActionPatchName>(
        shared_from_this(), nullptr, p, st, cfg.include_bool_props_mask, cfg.include_scroll_type,
        std::move(full)));
}

/**
 * Returns the base namer’s key for `node` (no interactive predicates), falling back to the base XPath if needed.
 */
std::string ActionPatchNamer::xpathKeyForNode(gui_tree::GUITreeNode &node) const {
    std::string k = base_ ? base_->xpathKeyForNode(node) : std::string();
    if (k.empty() && base_) {
        NamePtr n = base_->naming(node);
        if (n) {
            k = n->toXPath();
        }
    }
    // The key must omit action-patch predicates so a later pass can append them once.
    return k;
}

/** True if this namer’s dimension mask includes every dimension set in `other`. */
bool ActionPatchNamer::refinesTo(const Namer &other) const {
    uint32_t om = other.typeDimensionMask();
    const uint32_t mm = typeDimensionMask();
    return (mm & om) == om;
}

} // namespace naming
} // namespace fastbotx
