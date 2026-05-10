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
 * Namelet: binds an XPath expression fragment to a `Namer`, forming one step in a refinement hierarchy.
 * Child edges carry nested `Naming` instances; the parent weak pointer breaks cycles while keeping depth.
 */
#ifndef FASTBOTX_DESC_NAMING_NAMELET_H_
#define FASTBOTX_DESC_NAMING_NAMELET_H_

#include "Namer.h"

#include <map>
#include <memory>
#include <string>

namespace fastbotx {
namespace naming {

    class Naming;
    class Namelet;

    /**
     * Strict weak ordering for `shared_ptr<Namelet>` keys: compares pointed-to values when both non-null,
     * otherwise falls back to pointer address so `std::map` iteration order is stable for fingerprints.
     */
    struct PtrLess {
        bool operator()(const std::shared_ptr<Namelet> &a, const std::shared_ptr<Namelet> &b) const;
    };

    /** Maps each direct child namelet to the `Naming` subtree used under that refinement. */
    using ChildNamingMap = std::map<std::shared_ptr<Namelet>, std::shared_ptr<Naming>, PtrLess>;

    /**
     * One refinement node: expression text, namer policy, optional parent in the namelet tree, and child
     * naming graphs. Construction requires a non-null `namer`; `setParent` wires depth once.
     */
    class Namelet : public std::enable_shared_from_this<Namelet> {
    public:
        /** BASE vs REFINE classification for layered naming strategies. */
        enum class Type : unsigned char { BASE = 0, REFINE = 1 };

        /** Full constructor: `expr_str` is the XPath-related fragment; `namer` must not be null. */
        Namelet(Type type, std::string expr_str, NamerPtr namer);

        /** REFINE namelet: expression string and namer (delegates to the three-argument constructor). */
        explicit Namelet(std::string expr_str, NamerPtr namer);

        ~Namelet();

        /** Sort key: expression string, then type, then `compareNamer` on the two namers (see `Namelet.cpp`). */
        bool operator<(const Namelet &other) const;

        /** Value equality on type, expression, and namer ordering (same criterion as `operator<`). */
        bool operator==(const Namelet &other) const;

        Type getType() const { return type_; }

        /** Raw XPath expression substring stored on this namelet. */
        const std::string &getExprString() const { return expr_str_; }

        Namer &getNamer() const { return *namer_; }

        /** Shared ownership handle for the namer object. */
        NamerPtr getNamerPtr() const { return namer_; }

        /** Depth in the namelet tree; updated when `setParent` links this node under a parent. */
        int getDepth() const { return depth_; }

        bool isBase() const { return type_ == Type::BASE; }

        bool isRefine() const { return type_ == Type::REFINE; }

        /** Parent namelet, or null if unset or the parent object was destroyed. */
        std::shared_ptr<Namelet> getParent() const { return parent_.lock(); }

        /** Read-only view of child namelet → nested `Naming` registrations. */
        const ChildNamingMap &getChildNamings() const { return child_namings_; }

        /** Records `childNaming` for `child`; ignores null `child` or `childNaming`. */
        void addChildNaming(const std::shared_ptr<Namelet> &child, const std::shared_ptr<Naming> &childNaming);

        /** Lookup of the nested naming graph for `child`; null if missing. */
        std::shared_ptr<Naming> getChildNaming(const std::shared_ptr<Namelet> &child) const;

        /** Sets an immutable parent link and depth = parent.depth + 1; throws if parent null or already set. */
        void setParent(const std::shared_ptr<Namelet> &parent);

    private:
        Type type_{Type::REFINE};
        std::string expr_str_;
        NamerPtr namer_{nullptr};

        int depth_{0};
        std::weak_ptr<Namelet> parent_{};

        ChildNamingMap child_namings_{};
    };

    /** Shared ownership alias used across naming code paths. */
    using NameletPtr = std::shared_ptr<Namelet>;

} // namespace naming
} // namespace fastbotx

#endif
