/*
 * Optional HTTP POST via JNI when libcurl is not available (e.g. monkey/Android build).
 * Image stays in Java: native passes (url, apiKey, prompt, model, maxTokens); Java builds
 * body with prompt + stored screenshot and performs POST.
 */
/**
 * @authors Zhao Zhang
 */

#ifndef FASTBOTX_LLM_JAVA_HTTP_H
#define FASTBOTX_LLM_JAVA_HTTP_H

#include <cstddef>
#include <string>

namespace fastbotx {

/**
 * Perform HTTP POST using Java's stored screenshot (no image passed from C++).
 * Java builds the request body (prompt + lastScreenshotForLlm as base64) and POSTs.
 * Called from HttpLlmClient when FASTBOTX_HAS_CURL is 0 so image never crosses JNI.
 *
 * @param url       API URL
 * @param apiKey    Bearer token (may be empty)
 * @param prompt    User prompt text
 * @param model     Model name
 * @param maxTokens Max tokens
 * @param timeoutMs Total wall-clock budget for build + HTTP (Java Future.get); also drives HttpURLConnection read/connect caps. <=0 uses Java defaults.
 * @param outResponse  On success, set to response body; unchanged on failure
 * @return true if HTTP 2xx and outResponse was set; false otherwise
 */
bool llmHttpPostViaJavaWithPrompt(const char *url,
                                  const char *apiKey,
                                  const char *prompt,
                                  const char *model,
                                  int maxTokens,
                                  int timeoutMs,
                                  std::string *outResponse);

/**
 * Same as above but prompt is assembled in Java from payload JSON (reduces JNI string copy).
 * promptType is one of: "executor", "planner", "step_summary".
 */
bool llmHttpPostViaJavaWithPayload(const char *url,
                                   const char *apiKey,
                                   const char *promptType,
                                   const char *payloadJson,
                                   const char *model,
                                   int maxTokens,
                                   int timeoutMs,
                                   std::string *outResponse);

/**
 * Java {@code com.android.commands.monkey.utils.CodeCoverage.getCoverage()} (Jacoco / AndroLog if
 * Monkey initialized them; else {@code AiClient} native stagnation metric). Requires JVM from
 * {@code nativeRegisterLlmHttpRunner} and {@code initAgentNative} (caches CodeCoverage class).
 * @return coverage scalar, or quiet NaN if JNI is unavailable
 */
double getLlmdroidCoverageFromJava();

/**
 * Java {@code com.android.commands.monkey.utils.CodeCoverage.isExternalCoverageEnabled()}.
 * True only when Monkey actually initialized Jacoco/AndroLog via {@code --use-code-coverage}.
 */
bool isLlmdroidExternalCoverageEnabledFromJava();

} // namespace fastbotx

#endif // FASTBOTX_LLM_JAVA_HTTP_H
