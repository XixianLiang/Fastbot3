/**
 * @authors Zhao Zhang
 */
/**
 * Thread-local bridge during `Naming::namingInternal`: maps each widget node to the `Namer` chosen for that node
 * so ancestor-style bitmask namers can query per-node masks (`BitmaskNamer` path).
 */

#include "NamingRuntime.h"

namespace fastbotx {
namespace naming {
namespace {

    /** Active node→namer map for the current naming evaluation stack frame (set by `NamingEvalGuard`). */
    thread_local const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *g_node_to_namer{
        nullptr};

} // namespace

    /** Installs `map` for the duration of a nested naming evaluation (typically non-null during XPath building). */
    void namingEvalSetNodeToNamer(
        const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *map) {
        g_node_to_namer = map;
    }

    /** Clears the thread-local map after evaluation completes. */
    void namingEvalClear() { g_node_to_namer = nullptr; }

    /** Returns the active map, or nullptr outside a guarded naming evaluation. */
    const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *namingEvalNodeToNamer() {
        return g_node_to_namer;
    }

} // namespace naming
} // namespace fastbotx
