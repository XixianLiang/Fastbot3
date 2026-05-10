/**
 * @file ApeStringCache.h
 *
 * Process-wide interning of immutable UI strings: deduplicates identical `std::string` copies into stable
 * canonical instances and can record pointers in encounter order (bounded) for vocabulary-style export.
 * All `cache*` entry points are protected by a single mutex.
 *
 * @authors Zhao Zhang
 */

#ifndef FASTBOTX_DESC_APE_STRING_CACHE_H_
#define FASTBOTX_DESC_APE_STRING_CACHE_H_

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace fastbotx {

/**
 * String intern table used by the UI description layer (e.g. `GUITreeNode` text fields).
 * Empty and null inputs map to a shared empty string; non-empty content is stored once in an
 * `unordered_set` and returned by const reference.
 */
class ApeStringCache {
public:
    /** Shared empty string instance; same object for all "no text" cases. */
    static const std::string &empty();

    /**
     * Caps how many interned strings are retained in the ordered `getStringList()` view.
     * If the list already exceeds `v`, it is truncated to length `v` (newer entries may be dropped from the tail).
     */
    static void setMaxStringListSize(size_t v);

    /**
     * Like `cacheString`, but null pointers and empty strings resolve to `empty()` without inserting.
     * @param addToList When true and under the list cap, appends a pointer to the canonical instance.
     */
    static const std::string &cacheStringEmptyOnNull(const char *s, bool addToList);
    static const std::string &cacheStringEmptyOnNull(const std::string &s, bool addToList);
    static const std::string &cacheStringEmptyOnNull(std::string &&s, bool addToList);

    /**
     * Returns the canonical `std::string` stored in the set for equal content.
     * @param addToList When true, records `&ref` in the side list (until `maxStringListSize()` is reached).
     */
    static const std::string &cacheString(const std::string &s, bool addToList);
    static const std::string &cacheString(std::string &&s, bool addToList);

    /** Ordered view of pointers into interned strings (only entries cached with `addToList == true`). Not mutex-guarded. */
    static const std::vector<const std::string *> &getStringList();

private:
    static std::mutex &mutex();
    static std::unordered_set<std::string> &dict();
    static std::vector<const std::string *> &stringList();
    static size_t &maxStringListSize();
};

} // namespace fastbotx

#endif
