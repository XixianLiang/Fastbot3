#include "XPathNodeMapper.h"

#include <map>
#include <string>
#include <vector>

namespace fastbotx {
namespace gui_tree {

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML

    struct XPathNodeMapper::Impl {
        pugi::xml_document doc;
        std::map<pugi::xml_node, GUITreeNodePtr> node_map;
    };

    XPathNodeMapper::XPathNodeMapper() = default;
    XPathNodeMapper::~XPathNodeMapper() = default;
    XPathNodeMapper::XPathNodeMapper(XPathNodeMapper &&) noexcept = default;
    XPathNodeMapper &XPathNodeMapper::operator=(XPathNodeMapper &&) noexcept = default;

    bool XPathNodeMapper::loadXmlString(const std::string &utf8) {
        impl_ = std::make_unique<Impl>();
        pugi::xml_parse_result pr =
            impl_->doc.load_string(utf8.c_str(), pugi::parse_default | pugi::parse_declaration);
        return static_cast<bool>(pr);
    }

    pugi::xml_node XPathNodeMapper::documentElement() const {
        if (!impl_) {
            return {};
        }
        return impl_->doc.document_element();
    }

    void XPathNodeMapper::registerNode(pugi::xml_node xmlNode, const GUITreeNodePtr &gn) {
        if (!impl_ || !gn) {
            return;
        }
        if (xmlNode && xmlNode.type() == pugi::node_element) {
            impl_->node_map[xmlNode] = gn;
        }
    }

    std::vector<GUITreeNodePtr> XPathNodeMapper::nodesForXPath(const std::string &expr) const {
        std::vector<GUITreeNodePtr> out;
        if (!impl_ || expr.empty()) {
            return out;
        }
        try {
            pugi::xpath_query q(expr.c_str());
            pugi::xml_node ctx = impl_->doc.document_element();
            if (!ctx) {
                return out;
            }
            pugi::xpath_node_set ns = q.evaluate_node_set(ctx);
            for (size_t i = 0; i < ns.size(); ++i) {
                pugi::xpath_node xn = ns[i];
                pugi::xml_node n = xn.node();
                if (!n) {
                    continue;
                }
                auto it = impl_->node_map.find(n);
                if (it != impl_->node_map.end()) {
                    out.push_back(it->second);
                }
            }
        } catch (...) {
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

