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

} // namespace

    StateKey::StateKey(std::string activity, std::string naming_fp, std::vector<std::string> sorted_xpaths)
        : activity_(std::move(activity)),
          naming_fingerprint_(std::move(naming_fp)),
          sorted_xpaths_(std::move(sorted_xpaths)),
          hashcode_(computeStateKeyHash(activity_, naming_fingerprint_, sorted_xpaths_)) {}

    StateKey StateKey::fromParts(std::string activity, NamingPtr naming, std::vector<NamePtr> names) {
        std::string nf = naming ? naming->fingerprintString() : "";
        std::vector<std::string> xs;
        xs.reserve(names.size());
        for (const auto &np : names) {
            if (!np) {
                continue;
            }
            xs.push_back(np->toXPath());
        }
        std::sort(xs.begin(), xs.end());
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
        return fromParts(std::move(act), tree.getCurrentNaming(), tree.getCurrentNames());
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
