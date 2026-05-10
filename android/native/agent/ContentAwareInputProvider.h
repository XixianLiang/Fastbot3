/**
 * @authors Zhao Zhang
 */

/**
 * @file ContentAwareInputProvider.h
 *
 * Abstraction for generating short text to type into editable widgets (content-aware input).
 * The default implementation asks the configured `LlmClient` with prompt type `content_aware_input`.
 */

#ifndef FASTBOTX_CONTENT_AWARE_INPUT_PROVIDER_H
#define FASTBOTX_CONTENT_AWARE_INPUT_PROVIDER_H

#include "../desc/State.h"
#include "../desc/Action.h"
#include "../model/Model.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <list>

namespace fastbotx {

/**
 * Strategy interface for resolving fill text before an edit action runs.
 *
 * Callers supply the current abstract `State`, the chosen `Action`, and the live `Model` (for package name
 * and `LlmClient`). Implementations may call remote models, local rules, or caches; results should be
 * short human-like strings suitable for `EditText`-style widgets.
 */
class IContentAwareInputProvider {
public:
    virtual ~IContentAwareInputProvider() = default;

    /**
     * Produces input text for `action` when it targets an editable widget.
     *
     * @param state Current UI state (activity string used for context).
     * @param action Typically an `ActivityStateAction` with a concrete widget target.
     * @param model Provides `getPackageName()` and `getLlmClient()` when LLM-backed.
     * @return Text to apply, or empty string to skip / use defaults.
     */
    virtual std::string getInputTextForAction(const StatePtr &state,
                                              const ActionPtr &action,
                                              const ModelPtr &model) const = 0;

    /** Invalidate any fingerprint-keyed caches after naming or abstraction changes. */
    virtual void onStateAbstractionChanged() {}
};

using IContentAwareInputProviderPtr = std::shared_ptr<IContentAwareInputProvider>;

/**
 * LLM-backed provider: builds a JSON payload (package, activity, widget class, resource id, text, description),
 * calls `LlmClient::predictWithPayload("content_aware_input", ...)`, normalizes the returned string,
 * and caches results in a bounded FIFO map keyed by activity + widget identity fields.
 */
class LlmContentAwareInputProvider : public IContentAwareInputProvider {
public:
    std::string getInputTextForAction(const StatePtr &state,
                                      const ActionPtr &action,
                                      const ModelPtr &model) const override;

    void onStateAbstractionChanged() override;

private:
    static constexpr size_t kMaxContentAwareInputCacheSize = 64;

    mutable std::unordered_map<std::string, std::string> _cache;
    /** Eviction order for `_cache` when size exceeds `kMaxContentAwareInputCacheSize`. */
    mutable std::list<std::string> _cacheOrder;
};

}  // namespace fastbotx

#endif  // FASTBOTX_CONTENT_AWARE_INPUT_PROVIDER_H
