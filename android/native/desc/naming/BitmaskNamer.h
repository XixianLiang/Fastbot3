/**
 * @authors Zhao Zhang
 */
/**
 * `BitmaskNamer` implements `Namer` from a bitset of `NamerType` dimensions (TYPE, TEXT, INDEX, PARENT,
 * ANCESTOR). Each set bit selects part of the XPath string built from a `GUITreeNode`: local predicates,
 * parent-relative paths (`//*` + segments), or ancestor paths. See `BitmaskNamer.cpp` for exact fragments.
 */
#ifndef FASTBOTX_DESC_NAMING_BITMASKNAMER_H_
#define FASTBOTX_DESC_NAMING_BITMASKNAMER_H_

#include "Namer.h"

#include <cstdint>
#include <memory>

namespace fastbotx {
namespace gui_tree {
    class GUITreeNode;
}
namespace naming {

    class BitmaskNamer : public Namer, public std::enable_shared_from_this<BitmaskNamer> {
    public:
        /** Constructs a namer with the given dimension mask; prefer over raw constructor for `shared_ptr` usage. */
        static std::shared_ptr<BitmaskNamer> create(uint32_t mask);

        /** Raw bitmask of enabled `NamerType` bits (same value as `typeDimensionMask()`). */
        uint32_t getMask() const { return mask_; }

        uint32_t typeDimensionMask() const override { return mask_; }

        /** Sorted list of `NamerType` values corresponding to set bits in `mask_`. */
        std::vector<NamerType> getNamerTypes() const override;

        /** Materializes a `LocalXPathName` or `FullPathName` for `node` according to `mask_`. */
        NamePtr naming(gui_tree::GUITreeNode &node) override;

        /**
         * Wraps a precomputed XPath key: for local-only masks, `xpathKey` is `//*` plus predicate tail;
         * when PARENT/ANCESTOR bits are set, `xpathKey` is the full path string.
         */
        NamePtr namingWithXPathKey(gui_tree::GUITreeNode &node,
                                    const std::string &xpathKey) override;

        /** Canonical XPath key string used for caching and rebuild (matches `naming` output shape). */
        std::string xpathKeyForNode(gui_tree::GUITreeNode &node) const override;

        /** True if `mask_` includes every dimension bit set on `other`. */
        bool refinesTo(const Namer &other) const override;

    private:
        explicit BitmaskNamer(uint32_t mask);

        /** Bitset of active naming dimensions; see `NamerType`. */
        uint32_t mask_{0};
    };

} // namespace naming
} // namespace fastbotx

#endif
