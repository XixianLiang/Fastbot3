#include "GUITree.h"

#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace fastbotx {
namespace gui_tree {

    namespace {

        bool namePtrLess(const naming::NamePtr &a, const naming::NamePtr &b) {
            if (!a || !b) return a.get() < b.get();
            return a->toXPath() < b->toXPath();
        }

    } // namespace

    static std::atomic<int> g_tree_id{0};

    int GUITree::nextId() {
        return g_tree_id.fetch_add(1, std::memory_order_relaxed);
    }

    GUITree::GUITree(GUITreeNodePtr root, std::string activity_package, std::string activity_class)
        : id_(nextId()),
          root_(std::move(root)),
          activity_package_(std::move(activity_package)),
          activity_class_(std::move(activity_class)) {}

    bool GUITree::hasFocusedNode() const {
        for (const auto &group : current_node_groups_) {
            for (const auto &n : group) {
                if (n && n->isFocused()) return true;
            }
        }
        return false;
    }

    void GUITree::rebuild(std::vector<naming::NamePtr> names, std::vector<std::vector<GUITreeNodePtr>> node_groups) {
        if (names.size() != node_groups.size()) {
            throw std::invalid_argument("GUITree::rebuild: names and node_groups size mismatch");
        }
        std::vector<std::pair<naming::NamePtr, std::vector<GUITreeNodePtr>>> pairs;
        pairs.reserve(names.size());
        for (size_t i = 0; i < names.size(); ++i) {
            pairs.emplace_back(std::move(names[i]), std::move(node_groups[i]));
        }
        std::sort(pairs.begin(), pairs.end(),
                  [](const std::pair<naming::NamePtr, std::vector<GUITreeNodePtr>> &a,
                     const std::pair<naming::NamePtr, std::vector<GUITreeNodePtr>> &b) {
                      return namePtrLess(a.first, b.first);
                  });
        current_names_.clear();
        current_node_groups_.clear();
        current_names_.reserve(pairs.size());
        current_node_groups_.reserve(pairs.size());
        for (auto &p : pairs) {
            current_names_.push_back(std::move(p.first));
            current_node_groups_.push_back(std::move(p.second));
        }
    }

    void GUITree::setCurrentNaming(naming::NamingPtr naming,
                                   std::vector<naming::NamePtr> names,
                                   std::vector<std::vector<GUITreeNodePtr>> node_groups) {
        current_naming_ = std::move(naming);
        rebuild(std::move(names), std::move(node_groups));
    }

    void GUITree::validate() const {
        const size_t n = current_names_.size();
        if (current_node_groups_.size() != n) {
            throw std::runtime_error("GUITree::validate: names / node_groups size mismatch");
        }
        for (size_t i = 0; i < n; ++i) {
            const naming::NamePtr &w = current_names_[i];
            const auto &group = current_node_groups_[i];
            for (const auto &node : group) {
                if (!node || !w) continue;
                naming::NamePtr xn = node->getXPathName();
                if (!xn || !(*xn == *w)) {
                    throw std::runtime_error("GUITree::validate: mismatched node and name");
                }
            }
        }
    }

} // namespace gui_tree
} // namespace fastbotx
