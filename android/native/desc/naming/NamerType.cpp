#include "NamerType.h"

namespace fastbotx {
namespace naming {

#ifndef FASTBOTX_USE_ANCESTOR_NAMER
#define FASTBOTX_USE_ANCESTOR_NAMER 0
#endif

    const std::vector<NamerType> &namerTypesUsed() {
#if FASTBOTX_USE_ANCESTOR_NAMER
        static const std::vector<NamerType> kUsed = {
            NamerType::TYPE, NamerType::INDEX, NamerType::PARENT,
            NamerType::TEXT, NamerType::ANCESTOR};
#else
        static const std::vector<NamerType> kUsed = {
            NamerType::TYPE, NamerType::INDEX, NamerType::PARENT, NamerType::TEXT};
#endif
        return kUsed;
    }

} // namespace naming
} // namespace fastbotx
