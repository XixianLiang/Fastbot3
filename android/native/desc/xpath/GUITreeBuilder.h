/*
 * One-shot parse: UTF-8 UI hierarchy XML → GUITree + optional pugixml bridge for Namelet XPath.
 */
#ifndef FASTBOTX_DESC_XPATH_GUITREEBUILDER_H_
#define FASTBOTX_DESC_XPATH_GUITREEBUILDER_H_

#include "GUITreeDomBridge.h"
#include "../gui_tree/GUITree.h"
#include "../Element.h"

#include <memory>
#include <string>

namespace fastbotx {
namespace gui_tree {

    struct GUITreeBuildResult {
        /** Declared before `tree` so at teardown `tree` is destroyed first (names/nodes), then the pugi
         *  document in `dom`. Reversing order used to run ~GUITreeDomBridge before ~GUITree, which can
         *  interact badly with Scudo under heavy XPath + Name teardown on the same build result. */
        std::shared_ptr<GUITreeDomBridge> dom;
        GUITreePtr tree;
    };

    class GUITreeBuilder {
    public:
        /** Same attribute conventions as Element::fromXMLNode (short/long names). */
        static GUITreeBuildResult buildFromXml(const std::string &utf8, const std::string &activity_package,
                                               const std::string &activity_class);

        /**
         * Build GUITree by walking the existing Element tree; load pugixml once from element->toXML()
         * only to map pugi::xml_node → GUITreeNode for XPath (avoids a second tree build from XML).
         * Falls back to buildFromXml if pre-order node pairing fails (structure drift).
         */
        static GUITreeBuildResult buildFromElement(const ElementPtr &root, const std::string &activity_package,
                                                   const std::string &activity_class);
    };

} // namespace gui_tree
} // namespace fastbotx

#endif
