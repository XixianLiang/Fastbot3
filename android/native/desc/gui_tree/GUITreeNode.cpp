#include "GUITreeNode.h"
#include "../naming/Namelet.h"

namespace fastbotx {
namespace gui_tree {

    GUITreeNode::~GUITreeNode() = default;

    void GUITreeNode::setCurrentNamelet(std::shared_ptr<naming::Namelet> nl) {
        current_namelet_ = std::move(nl);
    }

    std::shared_ptr<naming::Namelet> GUITreeNode::getCurrentNamelet() const {
        return current_namelet_;
    }

    GUITreeNodePtr GUITreeNode::create(const GUITreeNodeWeakPtr &parent) {
        return GUITreeNodePtr(new GUITreeNode(parent));
    }

    GUITreeNode::GUITreeNode(GUITreeNodeWeakPtr parent)
        : parent_(std::move(parent)) {
        if (auto p = parent_.lock()) {
            depth_ = p->depth_ + 1;
        }
    }

    void GUITreeNode::appendChild(GUITreeNodePtr child) {
        if (!child) return;
        child->parent_ = GUITreeNodeWeakPtr(shared_from_this());
        children_.push_back(std::move(child));
    }

    bool GUITreeNode::isWebView() const {
        return class_name_ == "android.webkit.WebView";
    }

} // namespace gui_tree
} // namespace fastbotx
