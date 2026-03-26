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
 * Pre-registered namers for every subset of namerTypesUsed() (APE NamerFactory).
 * When usePatchNamer() is true (APE default), each namer is ActionPatchNamer(BitmaskNamer(mask)).
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
         * Live factory: rebuilt when {@link useAncestorNamer()} or {@link usePatchNamer()} changes
         * (e.g. after max.config sets max.useAncestorNamer / max.usePatchNamer).
         */
        static const NamerFactory &current();

        /** Builds the full bitmask cube for {@link namerTypesUsed()} at construction time. */
        NamerFactory();

        NamerFactory(const NamerFactory &) = delete;
        NamerFactory &operator=(const NamerFactory &) = delete;

        /** Bitmask uses {@code 1u << static_cast<unsigned>(NamerType)}. */
        NamerPtr getByMask(uint32_t mask) const;

        const std::vector<NamerPtr> &all() const { return ordered_; }

        NamerPtr empty() const { return empty_; }

    private:
        std::unordered_map<uint32_t, NamerPtr> by_mask_;
        std::vector<NamerPtr> ordered_;
        NamerPtr empty_{};
    };

} // namespace naming
} // namespace fastbotx

#endif
