/*
 * Keeps the pugixml document used to build a GUITree and maps XPath results to GUITreeNode.
 */
#ifndef FASTBOTX_DESC_XPATH_XPATHNODEMAPPER_H_
#define FASTBOTX_DESC_XPATH_XPATHNODEMAPPER_H_

#include "../gui_tree/GUITreeNode.h"

#include <memory>
#include <string>
#include <vector>

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
#include "../../thirdpart/pugixml/pugixml.hpp"
#endif

namespace fastbotx {
namespace gui_tree {

    class GUITreeFactory;

    class XPathNodeMapper {
    public:
        XPathNodeMapper();
        ~XPathNodeMapper();

        XPathNodeMapper(XPathNodeMapper &&) noexcept;
        XPathNodeMapper &operator=(XPathNodeMapper &&) noexcept;

        XPathNodeMapper(const XPathNodeMapper &) = delete;
        XPathNodeMapper &operator=(const XPathNodeMapper &) = delete;

        /** XPath 1.0 node-set mapped through the same DOM as the tree (invalid expr → empty). */
        std::vector<GUITreeNodePtr> nodesForXPath(const std::string &expr) const;

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
        /**
         * DOM setup helpers used by GUITreeFactory.
         * They keep pugixml details inside this module after splitting GUITreeFactory.cpp.
         */
        bool loadXmlString(const std::string &utf8);
        pugi::xml_node documentElement() const;
        void registerNode(pugi::xml_node xmlNode, const GUITreeNodePtr &gn);
#endif

    private:
        friend class GUITreeFactory;

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace gui_tree
} // namespace fastbotx

#endif
