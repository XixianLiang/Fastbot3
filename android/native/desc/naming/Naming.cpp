#include "Naming.h"
#include "NamerType.h"

#include <algorithm>
#include <string>

namespace fastbotx {
namespace naming {

    std::atomic<int> Naming::naming_counter_{0};

    Naming::Naming(std::vector<std::shared_ptr<Namelet>> namelets)
        : Naming(nullptr, std::move(namelets)) {}

    std::shared_ptr<Naming> Naming::createChild(std::shared_ptr<Naming> parent,
                                                 std::vector<std::shared_ptr<Namelet>> namelets) {
        return std::shared_ptr<Naming>(new Naming(std::move(parent), std::move(namelets)));
    }

    void Naming::addRefinementChild(const NamingEdge &edge, std::shared_ptr<Naming> child) {
        if (!child) {
            return;
        }
        children_[edge] = std::move(child);
    }

    Naming::Naming(std::shared_ptr<Naming> parent, std::vector<std::shared_ptr<Namelet>> namelets)
        : parent_(parent),
          namelets_(std::move(namelets)) {
        naming_name_ = "Naming[" + std::to_string(naming_counter_.fetch_add(1, std::memory_order_relaxed)) + "]";
        fineness_ = -1;
        for (const auto &nl : namelets_) {
            if (!nl) continue;
            int f = static_cast<int>(nl->getNamerPtr()->getNamerTypes().size());
            if (fineness_ < 0 || f > fineness_) {
                fineness_ = f;
            }
        }
        if (fineness_ < 0) {
            fineness_ = 0;
        }
    }

    size_t Naming::NamingResult::getNodeSize() const {
        size_t s = 0;
        for (const auto &g : node_groups) {
            s += g.size();
        }
        return s;
    }

    void Naming::NamingResult::updateNames() {
        for (size_t i = 0; i < names.size(); ++i) {
            if (i >= node_groups.size()) break;
            for (size_t j = 0; j < node_groups[i].size(); ++j) {
                gui_tree::GUITreeNodePtr &node = node_groups[i][j];
                if (!node) continue;
                node->setXPathName(names[i]);
                if (i < namelet_groups.size() && j < namelet_groups[i].size()) {
                    node->setCurrentNamelet(namelet_groups[i][j]);
                }
            }
        }
    }

    std::string Naming::fingerprintString() const {
        std::vector<std::string> parts;
        parts.reserve(namelets_.size());
        for (const auto &nl : namelets_) {
            if (!nl) {
                continue;
            }
            std::string s = nl->getExprString();
            s.push_back('\x1e');
            std::vector<NamerType> types = nl->getNamer().getNamerTypes();
            std::sort(types.begin(), types.end(), [](NamerType a, NamerType b) {
                return static_cast<unsigned char>(a) < static_cast<unsigned char>(b);
            });
            for (NamerType t : types) {
                s.push_back(static_cast<char>(static_cast<unsigned char>(t)));
            }
            parts.push_back(std::move(s));
        }
        std::sort(parts.begin(), parts.end());
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                out.push_back('|');
            }
            out.append(parts[i]);
        }
        return out;
    }

} // namespace naming
} // namespace fastbotx
