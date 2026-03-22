/*
 * Pre-registered BitmaskNamers for every subset of namerTypesUsed() (APE NamerFactory).
 */
#ifndef FASTBOTX_DESC_NAMING_NAMERFACTORY_H_
#define FASTBOTX_DESC_NAMING_NAMERFACTORY_H_

#include "BitmaskNamer.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fastbotx {
namespace naming {

    class NamerFactory {
    public:
        /** Singleton: all non-empty subsets of {@link namerTypesUsed()} plus mask 0 (empty namer). */
        static const NamerFactory CURRENT;

        /** Builds the full bitmask cube for {@link namerTypesUsed()}; prefer {@link CURRENT}. */
        NamerFactory();

        NamerFactory(const NamerFactory &) = delete;
        NamerFactory &operator=(const NamerFactory &) = delete;

        /** Bitmask uses {@code 1u << static_cast<unsigned>(NamerType)}. */
        std::shared_ptr<BitmaskNamer> getByMask(uint32_t mask) const;

        const std::vector<std::shared_ptr<BitmaskNamer>> &all() const { return ordered_; }

        std::shared_ptr<BitmaskNamer> empty() const { return empty_; }

    private:

        std::unordered_map<uint32_t, std::shared_ptr<BitmaskNamer>> by_mask_;
        std::vector<std::shared_ptr<BitmaskNamer>> ordered_;
        std::shared_ptr<BitmaskNamer> empty_{};
    };

} // namespace naming
} // namespace fastbotx

#endif
