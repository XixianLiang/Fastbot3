/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Model_CPP_
#define Model_CPP_

#include "Model.h"
#include "StateFactory.h"
#include "../utils.hpp"
#include <ctime>
#include <iostream>

namespace fastbotx {


    std::shared_ptr<Model> Model::create() {
        // Use new + shared_ptr instead of make_shared because constructor is protected
        // and make_shared cannot access protected constructors from outside the class
        return std::shared_ptr<Model>(new Model());
    }

    Model::Model() {
#ifndef FASTBOT_VERSION
#define FASTBOT_VERSION "local build"
#endif
        BLOG("---- native version " FASTBOT_VERSION " native version ----\n");
        this->_graph = std::make_shared<Graph>();
        this->_preference = Preference::inst();
        this->_netActionParam.netActionTaskid = 0;
    }


/// The general entrance of getting next step according to RL model, and return the next operation step in json format
/// \param descContent XML of the current page, in string format
/// \param activity activity name
/// \param deviceID The default value is "", you could provide your intended ID
/// \return the next operation step in json format
    std::string Model::getOperate(const std::string &descContent, const std::string &activity,
                                  const std::string &deviceID) //the entry for getting a new operation
    {
        const std::string &descContentCopy = descContent;
        ElementPtr elem = Element::createFromXml(
                descContentCopy); // get the xml object with tinyxml2
        if (nullptr == elem)
            return "";
        return this->getOperate(elem, activity, deviceID);
    }

    AbstractAgentPtr Model::addAgent(const std::string &deviceIDString, AlgorithmType agentType,
                                     DeviceType deviceType) {
        auto agent = AgentFactory::create(agentType, shared_from_this(), deviceType);
        const std::string &deviceID = deviceIDString.empty() ? ModelConstants::DefaultDeviceID
                                                             : deviceIDString; // deviceID is device id
        this->_deviceIDAgentMap.emplace(deviceID,
                                        agent); // add the pair of device and agent to the _deviceIDAgentMap
        this->_graph->addListener(
                agent); // add the agent to the graph and listen to updates of this graph.
        return agent;
    }

    AbstractAgentPtr Model::getAgent(const std::string &deviceID) const {
        const std::string &d = deviceID.empty() ? ModelConstants::DefaultDeviceID : deviceID;
        auto iter = this->_deviceIDAgentMap.find(d);
        if (iter != this->_deviceIDAgentMap.end())
            return (*iter).second;
        return nullptr;
    }


    std::string Model::getOperate(const ElementPtr &element, const std::string &activity,
                                  const std::string &deviceID) {
        OperatePtr operate = getOperateOpt(element, activity, deviceID);
        std::string operateString = operate->toString(); // wrap the operation as a json object and get its string
        return operateString;
    }


    // Helper method implementations
    ActionPtr Model::getCustomActionIfExists(const std::string &activity, const ElementPtr &element) const {
        if (this->_preference) {
            BLOG("try get custom action from preference");
            return this->_preference->resolvePageAndGetSpecifiedAction(activity, element);
        }
        return nullptr;
    }

    stringPtr Model::getOrCreateActivityPtr(const std::string &activity) {
        // get activity - optimize by using temporary shared_ptr for lookup
        stringPtrSet activityStringPtrSet = this->_graph->getVisitedActivities();
        // Create temporary shared_ptr for lookup to avoid unnecessary allocation
        stringPtr tempActivityPtr = std::make_shared<std::string>(activity);
        auto founded = activityStringPtrSet.find(tempActivityPtr);
        if (founded == activityStringPtrSet.end())
            return tempActivityPtr; // this is a new activity, use the temp pointer
        else
            return *founded; // use the cached activity
    }

    AbstractAgentPtr Model::getOrCreateAgent(const std::string &deviceID) {
        // create a default agent if map is empty
        if (this->_deviceIDAgentMap.empty()) {
            BLOG("%s", "use reuseAgent as the default agent");
            this->addAgent(ModelConstants::DefaultDeviceID, AlgorithmType::Reuse);
        }
        auto agentIterator = this->_deviceIDAgentMap.find(deviceID);
        if (agentIterator == this->_deviceIDAgentMap.end())
            return this->_deviceIDAgentMap[ModelConstants::DefaultDeviceID]; // get the agent from the default device
        else
            return (*agentIterator).second; // get the found agent
    }

    StatePtr Model::createAndAddState(const ElementPtr &element, const AbstractAgentPtr &agent,
                                      const stringPtr &activityPtr) {
        if (nullptr == element) // make sure the XML is not null
            return nullptr;
        
        // according to the type of the used agent, create the state of this page
        // include all the possible actions according to the widgets inside.
        StatePtr state = StateFactory::createState(agent->getAlgorithmType(), activityPtr, element);
        // add state - the agent will treat this state as the new state(_newState)
        state = this->_graph->addState(state);
        state->visit(this->_graph->getTimestamp());
        return state;
    }

    ActionPtr Model::selectAction(StatePtr &state, AbstractAgentPtr &agent, ActionPtr customAction, double &actionCost) {
        double startGeneratingActionTimestamp = currentStamp();
        actionCost = 0.0;
        ActionPtr action = customAction; // load the action specified by user

        if (state != nullptr) {
            BDLOGE("%s", state->toString().c_str());
        } else {
            BDLOGE("State is null, cannot log state information");
        }

        bool shouldSkipActionsFromModel = this->_preference ? this->_preference->skipAllActionsFromModel() : false;
        if (shouldSkipActionsFromModel) {
            LOGI("listen mode skip get action from model");
        }

        // if there is no action specified by user, ask the agent for a new action.
        if (nullptr == customAction && !shouldSkipActionsFromModel) {
            if (-1 != BLOCK_STATE_TIME_RESTART &&
                -1 != Preference::inst()->getForceMaxBlockStateTimes() &&
                agent->getCurrentStateBlockTimes() > BLOCK_STATE_TIME_RESTART) {
                action = Action::RESTART;
                BLOG("Ran into a block state %s", state ? state->getId().c_str() : "");
            } else {
                // this is also an entry for modifying RL model
                auto resolvedAction = agent->resolveNewAction();
                action = std::dynamic_pointer_cast<Action>(resolvedAction);
                // update the strategy based on the new action
                agent->updateStrategy();
                if (nullptr == action) {
                    BDLOGE("get null action!!!!");
                    return nullptr; // handle null action
                }
            }
            double endGeneratingActionTimestamp = currentStamp();
            actionCost = endGeneratingActionTimestamp - startGeneratingActionTimestamp;
            if (action->isModelAct() && state) {
                action->visit(this->_graph->getTimestamp());
                agent->moveForward(state); // update _currentState/Action with _newState/Action
            }
        }
        return action;
    }

    OperatePtr Model::convertActionToOperate(ActionPtr action, StatePtr state) {
        if (action == nullptr) {
            return DeviceOperateWrapper::OperateNop;
        }

        BLOG("selected action %s", action->toString().c_str());
        OperatePtr opt = action->toOperate();

        if (action->requireTarget()) {
            if (auto stateAction = std::dynamic_pointer_cast<fastbotx::ActivityStateAction>(action)) {
                std::shared_ptr<Widget> widget = stateAction->getTarget();
                if (widget) {
                    std::string widget_str = widget->toJson();
                    opt->widget = widget_str;
                    BLOG("stateAction Widget: %s", widget_str.c_str());
                }
            }
        }

        if (this->_preference) {
            this->_preference->patchOperate(opt);
        }

        if (DROP_DETAIL_AFTER_SATE && state && !state->hasNoDetail())
            state->clearDetails();

        return opt;
    }

    OperatePtr Model::getOperateOpt(const ElementPtr &element, const std::string &activity,
                                    const std::string &deviceID) {
        // the whole process begins.
        double methodStartTimestamp = currentStamp();
        
        // Get custom action from preference if exists
        ActionPtr customAction = getCustomActionIfExists(activity, element);
        
        // Get or create activity pointer
        stringPtr activityPtr = getOrCreateActivityPtr(activity);
        
        // Get or create agent
        AbstractAgentPtr agent = getOrCreateAgent(deviceID);
        
        // Create and add state
        StatePtr state = createAndAddState(element, agent, activityPtr);
        
        // Record state generation time
        double stateGeneratedTimestamp = currentStamp();
        
        // Select action
        double actionCost = 0.0;
        ActionPtr action = selectAction(state, agent, customAction, actionCost);
        
        // Handle null action
        if (nullptr == action) {
            return DeviceOperateWrapper::OperateNop;
        }
        
        // Convert action to operate
        OperatePtr opt = convertActionToOperate(action, state);
        
        // Record end time and log performance
        double methodEndTimestamp = currentStamp();
        BLOG("build state cost: %.3fs action cost: %.3fs total cost %.3fs",
             stateGeneratedTimestamp - methodStartTimestamp,
             actionCost,
             methodEndTimestamp - methodStartTimestamp);
        
        return opt;
    }

    Model::~Model() {
        this->_deviceIDAgentMap.clear();
    }

}
#endif //Model_CPP_
