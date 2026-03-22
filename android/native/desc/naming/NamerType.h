/*
 * APE NamerType (Java: com.android.commands.monkey.ape.naming.NamerType).
 */
#ifndef FASTBOTX_DESC_NAMING_NAMERTYPE_H_
#define FASTBOTX_DESC_NAMING_NAMERTYPE_H_

#include <vector>

namespace fastbotx {
namespace naming {

    enum class NamerType : unsigned char {
        TYPE = 0,
        INDEX,
        PARENT,
        TEXT,
        ANCESTOR
    };

    /** Ordered list used in lattice (Config.useAncestorNamer in Java). */
    const std::vector<NamerType> &namerTypesUsed();

} // namespace naming
} // namespace fastbotx

#endif
