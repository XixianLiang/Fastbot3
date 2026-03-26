/**
 * @authors Zhao Zhang
 */
 
#include "LocalXPathName.h"

namespace fastbotx {
namespace naming {

    LocalXPathName::LocalXPathName(NamerPtr namer, std::string predicates)
        : namer_(std::move(namer)),
          predicates_(std::move(predicates)) {
        xpath_full_ = localXPathToXPathWithPredicateTail(predicates_);
    }

    std::string LocalXPathName::toXPath() const { return xpath_full_; }

    void LocalXPathName::appendXPathLocalProperties(std::string &sb) const { sb.append(predicates_); }

    FullPathName::FullPathName(NamerPtr namer, std::string xpath)
        : namer_(std::move(namer)),
          xpath_(std::move(xpath)) {}

} // namespace naming
} // namespace fastbotx
