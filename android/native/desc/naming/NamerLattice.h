/*
 * Refinement neighborhood on the NamerFactory bitmask cube (APE NamerLattice).
 */
#ifndef FASTBOTX_DESC_NAMING_NAMERLATTICE_H_
#define FASTBOTX_DESC_NAMING_NAMERLATTICE_H_

#include "Namer.h"
#include "NamerFactory.h"

#include <memory>
#include <vector>

namespace fastbotx {
namespace naming {

    class NamerLattice {
    public:
        explicit NamerLattice(const NamerFactory &factory = NamerFactory::CURRENT);

        /** Namers obtained by adding exactly one NamerType w.r.t. {@code coarse}; sorted by bitmask for stable picks. */
        std::vector<NamerPtr> immediateRefinements(const NamerPtr &coarse) const;

        /** Namers obtained by removing exactly one NamerType w.r.t. {@code fine}; sorted by bitmask for stable picks. */
        std::vector<NamerPtr> immediateAbstractions(const NamerPtr &fine) const;

    private:
        const NamerFactory *factory_{nullptr};
    };

} // namespace naming
} // namespace fastbotx

#endif
