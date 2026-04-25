#ifndef FASTBOTX_MODEL_TREE_TRANSITION_H_
#define FASTBOTX_MODEL_TREE_TRANSITION_H_

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Action.h"
#include "Base.h"

#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
#include "../desc/gui_tree/GUITree.h"
#endif

namespace fastbotx {

struct ApeTransitionEntry;

struct TreeTransitionEntry {
    uint64_t transitionSeq{0};
    uintptr_t sourceStateHash{0};
    uintptr_t targetStateHash{0};
    uintptr_t actionHash{0};
    ActionType actionType{ActionType::NOP};
    std::vector<int> resolvedNodeStableIds;
    bool hasTargetBounds{false};
    Rect targetBounds{};
    bool hasTargetFullPath{false};
    uintptr_t targetFullPathHash{0};
    std::string sourceActivity;
    bool valid{false};
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
    gui_tree::GUITreePtr apeSourceGuiTree{};
    gui_tree::GUITreePtr apeTargetGuiTree{};
#endif
};

struct NondetTreeTransitionBranchPair {
    struct SourceTransition {
        uint64_t transitionSeq{0};
        uintptr_t sourceStateHash{0};
        uintptr_t targetStateHash{0};
        std::string sourceXml;
        std::vector<int> resolvedNodeStableIds;
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
        gui_tree::GUITreePtr sourceGuiSnapshot{};
#endif
    };

    uintptr_t sourceStateHash{0};
    uint64_t sourceTransitionSeq{0};
    uint64_t firstSeenSeq{0};
    uintptr_t targetKeyA{0};
    uintptr_t targetKeyB{0};
    uintptr_t nstTargetStateHash{0};
    std::vector<std::string> branchA;
    std::vector<std::string> branchB;
    std::vector<SourceTransition> branchATransitions;
    std::vector<SourceTransition> branchBTransitions;
};

using TreeTransitionIndex = std::unordered_map<uint64_t, const TreeTransitionEntry *>;

struct NondetBranchSourceSample {
    uint64_t seq{0};
    uint64_t transitionSeq{0};
    std::string xml;
    uintptr_t sourceStateHash{0};
    uintptr_t targetKeyHash{0};
    uintptr_t targetStateHash{0};
    std::vector<int> resolvedNodeStableIds;
#if defined(FASTBOT_HAS_PUGIXML) && FASTBOT_HAS_PUGIXML
    gui_tree::GUITreePtr sourceGuiSnapshot{};
#endif
};

struct NondetBranchInputStats {
    size_t logN{0};
    size_t orderedCount{0};
    size_t filteredByActivityOrPair{0};
    size_t filteredByTarget{0};
    size_t filteredBySnapshot{0};
    size_t filteredBySourceStateKey{0};
};

struct NondetBranchBuildResult {
    bool nstSeqFound{false};
    std::vector<NondetTreeTransitionBranchPair> branchPairs;
};

struct NondetBranchCollectionResult {
    NondetBranchInputStats inputStats;
    size_t orderedCount{0};
    bool nstSeqFound{false};
    std::vector<NondetTreeTransitionBranchPair> branchPairs;
};

struct NondetActionRefineTransitionContext {
    uintptr_t actionPredicateSourceStateHash{0};
    bool selectedTargetBoundsFound{false};
    Rect selectedTargetBounds{};
};

void buildTreeTransitionIndex(const std::vector<TreeTransitionEntry> &transitionLog,
                              const std::string &sourceActivity,
                              uintptr_t actionHash,
                              TreeTransitionIndex *outIndex);

bool findTreeTransitionTargetBoundsBySeq(const std::vector<TreeTransitionEntry> &transitionLog,
                                         uint64_t transitionSeq,
                                         Rect *outBounds);

void collectOrderedNondetBranchSourceSamples(
    const std::vector<ApeTransitionEntry> &transitionLog,
    size_t transitionLogWriteIndex,
    const std::string &sourceActivity,
    uintptr_t sourceKeyHash,
    uintptr_t actionHash,
    const std::unordered_set<uintptr_t> &targetKeyHashes,
    const TreeTransitionIndex &treeBySeq,
    const std::function<bool(uintptr_t)> &acceptSourceStateHash,
    std::vector<NondetBranchSourceSample> *outOrdered,
    NondetBranchInputStats *outStats);

void buildNondetTreeTransitionBranchPairsFromOrderedSamples(
    const std::vector<NondetBranchSourceSample> &ordered,
    uint64_t nstTransitionSeq,
    std::vector<NondetTreeTransitionBranchPair> *outPairs);

NondetBranchBuildResult buildNondetBranchPairs(const std::vector<NondetBranchSourceSample> &ordered,
                                               uint64_t nstTransitionSeq);

NondetBranchCollectionResult collectNondetBranchPairsForRefine(
    const std::vector<ApeTransitionEntry> &transitionLog,
    size_t transitionLogWriteIndex,
    const std::vector<TreeTransitionEntry> &treeTransitionLog,
    const std::string &sourceActivity,
    uintptr_t sourceKeyHash,
    uintptr_t actionHash,
    const std::unordered_set<uintptr_t> &targetKeyHashes,
    const std::function<bool(uintptr_t)> &acceptSourceStateHash,
    uint64_t nstTransitionSeq);

NondetActionRefineTransitionContext buildNondetActionRefineTransitionContext(
    const std::vector<NondetTreeTransitionBranchPair::SourceTransition> &branchATransitions,
    const std::vector<NondetTreeTransitionBranchPair::SourceTransition> &branchBTransitions,
    const std::vector<TreeTransitionEntry> &treeTransitionLog);

void sortNondetTreeTransitionBranchPairs(std::vector<NondetTreeTransitionBranchPair> *pairs);

} // namespace fastbotx

#endif // FASTBOTX_MODEL_TREE_TRANSITION_H_
