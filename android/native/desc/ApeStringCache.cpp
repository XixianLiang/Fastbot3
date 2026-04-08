/*
 * @authors Zhao Zhang
 */
#include "ApeStringCache.h"

#include <utility>

namespace fastbotx {

const std::string &ApeStringCache::empty() {
    static const std::string kEmpty;
    return kEmpty;
}

void ApeStringCache::setMaxStringListSize(size_t v) {
    std::lock_guard<std::mutex> lk(mutex());
    maxStringListSize() = v;
    if (stringList().size() > maxStringListSize()) {
        stringList().resize(maxStringListSize());
    }
}

const std::string &ApeStringCache::cacheStringEmptyOnNull(const char *s, bool addToList) {
    if (!s || *s == '\0') {
        return empty();
    }
    return cacheStringEmptyOnNull(std::string(s), addToList);
}

const std::string &ApeStringCache::cacheStringEmptyOnNull(const std::string &s, bool addToList) {
    if (s.empty()) {
        return empty();
    }
    return cacheString(s, addToList);
}

const std::string &ApeStringCache::cacheStringEmptyOnNull(std::string &&s, bool addToList) {
    if (s.empty()) {
        return empty();
    }
    return cacheString(std::move(s), addToList);
}

const std::vector<const std::string *> &ApeStringCache::getStringList() {
    return stringList();
}

const std::string &ApeStringCache::cacheString(const std::string &s, bool addToList) {
    std::lock_guard<std::mutex> lk(mutex());
    auto it = dict().find(s);
    if (it == dict().end()) {
        auto res = dict().insert(s);
        it = res.first;
    }
    const std::string &ref = *it;
    if (addToList && stringList().size() < maxStringListSize()) {
        stringList().push_back(&ref);
    }
    return ref;
}

const std::string &ApeStringCache::cacheString(std::string &&s, bool addToList) {
    std::lock_guard<std::mutex> lk(mutex());
    auto res = dict().insert(std::move(s));
    const std::string &ref = *res.first;
    if (addToList && stringList().size() < maxStringListSize()) {
        stringList().push_back(&ref);
    }
    return ref;
}

std::mutex &ApeStringCache::mutex() {
    static std::mutex m;
    return m;
}

std::unordered_set<std::string> &ApeStringCache::dict() {
    static std::unordered_set<std::string> d;
    return d;
}

std::vector<const std::string *> &ApeStringCache::stringList() {
    static std::vector<const std::string *> list;
    return list;
}

size_t &ApeStringCache::maxStringListSize() {
    static size_t maxSize = 2000; // Java APE default Config.maxStringListSize
    return maxSize;
}

} // namespace fastbotx
