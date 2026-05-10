/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */

/**
 * @file AgentFactory.cpp
 *
 * Factory entry point that maps `AlgorithmType` to a concrete `AbstractAgent` implementation and,
 * for SARSA variants, starts a detached background worker that periodically persists the reuse model.
 */

#ifndef Agent_Factory_CPP_
#define Agent_Factory_CPP_

#include "AgentFactory.h"
#include "utils.hpp"
#include "Model.h"
#include "DoubleSarsaAgent.h"
#include "SarsaAgent.h"
#include "CuriosityAgent.h"
#include "../desc/StateEncoder.h"
#include "json.hpp"
#include "Preference.h"

namespace fastbotx {

    /**
     * Constructs an `AbstractAgent` for `agentT` and wires optional background persistence.
     *
     * Dispatch:
     * - `Curiosity` → `CuriosityAgent` with a configurable state encoder (DNN vs handcrafted).
     * - `Sarsa` → single-Q `SarsaAgent` plus reuse-model save thread.
     * - `LLMExplorer` and any other value → `DoubleSarsaAgent` (double Q-learning) plus save thread.
     *
     * For `Sarsa` / default, `threadDelayExec(3000, …)` waits three seconds, then detaches
     * `threadModelStorage`, which periodically calls `saveReuseModel` (interval defined on the agent side,
     * typically on the order of ten minutes). The worker captures the agent via `weak_ptr` so it stops
     * when the agent is destroyed.
     *
     * @param agentT Requested RL algorithm selector.
     * @param model Shared model handle (graph, reuse storage paths, etc.).
     * @param deviceType Reserved; ignored today (`Normal` only).
     */
    AbstractAgentPtr
    AgentFactory::create(AlgorithmType agentT, const ModelPtr &model, DeviceType /*deviceType*/) {
        AbstractAgentPtr agent = nullptr;

        // Curiosity-driven exploration (dual novelty); encoder choice is compile-time (see FASTBOTX_CURIOSITY_*).
        if (agentT == AlgorithmType::Curiosity) {
            CuriosityAgentPtr curiosityAgent = std::make_shared<CuriosityAgent>(model);
#if !defined(FASTBOTX_CURIOSITY_DISABLE_DNN_ENCODER)
            // Default: use DnnStateEncoder (16->16->8); setStateEncoder sets _clusterDim from encoder->getOutputDim().
            // To compare with handcrafted-only clustering, build with FASTBOTX_CURIOSITY_DISABLE_DNN_ENCODER defined
            // so that CuriosityAgent uses 16-dim HandcraftedStateEncoder directly for clustering.
            curiosityAgent->setStateEncoder(std::make_shared<DnnStateEncoder>());
            agent = curiosityAgent;
            BLOG("Created CuriosityAgent (curiosity-driven, WebRLED-style, DNN encoder)");
#else
            // Handcrafted-only clustering: 16-dim HandcraftedStateEncoder, no DNN encoder.
            agent = curiosityAgent;
            BLOG("Created CuriosityAgent (curiosity-driven, WebRLED-style, handcrafted embedding)");
#endif
            return agent;
        }

        // `LLMExplorer` is no longer constructed here; unknown types including it use DoubleSarsa below.

        // Classic tabular SARSA with reuse-model persistence.
        if (agentT == AlgorithmType::Sarsa) {
            SarsaAgentPtr sarsaAgent = std::make_shared<SarsaAgent>(model);
            // Delay before spawning the saver avoids racing early-run checkpoints during startup.
            threadDelayExec(3000, false, &SarsaAgent::threadModelStorage,
                            std::weak_ptr<fastbotx::SarsaAgent>(sarsaAgent));
            agent = sarsaAgent;
            BLOG("Created SarsaAgent (legacy single-Q SARSA with reuse model)");
            return agent;
        }

        // Default path: double Q-learning agent (also handles `AlgorithmType::DoubleSarsa` and unknown enums).
        DoubleSarsaAgentPtr doubleSarsaAgent = std::make_shared<DoubleSarsaAgent>(model);
        threadDelayExec(3000, false, &DoubleSarsaAgent::threadModelStorage,
                        std::weak_ptr<fastbotx::DoubleSarsaAgent>(doubleSarsaAgent));

        agent = doubleSarsaAgent;
        BLOG("Created DoubleSarsaAgent");

        return agent;
    }

}

#endif // Agent_Factory_CPP_
