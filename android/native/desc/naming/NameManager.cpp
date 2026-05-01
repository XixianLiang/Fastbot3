#include "NameManager.h"
#include "Namer.h"

#include <mutex>
#include <unordered_map>

namespace fastbotx {
namespace naming {
namespace {

struct NameCacheBucket {
    std::unordered_map<std::string, NamePtr> byKey;
};

std::mutex gNameCacheMu;
std::unordered_map<std::string, NameCacheBucket> gNameCache;
int gNameOrderCounter = 0;

} // namespace

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
