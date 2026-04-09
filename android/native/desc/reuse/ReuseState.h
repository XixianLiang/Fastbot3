/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
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

    struct MiniGraphEdge {
        ReuseStatePtr next;
        ActionPtr action;
        bool isVisited;
    };

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

        // --- LLMDroid / MergedState overlay (see MIGRATION_LLMDROID_B2.md §3) ---

        void setMergedState(MergedStatePtr mergedState) { _mergedState = std::move(mergedState); }

        MergedStatePtr getMergedState() const { return _mergedState; }

        std::vector<MiniGraphEdge> _miniEdges;

        const std::vector<StateGraphEdge> &getEdges() const { return _edges; }

        void addSubSequentState(const ReuseStatePtr &state);

        /** LLMDroid RL graph: last action chosen while on this ReuseState (edge label to next). */
        ActionPtr _actionToPerform;

        std::vector<StateGraphEdge> _edges;

        MiniGraphEdge *getUnvisitedMiniEdge();

        void addMiniEdge(MiniGraphEdge edge);

        /** Textual page summary for GPT / MergedState::stateDescription (not RL identity). */
        std::string getStateDescriptionForMergedState() const;

        /**
         * Similarity for MergedState merge only (LLMDroid-style widget overlap).
         * Do not use for APE or RL clustering; see {@link computeSimilarityForMergedState} name.
         */
        float computeSimilarityForMergedState(const ReuseStatePtr &target) const;

        /** Widget-overlap similarity for navigation {@link guideCheck} (LLMDroid). */
        float computeSimilarity(const ReuseStatePtr &target) const;

        ActionPtr findSimilarAction(const ActionPtr &origin);

        ElementPtr findElementById(int id) const;

        WidgetPtr getWidgetForElement(const ElementPtr &element) const;
        
        int getStableElementIdForWidget(const WidgetPtr &widget) const;

        int findWhichWidget(WidgetPtr target) const;

        WidgetPtr findWidgetByHashAndLocation(uintptr_t hash, int location) const;

        std::vector<WidgetPtr> getAllWidgets() const;

        std::vector<ActivityStateActionPtr> findActionsByWidget(WidgetPtr widget) const;

        /** Resolve action index in {@link #getActions} from GPT element id + {@link ActionType}. */
        int findActionByElementId(int elementId, int actionType);

        /** Widgets in this state whose widget hash is not present in {@code target} (LLMDroid reanalysis). */
        std::vector<WidgetPtr> diffWidgets(const ReuseStatePtr &target);

        /** LLMDroid legacy helper for activity-level graph aggregation. */
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

        /// Widget key mask for dynamic state abstraction (used in buildHashForState and mergeWidgetsInState)
        WidgetKeyMask _widgetKeyMask{DefaultWidgetKeyMask};

    private:
        void buildFromElement(WidgetPtr parentWidget, ElementPtr elem) override;

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
