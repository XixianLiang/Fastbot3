/**
 * @authors Zhao Zhang
 *
 * XPath fragments align with APE Java naming:
 * - TypeNamer: [@class="..."][@resource-id="..."]
 * - TextNamer: [@text="..."][@content-desc="..."] (Java TextName.appendXPathLocalProperties)
 * - IndexNamer: [@index=N] (numeric, no quotes)
 * - Compound order: TYPE, TEXT, INDEX (Java CompoundNamer order for typeTextIndex)
 * - ParentNamer: recursive parent.toXPath, then child segment with slash-star + local predicates
 * - AncestorNamer: slash-star + local per level root→leaf; uniform local mask unless PARENT in mask,
 *   then per-node local mask from NamingRuntime (APE useParent branch).
 */

#include "BitmaskNamer.h"
#include "LocalXPathName.h"
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

    bool isEditTextClassName(const std::string &cls) {
        const char *p = cls.c_str();
        const size_t len = cls.size();
        return (len == 23 && std::strcmp(p, "android.widget.EditText") == 0) ||
               (len == 42 && std::strcmp(p, "android.inputmethodservice.ExtractEditText") == 0) ||
               (len == 35 && std::strcmp(p, "android.widget.AutoCompleteTextView") == 0) ||
               (len == 42 && std::strcmp(p, "android.widget.MultiAutoCompleteTextView") == 0);
    }

    bool hasMask(uint32_t mask, NamerType t) {
        int b = static_cast<int>(t);
        if (b < 0 || b >= kMaxNamerTypeBits) {
            return false;
        }
        return (mask & (1u << static_cast<unsigned>(b))) != 0;
    }

    /** Strip PARENT and ANCESTOR (Java NamerFactory.getLocalNamer). */
    uint32_t localMaskOnly(uint32_t mask) {
        constexpr uint32_t parentBit = 1u << static_cast<unsigned>(NamerType::PARENT);
        constexpr uint32_t ancestorBit = 1u << static_cast<unsigned>(NamerType::ANCESTOR);
        return mask & ~(parentBit | ancestorBit);
    }

    /** Java NamerFactory.escapeToXPathString: only escape double quotes. */
    std::string escapeJavaXPathString(const std::string &origin) {
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

    void appendLocalType(std::string &sb, const gui_tree::GUITreeNode &node) {
        sb.append("[@class=\"");
        sb.append(escapeJavaXPathString(node.getClassName()));
        sb.append("\"][@resource-id=\"");
        sb.append(escapeJavaXPathString(node.getResourceId()));
        sb.append("\"]");
    }

    void appendLocalText(std::string &sb, const gui_tree::GUITreeNode &node) {
        const std::string textPred =
            isEditTextClassName(node.getClassName())
                ? std::string()
                : ape_text::normalizeTextForApe(node.getText().c_str());
        const std::string cdPred = ape_text::normalizeContentDescForApe(node.getContentDesc().c_str());
        sb.append("[@text=\"");
        sb.append(escapeJavaXPathString(textPred));
        sb.append("\"][@content-desc=\"");
        sb.append(escapeJavaXPathString(cdPred));
        sb.append("\"]");
    }

    void appendLocalIndex(std::string &sb, const gui_tree::GUITreeNode &node) {
        sb.append("[@index=");
        sb.append(std::to_string(node.getIndex()));
        sb.append("]");
    }

    /**
     * Java CompoundNamer concatenates in namer order: typeTextIndex => TYPE, TEXT, INDEX.
     */
    void appendLocalPredicates(std::string &sb, uint32_t locMask, gui_tree::GUITreeNode &node) {
        // Compound order: TYPE, TEXT, INDEX (Java CompoundNamer order).
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

    std::string buildLocalPredicates(uint32_t locMask, gui_tree::GUITreeNode &node) {
        std::string sb;
        sb.reserve(128);
        appendLocalPredicates(sb, locMask, node);
        return sb;
    }

    void collectChainRootToLeaf(gui_tree::GUITreeNode *leaf, std::vector<gui_tree::GUITreeNode *> *out) {
        out->clear();
        for (gui_tree::GUITreeNode *x = leaf; x != nullptr;) {
            out->push_back(x);
            auto pw = x->getParent();
            x = pw ? pw.get() : nullptr;
        }
        std::reverse(out->begin(), out->end());
    }

    // Java ParentName: parent.toXPath, then slash-star + local; empty parent uses //* then slash-star + local.
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

    /** Java AncestorNamer with useParent: per-node getLocalNamer(getNodeNamer(node)). */
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

    std::shared_ptr<BitmaskNamer> BitmaskNamer::create(uint32_t mask) {
        return std::shared_ptr<BitmaskNamer>(new BitmaskNamer(mask));
    }

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

    NamePtr BitmaskNamer::naming(gui_tree::GUITreeNode &node) {
        const bool hasP = hasMask(mask_, NamerType::PARENT);
        const bool hasA = hasMask(mask_, NamerType::ANCESTOR);
        const uint32_t locBase = localMaskOnly(mask_);

        if (!hasP && !hasA) {
            return std::make_shared<LocalXPathName>(shared_from_this(), buildLocalPredicates(locBase, node));
        }
        if (!hasA && hasP) {
            return std::make_shared<FullPathName>(shared_from_this(), buildParentChainXPath(&node, locBase));
        }
        if (hasA && !hasP) {
            return std::make_shared<FullPathName>(shared_from_this(), buildAncestorUniformXPath(node, locBase));
        }
        const auto *map = namingEvalNodeToNamer();
        return std::make_shared<FullPathName>(shared_from_this(),
                                              buildAncestorPerNodeXPath(node, locBase, map));
    }

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
            return std::make_shared<LocalXPathName>(shared_from_this(), std::move(predicates));
        }

        // For PARENT/ANCESTOR modes, xpathKey is already the full path XPath.
        return std::make_shared<FullPathName>(shared_from_this(), xpathKey);
    }

    std::string BitmaskNamer::xpathKeyForNode(gui_tree::GUITreeNode &node) const {
        return canonicalXPathString(mask_, node);
    }

    bool BitmaskNamer::refinesTo(const Namer &other) const {
        const uint32_t om = other.typeDimensionMask();
        return (mask_ & om) == om;
    }

} // namespace naming
} // namespace fastbotx
