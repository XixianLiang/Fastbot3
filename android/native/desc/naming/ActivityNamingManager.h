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

    class ActivityNamingManager {
    public:
        /** @param activity_key Canonical activity (see StateKey::canonicalActivityString). */
        NamingPtr getNaming(const std::string &activity_key) const;

        void setNaming(const std::string &activity_key, NamingPtr n);

        bool hasNaming(const std::string &activity_key) const;

        void clear();

        /** Snapshot current per-activity naming roots (for cache-release traversal). */
        std::vector<NamingPtr> getAllNamings() const;

    private:
        mutable std::unordered_map<std::string, NamingPtr> current_{};
    };

} // namespace naming
} // namespace fastbotx

#endif
