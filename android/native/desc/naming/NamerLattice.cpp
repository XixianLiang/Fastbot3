#include "NamerLattice.h"
#include "BitmaskNamer.h"

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

    uint32_t maskOf(const Namer &n) {
        const auto *b = dynamic_cast<const BitmaskNamer *>(&n);
        if (b) {
            return b->getMask();
        }
        uint32_t m = 0;
        for (NamerType t : n.getNamerTypes()) {
            m |= (1u << static_cast<unsigned>(t));
        }
        return m;
    }

} // namespace

    NamerLattice::NamerLattice(const NamerFactory &factory) : factory_(&factory) {}

    std::vector<NamerPtr> NamerLattice::immediateRefinements(const NamerPtr &coarse) const {
        std::vector<NamerPtr> out;
        if (!coarse) {
            return out;
        }
        const uint32_t cm = maskOf(*coarse);
        const int cpc = popcount32(cm);
        for (const auto &n : factory_->all()) {
            const uint32_t fm = n->getMask();
            if (fm == cm) {
                continue;
            }
            if (popcount32(fm) != cpc + 1) {
                continue;
            }
            if ((fm & cm) != cm) {
                continue;
            }
            if (!n->refinesTo(*coarse)) {
                continue;
            }
            out.push_back(n);
        }
        std::sort(out.begin(), out.end(), [](const NamerPtr &a, const NamerPtr &b) {
            if (!a || !b) {
                return a.get() < b.get();
            }
            const uint32_t ma = maskOf(*a);
            const uint32_t mb = maskOf(*b);
            if (ma != mb) {
                return ma < mb;
            }
            return a.get() < b.get();
        });
        return out;
    }

    std::vector<NamerPtr> NamerLattice::immediateAbstractions(const NamerPtr &fine) const {
        std::vector<NamerPtr> out;
        if (!fine) {
            return out;
        }
        const uint32_t fm = maskOf(*fine);
        const int fpc = popcount32(fm);
        if (fpc == 0) {
            return out;
        }
        for (const auto &n : factory_->all()) {
            const uint32_t cm = n->getMask();
            if (cm == fm) {
                continue;
            }
            if (popcount32(cm) != fpc - 1) {
                continue;
            }
            if ((fm & cm) != cm) {
                continue;
            }
            if (!fine->refinesTo(*n)) {
                continue;
            }
            out.push_back(n);
        }
        std::sort(out.begin(), out.end(), [](const NamerPtr &a, const NamerPtr &b) {
            if (!a || !b) {
                return a.get() < b.get();
            }
            const uint32_t ma = maskOf(*a);
            const uint32_t mb = maskOf(*b);
            if (ma != mb) {
                return ma < mb;
            }
            return a.get() < b.get();
        });
        return out;
    }

} // namespace naming
} // namespace fastbotx
