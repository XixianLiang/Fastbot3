#include "NamerFactory.h"
#include "NamerType.h"

#include <algorithm>

namespace fastbotx {
namespace naming {
namespace {

    int popcount32(uint32_t x) {
        int c = 0;
        while (x) {
            c++;
            x &= x - 1u;
        }
        return c;
    }

} // namespace

    NamerFactory::NamerFactory() {
        const auto &used = namerTypesUsed();
        const size_t n = used.size();
        const uint32_t limit = n >= 32 ? 0u : (1u << static_cast<unsigned>(n));

        for (uint32_t subset = 0; subset < limit; ++subset) {
            uint32_t mask = 0;
            for (size_t i = 0; i < n; ++i) {
                if (subset & (1u << static_cast<unsigned>(i))) {
                    const int b = static_cast<int>(used[i]);
                    mask |= (1u << static_cast<unsigned>(b));
                }
            }
            auto bn = BitmaskNamer::create(mask);
            by_mask_[mask] = bn;
            ordered_.push_back(bn);
        }

        std::sort(ordered_.begin(), ordered_.end(),
                  [](const std::shared_ptr<BitmaskNamer> &a, const std::shared_ptr<BitmaskNamer> &b) {
                      const uint32_t ma = a->getMask();
                      const uint32_t mb = b->getMask();
                      const int pa = popcount32(ma);
                      const int pb = popcount32(mb);
                      if (pa != pb) {
                          return pa < pb;
                      }
                      return ma < mb;
                  });

        auto it = by_mask_.find(0);
        if (it != by_mask_.end()) {
            empty_ = it->second;
        }
    }

    const NamerFactory NamerFactory::CURRENT{};

    std::shared_ptr<BitmaskNamer> NamerFactory::getByMask(uint32_t mask) const {
        auto it = by_mask_.find(mask);
        if (it == by_mask_.end()) {
            return nullptr;
        }
        return it->second;
    }

} // namespace naming
} // namespace fastbotx
