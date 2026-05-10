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
 * `Namelet` ties an XPath expression string to a `Namer` and supports a small refinement tree (parent/child
 * namings). This file defines ordering, construction, and parent/child wiring.
 */

#include "Namelet.h"
#include "Naming.h"

#include <stdexcept>

namespace fastbotx {
namespace naming {

    /** Orders non-null namelets by value (`*a < *b`); null pointers sort by raw address for deterministic ties. */
    bool PtrLess::operator()(const std::shared_ptr<Namelet> &a, const std::shared_ptr<Namelet> &b) const {
        if (!a || !b) {
            return a.get() < b.get();
        }
        return *a < *b;
    }

    /** Validates `namer` is non-null; does not set parent depth until `setParent` is called for tree edges. */
    Namelet::Namelet(Type type, std::string expr_str, NamerPtr namer)
        : type_(type),
          expr_str_(std::move(expr_str)),
          namer_(std::move(namer)) {
        if (!namer_) {
            throw std::invalid_argument("Namelet: namer is null");
        }
    }

    /** Convenience: builds a REFINE namelet with the given XPath fragment and namer. */
    Namelet::Namelet(std::string expr_str, NamerPtr namer)
        : Namelet(Type::REFINE, std::move(expr_str), std::move(namer)) {}

    /** Default destructor; child maps hold `shared_ptr` edges. */
    Namelet::~Namelet() = default;

    /** Lexicographic tuple order: expression string, then type, then `compareNamer` on the two namers. */
    bool Namelet::operator<(const Namelet &other) const {
        int c = expr_str_.compare(other.expr_str_);
        if (c != 0) return c < 0;
        if (type_ != other.type_) return static_cast<unsigned char>(type_) < static_cast<unsigned char>(other.type_);
        return compareNamer(*namer_, *other.namer_) < 0;
    }

    /** Value equality on type, expression string, and namer ordering relation (`compareNamer == 0`). */
    bool Namelet::operator==(const Namelet &other) const {
        return type_ == other.type_ && expr_str_ == other.expr_str_ && compareNamer(*namer_, *other.namer_) == 0;
    }

    /** Associates a child namelet with its nested `Naming` graph; ignores null arguments. */
    void Namelet::addChildNaming(const std::shared_ptr<Namelet> &child, const std::shared_ptr<Naming> &childNaming) {
        if (!child || !childNaming) return;
        child_namings_[child] = childNaming;
    }

    /** Returns the stored `Naming` for `child`, or nullptr if not registered. */
    std::shared_ptr<Naming> Namelet::getChildNaming(const std::shared_ptr<Namelet> &child) const {
        auto it = child_namings_.find(child);
        if (it == child_namings_.end()) return nullptr;
        return it->second;
    }

    /**
     * Links this node under `parent` and sets `depth_` to parent depth + 1. Callable at most once;
     * throws if `parent` is null or a parent was already set.
     */
    void Namelet::setParent(const std::shared_ptr<Namelet> &parent) {
        if (!parent) {
            throw std::invalid_argument("Namelet::setParent: parent is null");
        }
        if (parent_.lock()) {
            throw std::logic_error("Namelet::setParent: already has parent");
        }
        parent_ = parent;
        depth_ = parent->getDepth() + 1;
    }

} // namespace naming
} // namespace fastbotx
