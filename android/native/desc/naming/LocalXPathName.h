/**
 * @authors Zhao Zhang
 */
/**
 * Concrete `Name` types for XPath strings: a predicate tail under the any-node step `//*`, or a prebuilt
 * full path (e.g. chained `/*` segments for ancestor- or parent-relative naming). Each instance records
 * the `Namer` that produced it.
 */
#ifndef FASTBOTX_DESC_NAMING_LOCALXPATHNAME_H_
#define FASTBOTX_DESC_NAMING_LOCALXPATHNAME_H_

#include "Name.h"
#include "Namer.h"

#include <string>

namespace fastbotx {
namespace naming {

    /**
     * Predicate-only fragment for a single node match: concatenated attribute tests such as
     * `[@class="…"][@resource-id="…"]`. The leading `//*` step is supplied when materializing the full XPath.
     */
    class LocalXPathName : public Name {
    public:
        /** Builds cached full XPath as `//*` or `//*` + `predicates` via `localXPathToXPathWithPredicateTail`. */
        LocalXPathName(NamerPtr namer, std::string predicates);

        std::shared_ptr<Namer> getNamer() const override { return namer_; }

        const std::string &toXPath() const override;
        std::string cacheKeyString() const override { return std::string("LocalXPathName{") + xpath_full_ + "}"; }

        /** Appends the predicate tail (no leading `//*`) for layering under composite namers. */
        void appendXPathLocalProperties(std::string &sb) const override;

    private:
        NamerPtr namer_{};
        /** Raw predicate chain without the `//*` prefix. */
        std::string predicates_;
        /** Cached `//*` + `predicates_` for repeated `toXPath()` queries. */
        std::string xpath_full_;
    };

    /**
     * Helper: empty `predicates` yields `//*`; otherwise `//*` + `predicates`. Matches `LocalXPathName::toXPath()`.
     */
    inline std::string localXPathToXPathWithPredicateTail(const std::string &predicates) {
        if (predicates.empty()) {
            return "//*";
        }
        return "//*" + predicates;
    }

    /**
     * Stores an entire XPath string already expanded by the namer (multi-segment paths for hierarchy modes).
     * `toXPath()` returns that string unchanged—there is no implicit `//*` prefix unlike `LocalXPathName`.
     */
    class FullPathName : public Name {
    public:
        FullPathName(NamerPtr namer, std::string xpath);

        std::shared_ptr<Namer> getNamer() const override { return namer_; }

        const std::string &toXPath() const override { return xpath_; }
        std::string cacheKeyString() const override { return std::string("FullPathName{") + xpath_ + "}"; }

        /** Full path is fixed; nothing further to append for local composition. */
        void appendXPathLocalProperties(std::string &sb) const override { (void)sb; }

    private:
        NamerPtr namer_{};
        std::string xpath_;
    };

} // namespace naming
} // namespace fastbotx

#endif
