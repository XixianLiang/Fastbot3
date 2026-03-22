/*
 * Keeps the pugixml document used to build a GUITree and maps XPath results to GUITreeNode.
 */
#ifndef FASTBOTX_DESC_XPATH_GUITREEDOMBRIDGE_H_
#define FASTBOTX_DESC_XPATH_GUITREEDOMBRIDGE_H_

#include "../gui_tree/GUITreeNode.h"

#include <memory>
#include <string>
#include <vector>

namespace fastbotx {
namespace gui_tree {

    class GUITreeBuilder;

    class GUITreeDomBridge {
    public:
        GUITreeDomBridge();
        ~GUITreeDomBridge();

        GUITreeDomBridge(GUITreeDomBridge &&) noexcept;
        GUITreeDomBridge &operator=(GUITreeDomBridge &&) noexcept;

        GUITreeDomBridge(const GUITreeDomBridge &) = delete;
        GUITreeDomBridge &operator=(const GUITreeDomBridge &) = delete;

        /** XPath 1.0 node-set mapped through the same DOM as the tree (invalid expr → empty). */
        std::vector<GUITreeNodePtr> nodesForXPath(const std::string &expr) const;

    private:
        friend class GUITreeBuilder;

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace gui_tree
} // namespace fastbotx

#endif
