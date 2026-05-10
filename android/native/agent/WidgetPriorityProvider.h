/**
 * @authors Zhao Zhang
 */

/**
 * @file WidgetPriorityProvider.h
 *
 * Optional LLM pass that assigns numeric weights to actionable widgets on one abstract state so SARSA-style
 * agents can bias roulette selection (`selectActionNotInModel`) toward semantically important controls.
 */

#ifndef FASTBOTX_WIDGET_PRIORITY_PROVIDER_H
#define FASTBOTX_WIDGET_PRIORITY_PROVIDER_H

#include "../desc/State.h"
#include "../desc/Action.h"
#include "../model/Model.h"

#include <memory>
#include <vector>
#include <string>

namespace fastbotx {

/**
 * Strategy for ranking widgets within a single state's valid action list.
 *
 * Implementations typically call `Model::getLlmClient()` with a structured payload and parse priority scores
 * or a recommended ordering back into one weight per `validActions[i]`.
 */
class IWidgetPriorityProvider {
public:
    struct Result {
        /// Per-action multiplicative weights (same length as `validActions` on success). Higher → stronger bias.
        std::vector<double> widgetPriorities;
        bool success = false;
    };

    virtual ~IWidgetPriorityProvider() = default;

    /**
     * Computes weights for `validActions` (caller guarantees each entry is non-null and `isValid()`).
     *
     * @param absStateId Opaque id for logging/correlation (often unused by backends).
     * @param validActions Ordered list matching executor INDEX semantics used elsewhere in the agent.
     * @param model Provides `getLlmClient()` and package context for HTTP.
     * @return Populated `widgetPriorities` when parsing succeeds; `success` false on skip/failure.
     */
    virtual Result organize(uintptr_t absStateId,
                            const std::vector<ActivityStateActionPtr> &validActions,
                            const ModelPtr &model) = 0;
};

using IWidgetPriorityProviderPtr = std::shared_ptr<IWidgetPriorityProvider>;

/**
 * Default provider: `predictWithPayload("knowledge_org", ...)` with a JSON list of widget features.
 * The model is expected to return `priorities` and/or `recommend_order` (see implementation for schema).
 */
class LlmWidgetPriorityProvider : public IWidgetPriorityProvider {
public:
    Result organize(uintptr_t absStateId,
                    const std::vector<ActivityStateActionPtr> &validActions,
                    const ModelPtr &model) override;
};

}  // namespace fastbotx

#endif  // FASTBOTX_WIDGET_PRIORITY_PROVIDER_H
