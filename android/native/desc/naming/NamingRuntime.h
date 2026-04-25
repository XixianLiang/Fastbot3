/**
 * @authors Zhao Zhang
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
