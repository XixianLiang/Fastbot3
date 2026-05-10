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

#include "ActivityNamingManager.h"
#include "NamingFactory.h"

namespace fastbotx {
namespace naming {

    /**
     * Returns the stored `Naming` for `activity_key`, or lazily inserts `NamingFactory::getBaseNaming()`
     * on first access and returns that instance.
     */
    NamingPtr ActivityNamingManager::getNaming(const std::string &activity_key) const {
        auto it = current_.find(activity_key);
        if (it == current_.end()) {
            NamingPtr base = NamingFactory::getBaseNaming();
            current_[activity_key] = base;
            return base;
        }
        return it->second;
    }

    /** Associates `activity_key` with `n`, replacing any previous entry. */
    void ActivityNamingManager::setNaming(const std::string &activity_key, NamingPtr n) {
        current_[activity_key] = std::move(n);
    }

    /** True if `activity_key` exists in the map (including entries created lazily by `getNaming`). */
    bool ActivityNamingManager::hasNaming(const std::string &activity_key) const {
        return current_.find(activity_key) != current_.end();
    }

    /** Removes all per-activity naming roots. */
    void ActivityNamingManager::clear() { current_.clear(); }

    /** Collects every non-null `NamingPtr` currently stored (for cache teardown or traversal). */
    std::vector<NamingPtr> ActivityNamingManager::getAllNamings() const {
        std::vector<NamingPtr> out;
        out.reserve(current_.size());
        for (const auto &kv : current_) {
            if (kv.second) {
                out.push_back(kv.second);
            }
        }
        return out;
    }

} // namespace naming
} // namespace fastbotx
