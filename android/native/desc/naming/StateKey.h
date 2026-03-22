/*
 * APE state identity: activity + Naming fingerprint + sorted widget Name XPaths (Java StateKey analogue).
 * Hash mixes the same way as fastbotx::State (activity term + xor-combined name hashes).
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
        /** Use the same activity string as State / ReuseState (e.g. Java component flatten). */
        static StateKey fromParts(std::string activity, NamingPtr naming, std::vector<NamePtr> names);

        static StateKey fromGUITree(const gui_tree::GUITree &tree);

        /** Split at first `/` (same convention as GUITreeBuilder / JNI activity). */
        static void splitActivityPackageClass(const std::string &activity, std::string *pkg, std::string *cls);

        /** Activity string for `fromGUITree` (pkg + cls on tree). */
        static std::string activityFromPackageAndClass(const std::string &pkg, const std::string &cls);

        /**
         * Canonical activity for `StateKey` and `ActivityNamingManager` keys: recomposes pkg/cls like `fromGUITree`.
         * Use this when indexing naming state so it matches `StateKey::activity()` from a `GUITree` built with
         * `splitActivityPackageClass` + `GUITreeBuilder`.
         */
        static std::string canonicalActivityString(const std::string &activity);

        /**
         * Deterministic identity when full GUITree/Naming cannot produce a key (same hash mixing as normal StateKey).
         * Used so dynamic non-static runs never fall back to widget-mask state hashes (no APE vs legacy mixing).
         */
        static StateKey fromFallbackXmlStringHash(const std::string &activity, uintptr_t xmlStringHash);

        const std::string &activity() const { return activity_; }

        /** Stable serialization of namelets (expr + namer types), order-independent. */
        const std::string &namingFingerprint() const { return naming_fingerprint_; }

        /** Sorted Name::toXPath() tokens (multiset of abstract widgets). */
        const std::vector<std::string> &sortedXPaths() const { return sorted_xpaths_; }

        /** Same structure as State::create: activity term ^ naming ^ combined name hashes. */
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
