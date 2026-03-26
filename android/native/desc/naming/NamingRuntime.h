/**
 * Thread-local map used while NamingFactory::evaluateNaming builds Name::toXPath()
 * for BitmaskNamer when ANCESTOR (+ PARENT) semantics need each node's assigned Namer
 * (APE AncestorNamer useParent branch).
 */
#ifndef FASTBOTX_DESC_NAMING_NAMINGRUNTIME_H_
#define FASTBOTX_DESC_NAMING_NAMINGRUNTIME_H_

#include <unordered_map>

namespace fastbotx {
namespace gui_tree {
    class GUITreeNode;
}
namespace naming {
    class Namer;

    void namingEvalSetNodeToNamer(
        const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *map);
    void namingEvalClear();
    const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *namingEvalNodeToNamer();

} // namespace naming
} // namespace fastbotx

#endif
