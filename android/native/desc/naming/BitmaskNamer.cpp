/**
 * @authors Zhao Zhang
 *
 * XPath fragments produced from widget fields:
 * - Type: `[@class="…"][@resource-id="…"]`
 * - Text: `[@text="…"][@content-desc="…"]` (display strings run through the shared text normalizer)
 * - Index: `[@index=N]` (numeric, unquoted)
 * - Local compound order on one node: TYPE, then TEXT, then INDEX.
 * - Parent mode: `//*` then `/*` + locals along root→leaf (see `buildParentChainXPath`).
 * - Ancestor mode: `/*` + locals per level root→leaf; uniform local mask, or per-node masks from `NamingRuntime`
 *   when both PARENT and ANCESTOR bits are set.
 */

#include "BitmaskNamer.h"
#include "LocalXPathName.h"
#include "NameManager.h"
#include "NamingRuntime.h"
#include "../ApeTextNormalize.h"
#include "../gui_tree/GUITreeNode.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace fastbotx {
namespace naming {
namespace {

    static constexpr int kMaxNamerTypeBits = 8;

    /** True for EditText-like classes where displayed text is intentionally omitted from the text predicate. */
    bool isEditTextClassName(const std::string &cls) {
        const char *p = cls.c_str();
        const size_t len = cls.size();
        return (len == 23 && std::strcmp(p, "android.widget.EditText") == 0) ||
               (len == 42 && std::strcmp(p, "android.inputmethodservice.ExtractEditText") == 0) ||
               (len == 35 && std::strcmp(p, "android.widget.AutoCompleteTextView") == 0) ||
               (len == 42 && std::strcmp(p, "android.widget.MultiAutoCompleteTextView") == 0);
    }

    /** True if bit `t` is set in `mask` (only the first `kMaxNamerTypeBits` bits are considered). */
    bool hasMask(uint32_t mask, NamerType t) {
        int b = static_cast<int>(t);
        if (b < 0 || b >= kMaxNamerTypeBits) {
            return false;
        }
        return (mask & (1u << static_cast<unsigned>(b))) != 0;
    }

    /** Clears PARENT and ANCESTOR bits, leaving only local-dimension flags (TYPE/TEXT/INDEX). */
    uint32_t localMaskOnly(uint32_t mask) {
        constexpr uint32_t parentBit = 1u << static_cast<unsigned>(NamerType::PARENT);
        constexpr uint32_t ancestorBit = 1u << static_cast<unsigned>(NamerType::ANCESTOR);
        return mask & ~(parentBit | ancestorBit);
    }

    /** Escapes `"` as `\"` inside XPath double-quoted attribute values. */
    std::string escapeDoubleQuotedXPath(const std::string &origin) {
        std::string out;
        out.reserve(origin.size() + 4);
        for (char c : origin) {
            if (c == '"') {
                out += "\\\"";
            } else {
                out += c;
            }
        }
        return out;
    }

    /** Appends class and resource-id predicates for the node. */
    void appendLocalType(std::string &sb, const gui_tree::GUITreeNode &node) {
        sb.append("[@class=\"");
        sb.append(escapeDoubleQuotedXPath(node.getClassName()));
        sb.append("\"][@resource-id=\"");
        sb.append(escapeDoubleQuotedXPath(node.getResourceId()));
        sb.append("\"]");
    }

    /** Appends text and content-desc predicates (normalized); EditText-like classes use empty text predicate. */
    void appendLocalText(std::string &sb, const gui_tree::GUITreeNode &node) {
        const std::string textPred =
            isEditTextClassName(node.getClassName())
                ? std::string()
                : ape_text::normalizeTextForApe(node.getText().c_str());
        const std::string cdPred = ape_text::normalizeContentDescForApe(node.getContentDesc().c_str());
        sb.append("[@text=\"");
        sb.append(escapeDoubleQuotedXPath(textPred));
        sb.append("\"][@content-desc=\"");
        sb.append(escapeDoubleQuotedXPath(cdPred));
        sb.append("\"]");
    }

    /** Appends the numeric sibling index predicate. */
    void appendLocalIndex(std::string &sb, const gui_tree::GUITreeNode &node) {
        sb.append("[@index=");
        sb.append(std::to_string(node.getIndex()));
        sb.append("]");
    }

    /** Appends TYPE/TEXT/INDEX predicate groups for `node` according to `locMask`, in fixed order. */
    void appendLocalPredicates(std::string &sb, uint32_t locMask, gui_tree::GUITreeNode &node) {
        if (hasMask(locMask, NamerType::TYPE)) {
            appendLocalType(sb, node);
        }
        if (hasMask(locMask, NamerType::TEXT)) {
            appendLocalText(sb, node);
        }
        if (hasMask(locMask, NamerType::INDEX)) {
            appendLocalIndex(sb, node);
        }
    }

    /** Returns the concatenated local predicate string for `node`. */
    std::string buildLocalPredicates(uint32_t locMask, gui_tree::GUITreeNode &node) {
        std::string sb;
        sb.reserve(128);
        appendLocalPredicates(sb, locMask, node);
        return sb;
    }

    /** Fills `out` with pointers from root to `leaf` along parent weak links. */
    void collectChainRootToLeaf(gui_tree::GUITreeNode *leaf, std::vector<gui_tree::GUITreeNode *> *out) {
        out->clear();
        for (gui_tree::GUITreeNode *x = leaf; x != nullptr;) {
            out->push_back(x);
            auto pw = x->getParent();
            x = pw ? pw.get() : nullptr;
        }
        std::reverse(out->begin(), out->end());
    }

    /**
     * Parent-relative path: `//*` then one `/*` + locals segment per node on the chain from root to `node`.
     * A null `node` yields `//*` only.
     */
    std::string buildParentChainXPath(gui_tree::GUITreeNode *node, uint32_t locMask) {
        if (!node) {
            return "//*";
        }

        // Build root->leaf chain once, then append "//*" + "/*local" for each node.
        // This is equivalent to the previous recursive implementation.
        std::vector<gui_tree::GUITreeNode *> chain;
        collectChainRootToLeaf(node, &chain);
        std::string s;
        s.reserve(chain.size() * 32 + 4);
        s.append("//*");
        for (gui_tree::GUITreeNode *n : chain) {
            s.append("/*");
            appendLocalPredicates(s, locMask, *n);
        }
        return s;
    }

    /** Ancestor path without leading `//*`: `/*` + same local mask at every level root→`node`. */
    std::string buildAncestorUniformXPath(gui_tree::GUITreeNode &node, uint32_t locMask) {
        std::vector<gui_tree::GUITreeNode *> chain;
        collectChainRootToLeaf(&node, &chain);
        std::string s;
        s.reserve(chain.size() * 32);
        for (gui_tree::GUITreeNode *n : chain) {
            s.append("/*");
            appendLocalPredicates(s, locMask, *n);
        }
        return s;
    }

    /** Resolves a per-node local mask from `map`, or returns `fallbackLoc` if unmapped. */
    uint32_t localMaskForNodeFromMap(const gui_tree::GUITreeNode *n,
                                     const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *map,
                                     uint32_t fallbackLoc) {
        if (!map || !n) {
            return fallbackLoc;
        }
        auto it = map->find(n);
        if (it == map->end() || it->second == nullptr) {
            return fallbackLoc;
        }
        return localMaskOnly(it->second->typeDimensionMask());
    }

    /**
     * Ancestor path with a potentially different local mask at each level, taken from `map`
     * when present (combined PARENT+ANCESTOR mode).
     */
    std::string buildAncestorPerNodeXPath(gui_tree::GUITreeNode &node, uint32_t fallbackLoc,
                                          const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *map) {
        std::vector<gui_tree::GUITreeNode *> chain;
        collectChainRootToLeaf(&node, &chain);
        std::string s;
        s.reserve(chain.size() * 48);
        for (gui_tree::GUITreeNode *n : chain) {
            const uint32_t lm = localMaskForNodeFromMap(n, map, fallbackLoc);
            s.append("/*");
            appendLocalPredicates(s, lm, *n);
        }
        return s;
    }

    /** Canonical XPath key string for `node` according to dimension bits in `mask`. */
    std::string canonicalXPathString(uint32_t mask, gui_tree::GUITreeNode &node) {
        const bool hasP = hasMask(mask, NamerType::PARENT);
        const bool hasA = hasMask(mask, NamerType::ANCESTOR);
        const uint32_t locBase = localMaskOnly(mask);

        if (!hasP && !hasA) {
            return localXPathToXPathWithPredicateTail(buildLocalPredicates(locBase, node));
        }
        if (!hasA && hasP) {
            return buildParentChainXPath(&node, locBase);
        }
        if (hasA && !hasP) {
            return buildAncestorUniformXPath(node, locBase);
        }
        const auto *map = namingEvalNodeToNamer();
        return buildAncestorPerNodeXPath(node, locBase, map);
    }

} // namespace

    BitmaskNamer::BitmaskNamer(uint32_t mask) : mask_(mask) {}

    /** Shared factory so `enable_shared_from_this` is used consistently. */
    std::shared_ptr<BitmaskNamer> BitmaskNamer::create(uint32_t mask) {
        return std::shared_ptr<BitmaskNamer>(new BitmaskNamer(mask));
    }

    /** Lists set bits in `mask_` as `NamerType` values in ascending numeric order. */
    std::vector<NamerType> BitmaskNamer::getNamerTypes() const {
        std::vector<NamerType> out;
        for (int i = 0; i < kMaxNamerTypeBits; ++i) {
            if (mask_ & (1u << static_cast<unsigned>(i))) {
                out.push_back(static_cast<NamerType>(i));
            }
        }
        std::sort(out.begin(), out.end(), [](NamerType a, NamerType b) {
            return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
        });
        return out;
    }

    /** Builds a cached `LocalXPathName` or `FullPathName` from `node` per `mask_` (local / parent / ancestor modes). */
    NamePtr BitmaskNamer::naming(gui_tree::GUITreeNode &node) {
        const bool hasP = hasMask(mask_, NamerType::PARENT);
        const bool hasA = hasMask(mask_, NamerType::ANCESTOR);
        const uint32_t locBase = localMaskOnly(mask_);

        if (!hasP && !hasA) {
            return cacheName(std::make_shared<LocalXPathName>(
                shared_from_this(), buildLocalPredicates(locBase, node)));
        }
        if (!hasA && hasP) {
            return cacheName(std::make_shared<FullPathName>(
                shared_from_this(), buildParentChainXPath(&node, locBase)));
        }
        if (hasA && !hasP) {
            return cacheName(std::make_shared<FullPathName>(
                shared_from_this(), buildAncestorUniformXPath(node, locBase)));
        }
        const auto *map = namingEvalNodeToNamer();
        return cacheName(std::make_shared<FullPathName>(
            shared_from_this(), buildAncestorPerNodeXPath(node, locBase, map)));
    }

    /**
     * Reconstructs a name from `xpathKey`: for pure-local mode, strips the `//*` prefix and keeps predicates;
     * for parent/ancestor modes, `xpathKey` is already the full path string.
     */
    NamePtr BitmaskNamer::namingWithXPathKey(gui_tree::GUITreeNode &node,
                                              const std::string &xpathKey) {
        (void)node;
        const bool hasP = hasMask(mask_, NamerType::PARENT);
        const bool hasA = hasMask(mask_, NamerType::ANCESTOR);

        if (!hasP && !hasA) {
            // xpathKey is "//*" or "//*<predicates>" produced by localXPathToXPathWithPredicateTail().
            std::string predicates;
            if (xpathKey.rfind("//*", 0) == 0) {
                predicates = xpathKey.substr(3);
            }
            return cacheName(std::make_shared<LocalXPathName>(shared_from_this(), std::move(predicates)));
        }

        // For PARENT/ANCESTOR modes, xpathKey is already the full path XPath.
        return cacheName(std::make_shared<FullPathName>(shared_from_this(), xpathKey));
    }

    /** Delegates to `canonicalXPathString` with this namer’s `mask_`. */
    std::string BitmaskNamer::xpathKeyForNode(gui_tree::GUITreeNode &node) const {
        return canonicalXPathString(mask_, node);
    }

    /** True if this namer’s mask includes every dimension bit set in `other`. */
    bool BitmaskNamer::refinesTo(const Namer &other) const {
        const uint32_t om = other.typeDimensionMask();
        return (mask_ & om) == om;
    }

} // namespace naming
} // namespace fastbotx
