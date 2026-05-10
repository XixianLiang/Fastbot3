/*
 * Copyright 2020 Advanced Software Technologies Lab at ETH Zurich, Switzerland
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @authors Tianxiao Gu, Zhao Zhang
 */
#ifndef FASTBOTX_DESC_NAMING_ACTIVITYNAMINGMANAGER_H_
#define FASTBOTX_DESC_NAMING_ACTIVITYNAMINGMANAGER_H_

#include "Naming.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fastbotx {
namespace naming {

    /**
     * Holds one `Naming` root per screen / activity key so XPath naming policy can differ by context.
     * Keys should match the canonical activity string used elsewhere in state (e.g. `StateKey::canonicalActivityString`).
     */
    class ActivityNamingManager {
    public:
        /**
         * Returns the `Naming` for `activity_key`. On first access for that key, inserts the global base naming
         * from `NamingFactory::getBaseNaming()` and returns it.
         */
        NamingPtr getNaming(const std::string &activity_key) const;

        /** Stores `n` under `activity_key`, replacing any existing pointer (may be null). */
        void setNaming(const std::string &activity_key, NamingPtr n);

        /** True if `activity_key` has an entry in the map (including lazy inserts from `getNaming`). */
        bool hasNaming(const std::string &activity_key) const;

        /** Drops every cached per-activity naming root. */
        void clear();

        /** Non-owning snapshot of all stored non-null naming roots (e.g. for releasing caches or visiting graphs). */
        std::vector<NamingPtr> getAllNamings() const;

    private:
        /** Mutable because `getNaming` lazily populates the map under `const` receivers. */
        mutable std::unordered_map<std::string, NamingPtr> current_{};
    };

} // namespace naming
} // namespace fastbotx

#endif
