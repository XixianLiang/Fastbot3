/**
 * @authors Zhao Zhang
 */
 /*
 * Activity label + naming fingerprint + sorted widget XPath strings for abstract UI-state identity.
 * Hash combines activity, naming chain id, and ordered XPath string hashes.
 */
#ifndef FASTBOTX_DESC_NAMING_STATEKEY_H_
#define FASTBOTX_DESC_NAMING_STATEKEY_H_

#include "Name.h"
#include "Naming.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fastbotx {
namespace gui_tree {
    class GUITree;
}
namespace naming {

    class StateKey {
    public:
        /** Builds from caller activity string plus naming graph and concrete `Name` list (`toXPath()` tokens). */
        static StateKey fromParts(std::string activity, NamingPtr naming, std::vector<NamePtr> names);

        static StateKey fromGUITree(const gui_tree::GUITree &tree);

        /** Hash-only fast path for GUITree (avoids building sorted_xpaths vector). */
        static uintptr_t hashFromGUITree(const gui_tree::GUITree &tree);

        /** Split at first `/` into package and class components (same convention as UI snapshot activity strings). */
        static void splitActivityPackageClass(const std::string &activity, std::string *pkg, std::string *cls);

        /**
         * Canonical activity for `StateKey` and `ActivityNamingManager` keys: recomposes pkg/cls like `fromGUITree`.
         * Falls back to `pkg` only when `cls` is empty so callers that get a single-field
         * activity string keep working.
         */
        static std::string activityFromPackageAndClass(const std::string &pkg, const std::string &cls);
        static std::string canonicalActivityString(const std::string &activity);

        const std::string &activity() const { return activity_; }

        /** Stable v3 serialization of namelets (order, base/refine, expr, hex mask); used with StateKey hash. */
        const std::string &namingFingerprint() const { return naming_fingerprint_; }

        /** Sorted Name::toXPath() tokens (multiset of abstract widgets). */
        const std::vector<std::string> &sortedXPaths() const { return sorted_xpaths_; }

        /** Precomputed mixed hash of activity, naming fingerprint, and XPath multiset (see `StateKey.cpp`). */
        uintptr_t hash() const { return hashcode_; }

        bool operator==(const StateKey &o) const;
        bool operator!=(const StateKey &o) const { return !(*this == o); }

    private:
        StateKey(std::string activity, std::string naming_fp, std::vector<std::string> sorted_xpaths);

        std::string activity_;
        std::string naming_fingerprint_;
        std::vector<std::string> sorted_xpaths_;
        uintptr_t hashcode_{0};
    };

} // namespace naming
} // namespace fastbotx

#endif
