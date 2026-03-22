/*
 * APE Namer (Java: com.android.commands.monkey.ape.naming.Namer).
 */
#ifndef FASTBOTX_DESC_NAMING_NAMER_H_
#define FASTBOTX_DESC_NAMING_NAMER_H_

#include "Name.h"
#include "NamerType.h"
#include "../gui_tree/GUITreeNode.h"

#include <vector>

namespace fastbotx {
namespace naming {

    class Namer {
    public:
        virtual ~Namer() = default;

        /** Which attribute dimensions this namer uses (Java: EnumSet<NamerType>). */
        virtual std::vector<NamerType> getNamerTypes() const = 0;

        virtual NamePtr naming(gui_tree::GUITreeNode &node) = 0;

        /**
         * If non-empty, must equal naming(node)->toXPath() for that node (used to dedupe without
         * allocating a Name when the key already exists in evaluateNaming). Default: empty → use naming()+toXPath().
         * evaluateNaming indexes by canonical name->toXPath(); the pre-check lookup only skips naming() when
         * this string matches that canonical key (BitmaskNamer satisfies this).
         */
        virtual std::string xpathKeyForNode(gui_tree::GUITreeNode &node) const {
            (void)node;
            return {};
        }

        virtual bool refinesTo(const Namer &other) const = 0;
    };

    using NamerPtr = std::shared_ptr<Namer>;

    /** Lexicographic compare on sorted namer-type sets (Java Namelet.namerComparator). */
    int compareNamer(const Namer &a, const Namer &b);

} // namespace naming
} // namespace fastbotx

#endif
