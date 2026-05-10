/**
 * @authors Zhao Zhang
 */

/**
 * @file ContentAwareInputProvider.cpp
 *
 * Default LLM path for editable-widget text: JSON payload, HTTP via `LlmClient`, trim/truncate, LRU-ish cache.
 */

#include "ContentAwareInputProvider.h"

#include "../utils.hpp"
#include "../thirdpart/json/json.hpp"

namespace fastbotx {

    using nlohmann::json;

    /**
     * Returns cached or freshly predicted fill text for an editable widget targeted by `action`.
     * Empty string means "do not override default typing" (early exits are silent except for logs).
     */
    std::string LlmContentAwareInputProvider::getInputTextForAction(
            const StatePtr &state,
            const ActionPtr &action,
            const ModelPtr &model) const {
        // Must have a concrete ActivityStateAction with an editable widget target.
        if (!state || !action) {
            BDLOG("ContentAwareInput: skip (no state or action)");
            return "";
        }

        auto stateAction = std::dynamic_pointer_cast<ActivityStateAction>(action);
        if (!stateAction || !stateAction->requireTarget()) {
            BDLOG("ContentAwareInput: skip (not ActivityStateAction or requireTarget()=false)");
            return "";
        }

        WidgetPtr target = stateAction->getTarget();
        if (!target || !target->isEditable()) {
            BDLOG("ContentAwareInput: skip (no target or !isEditable())");
            return "";
        }

        std::string activityStr = (state->getActivityString() && state->getActivityString().get())
                                  ? *state->getActivityString() : "";
        std::string resourceId = target->getResourceID();
        std::string text = target->getText();
        std::string contentDesc = target->getContentDesc();
        // Stable fingerprint for repeat visits to the same field (ordering matches header contract).
        std::string cacheKey = activityStr + "\t" + resourceId + "\t" + text + "\t" + contentDesc;

        BDLOG("ContentAwareInput: eligible activity=%s resource_id=%s",
              activityStr.c_str(), resourceId.c_str());

        // Hit path avoids remote latency when the same field asks again with unchanged widget snapshot.
        {
            auto it = _cache.find(cacheKey);
            if (it != _cache.end()) {
                BDLOG("ContentAwareInput: cache hit resource_id=%s", resourceId.c_str());
                return it->second;
            }
        }

        // No client → cannot predict; empty result avoids blocking the runner.
        if (!model) return "";
        std::shared_ptr<LlmClient> client = model->getLlmClient();
        if (!client) return "";

        std::string packageName = model->getPackageName();
        json payload;
        payload["package"] = packageName;
        payload["activity"] = activityStr;
        payload["class"] = target->getClassname();
        payload["resource_id"] = resourceId;
        payload["text"] = text;
        payload["content_desc"] = contentDesc;
        std::string payloadStr = payload.dump();

        // Prompt type is fixed; remote side maps it to the content-aware input template.
        BDLOG("ContentAwareInput: cache miss, request LLM resource_id=%s", resourceId.c_str());
        std::string response;
        if (!client->predictWithPayload("content_aware_input", payloadStr, {}, response)) {
            BDLOGE("ContentAwareInput: predict failed (check LLM HTTP / client logs)");
            return "";
        }

        // Models often wrap answers in quotes; strip edges before enforcing max length.
        size_t start = 0;
        while (start < response.size() &&
               (std::isspace(static_cast<unsigned char>(response[start])) ||
                response[start] == '"' || response[start] == '\'')) {
            start++;
        }
        size_t end = response.size();
        while (end > start &&
               (std::isspace(static_cast<unsigned char>(response[end - 1])) ||
                response[end - 1] == '"' || response[end - 1] == '\'')) {
            end--;
        }
        if (start >= end) return "";
        response = response.substr(start, end - start);

        const size_t kMaxInputLen = 200;
        if (response.size() > kMaxInputLen) response.resize(kMaxInputLen);

        // Insert at LRU tail; drop oldest keys when over capacity (see `kMaxContentAwareInputCacheSize`).
        while (_cacheOrder.size() >= kMaxContentAwareInputCacheSize) {
            std::string oldKey = std::move(_cacheOrder.front());
            _cacheOrder.pop_front();
            _cache.erase(oldKey);
        }
        _cache[cacheKey] = response;
        _cacheOrder.push_back(cacheKey);

        std::string logPreview = response.size() > 40 ? response.substr(0, 37) + "..." : response;
        BDLOG("ContentAwareInput: ok suggested=%s", logPreview.c_str());
        return response;
    }

    /** Naming / abstraction updates can change widget fingerprints; drop all stale entries. */
    void LlmContentAwareInputProvider::onStateAbstractionChanged() {
        _cache.clear();
        _cacheOrder.clear();
    }

}  // namespace fastbotx
