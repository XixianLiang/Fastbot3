#include "XPathNodeMapper.h"

#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

namespace fastbotx {
namespace gui_tree {

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML

    struct XPathNodeMapper::Impl {
        pugi::xml_document doc;
        std::map<pugi::xml_node, GUITreeNodePtr> node_map;
        // Protects doc/node_map and the XPath caches for future parallel callers.
        mutable std::mutex mu;
        // Cache compiled XPath queries to avoid repeated query compilation.
        // nodesForXPath() is const, so caches are mutable.
        mutable std::unordered_map<std::string, pugi::xpath_query> xpath_cache;
        mutable std::unordered_set<std::string> xpath_invalid_cache;
    };

    XPathNodeMapper::XPathNodeMapper() = default;
    XPathNodeMapper::~XPathNodeMapper() = default;
    XPathNodeMapper::XPathNodeMapper(XPathNodeMapper &&) noexcept = default;
    XPathNodeMapper &XPathNodeMapper::operator=(XPathNodeMapper &&) noexcept = default;

    bool XPathNodeMapper::loadXmlString(const std::string &utf8) {
        impl_ = std::make_unique<Impl>();
        std::lock_guard<std::mutex> lk(impl_->mu);
        pugi::xml_parse_result pr =
            impl_->doc.load_string(utf8.c_str(), pugi::parse_default | pugi::parse_declaration);
        return static_cast<bool>(pr);
    }

    pugi::xml_node XPathNodeMapper::initEmptyDocumentWithRoot(const char *tagName) {
        if (!tagName || !*tagName) {
            impl_.reset();
            return {};
        }
        impl_ = std::make_unique<Impl>();
        std::lock_guard<std::mutex> lk(impl_->mu);
        return impl_->doc.append_child(tagName);
    }

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

        // Fast path: previously-known invalid expressions.
        if (impl_->xpath_invalid_cache.count(expr) != 0) {
            return out;
        }

        pugi::xml_node ctx = impl_->doc.document_element();
        if (!ctx) {
            return out;
        }

        // Compile once, reuse many times.
        auto it = impl_->xpath_cache.find(expr);
        if (it == impl_->xpath_cache.end()) {
            try {
                pugi::xpath_query q(expr.c_str());
                it = impl_->xpath_cache.emplace(expr, std::move(q)).first;
            } catch (...) {
                // Only mark invalid when compilation fails.
                impl_->xpath_invalid_cache.insert(expr);
                return out;
            }
        }

        try {
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
            // Evaluation failure should behave like the original implementation:
            // return empty without permanently poisoning the expression.
        }
        return out;
    }

#else

    struct XPathNodeMapper::Impl {};

    XPathNodeMapper::XPathNodeMapper() = default;
    XPathNodeMapper::~XPathNodeMapper() = default;
    XPathNodeMapper::XPathNodeMapper(XPathNodeMapper &&) noexcept = default;
    XPathNodeMapper &XPathNodeMapper::operator=(XPathNodeMapper &&) noexcept = default;

    std::vector<GUITreeNodePtr> XPathNodeMapper::nodesForXPath(const std::string &) const {
        return {};
    }

#endif

} // namespace gui_tree
} // namespace fastbotx

