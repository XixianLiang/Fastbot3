/**
 * @authors Zhao Zhang
 */
 
/*
 * Concrete Name: XPath fragment + owning Namer (APE local widget name).
 */
#ifndef FASTBOTX_DESC_NAMING_LOCALXPATHNAME_H_
#define FASTBOTX_DESC_NAMING_LOCALXPATHNAME_H_

#include "Name.h"
#include "Namer.h"

#include <string>

namespace fastbotx {
namespace naming {

    /** Predicate segment(s) after any-node step, e.g. [@class='a'][@index='0']. Empty => bare wildcard. */
    class LocalXPathName : public Name {
    public:
        LocalXPathName(NamerPtr namer, std::string predicates);

        std::shared_ptr<Namer> getNamer() const override { return namer_; }

        std::string toXPath() const override;

        void appendXPathLocalProperties(std::string &sb) const override;

    private:
        NamerPtr namer_{};
        std::string predicates_;
    };

    /** Same rule as LocalXPathName::toXPath() for a predicate tail (empty => bare any-node step). */
    inline std::string localXPathToXPathWithPredicateTail(const std::string &predicates) {
        if (predicates.empty()) {
            return "//*";
        }
        return "//*" + predicates;
    }

    /**
     * APE AncestorName / ParentName-style full XPath (not always descendant-or-self wildcard + tail).
     * toXPath() returns the stored path verbatim (Java Name::toXPath).
     */
    class FullPathName : public Name {
    public:
        FullPathName(NamerPtr namer, std::string xpath);

        std::shared_ptr<Namer> getNamer() const override { return namer_; }

        std::string toXPath() const override { return xpath_; }

        void appendXPathLocalProperties(std::string &sb) const override { (void)sb; }

    private:
        NamerPtr namer_{};
        std::string xpath_;
    };

} // namespace naming
} // namespace fastbotx

#endif
