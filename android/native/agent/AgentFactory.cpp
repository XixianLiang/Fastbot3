/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */

#ifndef Agent_Factory_CPP_
#define Agent_Factory_CPP_

#include "AgentFactory.h"
#include "utils.hpp"
#include "Model.h"
#include "ModelReusableAgent.h"
#include "json.hpp"
#include "Preference.h"

namespace fastbotx {

    /**
     * @brief Create Agent instance
     * 
     * Always creates ModelReusableAgent instance regardless of algorithm type (agentT).
     * ModelReusableAgent is a SARSA reinforcement learning based reusable model agent.
     * 
     * Creation flow:
     * 1. Create ModelReusableAgent instance
     * 2. Start background thread to periodically save model (starts after 3 second delay, saves every 10 minutes)
     * 3. Return Agent pointer
     * 
     * @param agentT Algorithm type (currently unused, reserved interface)
     * @param model Model pointer, Agent needs access to model to get state graph info
     * @param deviceType Device type (currently unused, reserved interface)
     * @return Created Agent smart pointer
     * 
     * @note 
     * - Uses weak_ptr passed to background thread to avoid circular references
     * - Background thread automatically exits when Agent is destructed (weak_ptr becomes invalid)
     * - Model save thread starts after 3 second delay to avoid frequent saves during initialization
     */
    AbstractAgentPtr
    AgentFactory::create(AlgorithmType /*agentT*/, const ModelPtr &model, DeviceType /*deviceType*/) {
        AbstractAgentPtr agent = nullptr;
        
        // Always use ModelReusableAgent regardless of algorithm type
        ReuseAgentPtr reuseAgent = std::make_shared<ModelReusableAgent>(model);
        
        // Start background thread to periodically save model
        // Parameter explanation:
        // - 3000: Start after 3 second delay (avoid frequent saves during initialization)
        // - false: Non-blocking execution
        // - &ModelReusableAgent::threadModelStorage: Thread execution function
        // - weak_ptr: Use weak_ptr to avoid circular references, thread exits when Agent is destructed
        threadDelayExec(3000, false, &ModelReusableAgent::threadModelStorage,
                        std::weak_ptr<fastbotx::ModelReusableAgent>(reuseAgent));
        
        agent = reuseAgent;
        return agent;
    }

}

#endif
