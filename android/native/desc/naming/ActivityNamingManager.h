/*
 * Per-activity current Naming root (Java ActivityNamingManager subset).
 * Keys should be StateKey::canonicalActivityString(jniActivity) so they match StateKey::fromGUITree / treeToNaming.
 */
#ifndef FASTBOTX_DESC_NAMING_ACTIVITYNAMINGMANAGER_H_
#define FASTBOTX_DESC_NAMING_ACTIVITYNAMINGMANAGER_H_

#include "Naming.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace fastbotx {
namespace naming {

    class ActivityNamingManager {
    public:
        /** @param activity_key Canonical activity (see StateKey::canonicalActivityString). */
        NamingPtr getNaming(const std::string &activity_key) const;

        void setNaming(const std::string &activity_key, NamingPtr n);

        bool hasNaming(const std::string &activity_key) const;

        void clear();

    private:
        std::unordered_map<std::string, NamingPtr> current_{};
    };

} // namespace naming
} // namespace fastbotx

#endif
