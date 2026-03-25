/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Model_CPP_
#define Model_CPP_

#include "Model.h"
#include "StateFactory.h"
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
#include "../desc/gui_tree/GUITreeNode.h"
#include "../desc/reuse/ActivityNameAction.h"
#endif
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
#include "../desc/gui_tree/GUITreeFactory.h"
#include "../desc/gui_tree/GUITree.h"
#include "../desc/naming/NamingFactory.h"
#endif
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
namespace {
using namespace fastbotx;

struct ApeNonDetPairStat {
    std::string sourceActivity;
    uintptr_t sourceKeyHash{0};
    bool hasSourceStateKey{false};
    naming::StateKey sourceStateKey = naming::StateKey::fromFallbackXmlStringHash("", 0);
    uintptr_t actionHash{0};
    std::unordered_set<uintptr_t> targetKeyHashes;
    size_t targetCount{0};
};

void collectGUITreeNodesPreOrder(const gui_tree::GUITreeNode *node, std::vector<const gui_tree::GUITreeNode *> *out) {
    if (!node || !out) {
        return;
    }
    out->push_back(node);
    for (const auto &ch : node->getChildren()) {
        collectGUITreeNodesPreOrder(ch.get(), out);
    }
}

void applyApeDynamicActionHashesToReuseState(const StatePtr &state,
                                             const std::vector<const gui_tree::GUITreeNode *> &nodesPreOrder,
                                             const naming::StateKey &apeKey) {
    if (!state) {
        return;
    }
    const uintptr_t activityH = fastStringHash(apeKey.activity());
    const WidgetPtrVec &ws = state->getWidgets();
    const bool indexAligned = (ws.size() == nodesPreOrder.size());
    for (const auto &a : state->getActions()) {
        auto ana = std::dynamic_pointer_cast<ActivityNameAction>(a);
        if (!ana) {
            continue;
        }
        WidgetPtr w = ana->getTarget();
        if (!w) {
            ana->applyApeDynamicRlIdentity(activityH, 0x1);
            continue;
        }
        uintptr_t th = w->hash();
        if (indexAligned) {
            size_t idx = SIZE_MAX;
            for (size_t i = 0; i < ws.size(); ++i) {
                if (ws[i] == w) {
                    idx = i;
                    break;
                }
            }
            if (idx != SIZE_MAX) {
                const gui_tree::GUITreeNode *n = nodesPreOrder[idx];
                if (n) {
                    naming::NamePtr nxp = n->getXPathName();
                    if (nxp) {
                        th = fastStringHash(nxp->toXPath());
                    }
                }
            }
        }
        ana->applyApeDynamicRlIdentity(activityH, th);
    }
}
} // namespace

#include "../desc/naming/NamingFactory.h"
#include "../desc/naming/NamerLattice.h"
#include "../desc/naming/NamerFactory.h"
#endif
#include "../Base.h"
#include "../utils.hpp"
#include "../thirdpart/json/json.hpp"
#include "../llm/HttpLlmClient.h"
#include <algorithm>
#include <ctime>
#include <inttypes.h>
#include <iostream>
#include <map>
#include <fstream>
#include <sstream>
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {
    /// Convert WidgetKeyMask to human-readable dimension list for logging (e.g. "Clazz|ResourceID|ContentDesc").
    std::string maskToDimensionString(fastbotx::WidgetKeyMask m) {
        std::ostringstream os;
        const char *sep = "";
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::Clazz)) { os << sep << "Clazz"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::ResourceID)) { os << sep << "ResourceID"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::OperateMask)) { os << sep << "OperateMask"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::ScrollType)) { os << sep << "ScrollType"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::Text)) { os << sep << "Text"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::ContentDesc)) { os << sep << "ContentDesc"; sep = "|"; }
        if (m & static_cast<fastbotx::WidgetKeyMask>(fastbotx::WidgetKeyAttr::Index)) { os << sep << "Index"; sep = "|"; }
        return os.str().empty() ? "(none)" : os.str();
    }

    // APE NamingFactory.resolveNonDeterminism: NDActionBlacklist when refine fails and
    // getOutStateTransitions(action).size() >= 3 (see ape/src/.../NamingFactory.java).
    constexpr int kApeNDActionBlacklistMinOutEdges = 3;

    /**
     * APE NamingFactory.sortRefinementResults / filterRefinementResult tie-break:
     * after primary keys (replay/score), prefer fewer induced partitions — proxy: smaller finenessGain;
     * then lexicographic namelets (expr, then compareNamer — Java NamerComparator + updated expr).
     */
    int compareNamingLexicographicForApeFilter(const fastbotx::naming::NamingPtr &a,
                                               const fastbotx::naming::NamingPtr &b) {
        using namespace fastbotx::naming;
        if (!a && !b) {
            return 0;
        }
        if (!a) {
            return 1;
        }
        if (!b) {
            return -1;
        }
        const auto &va = a->getNamelets();
        const auto &vb = b->getNamelets();
        if (va.size() != vb.size()) {
            return va.size() < vb.size() ? -1 : 1;
        }
        for (size_t i = 0; i < va.size(); ++i) {
            const auto &nla = va[i];
            const auto &nlb = vb[i];
            if (!nla && !nlb) {
                continue;
            }
            if (!nla) {
                return 1;
            }
            if (!nlb) {
                return -1;
            }
            int e = nla->getExprString().compare(nlb->getExprString());
            if (e != 0) {
                return e;
            }
            int n = compareNamer(nla->getNamer(), nlb->getNamer());
            if (n != 0) {
                return n;
            }
        }
        return 0;
    }
}
#endif

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML && DYNAMIC_STATE_ABSTRACTION_ENABLED
namespace {
bool apeStateKeyFromXmlWithNaming(const std::string &activity, const std::string &xml,
                                  const fastbotx::naming::NamingPtr &naming,
                                  fastbotx::naming::StateKey *out) {
    using namespace fastbotx;
    if (!out || !naming || xml.empty()) {
        return false;
    }
    std::string pkg;
    std::string cls;
    naming::StateKey::splitActivityPackageClass(activity, &pkg, &cls);
    gui_tree::GUITreeBuildResult built = gui_tree::GUITreeFactory::buildFromXml(xml, pkg, cls);
    if (!built.tree || !built.dom) {
        return false;
    }
    if (!naming::NamingFactory::rebuildTree(naming, *built.tree, built.dom)) {
        return false;
    }
    *out = naming::StateKey::fromGUITree(*built.tree);
    return true;
}

bool evalApeSourcePartitionPredicateImpl(
    const std::unordered_map<uintptr_t, std::string> &xmlByHash, const std::string &predActivityKeyCanonical,
    const std::string &activity, const naming::NamingPtr &naming,
    const std::vector<std::vector<uintptr_t>> &partitions) {
    if (!naming || partitions.size() < 2) {
        return true;
    }
    if (predActivityKeyCanonical != naming::StateKey::canonicalActivityString(activity)) {
        return true;
    }
    std::unordered_set<uintptr_t> seen;
    for (const auto &part : partitions) {
        for (uintptr_t sh : part) {
            auto it = xmlByHash.find(sh);
            if (it == xmlByHash.end() || it->second.empty()) {
                continue;
            }
            naming::StateKey k = naming::StateKey::fromFallbackXmlStringHash("", 0);
            if (!apeStateKeyFromXmlWithNaming(activity, it->second, naming, &k)) {
                return false;
            }
            if (seen.count(k.hash()) != 0) {
                return false;
            }
        }
        for (uintptr_t sh : part) {
            auto it = xmlByHash.find(sh);
            if (it == xmlByHash.end() || it->second.empty()) {
                continue;
            }
            naming::StateKey k = naming::StateKey::fromFallbackXmlStringHash("", 0);
            if (!apeStateKeyFromXmlWithNaming(activity, it->second, naming, &k)) {
                return false;
            }
            seen.insert(k.hash());
        }
    }
    return true;
}

bool evalApeSourcePartitionPredicateImplTwoNamings(
    const std::unordered_map<uintptr_t, std::string> &xmlByHash, const std::string &predActivityKeyCanonical,
    const std::string &activity, const naming::NamingPtr &namingPrev, const naming::NamingPtr &namingCur,
    const std::vector<std::vector<uintptr_t>> &partitions,
    const std::unordered_set<uintptr_t> &affectedStateHashes) {
    if (!namingPrev || !namingCur || partitions.size() < 2) {
        return true;
    }
    if (predActivityKeyCanonical != naming::StateKey::canonicalActivityString(activity)) {
        return true;
    }
    std::unordered_set<uintptr_t> seen;
    for (const auto &part : partitions) {
        for (uintptr_t sh : part) {
            const auto it = xmlByHash.find(sh);
            if (it == xmlByHash.end() || it->second.empty()) {
                continue;
            }
            const naming::NamingPtr &namingToUse =
                affectedStateHashes.count(sh) != 0 ? namingPrev : namingCur;
            naming::StateKey k = naming::StateKey::fromFallbackXmlStringHash("", 0);
            if (!apeStateKeyFromXmlWithNaming(activity, it->second, namingToUse, &k)) {
                return false;
            }
            if (seen.count(k.hash()) != 0) {
                return false;
            }
        }
        for (uintptr_t sh : part) {
            auto it = xmlByHash.find(sh);
            if (it == xmlByHash.end() || it->second.empty()) {
                continue;
            }
            const naming::NamingPtr &namingToUse =
                affectedStateHashes.count(sh) != 0 ? namingPrev : namingCur;
            naming::StateKey k = naming::StateKey::fromFallbackXmlStringHash("", 0);
            if (!apeStateKeyFromXmlWithNaming(activity, it->second, namingToUse, &k)) {
                return false;
            }
            seen.insert(k.hash());
        }
    }
    return true;
}

bool evalApeActionPartitionPredicateImpl(
    const std::unordered_map<uintptr_t, std::string> &xmlByHash, const std::string &predActivityKeyCanonical,
    const std::string &activity, const fastbotx::naming::NamingPtr &naming,
    const std::vector<std::vector<std::pair<uintptr_t, size_t>>> &partitions) {
    if (!naming || partitions.size() < 2) {
        return true;
    }
    if (predActivityKeyCanonical != naming::StateKey::canonicalActivityString(activity)) {
        return true;
    }
    std::string pkg;
    std::string cls;
    naming::StateKey::splitActivityPackageClass(activity, &pkg, &cls);
    std::unordered_set<std::string> actions;
    for (const auto &part : partitions) {
        std::unordered_set<std::string> temp;
        for (const auto &entry : part) {
            const uintptr_t sh = entry.first;
            const size_t preIdx = entry.second;
            auto itXml = xmlByHash.find(sh);
            if (itXml == xmlByHash.end() || itXml->second.empty()) {
                continue;
            }
            gui_tree::GUITreeBuildResult built = gui_tree::GUITreeFactory::buildFromXml(itXml->second, pkg, cls);
            if (!built.tree || !built.dom) {
                return false;
            }
            if (!naming::NamingFactory::rebuildTree(naming, *built.tree, built.dom)) {
                return false;
            }
            std::vector<const gui_tree::GUITreeNode *> po;
            collectGUITreeNodesPreOrder(built.tree->getRootNode(), &po);
            if (preIdx >= po.size() || !po[preIdx]) {
                return false;
            }
            const naming::NamePtr nm = po[preIdx]->getXPathName();
            if (!nm) {
                return false;
            }
            const std::string x = nm->toXPath();
            const auto itT = temp.insert(x);
            if (itT.second) {
                const auto itA = actions.insert(x);
                if (!itA.second) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool evalApeActionPartitionPredicateImplTwoNamings(
    const std::unordered_map<uintptr_t, std::string> &xmlByHash, const std::string &predActivityKeyCanonical,
    const std::string &activity, const fastbotx::naming::NamingPtr &namingPrev,
    const fastbotx::naming::NamingPtr &namingCur,
    const std::vector<std::vector<std::pair<uintptr_t, size_t>>> &partitions,
    const std::unordered_set<uintptr_t> &affectedStateHashes) {
    if (!namingPrev || !namingCur || partitions.size() < 2) {
        return true;
    }
    if (predActivityKeyCanonical != naming::StateKey::canonicalActivityString(activity)) {
        return true;
    }
    std::string pkg;
    std::string cls;
    naming::StateKey::splitActivityPackageClass(activity, &pkg, &cls);
    std::unordered_set<std::string> actions;
    for (const auto &part : partitions) {
        std::unordered_set<std::string> temp;
        for (const auto &entry : part) {
            const uintptr_t sh = entry.first;
            const size_t preIdx = entry.second;
            auto itXml = xmlByHash.find(sh);
            if (itXml == xmlByHash.end() || itXml->second.empty()) {
                continue;
            }
            const fastbotx::naming::NamingPtr &namingToUse =
                affectedStateHashes.count(sh) != 0 ? namingPrev : namingCur;
            gui_tree::GUITreeBuildResult built = gui_tree::GUITreeFactory::buildFromXml(itXml->second, pkg, cls);
            if (!built.tree || !built.dom) {
                return false;
            }
            if (!naming::NamingFactory::rebuildTree(namingToUse, *built.tree, built.dom)) {
                return false;
            }
            std::vector<const gui_tree::GUITreeNode *> po;
            collectGUITreeNodesPreOrder(built.tree->getRootNode(), &po);
            if (preIdx >= po.size() || !po[preIdx]) {
                return false;
            }
            const naming::NamePtr nm = po[preIdx]->getXPathName();
            if (!nm) {
                return false;
            }
            const std::string x = nm->toXPath();
            const auto itT = temp.insert(x);
            if (itT.second) {
                const auto itA = actions.insert(x);
                if (!itA.second) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool evalApeStatesFewerThanPredicateImpl(
    const std::unordered_map<uintptr_t, std::string> &xmlByHash, const std::string &predActivityKeyCanonical,
    const std::string &activity, const fastbotx::naming::NamingPtr &naming,
    const std::vector<uintptr_t> &stateHashes, int threshold) {
    if (!naming || threshold < 1) {
        return true;
    }
    if (predActivityKeyCanonical != naming::StateKey::canonicalActivityString(activity)) {
        return true;
    }
    std::string pkg;
    std::string cls;
    naming::StateKey::splitActivityPackageClass(activity, &pkg, &cls);
    std::unordered_set<uintptr_t> distinct;
    for (uintptr_t sh : stateHashes) {
        auto itXml = xmlByHash.find(sh);
        if (itXml == xmlByHash.end() || itXml->second.empty()) {
            continue;
        }
        gui_tree::GUITreeBuildResult built = gui_tree::GUITreeFactory::buildFromXml(itXml->second, pkg, cls);
        if (!built.tree || !built.dom) {
            return false;
        }
        if (!naming::NamingFactory::rebuildTree(naming, *built.tree, built.dom)) {
            return false;
        }
        naming::StateKey k = naming::StateKey::fromGUITree(*built.tree);
        distinct.insert(k.hash());
        if (static_cast<int>(distinct.size()) > threshold) {
            return false;
        }
    }
    return true;
}

bool evalApeStatesFewerThanPredicateImplTwoNamings(
    const std::unordered_map<uintptr_t, std::string> &xmlByHash, const std::string &predActivityKeyCanonical,
    const std::string &activity, const fastbotx::naming::NamingPtr &namingPrev,
    const fastbotx::naming::NamingPtr &namingCur, const std::vector<uintptr_t> &stateHashes,
    int threshold, const std::unordered_set<uintptr_t> &affectedStateHashes) {
    if (!namingPrev || !namingCur || threshold < 1) {
        return true;
    }
    if (predActivityKeyCanonical != naming::StateKey::canonicalActivityString(activity)) {
        return true;
    }
    std::string pkg;
    std::string cls;
    naming::StateKey::splitActivityPackageClass(activity, &pkg, &cls);
    std::unordered_set<uintptr_t> distinct;
    for (uintptr_t sh : stateHashes) {
        auto itXml = xmlByHash.find(sh);
        if (itXml == xmlByHash.end() || itXml->second.empty()) {
            continue;
        }
        const fastbotx::naming::NamingPtr &namingToUse =
            affectedStateHashes.count(sh) != 0 ? namingPrev : namingCur;
        gui_tree::GUITreeBuildResult built = gui_tree::GUITreeFactory::buildFromXml(itXml->second, pkg, cls);
        if (!built.tree || !built.dom) {
            return false;
        }
        if (!naming::NamingFactory::rebuildTree(namingToUse, *built.tree, built.dom)) {
            return false;
        }
        naming::StateKey k = naming::StateKey::fromGUITree(*built.tree);
        distinct.insert(k.hash());
        if (static_cast<int>(distinct.size()) > threshold) {
            return false;
        }
    }
    return true;
}

} // namespace
#endif

namespace fastbotx {

    WidgetKeyMask Model::getActivityKeyMask(const std::string &activity) const {
        auto it = _activityKeyMask.find(activity);
        if (it != _activityKeyMask.end()) {
            return it->second;
        }
        return DefaultWidgetKeyMask;
    }

    std::shared_ptr<LlmClient> Model::getLlmClient() const {
        return _llmTaskAgent ? _llmTaskAgent->getLlmClient() : nullptr;
    }

    void Model::setActivityKeyMask(const std::string &activity, WidgetKeyMask mask) {
        _activityKeyMask[activity] = mask;
    }

    /**
     * @brief Log state information with each widget and action on a separate line
     * 
     * This helper function formats state information for debugging/logging purposes.
     * It prints the state hash, all widgets, and all actions in a readable format.
     * Long strings (>3000 chars) are split across multiple log lines.
     * 
     * @param state The state to log (nullptr is handled gracefully)
     */
    inline void logStatePerLine(const StatePtr &state) {
        if (state == nullptr) {
            BDLOGE("State is null, cannot log state information");
            return;
        }
        
        // Print state header with hash code
        BDLOG("{state: %lu", static_cast<unsigned long>(state->hash()));
        
        // Print each widget on a separate line for better readability; skip empty (e.g. toXPath returns "" when details cleared)
        BDLOG("widgets:");
        const auto &widgets = state->getWidgets();
        for (const auto &widget : widgets) {
            std::string widgetStr = widget->toString();
            if (widgetStr.empty()) continue;
            // If widget string is too long, split it across multiple log lines
            if (widgetStr.length() > 3000) {
                logLongStringInfo("   " + widgetStr);
            } else {
                BDLOG("   %s", widgetStr.c_str());
            }
        }
        
        // Print each action on a separate line for better readability
        BDLOG("action:");
        const auto &actions = state->getActions();
        for (const auto &action : actions) {
            std::string actionStr = action->toString();
            // If action string is too long, split it across multiple log lines
            if (actionStr.length() > 3000) {
                logLongStringInfo("   " + actionStr);
            } else {
                BDLOG("   %s", actionStr.c_str());
            }
        }
        
        BDLOG("}");
    }

    /**
     * @brief Factory method to create a new Model instance
     * 
     * Uses new + shared_ptr instead of make_shared because the constructor is protected
     * and make_shared cannot access protected constructors from outside the class.
     * 
     * @return Shared pointer to a new Model instance
     */
    std::shared_ptr<Model> Model::create() {
        return std::shared_ptr<Model>(new Model());
    }

    /**
     * @brief Constructor for Model class
     * 
     * Initializes the model with:
     * - A new Graph instance for state management
     * - Preference singleton instance
     * - Network action parameters set to default values
     */
    Model::Model() {
#ifndef FASTBOT_VERSION
    // Use build timestamp if available, otherwise use compile-time date/time
    #ifdef FASTBOT_BUILD_TIMESTAMP
        #define FASTBOT_VERSION FASTBOT_BUILD_TIMESTAMP
    #else
        // Fallback to compiler's __DATE__ and __TIME__ macros
        #define FASTBOT_VERSION __DATE__ " " __TIME__
    #endif
#endif
        BLOG("----Fastbot native build version: " FASTBOT_VERSION "----\n");
        this->_graph = std::make_shared<Graph>();
        this->_preference = Preference::inst();
        this->_netActionParam.netActionTaskid = 0;

        // Initialize LLMTaskAgent with HTTP LLM client if LLM is enabled in config.
        LlmRuntimeConfig llmCfg;
        if (this->_preference) {
            llmCfg = this->_preference->getLlmRuntimeConfig();
        }
        std::shared_ptr<LlmClient> client = nullptr;
        if (llmCfg.enabled) {
            client = std::make_shared<HttpLlmClient>(llmCfg);
            BLOG("LLMTaskAgent: HTTP LLM client initialized with model %s", llmCfg.model.c_str());
        } else {
            BLOG("LLMTaskAgent: LLM is disabled in config");
        }
        this->_llmTaskAgent = std::make_shared<LLMTaskAgent>(this->_preference, client);
        this->_apeStateNamingManager = std::make_shared<naming::StateNamingManager>(nullptr);
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        this->_apeTransitionLog.resize(MaxTransitionLogSize);
        BLOG("state abstraction: enabled (check interval=%d, batch every %d steps)",
             (int)RefinementCheckInterval, (int)RefinementCheckInterval);
#endif
    }


    /**
     * @brief General entry point for getting next operation step according to RL model
     * 
     * This is the main entry point that accepts XML content as a string.
     * It parses the XML string into an Element object and delegates to the
     * ElementPtr-based version of getOperate().
     * 
     * @param descContent XML content of the current page as a string
     * @param activity Activity name string
     * @param deviceID Device ID string (default: empty string uses default device)
     * @return Next operation step in JSON format, or empty string if parsing fails
     */
    std::string Model::getOperate(const std::string &descContent, const std::string &activity,
                                  const std::string &deviceID) {
        // Parse XML string into Element object using tinyxml2
        ElementPtr elem = Element::createFromXml(descContent);
        if (nullptr == elem) {
            return "";
        }
        // Delegate to ElementPtr-based version
        return this->getOperate(elem, activity, deviceID);
    }

    /**
     * @brief Create and add an agent to the model for a specific device
     * 
     * Creates a new agent using the AgentFactory, adds it to the device-agent map,
     * and registers it as a listener to the graph for state change notifications.
     * 
     * @param deviceIDString Device ID string (empty string uses default device ID)
     * @param agentType The type of algorithm/agent to create
     * @param deviceType The type of device (default: Normal)
     * @return Shared pointer to the newly created agent
     */
    AbstractAgentPtr Model::addAgent(const std::string &deviceIDString, AlgorithmType agentType,
                                     DeviceType deviceType) {
        // Create agent using factory pattern
        auto agent = AgentFactory::create(agentType, shared_from_this(), deviceType);
        
        // Use default device ID if empty string provided
        const std::string &deviceID = deviceIDString.empty() ? ModelConstants::DefaultDeviceID
                                                             : deviceIDString;
        
        // Add the device-agent pair to the map
        this->_deviceIDAgentMap.emplace(deviceID, agent);
        
        // Register agent as a listener to graph updates
        // This allows the agent to be notified when new states are added
        this->_graph->addListener(agent);
        
        return agent;
    }

    /**
     * @brief Get the agent for a specific device ID
     * 
     * @param deviceID Device ID string (empty string uses default device ID)
     * @return Shared pointer to the agent, or nullptr if not found
     */
    AbstractAgentPtr Model::getAgent(const std::string &deviceID) const {
        const std::string &d = deviceID.empty() ? ModelConstants::DefaultDeviceID : deviceID;
        auto iter = this->_deviceIDAgentMap.find(d);
        if (iter != this->_deviceIDAgentMap.end()) {
            return iter->second;
        }
        return nullptr;
    }


    /**
     * @brief Get next operation step from Element object, returning JSON string
     * 
     * This method wraps the core getOperateOpt() method and converts the result
     * to a JSON string format.
     * 
     * @param element XML Element object of the current page
     * @param activity Activity name string
     * @param deviceID Device ID string (default: empty string uses default device)
     * @return Next operation step in JSON format
     */
    std::string Model::getOperate(const ElementPtr &element, const std::string &activity,
                                  const std::string &deviceID) {
        OperatePtr operate = getOperateOpt(element, activity, deviceID);
        std::string operateString = operate->toString();
        return operateString;
    }


    /**
     * @brief Get custom action from preference if one exists for this page
     * 
     * Checks if the user has specified a custom action for this activity/page
     * in the preference settings. Returns nullptr if no custom action is defined.
     * 
     * @param activity Activity name string
     * @param element XML Element object of the current page
     * @return Custom action if exists, nullptr otherwise
     */
    /**
     * @brief Get or create an activity string pointer
     * 
     * This method optimizes memory usage by reusing existing activity string pointers
     * from the graph's visited activities set. If the activity already exists,
     * returns the cached shared pointer. Otherwise, creates a new one.
     * 
     * Performance optimization:
     * - Reuses existing string pointers to avoid duplicate string storage
     * - Uses hash-based set lookup for O(log n) complexity
     * 
     * @param activity The activity name string
     * @return Shared pointer to the activity string (cached or newly created)
     * 
     * @note The returned pointer may be from the cache or newly created.
     *       Newly created pointers will be added to the graph's visited activities
     *       when the state is added via createAndAddState().
     */
    stringPtr Model::getOrCreateActivityPtr(const std::string &activity) {
        // Get the set of visited activities (returns by value, but set is typically small)
        const stringPtrSet& activityStringPtrSet = this->_graph->getVisitedActivities();
        
        // Create temporary shared_ptr for lookup
        // Note: This creates a temporary object for comparison only
        // If not found, we'll return this pointer; if found, we'll return the cached one
        stringPtr tempActivityPtr = std::make_shared<std::string>(activity);
        
        // Try to find existing activity pointer in the set
        auto founded = activityStringPtrSet.find(tempActivityPtr);
        
        if (founded == activityStringPtrSet.end()) {
            // This is a new activity, return the newly created pointer
            return tempActivityPtr;
        } else {
            // Activity already exists, return the cached pointer to avoid duplication
            return *founded;
        }
    }

    /**
     * @brief Get or create an agent for the given device ID
     * 
     * This method retrieves an agent for the specified device ID. If no agent exists
     * for the device ID, returns the default agent. If no agents exist at all,
     * creates a default reuse agent.
     * 
     * Performance optimization:
     * - Uses find() instead of [] operator to avoid creating unnecessary map entries
     * - Falls back to default device ID if device ID is not found
     * 
     * @param deviceID The device ID string (empty string uses default device ID)
     * @return Shared pointer to the agent for the device
     * 
     * @note If the device ID is not found, returns the default agent instead of
     *       creating a new one. This ensures all devices have an agent to use.
     */
        AbstractAgentPtr Model::getOrCreateAgent(const std::string &deviceID) {
        // Create a default agent if map is empty
        if (this->_deviceIDAgentMap.empty()) {
            BLOG("%s", "use DoubleSarsaAgent as the default agent");
            this->addAgent(ModelConstants::DefaultDeviceID, AlgorithmType::DoubleSarsa);
        }
        
        // Use find() instead of [] to avoid creating unnecessary map entries
        // Performance: O(log n) lookup without side effects
        auto agentIterator = this->_deviceIDAgentMap.find(deviceID);
        
        if (agentIterator == this->_deviceIDAgentMap.end()) {
            // Device ID not found, return the default agent
            // Use find() again to avoid [] operator side effects
            auto defaultIterator = this->_deviceIDAgentMap.find(ModelConstants::DefaultDeviceID);
            if (defaultIterator != this->_deviceIDAgentMap.end()) {
                return defaultIterator->second;
            }
            // Should not reach here if addAgent worked correctly, but handle gracefully
            return nullptr;
        } else {
            // Found the agent for this device ID
            return agentIterator->second;
        }
    }

    /**
     * @brief Create a new state from element and add it to the graph
     * 
     * Creates a state object based on the agent's algorithm type, then adds it
     * to the graph. The graph will deduplicate if a similar state already exists.
     * Marks the state as visited with the current graph timestamp.
     * 
     * @param element XML Element object of the current page (must not be nullptr)
     * @param agent The agent to use for state creation (determines state type)
     * @param activityPtr Shared pointer to activity name string
     * @return Shared pointer to the created/existing state, or nullptr if element is null
     */
    StatePtr Model::buildStateOnly(const ElementPtr &element, const AbstractAgentPtr &agent,
                                   const stringPtr &activityPtr) {
        if (nullptr == element) {
            return nullptr;
        }
        std::string activityStr = activityPtr ? *activityPtr : "";
        WidgetKeyMask mask = getActivityKeyMask(activityStr);
        StatePtr state = StateFactory::createState(agent->getAlgorithmType(), activityPtr, element, mask);
        return state;
    }

    StatePtr Model::createAndAddState(const ElementPtr &element, const AbstractAgentPtr &agent,
                                      const stringPtr &activityPtr) {
        StatePtr state = buildStateOnly(element, agent, activityPtr);
        if (!state) return nullptr;
        state = this->_graph->addState(state);
        state->visit(this->_graph->getTimestamp());
        return state;
    }

    /**
     * @brief Select an action based on state, agent, and custom preferences
     * 
     * This method implements the action selection logic:
     * 1. Uses custom action from preference if available
     * 2. Checks for blocked state and returns RESTART if needed
     * 3. Otherwise, asks the agent to resolve a new action
     * 4. Updates agent strategy and marks action as visited if it's a model action
     * 
     * @param state The current state (may be modified)
     * @param agent The agent to use for action selection (may be modified)
     * @param customAction Custom action from preference, if any
     * @param actionCost Output parameter: time cost for action generation in seconds
     * @return Selected action, or nullptr if selection failed
     */
    ActionPtr Model::selectAction(StatePtr &state, AbstractAgentPtr &agent, ActionPtr customAction, double &actionCost) {
        double startGeneratingActionTimestamp = currentStamp();
        actionCost = 0.0;
        ActionPtr action = customAction; // Use custom action if provided

        // Log state information for debugging
        logStatePerLine(state);

        // Check if preference indicates we should skip model actions (listen mode)
        bool shouldSkipActionsFromModel = this->_preference ? this->_preference->skipAllActionsFromModel() : false;
        if (shouldSkipActionsFromModel) {
            LOGI("listen mode skip get action from model");
        }

        // If no custom action specified and not in listen mode, get action from agent
        if (nullptr == customAction && !shouldSkipActionsFromModel) {
            // Check if we're in a blocked state and should restart
            if (-1 != BLOCK_STATE_TIME_RESTART &&
                -1 != Preference::inst()->getForceMaxBlockStateTimes() &&
                agent->getCurrentStateBlockTimes() > BLOCK_STATE_TIME_RESTART) {
                // Force restart action when stuck in blocked state
                action = Action::RESTART;
                BLOG("Ran into a block state %s", state ? state->getId().c_str() : "");
            } else {
                // Ask agent to resolve a new action (this is the main RL model entry point)
                auto resolvedAction = agent->resolveNewAction();
                action = std::dynamic_pointer_cast<Action>(resolvedAction);
                
                // Update agent's strategy based on the new action
                agent->updateStrategy();
                
                if (nullptr == action) {
                    BDLOGE("get null action!!!!");
                    return nullptr; // Handle null action gracefully
                }
            }
            
            // Calculate action generation time cost
            double endGeneratingActionTimestamp = currentStamp();
            actionCost = endGeneratingActionTimestamp - startGeneratingActionTimestamp;
            
            // moveForward is now called at the start of the next getOperateOpt (before addState),
            // so (fromState, actionTaken, nextState) is correct for AIG/updateKnowledge.
            if (state && action->isModelAct()) {
                action->visit(this->_graph->getTimestamp());
            }
        }
        
        return action;
    }

    /**
     * @brief Convert an action to an operate object and apply patches
     * 
     * Converts an Action object to a DeviceOperateWrapper (OperatePtr) that can be
     * executed. If the action requires a target widget, extracts widget information.
     * Applies preference patches and optionally clears state details for memory optimization.
     * 
     * @param action The action to convert (nullptr returns NOP operation)
     * @param state The current state (used for detail clearing optimization)
     * @return OperatePtr The operation object ready for execution
     */
    OperatePtr Model::convertActionToOperate(ActionPtr action, StatePtr state) {
        if (action == nullptr) {
            // Return no-operation if action is null
            return DeviceOperateWrapper::OperateNop;
        }

        BLOG("selected action %s", action->toString().c_str());
        
        // Convert action to operation object
        OperatePtr opt = action->toOperate();

        // If action requires a target widget, extract widget information
        if (action->requireTarget()) {
            if (auto stateAction = std::dynamic_pointer_cast<fastbotx::ActivityStateAction>(action)) {
                std::shared_ptr<Widget> widget = stateAction->getTarget();
                if (widget) {
                    // Serialize widget to JSON and attach to operation
                    std::string widget_str = widget->toJson();
                    opt->widget = widget_str;
                    BLOG("stateAction Widget: %s", widget_str.c_str());
                }
            }
        }

        // Apply preference patches to the operation (e.g., custom modifications)
        if (this->_preference) {
            this->_preference->patchOperate(opt);
        }

        // Memory optimization: clear state details after use if enabled
        // This reduces memory usage for states that are no longer needed in detail
        if (DROP_DETAIL_AFTER_SATE && state && !state->hasNoDetail()) {
            state->clearDetails();
        }

        return opt;
    }

    /**
     * @brief Core method for getting next operation step and updating RL model
     * 
     * This is the main orchestration method that:
     * 1. Gets custom action from preference if available
     * 2. Gets or creates activity pointer (memory optimization)
     * 3. Gets or creates agent for the device
     * 4. Creates and adds state to the graph
     * 5. Selects an action using the agent or custom action
     * 6. Converts action to operation object
     * 7. Logs performance metrics
     * 
     * @param element XML Element object of the current page
     * @param activity Activity name string
     * @param deviceID Device ID string (default: empty string uses default device)
     * @return DeviceOperateWrapper object containing the next operation to perform
     * 
     * @note This method updates the RL model by adding states and actions to the graph
     */
    OperatePtr Model::getOperateOpt(const ElementPtr &element, const std::string &activity,
                                    const std::string &deviceID) {
        // Record method start time for performance tracking
        double methodStartTimestamp = currentStamp();
        
        // Step 0: Match LLM task on raw tree (before resolvePage) so checkpoint matches unmodified UI.
        LlmTaskConfigPtr preMatchedLlmTask = nullptr;
        if (this->_preference && element) {
            preMatchedLlmTask = this->_preference->matchLlmTask(activity, element);
        }
        
        // Step 1: Resolve page (black widgets, tree pruning, valid texts) before using element.
        if (this->_preference && element) {
            this->_preference->resolvePage(activity, element);
        }
        // Step 2: Custom action from max.xpath.actions (if any) for this activity and page.
        ActionPtr customAction = (this->_preference && element)
            ? this->_preference->getCustomActionFromXpath(activity, element)
            : nullptr;
        
        // Step 3: Get or create activity pointer (reuses existing pointers for memory efficiency)
        stringPtr activityPtr = getOrCreateActivityPtr(activity);
        
        // Step 4: Get or create agent for this device (creates default if needed)
        AbstractAgentPtr agent = getOrCreateAgent(deviceID);
        
        // Step 5: Build state, notify agent of transition (moveForward) before adding to graph, then add state
        // moveForward(currentState) must run before addState so agent still has previous _newState/_newAction
        // for (fromState, actionTaken, nextState) → updateKnowledge / AIG edges (see FIND_NAVIGATE_PATH_CODE_REVIEW §7).
        double buildStateStartTimestamp = currentStamp();
        StatePtr built = buildStateOnly(element, agent, activityPtr);
        StatePtr state = built;
        if (state) {
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
            naming::StateKey apeKey = naming::StateKey::fromParts(
                naming::StateKey::canonicalActivityString(activity), nullptr, {});
            bool haveApeKey = false;
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
            const bool wantApeRlIdentity = !_preference || !_preference->useStaticReuseAbstraction();
            const bool wantApeGraphDedup =
                _preference && _preference->useApeGraphDedupByStateKey();
            const bool wantApeStateKey = wantApeRlIdentity || wantApeGraphDedup;
            if (wantApeStateKey) {
                haveApeKey = buildApeStateKeyFromElementTree(
                    element, activity, &apeKey, wantApeRlIdentity ? built : StatePtr());
            } else {
                haveApeKey = false;
            }
            if (wantApeRlIdentity) {
                if (haveApeKey) {
                    built->applyDynamicAbstractionIdentityHash(apeKey.hash());
                } else {
                    const uintptr_t xmlH = fastStringHash(element->toXML());
                    apeKey = naming::StateKey::fromFallbackXmlStringHash(activity, xmlH);
                    built->applyDynamicAbstractionIdentityHash(apeKey.hash());
                    applyApeDynamicActionHashesToReuseState(built, {}, apeKey);
                    haveApeKey = true;
                }
            }
#else
            haveApeKey = buildApeStateKeyFromElementTree(element, activity, &apeKey);
#endif
            if (haveApeKey && _preference && _preference->useApeGraphDedupByStateKey()) {
                const uintptr_t kh = apeKey.hash();
                auto itDedup = _ape_graph_state_by_key.find(kh);
                if (itDedup != _ape_graph_state_by_key.end()) {
                    agent->moveForward(itDedup->second);
                    _graph->recordStateVisit(itDedup->second, built);
                    state = itDedup->second;
                } else {
                    agent->moveForward(built);
                    state = _graph->addState(built);
                    _ape_graph_state_by_key.emplace(kh, state);
                }
            } else {
                agent->moveForward(built);
                state = _graph->addState(built);
            }
            state->visit(this->_graph->getTimestamp());
            if (haveApeKey) {
                recordApeStateKey(state, apeKey);
                logApeStateKeySnapshot(activity, state, apeKey, _graph);
            }
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
            if (element && _preference && !_preference->useStaticReuseAbstraction() &&
                _preference->useApeNamingCandidateTransitionReplay()) {
                const uintptr_t sh = state->hash();
                _apeStateXmlByStateHash[sh] = element->toXML();
                constexpr size_t kMaxApeXmlCache = 384;
                while (_apeStateXmlByStateHash.size() > kMaxApeXmlCache) {
                    _apeStateXmlByStateHash.erase(_apeStateXmlByStateHash.begin());
                }
            }
#endif
#elif DYNAMIC_STATE_ABSTRACTION_ENABLED
            // No pugixml in this build: XPath/GUITree unavailable — use XML-digest StateKey only (still APE-style id).
            naming::StateKey apeKey = naming::StateKey::fromParts(
                naming::StateKey::canonicalActivityString(activity), nullptr, {});
            bool haveApeKey = false;
            const bool wantApeRlIdentity = !_preference || !_preference->useStaticReuseAbstraction();
            std::vector<const gui_tree::GUITreeNode *> guiPreOrder;
            if (wantApeRlIdentity) {
                const uintptr_t xmlH = fastStringHash(element->toXML());
                apeKey = naming::StateKey::fromFallbackXmlStringHash(activity, xmlH);
                built->applyDynamicAbstractionIdentityHash(apeKey.hash());
                applyApeDynamicActionHashesToReuseState(built, guiPreOrder, apeKey);
                haveApeKey = true;
            }
            if (haveApeKey && _preference && _preference->useApeGraphDedupByStateKey()) {
                const uintptr_t kh = apeKey.hash();
                auto itDedup = _ape_graph_state_by_key.find(kh);
                if (itDedup != _ape_graph_state_by_key.end()) {
                    agent->moveForward(itDedup->second);
                    _graph->recordStateVisit(itDedup->second, built);
                    state = itDedup->second;
                } else {
                    agent->moveForward(built);
                    state = _graph->addState(built);
                    _ape_graph_state_by_key.emplace(kh, state);
                }
            } else {
                agent->moveForward(built);
                state = _graph->addState(built);
            }
            state->visit(this->_graph->getTimestamp());
            if (haveApeKey) {
                recordApeStateKey(state, apeKey);
                logApeStateKeySnapshot(activity, state, apeKey, _graph);
            }
#else
            agent->moveForward(state);
            state = this->_graph->addState(state);
            state->visit(this->_graph->getTimestamp());
#endif
        }
        double buildStateEndTimestamp = currentStamp();
        bool fromLlm = (_llmTaskAgent && _llmTaskAgent->inSession());
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        if (!fromLlm) {
            recordTransition(agent, state);
        }
#endif
        // Step 5b: Removed — image now stays in Java (setLastScreenshotForLlm + doLlmHttpPostFromPrompt).
        // Native no longer returns NOP when screenshotBytes is empty; Java always has the image when needed.
        // Step 6: Optionally delegate to LLMTaskAgent before RL (pass pre-matched task from raw tree).
        ActionPtr llmAction = nullptr;
        if (this->_llmTaskAgent) {
            llmAction = this->_llmTaskAgent->selectNextAction(element, activity, deviceID, preMatchedLlmTask);
        }

        // Step 7: Select action (either LLM, custom, restart, or from agent)
        double actionCost = 0.0;
        ActionPtr action;
        if (llmAction) {
            // When LLMTaskAgent returns an action, we bypass RL for this step.
            action = llmAction;
        } else {
            action = selectAction(state, agent, customAction, actionCost);
        }
        
        // Handle null action gracefully
        if (nullptr == action) {
            return DeviceOperateWrapper::OperateNop;
        }

        // Resolve merged widgets: when multiple concrete nodes share the same abstract widget,
        // set action target to the next concrete node (visitCount % total) so each selection hits a different node (e.g. 特价→首页→秒送→新品).
        if (state && action && action->requireTarget()) {
            if (auto stateAction = std::dynamic_pointer_cast<ActivityStateAction>(action)) {
                state->resolveAt(stateAction, _graph->getTimestamp());
            }
        }

        // Step 8: Convert action to operation object and apply patches
        OperatePtr opt = convertActionToOperate(action, state);
        if (llmAction) {
            opt->allowFuzzing = false;
        }
        // Optional: agent-provided LLM-generated input text (e.g. LLMExplorerAgent content-aware input)
        if (agent) {
            std::string agentInputText = agent->getInputTextForAction(state, action);
            if (!agentInputText.empty()) {
                opt->setText(agentInputText);
            }
        }

        // Record end time and log performance metrics (currentStamp returns ms, keep ms for log)
        double methodEndTimestamp = currentStamp();
        double buildStateCostMs = buildStateEndTimestamp - buildStateStartTimestamp;
        double actionCostMs = actionCost;
        double totalCostMs = methodEndTimestamp - methodStartTimestamp;
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        if (state && state->usesDynamicAbstractionIdentityHash()) {
            BLOG("build state cost: %.3fms action cost: %.3fms total cost: %.3fms dims=[APE]",
                 buildStateCostMs,
                 actionCostMs,
                 totalCostMs);
        } else {
            BLOG("build state cost: %.3fms action cost: %.3fms total cost: %.3fms dims=[%s]",
                 buildStateCostMs,
                 actionCostMs,
                 totalCostMs,
                 maskToDimensionString(getActivityKeyMask(activity)).c_str());
        }
#else
        BLOG("build state cost: %.3fms action cost: %.3fms total cost: %.3fms",
             buildStateCostMs,
             actionCostMs,
             totalCostMs);
#endif
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        if (!fromLlm) {
            _stepCountSinceLastCheck++;
            if (_stepCountSinceLastCheck >= RefinementCheckInterval) {
                runRefinementAndCoarseningIfScheduled();
                _stepCountSinceLastCheck = 0;
            }
        }
#endif
        return opt;
    }

#if DYNAMIC_STATE_ABSTRACTION_ENABLED
    void Model::recordTransition(const AbstractAgentPtr &agent, const StatePtr &targetState) {
        if (!agent || !targetState) return;
        StatePtr srcState = agent->getCurrentState();
        ActivityStateActionPtr act = agent->getCurrentAction();
        if (!srcState || !act || !act->isModelAct() || !act->requireTarget()) return;
        recordApeTransitionForAbstraction(srcState, targetState, act);
    }

    void Model::recordApeTransitionForAbstraction(const StatePtr &src, const StatePtr &tgt,
                                                  const ActivityStateActionPtr &act) {
        if (!src || !tgt || !act || _apeTransitionLog.empty()) {
            return;
        }
        const int minTargets = (_preference ? _preference->getApeNamingMinNonDetTargets()
                                            : MinNonDeterminismCount);
        auto itS = _ape_state_keys_by_hash.find(src->hash());
        auto itT = _ape_state_keys_by_hash.find(tgt->hash());
        if (itS == _ape_state_keys_by_hash.end() || itT == _ape_state_keys_by_hash.end()) {
            return;
        }
        ApeTransitionEntry e;
        e.sourceKeyHash = itS->second.hash();
        e.hasSourceStateKey = true;
        e.sourceStateKey = itS->second;
        e.actionHash = act->hash();
        e.targetKeyHash = itT->second.hash();
        {
            auto actPtr = src->getActivityString();
            e.sourceActivity = naming::StateKey::canonicalActivityString(
                (actPtr && actPtr.get()) ? *actPtr : "");
        }
        e.valid = true;
        BDLOG("ape naming: transition srcKey=%lu act=%lu tgtKey=%lu activity=%s",
              (unsigned long)e.sourceKeyHash, (unsigned long)e.actionHash, (unsigned long)e.targetKeyHash,
              e.sourceActivity.c_str());
        const ApePairKey pairKey{e.sourceKeyHash, e.actionHash};
        size_t prevPairTargetCount = 0;
        auto itPairBefore = _apePairAgg.find(pairKey);
        if (itPairBefore != _apePairAgg.end()) {
            prevPairTargetCount = itPairBefore->second.targetCounts.size();
        }
        ApeTransitionEntry &aSlot = _apeTransitionLog[_apeTransitionLogWriteIndex];
        if (aSlot.valid) {
            apePairAggRemove(aSlot);
        }
        aSlot = std::move(e);
        apePairAggAdd(aSlot);
        size_t nowPairTargetCount = 0;
        std::unordered_set<uintptr_t> pairTargetHashes;
        auto itPairAfter = _apePairAgg.find(pairKey);
        if (itPairAfter != _apePairAgg.end()) {
            nowPairTargetCount = itPairAfter->second.targetCounts.size();
            for (const auto &te : itPairAfter->second.targetCounts) {
                pairTargetHashes.insert(te.first);
            }
        }
        _apeTransitionLogWriteIndex = (_apeTransitionLogWriteIndex + 1) % _apeTransitionLog.size();

        // APE event-layer alignment: on NEW_ACTION_TARGET-like growth of a non-det pair,
        // attempt immediate pair-scoped refinement and then rollback check.
        const bool pairFanoutIncreased = nowPairTargetCount > prevPairTargetCount;
        const bool becameNonDet =
            prevPairTargetCount < static_cast<size_t>(minTargets) &&
            nowPairTargetCount >= static_cast<size_t>(minTargets);
        if (!pairFanoutIncreased && !becameNonDet) {
            return;
        }
        auto countNonDetPairsByActivity = [&](const std::string &canonicalActivity) -> int {
            int c = 0;
            for (const auto &kv : _apePairAgg) {
                if (kv.second.sourceActivity == canonicalActivity &&
                    kv.second.targetCounts.size() >= static_cast<size_t>(minTargets)) {
                    ++c;
                }
            }
            return c;
        };
        const int nonDetPairs = countNonDetPairsByActivity(aSlot.sourceActivity);
        if (nonDetPairs <= 0 || nowPairTargetCount < static_cast<size_t>(minTargets)) {
            return;
        }
        ApeRefinePair rp;
        rp.sourceKeyHash = pairKey.sourceKeyHash;
        rp.hasSourceStateKey = true;
        rp.sourceStateKey = itS->second;
        rp.actionHash = pairKey.actionHash;
        rp.targetKeyHashes = std::move(pairTargetHashes);
        rp.targetCount = nowPairTargetCount;
        BDLOG("ape naming: event refine-attempt activity=%s srcKey=%lu act=%lu targets=%zu nonDetPairs=%d",
              aSlot.sourceActivity.c_str(), (unsigned long)rp.sourceKeyHash,
              (unsigned long)rp.actionHash, rp.targetCount, nonDetPairs);
        const std::string actKey = aSlot.sourceActivity;
        if (refineActivityApeNaming(aSlot.sourceActivity, &rp, nonDetPairs)) {
            _apeEventRefineSuccessCount++;
            if (coarsenActivityApeNamingIfNeeded(aSlot.sourceActivity)) {
                _apeEventCoarsenRollbackCount++;
            }
            notifyAgentsOfApeNamingChange();
        } else if (rp.actionHash != 0 && nowPairTargetCount >= static_cast<size_t>(kApeNDActionBlacklistMinOutEdges)) {
            _apeRefineActionBlacklist[actKey].insert(rp.actionHash);
            apeCapApeNamingCoarsenAndRefineBlacklists();
            BLOG("ape naming: NDActionBlacklist add (APE: out>=%d after failed resolve) activity=%s act=%lu targets=%zu",
                 kApeNDActionBlacklistMinOutEdges, aSlot.sourceActivity.c_str(),
                 (unsigned long)rp.actionHash, nowPairTargetCount);
        }
    }

    void Model::apePairAggRemove(const ApeTransitionEntry &e) {
        if (!e.valid || e.sourceKeyHash == e.targetKeyHash) {
            return;
        }
        ApePairKey pk{e.sourceKeyHash, e.actionHash};
        auto it = _apePairAgg.find(pk);
        if (it == _apePairAgg.end()) {
            return;
        }
        auto &tm = it->second.targetCounts;
        auto itT = tm.find(e.targetKeyHash);
        if (itT == tm.end()) {
            return;
        }
        if (--(itT->second) <= 0) {
            tm.erase(itT);
        }
        if (tm.empty()) {
            _apePairAgg.erase(it);
        }
    }

    void Model::apePairAggAdd(const ApeTransitionEntry &e) {
        if (!e.valid || e.sourceKeyHash == e.targetKeyHash) {
            return;
        }
        ApePairKey pk{e.sourceKeyHash, e.actionHash};
        auto &slot = _apePairAgg[pk];
        slot.targetCounts[e.targetKeyHash]++;
        slot.sourceActivity = e.sourceActivity;
        if (e.hasSourceStateKey) {
            slot.hasSourceStateKey = true;
            slot.sourceStateKey = e.sourceStateKey;
        }
    }

    void Model::apeClearTransitionAggregationForActivity(const std::string &actKeyCanonical) {
        for (auto &slot : _apeTransitionLog) {
            if (slot.valid && slot.sourceActivity == actKeyCanonical) {
                apePairAggRemove(slot);
                slot.valid = false;
            }
        }
        for (auto it = _apePairAgg.begin(); it != _apePairAgg.end();) {
            if (it->second.sourceActivity == actKeyCanonical) {
                it = _apePairAgg.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Model::notifyAgentsOfApeNamingChange() {
        for (const auto &kv : _deviceIDAgentMap) {
            if (kv.second) {
                kv.second->onStateAbstractionChanged();
            }
        }
    }

    bool Model::evalApeGuiTreeNamingBlacklist(const std::vector<uintptr_t> &stateHashes,
                                               const naming::NamingPtr &naming) const {
        if (!naming || stateHashes.empty()) {
            return true;
        }
        const std::string &fp = naming->fingerprintString();
        for (uintptr_t sh : stateHashes) {
            auto it = _apeGuiTreeNamingBlacklist.find(sh);
            if (it != _apeGuiTreeNamingBlacklist.end() && it->second.count(fp) != 0) {
                return false;
            }
        }
        return true;
    }

    void Model::apeBlacklistFinerNamingOnRollback(
        const std::string &activity, const naming::NamingPtr &finerNaming,
        const ApeNamingAbstractionContext &ctx, const std::unordered_set<uintptr_t> &affectedStateHashesForBlacklist) {
        (void)ctx;
        if (!finerNaming || affectedStateHashesForBlacklist.empty()) {
            return;
        }
        const std::string fp = finerNaming->fingerprintString();
        // Java blacklistRefinement blacklists exactly the affected GUI trees.
        // Native uses state-hash keyed cache for GUI-tree naming blacklists, so we blacklist
        // by those affected state hashes directly.
        for (uintptr_t sh : affectedStateHashesForBlacklist) {
            _apeGuiTreeNamingBlacklist[sh].insert(fp);
        }
        apeCapGuiTreeNamingBlacklist();
        apeCapApeNamingCoarsenAndRefineBlacklists();
    }

    void Model::apeCapApeNamingCoarsenAndRefineBlacklists() {
        // no-op: match Java unbounded blacklists (NDActionBlacklist/guiTreeNamingBlaclist).
    }

    std::vector<std::string> Model::detectNonDeterminismApe() const {
        const int minTargets = (_preference ? _preference->getApeNamingMinNonDetTargets()
                                            : MinNonDeterminismCount);
        std::set<std::string> activitiesSet;
        for (const auto &kv : _apePairAgg) {
            if (kv.second.targetCounts.size() >= static_cast<size_t>(minTargets)) {
                activitiesSet.insert(kv.second.sourceActivity);
            }
        }
        return std::vector<std::string>(activitiesSet.begin(), activitiesSet.end());
    }

    bool Model::refineActivityApeNaming(const std::string &activity) {
        return refineActivityApeNaming(activity, nullptr, -1);
    }

    bool Model::refineActivityApeNaming(const std::string &activity, const ApeRefinePair *pair,
                                        int precomputedActivityNonDetPairCount) {
        const std::string actKey = naming::StateKey::canonicalActivityString(activity);
        const int minTargets = (_preference ? _preference->getApeNamingMinNonDetTargets()
                                            : MinNonDeterminismCount);
        int nonDetPairs = 0;
        size_t dominantPairTargets = 0;
        uintptr_t dominantSourceKeyHash = 0;
        uintptr_t dominantActionHash = 0;
        std::unordered_set<uintptr_t> dominantTargetKeyHashes;
        const bool useBatchNonDet =
            precomputedActivityNonDetPairCount >= 0 && pair && pair->sourceKeyHash != 0 &&
            pair->actionHash != 0 && pair->targetCount >= static_cast<size_t>(minTargets);
        if (useBatchNonDet) {
            nonDetPairs = precomputedActivityNonDetPairCount;
            dominantPairTargets = pair->targetCount;
            dominantSourceKeyHash = pair->sourceKeyHash;
            dominantActionHash = pair->actionHash;
            dominantTargetKeyHashes = pair->targetKeyHashes;
        } else {
            for (const auto &kv : _apePairAgg) {
                if (kv.second.sourceActivity != actKey) {
                    continue;
                }
                const auto &tm = kv.second.targetCounts;
                if (tm.size() < static_cast<size_t>(minTargets)) {
                    continue;
                }
                nonDetPairs++;
                if (tm.size() > dominantPairTargets) {
                    dominantPairTargets = tm.size();
                    dominantSourceKeyHash = kv.first.sourceKeyHash;
                    dominantActionHash = kv.first.actionHash;
                    dominantTargetKeyHashes.clear();
                    for (const auto &te : tm) {
                        dominantTargetKeyHashes.insert(te.first);
                    }
                }
            }
            // Batch (`runApeNamingAbstractionBatch`) passes the exact non-det pair for this attempt; use it as
            // the trigger for blacklist/admissibility and `ctx.trigger*` so logs and behavior match `refine-attempt`.
            // With `pair == nullptr` (one-arg API), keep scan-only dominant = max target fan-out for this activity.
            if (pair && pair->sourceKeyHash != 0 && pair->actionHash != 0 &&
                pair->targetCount >= static_cast<size_t>(minTargets)) {
                dominantPairTargets = pair->targetCount;
                dominantSourceKeyHash = pair->sourceKeyHash;
                dominantActionHash = pair->actionHash;
                dominantTargetKeyHashes = pair->targetKeyHashes;
            }
        }
        if (dominantSourceKeyHash != 0 || dominantActionHash != 0) {
            ApePairKey pairKey{dominantSourceKeyHash, dominantActionHash};
            auto itBlk = _apeRefinePairBlacklist.find(actKey);
            if (itBlk != _apeRefinePairBlacklist.end() && itBlk->second.count(pairKey) != 0) {
                BDLOG("ape naming: skip refine activity=%s reason=trigger pair blacklisted srcKey=%lu act=%lu",
                      activity.c_str(), (unsigned long)dominantSourceKeyHash, (unsigned long)dominantActionHash);
                return false;
            }
            auto itActionBlk = _apeRefineActionBlacklist.find(actKey);
            if (itActionBlk != _apeRefineActionBlacklist.end() &&
                itActionBlk->second.count(dominantActionHash) != 0) {
                BDLOG("ape naming: skip refine activity=%s reason=trigger action blacklisted act=%lu",
                      activity.c_str(), (unsigned long)dominantActionHash);
                return false;
            }
        }
        const int minNonDetPairs = (_preference ? _preference->getApeNamingActionRefineMinNonDetPairs() : 1);
        if (nonDetPairs < minNonDetPairs) {
            BDLOG("ape naming: skip refine activity=%s reason=nonDetPairs<%d (%d)",
                  activity.c_str(), minNonDetPairs, nonDetPairs);
            return false;
        }
        const int minNonDetPairDelta =
            (_preference ? _preference->getApeNamingActionRefineMinNonDetPairDelta() : 0);
        auto itCtx = _apeNamingContext.find(actKey);
        if (itCtx != _apeNamingContext.end()) {
            const int lastPairs = itCtx->second.nonDetPairsAtLastNamingRefinement;
            if (nonDetPairs < lastPairs + minNonDetPairDelta) {
                BDLOG("ape naming: skip refine activity=%s reason=nonDetPairDelta<%d (now=%d,last=%d)",
                      activity.c_str(), minNonDetPairDelta, nonDetPairs, lastPairs);
                return false;
            }
        }
        const size_t activityStateCount = getGraph()->getStateCountByActivity(activity);
        const int minStates = (_preference ? _preference->getApeNamingActionRefineMinActivityStates() : 2);
        if (activityStateCount < static_cast<size_t>(minStates)) {
            BDLOG("ape naming: skip refine activity=%s reason=stateCount<%d (%zu)",
                  activity.c_str(), minStates, activityStateCount);
            return false;
        }
        const int minStateDelta = (_preference ? _preference->getApeNamingActionRefineMinStateDelta() : 1);
        if (itCtx != _apeNamingContext.end()) {
            const size_t lastCount = itCtx->second.stateCountAtLastNamingRefinement;
            if (activityStateCount < lastCount + static_cast<size_t>(minStateDelta)) {
                BDLOG("ape naming: skip refine activity=%s reason=stateDelta<%d (now=%zu,last=%zu)",
                      activity.c_str(), minStateDelta, activityStateCount, lastCount);
                return false;
            }
        }
        naming::ActivityNamingManager &mgr = _apeStateNamingManager->activityManager();
        naming::NamingPtr cur = mgr.getNaming(actKey);
        if (!cur) {
            cur = naming::NamingFactory::defaultRootNaming();
            if (!cur) {
                return false;
            }
            _apeStateNamingManager->updateNaming(actKey, naming::NamingUpdateKind::Refine, cur);
        }
        naming::NamerLattice lat(naming::NamerFactory::CURRENT);
        std::set<std::string> blk;
        for (const auto &p : _apeNamingCoarseningBlacklist) {
            if (p.first == actKey) {
                blk.insert(p.second);
            }
        }
        naming::NamingFactory::ActionRefinementOptions userOpts;
        userOpts.max_steps = (_preference ? _preference->getApeNamingActionRefineHops() : 8);
        userOpts.blacklist = &blk;
        const std::string predicateMode =
            (_preference ? _preference->getApeNamingActionRefinePredicateMode() : "fingerprint_change");
        const std::string selectionMode =
            (_preference ? _preference->getApeNamingActionRefineSelectionMode() : "first_accept");
        const std::string ruleProfile =
            (_preference ? _preference->getApeNamingActionRefineRuleProfile() : "baseline");
        userOpts.choose_deepest_acceptable = (selectionMode == "deepest_accept");
        if (ruleProfile == "java_rule_01_preview") {
            userOpts.choose_deepest_acceptable = true;
            const std::string curFp = cur->fingerprintString();
            const int curFineness = cur->getFineness();
            userOpts.accept_predicate = [curFp, curFineness](const naming::NamingPtr &candidate) {
                return candidate && candidate->fingerprintString() != curFp &&
                       candidate->getFineness() > curFineness;
            };
        } else if (ruleProfile == "java_rule_03_preview") {
            userOpts.choose_deepest_acceptable = true;
            userOpts.evaluate_all_immediate_candidates = true;
            const std::string curFp = cur->fingerprintString();
            userOpts.accept_predicate = [curFp](const naming::NamingPtr &candidate) {
                return candidate && candidate->fingerprintString() != curFp;
            };
        } else if (ruleProfile == "strict_baseline") {
            const std::string curFp = cur->fingerprintString();
            const int curFineness = cur->getFineness();
            userOpts.accept_predicate = [curFp, curFineness](const naming::NamingPtr &candidate) {
                return candidate && candidate->fingerprintString() != curFp &&
                       candidate->getFineness() > curFineness;
            };
        } else if (predicateMode == "fingerprint_change") {
            const std::string curFp = cur->fingerprintString();
            userOpts.accept_predicate = [curFp](const naming::NamingPtr &candidate) {
                return candidate && candidate->fingerprintString() != curFp;
            };
        } else if (predicateMode == "fineness_increase") {
            const int curFineness = cur->getFineness();
            userOpts.accept_predicate = [curFineness](const naming::NamingPtr &candidate) {
                return candidate && candidate->getFineness() > curFineness;
            };
        } else {
            userOpts.accept_predicate = {};
        }
        // Java NamingFactory.refine: actionRefinementFirst tries action-style resolve before state-style (or vice versa).
        naming::NamingFactory::ActionRefinementOptions strictBranchOpts;
        strictBranchOpts.max_steps = userOpts.max_steps;
        strictBranchOpts.blacklist = &blk;
        strictBranchOpts.choose_deepest_acceptable = true;
        strictBranchOpts.evaluate_all_immediate_candidates = true;
        {
            const std::string curFpS = cur->fingerprintString();
            const int curFinenessS = cur->getFineness();
            strictBranchOpts.accept_predicate = [curFpS, curFinenessS](const naming::NamingPtr &candidate) {
                return candidate && candidate->fingerprintString() != curFpS &&
                       candidate->getFineness() > curFinenessS;
            };
        }
        const bool actionRefinementFirst =
            !_preference || _preference->useApeNamingActionRefinementFirst();
        std::vector<naming::NamingPtr> candidates;
        if (actionRefinementFirst) {
            candidates =
                naming::NamingFactory::actionRefinementCandidatesWithOptions(cur, lat, strictBranchOpts);
            if (candidates.empty()) {
                candidates =
                    naming::NamingFactory::actionRefinementCandidatesWithOptions(cur, lat, userOpts);
            }
        } else {
            candidates =
                naming::NamingFactory::actionRefinementCandidatesWithOptions(cur, lat, userOpts);
            if (candidates.empty()) {
                candidates =
                    naming::NamingFactory::actionRefinementCandidatesWithOptions(cur, lat, strictBranchOpts);
            }
        }
        if (candidates.empty()) {
            BDLOG("ape naming: skip refine activity=%s reason=no non-blacklisted finer namer", activity.c_str());
            return false;
        }

        struct CandidateEval {
            naming::NamingPtr naming;
            int score{0};
            bool strictFiner{false};
            bool fingerprintChanged{false};
            int finenessGain{0};
            bool replayUsed{false};
            int replayDistinctTargets{0};
            int replaySourceChanged{0};
            /// APE RefinementResult comparator: |states1|+|states2| from checkStateRefinement (replay only); -1 = n/a.
            int apePartitionStateCost{-1};
        };
        std::vector<uintptr_t> guiTreeBlacklistCheckHashes;
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML && DYNAMIC_STATE_ABSTRACTION_ENABLED
        std::vector<uintptr_t> triggerSourceStateHashesForReplay;
        if (dominantSourceKeyHash != 0) {
            for (const auto &sp : getGraph()->getStates()) {
                if (!sp) {
                    continue;
                }
                auto ap = sp->getActivityString();
                const std::string a =
                    (ap && ap.get()) ? naming::StateKey::canonicalActivityString(*ap) : std::string();
                if (a != actKey) {
                    continue;
                }
                naming::StateKey k = naming::StateKey::fromFallbackXmlStringHash("", 0);
                if (tryGetApeStateKey(sp->hash(), &k) && k.hash() == dominantSourceKeyHash) {
                    triggerSourceStateHashesForReplay.push_back(sp->hash());
                }
            }
        }
        guiTreeBlacklistCheckHashes = triggerSourceStateHashesForReplay;
        bool replayActive = false;
        std::string replaySrcXml;
        std::vector<std::string> replayTgtXmls;
        if (_preference && _preference->useApeNamingCandidateTransitionReplay() &&
            dominantPairTargets >= static_cast<size_t>(minTargets) && dominantSourceKeyHash != 0 &&
            !dominantTargetKeyHashes.empty()) {
            auto findStateHashForApeKey = [&](uintptr_t keyH) -> uintptr_t {
                for (const auto &sp : getGraph()->getStates()) {
                    if (!sp) {
                        continue;
                    }
                    auto ap = sp->getActivityString();
                    const std::string a =
                        (ap && ap.get()) ? naming::StateKey::canonicalActivityString(*ap) : std::string();
                    if (a != actKey) {
                        continue;
                    }
                    naming::StateKey k = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (tryGetApeStateKey(sp->hash(), &k) && k.hash() == keyH) {
                        return sp->hash();
                    }
                }
                return static_cast<uintptr_t>(0);
            };
            const uintptr_t srcSh = findStateHashForApeKey(dominantSourceKeyHash);
            if (srcSh != 0) {
                auto itS = _apeStateXmlByStateHash.find(srcSh);
                if (itS != _apeStateXmlByStateHash.end() && !itS->second.empty()) {
                    replaySrcXml = itS->second;
                }
            }
            replayTgtXmls.reserve(dominantTargetKeyHashes.size());
            for (uintptr_t th : dominantTargetKeyHashes) {
                const uintptr_t tsh = findStateHashForApeKey(th);
                if (tsh == 0) {
                    continue;
                }
                auto itT = _apeStateXmlByStateHash.find(tsh);
                if (itT != _apeStateXmlByStateHash.end() && !itT->second.empty()) {
                    replayTgtXmls.push_back(itT->second);
                }
            }
            replayActive =
                !replaySrcXml.empty() && replayTgtXmls.size() == dominantTargetKeyHashes.size() &&
                replayTgtXmls.size() >= static_cast<size_t>(minTargets);
            if (!replayActive && dominantPairTargets >= static_cast<size_t>(minTargets)) {
                BDLOG("ape naming: replay skipped activity=%s haveSrcXml=%d tgtXml=%zu needTgt=%zu",
                      activity.c_str(), replaySrcXml.empty() ? 0 : 1, replayTgtXmls.size(),
                      dominantTargetKeyHashes.size());
            }
        }
#else
        const bool replayActive = false;
#endif
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        {
#if !defined(FASTBOT_HAS_PUGIXML) || !FASTBOT_HAS_PUGIXML
            if (guiTreeBlacklistCheckHashes.empty() && dominantSourceKeyHash != 0) {
                size_t srcReprAdded = 0;
                for (const auto &sp : getGraph()->getStates()) {
                    if (!sp) {
                        continue;
                    }
                    auto ap = sp->getActivityString();
                    const std::string a =
                        (ap && ap.get()) ? naming::StateKey::canonicalActivityString(*ap) : std::string();
                    if (a != actKey) {
                        continue;
                    }
                    naming::StateKey k = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (tryGetApeStateKey(sp->hash(), &k) && k.hash() == dominantSourceKeyHash) {
                        guiTreeBlacklistCheckHashes.push_back(sp->hash());
                        ++srcReprAdded;
                    }
                }
            }
#endif
            std::unordered_set<uintptr_t> seenGtb(guiTreeBlacklistCheckHashes.begin(),
                                                  guiTreeBlacklistCheckHashes.end());
            for (uintptr_t tkh : dominantTargetKeyHashes) {
                for (const auto &sp : getGraph()->getStates()) {
                    if (!sp) {
                        continue;
                    }
                    auto ap = sp->getActivityString();
                    const std::string a2 =
                        (ap && ap.get()) ? naming::StateKey::canonicalActivityString(*ap) : std::string();
                    if (a2 != actKey) {
                        continue;
                    }
                    naming::StateKey k2 = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (tryGetApeStateKey(sp->hash(), &k2) && k2.hash() == tkh) {
                        const uintptr_t sh = sp->hash();
                        if (seenGtb.insert(sh).second) {
                            guiTreeBlacklistCheckHashes.push_back(sh);
                        }
                    }
                }
            }
        }
#endif
        std::vector<CandidateEval> accepted;
        accepted.reserve(candidates.size());
        const std::string curFp = cur->fingerprintString();
        const int curFine = cur->getFineness();
        for (const auto &cand : candidates) {
            if (!cand) {
                continue;
            }
            CandidateEval e;
            e.naming = cand;
            e.finenessGain = cand->getFineness() - curFine;
            e.strictFiner = e.finenessGain > 0;
            e.fingerprintChanged = cand->fingerprintString() != curFp;

            // Transition-level admissibility baseline:
            // on concrete non-deterministic (sourceKey, action) pairs, require strict refinement.
            if (dominantPairTargets >= static_cast<size_t>(minTargets) &&
                (!e.strictFiner || !e.fingerprintChanged)) {
                continue;
            }
            // Java resolve/filter spirit:
            // rank candidates by stronger separation power while keeping deterministic tie-breaks.
            if (e.strictFiner) {
                e.score += 100;
            }
            if (e.fingerprintChanged) {
                e.score += 80;
            }
            if (e.finenessGain > 0) {
                e.score += std::min(5, e.finenessGain) * 10;
            }
            // In non-det scenario prefer deeper split candidate.
            if (dominantPairTargets >= static_cast<size_t>(minTargets)) {
                e.score += std::min<int>(10, static_cast<int>(dominantPairTargets)) * 3;
            }
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML && DYNAMIC_STATE_ABSTRACTION_ENABLED
            if (replayActive) {
                e.replayUsed = true;
                naming::StateKey srcKeyCand = naming::StateKey::fromFallbackXmlStringHash("", 0);
                naming::StateKey srcKeyCur = naming::StateKey::fromFallbackXmlStringHash("", 0);
                if (apeStateKeyFromXmlWithNaming(activity, replaySrcXml, cand, &srcKeyCand) &&
                    apeStateKeyFromXmlWithNaming(activity, replaySrcXml, cur, &srcKeyCur)) {
                    e.replaySourceChanged = (srcKeyCand.hash() != srcKeyCur.hash()) ? 1 : 0;
                }
                std::unordered_set<uintptr_t> uniqTgt;
                for (const auto &tx : replayTgtXmls) {
                    naming::StateKey tk = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (apeStateKeyFromXmlWithNaming(activity, tx, cand, &tk)) {
                        uniqTgt.insert(tk.hash());
                    }
                }
                e.replayDistinctTargets = static_cast<int>(uniqTgt.size());
                e.score += e.replayDistinctTargets * 500;
                e.score += e.replaySourceChanged * 150;

                // APE checkStateRefinement partition sizes: |states1| (source transition trees) + |states2| (targets).
                std::unordered_set<uintptr_t> srcKeysUnderCand;
                for (uintptr_t sh : triggerSourceStateHashesForReplay) {
                    auto itXml = _apeStateXmlByStateHash.find(sh);
                    if (itXml == _apeStateXmlByStateHash.end() || itXml->second.empty()) {
                        continue;
                    }
                    naming::StateKey sk = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (apeStateKeyFromXmlWithNaming(activity, itXml->second, cand, &sk)) {
                        srcKeysUnderCand.insert(sk.hash());
                    }
                }
                if (srcKeysUnderCand.empty() && !replaySrcXml.empty()) {
                    naming::StateKey sk = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (apeStateKeyFromXmlWithNaming(activity, replaySrcXml, cand, &sk)) {
                        srcKeysUnderCand.insert(sk.hash());
                    }
                }
                bool tgtIntersectsSrc = false;
                for (uintptr_t th : uniqTgt) {
                    if (srcKeysUnderCand.count(th) != 0) {
                        tgtIntersectsSrc = true;
                        break;
                    }
                }
                if (tgtIntersectsSrc) {
                    continue;
                }
                e.apePartitionStateCost =
                    static_cast<int>(srcKeysUnderCand.size() + uniqTgt.size());
            }
#endif
            // Match java.util.Predicate.Type order: STATE_ABSTRACTION before STATE_REFINEMENT.
            if (!evalApeGuiTreeNamingBlacklist(guiTreeBlacklistCheckHashes, cand) ||
                !evalApeStatesFewerThanPredicates(activity, cand) ||
                !evalApeSourcePartitionPredicates(activity, cand) ||
                !evalApeActionPartitionPredicates(activity, cand)) {
                continue;
            }
            accepted.push_back(std::move(e));
        }
        if (accepted.empty()) {
            BDLOG("ape naming: skip refine activity=%s reason=all candidates filtered "
                  "candidateCount=%zu dominantPairTargets=%zu",
                  activity.c_str(), candidates.size(), dominantPairTargets);
            return false;
        }
        std::sort(accepted.begin(), accepted.end(), [](const CandidateEval &a, const CandidateEval &b) {
            if (a.replayUsed && b.replayUsed) {
                if (a.replayDistinctTargets != b.replayDistinctTargets) {
                    return a.replayDistinctTargets > b.replayDistinctTargets;
                }
                if (a.replaySourceChanged != b.replaySourceChanged) {
                    return a.replaySourceChanged > b.replaySourceChanged;
                }
                if (a.apePartitionStateCost >= 0 && b.apePartitionStateCost >= 0 &&
                    a.apePartitionStateCost != b.apePartitionStateCost) {
                    return a.apePartitionStateCost < b.apePartitionStateCost;
                }
            }
            if (a.score != b.score) return a.score > b.score;
            // APE filterRefinementResult: prefer smaller |states1|+|states2| when replay did not supply cost.
            if (a.apePartitionStateCost >= 0 && b.apePartitionStateCost >= 0 &&
                a.apePartitionStateCost != b.apePartitionStateCost) {
                return a.apePartitionStateCost < b.apePartitionStateCost;
            }
            if (a.finenessGain != b.finenessGain) return a.finenessGain < b.finenessGain;
            const int lex = compareNamingLexicographicForApeFilter(a.naming, b.naming);
            if (lex != 0) return lex < 0;
            return a.naming->fingerprintString() < b.naming->fingerprintString();
        });
        naming::NamingPtr next = accepted.front().naming;
        BLOG("ape naming: refine-candidates activity=%s total=%zu accepted=%zu bestScore=%d bestFineGain=%d "
             "replay=%s distinctTgt=%d srcChanged=%d partitionCost=%d",
             activity.c_str(), candidates.size(), accepted.size(), accepted.front().score,
             accepted.front().finenessGain, accepted.front().replayUsed ? "yes" : "no",
             accepted.front().replayDistinctTargets, accepted.front().replaySourceChanged,
             accepted.front().apePartitionStateCost);
        ApeNamingAbstractionContext &ctx = _apeNamingContext[actKey];
        ctx.previousNamingBeforeRefine = cur;
        ctx.previousNamingFingerprintBeforeRefine = cur->fingerprintString();
        ctx.oldKeyHashToNewKeyHashes.clear();
        ctx.oldKeyHashToObservationCount.clear();
        ctx.stateCountAtLastNamingRefinement = getGraph()->getStateCountByActivity(activity);
        ctx.nonDetPairsAtLastNamingRefinement = nonDetPairs;
        ctx.triggerSourceKeyHash = dominantSourceKeyHash;
        ctx.triggerSourceKeyExact = (pair && pair->hasSourceStateKey);
        if (ctx.triggerSourceKeyExact) {
            ctx.triggerSourceKey = pair->sourceStateKey;
        } else {
            ctx.triggerSourceKey = naming::StateKey::fromFallbackXmlStringHash("", 0);
        }
        ctx.triggerActionHash = dominantActionHash;
        ctx.triggerTargetCountAtRefine = dominantPairTargets;
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
        {
            std::vector<std::vector<uintptr_t>> predParts;
            std::vector<uintptr_t> partA;
            constexpr size_t kMaxSourcePartitionRepr = 2;
            constexpr size_t kMaxTargetPartitionParts = 4; // total target partitions (each by one tkh)
            constexpr size_t kMaxTargetPartitionReprPerKey = 2;
            if (dominantSourceKeyHash != 0) {
                for (const auto &sp : getGraph()->getStates()) {
                    if (!sp) {
                        continue;
                    }
                    auto ap = sp->getActivityString();
                    const std::string a =
                        (ap && ap.get()) ? naming::StateKey::canonicalActivityString(*ap) : std::string();
                    if (a != actKey) {
                        continue;
                    }
                    naming::StateKey k = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (tryGetApeStateKey(sp->hash(), &k) && k.hash() == dominantSourceKeyHash) {
                        partA.push_back(sp->hash());
                        if (partA.size() >= kMaxSourcePartitionRepr) {
                            break;
                        }
                    }
                }
            }
            if (partA.size() >= 1) {
                predParts.push_back(std::move(partA));
            }

            size_t targetPartitionsAdded = 0;
            for (uintptr_t tkh : dominantTargetKeyHashes) {
                if (targetPartitionsAdded >= kMaxTargetPartitionParts) {
                    break;
                }
                std::vector<uintptr_t> partT;
                for (const auto &sp : getGraph()->getStates()) {
                    if (!sp) {
                        continue;
                    }
                    auto ap = sp->getActivityString();
                    const std::string a2 =
                        (ap && ap.get()) ? naming::StateKey::canonicalActivityString(*ap) : std::string();
                    if (a2 != actKey) {
                        continue;
                    }
                    naming::StateKey k2 = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (tryGetApeStateKey(sp->hash(), &k2) && k2.hash() == tkh) {
                        partT.push_back(sp->hash());
                        if (partT.size() >= kMaxTargetPartitionReprPerKey) {
                            break;
                        }
                    }
                    if (partT.size() >= kMaxTargetPartitionReprPerKey) {
                        break;
                    }
                }
                if (!partT.empty()) {
                    predParts.push_back(std::move(partT));
                    ++targetPartitionsAdded;
                }
            }

            if (predParts.size() >= 2) {
                pushApeSourcePartitionPredicate(activity, next, std::move(predParts));
            }
            if (dominantActionHash != 0 && triggerSourceStateHashesForReplay.size() >= 1) {
                // Java AssertActionDivergent2 partitions "resolved nodes" by the Name generated from @next.
                // Native does not store ModelAction.resolvedNodes directly, so we approximate them by:
                // 1) find the action target widget (matching dominantActionHash) for each source state;
                // 2) in the rebuilt GUI tree, collect all nodes whose bounds match that widget bounds;
                // 3) group collected nodes by their node name under @next.
                //
                // This is a closer analog to Java GUITree.pickNodes(action) than using one node position
                // per source state.
                std::unordered_map<std::string, std::vector<std::pair<uintptr_t, size_t>>> partsByName;
                // Java AssertActionDivergent2 is built from a single GUI tree's action.getResolvedNodes().
                // For full semantic alignment, only use one representative source tree in this predicate.
                constexpr size_t kMaxSrcForActionPred = 1;
                // No resolved-nodes truncation: keep all nodes matching the action target Name.
                constexpr size_t kMaxResolvedNodesPerSource = 4096;

                // dominantActionHash in native may include source-state hash, so matching it verbatim
                // across other source states could under-collect resolved nodes.
                // Extract a more stable identity (action type + target widget hash) from the first
                // state where the full hash matches, then match by that identity for other states.
                ActionType dominantActionType = ActionType::NOP;
                uintptr_t dominantTargetWidgetHash = 0;
                bool hasDominantActionIdentity = false;
                for (uintptr_t repSh : triggerSourceStateHashesForReplay) {
                    StatePtr repSp;
                    for (const auto &s : getGraph()->getStates()) {
                        if (s && s->hash() == repSh) {
                            repSp = s;
                            break;
                        }
                    }
                    if (!repSp) {
                        continue;
                    }
                    for (const auto &a : repSp->getActions()) {
                        auto asa = std::dynamic_pointer_cast<ActivityStateAction>(a);
                        if (!asa || asa->hash() != dominantActionHash) {
                            continue;
                        }
                        dominantActionType = asa->getActionType();
                        if (asa->getTarget()) {
                            dominantTargetWidgetHash = asa->getTarget()->hash();
                            hasDominantActionIdentity = true;
                        }
                        break;
                    }
                    if (hasDominantActionIdentity) {
                        break;
                    }
                }

                size_t actionPredAdded = 0;
                for (uintptr_t sh : triggerSourceStateHashesForReplay) {
                    if (actionPredAdded >= kMaxSrcForActionPred) {
                        break;
                    }
                    // Locate the source state.
                    StatePtr sp;
                    for (const auto &s : getGraph()->getStates()) {
                        if (s && s->hash() == sh) {
                            sp = s;
                            break;
                        }
                    }
                    if (!sp) {
                        continue;
                    }

                    // Locate the specific action target widget for @dominantActionHash.
                    WidgetPtr targetWidget;
                    for (const auto &a : sp->getActions()) {
                        auto asa = std::dynamic_pointer_cast<ActivityStateAction>(a);
                        if (!asa) {
                            continue;
                        }
                        bool match = false;
                        if (hasDominantActionIdentity && asa->getTarget()) {
                            match = (asa->getActionType() == dominantActionType &&
                                     asa->getTarget()->hash() == dominantTargetWidgetHash);
                        } else {
                            match = (asa->hash() == dominantActionHash);
                        }
                        if (!match) {
                            continue;
                        }
                        targetWidget = asa->getTarget();
                        break;
                    }
                    if (!targetWidget || !targetWidget->getBounds()) {
                        continue;
                    }
                    const Rect targetRect = *targetWidget->getBounds();

                    auto itXml = _apeStateXmlByStateHash.find(sh);
                    if (itXml == _apeStateXmlByStateHash.end() || itXml->second.empty()) {
                        continue;
                    }

                    std::string pkg;
                    std::string cls;
                    naming::StateKey::splitActivityPackageClass(activity, &pkg, &cls);
                    gui_tree::GUITreeBuildResult built =
                        gui_tree::GUITreeFactory::buildFromXml(itXml->second, pkg, cls);
                    if (!built.tree || !built.dom) {
                        continue;
                    }
                    std::vector<const gui_tree::GUITreeNode *> po;
                    collectGUITreeNodesPreOrder(built.tree->getRootNode(), &po);
                    if (po.empty()) {
                        continue;
                    }

                    // Java's resolvedNodes are selected by action target Name computed before refinement
                    // (i.e., under @cur). To align more closely, rebuild once under @cur to derive a stable
                    // targetNameCurXPath, pick resolved node indices by (bounds match AND targetNameCur),
                    // then rebuild under @next and group those same indices by their new Name.
                    if (!naming::NamingFactory::rebuildTree(cur, *built.tree, built.dom)) {
                        continue;
                    }

                    std::string targetNameCurXPath;
                    // More strict: derive the action target Name under @cur from the specific targetWidget
                    // (when widget<->preorder index mapping is aligned). Fallback to bounds-match.
                    {
                        const WidgetPtrVec &ws = sp->getWidgets();
                        if (ws.size() == po.size()) {
                            for (size_t i = 0; i < ws.size(); ++i) {
                                if (ws[i] == targetWidget) {
                                    if (i < po.size() && po[i]) {
                                        const naming::NamePtr nm = po[i]->getXPathName();
                                        if (nm) {
                                            targetNameCurXPath = nm->toXPath();
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                        // Fallback: derive the target Name under @cur from a node whose bounds match.
                        // This still yields the correct Name for resolved-nodes selection (we only use Name
                        // equality afterwards, not bounds).
                        if (targetNameCurXPath.empty()) {
                            for (size_t i = 0; i < po.size(); ++i) {
                                const auto *node = po[i];
                                if (!node) continue;
                                if (!(node->getBounds() == targetRect)) continue;
                                const naming::NamePtr nm = node->getXPathName();
                                if (!nm) continue;
                                targetNameCurXPath = nm->toXPath();
                                if (!targetNameCurXPath.empty()) {
                                    break;
                                }
                            }
                        }
                    }
                    // Strict mode: if we cannot derive the target Name under @cur,
                    // skip this source state for action partitions.
                    if (targetNameCurXPath.empty()) {
                        continue;
                    }

                    std::vector<size_t> resolvedIdx;
                    resolvedIdx.reserve(kMaxResolvedNodesPerSource);
                    for (size_t i = 0; i < po.size(); ++i) {
                        const auto *node = po[i];
                        if (!node) continue;
                        const naming::NamePtr nm = node->getXPathName();
                        if (!nm || nm->toXPath() != targetNameCurXPath) {
                            continue;
                        }
                        // Java includes all nodes with the same Name; no extra bounds constraint.
                        resolvedIdx.push_back(i);
                    }

                    if (resolvedIdx.empty()) {
                        continue;
                    }

                    // Rebuild with @next for grouping into partitions.
                    if (!naming::NamingFactory::rebuildTree(next, *built.tree, built.dom)) {
                        continue;
                    }

                    size_t insertedAny = 0;
                    for (size_t idx : resolvedIdx) {
                        if (idx >= po.size() || !po[idx]) {
                            continue;
                        }
                        const naming::NamePtr nm = po[idx]->getXPathName();
                        if (!nm) {
                            continue;
                        }
                        const std::string nameXPath = nm->toXPath();
                        auto &vec = partsByName[nameXPath];
                        vec.push_back({sh, idx});
                        insertedAny++;
                    }
                    if (insertedAny > 0) {
                        ++actionPredAdded;
                    }
                }

                if (partsByName.size() >= 2) {
                    std::vector<std::vector<std::pair<uintptr_t, size_t>>> actionPredParts;
                    actionPredParts.reserve(partsByName.size());
                    for (auto &kv : partsByName) {
                        if (!kv.second.empty()) {
                            actionPredParts.push_back(std::move(kv.second));
                        }
                    }
                    if (actionPredParts.size() >= 2) {
                        pushApeActionPartitionPredicate(activity, next, std::move(actionPredParts));
                    }
                }
            }
        }
#endif
        ctx.triggerTargetKeyHashes = std::move(dominantTargetKeyHashes);
        if (ctx.triggerSourceKeyExact) {
            _apeStateNamingManager->updateNamingWithStateKey(
                actKey, naming::NamingUpdateKind::Refine, cur, next, ctx.triggerSourceKey);
        } else {
            _apeStateNamingManager->updateNamingWithStateHash(
                actKey, naming::NamingUpdateKind::Refine, cur, next, ctx.triggerSourceKeyHash);
        }
        invalidateApeGraphStateKeyDedupMap();
        apeClearTransitionAggregationForActivity(actKey);
        BLOG("ape naming: refine activity=%s", activity.c_str());
        return true;
    }

    void Model::invalidateApeGraphStateKeyDedupMap() {
        _ape_graph_state_by_key.clear();
    }

#if DYNAMIC_STATE_ABSTRACTION_ENABLED
    bool Model::evalApeSourcePartitionPredicates(const std::string &activity,
                                                 const naming::NamingPtr &naming) const {
#if !defined(FASTBOT_HAS_PUGIXML) || !FASTBOT_HAS_PUGIXML
        (void)activity;
        (void)naming;
        return true;
#else
        if (!naming) {
            return true;
        }
        const std::string ak = naming::StateKey::canonicalActivityString(activity);
        for (const auto &p : _apeSourcePartitionPredicates) {
            if (p.activityKey != ak) {
                continue;
            }
            if (!evalApeSourcePartitionPredicateImpl(_apeStateXmlByStateHash, p.activityKey, activity,
                                                     naming, p.partitions)) {
                return false;
            }
        }
        return true;
#endif
    }

    void Model::pushApeSourcePartitionPredicate(const std::string &activity,
                                               const naming::NamingPtr &updatedNaming,
                                               std::vector<std::vector<uintptr_t>> partitions) {
#if !defined(FASTBOT_HAS_PUGIXML) || !FASTBOT_HAS_PUGIXML
        (void)activity;
        (void)updatedNaming;
        (void)partitions;
#else
        if (!updatedNaming || partitions.size() < 2) {
            return;
        }
        ApeSourcePartitionPredicate p;
        p.activityKey = naming::StateKey::canonicalActivityString(activity);
        p.updatedNamingFingerprint = updatedNaming->fingerprintString();
        p.partitions = std::move(partitions);
        _apeSourcePartitionPredicates.push_back(std::move(p));
#endif
    }

    void Model::pruneApeSourcePartitionPredicates(const std::string &activity,
                                                  const naming::NamingPtr &namingPrev,
                                                  const naming::NamingPtr &namingCur,
                                                  const std::unordered_set<uintptr_t> &affectedStateHashes) {
#if !defined(FASTBOT_HAS_PUGIXML) || !FASTBOT_HAS_PUGIXML
        (void)activity;
        (void)namingPrev;
        (void)namingCur;
        (void)affectedStateHashes;
#else
        if (!namingPrev || !namingCur) {
            return;
        }
        const std::string ak = naming::StateKey::canonicalActivityString(activity);
        _apeSourcePartitionPredicates.erase(
            std::remove_if(_apeSourcePartitionPredicates.begin(), _apeSourcePartitionPredicates.end(),
                           [&](const ApeSourcePartitionPredicate &pred) {
                               if (pred.activityKey != ak) {
                                   return false;
                               }
                               return !evalApeSourcePartitionPredicateImplTwoNamings(
                                   _apeStateXmlByStateHash, pred.activityKey, activity, namingPrev,
                                   namingCur, pred.partitions, affectedStateHashes);
                           }),
            _apeSourcePartitionPredicates.end());
#endif
    }

    bool Model::evalApeActionPartitionPredicates(const std::string &activity,
                                                 const naming::NamingPtr &naming) const {
#if !defined(FASTBOT_HAS_PUGIXML) || !FASTBOT_HAS_PUGIXML
        (void)activity;
        (void)naming;
        return true;
#else
        if (!naming) {
            return true;
        }
        const std::string ak = naming::StateKey::canonicalActivityString(activity);
        for (const auto &p : _apeActionPartitionPredicates) {
            if (p.activityKey != ak) {
                continue;
            }
            if (!evalApeActionPartitionPredicateImpl(_apeStateXmlByStateHash, p.activityKey, activity,
                                                     naming, p.partitions)) {
                return false;
            }
        }
        return true;
#endif
    }

    void Model::pushApeActionPartitionPredicate(const std::string &activity,
                                               const naming::NamingPtr &updatedNaming,
                                               std::vector<std::vector<std::pair<uintptr_t, size_t>>> partitions) {
#if !defined(FASTBOT_HAS_PUGIXML) || !FASTBOT_HAS_PUGIXML
        (void)activity;
        (void)updatedNaming;
        (void)partitions;
#else
        if (!updatedNaming || partitions.size() < 2) {
            return;
        }
        ApeActionPartitionPredicate p;
        p.activityKey = naming::StateKey::canonicalActivityString(activity);
        p.updatedNamingFingerprint = updatedNaming->fingerprintString();
        p.partitions = std::move(partitions);
        _apeActionPartitionPredicates.push_back(std::move(p));
#endif
    }

    void Model::pruneApeActionPartitionPredicates(const std::string &activity,
                                                  const naming::NamingPtr &namingPrev,
                                                  const naming::NamingPtr &namingCur,
                                                  const std::unordered_set<uintptr_t> &affectedStateHashes) {
#if !defined(FASTBOT_HAS_PUGIXML) || !FASTBOT_HAS_PUGIXML
        (void)activity;
        (void)namingPrev;
        (void)namingCur;
        (void)affectedStateHashes;
#else
        if (!namingPrev || !namingCur) {
            return;
        }
        const std::string ak = naming::StateKey::canonicalActivityString(activity);
        _apeActionPartitionPredicates.erase(
            std::remove_if(_apeActionPartitionPredicates.begin(), _apeActionPartitionPredicates.end(),
                           [&](const ApeActionPartitionPredicate &pred) {
                               if (pred.activityKey != ak) {
                                   return false;
                               }
                               return !evalApeActionPartitionPredicateImplTwoNamings(
                                   _apeStateXmlByStateHash, pred.activityKey, activity, namingPrev,
                                   namingCur, pred.partitions, affectedStateHashes);
                           }),
            _apeActionPartitionPredicates.end());
#endif
    }

    bool Model::evalApeStatesFewerThanPredicates(const std::string &activity,
                                                 const naming::NamingPtr &naming) const {
#if !defined(FASTBOT_HAS_PUGIXML) || !FASTBOT_HAS_PUGIXML
        (void)activity;
        (void)naming;
        return true;
#else
        if (!naming) {
            return true;
        }
        const std::string ak = naming::StateKey::canonicalActivityString(activity);
        for (const auto &p : _apeStatesFewerThanPredicates) {
            if (p.activityKey != ak) {
                continue;
            }
            if (!evalApeStatesFewerThanPredicateImpl(_apeStateXmlByStateHash, p.activityKey, activity,
                                                     naming, p.stateHashes, p.threshold)) {
                return false;
            }
        }
        return true;
#endif
    }

    void Model::pushApeStatesFewerThanPredicate(const std::string &activity,
                                               const naming::NamingPtr &updatedNaming, int threshold,
                                               std::vector<uintptr_t> stateHashes) {
#if !defined(FASTBOT_HAS_PUGIXML) || !FASTBOT_HAS_PUGIXML
        (void)activity;
        (void)updatedNaming;
        (void)threshold;
        (void)stateHashes;
#else
        if (!updatedNaming || stateHashes.empty() || threshold < 1) {
            return;
        }
        ApeStatesFewerThanPredicate p;
        p.activityKey = naming::StateKey::canonicalActivityString(activity);
        p.updatedNamingFingerprint = updatedNaming->fingerprintString();
        p.threshold = threshold;
        p.stateHashes = std::move(stateHashes);
        _apeStatesFewerThanPredicates.push_back(std::move(p));
#endif
    }

    void Model::pruneApeStatesFewerThanPredicates(const std::string &activity,
                                                 const naming::NamingPtr &namingPrev,
                                                 const naming::NamingPtr &namingCur,
                                                 const std::unordered_set<uintptr_t> &affectedStateHashes) {
#if !defined(FASTBOT_HAS_PUGIXML) || !FASTBOT_HAS_PUGIXML
        (void)activity;
        (void)namingPrev;
        (void)namingCur;
        (void)affectedStateHashes;
#else
        if (!namingPrev || !namingCur) {
            return;
        }
        const std::string ak = naming::StateKey::canonicalActivityString(activity);
        _apeStatesFewerThanPredicates.erase(
            std::remove_if(_apeStatesFewerThanPredicates.begin(), _apeStatesFewerThanPredicates.end(),
                           [&](const ApeStatesFewerThanPredicate &pred) {
                               if (pred.activityKey != ak) {
                                   return false;
                               }
                               return !evalApeStatesFewerThanPredicateImplTwoNamings(
                                   _apeStateXmlByStateHash, pred.activityKey, activity, namingPrev,
                                   namingCur, pred.stateHashes, pred.threshold, affectedStateHashes);
                           }),
            _apeStatesFewerThanPredicates.end());
#endif
    }

    void Model::apeCapGuiTreeNamingBlacklist() {
        // no-op: match Java unbounded guiTreeNamingBlaclist.
    }
#endif

    bool Model::coarsenActivityApeNamingIfNeeded(const std::string &activity) {
        const std::string actKey = naming::StateKey::canonicalActivityString(activity);
        auto it = _apeNamingContext.find(actKey);
        if (it == _apeNamingContext.end()) {
            return false;
        }
        ApeNamingAbstractionContext &ctx = it->second;
        naming::ActivityNamingManager &mgr2 = _apeStateNamingManager->activityManager();
        naming::NamingPtr cur = mgr2.getNaming(actKey);
        naming::NamingPtr prev = ctx.previousNamingBeforeRefine;
        if (!cur || !prev) {
            return false;
        }
        size_t affectedStateObservations = 0;
        for (const auto &p : ctx.oldKeyHashToObservationCount) {
            affectedStateObservations += p.second;
        }
        std::unordered_set<uintptr_t> totalNewKeys;
        bool overSplit = false;
        for (const auto &p : ctx.oldKeyHashToNewKeyHashes) {
            totalNewKeys.insert(p.second.begin(), p.second.end());
            if (p.second.size() > static_cast<size_t>(BetaMaxSplitCount)) {
                overSplit = true;
                break;
            }
        }
        // Java batchAbstract-inspired global rollback gates:
        // 1) affected old states threshold; 2) resulting refined targets threshold by fineness.
        const int affectedThreshold = 8;
        const int totalTypes = static_cast<int>(naming::namerTypesUsed().size());
        const int fineness = cur->getFineness();
        const int shift = std::max(0, totalTypes - fineness);
        const int targetThreshold = std::min(8, std::max(1, 2 << shift));
        const bool overAffected = affectedStateObservations > static_cast<size_t>(affectedThreshold);
        const bool overTargets = totalNewKeys.size() > static_cast<size_t>(targetThreshold);
        // Java batchAbstract filterTargets-like gate: focus on trigger source-key bucket.
        const uintptr_t triggerSource = ctx.triggerSourceKeyHash;
        size_t filteredAffected = 0;
        size_t filteredTargets = 0;
        // optimization 4 (align Java): recompute affectedStates/targets.size with the same
        // originState.equals(oldState) filtering semantics used by optimization 3.
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML && DYNAMIC_STATE_ABSTRACTION_ENABLED
        {
            bool computed = false;
            if (triggerSource != 0) {
                std::unordered_set<uintptr_t> distinctTargetKeys;
                std::unordered_set<uintptr_t> affectedStateHashesForPrune;
                for (const auto &sp : getGraph()->getStates()) {
                    if (!sp) {
                        continue;
                    }
                    auto apBa = sp->getActivityString();
                    const std::string aBa = (apBa && apBa.get())
                                                ? naming::StateKey::canonicalActivityString(*apBa)
                                                : std::string();
                    if (aBa != actKey) {
                        continue;
                    }
                    const uintptr_t ghBa = sp->hash();
                    naming::StateKey kStored = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    const bool haveStoredApeKey = tryGetApeStateKey(ghBa, &kStored);
                    const uintptr_t khBa = haveStoredApeKey ? kStored.hash() : ghBa;

                    // Approximate Java targetStates membership by checking whether this state belongs to
                    // targetNaming (`cur`) key space (i.e., its StateKey under `cur` is in `totalNewKeys`).
                    auto itXml = _apeStateXmlByStateHash.find(ghBa);
                    const bool haveXml = (itXml != _apeStateXmlByStateHash.end() && !itXml->second.empty());
                    // optimization 6: AssertStatesFewerThan.eval() skips missing XML.
                    // To keep gate/affected aligned with predicate inputs, exclude states without XML.
                    if (!haveXml) {
                        continue;
                    }
                    uintptr_t tgtKeyHash = 0;
                    if (haveXml) {
                        naming::StateKey kTgt = naming::StateKey::fromFallbackXmlStringHash("", 0);
                        if (apeStateKeyFromXmlWithNaming(activity, itXml->second, cur, &kTgt)) {
                            tgtKeyHash = kTgt.hash();
                        }
                    }

                    if (!totalNewKeys.empty()) {
                        if (tgtKeyHash == 0 || totalNewKeys.count(tgtKeyHash) == 0) {
                            continue;
                        }
                    }

                    bool affectedBa = false;
                    naming::StateKey kOld = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (apeStateKeyFromXmlWithNaming(activity, itXml->second, prev, &kOld)) {
                        affectedBa = (kOld.hash() == triggerSource);
                    }

                    if (affectedBa) {
                        filteredAffected++;
                        affectedStateHashesForPrune.insert(ghBa);
                        // Java targets = GUITreeBuilder.getStateKey(targetNaming, tree) for all trees in affected states.
                        if (tgtKeyHash != 0) {
                            distinctTargetKeys.insert(tgtKeyHash);
                        } else if (haveStoredApeKey) {
                            distinctTargetKeys.insert(khBa);
                        }
                    }
                }
                filteredTargets = distinctTargetKeys.size();
                computed = true;
            }
            if (!computed) {
                auto itFiltered = ctx.oldKeyHashToNewKeyHashes.find(triggerSource);
                if (itFiltered != ctx.oldKeyHashToNewKeyHashes.end()) {
                    auto itCnt = ctx.oldKeyHashToObservationCount.find(triggerSource);
                    filteredAffected = (itCnt == ctx.oldKeyHashToObservationCount.end()) ? 0 : itCnt->second;
                    filteredTargets = itFiltered->second.size();
                }
            }
        }
#else
        {
            auto itFiltered = ctx.oldKeyHashToNewKeyHashes.find(triggerSource);
            if (itFiltered != ctx.oldKeyHashToNewKeyHashes.end()) {
                auto itCnt = ctx.oldKeyHashToObservationCount.find(triggerSource);
                filteredAffected = (itCnt == ctx.oldKeyHashToObservationCount.end()) ? 0 : itCnt->second;
                filteredTargets = itFiltered->second.size();
            }
        }
#endif

        const bool overFilteredAffected = filteredAffected > static_cast<size_t>(affectedThreshold);
        const bool overFilteredTargets = filteredTargets > static_cast<size_t>(targetThreshold);

        // Pair-driven effectiveness check: if trigger source is observed but still unsplit
        // and trigger targets remain divergent after refinement, rollback.
        bool unresolvedTriggerPair = false;
        size_t postRefineMaxFanoutForAction = 0;
        auto itFiltered = ctx.oldKeyHashToNewKeyHashes.find(triggerSource);
        if (triggerSource != 0 && itFiltered != ctx.oldKeyHashToNewKeyHashes.end()) {
            const bool sourceSplit = itFiltered->second.size() > 1;
            std::unordered_set<uintptr_t> mappedTargetUnion;
            size_t coveredOldTargets = 0;
            for (auto oldT : ctx.triggerTargetKeyHashes) {
                auto itOld = ctx.oldKeyHashToNewKeyHashes.find(oldT);
                if (itOld == ctx.oldKeyHashToNewKeyHashes.end()) {
                    continue;
                }
                coveredOldTargets++;
                mappedTargetUnion.insert(itOld->second.begin(), itOld->second.end());
            }
            const size_t minEvidence = std::min<size_t>(ctx.triggerTargetCountAtRefine, 2);
            if (!sourceSplit && coveredOldTargets >= minEvidence && mappedTargetUnion.size() >= minEvidence) {
                unresolvedTriggerPair = true;
            }
        }
        // Action-level transition-sample check (Java checkActionRefinement approximation):
        // estimate post-refine fan-out via old->new key mapping evidence.
        if (ctx.triggerActionHash != 0 && ctx.triggerSourceKeyHash != 0) {
            auto itSrcMap = ctx.oldKeyHashToNewKeyHashes.find(ctx.triggerSourceKeyHash);
            if (itSrcMap != ctx.oldKeyHashToNewKeyHashes.end() && !itSrcMap->second.empty()) {
                std::unordered_set<uintptr_t> mappedTargetUnion;
                size_t coveredOldTargets = 0;
                for (auto oldT : ctx.triggerTargetKeyHashes) {
                    auto itOld = ctx.oldKeyHashToNewKeyHashes.find(oldT);
                    if (itOld == ctx.oldKeyHashToNewKeyHashes.end()) {
                        continue;
                    }
                    coveredOldTargets++;
                    mappedTargetUnion.insert(itOld->second.begin(), itOld->second.end());
                }
                postRefineMaxFanoutForAction = mappedTargetUnion.size();
                const size_t minEvidence = std::min<size_t>(ctx.triggerTargetCountAtRefine, 2);
                // Evidence guard: only fail when source and targets both have enough remap evidence.
                if (ctx.triggerTargetCountAtRefine > 0 &&
                    itSrcMap->second.size() >= minEvidence &&
                    coveredOldTargets >= minEvidence &&
                    postRefineMaxFanoutForAction >= ctx.triggerTargetCountAtRefine) {
                    unresolvedTriggerPair = true;
                }
            }
        }
        const bool hasTriggerSource = (triggerSource != 0);
        // When originState is available, align batchAbstract rollback gate to Java:
        // rollback only if (affectedStates.size > affectedThreshold) OR (targets.size > threshold).
        bool shouldRollback = false;
        if (hasTriggerSource) {
            shouldRollback = overFilteredAffected || overFilteredTargets;
        } else {
            shouldRollback = overSplit || overAffected || overTargets || unresolvedTriggerPair;
        }
        if (shouldRollback) {
            std::string fpFiner = cur->fingerprintString();
            std::unordered_set<uintptr_t> affectedStateHashesForBlacklist;
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML && DYNAMIC_STATE_ABSTRACTION_ENABLED
            // Compute affected state-hash set with the same filtering semantics as batchAbstract
            // (Java filterTargets(originState.equals(oldState))).
            for (const auto &kv : _apeStateXmlByStateHash) {
                const uintptr_t ghBa = kv.first;
                const std::string &xml = kv.second;
                if (xml.empty()) {
                    continue;
                }
                naming::StateKey storedKey = naming::StateKey::fromFallbackXmlStringHash("", 0);
                if (!tryGetApeStateKey(ghBa, &storedKey)) {
                    continue;
                }
                if (storedKey.activity() != actKey) {
                    continue;
                }
                // target membership under @cur
                naming::StateKey kTgt = naming::StateKey::fromFallbackXmlStringHash("", 0);
                if (!apeStateKeyFromXmlWithNaming(activity, xml, cur, &kTgt)) {
                    continue;
                }
                const uintptr_t tgtKeyHash = kTgt.hash();
                if (!totalNewKeys.empty() && totalNewKeys.count(tgtKeyHash) == 0) {
                    continue;
                }

                bool affectedBa = false;
                if (triggerSource != 0) {
                    // originState.equals(oldState) under @prev.
                    naming::StateKey kOld = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (apeStateKeyFromXmlWithNaming(activity, xml, prev, &kOld)) {
                        affectedBa = (kOld.hash() == triggerSource);
                    }
                } else {
                    // Fallback when triggerSource is missing: use old->new mapping evidence.
                    const uintptr_t khBa = storedKey.hash();
                    for (const auto &p : ctx.oldKeyHashToNewKeyHashes) {
                        if (p.first == khBa) {
                            affectedBa = true;
                            break;
                        }
                        for (uintptr_t nh : p.second) {
                            if (nh == khBa) {
                                affectedBa = true;
                                break;
                            }
                        }
                        if (affectedBa) {
                            break;
                        }
                    }
                }
                if (affectedBa) {
                    affectedStateHashesForBlacklist.insert(ghBa);
                }
            }
#endif
            apeBlacklistFinerNamingOnRollback(activity, cur, ctx, affectedStateHashesForBlacklist);
            if (ctx.triggerSourceKeyExact) {
                _apeStateNamingManager->updateNamingWithStateKey(
                    actKey, naming::NamingUpdateKind::Abstract, cur, prev, ctx.triggerSourceKey);
            } else {
                _apeStateNamingManager->updateNamingWithStateHash(
                    actKey, naming::NamingUpdateKind::Abstract, cur, prev, ctx.triggerSourceKeyHash);
            }
            invalidateApeGraphStateKeyDedupMap();
            // removeConflictPredicates(aligned): only evaluate constraints whose predicates intersect affectedGUITrees.
            // For now we approximate affectedGUITrees as affected state-hashes (when available under pugixml build).
            std::unordered_set<uintptr_t> affectedStateHashesForPrune;
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML && DYNAMIC_STATE_ABSTRACTION_ENABLED
            // We approximate affectedGUITrees using exactly the GUI-tree XML entries we cache for predicate eval.
            // This better matches Java's affectedGUITrees set fed into removeConflictPredicates.
            for (const auto &kv : _apeStateXmlByStateHash) {
                const uintptr_t ghBa = kv.first;
                const std::string &xml = kv.second;
                if (xml.empty()) {
                    continue;
                }
                naming::StateKey storedKey = naming::StateKey::fromFallbackXmlStringHash("", 0);
                if (!tryGetApeStateKey(ghBa, &storedKey)) {
                    continue;
                }
                if (storedKey.activity() != actKey) {
                    continue;
                }

                // Membership in targetNaming: use cur to derive tgtStateKey and check totalNewKeys.
                naming::StateKey kTgt = naming::StateKey::fromFallbackXmlStringHash("", 0);
                if (!apeStateKeyFromXmlWithNaming(activity, xml, cur, &kTgt)) {
                    continue;
                }
                const uintptr_t tgtKeyHash = kTgt.hash();
                if (!totalNewKeys.empty() && totalNewKeys.count(tgtKeyHash) == 0) {
                    continue;
                }

                bool affectedBa = false;
                if (triggerSource != 0) {
                    // originState.equals(oldState): prev should derive oldState key hash == triggerSource.
                    naming::StateKey kOld = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    if (apeStateKeyFromXmlWithNaming(activity, xml, prev, &kOld)) {
                        affectedBa = (kOld.hash() == triggerSource);
                    }
                } else {
                    // Fallback when triggerSource is missing: match against old->new mapping evidence.
                    const uintptr_t khBa = storedKey.hash();
                    for (const auto &p : ctx.oldKeyHashToNewKeyHashes) {
                        if (p.first == khBa) {
                            affectedBa = true;
                            break;
                        }
                        for (uintptr_t nh : p.second) {
                            if (nh == khBa) {
                                affectedBa = true;
                                break;
                            }
                        }
                        if (affectedBa) {
                            break;
                        }
                    }
                }

                if (affectedBa) {
                    affectedStateHashesForPrune.insert(ghBa);
                }
            }
#endif
            pruneApeSourcePartitionPredicates(activity, prev, cur, affectedStateHashesForPrune);
            pruneApeActionPartitionPredicates(activity, prev, cur, affectedStateHashesForPrune);
            pruneApeStatesFewerThanPredicates(activity, prev, cur, affectedStateHashesForPrune);
            {
                // NamingFactory.batchAbstract: after rollback to targetParentNaming, add AssertStatesFewerThan
                // (threshold from finer targetNaming fineness; trees = affected in rollback bucket).
                std::vector<uintptr_t> batchAbstractHashes;
                batchAbstractHashes.reserve(64);
                // Align Java "distinct StateKeys" nature: keep one representative state hash per APE key hash.
                std::unordered_set<uintptr_t> seenApeKeyHashes;
                const uintptr_t originOldKeyHash = ctx.triggerSourceKeyHash;
                // Java AssertStatesFewerThan stops once distinct StateKeys size > threshold.
                const int totalTypesBa = static_cast<int>(naming::namerTypesUsed().size());
                const int shiftBa = std::max(0, totalTypesBa - cur->getFineness());
                const int thrBa = std::min(8, std::max(1, 2 << shiftBa));
                // thrBa is also passed into AssertStatesFewerThan below.
                // Fallback（当我们无法基于 XML 计算 oldState 时）沿用优化 2：
                // keep only target (new) key hashes remapped from the trigger source bucket.
                std::unordered_set<uintptr_t> allowedNewKeyHashes;
                if (originOldKeyHash != 0) {
                    auto itNew = ctx.oldKeyHashToNewKeyHashes.find(originOldKeyHash);
                    if (itNew != ctx.oldKeyHashToNewKeyHashes.end()) {
                        // Strict originState semantics (Java filterTargets):
                        // if a new key hash remaps from multiple different old key hashes, we cannot
                        // guarantee it came from the originState bucket => drop it to avoid false affected.
                        std::unordered_map<uintptr_t, size_t> newKeyToOldCount;
                        for (const auto &p : ctx.oldKeyHashToNewKeyHashes) {
                            for (uintptr_t nh : p.second) {
                                newKeyToOldCount[nh]++;
                            }
                        }
                        for (uintptr_t nh : itNew->second) {
                            if (newKeyToOldCount[nh] == 1) {
                                allowedNewKeyHashes.insert(nh);
                            }
                        }
                    }
                }
                for (const auto &sp : getGraph()->getStates()) {
                    if (!sp) {
                        continue;
                    }
                    auto apBa = sp->getActivityString();
                    const std::string aBa = (apBa && apBa.get())
                                                ? naming::StateKey::canonicalActivityString(*apBa)
                                                : std::string();
                    if (aBa != actKey) {
                        continue;
                    }
                    const uintptr_t ghBa = sp->hash();
                    naming::StateKey kBa = naming::StateKey::fromFallbackXmlStringHash("", 0);
                    const bool haveStoredApeKey = tryGetApeStateKey(ghBa, &kBa);
                    const uintptr_t khBa = haveStoredApeKey ? kBa.hash() : ghBa;
                    // Optimization 5: AssertStatesFewerThan distinct-key must match Java,
                    // i.e. distinct StateKeys computed under `prev` (targetParentNaming).
                    // Default to stored APE key hash; if we can rebuild oldState under `prev`,
                    // we'll overwrite this with `oldState.hash()`.
                    uintptr_t dedupKey = khBa;
                    // Keep batchAbstract `affectedStates` consistent with rollback gate (optimization 4):
                    // approximate Java `targetStates` membership by checking whether this concrete state belongs
                    // to `targetNaming` (`cur`) key space, i.e. StateKey under `cur` is within `totalNewKeys`.
                    uintptr_t tgtKeyHashForMembership = 0;
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML && DYNAMIC_STATE_ABSTRACTION_ENABLED
                    {
                        auto itXml = _apeStateXmlByStateHash.find(ghBa);
                        const bool haveXml = (itXml != _apeStateXmlByStateHash.end() && !itXml->second.empty());
                        if (haveXml) {
                            naming::StateKey kTgt = naming::StateKey::fromFallbackXmlStringHash("", 0);
                            if (apeStateKeyFromXmlWithNaming(activity, itXml->second, cur, &kTgt)) {
                                tgtKeyHashForMembership = kTgt.hash();
                            }
                        }
                    }
#else
                    tgtKeyHashForMembership = dedupKey;
#endif
                    if (!totalNewKeys.empty() &&
                        (tgtKeyHashForMembership == 0 || totalNewKeys.count(tgtKeyHashForMembership) == 0)) {
                        continue;
                    }
                    bool affectedBa = false;

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML && DYNAMIC_STATE_ABSTRACTION_ENABLED
                    if (originOldKeyHash != 0) {
                        // Java filterTargets(originState.equals(oldState)):
                        // recompute oldState under prev using cached XML.
                        auto itXml = _apeStateXmlByStateHash.find(ghBa);
                        if (itXml != _apeStateXmlByStateHash.end() && !itXml->second.empty()) {
                            naming::StateKey kOld = naming::StateKey::fromFallbackXmlStringHash("", 0);
                            if (apeStateKeyFromXmlWithNaming(activity, itXml->second, prev, &kOld)) {
                                dedupKey = kOld.hash();
                                affectedBa = (kOld.hash() == originOldKeyHash);
                            }
                        }
                    } else
#endif
                    {
                        // Fallback（当 originOldKeyHash 不可用 / 或无 pugixml 时）沿用优化 2：
                        // match against stored APE key hash using old->new mapping evidence.
                        if (!haveStoredApeKey) {
                            continue;
                        }
                        if (!allowedNewKeyHashes.empty()) {
                            // Prefer strict "new target key hashes only" semantics.
                            affectedBa = allowedNewKeyHashes.count(khBa) != 0;
                        } else {
                            for (const auto &p : ctx.oldKeyHashToNewKeyHashes) {
                                if (p.first == khBa) {
                                    affectedBa = true;
                                    break;
                                }
                                for (uintptr_t nh : p.second) {
                                    if (nh == khBa) {
                                        affectedBa = true;
                                        break;
                                    }
                                }
                                if (affectedBa) {
                                    break;
                                }
                            }
                        }
                    }
                    if (!affectedBa) continue;

                    if (seenApeKeyHashes.insert(dedupKey).second) {
                        batchAbstractHashes.push_back(ghBa);
                        // Early stop when distinct-count would already fail predicate.
                        if (seenApeKeyHashes.size() > static_cast<size_t>(thrBa)) {
                            break;
                        }
                    }
                }
                if (!batchAbstractHashes.empty()) {
                    pushApeStatesFewerThanPredicate(activity, prev, thrBa, std::move(batchAbstractHashes));
                }
            }
            apeClearTransitionAggregationForActivity(actKey);
            _apeNamingCoarseningBlacklist.insert(std::make_pair(actKey, fpFiner));
            if (ctx.triggerSourceKeyHash != 0 || ctx.triggerActionHash != 0) {
                _apeRefinePairBlacklist[actKey].insert(
                    ApePairKey{ctx.triggerSourceKeyHash, ctx.triggerActionHash});
            }
            apeCapApeNamingCoarsenAndRefineBlacklists();
            ctx.oldKeyHashToNewKeyHashes.clear();
            ctx.oldKeyHashToObservationCount.clear();
            ctx.previousNamingBeforeRefine = nullptr;
            ctx.previousNamingFingerprintBeforeRefine.clear();
            ctx.triggerSourceKeyHash = 0;
            ctx.triggerSourceKeyExact = false;
            ctx.triggerSourceKey = naming::StateKey::fromFallbackXmlStringHash("", 0);
            ctx.triggerActionHash = 0;
            ctx.triggerTargetKeyHashes.clear();
            ctx.triggerTargetCountAtRefine = 0;
            ctx.stateCountAtLastNamingRefinement = getGraph()->getStateCountByActivity(activity);
            BLOG("ape naming: coarsen activity=%s rollback split=%d overAffected=%d overTargets=%d "
                 "overFilteredAffected=%d overFilteredTargets=%d unresolvedTriggerPair=%d "
                 "affectedStates=%zu totalNew=%zu filteredAffected=%zu filteredTargets=%zu triggerTargets=%zu postFanout=%zu "
                 "targetThreshold=%d fp=%s",
                 activity.c_str(), overSplit ? 1 : 0, overAffected ? 1 : 0, overTargets ? 1 : 0,
                 overFilteredAffected ? 1 : 0, overFilteredTargets ? 1 : 0, unresolvedTriggerPair ? 1 : 0,
                 affectedStateObservations, totalNewKeys.size(), filteredAffected, filteredTargets,
                 ctx.triggerTargetCountAtRefine,
                 postRefineMaxFanoutForAction, targetThreshold, fpFiner.c_str());
            return true;
        }
        return false;
    }

    bool Model::runApeNamingAbstractionBatch() {
        if (Preference::inst() && Preference::inst()->useStaticReuseAbstraction()) {
            return false;
        }
        const size_t eventRefineBefore = _apeEventRefineSuccessCount;
        const size_t eventRollbackBefore = _apeEventCoarsenRollbackCount;
        const size_t batchRefineBefore = _apeBatchRefineSuccessCount;
        const size_t batchRollbackBefore = _apeBatchCoarsenRollbackCount;
        const int minTargets = (_preference ? _preference->getApeNamingMinNonDetTargets()
                                            : MinNonDeterminismCount);
        for (const auto &kv : _apeNamingContext) {
            const std::string &activity = kv.first;
            const ApeNamingAbstractionContext &ctx = kv.second;
            if (!ctx.previousNamingBeforeRefine) {
                continue;
            }
            naming::NamingPtr n = _apeStateNamingManager->activityManager().getNaming(activity);
            if (!n) {
                continue;
            }
            if (ctx.previousNamingFingerprintBeforeRefine != n->fingerprintString()) {
                if (coarsenActivityApeNamingIfNeeded(activity)) {
                    _apeBatchCoarsenRollbackCount++;
                }
            }
        }
        if (Preference::inst() && !Preference::inst()->useApeNamingPeriodicRefinement()) {
            const bool mutatedPrefetch =
                (_apeBatchRefineSuccessCount > batchRefineBefore ||
                 _apeBatchCoarsenRollbackCount > batchRollbackBefore);
            if (mutatedPrefetch) {
                notifyAgentsOfApeNamingChange();
            }
            return mutatedPrefetch;
        }
        const std::string ruleProfile =
            (_preference ? _preference->getApeNamingActionRefineRuleProfile() : "baseline");
        auto collectNonDetPairs = [&]() -> std::vector<ApeNonDetPairStat> {
            std::vector<ApeNonDetPairStat> out;
            out.reserve(_apePairAgg.size());
            for (const auto &kv : _apePairAgg) {
                const auto &tm = kv.second.targetCounts;
                if (tm.size() < static_cast<size_t>(minTargets)) {
                    continue;
                }
                ApeNonDetPairStat s;
                s.sourceKeyHash = kv.first.sourceKeyHash;
                s.actionHash = kv.first.actionHash;
                s.sourceActivity = kv.second.sourceActivity;
                s.hasSourceStateKey = kv.second.hasSourceStateKey;
                if (s.hasSourceStateKey) {
                    s.sourceStateKey = kv.second.sourceStateKey;
                }
                for (const auto &te : tm) {
                    s.targetKeyHashes.insert(te.first);
                }
                s.targetCount = s.targetKeyHashes.size();
                out.push_back(std::move(s));
            }
            std::sort(out.begin(), out.end(), [](const ApeNonDetPairStat &a, const ApeNonDetPairStat &b) {
                if (a.targetCount != b.targetCount) return a.targetCount > b.targetCount;
                if (a.sourceActivity != b.sourceActivity) return a.sourceActivity < b.sourceActivity;
                if (a.sourceKeyHash != b.sourceKeyHash) return a.sourceKeyHash < b.sourceKeyHash;
                return a.actionHash < b.actionHash;
            });
            return out;
        };
        auto toRefinePair = [](const ApeNonDetPairStat &p) -> ApeRefinePair {
            ApeRefinePair out;
            out.sourceKeyHash = p.sourceKeyHash;
            out.hasSourceStateKey = p.hasSourceStateKey;
            if (out.hasSourceStateKey) {
                out.sourceStateKey = p.sourceStateKey;
            }
            out.actionHash = p.actionHash;
            out.targetKeyHashes = p.targetKeyHashes;
            out.targetCount = p.targetCount;
            return out;
        };
        auto countNonDetPairsPerActivity = [](const std::vector<ApeNonDetPairStat> &v) {
            std::unordered_map<std::string, int> m;
            m.reserve(std::max<size_t>(v.size(), 8) * 2);
            for (const auto &p : v) {
                const std::string k = naming::StateKey::canonicalActivityString(p.sourceActivity);
                m[k]++;
            }
            return m;
        };
        if (UsePaperRefinementOrder) {
            std::vector<ApeNonDetPairStat> nonDetPairs = collectNonDetPairs();
            const std::unordered_map<std::string, int> nonDetCountByAct = countNonDetPairsPerActivity(nonDetPairs);
            if (ruleProfile == "java_rule_02_preview" && nonDetPairs.size() > 1) {
                nonDetPairs.resize(1);
            }
            BLOG("ape naming: paper order nonDetPairs=%zu", nonDetPairs.size());
            std::unordered_set<std::string> refinedActivities;
            for (const auto &p : nonDetPairs) {
                if (refinedActivities.count(p.sourceActivity) != 0) {
                    continue;
                }
                ApeRefinePair rp = toRefinePair(p);
                BLOG("ape naming: refine-attempt activity=%s srcKey=%lu act=%lu targets=%zu",
                     p.sourceActivity.c_str(), (unsigned long)p.sourceKeyHash,
                     (unsigned long)p.actionHash, p.targetCount);
                const int preN = nonDetCountByAct.at(
                    naming::StateKey::canonicalActivityString(p.sourceActivity));
                if (refineActivityApeNaming(p.sourceActivity, &rp, preN)) {
                    _apeBatchRefineSuccessCount++;
                    if (coarsenActivityApeNamingIfNeeded(p.sourceActivity)) {
                        _apeBatchCoarsenRollbackCount++;
                    }
                    refinedActivities.insert(p.sourceActivity);
                } else if (rp.actionHash != 0 &&
                           p.targetCount >= static_cast<size_t>(kApeNDActionBlacklistMinOutEdges)) {
                    const std::string actKey = naming::StateKey::canonicalActivityString(p.sourceActivity);
                    _apeRefineActionBlacklist[actKey].insert(rp.actionHash);
                    apeCapApeNamingCoarsenAndRefineBlacklists();
                    BLOG("ape naming: NDActionBlacklist add (APE: out>=%d after failed resolve) activity=%s "
                         "act=%lu targets=%zu",
                         kApeNDActionBlacklistMinOutEdges, p.sourceActivity.c_str(),
                         (unsigned long)rp.actionHash, p.targetCount);
                }
            }
        } else {
            std::vector<ApeNonDetPairStat> nonDetPairs = collectNonDetPairs();
            const std::unordered_map<std::string, int> nonDetCountByAct = countNonDetPairsPerActivity(nonDetPairs);
            if (ruleProfile == "java_rule_02_preview" && nonDetPairs.size() > 1) {
                nonDetPairs.resize(1);
            }
            BLOG("ape naming: batch nonDetPairs=%zu", nonDetPairs.size());
            std::vector<std::string> refinedActs;
            std::unordered_set<std::string> refinedActivities;
            for (const auto &p : nonDetPairs) {
                if (refinedActivities.count(p.sourceActivity) != 0) {
                    continue;
                }
                ApeRefinePair rp = toRefinePair(p);
                BLOG("ape naming: refine-attempt activity=%s srcKey=%lu act=%lu targets=%zu",
                     p.sourceActivity.c_str(), (unsigned long)p.sourceKeyHash,
                     (unsigned long)p.actionHash, p.targetCount);
                const int preN = nonDetCountByAct.at(
                    naming::StateKey::canonicalActivityString(p.sourceActivity));
                if (refineActivityApeNaming(p.sourceActivity, &rp, preN)) {
                    _apeBatchRefineSuccessCount++;
                    refinedActs.push_back(p.sourceActivity);
                    refinedActivities.insert(p.sourceActivity);
                } else if (rp.actionHash != 0 &&
                           p.targetCount >= static_cast<size_t>(kApeNDActionBlacklistMinOutEdges)) {
                    const std::string actKey = naming::StateKey::canonicalActivityString(p.sourceActivity);
                    _apeRefineActionBlacklist[actKey].insert(rp.actionHash);
                    apeCapApeNamingCoarsenAndRefineBlacklists();
                    BLOG("ape naming: NDActionBlacklist add (APE: out>=%d after failed resolve) activity=%s "
                         "act=%lu targets=%zu",
                         kApeNDActionBlacklistMinOutEdges, p.sourceActivity.c_str(),
                         (unsigned long)rp.actionHash, p.targetCount);
                }
            }
            for (const auto &a : refinedActs) {
                if (coarsenActivityApeNamingIfNeeded(a)) {
                    _apeBatchCoarsenRollbackCount++;
                }
            }
        }
        const size_t eventRefineDelta = _apeEventRefineSuccessCount - eventRefineBefore;
        const size_t eventRollbackDelta = _apeEventCoarsenRollbackCount - eventRollbackBefore;
        const size_t batchRefineDelta = _apeBatchRefineSuccessCount - batchRefineBefore;
        const size_t batchRollbackDelta = _apeBatchCoarsenRollbackCount - batchRollbackBefore;
        BLOG("ape naming: counters delta event(refine=%zu,rollback=%zu) "
             "batch(refine=%zu,rollback=%zu) total event(refine=%zu,rollback=%zu) "
             "batch(refine=%zu,rollback=%zu)",
             eventRefineDelta, eventRollbackDelta, batchRefineDelta, batchRollbackDelta,
             _apeEventRefineSuccessCount, _apeEventCoarsenRollbackCount,
             _apeBatchRefineSuccessCount, _apeBatchCoarsenRollbackCount);
        if (_apeStateNamingManager) {
            const auto edgeStats = _apeStateNamingManager->consumeEdgeLookupStats();
            const uint64_t total =
                edgeStats.exact_hit + edgeStats.hash_only_hit + edgeStats.miss;
            if (total > 0) {
                const double exactRate = (100.0 * static_cast<double>(edgeStats.exact_hit)) /
                                         static_cast<double>(total);
                const double fallbackRate = (100.0 * static_cast<double>(edgeStats.hash_only_hit)) /
                                            static_cast<double>(total);
                const double missRate = (100.0 * static_cast<double>(edgeStats.miss)) /
                                        static_cast<double>(total);
                BLOG("ape naming: edge lookup window total=%" PRIu64
                     " exact=%" PRIu64 " (%.2f%%) hashOnly=%" PRIu64 " (%.2f%%) miss=%" PRIu64 " (%.2f%%)",
                     total, edgeStats.exact_hit, exactRate,
                     edgeStats.hash_only_hit, fallbackRate, edgeStats.miss, missRate);
            }
        }
        const bool batchMutated = (batchRefineDelta > 0 || batchRollbackDelta > 0);
        if (batchMutated) {
            notifyAgentsOfApeNamingChange();
        }
        return batchMutated;
    }

    void Model::runRefinementAndCoarseningIfScheduled() {
        if (Preference::inst() && Preference::inst()->useStaticReuseAbstraction()) {
            return;
        }
        if (_stepCountSinceLastCheck < static_cast<size_t>(RefinementCheckInterval)) return;
        BLOG("state abstraction: ape-only batch at step %zu (interval=%d)",
             _stepCountSinceLastCheck, (int)RefinementCheckInterval);
        (void)runApeNamingAbstractionBatch();
    }
#endif

    void Model::reportActivity(const std::string &activity) {
        if (activity.empty()) return;
        std::lock_guard<std::mutex> lock(_coverageMutex);
        _visitedActivities.insert(activity);
        _coverageStepCount++;
    }

    std::string Model::getCoverageJson() const {
        std::lock_guard<std::mutex> lock(_coverageMutex);
        nlohmann::json j;
        j["stepsCount"] = _coverageStepCount;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto &a : _visitedActivities) {
            arr.push_back(a);
        }
        j["testedActivities"] = arr;
        return j.dump();
    }

    void Model::loadStateAbstractionPolicy() {
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        auto pref = Preference::inst();
        // Only meaningful when dynamic abstraction is actually used and policy persistence is enabled.
        if (!pref || pref->useStaticReuseAbstraction() || !pref->isStateAbstractionPolicyEnabled()) {
            return;
        }

        const std::string &pkg = getPackageName();
        if (pkg.empty()) {
            return;
        }

        std::string path = "/sdcard/fastbot_" + pkg + ".statekey.json";
        BLOG("state abstraction: try load policy from %s", path.c_str());
        std::ifstream in(path);
        if (!in.is_open()) {
            return;
        }

        try {
            // Basic size guard to avoid attempting to parse extremely large / corrupted files.
            in.seekg(0, std::ios::end);
            std::streamoff sz = in.tellg();
            if (sz <= 0 || sz > static_cast<std::streamoff>(1024 * 1024)) { // 1MB hard upper bound
                BLOGE("state abstraction: skip loading %s (size=%lld bytes out of bounds)", path.c_str(),
                      static_cast<long long>(sz));
                return;
            }
            in.seekg(0, std::ios::beg);

            nlohmann::json j;
            in >> j;

            if (!j.is_object()) {
                BLOGE("state abstraction: policy file %s is not a JSON object", path.c_str());
                return;
            }

            // v1 files may contain widget-key masks and coarseningBlacklist (legacy); do not apply — dynamic
            // identity is APE StateKey-only; keeping old entries would confuse debugging.
            auto itActs = j.find("activities");
            if (itActs != j.end() && itActs->is_array() && !itActs->empty()) {
                BLOG("state abstraction: %s contains legacy activities[]; ignored", path.c_str());
            }
            auto itBlk = j.find("coarseningBlacklist");
            if (itBlk != j.end() && itBlk->is_array() && !itBlk->empty()) {
                BLOG("state abstraction: %s contains legacy coarseningBlacklist; ignored", path.c_str());
            }

            BLOG("state abstraction: loaded policy metadata from %s (no widget-mask state applied)", path.c_str());
        } catch (const std::exception &ex) {
            BLOGE("state abstraction: failed to load policy from %s: %s", path.c_str(), ex.what());
        }
#else
        (void)this;
#endif
    }

    void Model::saveStateAbstractionPolicy() const {
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        auto pref = Preference::inst();
        // Only meaningful when dynamic abstraction is actually used and policy persistence is enabled.
        if (!pref || pref->useStaticReuseAbstraction() || !pref->isStateAbstractionPolicyEnabled()) {
            return;
        }

        const std::string &pkg = getPackageName();
        if (pkg.empty()) {
            return;
        }

        std::string path = "/sdcard/fastbot_" + pkg + ".statekey.json";
        std::string tmpPath = path + ".tmp";

        nlohmann::json j;
        j["version"] = 2;
        j["activities"] = nlohmann::json::array();

        try {
            std::ofstream out(tmpPath, std::ios::trunc);
            if (!out.is_open()) {
                BLOGE("state abstraction: cannot open temp policy file %s for writing", tmpPath.c_str());
                return;
            }
            out << j.dump();
            out.close();
            if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
                BLOGE("state abstraction: failed to rename policy file %s -> %s", tmpPath.c_str(), path.c_str());
            } else {
                BLOG("state abstraction: policy saved to %s", path.c_str());
            }
        } catch (const std::exception &ex) {
            BLOGE("state abstraction: failed to save policy to %s: %s", path.c_str(), ex.what());
        }
#else
        (void)this;
#endif
    }

#if DYNAMIC_STATE_ABSTRACTION_ENABLED
    void Model::logApeStateKeySnapshot(const std::string &rawActivity, const StatePtr &state,
                                       const naming::StateKey &key, const GraphPtr &graph) {
        std::ostringstream head;
        head << "ape-key: activityRaw=" << rawActivity
             << " activityKey=" << key.activity()
             << " stateHash=" << static_cast<unsigned long>(state ? state->hash() : 0U)
             << " stateKeyHash=" << static_cast<unsigned long>(key.hash())
             << " graphSize=" << static_cast<unsigned long>(graph ? graph->stateSize() : 0U)
             << " names=" << key.sortedXPaths().size();
        BDLOG("%s", head.str().c_str());
        std::ostringstream sample;
        const auto &xs = key.sortedXPaths();
        const size_t k = std::min<size_t>(3, xs.size());
        for (size_t i = 0; i < k; ++i) {
            if (i != 0) {
                sample << " | ";
            }
            sample << xs[i];
        }
        BDLOG("ape-key: sample=%s", sample.str().c_str());
    }

#endif

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML

    bool Model::buildApeStateKeyFromElementTree(const ElementPtr &element, const std::string &activity,
                                               naming::StateKey *outKey,
                                               const StatePtr &stateForDynamicApply) {
        if (!element || !outKey) {
            return false;
        }
        // When static reuse abstraction is enabled, we must not sync APE naming fixed-point
        // refinement or update dynamic naming bookkeeping; we only need enough naming to
        // compute the StateKey identity.
        const bool wantApeRlIdentity =
            !_preference || !_preference->useStaticReuseAbstraction();
        std::string pkg;
        std::string cls;
        naming::StateKey::splitActivityPackageClass(activity, &pkg, &cls);
        const std::string actKey = naming::StateKey::canonicalActivityString(activity);
        gui_tree::GUITreeBuildResult built = gui_tree::GUITreeFactory::buildFromElement(element, pkg, cls);
        if (!built.tree || !built.dom) {
            const std::string xml = element->toXML();
            built = gui_tree::GUITreeFactory::buildFromXml(xml, pkg, cls);
        }
        if (!built.tree || !built.dom) {
            return false;
        }
        naming::ActivityNamingManager &mgr = _apeStateNamingManager->activityManager();
        const int fpSteps = (_preference && wantApeRlIdentity)
                                ? _preference->getApeNamingFixedPointMaxIter()
                                : 0;
        naming::NamingPtr naming;
        if (fpSteps > 0) {
            naming = _apeStateNamingManager->getNamingFixedPoint(actKey, *built.tree, built.dom, fpSteps);
            if (!naming) {
                return false;
            }
        } else {
            naming = mgr.getNaming(actKey);
            if (!naming) {
                naming = naming::NamingFactory::defaultRootNaming();
                // In static reuse abstraction mode, avoid syncing a newly created naming into the manager.
                if (naming && wantApeRlIdentity) {
                    _apeStateNamingManager->updateNaming(
                        actKey, naming::NamingUpdateKind::Refine, naming);
                }
            }
            if (!naming) {
                return false;
            }
            if (!naming::NamingFactory::rebuildTree(naming, *built.tree, built.dom)) {
                return false;
            }
        }
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        if (wantApeRlIdentity) {
            auto itCtx = _apeNamingContext.find(actKey);
            if (itCtx != _apeNamingContext.end() && itCtx->second.previousNamingBeforeRefine) {
                naming::NamingPtr prevN = itCtx->second.previousNamingBeforeRefine;
                if (!naming::NamingFactory::rebuildTree(prevN, *built.tree, built.dom)) {
                    return false;
                }
                naming::StateKey kOld = naming::StateKey::fromGUITree(*built.tree);
                if (!naming::NamingFactory::rebuildTree(naming, *built.tree, built.dom)) {
                    return false;
                }
                naming::StateKey kNewAfter = naming::StateKey::fromGUITree(*built.tree);
                uintptr_t oldH = kOld.hash();
                uintptr_t newH = kNewAfter.hash();
                if (oldH != newH) {
                    itCtx->second.oldKeyHashToNewKeyHashes[oldH].insert(newH);
                    itCtx->second.oldKeyHashToObservationCount[oldH]++;
                }
            }
        }
#endif
        naming::StateKey kNew = naming::StateKey::fromGUITree(*built.tree);
#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        if (stateForDynamicApply) {
            std::vector<const gui_tree::GUITreeNode *> guiPreOrder;
            collectGUITreeNodesPreOrder(built.tree->getRootNode(), &guiPreOrder);
            applyApeDynamicActionHashesToReuseState(stateForDynamicApply, guiPreOrder, kNew);
        }
#endif
        *outKey = std::move(kNew);
        return true;
    }
#endif

    void Model::recordApeStateKey(const StatePtr &state, const naming::StateKey &key) {
        if (!state) {
            return;
        }
        const uintptr_t h = state->hash();
        auto it = _ape_state_keys_by_hash.find(h);
        if (it != _ape_state_keys_by_hash.end()) {
            it->second = key;
        } else {
            _ape_state_keys_by_hash.emplace(h, key);
        }
    }

    bool Model::tryGetApeStateKey(uintptr_t stateHash, naming::StateKey *out) const {
        auto it = _ape_state_keys_by_hash.find(stateHash);
        if (it == _ape_state_keys_by_hash.end()) {
            return false;
        }
        if (out != nullptr) {
            *out = it->second;
        }
        return true;
    }

    /**
     * @brief Destructor for Model class
     * 
     * Clears the device-agent map to release all agent resources.
     * The graph and preference are shared pointers and will be automatically
     * cleaned up when the last reference is released.
     */
    Model::~Model() {
        this->_deviceIDAgentMap.clear();
    }

}  // namespace fastbotx

#endif  // Model_CPP_