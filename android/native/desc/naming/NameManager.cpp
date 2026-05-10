/**
 * Global interning of `Name` instances: deduplicates equal logical names per namer and assigns a monotonic
 * `order_` index the first time a distinct key is inserted (see `Name::operator<`).
 */

#include "NameManager.h"
#include "Namer.h"

#include <mutex>
#include <unordered_map>

namespace fastbotx {
namespace naming {
namespace {

/** Per-namer slice of the cache: maps `Name::cacheKeyString()` to the canonical shared `Name`. */
struct NameCacheBucket {
    std::unordered_map<std::string, NamePtr> byKey;
};

std::mutex gNameCacheMu;
/** Top-level key is `namerSemanticKey(namer)` so unrelated namers never collide. */
std::unordered_map<std::string, NameCacheBucket> gNameCache;
/** Next value assigned to `Name::setOrder` for a newly interned distinct key. */
int gNameOrderCounter = 0;

} // namespace

/**
 * Returns an existing interned `Name` when `namerKey` + `cacheKeyString()` match; otherwise registers `name`,
 * assigns a new order index, and returns it. Null names or names without a namer pass through unchanged.
 */
NamePtr cacheName(const NamePtr &name) {
    if (!name || !name->getNamer()) {
        return name;
    }
    const std::string namerKey = namerSemanticKey(*name->getNamer());
    const std::string cacheKey = name->cacheKeyString();
    std::lock_guard<std::mutex> lk(gNameCacheMu);
    NameCacheBucket &bucket = gNameCache[namerKey];
    auto it = bucket.byKey.find(cacheKey);
    if (it != bucket.byKey.end()) {
        return it->second;
    }
    name->setOrder(gNameOrderCounter++);
    bucket.byKey.emplace(cacheKey, name);
    return name;
}

} // namespace naming
} // namespace fastbotx
