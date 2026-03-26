#include "NamingRuntime.h"

namespace fastbotx {
namespace naming {
namespace {

    thread_local const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *g_node_to_namer{
        nullptr};

} // namespace

    void namingEvalSetNodeToNamer(
        const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *map) {
        g_node_to_namer = map;
    }

    void namingEvalClear() { g_node_to_namer = nullptr; }

    const std::unordered_map<const gui_tree::GUITreeNode *, const Namer *> *namingEvalNodeToNamer() {
        return g_node_to_namer;
    }

} // namespace naming
} // namespace fastbotx
