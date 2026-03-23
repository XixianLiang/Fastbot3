/**
 * @authors Zhao Zhang
 */
 /*
 * Namer keyed by NamerType bitmask (APE AbstractNamer + concrete namers, unified).
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
        static std::shared_ptr<BitmaskNamer> create(uint32_t mask);

        uint32_t getMask() const { return mask_; }

        std::vector<NamerType> getNamerTypes() const override;

        NamePtr naming(gui_tree::GUITreeNode &node) override;

        std::string xpathKeyForNode(gui_tree::GUITreeNode &node) const override;

        bool refinesTo(const Namer &other) const override;

    private:
        explicit BitmaskNamer(uint32_t mask);

        uint32_t mask_{0};
    };

} // namespace naming
} // namespace fastbotx

#endif
