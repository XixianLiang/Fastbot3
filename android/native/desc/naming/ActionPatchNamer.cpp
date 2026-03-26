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
#include "../gui_tree/GUITreeNode.h"

#include <cstring>

namespace fastbotx {
namespace naming {
namespace {

    // Java ActionPatchNamer.interactiveProperties order; toXPath iterates same order (LSB = enabled).
    const char *const kInteractiveProps[5] = {"enabled", "clickable", "checkable", "long-clickable",
                                                "scrollable"};

} // namespace

    std::string appendActionPatchXPathSuffix(const std::string &baseXPath, int patch) {
        std::string out = baseXPath;
        out.reserve(baseXPath.size() + 128);
        int k = patch;
        for (const char *prop : kInteractiveProps) {
            out.append("[@");
            out.append(prop);
            out.append("=");
            out.append((k % 2 == 1) ? "'true'" : "'false'");
            out.append("]");
            k >>= 1;
        }
        return out;
    }

    int actionPatchBitsForNode(const gui_tree::GUITreeNode &node) {
        // Java patch(): for i from len-1 down to 0, flag = flag<<1 | bit
        int flag = 0;
        for (int i = 4; i >= 0; --i) {
            flag <<= 1;
            const char *prop = kInteractiveProps[i];
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

    ActionPatchName::ActionPatchName(NamerPtr namer, NamePtr baseName, int patch)
        : namer_(std::move(namer)),
          base_(std::move(baseName)),
          patch_(patch) {}

    std::string ActionPatchName::toXPath() const {
        if (!base_) {
            return appendActionPatchXPathSuffix("", patch_);
        }
        return appendActionPatchXPathSuffix(base_->toXPath(), patch_);
    }

    void ActionPatchName::appendXPathLocalProperties(std::string &sb) const {
        if (base_) {
            base_->appendXPathLocalProperties(sb);
        }
        int k = patch_;
        for (const char *prop : kInteractiveProps) {
            sb.append("[@");
            sb.append(prop);
            sb.append("=");
            sb.append((k % 2 == 1) ? "'true'" : "'false'");
            sb.append("]");
            k >>= 1;
        }
    }

    ActionPatchNamer::ActionPatchNamer(NamerPtr base) : base_(std::move(base)) {}

    std::vector<NamerType> ActionPatchNamer::getNamerTypes() const {
        return base_ ? base_->getNamerTypes() : std::vector<NamerType>{};
    }

    uint32_t ActionPatchNamer::typeDimensionMask() const {
        return base_ ? base_->typeDimensionMask() : 0u;
    }

    NamePtr ActionPatchNamer::naming(gui_tree::GUITreeNode &node) {
        NamePtr baseName = base_ ? base_->naming(node) : nullptr;
        const int p = actionPatchBitsForNode(node);
        return std::make_shared<ActionPatchName>(shared_from_this(), std::move(baseName), p);
    }

    std::string ActionPatchNamer::xpathKeyForNode(gui_tree::GUITreeNode &node) const {
        std::string k = base_ ? base_->xpathKeyForNode(node) : std::string();
        if (k.empty() && base_) {
            NamePtr n = base_->naming(node);
            if (n) {
                k = n->toXPath();
            }
        }
        return appendActionPatchXPathSuffix(k, actionPatchBitsForNode(node));
    }

    bool ActionPatchNamer::refinesTo(const Namer &other) const {
        uint32_t om = other.typeDimensionMask();
        const uint32_t mm = typeDimensionMask();
        return (mm & om) == om;
    }

} // namespace naming
} // namespace fastbotx
