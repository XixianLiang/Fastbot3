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

/*
 * APE-compatible abstract widget name
 * Concrete implementations (TypeNamer, XPath composition, etc.) are added incrementally.
 */
#ifndef FASTBOTX_DESC_NAMING_NAME_H_
#define FASTBOTX_DESC_NAMING_NAME_H_

#include <memory>
#include <string>

namespace fastbotx {
namespace naming {

    class Namer;

    class Name {
    public:
        virtual ~Name();

        virtual std::shared_ptr<Namer> getNamer() const = 0;

        /** XPath fragment for this name (APE: toXPath). */
        virtual const std::string &toXPath() const = 0;

        /** Local predicate fragment after `// *` (any node) for AbstractLocalName-style names. */
        virtual void appendXPathLocalProperties(std::string &sb) const { (void)sb; }

        /** Lexicographic order on XPath string (APE: Comparable<Name>). */
        bool operator<(const Name &other) const;
        bool operator==(const Name &other) const;
    };

    using NamePtr = std::shared_ptr<Name>;

} // namespace naming
} // namespace fastbotx

#endif
