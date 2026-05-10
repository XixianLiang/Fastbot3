/**
 * @authors Zhao Zhang
 */

/**
 * @file WidgetPriorityProvider.cpp
 *
 * Builds the `knowledge_org` JSON payload, calls `LlmClient::predictWithPayload`, strips prose/markers,
 * and maps `priorities` / `recommend_order` into amplified weights for weighted random selection.
 */

#include "WidgetPriorityProvider.h"

#include "../utils.hpp"
#include "../thirdpart/json/json.hpp"

#include <cmath>
#include <cstdio>

namespace fastbotx {

    namespace {

        using nlohmann::json;

        /** Stable string id for an action row in the payload (hash hex, matches object-key form in some responses). */
        std::string actionHashToId(uintptr_t h) {
            char buf[24];
            snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long) h);
            return std::string(buf);
        }

        /**
         * Normalizes several LLM JSON shapes into `out.widgetPriorities`.
         *
         * Supported top-level keys:
         * - `priorities` as array `[p0, p1, ...]` aligned with `validActions`, or
         * - `priorities` as object `{"0x...": p, ...}` keyed by `actionHashToId`, or
         * - `recommend_order` as array of indices (best-first); converted to implied scores then scaled.
         *
         * Raw scores are assumed in [0, 1] and amplified to roughly [1, 5] so weighted sampling separates tiers.
         */
        bool applyParsedWidgetPriorities(const json &j,
                                         const std::vector<ActivityStateActionPtr> &validActions,
                                         IWidgetPriorityProvider::Result &out) {
            out.widgetPriorities.clear();
            const size_t n = validActions.size();
            if (n == 0) return false;

            // Lift LLM scores into a wider numeric range for weighted random choice; exponent softens extremes.
            const double kAmplify = 4.0;
            const double kExponent = 0.8;
            auto scaleLlmPriority = [kAmplify, kExponent](double p) -> double {
                if (p < 0.0) p = 0.0;
                if (p > 1.0) p = 1.0;
                double q = std::pow(p, kExponent);
                return 1.0 + kAmplify * q;
            };

            if (j.contains("priorities")) {
                const auto &pri = j["priorities"];
                out.widgetPriorities.resize(n, 1.0);
                // Array form: one score per slot; indices align with validActions order (sparse/truncated arrays leave 1.0 defaults).
                if (pri.is_array()) {
                    for (size_t i = 0; i < n && i < pri.size(); ++i) {
                        if (pri[i].is_number()) {
                            out.widgetPriorities[i] = scaleLlmPriority(pri[i].get<double>());
                        }
                    }
                    return true;
                }
                // Object form: scores keyed by the same id strings we send in payload["elements"][].id.
                if (pri.is_object()) {
                    for (size_t i = 0; i < n; ++i) {
                        std::string id = actionHashToId(validActions[i]->hash());
                        if (pri.contains(id) && pri[id].is_number()) {
                            out.widgetPriorities[i] = scaleLlmPriority(pri[id].get<double>());
                        }
                    }
                    return true;
                }
            }
            // Relative ranking only: map position in the list to a pseudo score in (0,1], best-first.
            if (j.contains("recommend_order") && j["recommend_order"].is_array()) {
                const auto &order = j["recommend_order"];
                out.widgetPriorities.assign(n, 1.0);
                const double nD = static_cast<double>(order.size());
                for (size_t rank = 0; rank < order.size(); ++rank) {
                    int idx = order[rank].is_number_integer() ? static_cast<int>(order[rank].get<int>()) : -1;
                    if (idx >= 0 && idx < static_cast<int>(n)) {
                        double raw = 1.0 - (static_cast<double>(rank) / (nD > 1 ? nD : 1.0));
                        out.widgetPriorities[static_cast<size_t>(idx)] = scaleLlmPriority(raw);
                    }
                }
                return true;
            }
            return false;
        }

    }  // anonymous namespace

    IWidgetPriorityProvider::Result LlmWidgetPriorityProvider::organize(
            uintptr_t absStateId,
            const std::vector<ActivityStateActionPtr> &validActions,
            const ModelPtr &model) {
        Result result;
        if (!model) return result;

        std::shared_ptr<LlmClient> client = model->getLlmClient();
        // With zero or one candidate there is nothing to prioritize between.
        if (!client || validActions.size() < 2) {
            BDLOG("WidgetPriorityProvider: widget_priority skip absStateId=%llu (no client or elements<2)",
                  (unsigned long long) absStateId);
            return result;
        }

        json payload;
        // Contract for remote prompt type `knowledge_org`: bounded index range plus per-widget descriptors.
        payload["max_index"] = static_cast<int>(validActions.size() - 1);
        json elements = json::array();
        for (size_t i = 0; i < validActions.size(); ++i) {
            const auto &a = validActions[i];
            WidgetPtr w = a ? a->getTarget() : nullptr;
            json el;
            el["id"] = actionHashToId(a->hash());
            el["class"] = w ? w->getClassname() : "";
            el["resource_id"] = w ? w->getResourceID() : "";
            el["text"] = w ? w->getText() : "";
            el["content_desc"] = w ? w->getContentDesc() : "";
            elements.push_back(el);
        }
        payload["elements"] = std::move(elements);
        std::string payloadStr = payload.dump();

        BDLOG("WidgetPriorityProvider: widget_priority request absStateId=%llu elements=%zu",
              (unsigned long long) absStateId, validActions.size());

        std::string response;
        if (!client->predictWithPayload("knowledge_org", payloadStr, {}, response)) {
            BDLOGE("WidgetPriorityProvider: widget_priority predict failed (check LLM HTTP / HttpLlmClient logs)");
            return result;
        }

        // Models sometimes prepend natural language; strip known prefixes then brace-scan.
        std::string toParse = response;
        const std::string jsonMarker("JSON:");
        size_t pos = response.find(jsonMarker);
        if (pos != std::string::npos) {
            toParse = response.substr(pos + jsonMarker.size());
            size_t start = toParse.find_first_not_of(" \t\n\r");
            if (start != std::string::npos) toParse = toParse.substr(start);
        } else {
            // Prefer the object shape we expect; otherwise fall back to the first '{' in the buffer.
            size_t braceP = response.find("{\"priorities\"");
            size_t braceR = response.find("{\"recommend_order\"");
            if (braceP != std::string::npos) toParse = response.substr(braceP);
            else if (braceR != std::string::npos) toParse = response.substr(braceR);
            else {
                size_t brace = response.find('{');
                if (brace != std::string::npos) toParse = response.substr(brace);
            }
        }

        try {
            json j = json::parse(toParse);
            // Failure leaves success=false and empty priorities; callers treat that as uniform weights.
            if (applyParsedWidgetPriorities(j, validActions, result)) {
                result.success = true;
                BDLOG("WidgetPriorityProvider: widget_priority done absStateId=%llu n=%zu",
                      (unsigned long long) absStateId, result.widgetPriorities.size());
            }
        } catch (...) {
            BDLOG("WidgetPriorityProvider: widget_priority parse failed absStateId=%llu",
                  (unsigned long long) absStateId);
        }

        return result;
    }

}  // namespace fastbotx
