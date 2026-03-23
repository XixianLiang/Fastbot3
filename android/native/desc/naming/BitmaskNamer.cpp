/**
 * @authors Zhao Zhang
 */

#include "BitmaskNamer.h"
#include "LocalXPathName.h"
#include "../gui_tree/GUITreeNode.h"

#include <algorithm>
#include <sstream>

namespace fastbotx {
namespace naming {
namespace {

    std::string escapeSingleQuoted(const std::string &s) {
        std::string out;
        out.reserve(s.size() + 4);
        for (char c : s) {
            if (c == '\'') {
                out += "''";
            } else {
                out += c;
            }
        }
        return out;
    }

    static constexpr int kMaxNamerTypeBits = 8;

    uint32_t maskFromNamerTypes(const std::vector<NamerType> &types) {
        uint32_t m = 0;
        for (NamerType t : types) {
            int b = static_cast<int>(t);
            if (b >= 0 && b < kMaxNamerTypeBits) {
                m |= (1u << static_cast<unsigned>(b));
            }
        }
        return m;
    }

    bool hasMask(uint32_t mask, NamerType t) {
        int b = static_cast<int>(t);
        if (b < 0 || b >= kMaxNamerTypeBits) return false;
        return (mask & (1u << static_cast<unsigned>(b))) != 0;
    }

    std::string buildAncestorPath(gui_tree::GUITreeNode *self) {
        if (!self) {
            return "";
        }
        std::vector<gui_tree::GUITreeNodePtr> chain;
        for (auto p = self->getParent(); p; p = p->getParent()) {
            chain.push_back(p);
        }
        if (chain.empty()) {
            return "";
        }
        std::reverse(chain.begin(), chain.end());
        std::ostringstream oss;
        for (const auto &c : chain) {
            oss << "/" << c->getClassName() << "[" << c->getIndex() << "]";
        }
        return oss.str();
    }

    std::string buildPredicates(uint32_t mask, gui_tree::GUITreeNode &node) {
        std::string sb;
        sb.reserve(128);

        if (hasMask(mask, NamerType::TYPE)) {
            const std::string &cls = node.getClassName();
            sb.append("[@class='");
            sb.append(escapeSingleQuoted(cls));
            sb.append("']");
        }

        if (hasMask(mask, NamerType::INDEX)) {
            sb.append("[@index='");
            sb.append(std::to_string(node.getIndex()));
            sb.append("']");
        }

        if (hasMask(mask, NamerType::TEXT)) {
            std::string t = node.getText();
            if (t.empty()) {
                t = node.getContentDesc();
            }
            sb.append("[@text='");
            sb.append(escapeSingleQuoted(t));
            sb.append("']");
        }

        if (hasMask(mask, NamerType::PARENT)) {
            auto pp = node.getParent();
            if (pp) {
                sb.append("[@parent-class='");
                sb.append(escapeSingleQuoted(pp->getClassName()));
                sb.append("'][@parent-index='");
                sb.append(std::to_string(pp->getIndex()));
                sb.append("']");
            } else {
                sb.append("[@parent-class=''][@parent-index='-1']");
            }
        }

        if (hasMask(mask, NamerType::ANCESTOR)) {
            std::string ap = buildAncestorPath(&node);
            sb.append("[@ancestor-path='");
            sb.append(escapeSingleQuoted(ap));
            sb.append("']");
        }

        return sb;
    }

} // namespace

    BitmaskNamer::BitmaskNamer(uint32_t mask) : mask_(mask) {}

    std::shared_ptr<BitmaskNamer> BitmaskNamer::create(uint32_t mask) {
        return std::shared_ptr<BitmaskNamer>(new BitmaskNamer(mask));
    }

    std::vector<NamerType> BitmaskNamer::getNamerTypes() const {
        std::vector<NamerType> out;
        for (int i = 0; i < kMaxNamerTypeBits; ++i) {
            if (mask_ & (1u << static_cast<unsigned>(i))) {
                out.push_back(static_cast<NamerType>(i));
            }
        }
        std::sort(out.begin(), out.end(), [](NamerType a, NamerType b) {
            return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
        });
        return out;
    }

    NamePtr BitmaskNamer::naming(gui_tree::GUITreeNode &node) {
        std::string pred = buildPredicates(mask_, node);
        return std::make_shared<LocalXPathName>(shared_from_this(), std::move(pred));
    }

    std::string BitmaskNamer::xpathKeyForNode(gui_tree::GUITreeNode &node) const {
        return localXPathToXPathWithPredicateTail(buildPredicates(mask_, node));
    }

    bool BitmaskNamer::refinesTo(const Namer &other) const {
        uint32_t om = maskFromNamerTypes(other.getNamerTypes());
        return (mask_ & om) == om;
    }

} // namespace naming
} // namespace fastbotx
