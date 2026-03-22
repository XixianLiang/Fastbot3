#include "Namer.h"

#include <algorithm>

namespace fastbotx {
namespace naming {

    int compareNamer(const Namer &a, const Namer &b) {
        std::vector<NamerType> ta = a.getNamerTypes();
        std::vector<NamerType> tb = b.getNamerTypes();
        std::sort(ta.begin(), ta.end(), [](NamerType x, NamerType y) {
            return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
        });
        std::sort(tb.begin(), tb.end(), [](NamerType x, NamerType y) {
            return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
        });
        if (ta.size() != tb.size()) {
            return ta.size() < tb.size() ? -1 : 1;
        }
        for (size_t i = 0; i < ta.size(); ++i) {
            if (ta[i] != tb[i]) {
                return static_cast<unsigned char>(ta[i]) < static_cast<unsigned char>(tb[i]) ? -1 : 1;
            }
        }
        return 0;
    }

} // namespace naming
} // namespace fastbotx
