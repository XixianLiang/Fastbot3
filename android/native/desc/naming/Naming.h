/*
 * APE Naming: ordered Namelets + refinement child map (Java: naming.Naming).
 * extend/replaceLast/doExtend — later (NamingFactory phase).
 */
#ifndef FASTBOTX_DESC_NAMING_NAMING_H_
#define FASTBOTX_DESC_NAMING_NAMING_H_

#include "Namelet.h"
#include "../gui_tree/GUITreeNode.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fastbotx {
namespace naming {

    struct NamingEdge {
        std::shared_ptr<Namelet> from;
        std::shared_ptr<Namelet> to;

        bool operator<(const NamingEdge &o) const {
            if (from.get() != o.from.get()) return from.get() < o.from.get();
            return to.get() < o.to.get();
        }
    };

    class Naming : public std::enable_shared_from_this<Naming> {
    public:
        /** Result of evaluating a Naming on a GUITree (Java inner class NamingResult). */
        struct NamingResult {
            std::vector<NamePtr> names;
            std::vector<std::vector<gui_tree::GUITreeNodePtr>> node_groups;
            std::vector<std::vector<NameletPtr>> namelet_groups;

            size_t getNameSize() const { return names.size(); }

            size_t getNodeSize() const;

            /** Propagate Name / Namelet to nodes (Java NamingResult.updateNames). */
            void updateNames();
        };

        explicit Naming(std::vector<std::shared_ptr<Namelet>> namelets);

        /** Child Naming in the refinement lattice (Java: children map). */
        static std::shared_ptr<Naming> createChild(std::shared_ptr<Naming> parent,
                                                   std::vector<std::shared_ptr<Namelet>> namelets);

        std::shared_ptr<Naming> getParent() const { return parent_.lock(); }

        size_t size() const { return namelets_.size(); }

        const std::vector<std::shared_ptr<Namelet>> &getNamelets() const { return namelets_; }

        /** True when this naming is a leaf in the refinement lattice (Java hasChild). */
        bool hasChild() const { return children_.empty(); }

        int getFineness() const { return fineness_; }

        const std::string &getNamingName() const { return naming_name_; }

        const std::map<NamingEdge, std::shared_ptr<Naming>> &getRefinementChildren() const { return children_; }

        void addRefinementChild(const NamingEdge &edge, std::shared_ptr<Naming> child);

        /** Stable fingerprint for StateKey / lattice (same serialization as StateKey::fromParts). */
        std::string fingerprintString() const;

        virtual ~Naming() = default;

    private:
        Naming(std::shared_ptr<Naming> parent, std::vector<std::shared_ptr<Namelet>> namelets);

        static std::atomic<int> naming_counter_;

        std::string naming_name_;
        std::weak_ptr<Naming> parent_{};
        std::vector<std::shared_ptr<Namelet>> namelets_{};
        int fineness_{-1};

        std::map<NamingEdge, std::shared_ptr<Naming>> children_{};
    };

    using NamingPtr = std::shared_ptr<Naming>;

} // namespace naming
} // namespace fastbotx

#endif
