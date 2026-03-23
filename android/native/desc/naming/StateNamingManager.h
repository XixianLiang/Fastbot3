/*
 * Copyright 2020 Advanced Software Technologies Lab at ETH Zurich, Switzerland
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @authors Tianxiao Gu, Zhao Zhang
 */

/*
 * Coordinates Naming lattice navigation per activity.
 * treeToNaming / refine fixed-point — full logic in NamingFactory.
 */
#ifndef FASTBOTX_DESC_NAMING_STATENAMINGMANAGER_H_
#define FASTBOTX_DESC_NAMING_STATENAMINGMANAGER_H_

#include "ActivityNamingManager.h"
#include "Naming.h"

#include <memory>
#include <string>

namespace fastbotx {
namespace gui_tree {
    class GUITree;
    class XPathNodeMapper;
}
namespace naming {

    enum class NamingUpdateKind : unsigned char { Refine, Abstract };

    class StateNamingManager {
    public:
        explicit StateNamingManager(std::shared_ptr<ActivityNamingManager> activity_mgr);

        ActivityNamingManager &activityManager();
        const ActivityNamingManager &activityManager() const;

        NamingPtr getNamingForActivity(const std::string &activity_key) const;

        /** Later: Refine picks finer child; Abstract coarsens — currently stores n regardless. */
        void updateNaming(const std::string &activity_key, NamingUpdateKind kind, NamingPtr n);

        /** True if to is a direct refinement child of from (edge in from's refinement map). */
        bool namingToEdge(const NamingPtr &from, const NamingPtr &to, NamingEdge *out_edge) const;

        /** Current naming for the tree's activity, or {@link NamingFactory::defaultRootNaming} if unset. */
        NamingPtr treeToNaming(const gui_tree::GUITree &tree);

        /**
         * Without dom: returns stored naming (or default root) — no rebuild.
         */
        NamingPtr getNamingFixedPoint(const std::string &activity_key, const gui_tree::GUITree &tree, int max_iter);

        /**
         * Bitmask refinement with rebuild after each step: refineNaming → rebuildTree → compare StateKey::hash();
         * stops when refineNaming has no step, or StateKey unchanged (fixed point), or max_iter steps.
         * Persists result in ActivityNamingManager.
         */
        NamingPtr getNamingFixedPoint(const std::string &activity_key, gui_tree::GUITree &tree,
                                      const std::shared_ptr<gui_tree::XPathNodeMapper> &dom, int max_iter);

    private:
        std::shared_ptr<ActivityNamingManager> activity_mgr_;
    };

} // namespace naming
} // namespace fastbotx

#endif
