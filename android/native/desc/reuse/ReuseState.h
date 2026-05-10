/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @file ReuseState.h
 *
 * UI state representation for reuse-oriented exploration: builds from an accessibility `Element` tree into
 * `RichWidget` metadata, hashes, actions, and optional graph / merged-state bookkeeping.
 *
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef ReuseState_H_
#define ReuseState_H_

#include "State.h"
#include "RichWidget.h"
#include "../StateStructure.h"
#include "../Base.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace fastbotx {

    class ReuseState;
    typedef std::shared_ptr<ReuseState> ReuseStatePtr;

    class MergedState;
    typedef std::shared_ptr<MergedState> MergedStatePtr;

    /** Lightweight transition in a small exploration subgraph (next state, label action, visit flag). */
    struct MiniGraphEdge {
        ReuseStatePtr next;
        ActionPtr action;
        bool isVisited;
    };

    /** Recorded transition in the activity-level state graph (deduped by action+next hash). */
    struct StateGraphEdge {
        ActionPtr action;
        ReuseStatePtr nextState;
        int remainTimes{1};
        bool isDrawn{false};
        uintptr_t hash{};
        int whichWidget{-1};
        double createdTime{0};
    };

    /**
     * @brief ReuseState class for building states with RichWidgets
     *
     * ReuseState extends State to use RichWidget instead of regular Widget.
     * RichWidget contains additional information for reuse-based algorithms.
     *
     * This class builds a state that holds all RichWidgets and their associated
     * actions, optimized for reuse-based reinforcement learning algorithms.
     */
    class ReuseState : public State {
    public:
        /**
         * @brief Factory method to create a ReuseState from Element and activity name
         *
         * @param element Root Element of the UI hierarchy
         * @param activityName Activity name string pointer
         * @param mask Widget key mask for dynamic state abstraction (default: DefaultWidgetKeyMask)
         * @return Shared pointer to created ReuseState
         */
        static std::shared_ptr<ReuseState>
        create(const ElementPtr &element, const stringPtr &activityName,
               WidgetKeyMask mask = DefaultWidgetKeyMask);

        // --- Optional MergedState overlay: textual summaries, similarity, and graph edges ---

        void setMergedState(MergedStatePtr mergedState) { _mergedState = std::move(mergedState); }

        MergedStatePtr getMergedState() const { return _mergedState; }

        std::vector<MiniGraphEdge> _miniEdges;

        const std::vector<StateGraphEdge> &getEdges() const { return _edges; }

        void addSubSequentState(const ReuseStatePtr &state);

        /** Last action chosen while at this state; labels the outgoing edge to the successor in the reuse graph. */
        ActionPtr _actionToPerform;

        std::vector<StateGraphEdge> _edges;

        MiniGraphEdge *getUnvisitedMiniEdge();

        void addMiniEdge(MiniGraphEdge edge);

        /** Human-readable page summary for merged-state / LLM context (not used as RL state identity). */
        std::string getStateDescriptionForMergedState() const;

        float computeSimilarityForMergedState(const ReuseStatePtr &target) const;

        /** Widget-overlap similarity vs. another state; used for navigation-style matching. */
        float computeSimilarity(const ReuseStatePtr &target) const;

        ActionPtr findSimilarAction(const ActionPtr &origin);

        ElementPtr findElementById(int id) const;

        WidgetPtr getWidgetForElement(const ElementPtr &element) const;
        
        int getStableElementIdForWidget(const WidgetPtr &widget) const;

        /**
         * Locates `target` among representatives and merged duplicates.
         * Returns `-1` if it matches the primary row in `_widgets`, a non-negative index into the merge group,
         * or a negative code if not found (see implementation).
         */
        int findWhichWidget(WidgetPtr target) const;

        WidgetPtr findWidgetByHashAndLocation(uintptr_t hash, int location) const;

        std::vector<WidgetPtr> getAllWidgets() const;

        std::vector<ActivityStateActionPtr> findActionsByWidget(WidgetPtr widget) const;

        /** Map stable element id and `ActionType` to an index in `State::getActions()`. */
        int findActionByElementId(int elementId, int actionType);

        /** Widgets present here whose hash does not appear in `target` (diff between two snapshots). */
        std::vector<WidgetPtr> diffWidgets(const ReuseStatePtr &target);

        /** Widgets that expose actions or visible text / content-description; used when summarizing an activity. */
        std::vector<WidgetPtr> getValuableWidgets() const;

    protected:
        virtual void buildStateFromElement(WidgetPtr parentWidget, ElementPtr element);

        virtual void buildHashForState();

        virtual void buildActionForState();

        virtual void mergeWidgetsInState();

        /**
         * @brief Get max widgets per model action (for α / Action Refinement).
         * ReuseState stores full group in _mergedWidgets, so max is max of group sizes.
         */
        size_t getMaxWidgetsPerModelAction() const override;

#if DYNAMIC_STATE_ABSTRACTION_ENABLED
        /**
         * @brief Get state hash as if computed under the given widget key mask (for coarsening).
         */
        uintptr_t getHashUnderMask(WidgetKeyMask mask) const override;
        /**
         * @brief Number of distinct widget hashes under the given mask (for "skip Text if would explode").
         */
        size_t getUniqueWidgetCountUnderMask(WidgetKeyMask mask) const override;
#endif

        explicit ReuseState(stringPtr activityName);

        ReuseState();

        virtual void buildState(const ElementPtr &element);

        virtual void buildBoundingBox(const ElementPtr &element);

        /// Mask selecting which widget fields participate in hashing and merge keys (dynamic abstraction).
        WidgetKeyMask _widgetKeyMask{DefaultWidgetKeyMask};

    private:
        void buildFromElement(WidgetPtr parentWidget, ElementPtr elem) override;

        /** DFS preorder assigns `Element::stableElementId` and mirrors ids onto widgets for remote action lookup. */
        void rebuildElementIdMaps(const ElementPtr &root);

        MergedStatePtr _mergedState;
        ElementPtr _rootElement;
        mutable StateStructure _stateStructure;
        std::vector<WidgetPtr> _valuableWidgets;
        std::unordered_map<int, ElementPtr> _elementByStableId;
        std::unordered_map<const Element *, WidgetPtr> _elementPtrToWidget;
        std::unordered_map<const Widget *, int> _widgetPtrToStableElementId;

        std::unordered_set<uintptr_t> _existedStateGraphEdges;

        ActivityStateActionPtr findActionByWidgetHash(uintptr_t h, ActionType actionType) const;
    };

}


#endif //ReuseState_H_
