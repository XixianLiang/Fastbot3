/**
 * @authors Zhao Zhang
 */
 
#include "LocalXPathName.h"

namespace fastbotx {
namespace naming {

    LocalXPathName::LocalXPathName(NamerPtr namer, std::string predicates)
        : namer_(std::move(namer)),
          predicates_(std::move(predicates)) {}

    std::string LocalXPathName::toXPath() const { return localXPathToXPathWithPredicateTail(predicates_); }

    void LocalXPathName::appendXPathLocalProperties(std::string &sb) const { sb.append(predicates_); }

} // namespace naming
} // namespace fastbotx
