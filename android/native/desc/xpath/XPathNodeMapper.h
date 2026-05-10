/*
 * Keeps the pugixml document used to build a GUITree and maps XPath results to GUITreeNode.
 */
/**
 * @file XPathNodeMapper.h
 *
 * Bridges the GUI tree to XPath: owns an in-memory XML DOM parallel to `GUITreeNode`, registers each
 * DOM element against its tree node, and resolves XPath 1.0 expressions to matching `GUITreeNode` rows.
 * When `FASTBOT_HAS_PUGIXML` is unset, public APIs degrade to no-op / empty results.
 *
 * @authors Zhao Zhang
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

    /**
     * Encapsulates pugixml state for `GUITreeFactory`: document lifecycle, `xml_node` → `GUITreeNodePtr`
     * mapping, and cached XPath compilation. All mutation and queries run under an internal lock.
     */
    class XPathNodeMapper {
    public:
        XPathNodeMapper();
        ~XPathNodeMapper();

        XPathNodeMapper(XPathNodeMapper &&) noexcept;
        XPathNodeMapper &operator=(XPathNodeMapper &&) noexcept;

        XPathNodeMapper(const XPathNodeMapper &) = delete;
        XPathNodeMapper &operator=(const XPathNodeMapper &) = delete;

        /**
         * Evaluates an XPath 1.0 expression against the document root and returns the corresponding
         * GUI nodes. Invalid compile-time expressions are remembered and yield an empty vector;
         * runtime evaluation errors yield empty without blacklisting the expression.
         */
        std::vector<GUITreeNodePtr> nodesForXPath(const std::string &expr) const;
        /** Serializes the live DOM to a string for debugging (indentation via pugixml defaults). */
        std::string dumpXmlString() const;

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
        /**
         * Replaces the internal document by parsing `utf8` as XML (UTF-8). Returns whether parsing succeeded.
         * Clears prior node registrations; callers must rebuild the tree mapping afterward.
         */
        bool loadXmlString(const std::string &utf8);
        /** Creates a new empty document whose document element is `<tagName>...</tagName>`; returns that element. */
        pugi::xml_node initEmptyDocumentWithRoot(const char *tagName);
        /** Root element of the current document, or null if uninitialized / empty. */
        pugi::xml_node documentElement() const;
        /** Associates a DOM element node with its parallel `GUITreeNode` for XPath result translation. */
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
