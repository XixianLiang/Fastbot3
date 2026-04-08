/*
 * C++ analogue of Java APE StringCache: intern repeated text / content-desc to cut allocations.
 * removeQuotes + truncateText happen before cache (see ApeTextNormalize + GUITreeFactory).
 */
/**
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

class ApeStringCache {
public:
    static const std::string &empty();

    /** Java APE Config.maxStringListSize default is 2000. */
    static void setMaxStringListSize(size_t v);

    static const std::string &cacheStringEmptyOnNull(const char *s, bool addToList);
    static const std::string &cacheStringEmptyOnNull(const std::string &s, bool addToList);
    static const std::string &cacheStringEmptyOnNull(std::string &&s, bool addToList);

    static const std::string &cacheString(const std::string &s, bool addToList);
    static const std::string &cacheString(std::string &&s, bool addToList);

    static const std::vector<const std::string *> &getStringList();

private:
    static std::mutex &mutex();
    static std::unordered_set<std::string> &dict();
    static std::vector<const std::string *> &stringList();
    static size_t &maxStringListSize();
};

} // namespace fastbotx

#endif
