/**
 * @authors Zhao Zhang
 */
/**
 * Thread-local context for naming evaluation: which `Namer` applies to each `GUITreeNode` while building XPaths.
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

    /** Sets the per-node namer map for the current thread (see `NamingEvalGuard` in `Naming.cpp`). */
    void namingEvalSetNodeToNamer(
        const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *map);
    /** Removes the thread-local node→namer map after an evaluation scope exits. */
    void namingEvalClear();
    /** Pointer to the active map during evaluation, or nullptr when unset. */
    const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *namingEvalNodeToNamer();

} // namespace naming
} // namespace fastbotx

#endif
