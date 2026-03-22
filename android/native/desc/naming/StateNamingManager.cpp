#include "StateNamingManager.h"
#include "NamingFactory.h"
#include "StateKey.h"
#include "NamerFactory.h"
#include "NamerLattice.h"
#include "../gui_tree/GUITree.h"
#include "../xpath/GUITreeDomBridge.h"

namespace fastbotx {
namespace naming {
namespace {

    std::string activityKeyFromTree(const gui_tree::GUITree &tree) {
        return StateKey::activityFromPackageAndClass(tree.getActivityPackageName(), tree.getActivityClassName());
    }

} // namespace

    StateNamingManager::StateNamingManager(std::shared_ptr<ActivityNamingManager> activity_mgr)
        : activity_mgr_(std::move(activity_mgr)) {
        if (!activity_mgr_) {
            activity_mgr_ = std::make_shared<ActivityNamingManager>();
        }
    }

    ActivityNamingManager &StateNamingManager::activityManager() { return *activity_mgr_; }

    const ActivityNamingManager &StateNamingManager::activityManager() const { return *activity_mgr_; }

    NamingPtr StateNamingManager::getNamingForActivity(const std::string &activity_key) const {
        return activity_mgr_->getNaming(activity_key);
    }

    void StateNamingManager::updateNaming(const std::string &activity_key, NamingUpdateKind kind, NamingPtr n) {
        (void)kind;
        activity_mgr_->setNaming(activity_key, std::move(n));
    }

    bool StateNamingManager::namingToEdge(const NamingPtr &from, const NamingPtr &to, NamingEdge *out_edge) const {
        if (!from || !to || !out_edge) {
            return false;
        }
        for (const auto &p : from->getRefinementChildren()) {
            if (p.second == to) {
                *out_edge = p.first;
                return true;
            }
        }
        return false;
    }

    NamingPtr StateNamingManager::treeToNaming(const gui_tree::GUITree &tree) {
        NamingPtr n = activity_mgr_->getNaming(activityKeyFromTree(tree));
        if (n) {
            return n;
        }
        return NamingFactory::defaultRootNaming();
    }

    NamingPtr StateNamingManager::getNamingFixedPoint(const std::string &activity_key,
                                                      const gui_tree::GUITree & /*tree*/, int max_iter) {
        NamingPtr n = activity_mgr_->getNaming(activity_key);
        if (!n && max_iter > 0) {
            return NamingFactory::defaultRootNaming();
        }
        return n;
    }

    NamingPtr StateNamingManager::getNamingFixedPoint(const std::string &activity_key, gui_tree::GUITree &tree,
                                                      const std::shared_ptr<gui_tree::GUITreeDomBridge> &dom,
                                                      int max_iter) {
        if (!dom) {
            return getNamingFixedPoint(activity_key, tree, max_iter);
        }
        NamingPtr n = activity_mgr_->getNaming(activity_key);
        if (!n) {
            n = NamingFactory::defaultRootNaming();
            if (!n) {
                return nullptr;
            }
        }
        const int steps = max_iter > 0 ? max_iter : 64;
        NamerLattice lat(NamerFactory::CURRENT);
        n = NamingFactory::batchRefineWithRebuildFixedPoint(n, lat, tree, dom, steps);
        if (!n) {
            return nullptr;
        }
        activity_mgr_->setNaming(activity_key, n);
        return n;
    }

} // namespace naming
} // namespace fastbotx
