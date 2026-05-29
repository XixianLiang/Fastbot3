/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 *
 * @file Preference.h
 * @brief Configuration singleton: loads `max.config` and related files, applies UI-tree preprocessing,
 *        and exposes feature toggles for exploration, naming, and optional LLM-assisted modes.
 *        External keys often keep the `max.ape*` prefix for compatibility with shared tooling.
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

    /// User-defined action template parsed from preference files (xpath, bounds, shell command, throttling).
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

    /**
     * Global settings and XML preprocessing. Thread-safety follows `inst()` usage in the rest of the codebase.
     */
    class Preference {
    public:
        Preference();

        static std::shared_ptr<Preference> inst();

        /**
         * Preprocess the accessibility XML tree: avoid/modify rules, WebView policies, clickable patching,
         * optional pseudo-text for icon buttons, and valid-text pruning. Call before building `State` / tasks.
         */
        void resolvePage(const std::string &activity, const ElementPtr &rootXML);

        /** Mutates an executed operate (e.g. fuzzed input text) according to loaded fuzzing settings. */
        void patchOperate(const OperatePtr &opt);

        /** Load resource id aliasing from the given path (overrides entries from the default `max.mapping`). */
        void loadMixResMapping(const std::string &resourceMappingPath);

        /** Load allowed vocabulary strings (labels, buttons) used when pruning non-listed text nodes. */
        void loadValidTexts(const std::string &pathOfValidTexts);

        bool checkPointIsInBlackRects(const std::string &activity, int pointX, int pointY);

        void setListenMode(bool listen);

        bool skipAllActionsFromModel() const { return this->_skipAllActionsFromModel; }

        /**
         * @brief Whether to use legacy static reuse state abstraction instead of dynamic abstraction.
         * Controlled via max.config key: max.staticStateAbstraction=true|false.
         */
        bool useStaticReuseAbstraction() const;

        /** When true, graph logic may collapse transitions by identical StateKey (max.apeGraphDedupByStateKey). */
        bool useApeGraphDedupByStateKey() const;

        /** Native APE parity keeps StateKey-time naming fixed-point refinement disabled. */
        int getApeNamingFixedPointMaxIter() const { return _apeNamingFixedPointMaxIter; }

        /**
         * When false, Model skips periodic aming refinement (non-determinism-driven refine batch).
         * Default true.
         * max.config: max.apeNamingPeriodicRefinement=true|false
         */
        bool useApeNamingPeriodicRefinement() const { return _apeNamingPeriodicRefinement; }
        /**
         * UI-tree normalization: WebView pruning.
         * max.config: max.apeAlwaysIgnoreWebView=true|false
         */
        bool useApeAlwaysIgnoreWebView() const { return _apeAlwaysIgnoreWebView; }
        /**
         * UI-tree normalization: ignore actions inside WebView subtree.
         * max.config: max.apeAlwaysIgnoreWebViewAction=true|false
         */
        bool useApeAlwaysIgnoreWebViewAction() const { return _apeAlwaysIgnoreWebViewAction; }
        /**
         * UI-tree normalization: if a WebView subtree descendant count exceeds this threshold,
         * its children are cleared (keep WebView shell node).
         * max.config: max.apeIgnoreWebViewThreshold=N (0 disables)
         */
        int getApeIgnoreWebViewThreshold() const { return _apeIgnoreWebViewThreshold; }
        /**
         * UI-tree normalization: extra WebView class patterns.
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
         * UI-tree normalization: compute stable pseudo text for icon-only widgets.
         *
         * Without bitmap OCR, native builds a deterministic hash from widget attributes (class, resource id,
         * bounds, index) and writes it into @text when both text and content-desc are empty (reference parity).
         * Default true. max.config: max.apeComputeImageText=true|false
         */
        bool useApeComputeImageText() const { return _apeComputeImageText; }
        /**
         * UI-tree normalization: clickable patching (container->child) based on bounds.
         * max.config: max.apePatchGUITree=true|false
         */
        bool useApePatchGUITree() const { return _apePatchGUITree; }
        /**
         * Mirrors Config.ignoreEmpty — ignore nodes with empty/invalid bounds during naming.
         * max.config: max.apeIgnoreEmpty=true|false (default true)
         */
        bool useApeIgnoreEmpty() const { return _apeIgnoreEmpty; }
        /**
         * Mirrors Config.ignoreOutOfBounds — ignore nodes outside root bounds during naming.
         * max.config: max.apeIgnoreOutOfBounds=true|false (default true)
         */
        bool useApeIgnoreOutOfBounds() const { return _apeIgnoreOutOfBounds; }
        /**
         * Mirrors Config.excludeEmptyChild — when building GUITree from UI XML, omit placeholder / empty-bounds
         * leaf nodes (Accessibility null-slot analogue).
         * max.config: max.apeExcludeEmptyChild=true|false (default true)
         */
        bool useApeExcludeEmptyChild() const { return _apeExcludeEmptyChild; }
        /**
         * Mirrors Config.excludeInvisibleNode — when building GUITree from UI XML, omit subtrees that are not
         * visible to the user (@visible-to-user=\"false\", or visibility gone/invisible).
         * max.config: max.apeExcludeInvisibleNode=true|false (default true)
         */
        bool useApeExcludeInvisibleNode() const { return _apeExcludeInvisibleNode; }
        /**
         * Only used when native is built without pugixml (no GUITree). If true, skip legacy widget/mask batch
         * and run only the naming abstraction batch (often empty). With FASTBOT_HAS_PUGIXML, Model always
         * uses the naming batch for periodic refinement; this flag is ignored.
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
         * Values: first_accept | deepest_accept. Non-deterministic refinement sorts candidates with
         * NamingFactory::comparator and typically accepts the first match in Model; this key mainly affects
         * other naming-factory batch paths.
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
         * Non-determinism target threshold for naming (sourceKey,action)->targetKey fan-out.
         * max.config: max.apeNamingMinNonDetTargets=N (clamped to [2, 100], default MinNonDeterminismCount).
         */
        int getApeNamingMinNonDetTargets() const { return _apeNamingMinNonDetTargets; }

        /**
         * Minimal activity state count delta since last successful naming refinement.
         * max.config: max.apeNamingActionRefineMinStateDelta=N (clamped to [1, 10000], default 1).
         */
        int getApeNamingActionRefineMinStateDelta() const { return _apeNamingActionRefineMinStateDelta; }

        /**
         * Minimal non-deterministic pair count delta since last successful naming refinement.
         * max.config: max.apeNamingActionRefineMinNonDetPairDelta=N (clamped to [0, 10000], default 0).
         */
        int getApeNamingActionRefineMinNonDetPairDelta() const {
            return _apeNamingActionRefineMinNonDetPairDelta;
        }

        /**
         * Rule profile for periodic action refinement baseline.
         * Values: baseline | strict_baseline | java_rule_01_preview | java_rule_02_preview | java_rule_03_preview
         * (legacy identifier strings for preview rule packs).
         * max.config: max.apeNamingActionRefineRuleProfile=<profile>
         */
        const std::string &getApeNamingActionRefineRuleProfile() const {
            return _apeNamingActionRefineRuleProfile;
        }

        /**
         * When true (default), refine candidate selection uses transition replay: rebuildTree(candidate) on
         * cached page XML and rank by distinct target StateKey fan-out (effect check).
         * max.config: max.apeNamingCandidateTransitionReplay=true|false
         */
        bool useApeNamingCandidateTransitionReplay() const { return _apeNamingCandidateTransitionReplay; }

        /**
         * Mirrors Config.actionRefinementFirst: when true (default), try a strict multi-branch refine pass
         * before the user ruleProfile/predicate-driven pass in Model::refineActivityApeNaming.
         * max.config: max.apeNamingActionRefinementFirst=true|false
         */
        bool useApeNamingActionRefinementFirst() const { return _apeNamingActionRefinementFirst; }

        /**
         * Mirrors Config.enableReplacingNamelet: try Naming.replaceLast on the lattice leaf before extend.
         * max.config: max.apeNamingEnableReplacingNamelet=true|false
         */
        bool useApeNamingEnableReplacingNamelet() const { return _apeNamingEnableReplacingNamelet; }

        /**
         * Mirrors Config.maxStatesPerActivity — NamingFactory.refine pre-check on activity state count.
         * max.config: max.apeMaxStatesPerActivity=N
         */
        int getApeMaxStatesPerActivity() const { return _apeMaxStatesPerActivity; }

        /**
         * Mirrors Config.maxGUITreesPerState — NamingFactory.refine second gate bounds GUI trees recorded
         * on the representative source state (GUI trees attached to that state).
         * max.config: max.apeMaxGuitreesPerState=N
         */
        int getApeMaxGuitreesPerState() const { return _apeMaxGuitreesPerState; }

        /**
         * Mirrors Config.evolveModel: after a new state + XML, run over-abstracted action refinement
         * (merged-widget analogue of resolvedNodes.length > threshold) before selectAction.
         * Default true. max.config: max.apeEvolveModel=true|false
         */
        bool useApeEvolveModel() const { return _apeEvolveModel; }

        /**
         * Merged-widget / resolved-node count must exceed this to trigger evolveModel action refinement
         * (Config.actionRefinementThreshold). max.config: max.apeActionRefinementThreshold=N (default 3, reference default)
         */
        int getApeActionRefinementThreshold() const { return _apeActionRefinementThreshold; }

        /**
         * Upper bound on StateKey sorted XPath count after rebuild (maxInitialNamesPerStateThreshold).
         * max.config: max.apeMaxInitialNamesPerState=N (clamped, default 20, reference default)
         */
        int getApeMaxInitialNamesPerState() const { return _apeMaxInitialNamesPerState; }

        bool isForceUseTextModel() const { return this->_forceUseTextModel; }

        int getForceMaxBlockStateTimes() const { return this->_forceMaxBlockStateTimes; }

        /**
         * Get runtime LLM configuration (OpenAI-compatible HTTP endpoint).
         */
        const LlmRuntimeConfig &getLlmRuntimeConfig() const { return this->_llmRuntimeConfig; }

        /**
         * Enables merged-state / planner-heavy exploration (GPTAgent overview, guide, test-function).
         * Requires both max.llm.llmdroid=true and max.llm.enabled=true (HTTP LlmClient).
         * Default false. Only the literal value "true" on llmdroid enables the feature flag; omitted key stays off.
         * max.config: max.llm.llmdroid=true|false
         */
        bool isLlmdroidEnabled() const {
            return this->_llmdroidEnabled && this->_llmRuntimeConfig.enabled;
        }
        /** Raw max.llm.llmdroid flag before the max.llm.enabled gate. */
        bool isLlmdroidConfigRequested() const { return this->_llmdroidEnabled; }
        /**
         * EXPLORE stage window in seconds for time-mode switching when planner pipeline features are on.
         * max.config: max.llm.llmdroid.exploreWindowSec=N (default 120s).
         */
        int getLlmdroidExploreWindowSec() const { return this->_llmdroidExploreWindowSec; }

        /**
         * Test hook to toggle planner pipeline features without rewriting config files.
         * Production runs should prefer max.config `max.llm.llmdroid`.
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
        bool _apeNamingOnly{true};
        int _apeNamingActionRefineHops{8};
        bool _apeNamingActionRefineRequireFingerprintChange{true};
        std::string _apeNamingActionRefinePredicateMode{"fingerprint_change"};
        std::string _apeNamingActionRefineSelectionMode{"first_accept"};
        int _apeNamingActionRefineMinActivityStates{2};
        int _apeNamingActionRefineMinNonDetPairs{1};
        int _apeNamingMinNonDetTargets{2};
        int _apeNamingActionRefineMinStateDelta{1};
        int _apeNamingActionRefineMinNonDetPairDelta{0};
        std::string _apeNamingActionRefineRuleProfile{"baseline"};
        bool _apeNamingCandidateTransitionReplay{true};
        bool _apeNamingActionRefinementFirst{true};
        bool _apeNamingEnableReplacingNamelet{false};
        int _apeMaxStatesPerActivity{10};
        int _apeMaxGuitreesPerState{20};
        bool _apeEvolveModel{true};
        int _apeActionRefinementThreshold{3};
        int _apeMaxInitialNamesPerState{20};
        // UI-tree normalization switches (WebView handling + clickable patch), keyed by max.ape* in config.
        bool _apeAlwaysIgnoreWebView{false};
        bool _apeAlwaysIgnoreWebViewAction{false};
        int _apeIgnoreWebViewThreshold{64};
        std::vector<std::string> _apeWebViewClassPatterns;
        bool _apeComputeImageText{true};
        bool _apePatchGUITree{true};
        bool _apeIgnoreEmpty{true};
        bool _apeIgnoreOutOfBounds{true};
        bool _apeExcludeEmptyChild{true};
        bool _apeExcludeInvisibleNode{true};
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

        /// max.llm.llmdroid: planner / merged-state enrichment pipeline (default off). Invalid/missing => false.
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
        // Default on-device paths when running under Android (see runtime initialization).
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
