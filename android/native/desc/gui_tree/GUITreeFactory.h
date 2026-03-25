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
 * One-shot parse: UTF-8 UI hierarchy XML → GUITree + optional pugixml bridge for Namelet XPath.
 */
#ifndef FASTBOTX_DESC_GUI_TREE_GUITREEFACTORY_H_
#define FASTBOTX_DESC_GUI_TREE_GUITREEFACTORY_H_

#include "XPathNodeMapper.h"
#include "GUITree.h"
#include "../Element.h"

#include <memory>
#include <string>

namespace fastbotx {
namespace gui_tree {

    struct GUITreeBuildResult {
        /** Declared before `tree` so at teardown `tree` is destroyed first (names/nodes), then the pugi
         *  document in `dom`. Reversing order used to run ~GUITreeDomBridge before ~GUITree, which can
         *  interact badly with Scudo under heavy XPath + Name teardown on the same build result. */
        std::shared_ptr<XPathNodeMapper> dom;
        GUITreePtr tree;
    };

    class GUITreeFactory {
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
