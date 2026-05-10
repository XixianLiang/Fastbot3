/**
 * @authors Zhao Zhang
 */
/**
 * Canonical UI-state fingerprint: canonical activity string, naming fingerprint, and lexicographically sorted
 * widget XPath strings. Hash mixes activity, naming chain id, and ordered XPath hashes for RL/state dedup.
 */

#include "StateKey.h"
#include "../gui_tree/GUITree.h"

#include "../utils.hpp"

#include <algorithm>
#include <utility>

namespace fastbotx {
namespace naming {
namespace {
    /** When true, each XPath string’s hash is perturbed by its index after sorting (order-aware multiset). */
    constexpr bool kStateKeyHashXPathOrder = true;

    /** Fold ordered string hashes into one uintptr using a hash_combine-style mix. */
    uintptr_t combineStringHashes(const std::vector<std::string> &sorted, bool withOrder) {
        // Avoid XOR-only multiset hashing (collision-prone). Use a cheap mix similar to boost::hash_combine.
        uintptr_t combined = 0x9e3779b97f4a7c15ULL;
        for (size_t i = 0; i < sorted.size(); ++i) {
            uintptr_t h = fastStringHash(sorted[i]);
            if (withOrder) {
                h ^= (127U * (static_cast<unsigned>(i) << 6));
            }
            combined ^= h + 0x9e3779b97f4a7c15ULL + (combined << 6) + (combined >> 2);
        }
        return combined;
    }

    /** Full state hash from canonical activity, naming fingerprint, and sorted non-empty XPath strings. */
    uintptr_t computeStateKeyHash(const std::string &activity, const std::string &naming_fp,
                                  const std::vector<std::string> &sorted_xpaths) {
        uintptr_t activityHash = (fastStringHash(activity) * 31U) << 5;
        if (!naming_fp.empty()) {
            activityHash ^= (fastStringHash(naming_fp) << 2);
        }
        activityHash ^= (combineStringHashes(sorted_xpaths, kStateKeyHashXPathOrder) << 1);
        return activityHash;
    }

    /** Same mixing as `computeStateKeyHash` but skips empty XPath entries while keeping sequential index tags. */
    uintptr_t computeStateKeyHashFromXPaths(const std::string &activity, const std::string &naming_fp,
                                            const std::vector<std::string> &sorted_xpaths) {
        // Same index semantics as `combineStringHashes(..., withOrder=true)` for non-skipped elements only.
        uintptr_t activityHash = (fastStringHash(activity) * 31U) << 5;
        if (!naming_fp.empty()) {
            activityHash ^= (fastStringHash(naming_fp) << 2);
        }
        uintptr_t combined = 0x9e3779b97f4a7c15ULL;
        size_t j = 0;
        for (const auto &s : sorted_xpaths) {
            if (s.empty()) {
                continue;
            }
            uintptr_t h = fastStringHash(s);
            if (kStateKeyHashXPathOrder) {
                h ^= (127U * (static_cast<unsigned>(j) << 6));
            }
            combined ^= h + 0x9e3779b97f4a7c15ULL + (combined << 6) + (combined >> 2);
            ++j;
        }
        activityHash ^= (combined << 1);
        return activityHash;
    }

} // namespace

    /** Stores fields and precomputes `hashcode_` from activity + naming fingerprint + sorted XPath list. */
    StateKey::StateKey(std::string activity, std::string naming_fp, std::vector<std::string> sorted_xpaths)
        : activity_(std::move(activity)),
          naming_fingerprint_(std::move(naming_fp)),
          sorted_xpaths_(std::move(sorted_xpaths)),
          hashcode_(computeStateKeyHash(activity_, naming_fingerprint_, sorted_xpaths_)) {}

    /** Builds a key from explicit naming + name objects; collects non-empty `toXPath()` strings and sorts them. */
    StateKey StateKey::fromParts(std::string activity, NamingPtr naming, std::vector<NamePtr> names) {
        std::string nf = naming ? naming->fingerprintString() : std::string();
        std::vector<std::string> xs;
        xs.reserve(names.size());
        for (const auto &np : names) {
            if (!np) {
                continue;
            }
            const std::string &xp = np->toXPath();
            if (xp.empty()) {
                continue;
            }
            xs.push_back(xp);
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

    /** Splits `pkg/cls` style activity; if no slash, treats entire string as class and clears package. */
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

    /** Prefers fully qualified class when present; otherwise uses package-only activity label. */
    std::string StateKey::activityFromPackageAndClass(const std::string &pkg, const std::string &cls) {
        if (!cls.empty()) {
            return cls;
        }
        return pkg;
    }

    /** Normalizes a slash-separated or single-field activity string via split + `activityFromPackageAndClass`. */
    std::string StateKey::canonicalActivityString(const std::string &activity) {
        std::string pkg;
        std::string cls;
        splitActivityPackageClass(activity, &pkg, &cls);
        return activityFromPackageAndClass(pkg, cls);
    }

    /** Snapshot key from tree metadata and cached sorted XPaths (already ordered after `GUITree::rebuild`). */
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

    /** Lightweight hash for observers that skips constructing a filtered XPath vector on hot paths. */
    uintptr_t StateKey::hashFromGUITree(const gui_tree::GUITree &tree) {
        std::string act = activityFromPackageAndClass(tree.getActivityPackageName(), tree.getActivityClassName());
        const std::string &nf =
            tree.getCurrentNaming() ? tree.getCurrentNaming()->fingerprintString() : std::string();
        const auto &cached = tree.getCurrentXPaths();
        return computeStateKeyHashFromXPaths(act, nf, cached);
    }

    /** Value equality on all three components (including XPath multiset equality). */
    bool StateKey::operator==(const StateKey &o) const {
        return activity_ == o.activity_ && naming_fingerprint_ == o.naming_fingerprint_
               && sorted_xpaths_ == o.sorted_xpaths_;
    }

} // namespace naming
} // namespace fastbotx
