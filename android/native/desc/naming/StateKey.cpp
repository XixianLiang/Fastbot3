/**
 * @authors Zhao Zhang
 */

#include "StateKey.h"
#include "../gui_tree/GUITree.h"

#include "../utils.hpp"

#include <algorithm>
#include <utility>

namespace fastbotx {
namespace naming {
namespace {

    uintptr_t combineStringHashes(const std::vector<std::string> &sorted, bool withOrder) {
        uintptr_t combinedHashcode = 0x1;
        for (size_t i = 0; i < sorted.size(); ++i) {
            combinedHashcode ^= fastStringHash(sorted[i]);
            if (withOrder) {
                combinedHashcode ^= (127U * (static_cast<unsigned>(i) << 6));
            }
        }
        return combinedHashcode;
    }

    uintptr_t computeStateKeyHash(const std::string &activity, const std::string &naming_fp,
                                  const std::vector<std::string> &sorted_xpaths) {
        uintptr_t activityHash = (fastStringHash(activity) * 31U) << 5;
        if (!naming_fp.empty()) {
            activityHash ^= (fastStringHash(naming_fp) << 2);
        }
        activityHash ^= (combineStringHashes(sorted_xpaths, STATE_WITH_WIDGET_ORDER) << 1);
        return activityHash;
    }

    uintptr_t computeStateKeyHashFromXPaths(const std::string &activity, const std::string &naming_fp,
                                            const std::vector<std::string> &sorted_xpaths) {
        // Like computeStateKeyHash() but allows skipping empty entries while preserving
        // the order-index semantics used by combineStringHashes(..., withOrder=true).
        uintptr_t activityHash = (fastStringHash(activity) * 31U) << 5;
        if (!naming_fp.empty()) {
            activityHash ^= (fastStringHash(naming_fp) << 2);
        }
        uintptr_t combinedHashcode = 0x1;
        size_t j = 0;
        for (const auto &s : sorted_xpaths) {
            if (s.empty()) {
                continue;
            }
            combinedHashcode ^= fastStringHash(s);
            if (STATE_WITH_WIDGET_ORDER) {
                combinedHashcode ^= (127U * (static_cast<unsigned>(j) << 6));
            }
            ++j;
        }
        activityHash ^= (combinedHashcode << 1);
        return activityHash;
    }

} // namespace

    StateKey::StateKey(std::string activity, std::string naming_fp, std::vector<std::string> sorted_xpaths)
        : activity_(std::move(activity)),
          naming_fingerprint_(std::move(naming_fp)),
          sorted_xpaths_(std::move(sorted_xpaths)),
          hashcode_(computeStateKeyHash(activity_, naming_fingerprint_, sorted_xpaths_)) {}

    StateKey StateKey::fromParts(std::string activity, NamingPtr naming, std::vector<NamePtr> names) {
        std::string nf = naming ? naming->fingerprintString() : std::string();
        std::vector<std::string> xs;
        xs.reserve(names.size());
        for (const auto &np : names) {
            if (!np) {
                continue;
            }
            xs.push_back(np->toXPath());
        }
        // Fast path: GUITree::rebuild() keeps current names sorted lexicographically by toXPath().
        // If input is already sorted, skip the O(n log n) sort.
        bool already_sorted = true;
        for (size_t i = 1; i < xs.size(); ++i) {
            if (xs[i - 1] > xs[i]) {
                already_sorted = false;
                break;
            }
        }
        if (!already_sorted && xs.size() > 1) {
            std::sort(xs.begin(), xs.end());
        }
        return StateKey(std::move(activity), std::move(nf), std::move(xs));
    }

    void StateKey::splitActivityPackageClass(const std::string &activity, std::string *pkg, std::string *cls) {
        if (!pkg || !cls) {
            return;
        }
        const auto p = activity.find('/');
        if (p == std::string::npos) {
            pkg->clear();
            *cls = activity;
            return;
        }
        *pkg = activity.substr(0, p);
        *cls = activity.substr(p + 1);
    }

    std::string StateKey::activityFromPackageAndClass(const std::string &pkg, const std::string &cls) {
        if (!pkg.empty() && !cls.empty()) {
            return pkg + "/" + cls;
        }
        if (!cls.empty()) {
            return cls;
        }
        return pkg;
    }

    std::string StateKey::canonicalActivityString(const std::string &activity) {
        std::string pkg;
        std::string cls;
        splitActivityPackageClass(activity, &pkg, &cls);
        return activityFromPackageAndClass(pkg, cls);
    }

    StateKey StateKey::fromGUITree(const gui_tree::GUITree &tree) {
        std::string act = activityFromPackageAndClass(tree.getActivityPackageName(), tree.getActivityClassName());
        std::string nf = tree.getCurrentNaming() ? tree.getCurrentNaming()->fingerprintString() : std::string();
        const auto &cached = tree.getCurrentXPaths();
        std::vector<std::string> xs;
        xs.reserve(cached.size());
        for (const auto &s : cached) {
            if (s.empty()) {
                continue;
            }
            xs.push_back(s);
        }
        // Cached xpaths are already sorted lexicographically by GUITree::rebuild().
        return StateKey(std::move(act), std::move(nf), std::move(xs));
    }

    uintptr_t StateKey::hashFromGUITree(const gui_tree::GUITree &tree) {
        std::string act = activityFromPackageAndClass(tree.getActivityPackageName(), tree.getActivityClassName());
        const std::string &nf =
            tree.getCurrentNaming() ? tree.getCurrentNaming()->fingerprintString() : std::string();
        const auto &cached = tree.getCurrentXPaths();
        return computeStateKeyHashFromXPaths(act, nf, cached);
    }

    StateKey StateKey::fromFallbackXmlStringHash(const std::string &activity, uintptr_t xmlStringHash) {
        std::string nf = "fallback";
        std::vector<std::string> xs;
        xs.push_back(std::string("x/") + std::to_string(static_cast<unsigned long long>(xmlStringHash)));
        std::sort(xs.begin(), xs.end());
        return StateKey(canonicalActivityString(activity), std::move(nf), std::move(xs));
    }

    bool StateKey::operator==(const StateKey &o) const {
        return activity_ == o.activity_ && naming_fingerprint_ == o.naming_fingerprint_
               && sorted_xpaths_ == o.sorted_xpaths_;
    }

} // namespace naming
} // namespace fastbotx
