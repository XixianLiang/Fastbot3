/*
 * Copyright 2020 Advanced Software Technologies Lab at ETH Zurich, Switzerland
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */
/**
 * @authors Tianxiao Gu, Zhao Zhang
 */

/*
 * Pre-built `BitmaskNamer` instances for every subset of dimensions listed by `namerTypesUsed()`.
 * When `usePatchNamer()` is true, each entry wraps `ActionPatchNamer` around that bitmask namer.
 */
#ifndef FASTBOTX_DESC_NAMING_NAMERFACTORY_H_
#define FASTBOTX_DESC_NAMING_NAMERFACTORY_H_

#include "Namer.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace fastbotx {
namespace naming {

    class NamerFactory {
    public:
        /**
         * Singleton-like accessor: the cached factory is rebuilt when `useAncestorNamer()` or `usePatchNamer()`
         * toggles so stacking matches current preferences.
         */
        static const NamerFactory &current();

        /** Materializes all bitmask combinations over `namerTypesUsed()` at startup (see `.cpp`). */
        NamerFactory();

        NamerFactory(const NamerFactory &) = delete;
        NamerFactory &operator=(const NamerFactory &) = delete;

        /** Returns the namer whose `typeDimensionMask()` equals `mask` (bits `1u << NamerType`). */
        NamerPtr getByMask(uint32_t mask) const;

        /** Namers sorted by increasing population count of `mask`, then by numeric mask. */
        const std::vector<NamerPtr> &all() const { return ordered_; }

        /** The namer for bitmask zero when generated (typically minimal/local naming only). */
        NamerPtr empty() const { return empty_; }

    private:
        std::unordered_map<uint32_t, NamerPtr> by_mask_;
        std::vector<NamerPtr> ordered_;
        NamerPtr empty_{};
    };

} // namespace naming
} // namespace fastbotx

#endif
