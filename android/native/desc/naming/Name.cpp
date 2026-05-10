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
/**
 * Out-of-line definitions for `Name` comparison operators (ordering uses the `order_` field on `Name`).
 */

#include "Name.h"

#include <stdexcept>

namespace fastbotx {
namespace naming {

    /** Virtual destructor; default implementation is sufficient for the base class. */
    Name::~Name() = default;

    /**
     * Strict weak ordering for use in `std::set` / `std::map` of `Name*`.
     * Compares `getOrder()`; if equal for two different objects, throws (invalid total order / mis-set orders).
     */
    bool Name::operator<(const Name &other) const {
        const int ret = getOrder() - other.getOrder();
        if (ret == 0 && this != &other) {
            throw std::logic_error("Name order collision on distinct instances");
        }
        return ret < 0;
    }

    /** Identity equality: only the same object compares equal (not value equality on XPath text). */
    bool Name::operator==(const Name &other) const {
        return this == &other;
    }

} // namespace naming
} // namespace fastbotx
