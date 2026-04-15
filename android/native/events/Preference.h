/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Preference_H_
#define Preference_H_

#include <string>
#include <map>
#include <vector>
#include <deque>
#include <queue>
#include "Base.h"
#include "Action.h"
#include "DeviceOperateWrapper.h"
#include "Element.h"
#include "../llm/LlmTypes.h"


namespace fastbotx {

    /// The class for describing the actions that user specified in preference file
    class CustomAction : public Action {
    public:
        OperatePtr toOperate() const override;

        CustomAction();

        explicit CustomAction(ActionType act);

        XpathPtr xpath;
        std::string resourceID;
        std::string contentDescription;
        std::string text;
        std::string classname;
        std::string clickable;  ///< "true" | "false" | "" (skip); used by tree pruning only
        std::string activity;
        std::string command;
        std::vector<float> bounds;
        bool allowFuzzing{true};
        bool clearText{};
        int throttle{};
        int waitTime{};

        ~CustomAction() override = default;
    };

    typedef std::shared_ptr<CustomAction> CustomActionPtr;
    typedef std::vector<CustomActionPtr> CustomActionPtrVec;

    typedef std::map<std::string, std::vector<RectPtr>> StringRectsMap;

    /// Unified avoidance rule: avoid (delete + rect cache) or modify (property overrides only).
    struct AvoidRule {
        std::string activity;
        XpathPtr xpath;
        std::vector<float> bounds;  // size 4 when present
        enum class Action { Avoid, Modify };
        Action action{Action::Avoid};
        std::string resourceID;
        std::string text;
        std::string contentDescription;
        std::string classname;
        std::string clickable;
    };
    typedef std::shared_ptr<AvoidRule> AvoidRulePtr;
    typedef std::vector<AvoidRulePtr> AvoidRulePtrVec;

    /// One step in a max.xpath.actions case: xpath locator + action type + optional text (for CLICK input).
    struct XpathActionStep {
        std::string xpath;
        std::string actionType;  // CLICK, LONG_CLICK, BACK, SCROLL_TOP_DOWN, etc.
        std::string text;       // optional; for CLICK when input is needed
        bool clearText{false};
        int throttle{0};        // per-step override; 0 = use case throttle
        int waitTime{0};        // wait after this step (ms)
    };
    /// One case in max.xpath.actions: activity + prob + throttle + sequence of steps.
    struct XpathCase {
        std::string activity;
        float prob{1.0f};        // 0..1 or 1 = 100%
        int times{1};
        int throttle{0};
        std::vector<XpathActionStep> steps;
    };

    class Preference {
    public:
        Preference();

        static std::shared_ptr<Preference> inst();

        /**
         * Resolve the page: apply black widgets, tree pruning, valid texts, etc. to the element tree.
         * Call this before using the element for state/LLMTaskAgent when custom actions (max.xpath.actions) are not used.
         */
        void resolvePage(const std::string &activity, const ElementPtr &rootXML);

        //@brief patch operate: 1. fuzz input text 2. ..
        void patchOperate(const OperatePtr &opt);

        // load resource mapping file, override the mapings from default file max.mapping,
        void loadMixResMapping(const std::string &resourceMappingPath);

        // load label, text, button valid text dumped from apk
        void loadValidTexts(const std::string &pathOfValidTexts);

        bool checkPointIsInBlackRects(const std::string &activity, int pointX, int pointY);

        void setListenMode(bool listen);

        bool skipAllActionsFromModel() const { return this->_skipAllActionsFromModel; }

        /**
         * @brief Whether to use legacy static reuse state abstraction instead of dynamic abstraction.
         * Controlled via max.config key: max.staticStateAbstraction=true|false.
         */
        bool useStaticReuseAbstraction() const;

        /**
         * When true with FASTBOTX_APE_NATIVE_RECORD, graph merges states by APE StateKey::hash()
         * (in addition to widget-hash dedup inside addState). max.config: max.apeGraphDedupByStateKey=true|false
         */
        bool useApeGraphDedupByStateKey() const;

        /**
         * Greedy Naming lattice steps in Model::buildApeStateKeyFromElementTree (0 = off).
         * max.config: max.apeNamingFixedPointSteps=N (capped, e.g. 256).
         */
        int getApeNamingFixedPointMaxIter() const { return _apeNamingFixedPointMaxIter; }

        /**
         * When false, Model skips periodic APE Naming refinement (non-determinism-driven refine batch).
         * Use with max.apeNamingFixedPointSteps to avoid double refinement. Default true.
         * max.config: max.apeNamingPeriodicRefinement=true|false
         */
        bool useApeNamingPeriodicRefinement() const { return _apeNamingPeriodicRefinement; }
        /**
         * APE-aligned L-input normalization: WebView pruning.
         * max.config: max.apeAlwaysIgnoreWebView=true|false
         */
        bool useApeAlwaysIgnoreWebView() const { return _apeAlwaysIgnoreWebView; }
        /**
         * APE-aligned L-input normalization: ignore actions inside WebView subtree.
         * max.config: max.apeAlwaysIgnoreWebViewAction=true|false
         */
        bool useApeAlwaysIgnoreWebViewAction() const { return _apeAlwaysIgnoreWebViewAction; }
        /**
         * APE-aligned L-input normalization: if a WebView subtree descendant count exceeds this threshold,
         * its children are cleared (keep WebView shell node).
         * max.config: max.apeIgnoreWebViewThreshold=N (0 disables)
         */
        int getApeIgnoreWebViewThreshold() const { return _apeIgnoreWebViewThreshold; }
        /**
         * APE-aligned L-input normalization: extra WebView class patterns.
         *
         * Comma/semicolon-separated patterns. Supported forms:
         * - Exact match:   android.webkit.WebView
         * - Prefix match:  com.tencent.smtt.sdk.WebView*   (suffix '*')
         *
         * Empty list means strict match only (android.webkit.WebView).
         * max.config: max.apeWebViewClassPatterns=<patterns>
         */
        const std::vector<std::string> &getApeWebViewClassPatterns() const {
            return _apeWebViewClassPatterns;
        }
        /**
         * APE-aligned L-input normalization: compute stable pseudo text for icon-only widgets.
         *
         * Since native has no screenshot bitmap, we compute a deterministic hash from widget attributes
         * (class/resource-id/bounds/index) and write it into @text when both text and content-desc are empty.
         * This approximates Java APE computeImageText() behavior.
         * max.config: max.apeComputeImageText=true|false
         */
        bool useApeComputeImageText() const { return _apeComputeImageText; }
        /**
         * APE-aligned L-input normalization: clickable patching (container->child) based on bounds.
         * max.config: max.apePatchGUITree=true|false
         */
        bool useApePatchGUITree() const { return _apePatchGUITree; }
        /**
         * Only used when native is built without pugixml (no GUITree). If true, skip legacy widget/mask batch
         * and run only runApeNamingAbstractionBatch (often empty). With FASTBOT_HAS_PUGIXML, Model always
         * uses the APE naming batch for periodic refinement; this flag is ignored.
         */
        bool useApeNamingOnly() const { return _apeNamingOnly; }

        /**
         * Max refinement hops for periodic actionRefinementWithOptions in Model::refineActivityApeNaming.
         * max.config: max.apeNamingActionRefineHops=N (clamped to [1, 64], default 8).
         */
        int getApeNamingActionRefineHops() const { return _apeNamingActionRefineHops; }

        /**
         * Whether periodic action refinement requires candidate fingerprint to differ from current.
         * max.config: max.apeNamingActionRefineRequireFingerprintChange=true|false (default true).
         */
        bool requireApeNamingActionRefineFingerprintChange() const {
            return _apeNamingActionRefineRequireFingerprintChange;
        }

        /**
         * Predicate mode for periodic action refinement candidate acceptance.
         * Values: fingerprint_change | always_accept | fineness_increase.
         * max.config: max.apeNamingActionRefinePredicateMode=<mode>
         */
        const std::string &getApeNamingActionRefinePredicateMode() const {
            return _apeNamingActionRefinePredicateMode;
        }

        /**
         * Candidate selection mode among acceptable refinement hops.
         * Values: first_accept | deepest_accept.
         * max.config: max.apeNamingActionRefineSelectionMode=<mode>
         */
        const std::string &getApeNamingActionRefineSelectionMode() const {
            return _apeNamingActionRefineSelectionMode;
        }

        /**
         * Minimal activity state count required before periodic action refinement is attempted.
         * max.config: max.apeNamingActionRefineMinActivityStates=N (clamped to [1, 10000], default 2).
         */
        int getApeNamingActionRefineMinActivityStates() const {
            return _apeNamingActionRefineMinActivityStates;
        }

        /**
         * Minimal non-deterministic (stateKey, action) pair count for an activity before
         * periodic action refinement is attempted.
         * max.config: max.apeNamingActionRefineMinNonDetPairs=N (clamped to [1, 10000], default 1).
         */
        int getApeNamingActionRefineMinNonDetPairs() const {
            return _apeNamingActionRefineMinNonDetPairs;
        }

        /**
         * Non-determinism target threshold for APE naming (sourceKey,action)->targetKey fan-out.
         * max.config: max.apeNamingMinNonDetTargets=N (clamped to [2, 100], default MinNonDeterminismCount).
         */
        int getApeNamingMinNonDetTargets() const { return _apeNamingMinNonDetTargets; }

        /**
         * Minimal activity state count delta since last successful APE naming refinement.
         * max.config: max.apeNamingActionRefineMinStateDelta=N (clamped to [1, 10000], default 1).
         */
        int getApeNamingActionRefineMinStateDelta() const { return _apeNamingActionRefineMinStateDelta; }

        /**
         * Minimal non-deterministic pair count delta since last successful APE naming refinement.
         * max.config: max.apeNamingActionRefineMinNonDetPairDelta=N (clamped to [0, 10000], default 0).
         */
        int getApeNamingActionRefineMinNonDetPairDelta() const {
            return _apeNamingActionRefineMinNonDetPairDelta;
        }

        /**
         * Rule profile for periodic APE action refinement baseline.
         * Values: baseline | strict_baseline | java_rule_01_preview | java_rule_02_preview | java_rule_03_preview.
         * max.config: max.apeNamingActionRefineRuleProfile=<profile>
         */
        const std::string &getApeNamingActionRefineRuleProfile() const {
            return _apeNamingActionRefineRuleProfile;
        }

        /**
         * When true (default), refine candidate selection uses transition replay: rebuildTree(candidate) on
         * cached page XML and rank by distinct target StateKey fan-out (APE-style effect check).
         * max.config: max.apeNamingCandidateTransitionReplay=true|false
         */
        bool useApeNamingCandidateTransitionReplay() const { return _apeNamingCandidateTransitionReplay; }

        /**
         * Mirrors APE Config.actionRefinementFirst: when true (default), try a strict multi-branch refine pass
         * before the user ruleProfile/predicate-driven pass in Model::refineActivityApeNaming.
         * max.config: max.apeNamingActionRefinementFirst=true|false
         */
        bool useApeNamingActionRefinementFirst() const { return _apeNamingActionRefinementFirst; }

        /**
         * Mirrors APE Config.enableReplacingNamelet: try Naming.replaceLast on the lattice leaf before extend.
         * max.config: max.apeNamingEnableReplacingNamelet=true|false
         */
        bool useApeNamingEnableReplacingNamelet() const { return _apeNamingEnableReplacingNamelet; }

        /**
         * Mirrors APE Config.maxStatesPerActivity — NamingFactory.refine pre-check on activity state count.
         * max.config: max.apeMaxStatesPerActivity=N
         */
        int getApeMaxStatesPerActivity() const { return _apeMaxStatesPerActivity; }

        /**
         * Mirrors APE Config.maxGUITreesPerState — Java refine second gate uses the same metric as the first
         * (activity state count); kept for config parity with ape.properties.
         * max.config: max.apeMaxGuitreesPerState=N
         */
        int getApeMaxGuitreesPerState() const { return _apeMaxGuitreesPerState; }

        /**
         * Mirrors APE Config.evolveModel: after a new state + XML, run over-abstracted action refinement
         * (merged-widget analogue of resolvedNodes.length > threshold) before selectAction.
         * Default false. max.config: max.apeEvolveModel=true|false
         */
        bool useApeEvolveModel() const { return _apeEvolveModel; }

        /**
         * Merged-widget count must exceed this to trigger evolveModel action refinement (Java: actionRefinmentThreshold).
         * max.config: max.apeActionRefinementThreshold=N (default 1 → need at least 2 concretes)
         */
        int getApeActionRefinementThreshold() const { return _apeActionRefinementThreshold; }

        /**
         * Upper bound on StateKey sorted XPath count after rebuild (Java maxInitialNamesPerStateThreshold analogue).
         * max.config: max.apeMaxInitialNamesPerState=N (clamped, default 256)
         */
        int getApeMaxInitialNamesPerState() const { return _apeMaxInitialNamesPerState; }

        bool isForceUseTextModel() const { return this->_forceUseTextModel; }

        int getForceMaxBlockStateTimes() const { return this->_forceMaxBlockStateTimes; }

        /**
         * Get runtime LLM configuration (OpenAI-compatible HTTP endpoint).
         */
        const LlmRuntimeConfig &getLlmRuntimeConfig() const { return this->_llmRuntimeConfig; }

        /**
         * Whether the LLMDroid pipeline is on (MergedState / GPT exploration modes).
         * Named under max.llm.* for discoverability; orthogonal to max.llm.enabled (LLMTaskAgent HTTP gate).
         * Default false. Only the literal value "true" enables; omitted key or any other value stays off after reload.
         * max.config: max.llm.llmdroid=true|false — see android/docs/MIGRATION_LLMDROID_B2.md.
         */
        bool isLlmdroidEnabled() const { return this->_llmdroidEnabled; }
        /**
         * EXPLORE stage window in seconds for LLMDroid time-mode switching.
         * max.config: max.llm.llmdroid.exploreWindowSec=N (default 120s).
         */
        int getLlmdroidExploreWindowSec() const { return this->_llmdroidExploreWindowSec; }

        /**
         * Native unit tests only (`test_merged_state` / MIGRATION_LLMDROID_B2 §3.5).
         * Production monkey code should rely on max.config `max.llm.llmdroid`, not this.
         */
        void setLlmdroidEnabledForTests(bool enabled) { this->_llmdroidEnabled = enabled; }

        /**
         * Whether to call LLM knowledge_org for same-function grouping (LLMExplorerAgent).
         * Controlled via max.config: max.llm.knowledge=true|false. Only when true is the LLM called.
         */
        bool isLlmKnowledgeEnabled() const { return this->_llmKnowledge; }

        /**
         * Whether to call LLM content_aware_input for editable widgets (LLMExplorerAgent).
         * Controlled via max.config: max.llm.contextAwareInput=true|false. Only when true is the LLM called.
         */
        bool isLlmContextAwareInputEnabled() const { return this->_llmContextAwareInput; }
        /**
         * Whether to enable advanced reuse-based decision tuning (loop avoidance, coverage bias).
         * Controlled via max.config: max.reuse.decisionTuning=true|false.
         */
        bool isReuseDecisionTuningEnabled() const { return this->_reuseDecisionTuning; }

        /**
         * Whether to enable loading/saving dynamic state abstraction policy (statekey.json).
         * Controlled via max.config: max.stateAbstractionPolicy=true|false.
         */
        bool isStateAbstractionPolicyEnabled() const { return this->_stateAbstractionPolicyEnabled; }

        /**
         * Load LLM task configurations from external file.
         * This is typically called from loadConfigs().
         */
        void loadLlmTasks();

        /**
         * Match all configured LLM tasks for the current activity and page,
         * then randomly pick one whose checkpoint XPath matches the given root XML.
         *
         * @param activity Current activity name.
         * @param rootXML  Current page root element.
         * @return A matched LlmTaskConfigPtr or nullptr if no task matches.
         */
        LlmTaskConfigPtr matchLlmTask(const std::string &activity,
                                      const ElementPtr &rootXML);

        /** True iff this task is still allowed to start (run count < max_times when max_times > 0). */
        bool canStartLlmTask(const LlmTaskConfigPtr &cfg) const;
        /** Call when an LLM session is actually started for this task (so max_times is counted per session start, not per match). */
        void incrementLlmTaskRunCount(const LlmTaskConfigPtr &cfg);

        /**
         * Get the next custom action from max.xpath.actions for the current activity and page.
         * When a case is in progress, returns the next step (element located by xpath); otherwise
         * may start a new case with probability prob. BACK/SCROLL do not require xpath match.
         */
        ActionPtr getCustomActionFromXpath(const std::string &activity,
                                          const ElementPtr &rootXML);

        ~Preference();

    protected:

        void deMixResMapping(const ElementPtr &rootXML);

        enum class ResolveRulePhase : uint8_t {
            ModifyOnly = 0,
            AvoidOnly = 1,
        };

        /// Single-pass resolve: avoid (delete + rect cache) + modify (property overrides), deMixResMapping, cachePageTexts, pruningValidTexts, then recurse.
        void resolveElementWithAvoid(const ElementPtr &element,
                                     const std::string &activity,
                                     ResolveRulePhase phase);

        void pruningValidTexts(const ElementPtr &element);

        // recursive
        void
        findMatchedElements(std::vector<ElementPtr> &outElements, const XpathPtr &xpathSelector,
                            const ElementPtr &elementXML);

        // Performance optimization: Find first matched element (early termination)
        // Returns first matching element or nullptr if none found
        ElementPtr findFirstMatchedElement(const XpathPtr &xpathSelector,
                                          const ElementPtr &elementXML);

        void cachePageTexts(const ElementPtr &rootElement);

        void loadConfigs();

        void loadBaseConfig();

        void loadAvoidRules();

        void loadXpathActions();

        void loadWhiteBlackList();

        void loadInputTexts();

    private:

        static std::shared_ptr<Preference> _preferenceInst;

        std::vector<std::string> _whiteList;
        std::vector<std::string> _blackList;

        std::vector<std::string> _inputTexts;
        std::vector<std::string> _fuzzingTexts;
        std::deque<std::string> _pageTextsCache;

        AvoidRulePtrVec _avoidRules;
        std::map<std::string, AvoidRulePtrVec> _avoidRulesByActivity;

        std::map<std::string, std::string> _resMapping;
        std::map<std::string, std::string> _resMixedMapping;

        bool _randomInputText;
        bool _doInputFuzzing;

        std::set<std::string> _validTexts;
        bool _pruningValidTexts;
        bool _skipAllActionsFromModel;
        bool _useStaticReuseAbstraction{};
        bool _useApeGraphDedupByStateKey{};
        int _apeNamingFixedPointMaxIter{0};
        bool _apeNamingPeriodicRefinement{true};
        // Default true: prefer APE naming dynamic abstraction batch over legacy widget/mask batch.
        bool _apeNamingOnly{true};
        int _apeNamingActionRefineHops{8};
        bool _apeNamingActionRefineRequireFingerprintChange{true};
        std::string _apeNamingActionRefinePredicateMode{"fingerprint_change"};
        std::string _apeNamingActionRefineSelectionMode{"first_accept"};
        int _apeNamingActionRefineMinActivityStates{2};
        int _apeNamingActionRefineMinNonDetPairs{1};
        int _apeNamingMinNonDetTargets{MinNonDeterminismCount};
        int _apeNamingActionRefineMinStateDelta{1};
        int _apeNamingActionRefineMinNonDetPairDelta{0};
        std::string _apeNamingActionRefineRuleProfile{"baseline"};
        bool _apeNamingCandidateTransitionReplay{true};
        /// Default true, same as Java Config.actionRefinementFirst.
        bool _apeNamingActionRefinementFirst{true};
        /// Default false, same as Java Config.enableReplacingNamelet.
        bool _apeNamingEnableReplacingNamelet{false};
        /// Default 10 / 20, same as Java Config.maxStatesPerActivity / maxGUITreesPerState.
        int _apeMaxStatesPerActivity{10};
        int _apeMaxGuitreesPerState{20};
        /// Default off; Java Config.evolveModel drives preEvolveModel / actionRefinement on over-abstracted actions.
        bool _apeEvolveModel{false};
        /// Default 1: merged group size must be > 1 (i.e. ≥2 widgets) to trigger evolve refinement.
        int _apeActionRefinementThreshold{1};
        int _apeMaxInitialNamesPerState{256};
        // APE-aligned L-input normalization switches (WebView + clickable patch).
        bool _apeAlwaysIgnoreWebView{false};
        bool _apeAlwaysIgnoreWebViewAction{false};
        int _apeIgnoreWebViewThreshold{64};
        std::vector<std::string> _apeWebViewClassPatterns;
        bool _apeComputeImageText{false};
        bool _apePatchGUITree{true};
        bool _forceUseTextModel{};
        int _forceMaxBlockStateTimes{};
        RectPtr _rootScreenSize;

        /// LLM task configurations loaded from external file.
        std::vector<LlmTaskConfigPtr> _llmTasks;
        /// Per-task run count (key = activity + "|" + checkpointXpathString) for maxTimes limit.
        std::map<std::string, int> _llmTaskRunCount;
        /// Last activity passed to matchLlmTask; used to detect activity switch and clear run counts when resetCount is true.
        std::string _lastLlmActivity;

        /// Runtime LLM HTTP configuration loaded from base config.
        LlmRuntimeConfig _llmRuntimeConfig;

        /// max.llm.llmdroid: LLMDroid B2 pipeline (default off). Invalid/missing value treated as false.
        bool _llmdroidEnabled{false};
        /// max.llm.llmdroid.exploreWindowSec: time-mode EXPLORE window in seconds (default 120).
        int _llmdroidExploreWindowSec{120};

        /// max.llm.knowledge: when true, LLMExplorerAgent calls knowledge_org for same-function grouping.
        bool _llmKnowledge{false};
        /// max.llm.contextAwareInput: when true, LLMExplorerAgent calls content_aware_input for editable widgets.
        bool _llmContextAwareInput{false};
        /// max.reuse.decisionTuning: when true, enable advanced reuse-based decision tuning (loop avoidance, coverage bias).
        bool _reuseDecisionTuning{false};
        /// max.stateAbstractionPolicy: when true, load/save dynamic state abstraction policy (statekey.json).
        bool _stateAbstractionPolicyEnabled{false};

        /// max.xpath.actions: cases and current execution state.
        std::vector<XpathCase> _xpathActionCases;
        /// Remaining trigger count per case (times in config); when 0 the case is not chosen.
        std::vector<int> _xpathCaseRemainingTimes;
        int _currentXpathCaseIdx{-1};
        int _currentXpathStepIdx{0};

        static std::string loadFileContent(const std::string &fileAbsolutePath);

        StringRectsMap _cachedBlackWidgetRects;
        /// Bounds-only avoid rects for current resolvePage(activity), used to delete elements whose center is inside.
        std::vector<RectPtr> _currentBoundsOnlyAvoidRects;

    public:
        static std::string InvalidProperty;
        // static configs for android
        static std::string DefaultResMappingFilePath;
        static std::string BaseConfigFilePath;      // /sdcard/max.config
        static std::string InputTextConfigFilePath; // /sdcard/max.strings
        static std::string LlmTaskConfigFilePath;   // /sdcard/max.llm.tasks
        static std::string WhiteListFilePath;       // /sdcard/awl.strings
        static std::string BlackListFilePath;       // /sdcard/abl.strings
        static std::string AvoidRulesFilePath;      // /sdcard/max.avoid.rules (unified black + pruning)
        static std::string XpathActionsFilePath;    // /sdcard/max.xpath.actions (custom event sequence)
        static std::string ValidTextFilePath;       // /sdcard/max.valid.strings
        static std::string FuzzingTextsFilePath;    // /sdcard/max.fuzzing.strings
        static std::string PackageName;
    };

    typedef std::shared_ptr<Preference> PreferencePtr;

};

#endif //Preference_H_
