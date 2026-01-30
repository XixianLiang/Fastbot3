/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <mutex>
#include <cstring>
#include <climits>
#include "utils.hpp"
#include "Preference.h"
#include "../thirdpart/json/json.hpp"

// Performance optimization: Maximum number of page texts to cache
#define PageTextsMaxCount 300

namespace fastbotx {


    /**
     * @brief Default constructor for CustomAction
     * 
     * Initializes a CustomAction with default values. XPath is set to nullptr.
     */
    CustomAction::CustomAction()
            : Action(), xpath(nullptr) {

    }

    /**
     * @brief Constructor for CustomAction with specified action type
     * 
     * @param act The action type for this custom action
     */
    CustomAction::CustomAction(ActionType act)
            : Action(act), xpath(nullptr) {

    }

    /**
     * @brief Convert CustomAction to Operate object
     * 
     * Converts this CustomAction to an Operate object that can be executed.
     * Sets all relevant fields including bounds, text, throttle, wait time, etc.
     * 
     * @return OperatePtr - Shared pointer to the created Operate object
     * 
     * @note Performance optimizations:
     *       - Avoids unnecessary setText() calls when text/command is empty
     *       - Checks action type first to determine which text to set
     *       - Caches bounds size check to avoid repeated vector operations
     */
    OperatePtr CustomAction::toOperate() const {
        OperatePtr opt = Action::toOperate();
        opt->sid = "customact";
        opt->aid = "customact";
        opt->editable = true;
        
        // Performance: Check action type first to avoid unnecessary setText calls
        // For SHELL_EVENT, use command; otherwise use text
        if (opt->act == ActionType::SHELL_EVENT) {
            // Only call setText if command is not empty
            if (!this->command.empty()) {
                opt->setText(this->command);
            }
        } else {
            // Only call setText if text is not empty
            if (!this->text.empty()) {
                opt->setText(this->text);
            }
        }
        
        // Performance: Cache bounds size check
        const size_t boundsSize = this->bounds.size();
        if (boundsSize >= 4) {
            opt->pos = Rect(static_cast<int>(this->bounds[0]), static_cast<int>(this->bounds[1]),
                            static_cast<int>(this->bounds[2]), static_cast<int>(this->bounds[3]));
        }
        
        opt->clear = this->clearText;
        opt->throttle = static_cast<float>(this->throttle);
        opt->waitTime = this->waitTime;
        opt->adbInput = this->adbInput;
        opt->allowFuzzing = this->allowFuzzing;
        
        return opt;
    }

    /**
     * @brief Default constructor for Xpath
     * 
     * Initializes XPath selector with default values:
     * - index = -1 (ignore index)
     * - operationAND = false (OR mode)
     */
    Xpath::Xpath()
            : index(-1), operationAND(false) {}

    namespace {
        const std::regex &getResourceIDRegex() {
            static const std::regex r("resource-id='(.*?)'");
            return r;
        }
        const std::regex &getTextRegex() {
            static const std::regex r("text='(.*?)'");
            return r;
        }
        const std::regex &getIndexRegex() {
            static const std::regex r("index=(\\d+)");
            return r;
        }
        const std::regex &getContentRegex() {
            static const std::regex r("content-desc='(.*?)'");
            return r;
        }
        const std::regex &getClazzRegex() {
            static const std::regex r("class='(.*?)'");
            return r;
        }
    }

    /**
     * @brief Constructor for Xpath from string
     * 
     * Parses an XPath string and extracts matching criteria:
     * - resource-id: from pattern `resource-id='...'`
     * - text: from pattern `text='...'`
     * - content-desc: from pattern `content-desc='...'`
     * - class: from pattern `class='...'`
     * - index: from pattern `index=123`
     * - operationAND: true if string contains " and " (with spaces) and has multiple '=' signs
     * 
     * @param xpathString The XPath string to parse
     * 
     * @example
     * Xpath("resource-id='com.example:id/button' and text='Click Me'")
     * Xpath("class='android.widget.TextView'")
     * 
     * @note Performance optimizations:
     *       - Uses manual parsing instead of multiple regex searches (single pass)
     *       - More precise " and " detection (with spaces) to avoid false matches
     *       - Counts '=' signs during parsing to avoid separate traversal
     */
    Xpath::Xpath(const std::string &xpathString)
            : Xpath() {
        if (xpathString.empty())
            return;
        this->_xpathStr = xpathString;
        
        // Performance optimization: Manual parsing instead of multiple regex searches
        // This avoids 5 separate regex searches and reduces string operations
        const size_t len = xpathString.length();
        int equalsCount = 0;
        bool hasAndKeyword = false;
        
        // Helper lambda to extract quoted value: key='value'
        auto extractQuotedValue = [&xpathString](const std::string& key, std::string& out) -> bool {
            size_t keyPos = xpathString.find(key);
            if (keyPos == std::string::npos) return false;
            
            size_t quoteStart = keyPos + key.length();
            if (quoteStart >= xpathString.length() || xpathString[quoteStart] != '\'') return false;
            
            quoteStart++; // Skip opening quote
            size_t quoteEnd = xpathString.find('\'', quoteStart);
            if (quoteEnd == std::string::npos) return false;
            
            out = xpathString.substr(quoteStart, quoteEnd - quoteStart);
            return true;
        };
        
        // Extract resource-id='...'
        extractQuotedValue("resource-id='", this->resourceID);
        
        // Extract text='...'
        extractQuotedValue("text='", this->text);
        
        // Extract content-desc='...'
        extractQuotedValue("content-desc='", this->contentDescription);
        
        // Extract class='...'
        extractQuotedValue("class='", this->clazz);
        
        // Extract index=123 (no quotes)
        size_t indexPos = xpathString.find("index=");
        if (indexPos != std::string::npos) {
            size_t numStart = indexPos + 6; // Skip "index="
            if (numStart < len && xpathString[numStart] >= '0' && xpathString[numStart] <= '9') {
                try {
                    size_t numEnd = numStart;
                    while (numEnd < len && xpathString[numEnd] >= '0' && xpathString[numEnd] <= '9') {
                        numEnd++;
                    }
                    long parsedIndex = std::stol(xpathString.substr(numStart, numEnd - numStart));
                    if (parsedIndex >= 0 && parsedIndex <= INT_MAX) {
                        this->index = static_cast<int>(parsedIndex);
                    }
                } catch (...) {
                    // Ignore parsing errors, keep default index = -1
                }
            }
        }
        
        // Performance: Count '=' and detect " and " in single pass
        for (size_t i = 0; i < len; ++i) {
            if (xpathString[i] == '=') {
                equalsCount++;
            }
            // Check for " and " (with spaces) to avoid false matches like "android"
            if (i + 4 < len && 
                xpathString[i] == ' ' && 
                xpathString[i+1] == 'a' && xpathString[i+2] == 'n' && xpathString[i+3] == 'd' && 
                xpathString[i+4] == ' ') {
                hasAndKeyword = true;
            }
        }
        
        // Set operationAND if both conditions are met
        if (hasAndKeyword && equalsCount > 1) {
            this->operationAND = true;
        }
        
        BDLOG(" xpath parsed: res id %s, text %s, index %d, content %s %d",
              this->resourceID.c_str(), this->text.c_str(), this->index,
              this->contentDescription.c_str(), this->operationAND);
    }


    /**
     * @brief Constructor for Preference
     * 
     * Initializes Preference with default values and loads all configuration files.
     * 
     * Default values:
     * - _randomInputText: false
     * - _doInputFuzzing: true
     * - _pruningValidTexts: false
     * - _skipAllActionsFromModel: false
     * - _rootScreenSize: nullptr
     * 
     * Automatically calls loadConfigs() to load all configuration files.
     */
    Preference::Preference()
            : _randomInputText(false), _doInputFuzzing(true), _pruningValidTexts(false),
              _skipAllActionsFromModel(false), _rootScreenSize(nullptr) {
        loadConfigs();
    }

    /**
     * @brief Get the singleton instance of Preference (thread-safe)
     * 
     * Uses std::call_once to ensure thread-safe initialization.
     * The singleton is created on first call and reused for subsequent calls.
     * 
     * @return PreferencePtr - Shared pointer to the singleton Preference instance
     */
    PreferencePtr Preference::inst() {
        static std::once_flag once;
        std::call_once(once, []() { _preferenceInst = std::make_shared<Preference>(); });
        return _preferenceInst;
    }

    PreferencePtr Preference::_preferenceInst = nullptr;

    /**
     * @brief Destructor for Preference
     * 
     * Cleans up all configuration data and cached information.
     */
    Preference::~Preference() {
        this->_resMixedMapping.clear();
        this->_resMapping.clear();
        this->_blackWidgetActions.clear();
        this->_treePrunings.clear();
        this->_inputTexts.clear();
        this->_blackList.clear();
        std::queue<ActionPtr> empty;
        this->_currentActions.swap(empty);
        this->_customEvents.clear();
        this->_validTexts.clear();
    }

    /**
     * @brief Resolve page and get specified custom action
     * 
     * This is the main entry point for page processing. It:
     * 1. Preprocesses the UI tree (black widgets, tree pruning, etc.)
     * 2. Checks for pending custom actions in the queue
     * 3. If queue is empty, matches custom events based on probability
     * 4. Returns the first action from the queue
     * 
     * @param activity Current activity name
     * @param rootXML Root Element of the UI tree
     * 
     * @return ActionPtr - Custom action if available, nullptr otherwise
     * 
     * @note Custom events are matched based on:
     *       - Activity name match
     *       - Random probability < event.prob
     *       - Remaining times > 0
     * 
     * @note Performance optimizations:
     *       - Early activity check to avoid unnecessary random number generation
     *       - Removed redundant empty() check inside loop
     *       - Moved logging after matching checks to reduce overhead
     */
    ActionPtr Preference::resolvePageAndGetSpecifiedAction(const std::string &activity,
                                                           const ElementPtr &rootXML) {
        if (nullptr != rootXML)
            this->resolvePage(activity, rootXML);

        // resolve action
        ActionPtr returnAction = nullptr;
        if (this->_currentActions.empty()) {
            // Performance: Check activity match first to avoid unnecessary random number generation
            // Most events won't match the current activity, so early exit saves computation
            for (const CustomEventPtr &customEvent: this->_customEvents) {
                // Early exit: Check activity match first (fastest check)
                if (customEvent->activity != activity) {
                    continue;
                }
                
                // Early exit: Check times before generating random number
                if (customEvent->times <= 0) {
                    continue;
                }
                
                // Only generate random number if activity matches and times > 0
                float eventRate = randomInt(0, 10) / 10.0;
                
                // Check probability match
                if (eventRate < customEvent->prob) {
                    // Performance: Removed redundant empty() check - we know queue is empty here
                    // (entered this block only if _currentActions.empty() was true)
                    BLOG("custom event matched: %s actions size: %d", activity.c_str(),
                         (int) customEvent->actions.size());
                    
                    // Add all actions to queue
                    for (const auto &matchedAction: customEvent->actions) {
                        this->_currentActions.push(matchedAction);
                    }
                    
                    customEvent->times--;
                    
                    // Performance: Log detailed info only when event matches
                    BLOG("customEvent activities %s, page event is %s, event times %d , rate is %f/%f",
                         customEvent->activity.c_str(), activity.c_str(), 
                         customEvent->times, eventRate, customEvent->prob);
                }
            }
        }
        
        // Process queue if not empty
        if (!this->_currentActions.empty()) {
            BLOG("check custom action queue");
            auto frontAction = this->_currentActions.front();
            this->_currentActions.pop();
            
            // Performance: Cache action type check
            ActionType actionType = frontAction->getActionType();
            if (actionType >= ActionType::CLICK &&
                actionType <= ActionType::SCROLL_RIGHT_LEFT) {
                // android action type
                auto customAction = std::dynamic_pointer_cast<CustomAction>(frontAction);
                // Security: Check if dynamic_cast succeeded
                if (!customAction) {
                    BLOGE("Failed to cast action to CustomAction");
                    return nullptr;
                }
                if (rootXML && !this->patchActionBounds(customAction, rootXML)) {
                    return nullptr; // do nothing when action match failed
                }
                // Security: Check xpath before accessing
                if (customAction->xpath) {
                    BLOG("custom action %s happened", customAction->xpath->toString().c_str());
                }
                BLOG("custom action: %s happened", customAction->toString().c_str());
                return customAction;
            }
        }
        return returnAction;
    }

    /**
     * @brief Patch action bounds by finding matching element in UI tree
     * 
     * Finds the first element matching the action's xpath and sets the action's bounds
     * to match the element's bounds. This is used to convert xpath-based actions to
     * bounds-based actions for execution.
     * 
     * @param action The custom action that needs bounds patching
     * @param rootXML The XML tree of the current page
     * 
     * @return bool - true if bounds were successfully found and set, false otherwise
     * 
     * @note Performance optimizations:
     *       - Uses findFirstMatchedElement for early termination
     *       - Pre-allocates bounds vector to avoid multiple reallocations
     *       - Early validation of xpath and rootXML
     *       - Caches xpath string for error logging
     */
    bool Preference::patchActionBounds(const CustomActionPtr &action, const ElementPtr &rootXML) {
        // Performance: Early validation
        if (nullptr == action || nullptr == rootXML) {
            return false;
        }
        
        // Performance: Early validation of xpath
        if (nullptr == action->xpath) {
            return false;
        }
        
        // Performance optimization: Use findFirstMatchedElement for early termination
        // Since we only need the first matched element, no need to collect all matches
        ElementPtr matchedElement = this->findFirstMatchedElement(action->xpath, rootXML);
        if (matchedElement == nullptr) {
            BLOG("action xpath not found %s", action->xpath->toString().c_str());
            return false;
        }
        
        RectPtr rect = matchedElement->getBounds();
        if (rect == nullptr) {
            BLOGE("action xpath matched but bounds is null %s", action->xpath->toString().c_str());
            return false;
        }
        
        // Performance optimization: Pre-allocate vector to avoid multiple reallocations
        // Using resize + direct assignment is faster than 4 push_back calls
        action->bounds.resize(4);
        action->bounds[0] = static_cast<float>(rect->left);
        action->bounds[1] = static_cast<float>(rect->top);
        action->bounds[2] = static_cast<float>(rect->right);
        action->bounds[3] = static_cast<float>(rect->bottom);
        
        return true;
    }

    /**
     * @brief Patch operate object with input text fuzzing
     * 
     * For editable widgets with empty text, fills in text based on configuration:
     * 1. If _randomInputText is true: use user preset strings (_inputTexts)
     * 2. Otherwise: 50% probability use fuzzing texts (_fuzzingTexts)
     * 3. Otherwise: 35% probability use page texts (_pageTextsCache)
     * 
     * Only applies to CLICK and LONG_CLICK actions on editable widgets.
     * 
     * @param opt The operate object to patch
     * 
     * @note This function is called before executing an action to provide input text
     *       for text input fields.
     * 
     * @note Performance optimizations:
     *       - Early exit for non-editable or non-empty text cases
     *       - Caches text and action type to avoid repeated calls
     *       - Caches vector sizes to avoid repeated size() calls
     *       - Uses string literals instead of strcpy for better performance
     *       - Only generates random numbers when needed
     */
    void Preference::patchOperate(const OperatePtr &opt) {
        // Performance: Early exit if input fuzzing is disabled
        if (!this->_doInputFuzzing) {
            return;
        }

        // Performance: Early exit - check action type first (fastest check)
        ActionType act = opt->act;
        if (act != ActionType::CLICK && act != ActionType::LONG_CLICK) {
            return;
        }

        // Performance: Early exit - check editable flag
        if (!opt->editable) {
            return;
        }

        // Performance: Cache getText() result to avoid repeated calls
        const std::string &currentText = opt->getText();
        if (!currentText.empty()) {
            return; // Text already set, no need to patch
        }

        // Performance: Use const char* instead of strcpy for string literals
        const char* prelog = nullptr;
        bool textSet = false;

        // Priority 1: Use preset strings if enabled and available
        if (this->_randomInputText && !this->_inputTexts.empty()) {
            // Performance: Cache size to avoid repeated size() calls
            const int n = static_cast<int>(this->_inputTexts.size());
            if (n > 0) {
                int randIdx = randomInt(0, n);
                opt->setText(this->_inputTexts[randIdx]);
                prelog = "user preset strings";
                textSet = true;
            }
        } else {
            // Priority 2 & 3: Use fuzzing texts or page texts based on probability
            // Performance: Only generate random number when needed
            int rate = randomInt(0, 100);
            
            // 50% probability: Use fuzzing texts
            if (rate < 50 && !this->_fuzzingTexts.empty()) {
                const int n = static_cast<int>(this->_fuzzingTexts.size());
                if (n > 0) {
                    int randIdx = randomInt(0, n);
                    opt->setText(this->_fuzzingTexts[randIdx]);
                    prelog = "fuzzing text";
                    textSet = true;
                }
            }
            // 35% probability (rate 50-84): Use page texts cache
            else if (rate < 85 && !this->_pageTextsCache.empty()) {
                const int n = static_cast<int>(this->_pageTextsCache.size());
                if (n > 0) {
                    int randIdx = randomInt(0, n);
                    opt->setText(this->_pageTextsCache[randIdx]);
                    prelog = "page text";
                    textSet = true;
                }
            }
        }

        // Performance: Only log when text was actually set
        if (textSet && prelog != nullptr) {
            BLOG("patch %s input text: %s", prelog, opt->getText().c_str());
        }
    }

    /**
     * @brief Resolve page: preprocess UI tree before exploration
     * 
     * Main entry point for page preprocessing. Performs the following operations:
     * 1. Gets and caches root screen size
     * 2. Resolves black widgets (deletes blacklisted controls/regions)
     * 3. Resolves element tree (resource mapping, tree pruning, valid texts)
     * 
     * This function is called once per page before the UI tree is used for exploration.
     * 
     * @param activity Current activity name
     * @param rootXML Root Element of the UI tree
     * 
     * @note Performance: Tree traversal reduced from 3 passes to 2 passes
     *       (resolveBlackWidgets + resolveElement which includes deMixResMapping)
     * 
     * @note Performance optimizations:
     *       - Early validation of rootXML
     *       - Simplified root size caching logic
     *       - Reduced redundant bounds checks
     *       - Cached children reference
     */
    void Preference::resolvePage(const std::string &activity, const ElementPtr &rootXML) {
        // Performance: Early validation
        if (nullptr == rootXML) {
            return;
        }

        BDLOG("preference resolve page: %s black widget %zu tree pruning %zu", activity.c_str(),
              this->_blackWidgetActions.size(), this->_treePrunings.size());

        // Performance: Get and cache root size only if not already cached or if cached size is invalid
        // Fix: Check isEmpty() instead of (left + top) != 0, which was incorrect logic
        if (nullptr == this->_rootScreenSize || this->_rootScreenSize->isEmpty()) {
            RectPtr rootSize = rootXML->getBounds();
            
            // Performance: If root bounds are empty, try to get from first child
            if (!rootSize || rootSize->isEmpty()) {
                // Performance: Cache children reference to avoid repeated getChildren() calls
                const auto &children = rootXML->getChildren();
                if (!children.empty()) {
                    rootSize = children[0]->getBounds();
                }
            }
            
            this->_rootScreenSize = rootSize;
            
            // Performance: Log error only once when root size cannot be determined
            if (!this->_rootScreenSize || this->_rootScreenSize->isEmpty()) {
                BLOGE("%s", "No root size in current page");
            }
        }
        
        // recursively resolve black widgets
        this->resolveBlackWidgets(rootXML, activity);
        // Performance optimization: deMixResMapping is now merged into resolveElement
        // This reduces tree traversal from 3 passes to 2 passes (resolveBlackWidgets + resolveElement)
        // recursively deal all rootXML tree (includes deMixResMapping)
        this->resolveElement(rootXML, activity);
    }

    /**
     * @brief Recursively resolve element and its children
     * 
     * Processes a single Element and recursively processes all its children.
     * Performs the following operations in a single traversal:
     * 1. Resource ID mapping (deMixResMapping): Maps obfuscated resource IDs back to original
     * 2. Page text caching: Collects page texts for input fuzzing
     * 3. Tree pruning: Modifies element properties based on xpath rules
     * 4. Valid text pruning: Marks valid texts and sets clickable if needed
     * 
     * @param element Current Element to process
     * @param activity Current activity name
     * 
     * @note Performance optimization: Merges multiple operations into single traversal
     *       to reduce tree traversal overhead.
     */
    void Preference::resolveElement(const ElementPtr &element, const std::string &activity) {
        if (!element)
            return;
            
        // Performance optimization: Merge deMixResMapping into resolveElement traversal
        // This reduces tree traversal from 3 passes to 2 passes
        // Process resource ID mapping (deMixResMapping logic)
        if (!this->_resMixedMapping.empty()) {
            std::string stringOfResourceID = element->getResourceID();
            if (!stringOfResourceID.empty()) {
                auto iterator = this->_resMixedMapping.find(stringOfResourceID);
                if (iterator != this->_resMixedMapping.end()) {
                    element->reSetResourceID((*iterator).second);
                    BDLOG("de-mixed %s as %s", stringOfResourceID.c_str(), (*iterator).second.c_str());
                }
            }
        }
            
        // Performance optimization: Cache page texts during resolveElement traversal
        // This avoids a separate full tree traversal in cachePageTexts
        if (!element->getText().empty()) {
            if (this->_pageTextsCache.size() > PageTextsMaxCount) {
                for (int i = 0; i < 20 && !this->_pageTextsCache.empty(); i++)
                    this->_pageTextsCache.pop_front();
            }
            this->_pageTextsCache.push_back(element->getText());
        }
        
        // resolve tree pruning
        this->resolveTreePruning(element, activity);
        // pruning Valid Texts
        if (this->_pruningValidTexts)
            this->pruningValidTexts(element);
            
        for (const auto &child: element->getChildren()) {
            this->resolveElement(child, activity);
        }
    }

    /**
     * @brief Resolve and delete black widgets (blacklisted controls/regions)
     * 
     * Black widgets are pre-configured controls or regions that should be avoided
     * during exploration (e.g., logout button). This function:
     * 1. Collects all blackWidgetActions for the current activity
     * 2. Phase 1: Deletes xpath-only black widgets (no bounds specified)
     * 3. Phase 2: Deletes bounds-based black widgets (with bounds)
     * 4. Caches all black widget rects for point checking
     * 
     * @param rootXML Root Element of the UI tree
     * @param activity Current activity name
     * 
     * @note Performance optimization:
     *       - Two-phase processing reduces redundant tree traversals
     *       - Unified caching prevents overwriting previous results
     *       - Early activity filtering reduces processing overhead
     */
    void Preference::resolveBlackWidgets(const ElementPtr &rootXML, const std::string &activity) {
        if (this->_blackWidgetActions.empty() || !rootXML) {
            return;
        }
        
        if (nullptr == this->_rootScreenSize) {
            BLOGE("black widget match failed %s", "No root node in current page");
            return;
        }
        
        // Performance optimization: Collect all blackWidgetActions for current activity first
        // This avoids repeated activity checks in the loop
        std::vector<CustomActionPtr> actionsForActivity;
        for (const CustomActionPtr &blackWidgetAction: this->_blackWidgetActions) {
            if (activity.empty() || blackWidgetAction->activity == activity) {
                actionsForActivity.push_back(blackWidgetAction);
            }
        }
        if (actionsForActivity.empty()) {
            return;
        }
        
        // Performance optimization: Two-phase processing
        // Phase 1: Process xpath-only blackWidgets (no bounds specified)
        // Phase 2: Process bounds-based blackWidgets (with bounds)
        // This reduces redundant tree traversals and simplifies logic
        
        std::vector<RectPtr> allCachedRects;  // Unified cache for all black widget rects
        
        // Phase 1: Process xpath-only blackWidgets
        for (const CustomActionPtr &action: actionsForActivity) {
            if (!action->xpath || action->bounds.size() >= 4) {
                continue; // Skip bounds-based widgets in phase 1
            }
            
            std::vector<ElementPtr> matchedElements;
            this->findMatchedElements(matchedElements, action->xpath, rootXML);
            
            if (!matchedElements.empty()) {
                // Security: Check xpath before accessing (defensive programming)
                if (action->xpath) {
                    BDLOG("black widget xpath %s, matched %d nodes", 
                          action->xpath->toString().c_str(), (int) matchedElements.size());
                } else {
                    BDLOG("black widget (no xpath), matched %d nodes", (int) matchedElements.size());
                }
                
                for (const auto &matchedElement: matchedElements) {
                    if (matchedElement) {
                        BLOG("black widget, delete node: %s depends xpath",
                             matchedElement->getResourceID().c_str());
                        RectPtr bounds = matchedElement->getBounds();
                        if (bounds) {
                            allCachedRects.push_back(bounds);
                        }
                        matchedElement->deleteElement();
                    }
                }
            }
        }
        
        // Phase 2: Process bounds-based blackWidgets
        for (const CustomActionPtr &action: actionsForActivity) {
            if (action->bounds.size() < 4) {
                continue; // Skip xpath-only widgets in phase 2
            }
            
            // Security: Double-check bounds size (defensive programming)
            if (action->bounds.size() < 4) {
                BLOGE("Invalid bounds size: %zu (expected 4)", action->bounds.size());
                continue;
            }
            
            std::vector<float> bounds = action->bounds;
            
            // Convert relative bounds to absolute bounds if needed
            // Security: More accurate check - all bounds should be in [0, 1.1] range for relative coordinates
            bool isRelative = (bounds[0] >= 0.0f && bounds[0] <= 1.1f &&
                              bounds[1] >= 0.0f && bounds[1] <= 1.1f &&
                              bounds[2] >= 0.0f && bounds[2] <= 1.1f &&
                              bounds[3] >= 0.0f && bounds[3] <= 1.1f);
            
            if (isRelative) {
                int rootWidth = this->_rootScreenSize->right;
                int rootHeight = this->_rootScreenSize->bottom;
                bounds[0] = bounds[0] * static_cast<float>(rootWidth);
                bounds[1] = bounds[1] * static_cast<float>(rootHeight);
                bounds[2] = bounds[2] * static_cast<float>(rootWidth);
                bounds[3] = bounds[3] * static_cast<float>(rootHeight);
            }
            
            RectPtr rejectRect = std::make_shared<Rect>(bounds[0], bounds[1], bounds[2], bounds[3]);
            allCachedRects.push_back(rejectRect);
            
            // If xpath is specified, first filter by xpath, then by bounds
            std::vector<ElementPtr> candidates;
            if (action->xpath) {
                // Collect elements matching xpath
                this->findMatchedElements(candidates, action->xpath, rootXML);
                // Security: xpath is already checked, but add defensive check for logging
                BDLOG("black widget xpath %s with bounds, matched %d nodes",
                      action->xpath ? action->xpath->toString().c_str() : "(null)",
                      (int) candidates.size());
            } else {
                // No xpath, check all elements in the reject rect
                rootXML->recursiveElements([&rejectRect](const ElementPtr &child) -> bool {
                    RectPtr b = child->getBounds();
                    return b && rejectRect->contains(b->center());
                }, candidates);
            }
            
            // Delete elements that are in the reject rect
            for (const auto &element: candidates) {
                if (element) {
                    RectPtr elementBounds = element->getBounds();
                    if (elementBounds && rejectRect->contains(elementBounds->center())) {
                        BLOG("black widget, delete node: %s depends bounds",
                             element->getResourceID().c_str());
                        element->deleteElement();
                    }
                }
            }
        }
        
        // Unified cache: Store all black widget rects for this activity
        // This replaces the previous per-action caching which would overwrite previous results
        if (!allCachedRects.empty()) {
            this->_cachedBlackWidgetRects[activity] = allCachedRects;
        }
    }

    /**
     * @brief Check if a point is inside blacklisted rectangles
     * 
     * Checks if the given coordinate point falls within any of the cached black widget
     * rectangles for the specified activity. This is used to prevent clicking on
     * blacklisted regions (e.g., logout button).
     * 
     * @param activity Current activity name
     * @param pointX X coordinate of the point
     * @param pointY Y coordinate of the point
     * 
     * @return bool - true if point is inside any black rect, false otherwise
     * 
     * @note Performance optimization:
     *       - Early return if no cached rects
     *       - Inline coordinate comparison (avoids Point object creation)
     *       - Early exit on first match
     *       - Logging controlled by FASTBOT_LOG_BLACK_RECT_CHECK macro
     * 
     * @note This function is called frequently (before every click action),
     *       so performance is critical.
     */
    bool Preference::checkPointIsInBlackRects(const std::string &activity, int pointX, int pointY) {
        // Performance optimization: Early return if no cached rects for this activity
        auto iter = this->_cachedBlackWidgetRects.find(activity);
        if (iter == this->_cachedBlackWidgetRects.end() || iter->second.empty()) {
            return false;
        }
        
        // Performance optimization: Direct point comparison instead of creating Point object
        // Rect::contains checks: point.x >= left && point.x <= right && point.y >= top && point.y <= bottom
        // We can inline this check to avoid Point object creation
        bool isInsideBlackList = false;
        const std::vector<RectPtr> &rects = iter->second;
        
        // Performance: Use range-based for loop with early exit
        // Most points will not be in black rects, so early exit is common
        for (const auto &rect : rects) {
            if (rect && 
                pointX >= rect->left && pointX <= rect->right &&
                pointY >= rect->top && pointY <= rect->bottom) {
                isInsideBlackList = true;
                break;
            }
        }
        
        // Performance optimization: Only log when enabled (this function is called frequently)
#if FASTBOT_LOG_BLACK_RECT_CHECK
        BLOG("check point [%d, %d] is %s in black widgets", pointX, pointY,
             isInsideBlackList ? "" : "not");
#endif
        return isInsideBlackList;
    }

    /**
     * @brief Resolve tree pruning: modify element properties based on xpath rules
     * 
     * Tree pruning allows modifying element properties (resourceID, text, contentDescription,
     * classname) based on xpath matching rules. This is useful for:
     * - Unifying resource IDs for similar elements
     * - Modifying text content for consistency
     * - Changing class names for better matching
     * 
     * @param elem Current Element to check and potentially modify
     * @param activity Current activity name
     * 
     * @note Performance optimization:
     *       - Uses activity-grouped tree prunings for faster lookup
     *       - Early exit if xpath doesn't match
     *       - Uses != operator instead of compare() for string comparison
     * 
     * @note Only modifies properties that are not set to InvalidProperty
     */
    void Preference::resolveTreePruning(const ElementPtr &elem, const std::string &activity) {
        if (!elem) {
            return;
        }
        
        // Performance optimization: Use activity-grouped tree prunings for faster lookup
        // Only iterate through prunings for the current activity instead of all prunings
        const CustomActionPtrVec *pruningsForActivity = nullptr;
        auto activityIt = this->_treePruningsByActivity.find(activity);
        if (activityIt != this->_treePruningsByActivity.end()) {
            pruningsForActivity = &activityIt->second;
        } else if (this->_treePruningsByActivity.empty() && !this->_treePrunings.empty()) {
            // Fallback: If activity-grouped map is empty, use original vector (for backward compatibility)
            // This should not happen if loadTreePruning is called correctly
            // Build temporary vector for current activity
            static thread_local CustomActionPtrVec tempPrunings;
            tempPrunings.clear();
            for (const auto &prun: this->_treePrunings) {
                if (prun->activity == activity) {
                    tempPrunings.push_back(prun);
                }
            }
            if (!tempPrunings.empty()) {
                pruningsForActivity = &tempPrunings;
            }
        }
        
        if (!pruningsForActivity || pruningsForActivity->empty()) {
            return;
        }
        
        // Performance optimization: Extract common logic to avoid code duplication
        // Process all prunings for the current activity
        for (const auto &prun: *pruningsForActivity) {
            XpathPtr xpath = prun->xpath;
            if (!xpath) {
                continue;
            }
            
            // Performance: Early exit if xpath doesn't match
            if (!elem->matchXpathSelector(xpath)) {
                continue;
            }
            
            BLOG("pruning node %s for xpath: %s", elem->getResourceID().c_str(),
                 xpath->toString().c_str());
            
            // Performance optimization: Use != operator instead of compare for string comparison
            // InvalidProperty is a constant string, so direct comparison is safe
            if (prun->resourceID != InvalidProperty) {
                elem->reSetResourceID(prun->resourceID);
            }
            if (prun->contentDescription != InvalidProperty) {
                elem->reSetContentDesc(prun->contentDescription);
            }
            if (prun->text != InvalidProperty) {
                elem->reSetText(prun->text);
            }
            if (prun->classname != InvalidProperty) {
                elem->reSetClassname(prun->classname);
            }
        }
    }

    /**
     * @brief Prune valid texts: mark valid texts and set clickable
     * 
     * Recursively processes elements to find valid texts (texts that appear in _validTexts set).
     * If a valid text is found:
     * 1. Sets element->validText to the found text
     * 2. If parent is not clickable, sets current element as clickable
     * 
     * Valid texts are typically extracted from APK resources and represent legitimate
     * UI text that should be clickable.
     * 
     * @param element Current Element to process
     * 
     * @note Performance optimization:
     *       - Checks text first, then contentDescription (most elements have text)
     *       - Caches parent lock result to avoid repeated weak_ptr operations
     * 
     * @note Only processes if _pruningValidTexts flag is enabled
     */
    void Preference::pruningValidTexts(const ElementPtr &element) {
        if (!element || this->_validTexts.empty()) {
            return;
        }
        
        // Performance optimization: Check text first, then content description
        // Most elements have text, so checking text first is more efficient
        bool valid = false;
        const std::string &originalTextOfElement = element->getText();
        if (!originalTextOfElement.empty()) {
            // Performance: Use find() which returns iterator, more efficient than count()
            auto textIt = this->_validTexts.find(originalTextOfElement);
            if (textIt != this->_validTexts.end()) {
                element->validText = originalTextOfElement;
                valid = true;
            }
        }
        
        // If text is not valid, try content description
        if (!valid) {
            const std::string &contentDescription = element->getContentDesc();
            if (!contentDescription.empty()) {
                auto contentIt = this->_validTexts.find(contentDescription);
                if (contentIt != this->_validTexts.end()) {
                    element->validText = contentDescription;
                    valid = true;
                }
            }
        }
        
        BDLOG("set valid Text: %s ", element->validText.c_str());
        
        // Performance optimization: Cache parent lock result to avoid repeated weak_ptr operations
        // if we find valid text from text field or content description field,
        // and its parent is clickable, then set it as clickable.
        if (valid) {
            auto parentWeak = element->getParent();
            if (!parentWeak.expired()) {
                auto parentLocked = parentWeak.lock();
                if (parentLocked && !parentLocked->getClickable()) {
                    BDLOG("%s", "set valid Text  set clickable true");
                    element->reSetClickable(true);
                }
            }
        }

        // Recursively process children
        for (const auto &child: element->getChildren()) {
            pruningValidTexts(child);
        }
    }

    /**
     * @brief Find all elements matching the xpath selector
     * 
     * Recursively traverses the UI tree and collects all elements that match
     * the given xpath selector. Results are appended to the output vector.
     * 
     * @param outElements Output parameter: vector to store matched elements
     * @param xpathSelector XPath selector describing matching criteria
     * @param elementXML Root Element to start searching from
     * 
     * @note This function collects ALL matching elements. For better performance
     *       when only the first match is needed, use findFirstMatchedElement().
     */
    /**
     * @brief Find all elements matching the xpath selector
     * 
     * Recursively traverses the UI tree and collects all elements that match
     * the given xpath selector. Results are appended to the output vector.
     * 
     * @param outElements Output parameter: vector to store matched elements
     * @param xpathSelector XPath selector describing matching criteria
     * @param elementXML Root Element to start searching from
     * 
     * @note This function collects ALL matching elements. For better performance
     *       when only the first match is needed, use findFirstMatchedElement().
     * 
     * @note Performance optimizations:
     *       - Early validation of inputs
     *       - Cached children reference to avoid repeated getChildren() calls
     *       - Reduced logging overhead in recursive calls
     */
    void Preference::findMatchedElements(std::vector<ElementPtr> &outElements,
                                         const XpathPtr &xpathSelector,
                                         const ElementPtr &elementXML) {
        // Performance: Early validation - check both inputs
        if (!elementXML || !xpathSelector) {
            // Performance: Skip logging in recursive calls (caller should validate root)
            // Only log if this is the root call (outElements is empty)
            if (outElements.empty()) {
                BLOGE("findMatchedElements: elementXML or xpathSelector is null");
            }
            return;
        }
        
        // Check if current element matches
        if (elementXML->matchXpathSelector(xpathSelector)) {
            // Performance: Use emplace_back for in-place construction (though shared_ptr copy is cheap)
            outElements.push_back(elementXML);
        }

        // Performance: Cache children reference to avoid repeated getChildren() calls
        const auto &children = elementXML->getChildren();
        // Performance: Early exit if no children (common case for leaf nodes)
        if (children.empty()) {
            return;
        }
        
        // Recursively process children
        for (const auto &child: children) {
            findMatchedElements(outElements, xpathSelector, child);
        }
    }

    /**
     * @brief Find the first element matching the xpath selector (early termination)
     * 
     * Recursively searches the UI tree and returns the first element that matches
     * the xpath selector. Stops searching immediately after finding the first match.
     * 
     * @param xpathSelector XPath selector describing matching criteria
     * @param elementXML Root Element to start searching from
     * 
     * @return ElementPtr - First matching element, or nullptr if none found
     * 
     * @note Performance optimization: Early termination avoids unnecessary tree traversal
     *       when only the first match is needed (e.g., patchActionBounds).
     * 
     * @note Performance optimizations:
     *       - Early validation of inputs
     *       - Cached children reference to avoid repeated getChildren() calls
     *       - Early exit for leaf nodes (no children)
     *       - Direct return instead of storing in temporary variable
     */
    ElementPtr Preference::findFirstMatchedElement(const XpathPtr &xpathSelector,
                                                   const ElementPtr &elementXML) {
        // Performance: Early validation - check both inputs
        if (!elementXML || !xpathSelector) {
            return nullptr;
        }
        
        // Check current element first (depth-first search)
        if (elementXML->matchXpathSelector(xpathSelector)) {
            return elementXML;
        }
        
        // Performance: Cache children reference to avoid repeated getChildren() calls
        const auto &children = elementXML->getChildren();
        // Performance: Early exit if no children (common case for leaf nodes)
        if (children.empty()) {
            return nullptr;
        }
        
        // Recursively check children (early termination on first match)
        for (const auto &child: children) {
            // Performance: Direct return instead of storing in temporary variable
            // This avoids an extra assignment and allows compiler optimization
            ElementPtr result = findFirstMatchedElement(xpathSelector, child);
            if (result != nullptr) {
                return result;
            }
        }
        
        return nullptr;
    }

    /**
     * @brief De-mix resource ID mapping: map obfuscated resource IDs back to original
     * 
     * Recursively processes elements to replace obfuscated resource IDs with their
     * original values using the _resMixedMapping lookup table.
     * 
     * @param rootXML Root Element to process
     * 
     * @note This function is now merged into resolveElement() to reduce tree traversals.
     *       It's kept as a separate function for backward compatibility but is no longer
     *       called directly from resolvePage().
     * 
     * @deprecated Use resolveElement() instead, which includes this functionality.
     * 
     * @note Performance optimizations:
     *       - Uses const reference for getResourceID() to avoid string copy
     *       - Cached children reference to avoid repeated getChildren() calls
     *       - Early exit for leaf nodes (no children)
     *       - Optimized iterator access using -> instead of (*)
     */
    void Preference::deMixResMapping(const ElementPtr &rootXML) {
        // Performance: Early validation
        if (!rootXML || this->_resMixedMapping.empty()) {
            return;
        }
        
        // Performance: Use const reference instead of copy to avoid string allocation
        const std::string &stringOfResourceID = rootXML->getResourceID();
        if (!stringOfResourceID.empty()) {
            auto iterator = this->_resMixedMapping.find(stringOfResourceID);
            if (iterator != this->_resMixedMapping.end()) {
                // Performance: Use -> instead of (*) for iterator access
                rootXML->reSetResourceID(iterator->second);
                BDLOG("de-mixed %s as %s", stringOfResourceID.c_str(), iterator->second.c_str());
            }
        }

        // Performance: Cache children reference to avoid repeated getChildren() calls
        const auto &children = rootXML->getChildren();
        // Performance: Early exit if no children (common case for leaf nodes)
        if (children.empty()) {
            return;
        }
        
        // Recursively process children
        for (const auto &child: children) {
            deMixResMapping(child);
        }
    }

    /**
     * @brief Load resource ID mapping file
     * 
     * Loads a resource ID mapping file that maps obfuscated resource IDs back to
     * their original values. This is used for handling code obfuscation.
     * 
     * @param resourceMappingPath Path to the mapping file
     * 
     * @note File format:
     *       - Lines containing ".R.id." are processed
     *       - Format: "0x7f0a0012: id/foo -> :id/bar"
     *       - Creates bidirectional mapping (_resMapping and _resMixedMapping)
     */
    /**
     * @brief Load resource ID mapping file
     * 
     * Loads a resource ID mapping file that maps obfuscated resource IDs back to
     * their original values. This is used for handling code obfuscation.
     * 
     * @param resourceMappingPath Path to the mapping file
     * 
     * @note File format:
     *       - Lines containing ".R.id." are processed
     *       - Format: "0x7f0a0012: id/foo -> :id/bar"
     *       - Creates bidirectional mapping (_resMapping and _resMixedMapping)
     * 
     * @note Performance optimizations:
     *       - Uses reference instead of copy for line iteration
     *       - Early exit for lines without ".R.id."
     *       - Manual string processing to avoid multiple passes
     *       - Reduced string allocations
     */
    void Preference::loadMixResMapping(const std::string &resourceMappingPath) {
        BLOG("loading resource mapping : %s", resourceMappingPath.c_str());
        std::string content = loadFileContent(resourceMappingPath);
        if (content.empty()) {
            return;
        }
        
        std::vector<std::string> lines;
        splitString(content, lines, '\n');
        
        // Performance: Use reference instead of copy to avoid string allocation per iteration
        for (auto &line: lines) {
            // Performance: Early exit for lines that don't contain ".R.id."
            if (line.find(".R.id.") == std::string::npos) {
                continue;
            }
            
            // Parse format like "0x7f0a0012: id/foo -> :id/bar": take substring after first colon after "0x"
            size_t pos0x = line.find("0x");
            if (pos0x != std::string::npos) {
                size_t posColon = line.find(':', pos0x);
                if (posColon != std::string::npos) {
                    // Performance: Modify line in-place instead of creating new string
                    line = line.substr(posColon + 1);
                }
            }
            
            // Performance: Remove spaces and replace ".R.id." with ":id/"
            // Using stringReplaceAll is efficient enough for this use case
            stringReplaceAll(line, " ", "");
            stringReplaceAll(line, ".R.id.", ":id/");
            
            // Find "->" separator
            size_t startPos = line.find("->");
            if (startPos == std::string::npos || startPos + 2 >= line.length()) {
                continue; // Invalid format or no value after "->"
            }
            
            // Performance: Extract substrings directly
            std::string resId = line.substr(0, startPos);
            std::string mixedResid = line.substr(startPos + 2);
            
            // Security: Skip empty mappings to avoid invalid map entries
            if (resId.empty() || mixedResid.empty()) {
                continue;
            }
            
            BDLOG("res id %s mixed to %s", resId.c_str(), mixedResid.c_str());
            this->_resMapping[resId] = mixedResid;
            this->_resMixedMapping[mixedResid] = resId;
        }
    }

    /**
     * @brief Load valid texts from file
     * 
     * Loads a list of valid texts (typically extracted from APK resources).
     * These texts are used by pruningValidTexts() to identify legitimate UI text.
     * 
     * @param pathOfValidTexts Path to the valid texts file
     * 
     * @note File format:
     *       - Regular text: one text per line
     *       - String resources: "String #123: actual_text" format
     *       - Automatically enables _pruningValidTexts flag if file is loaded
     */
    /**
     * @brief Load valid texts from file
     * 
     * Loads a list of valid texts (typically extracted from APK resources).
     * These texts are used by pruningValidTexts() to identify legitimate UI text.
     * 
     * @param pathOfValidTexts Path to the valid texts file
     * 
     * @note File format:
     *       - Regular text: one text per line
     *       - String resources: "String #123: actual_text" format
     *       - Automatically enables _pruningValidTexts flag if file is loaded
     * 
     * @note Performance optimizations:
     *       - Early exit for empty lines
     *       - Check "String #" first to avoid unnecessary find(": ") calls
     *       - Use const reference for line iteration
     *       - Skip empty strings before inserting
     */
    void Preference::loadValidTexts(const std::string &pathOfValidTexts) {
        std::string fileContent = loadFileContent(pathOfValidTexts);
        if (fileContent.empty()) {
            return;
        }
        
        this->_validTexts.clear();
        std::vector<std::string> validStringLines;
        splitString(fileContent, validStringLines, '\n');
        
        // Performance: Use const reference to avoid unnecessary string operations
        for (const auto &line: validStringLines) {
            // Performance: Skip empty lines early
            if (line.empty()) {
                continue;
            }
            
            // Performance: Check "String #" first (more specific pattern)
            // Most lines won't contain "String #", so this avoids unnecessary find(": ") calls
            size_t stringHashPos = line.find("String #");
            if (stringHashPos != std::string::npos) {
                // String resource format: "String #123: actual_text"
                // Find ": " after "String #"
                size_t colonPos = line.find(": ", stringHashPos);
                if (colonPos != std::string::npos && colonPos + 2 < line.length()) {
                    std::string extractedText = line.substr(colonPos + 2);
                    // Performance: Only insert non-empty strings
                    if (!extractedText.empty()) {
                        this->_validTexts.emplace(std::move(extractedText));
                    }
                }
            } else {
                // Regular text format: use the whole line
                this->_validTexts.emplace(line);
            }
        }
        
        // Performance: Set flag only if we actually loaded texts
        if (!this->_validTexts.empty()) {
            this->_pruningValidTexts = true;
        }
    }

    /**
     * @brief Load all configuration files
     * 
     * Main configuration loading function. Loads all configuration files in order:
     * 1. Resource mapping (max.mapping)
     * 2. Valid texts (max.valid.strings)
     * 3. Base config (max.config)
     * 4. Black widgets (max.widget.black)
     * 5. Custom actions (max.xpath.actions)
     * 6. White/black lists (awl.strings, abl.strings)
     * 7. Tree pruning (max.tree.pruning)
     * 8. Input texts (max.strings, max.fuzzing.strings)
     * 
     * @note Only loads on Android or in debug mode
     * 
     * @note Performance optimizations:
     *       - Individual error handling for each config to prevent one failure from blocking others
     *       - More detailed error logging to help identify which config failed
     *       - Optimized loading order: critical configs first
     */
    void Preference::loadConfigs() {
#if defined(__ANDROID__) || defined(_DEBUG_)
        // Performance: Individual error handling for each config
        // This ensures that if one config fails to load, others can still be loaded
        // Critical configs are loaded first
        
        // 1. Resource mapping (used for de-obfuscation)
        try {
            loadMixResMapping(DefaultResMappingFilePath);
        } catch (const std::exception &ex) {
            BLOGE("Failed to load resource mapping: %s", ex.what());
        }
        
        // 2. Valid texts (used for text pruning)
        try {
            loadValidTexts(ValidTextFilePath);
        } catch (const std::exception &ex) {
            BLOGE("Failed to load valid texts: %s", ex.what());
        }
        
        // 3. Base config (contains critical settings like input fuzzing flags)
        try {
            loadBaseConfig();
        } catch (const std::exception &ex) {
            BLOGE("Failed to load base config: %s", ex.what());
        }
        
        // 4. Black widgets (used for avoiding certain UI elements)
        try {
            loadBlackWidgets();
        } catch (const std::exception &ex) {
            BLOGE("Failed to load black widgets: %s", ex.what());
        }
        
        // 5. Custom actions (user-defined actions)
        try {
            loadActions();
        } catch (const std::exception &ex) {
            BLOGE("Failed to load custom actions: %s", ex.what());
        }
        
        // 6. White/black lists (currently not actively used)
        try {
            loadWhiteBlackList();
        } catch (const std::exception &ex) {
            BLOGE("Failed to load white/black lists: %s", ex.what());
        }
        
        // 7. Tree pruning (used for modifying element properties)
        try {
            loadTreePruning();
        } catch (const std::exception &ex) {
            BLOGE("Failed to load tree pruning: %s", ex.what());
        }
        
        // 8. Input texts (used for input fuzzing)
        try {
            loadInputTexts();
        } catch (const std::exception &ex) {
            BLOGE("Failed to load input texts: %s", ex.what());
        }
#endif
    }

#define MaxRandomPickSTR  "max.randomPickFromStringList"
#define InputFuzzSTR "max.doinputtextFuzzing"
#define ListenMode "max.listenMode"

    /**
     * @brief Load base configuration file
     * 
     * Loads the base configuration file (/sdcard/max.config) which contains
     * key-value pairs for various settings.
     * 
     * Supported configuration keys:
     * - max.randomPickFromStringList: Use random preset strings for input
     * - max.doinputtextFuzzing: Enable input text fuzzing
     * - max.listenMode: Enable listen mode (skip all model actions)
     * 
     * @note File format: key=value, one per line
     */
    /**
     * @brief Load base configuration file
     * 
     * Loads the base configuration file (/sdcard/max.config) which contains
     * key-value pairs for various settings.
     * 
     * Supported configuration keys:
     * - max.randomPickFromStringList: Use random preset strings for input
     * - max.doinputtextFuzzing: Enable input text fuzzing
     * - max.listenMode: Enable listen mode (skip all model actions)
     * 
     * @note File format: key=value, one per line
     * 
     * @note Performance optimizations:
     *       - Early exit for empty lines
     *       - Manual key-value parsing to avoid multiple splitString calls
     *       - String comparison optimization
     *       - Reduced logging overhead
     */
    void Preference::loadBaseConfig() {
        LOGI("pref init checking curr packageName is offset: %s", Preference::PackageName.c_str());
        std::string configContent = loadFileContent(BaseConfigFilePath);
        if (configContent.empty()) {
            return;
        }
        
        BLOG("max.config:\n %s", configContent.c_str());
        std::vector<std::string> lines;
        splitString(configContent, lines, '\n');
        
        for (const std::string &line: lines) {
            // Performance: Skip empty lines early
            if (line.empty()) {
                continue;
            }
            
            // Performance: Manual key-value parsing to avoid splitString overhead
            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos || eqPos == 0 || eqPos == line.length() - 1) {
                continue; // No '=' or '=' at start/end
            }
            
            // Extract key and value
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            
            // Trim whitespace
            trimString(key);
            trimString(value);
            
            // Performance: Skip if key or value is empty after trimming
            if (key.empty() || value.empty()) {
                continue;
            }
            
            BDLOG("base config key:-%s- value:-%s-", key.c_str(), value.c_str());
            
            // Performance: Use string comparison with early exit
            // Check most common keys first (if we know the distribution)
            if (key == MaxRandomPickSTR) {
                BDLOG("set %s", MaxRandomPickSTR);
                this->_randomInputText = (value == "true");
            } else if (key == InputFuzzSTR) {
                BDLOG("set %s", InputFuzzSTR);
                this->_doInputFuzzing = (value == "true");
            } else if (key == ListenMode) {
                BDLOG("set %s", ListenMode);
                this->setListenMode(value == "true");
            }
        }
    }

    /**
     * @brief Cache page texts recursively
     * 
     * Recursively collects all non-empty text from elements and caches them
     * in _pageTextsCache. If cache exceeds PageTextsMaxCount, removes oldest entries.
     * 
     * @param rootElement Root Element to start caching from
     * 
     * @note This function is now merged into resolveElement() to reduce tree traversals.
     *       It's kept as a separate function for backward compatibility but is no longer
     *       called directly from resolvePage().
     * 
     * @deprecated Use resolveElement() instead, which includes this functionality.
     */
    /**
     * @brief Cache page texts recursively
     * 
     * Recursively collects all non-empty text from elements and caches them
     * in _pageTextsCache. If cache exceeds PageTextsMaxCount, removes oldest entries.
     * 
     * @param rootElement Root Element to start caching from
     * 
     * @note This function is now merged into resolveElement() to reduce tree traversals.
     *       It's kept as a separate function for backward compatibility but is no longer
     *       called directly from resolvePage().
     * 
     * @deprecated Use resolveElement() instead, which includes this functionality.
     * 
     * @note Performance optimizations:
     *       - Cached children reference to avoid repeated getChildren() calls
     *       - Early exit for empty children
     *       - Cache text reference to avoid repeated getText() calls
     */
    void Preference::cachePageTexts(const ElementPtr &rootElement) {
        if (!rootElement) {
            return;
        }
        
        // Performance: Check cache size and trim if needed
        if (this->_pageTextsCache.size() > PageTextsMaxCount) {
            for (int i = 0; i < 20 && !this->_pageTextsCache.empty(); i++) {
                this->_pageTextsCache.pop_front();
            }
        }
        
        // Performance: Cache text reference to avoid repeated getText() calls
        const std::string &text = rootElement->getText();
        if (!text.empty()) {
            this->_pageTextsCache.push_back(text);
        }
        
        // Performance: Cache children reference to avoid repeated getChildren() calls
        const auto &children = rootElement->getChildren();
        // Performance: Early exit if no children (common case for leaf nodes)
        if (children.empty()) {
            return;
        }
        
        // Recursively process children
        for (const auto &childElement: children) {
            this->cachePageTexts(childElement);
        }
    }


    /**
     * @brief Set listen mode
     * 
     * Enables or disables listen mode. In listen mode, all actions from the model
     * are skipped, allowing the system to listen to user interactions only.
     * 
     * @param listen true to enable listen mode, false to disable
     */
    void Preference::setListenMode(bool listen) {
        BDLOG("set %s", ListenMode);
        this->_skipAllActionsFromModel = listen;
        LOGI("fastbot native use a listen mode: %d !!!", this->_skipAllActionsFromModel);
    }

    /**
     * @brief Load custom actions from JSON file
     * 
     * Loads custom action events from /sdcard/max.xpath.actions file.
     * Each event contains:
     * - activity: Activity name to match
     * - prob: Probability of triggering (0.0 - 1.0)
     * - times: Number of times this event can be triggered
     * - actions: List of actions to execute
     * 
     * Each action contains:
     * - action: Action type (CLICK, LONG_CLICK, etc.)
     * - xpath: XPath selector to find target element
     * - text: Input text (for editable widgets)
     * - clearText: Whether to clear text before input
     * - throttle: Throttle time in milliseconds
     * - wait: Wait time in milliseconds
     * - useAdbInput: Whether to use ADB input
     * 
     * @note File format: JSON array of event objects
     */
    /**
     * @brief Load custom actions from JSON file
     * 
     * Loads custom action events from /sdcard/max.xpath.actions file.
     * Each event contains:
     * - activity: Activity name to match
     * - prob: Probability of triggering (0.0 - 1.0)
     * - times: Number of times this event can be triggered
     * - actions: List of actions to execute
     * 
     * Each action contains:
     * - action: Action type (CLICK, LONG_CLICK, etc.)
     * - xpath: XPath selector to find target element
     * - text: Input text (for editable widgets)
     * - clearText: Whether to clear text before input
     * - throttle: Throttle time in milliseconds
     * - wait: Wait time in milliseconds
     * - useAdbInput: Whether to use ADB input
     * 
     * @note File format: JSON array of event objects
     * 
     * @note Performance optimizations:
     *       - Removed duplicate logging for xpath
     *       - Cache action type to avoid repeated getActionType() calls
     *       - Early exit for empty xpath (still create Xpath object for consistency)
     *       - Fixed bug: command should be read from action, not actions
     */
    void Preference::loadActions() {
        std::string fileContent = loadFileContent(ActionConfigFilePath);
        if (fileContent.empty()) {
            return;
        }
        
        BLOG("loading actions  : %s", ActionConfigFilePath.c_str());
        try {
            ::nlohmann::json actionEvents = ::nlohmann::json::parse(fileContent);
            for (const ::nlohmann::json &actionEvent: actionEvents) {
                CustomEventPtr customEvent = std::make_shared<CustomEvent>();
                customEvent->prob = static_cast<float>(getJsonValue<float>(actionEvent, "prob", 1));
                customEvent->times = getJsonValue<int>(actionEvent, "times", 1);
                customEvent->activity = getJsonValue<std::string>(actionEvent, "activity", "");
                BLOG("loading event %s", customEvent->activity.c_str());
                
                ::nlohmann::json actions = getJsonValue<::nlohmann::json>(actionEvent, "actions",
                                                                          ::nlohmann::json());
                for (const ::nlohmann::json &action: actions) {
                    std::string actionTypeString = getJsonValue<std::string>(action, "action", "");
                    auto customAction = std::make_shared<CustomAction>(
                            stringToActionType(actionTypeString));
                    
                    std::string xPathString = getJsonValue<std::string>(action, "xpath", "");
                    // Performance: Only log once per action
                    BLOG("loading action %s", xPathString.c_str());
                    customAction->xpath = std::make_shared<Xpath>(xPathString);
                    
                    customAction->text = getJsonValue<std::string>(action, "text", "");
                    customAction->clearText = getJsonValue<bool>(action, "clearText", false);
                    customAction->throttle = getJsonValue<int>(action, "throttle", 1000);
                    customAction->waitTime = getJsonValue<int>(action, "wait", 0);
                    customAction->adbInput = getJsonValue<bool>(action, "useAdbInput", false);
                    customAction->allowFuzzing = false;
                    
                    // Performance: Cache action type to avoid repeated getActionType() calls
                    ActionType actionType = customAction->getActionType();
                    if (actionType == ActionType::SHELL_EVENT) {
                        // Bug fix: command should be read from action, not actions
                        customAction->command = getJsonValue<std::string>(action, "command", "");
                    }
                    
                    customEvent->actions.push_back(customAction);
                }
                this->_customEvents.push_back(customEvent);
            }
        } catch (nlohmann::json::exception &ex) {
            BLOGE("parse actions error happened: id,%d: %s", ex.id, ex.what());
        }
    }

    /**
     * @brief Load black widget configurations from JSON file
     * 
     * Loads black widget configurations from /sdcard/max.widget.black file.
     * Black widgets are controls or regions that should be avoided during exploration.
     * 
     * Each black widget configuration contains:
     * - activity: Activity name (optional, empty means all activities)
     * - xpath: XPath selector to find blacklisted elements (optional)
     * - bounds: Bounding box in format "[x1,y1][x2,y2]" or "x1,y1,x2,y2" (optional)
     * 
     * @note File format: JSON array of black widget objects
     * @note If both xpath and bounds are specified, elements must match both
     * @note Bounds can be relative (0.0-1.1) or absolute (pixels)
     */
    /**
     * @brief Load black widget configurations from JSON file
     * 
     * Loads black widget configurations from /sdcard/max.widget.black file.
     * Black widgets are controls or regions that should be avoided during exploration.
     * 
     * Each black widget configuration contains:
     * - activity: Activity name (optional, empty means all activities)
     * - xpath: XPath selector to find blacklisted elements (optional)
     * - bounds: Bounding box in format "[x1,y1][x2,y2]" or "x1,y1,x2,y2" (optional)
     * 
     * @note File format: JSON array of black widget objects
     * @note If both xpath and bounds are specified, elements must match both
     * @note Bounds can be relative (0.0-1.1) or absolute (pixels)
     * 
     * @note Performance optimizations:
     *       - Fixed bug: Removed duplicate sscanf calls (second call overwrites first)
     *       - Optimized bounds parsing with format detection
     *       - Reduced logging overhead
     */
    void Preference::loadBlackWidgets() {
        std::string fileContent = fastbotx::Preference::loadFileContent(BlackWidgetFilePath);
        if (fileContent.empty()) {
            return;
        }
        
        try {
            BLOG("loading black widgets  : %s", BlackWidgetFilePath.c_str());
            ::nlohmann::json actions = ::nlohmann::json::parse(fileContent);
            for (const ::nlohmann::json &action: actions) {
                CustomActionPtr act = std::make_shared<CustomAction>();
                std::string xpathstr = getJsonValue<std::string>(action, "xpath", "");
                if (!xpathstr.empty()) {
                    act->xpath = std::make_shared<Xpath>(xpathstr);
                    BLOG("loading black widget %s", xpathstr.c_str());
                }
                
                act->activity = getJsonValue<std::string>(action, "activity", "");
                this->_blackWidgetActions.push_back(act);
                
                std::string boundsstr = getJsonValue<std::string>(action, "bounds", "");
                if (!boundsstr.empty()) {
                    act->bounds.resize(4);
                    // Performance: Try format "[x1,y1][x2,y2]" first, then "x1,y1,x2,y2"
                    // Bug fix: Removed duplicate sscanf - second call was overwriting first result
                    int parsed = 0;
                    if (boundsstr.find('[') != std::string::npos) {
                        // Format: "[x1,y1][x2,y2]"
                        parsed = sscanf(boundsstr.c_str(), "[%f,%f][%f,%f]", 
                                        &act->bounds[0], &act->bounds[1],
                                        &act->bounds[2], &act->bounds[3]);
                    } else {
                        // Format: "x1,y1,x2,y2"
                        parsed = sscanf(boundsstr.c_str(), "%f,%f,%f,%f", 
                                       &act->bounds[0], &act->bounds[1],
                                       &act->bounds[2], &act->bounds[3]);
                    }
                    // Security: Check if parsing succeeded
                    if (parsed != 4) {
                        BLOGE("Failed to parse bounds: %s (parsed %d values, expected 4)", 
                              boundsstr.c_str(), parsed);
                        act->bounds.clear();
                    }
                } else {
                    act->bounds.clear();
                }
            }
        } catch (nlohmann::json::exception &ex) {
            BLOGE("parse black widgets error happened: id,%d: %s", ex.id, ex.what());
        }
    }

    /**
     * @brief Load white list and black list text files
     * 
     * Loads white list and black list from text files:
     * - White list: /sdcard/awl.strings
     * - Black list: /sdcard/abl.strings
     * 
     * File format: One text per line
     * 
     * @note Currently loaded but not actively used in the codebase.
     *       Kept for future use or backward compatibility.
     */
    /**
     * @brief Load white list and black list text files
     * 
     * Loads white list and black list from text files:
     * - White list: /sdcard/awl.strings
     * - Black list: /sdcard/abl.strings
     * 
     * File format: One text per line
     * 
     * @note Currently loaded but not actively used in the codebase.
     *       Kept for future use or backward compatibility.
     * 
     * @note Performance optimizations:
     *       - Early exit if black list is empty (most common case)
     *       - Reduced logging overhead (only log if content exists)
     */
    void Preference::loadWhiteBlackList() {
        std::string contentBlack = fastbotx::Preference::loadFileContent(BlackListFilePath);
        if (!contentBlack.empty()) {
            std::vector<std::string> texts;
            splitString(contentBlack, texts, '\n');
            this->_blackList.swap(texts);
            BLOG("blacklist :\n %s", contentBlack.c_str());
        }
        
        std::string contentWhite = fastbotx::Preference::loadFileContent(WhiteListFilePath);
        if (!contentWhite.empty()) {
            std::vector<std::string> textsw;
            splitString(contentWhite, textsw, '\n');
            this->_whiteList.swap(textsw);
            BLOG("whitelist :\n %s", contentWhite.c_str());
        }
    }

    /**
     * @brief Load input texts for fuzzing
     * 
     * Loads two types of input texts:
     * 1. Preset texts: /sdcard/max.strings - User-designed input texts
     * 2. Fuzzing texts: /sdcard/max.fuzzing.strings - Random fuzzing texts
     * 
     * File format:
     * - Preset texts: One text per line
     * - Fuzzing texts: One text per line (lines starting with '#' are comments)
     * 
     * These texts are used by patchOperate() to fill in input fields during exploration.
     */
    /**
     * @brief Load input texts for fuzzing
     * 
     * Loads two types of input texts:
     * 1. Preset texts: /sdcard/max.strings - User-designed input texts
     * 2. Fuzzing texts: /sdcard/max.fuzzing.strings - Random fuzzing texts
     * 
     * File format:
     * - Preset texts: One text per line
     * - Fuzzing texts: One text per line (lines starting with '#' are comments)
     * 
     * These texts are used by patchOperate() to fill in input fields during exploration.
     * 
     * @note Performance optimizations:
     *       - Use swap instead of assign for better performance
     *       - Use const reference for line iteration
     *       - Early exit for empty lines and comments
     *       - Pre-filter during split to reduce iterations
     */
    void Preference::loadInputTexts() {
        // Load specified designed text by tester
        std::string content = fastbotx::Preference::loadFileContent(InputTextConfigFilePath);
        if (!content.empty()) {
            std::vector<std::string> texts;
            splitString(content, texts, '\n');
            // Performance: Use swap instead of assign for better performance
            this->_inputTexts.swap(texts);
        }
        
        // Load fuzzing texts
        std::string fuzzContent = fastbotx::Preference::loadFileContent(FuzzingTextsFilePath);
        if (!fuzzContent.empty()) {
            std::vector<std::string> fuzzTexts;
            splitString(fuzzContent, fuzzTexts, '\n');
            
            // Performance: Pre-allocate capacity if we can estimate size
            // Most lines will be valid (not comments), so reserve approximate size
            this->_fuzzingTexts.reserve(fuzzTexts.size());
            
            // Performance: Use const reference to avoid unnecessary string operations
            for (const auto &line: fuzzTexts) {
                // Performance: Early exit for empty lines and comments
                if (line.empty() || line[0] == '#') {
                    continue; // Comment line, skip
                }
                this->_fuzzingTexts.push_back(line);
            }
        }
    }

    /**
     * @brief Load tree pruning rules from JSON file
     * 
     * Loads tree pruning rules from /sdcard/max.tree.pruning file.
     * Tree pruning allows modifying element properties based on xpath matching.
     * 
     * Each rule contains:
     * - activity: Activity name to apply this rule
     * - xpath: XPath selector to match elements
     * - resourceid: New resource ID (use InvalidProperty to skip)
     * - text: New text (use InvalidProperty to skip)
     * - contentdesc: New content description (use InvalidProperty to skip)
     * - classname: New class name (use InvalidProperty to skip)
     * 
     * @note File format: JSON array of pruning rule objects
     * @note Performance optimization: Groups rules by activity for faster lookup
     */
    /**
     * @brief Load tree pruning rules from JSON file
     * 
     * Loads tree pruning rules from /sdcard/max.tree.pruning file.
     * Tree pruning allows modifying element properties based on xpath matching.
     * 
     * Each rule contains:
     * - activity: Activity name to apply this rule
     * - xpath: XPath selector to match elements
     * - resourceid: New resource ID (use InvalidProperty to skip)
     * - text: New text (use InvalidProperty to skip)
     * - contentdesc: New content description (use InvalidProperty to skip)
     * - classname: New class name (use InvalidProperty to skip)
     * 
     * @note File format: JSON array of pruning rule objects
     * 
     * @note Performance optimizations:
     *       - Groups rules by activity for faster lookup
     *       - Pre-allocates vectors to avoid reallocations
     *       - Clears old data before loading new
     */
    void Preference::loadTreePruning() {
        std::string fileContent = fastbotx::Preference::loadFileContent(TreePruningFilePath);
        if (fileContent.empty()) {
            return;
        }
        
        try {
            ::nlohmann::json actions = ::nlohmann::json::parse(fileContent);
            
            // Performance optimization: Clear and rebuild activity-grouped map
            this->_treePruningsByActivity.clear();
            this->_treePrunings.clear();
            
            // Performance: Pre-allocate capacity if we know the size
            if (actions.is_array()) {
                size_t estimatedSize = actions.size();
                this->_treePrunings.reserve(estimatedSize);
            }
            
            for (const ::nlohmann::json &action: actions) {
                CustomActionPtr act = std::make_shared<CustomAction>();
                std::string xpathStr = getJsonValue<std::string>(action, "xpath", "");
                act->xpath = std::make_shared<Xpath>(xpathStr);
                act->activity = getJsonValue<std::string>(action, "activity", "");
                act->resourceID = getJsonValue<std::string>(action, "resourceid", InvalidProperty);
                act->text = getJsonValue<std::string>(action, "text", InvalidProperty);
                act->contentDescription = getJsonValue<std::string>(action, "contentdesc",
                                                                    InvalidProperty);
                act->classname = getJsonValue<std::string>(action, "classname", InvalidProperty);
                this->_treePrunings.push_back(act);
                
                // Performance optimization: Group by activity for faster lookup
                this->_treePruningsByActivity[act->activity].push_back(act);
            }
        } catch (nlohmann::json::exception &ex) {
            BLOGE("parse tree pruning error happened: id,%d: %s", ex.id, ex.what());
        }
    }


    /**
     * @brief Load file content into string
     * 
     * Utility function to read a file's entire content into a string.
     * 
     * @param fileAbsolutePath Absolute path to the file
     * 
     * @return std::string - File content, or empty string if file doesn't exist
     * 
     * @note Used by all configuration loading functions
     * @note Logs a warning if file doesn't exist
     */
    /**
     * @brief Load file content into string
     * 
     * Utility function to read a file's entire content into a string.
     * 
     * @param fileAbsolutePath Absolute path to the file
     * 
     * @return std::string - File content, or empty string if file doesn't exist
     * 
     * @note Used by all configuration loading functions
     * @note Logs a warning if file doesn't exist
     * 
     * @note Performance optimizations:
     *       - Uses file size hint to pre-allocate string capacity
     *       - More efficient file reading with size estimation
     */
    std::string Preference::loadFileContent(const std::string &fileAbsolutePath) {
        std::ifstream fileStringReader(fileAbsolutePath, std::ios::binary | std::ios::ate);
        if (!fileStringReader.good()) {
            LOGW("load file %s not exists!!!", fileAbsolutePath.c_str());
            return std::string();
        }
        
        // Performance: Get file size and pre-allocate string capacity
        // Security: Check for tellg() errors (-1 indicates error)
        std::streamsize fileSize = fileStringReader.tellg();
        if (fileSize <= 0 || fileSize == std::streamsize(-1)) {
            LOGW("Failed to get file size for %s (size: %ld)", fileAbsolutePath.c_str(), 
                 static_cast<long>(fileSize));
            return std::string();
        }
        
        fileStringReader.seekg(0, std::ios::beg);
        // Security: Verify seekg succeeded
        if (!fileStringReader.good()) {
            LOGW("Failed to seek to beginning of file %s", fileAbsolutePath.c_str());
            return std::string();
        }
        std::string retStr;
        retStr.reserve(static_cast<size_t>(fileSize)); // Pre-allocate to avoid reallocations
        
        retStr.assign((std::istreambuf_iterator<char>(fileStringReader)),
                      std::istreambuf_iterator<char>());
        
        return retStr;
    }

    std::string Preference::InvalidProperty = "-f0s^%a@d";
    // static configs for android
    std::string Preference::DefaultResMappingFilePath = "/sdcard/max.mapping";
    std::string Preference::BaseConfigFilePath = "/sdcard/max.config";
    std::string Preference::InputTextConfigFilePath = "/sdcard/max.strings";
    std::string Preference::ActionConfigFilePath = "/sdcard/max.xpath.actions";
    std::string Preference::WhiteListFilePath = "/sdcard/awl.strings";
    std::string Preference::BlackListFilePath = "/sdcard/abl.strings";
    std::string Preference::BlackWidgetFilePath = "/sdcard/max.widget.black";
    std::string Preference::TreePruningFilePath = "/sdcard/max.tree.pruning";
    std::string Preference::ValidTextFilePath = "/sdcard/max.valid.strings";
    std::string Preference::FuzzingTextsFilePath = "/sdcard/max.fuzzing.strings";
    std::string Preference::PackageName;

} // namespace fastbotx
