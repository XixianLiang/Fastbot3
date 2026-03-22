#include "ActivityNamingManager.h"

namespace fastbotx {
namespace naming {

    NamingPtr ActivityNamingManager::getNaming(const std::string &activity_key) const {
        auto it = current_.find(activity_key);
        if (it == current_.end()) {
            return nullptr;
        }
        return it->second;
    }

    void ActivityNamingManager::setNaming(const std::string &activity_key, NamingPtr n) {
        current_[activity_key] = std::move(n);
    }

    bool ActivityNamingManager::hasNaming(const std::string &activity_key) const {
        return current_.find(activity_key) != current_.end();
    }

    void ActivityNamingManager::clear() { current_.clear(); }

} // namespace naming
} // namespace fastbotx
