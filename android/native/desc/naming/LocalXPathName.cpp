/**
 * @authors Zhao Zhang
 */
/**
 * Implementations of lightweight `Name` types: a predicate tail under `//*`, or a stored full XPath string.
 */

#include "LocalXPathName.h"

namespace fastbotx {
namespace naming {

    /**
     * Stores `predicates` (e.g. `[@class="…"]…`) and caches `//*` + predicates via `localXPathToXPathWithPredicateTail`.
     */
    LocalXPathName::LocalXPathName(NamerPtr namer, std::string predicates)
        : namer_(std::move(namer)),
          predicates_(std::move(predicates)) {
        xpath_full_ = localXPathToXPathWithPredicateTail(predicates_);
    }

    /** Returns the cached full XPath (`//*` or `//*` + predicate tail). */
    const std::string &LocalXPathName::toXPath() const { return xpath_full_; }

    /** Appends only the predicate segment (not the leading `//*`). */
    void LocalXPathName::appendXPathLocalProperties(std::string &sb) const { sb.append(predicates_); }

    /** Stores a complete path string; `toXPath` returns it unchanged (see header). */
    FullPathName::FullPathName(NamerPtr namer, std::string xpath)
        : namer_(std::move(namer)),
          xpath_(std::move(xpath)) {}

} // namespace naming
} // namespace fastbotx
