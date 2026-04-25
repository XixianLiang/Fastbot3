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

#include "GUITreeFactory.h"
#include "../ApeTextNormalize.h"
#include "../xpath/XPathNodeMapper.h"
#include "../Element.h"
#include "../../events/Preference.h"

#include "../../Base.h"
#include "../../utils.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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

    bool isEditTextClassName(const char *cls) {
        if (!cls || !*cls) {
            return false;
        }
        const size_t len = std::strlen(cls);
        return (len == 23 && std::strcmp(cls, "android.widget.EditText") == 0) ||
               (len == 42 && std::strcmp(cls, "android.inputmethodservice.ExtractEditText") == 0) ||
               (len == 35 && std::strcmp(cls, "android.widget.AutoCompleteTextView") == 0) ||
               (len == 42 && std::strcmp(cls, "android.widget.MultiAutoCompleteTextView") == 0);
    }

    int parseScrollableBitsFromAttr(const char *scrollableStr) {
        if (!scrollableStr || !*scrollableStr) {
            return 0;
        }
        if (std::strcmp(scrollableStr, "true") == 0) {
            return 3;
        }
        if (std::strcmp(scrollableStr, "false") == 0) {
            return 0;
        }
        const char *p = scrollableStr;
        if (*p == '-' || *p == '+') {
            ++p;
        }
        if (*p == '\0') {
            return 0;
        }
        for (const char *q = p; *q; ++q) {
            if (*q < '0' || *q > '9') {
                return 0;
            }
        }
        int v = std::atoi(scrollableStr);
        if (v < 0) {
            v = 0;
        }
        if (v > 3) {
            v = 3;
        }
        return v;
    }

    const char *computeScrollTypeString(int scrollableBits, const char *className) {
        if (scrollableBits == 0) {
            return "none";
        }
        if (!className || !*className) {
            return "all";
        }
        if (std::strcmp(className, "android.widget.ScrollView") == 0 ||
            std::strcmp(className, "android.widget.ListView") == 0 ||
            std::strcmp(className, "android.widget.ExpandableListView") == 0 ||
            std::strcmp(className, "android.support.v17.leanback.widget.VerticalGridView") == 0) {
            return "vertical";
        }
        if (std::strcmp(className, "android.widget.HorizontalScrollView") == 0 ||
            std::strcmp(className, "android.support.v17.leanback.widget.HorizontalGridView") == 0 ||
            std::strcmp(className, "android.support.v4.view.ViewPager") == 0) {
            return "horizontal";
        }
        if (scrollableBits == 1) {
            return "vertical";
        }
        if (scrollableBits == 2) {
            return "horizontal";
        }
        return "all";
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
                            gn->setBounds(r);
                        }
                    }
                }
            }
        }
        const char *tclassname = nullptr;
        if (readString(xmlNode, "class", "class", tclassname)) gn->setClassName(std::string(tclassname));
        const bool isEditText = tclassname != nullptr && isEditTextClassName(tclassname);
        std::string normText;
        const char *rawText = nullptr;
        if (readString(xmlNode, "t", "text", rawText)) {
            normText = ape_text::normalizeTextForApe(rawText);
            gn->setText(normText);
        }
        // Ensure DOM has canonical @text for XPath naming.
        {
            pugi::xml_attribute t = xmlNode.attribute("text");
            if (!t) {
                t = xmlNode.append_attribute("text");
            }
            t.set_value(isEditText ? "" : normText.c_str());
        }
        const char *resource_id = nullptr;
        if (readString(xmlNode, "rid", "resource-id", resource_id)) gn->setResourceId(std::string(resource_id));
        const char *pkgname = nullptr;
        if (readString(xmlNode, "pkg", "package", pkgname)) gn->setPackageName(std::string(pkgname));
        std::string normContentDesc;
        const char *rawContentDesc = nullptr;
        if (readString(xmlNode, "cd", "content-desc", rawContentDesc)) {
            normContentDesc = ape_text::normalizeContentDescForApe(rawContentDesc);
            gn->setContentDesc(normContentDesc);
        }
        // Ensure DOM has canonical @content-desc for XPath naming.
        {
            pugi::xml_attribute cd = xmlNode.attribute("content-desc");
            if (!cd) {
                cd = xmlNode.append_attribute("content-desc");
            }
            cd.set_value(normContentDesc.c_str());
        }
        bool b = false;
        if (readBool(xmlNode, "en", "enabled", b)) gn->setEnabled(b);
        if (readBool(xmlNode, "ck", "checkable", b)) gn->setCheckable(b);
        if (readBool(xmlNode, "cked", "checked", b)) gn->setChecked(b);
        if (readBool(xmlNode, "clk", "clickable", b)) gn->setClickable(b);
        if (readBool(xmlNode, "foc", "focusable", b)) gn->setFocusable(b);
        if (readBool(xmlNode, "fcd", "focused", b)) gn->setFocused(b);
        // Ensure DOM has canonical boolean attrs for XPath naming.
        {
            pugi::xml_attribute ckd = xmlNode.attribute("checked");
            if (!ckd) {
                ckd = xmlNode.append_attribute("checked");
            }
            ckd.set_value(gn->isChecked() ? "true" : "false");
        }
        {
            pugi::xml_attribute foc = xmlNode.attribute("focusable");
            if (!foc) {
                foc = xmlNode.append_attribute("focusable");
            }
            foc.set_value(gn->isFocusable() ? "true" : "false");
        }
        int scrollableBits = 0;
        const char *scrollableStr = nullptr;
        if (readString(xmlNode, "scl", "scrollable", scrollableStr)) {
            scrollableBits = parseScrollableBitsFromAttr(scrollableStr);
            gn->setScrollable(scrollableBits);
        }
        {
            pugi::xml_attribute scl = xmlNode.attribute("scrollable");
            if (!scl) {
                scl = xmlNode.append_attribute("scrollable");
            }
            scl.set_value(scrollableBits != 0 ? "true" : "false");
        }
        {
            const int bitsForScrollType = gn->getScrollable();
            const char *scrollTypeStr = computeScrollTypeString(bitsForScrollType, tclassname);
            pugi::xml_attribute st = xmlNode.attribute("scroll-type");
            if (!st) {
                st = xmlNode.append_attribute("scroll-type");
            }
            st.set_value(scrollTypeStr);
        }
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

    bool asciiLiteralEqualsIgnoreCase(const char *v, const char *lit) {
        if (!v || !lit) {
            return false;
        }
        while (*v && *lit) {
            char cv = *v++;
            char cl = *lit++;
            if (cv >= 'A' && cv <= 'Z') {
                cv = static_cast<char>(cv + 32);
            }
            if (cl >= 'A' && cl <= 'Z') {
                cl = static_cast<char>(cl + 32);
            }
            if (cv != cl) {
                return false;
            }
        }
        return *v == *lit;
    }

    /**
     * when excludeInvisibleNode (default true), skip non-visible children;
     * when excludeEmptyChild (default true), skip null-slot analogues (leaf + empty bounds attr).
     */
    bool xmlSubtreeExcludedByApePreference(const pugi::xml_node &xe, bool isOuterXmlRoot) {
        if (!xe || xe.type() != pugi::node_element || isOuterXmlRoot) {
            return false;
        }

        fastbotx::PreferencePtr pref = fastbotx::Preference::inst();
        const bool filterInvisible = !pref || pref->useApeExcludeInvisibleNode();
        if (filterInvisible) {
            bool vu = true;
            if (readBool(xe, "vu", "visible-to-user", vu) && !vu) {
                return true;
            }

            const char *vis = nullptr;
            if (readString(xe, "vis", "visibility", vis) && vis && *vis) {
                if (asciiLiteralEqualsIgnoreCase(vis, "gone") ||
                    asciiLiteralEqualsIgnoreCase(vis, "invisible")) {
                    return true;
                }
            }
        }
        const bool filterEmptySlot = !pref || pref->useApeExcludeEmptyChild();
        if (filterEmptySlot) {
            bool hasElemChild = false;
            for (pugi::xml_node ch = xe.first_child(); ch; ch = ch.next_sibling()) {
                if (ch.type() == pugi::node_element) {
                    hasElemChild = true;
                    break;
                }
            }
            if (!hasElemChild) {
                pugi::xml_attribute b = xe.attribute("bounds");
                if (b && b.value() && b.value()[0] == '\0') {
                    return true;
                }
            }
        }
        return false;
    }

    bool elementSubtreeExcludedByApePreference(const ElementPtr &xe, bool isOuterElementRoot) {
        if (!xe || isOuterElementRoot) {
            return false;
        }
        fastbotx::PreferencePtr pref = fastbotx::Preference::inst();
        const bool filterInvisible = !pref || pref->useApeExcludeInvisibleNode();
        if (filterInvisible) {
            if (xe->hasApeVisibleToUserAttribute() && !xe->getApeVisibleToUser()) {
                return true;
            }
            const std::string &vis = xe->getApeVisibilityRaw();
            if (!vis.empty()) {
                if (asciiLiteralEqualsIgnoreCase(vis.c_str(), "gone") ||
                    asciiLiteralEqualsIgnoreCase(vis.c_str(), "invisible")) {
                    return true;
                }
            }
        }
        const bool filterEmptySlot = !pref || pref->useApeExcludeEmptyChild();
        if (filterEmptySlot && xe->getChildren().empty() && xe->hasApeEmptyBoundsAttribute()) {
            return true;
        }
        return false;
    }

    GUITreeNodePtr parseElement(pugi::xml_node xe, const GUITreeNodeWeakPtr &parentWeak,
                                XPathNodeMapper &dom, bool isOuterXmlRoot) {
        if (xmlSubtreeExcludedByApePreference(xe, isOuterXmlRoot)) {
            return nullptr;
        }
        GUITreeNodePtr gn = GUITreeNode::create(parentWeak);
        if (xe.type() == pugi::node_element) {
            dom.registerNode(xe, gn);
        }
        fillFromAttributes(xe, gn);
        for (pugi::xml_node ch = xe.first_child(); ch; ch = ch.next_sibling()) {
            if (ch.type() != pugi::node_element) {
                continue;
            }
            GUITreeNodePtr child = parseElement(ch, gn, dom, false);
            if (child) {
                gn->appendChild(std::move(child));
            }
        }
        return gn;
    }

    void fillFromElement(const ElementPtr &el, GUITreeNodePtr gn) {
        if (!el || !gn) {
            return;
        }
        gn->setIndex(el->getIndex());
        RectPtr b = el->getBounds();
        if (b) {
            gn->setBounds(*b);
        }
        gn->setText(ape_text::normalizeTextForApe(el->getText().c_str()));
        gn->setResourceId(el->getResourceID());
        gn->setClassName(el->getClassname());
        gn->setPackageName(el->getPackageName());
        gn->setContentDesc(ape_text::normalizeContentDescForApe(el->getContentDesc().c_str()));
        gn->setEnabled(el->getEnable());
        gn->setCheckable(el->getCheckable());
        gn->setChecked(el->getChecked());
        gn->setClickable(el->getClickable());
        gn->setFocusable(el->getFocusable());
        gn->setFocused(el->getFocused());
        gn->setScrollable(el->getScrollable() ? 3 : 0);
        gn->setLongClickable(el->getLongClickable());
        gn->setPassword(el->getPassword());
    }

    void fillPugiFromElement(pugi::xml_node xml, const ElementPtr &el) {
        if (!xml || !el) {
            return;
        }
        RectPtr bounds = el->getBounds();
        if (bounds) {
            char boundsBuf[48];
            std::snprintf(boundsBuf, sizeof(boundsBuf), "[%d,%d][%d,%d]",
                          bounds->left, bounds->top, bounds->right, bounds->bottom);
            xml.append_attribute("bounds").set_value(boundsBuf);
        } else {
            xml.append_attribute("bounds").set_value("");
        }
        char idxBuf[32];
        std::snprintf(idxBuf, sizeof(idxBuf), "%d", el->getIndex());
        xml.append_attribute("index").set_value(idxBuf);
        {
            const std::string normText = ape_text::normalizeTextForApe(el->getText().c_str());
            const char *normalizedText = el->isEditText() ? "" : normText.c_str();
            xml.append_attribute("text").set_value(normalizedText);
        }
        xml.append_attribute("class").set_value(el->getClassname().c_str());
        xml.append_attribute("resource-id").set_value(el->getResourceID().c_str());
        xml.append_attribute("package").set_value(el->getPackageName().c_str());
        {
            const std::string normCd = ape_text::normalizeContentDescForApe(el->getContentDesc().c_str());
            xml.append_attribute("content-desc").set_value(normCd.c_str());
        }
        xml.append_attribute("checkable").set_value(el->getCheckable() ? "true" : "false");
        xml.append_attribute("checked").set_value(el->getChecked() ? "true" : "false");
        xml.append_attribute("clickable").set_value(el->getClickable() ? "true" : "false");
        xml.append_attribute("enabled").set_value(el->getEnable() ? "true" : "false");
        xml.append_attribute("focusable").set_value(el->getFocusable() ? "true" : "false");
        xml.append_attribute("focused").set_value(el->getFocused() ? "true" : "false");
        {
            const int scrollableBits = el->getScrollable() ? 3 : 0;
            xml.append_attribute("scrollable").set_value(scrollableBits != 0 ? "true" : "false");
        }
        xml.append_attribute("long-clickable").set_value(el->getLongClickable() ? "true" : "false");
        xml.append_attribute("password").set_value(el->getPassword() ? "true" : "false");
        {
            const int scrollableBits = el->getScrollable() ? 3 : 0;
            const char *stName = computeScrollTypeString(scrollableBits, el->getClassname().c_str());
            xml.append_attribute("scroll-type").set_value(stName);
        }
        if (el->hasApeVisibleToUserAttribute()) {
            xml.append_attribute("visible-to-user").set_value(el->getApeVisibleToUser() ? "true" : "false");
        }
        if (!el->getApeVisibilityRaw().empty()) {
            xml.append_attribute("visibility").set_value(el->getApeVisibilityRaw().c_str());
        }
    }

    GUITreeNodePtr parseElementFromElementWithDom(const ElementPtr &xe, const GUITreeNodeWeakPtr &parentWeak,
                                                  pugi::xml_node xmlNode, XPathNodeMapper &dom,
                                                  bool isOuterElementRoot) {
        if (!xe || elementSubtreeExcludedByApePreference(xe, isOuterElementRoot)) {
            return nullptr;
        }
        GUITreeNodePtr gn = GUITreeNode::create(parentWeak);
        fillFromElement(xe, gn);
        if (xmlNode && xmlNode.type() == pugi::node_element) {
            fillPugiFromElement(xmlNode, xe);
            dom.registerNode(xmlNode, gn);
        }
        for (const auto &ch : xe->getChildren()) {
            if (!ch) {
                continue;
            }
            pugi::xml_node xmlChild{};
            if (xmlNode && xmlNode.type() == pugi::node_element) {
                xmlChild = xmlNode.append_child("node");
            }
            GUITreeNodePtr childGn = parseElementFromElementWithDom(ch, gn, xmlChild, dom, false);
            if (childGn) {
                gn->appendChild(std::move(childGn));
            } else if (xmlChild) {
                xmlNode.remove_child(xmlChild);
            }
        }
        return gn;
    }

#endif

} // namespace

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML

    GUITreeBuildResult GUITreeFactory::buildFromXml(const std::string &utf8, const std::string &activity_package,
                                                    const std::string &activity_class) {
        GUITreeBuildResult r;
        const size_t xmlLen = utf8.size();
        const std::string prefix = utf8.substr(0, std::min<size_t>(120, xmlLen));
        std::string suffix;
        if (xmlLen > 120) {
            suffix = utf8.substr(xmlLen - 120);
        }
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
        GUITreeNodePtr rootGn = parseElement(root, GUITreeNodeWeakPtr(), *bridge, true);
        if (!rootGn) {
            bridge.reset();
            return r;
        }
        postOrderStats(rootGn);
        r.tree = std::make_shared<GUITree>(std::move(rootGn), activity_package, activity_class);
        r.dom = std::move(bridge);
        return r;
    }

    GUITreeBuildResult GUITreeFactory::buildFromElement(const ElementPtr &root, const std::string &activity_package,
                                                        const std::string &activity_class) {
        GUITreeBuildResult r;
        if (!root) {
            return r;
        }
        // Build pugixml DOM directly from the existing Element tree.
        // This avoids Element::toXML() string serialization + pugixml parse on every step.
        auto bridge = std::make_shared<XPathNodeMapper>();
        pugi::xml_node xmlRoot = bridge->initEmptyDocumentWithRoot("node");
        if (!xmlRoot) {
            bridge.reset();
            return r;
        }
        GUITreeNodePtr rootGn = parseElementFromElementWithDom(root, GUITreeNodeWeakPtr(), xmlRoot, *bridge, true);
        if (!rootGn) {
            bridge.reset();
            return r;
        }
        postOrderStats(rootGn);
        r.tree = std::make_shared<GUITree>(std::move(rootGn), activity_package, activity_class);
        r.dom = std::move(bridge);
        return r;
    }

#else

    GUITreeBuildResult GUITreeFactory::buildFromXml(const std::string &, const std::string &, const std::string &) {
        return GUITreeBuildResult{};
    }

    GUITreeBuildResult GUITreeFactory::buildFromElement(const ElementPtr &, const std::string &, const std::string &) {
        return GUITreeBuildResult{};
    }

#endif

} // namespace gui_tree
} // namespace fastbotx
