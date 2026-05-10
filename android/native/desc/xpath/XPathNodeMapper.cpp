/**
 * @file XPathNodeMapper.cpp
 *
 * pugixml-backed implementation of DOM storage, node registration, XPath evaluation, and optional
 * stubs when the library is disabled at compile time.
 *
 * @authors Zhao Zhang
 */

#include "XPathNodeMapper.h"

#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

namespace fastbotx {
namespace gui_tree {

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML

    // --- pugixml implementation: document, DOM↔GUI map, XPath compile cache (all under `impl_->mu`) ---

    /** Owning document plus bidirectional lookup from DOM nodes to GUI nodes; guarded by `mu`. */
    struct XPathNodeMapper::Impl {
        pugi::xml_document doc;
        /** Populated by `registerNode`; XPath hits outside this map are skipped (no `GUITreeNode` to return). */
        std::map<pugi::xml_node, GUITreeNodePtr> node_map;
        mutable std::mutex mu;
        /** Compiled expressions keyed by source string (`nodesForXPath` is logically const). */
        mutable std::unordered_map<std::string, pugi::xpath_query> xpath_cache;
        /** Expressions that failed XPath compilation and must short-circuit without retrying compile. */
        mutable std::unordered_set<std::string> xpath_invalid_cache;
    };

    XPathNodeMapper::XPathNodeMapper() = default;
    XPathNodeMapper::~XPathNodeMapper() = default;
    XPathNodeMapper::XPathNodeMapper(XPathNodeMapper &&) noexcept = default;
    XPathNodeMapper &XPathNodeMapper::operator=(XPathNodeMapper &&) noexcept = default;

    bool XPathNodeMapper::loadXmlString(const std::string &utf8) {
        // New `Impl` drops prior DOM, node_map, and XPath caches in one shot.
        impl_ = std::make_unique<Impl>();
        std::lock_guard<std::mutex> lk(impl_->mu);
        pugi::xml_parse_result pr =
            impl_->doc.load_string(utf8.c_str(), pugi::parse_default | pugi::parse_declaration);
        return static_cast<bool>(pr);
    }

    /** Empty tag name clears `impl_` and yields a null node; otherwise allocates a fresh `Impl` and root. */
    pugi::xml_node XPathNodeMapper::initEmptyDocumentWithRoot(const char *tagName) {
        if (!tagName || !*tagName) {
            impl_.reset();
            return {};
        }
        impl_ = std::make_unique<Impl>();
        std::lock_guard<std::mutex> lk(impl_->mu);
        return impl_->doc.append_child(tagName);
    }

    /** Same document element `GUITreeFactory` uses as XPath evaluation context (`/` queries). */
    pugi::xml_node XPathNodeMapper::documentElement() const {
        if (!impl_) {
            return {};
        }
        std::lock_guard<std::mutex> lk(impl_->mu);
        return impl_->doc.document_element();
    }

    void XPathNodeMapper::registerNode(pugi::xml_node xmlNode, const GUITreeNodePtr &gn) {
        if (!impl_ || !gn) {
            return;
        }
        std::lock_guard<std::mutex> lk(impl_->mu);
        // Only element nodes participate in GUI mapping; attributes/text nodes are ignored.
        if (xmlNode && xmlNode.type() == pugi::node_element) {
            impl_->node_map[xmlNode] = gn;
        }
    }

    std::vector<GUITreeNodePtr> XPathNodeMapper::nodesForXPath(const std::string &expr) const {
        std::vector<GUITreeNodePtr> out;
        if (!impl_ || expr.empty()) {
            return out;
        }

        std::lock_guard<std::mutex> lk(impl_->mu);

        if (impl_->xpath_invalid_cache.count(expr) != 0) {
            return out;
        }

        pugi::xml_node ctx = impl_->doc.document_element();
        if (!ctx) {
            return out;
        }

        // Compile path: failures are permanent for this `expr` until process restart (cache lifetime).
        auto it = impl_->xpath_cache.find(expr);
        if (it == impl_->xpath_cache.end()) {
            try {
                pugi::xpath_query q(expr.c_str());
                it = impl_->xpath_cache.emplace(expr, std::move(q)).first;
            } catch (...) {
                impl_->xpath_invalid_cache.insert(expr);
                return out;
            }
        }

        try {
            // Evaluate relative to the document element node-set context.
            pugi::xpath_node_set ns = it->second.evaluate_node_set(ctx);
            for (size_t i = 0; i < ns.size(); ++i) {
                pugi::xpath_node xn = ns[i];
                pugi::xml_node n = xn.node();
                if (!n) {
                    continue;
                }
                auto itNode = impl_->node_map.find(n);
                if (itNode != impl_->node_map.end()) {
                    out.push_back(itNode->second);
                }
            }
        } catch (...) {
            // Unlike compile failures, evaluation errors do not blacklist `expr` (transient data / context).
        }
        return out;
    }

    /** Pretty-printed XML snapshot; not used on the hot path. */
    std::string XPathNodeMapper::dumpXmlString() const {
        if (!impl_) {
            return std::string();
        }
        std::lock_guard<std::mutex> lk(impl_->mu);
        std::ostringstream os;
        impl_->doc.save(os, " ", pugi::format_default);
        return os.str();
    }

#else

    // --- Stub build: factory can still link; XPath and DOM helpers are absent from this TU ---

    /** Placeholder so `XPathNodeMapper` stays constructible when pugixml is compiled out. */
    struct XPathNodeMapper::Impl {};

    XPathNodeMapper::XPathNodeMapper() = default;
    XPathNodeMapper::~XPathNodeMapper() = default;
    XPathNodeMapper::XPathNodeMapper(XPathNodeMapper &&) noexcept = default;
    XPathNodeMapper &XPathNodeMapper::operator=(XPathNodeMapper &&) noexcept = default;

    std::vector<GUITreeNodePtr> XPathNodeMapper::nodesForXPath(const std::string &) const {
        return {};
    }

    std::string XPathNodeMapper::dumpXmlString() const {
        return {};
    }

#endif

} // namespace gui_tree
} // namespace fastbotx

