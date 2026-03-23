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

#include "GUITreeBuilder.h"
#include "XPathNodeMapper.h"
#include "../Element.h"

#include "../../Base.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
#include "../../thirdpart/pugixml/pugixml.hpp"
#endif

namespace fastbotx {
namespace gui_tree {
namespace {

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML

    int parseIntAndAdvance(const char *&p) {
        bool neg = (*p == '-');
        if (neg) ++p;
        int v = 0;
        while (*p >= '0' && *p <= '9')
            v = v * 10 + (*p++ - '0');
        return neg ? -v : v;
    }

    bool readString(pugi::xml_node el, const char *shortName, const char *longName, const char *&out) {
        pugi::xml_attribute a = el.attribute(shortName);
        if (!a) a = el.attribute(longName);
        if (!a) return false;
        out = a.value();
        return out && *out;
    }

    bool readInt(pugi::xml_node el, const char *shortName, const char *longName, int &out) {
        pugi::xml_attribute a = el.attribute(shortName);
        if (!a) a = el.attribute(longName);
        if (!a) return false;
        out = a.as_int();
        return true;
    }

    bool readBool(pugi::xml_node el, const char *shortName, const char *longName, bool &out) {
        pugi::xml_attribute a = el.attribute(shortName);
        if (!a) a = el.attribute(longName);
        if (!a) return false;
        const char *v = a.value();
        if (std::strcmp(v, "true") == 0) {
            out = true;
            return true;
        }
        if (std::strcmp(v, "false") == 0) {
            out = false;
            return true;
        }
        return false;
    }

    void fillFromAttributes(pugi::xml_node xmlNode, GUITreeNodePtr gn) {
        int indexOfNode = 0;
        if (readInt(xmlNode, "idx", "index", indexOfNode)) {
            gn->setIndex(indexOfNode);
        }
        const char *boundingBoxStr = nullptr;
        if (readString(xmlNode, "bnd", "bounds", boundingBoxStr) && boundingBoxStr && *boundingBoxStr == '[') {
            const char *p = boundingBoxStr + 1;
            int xl = parseIntAndAdvance(p);
            if (*p == ',') {
                ++p;
                int yl = parseIntAndAdvance(p);
                if (p[0] == ']' && p[1] == '[') {
                    p += 2;
                    int xr = parseIntAndAdvance(p);
                    if (*p == ',') {
                        ++p;
                        int yr = parseIntAndAdvance(p);
                        if (*p == ']') {
                            Rect r(xl, yl, xr, yr);
                            if (!r.isEmpty())
                                gn->setBounds(r);
                        }
                    }
                }
            }
        }
        const char *text = nullptr;
        if (readString(xmlNode, "t", "text", text)) gn->setText(std::string(text));
        const char *resource_id = nullptr;
        if (readString(xmlNode, "rid", "resource-id", resource_id)) gn->setResourceId(std::string(resource_id));
        const char *tclassname = nullptr;
        if (readString(xmlNode, "class", "class", tclassname)) gn->setClassName(std::string(tclassname));
        const char *pkgname = nullptr;
        if (readString(xmlNode, "pkg", "package", pkgname)) gn->setPackageName(std::string(pkgname));
        const char *content_desc = nullptr;
        if (readString(xmlNode, "cd", "content-desc", content_desc)) gn->setContentDesc(std::string(content_desc));
        bool b = false;
        if (readBool(xmlNode, "ck", "checkable", b)) gn->setCheckable(b);
        if (readBool(xmlNode, "clk", "clickable", b)) gn->setClickable(b);
        if (readBool(xmlNode, "fcd", "focused", b)) gn->setFocused(b);
        if (readBool(xmlNode, "scl", "scrollable", b)) gn->setScrollable(b ? 1 : 0);
        if (readBool(xmlNode, "lclk", "long-clickable", b)) gn->setLongClickable(b);
        if (readBool(xmlNode, "pwd", "password", b)) gn->setPassword(b);
    }

    void postOrderStats(GUITreeNodePtr n) {
        int dc = 1;
        int h = 1;
        for (const auto &c : n->getChildren()) {
            postOrderStats(c);
            dc += c->getDescendantCount();
            h = std::max(h, 1 + c->getHeight());
        }
        n->setDescendantCount(dc);
        n->setHeight(h);
    }

    GUITreeNodePtr parseElement(pugi::xml_node xe, const GUITreeNodeWeakPtr &parentWeak,
                                XPathNodeMapper &dom) {
        GUITreeNodePtr gn = GUITreeNode::create(parentWeak);
        if (xe.type() == pugi::node_element) {
            dom.registerNode(xe, gn);
        }
        fillFromAttributes(xe, gn);
        for (pugi::xml_node ch = xe.first_child(); ch; ch = ch.next_sibling()) {
            if (ch.type() != pugi::node_element) continue;
            GUITreeNodePtr child = parseElement(ch, gn, dom);
            gn->appendChild(std::move(child));
        }
        return gn;
    }

    void fillFromElement(const ElementPtr &el, GUITreeNodePtr gn) {
        if (!el || !gn) {
            return;
        }
        gn->setIndex(el->getIndex());
        RectPtr b = el->getBounds();
        if (b && !b->isEmpty()) {
            gn->setBounds(*b);
        }
        gn->setText(el->getText());
        gn->setResourceId(el->getResourceID());
        gn->setClassName(el->getClassname());
        gn->setPackageName(el->getPackageName());
        gn->setContentDesc(el->getContentDesc());
        gn->setCheckable(el->getCheckable());
        gn->setClickable(el->getClickable());
        gn->setFocused(el->getFocused());
        gn->setScrollable(el->getScrollable() ? 1 : 0);
        gn->setLongClickable(el->getLongClickable());
        gn->setPassword(el->getPassword());
    }

    GUITreeNodePtr parseElementFromElement(const ElementPtr &xe, const GUITreeNodeWeakPtr &parentWeak,
                                           std::vector<GUITreeNodePtr> *order_out) {
        GUITreeNodePtr gn = GUITreeNode::create(parentWeak);
        fillFromElement(xe, gn);
        if (order_out != nullptr) {
            order_out->push_back(gn);
        }
        for (const auto &ch : xe->getChildren()) {
            if (!ch) {
                continue;
            }
            gn->appendChild(parseElementFromElement(ch, gn, order_out));
        }
        return gn;
    }

    void collectPugiElementNodesPreOrder(pugi::xml_node xe, std::vector<pugi::xml_node> *out) {
        if (!out || !xe) {
            return;
        }
        if (xe.type() == pugi::node_element) {
            out->push_back(xe);
        }
        for (pugi::xml_node ch = xe.first_child(); ch; ch = ch.next_sibling()) {
            if (ch.type() != pugi::node_element) {
                continue;
            }
            collectPugiElementNodesPreOrder(ch, out);
        }
    }

#endif

} // namespace

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML

    GUITreeBuildResult GUITreeBuilder::buildFromXml(const std::string &utf8, const std::string &activity_package,
                                                    const std::string &activity_class) {
        GUITreeBuildResult r;
        auto bridge = std::make_shared<XPathNodeMapper>();
        if (!bridge->loadXmlString(utf8)) {
            bridge.reset();
            return r;
        }
        pugi::xml_node root = bridge->documentElement();
        if (!root) {
            bridge.reset();
            return r;
        }
        GUITreeNodePtr rootGn = parseElement(root, GUITreeNodeWeakPtr(), *bridge);
        postOrderStats(rootGn);
        r.tree = std::make_shared<GUITree>(std::move(rootGn), activity_package, activity_class);
        r.dom = std::move(bridge);
        return r;
    }

    GUITreeBuildResult GUITreeBuilder::buildFromElement(const ElementPtr &root, const std::string &activity_package,
                                                        const std::string &activity_class) {
        GUITreeBuildResult r;
        if (!root) {
            return r;
        }
        const std::string xml = root->toXML();
        auto bridge = std::make_shared<XPathNodeMapper>();
        if (!bridge->loadXmlString(xml)) {
            bridge.reset();
            return r;
        }
        pugi::xml_node xmlRoot = bridge->documentElement();
        if (!xmlRoot) {
            bridge.reset();
            return r;
        }
        std::vector<pugi::xml_node> pugi_order;
        collectPugiElementNodesPreOrder(xmlRoot, &pugi_order);
        std::vector<GUITreeNodePtr> gui_order;
        GUITreeNodePtr rootGn = parseElementFromElement(root, GUITreeNodeWeakPtr(), &gui_order);
        if (pugi_order.size() != gui_order.size() || pugi_order.empty()) {
            bridge.reset();
            return r;
        }
        for (size_t i = 0; i < pugi_order.size(); ++i) {
            bridge->registerNode(pugi_order[i], gui_order[i]);
        }
        postOrderStats(rootGn);
        r.tree = std::make_shared<GUITree>(std::move(rootGn), activity_package, activity_class);
        r.dom = std::move(bridge);
        return r;
    }

#else

    GUITreeBuildResult GUITreeBuilder::buildFromXml(const std::string &, const std::string &, const std::string &) {
        return GUITreeBuildResult{};
    }

    GUITreeBuildResult GUITreeBuilder::buildFromElement(const ElementPtr &, const std::string &, const std::string &) {
        return GUITreeBuildResult{};
    }

#endif

} // namespace gui_tree
} // namespace fastbotx
