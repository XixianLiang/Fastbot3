/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @file Action.cpp
 *
 * Implements hashing, string formatting, priority rules, `OperatePtr` conversion, and `ActivityStateAction`
 * lifecycle for Fastbot's action layer.
 *
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang, Zhengwei Lv
 */
#ifndef Action_CPP_H_
#define Action_CPP_H_

#include "Action.h"

#include <utility>
#include "State.h"


namespace fastbotx {

    Action::Action()
            : Node(), PriorityNode(), _actionType(ActionType::NOP), _qValue(0) {
    }

    Action::Action(ActionType actionType)
            : Node(), PriorityNode(), _actionType(actionType), _qValue(0) {
    }

    Action::Action(const Action &other)
            : Node(), PriorityNode(), _actionType(other._actionType), _qValue(other._qValue) {
        this->_hashcode = other._hashcode;
        this->_priority = other._priority;
        this->_visitedCount.store(other.getVisitedCount());
        this->_id = other.getIdi();
    }

    /** Upper bound (ms) for randomized throttle in `toOperate`; first dispatch uses a random delay up to this value. */
    int Action::_throttle = 100;

    /** Fixed heuristic ordering: tap beats scroll beats generic actions during competing selection. */
    int Action::getPriorityByActionType() const {
        switch (this->_actionType) {
            case ActionType::CLICK:
                return 4;
            case ActionType::LONG_CLICK:
            case ActionType::SCROLL_TOP_DOWN:
            case ActionType::SCROLL_BOTTOM_UP:
            case ActionType::SCROLL_LEFT_RIGHT:
            case ActionType::SCROLL_RIGHT_LEFT:
                return 2;
            default:
                return 1;
        }
    }

    bool Action::isValid() const {
        return true;
    }

    /** True for standard widget-driven gestures (from `BACK` through extended scroll variants). */
    bool Action::isModelAct() const {
        return this->_actionType >= ActionType::BACK &&
               this->_actionType <= ActionType::SCROLL_BOTTOM_UP_N;
    }

    /** Spatial actions need bounds on a widget; meta-actions such as `NOP` do not. */
    bool Action::requireTarget() const {
        return this->_actionType >= ActionType::CLICK &&
               this->_actionType <= ActionType::SCROLL_BOTTOM_UP_N;
    }

    bool Action::canStartTestApp() const {
        return this->_actionType == ActionType::START ||
               this->_actionType == ActionType::RESTART ||
               this->_actionType == ActionType::CLEAN_RESTART;
    }

    bool Action::operator==(const Action &action) {
        return this->_actionType == action._actionType;
    }

    void Action::setPriority(int priority) {
        this->_priority = priority;
    }

    std::string Action::toString() const {
        std::stringstream strs;
        std::string actStr;
        if (this->_actionType >= 0 && this->_actionType < ActionType::ActTypeSize) {
            actStr = actName[this->_actionType];
        } else {
            actStr = "INVALID_ACTION(" + std::to_string(static_cast<int>(this->_actionType)) + ")";
        }
        strs << "{id: " << this->getId() << ", act: " << actStr <<
             ", value: " << this->_qValue << "}";
        return strs.str();
    }

    std::string Action::toDescription() const {
        return toString();
    }

    OperatePtr Action::toOperate() const {
        OperatePtr opt = std::make_shared<DeviceOperateWrapper>();
        opt->act = this->_actionType;
        opt->aid = this->getId();
        // Jitter early executions to spread device load; repeat visits leave throttle unset elsewhere.
        if (this->_visitedCount <= 1) {
            opt->throttle = static_cast<float>(randomInt(10, Action::_throttle));
        }
        return opt;
    }

    /** Shared singletons for non-widget meta-actions used across agents. */
    std::shared_ptr<Action> Action::NOP = std::make_shared<Action>(ActionType::NOP);
    std::shared_ptr<Action> Action::ACTIVATE = std::make_shared<Action>(ActionType::ACTIVATE);
    std::shared_ptr<Action> Action::RESTART = std::make_shared<Action>(ActionType::RESTART);
    std::shared_ptr<Action> Action::CLEAN_RESTART = std::make_shared<Action>(ActionType::CLEAN_RESTART);
    std::shared_ptr<Action> Action::FUZZ = std::make_shared<Action>(ActionType::FUZZ);
    std::shared_ptr<Action> Action::DEEP_LINK = std::make_shared<Action>(ActionType::DEEP_LINK);

    PropertyIDPrefixImpl(Action, "g0a");
    const int Action::DefaultValue = 0;

    ActivityStateAction::ActivityStateAction()
            : Action(), _target(nullptr), _functionListener(nullptr), _whichWidget(-1) {

    }

    ActivityStateAction::ActivityStateAction(const StatePtr &state, WidgetPtr targetWidget,
                                             ActionType actionType)
            : Action(actionType), _state(state), _target(std::move(targetWidget)),
              _functionListener(nullptr), _whichWidget(-1) {

        uintptr_t hashcode = std::hash<int>{}(this->getActionType());
        uintptr_t stateHash = this->_state.expired() ? 0x1 : this->_state.lock()->hash();
        uintptr_t targetHash = nullptr == this->_target ? 0x1 : this->_target->hash();

        // Mix action enum, abstract state id, and widget identity into a stable uintptr key.
        this->_hashcode =
                0x9e3779b9 + (hashcode << 2) ^ (((stateHash << 4) ^ (targetHash << 3)) << 1);
    }

    ActivityStateAction::ActivityStateAction(const ActivityStateAction &other)
            : Action(other.getActionType()),
              _state(other._state),
              _target(other._target),
              _hashcode(other._hashcode),
              _functionListener(nullptr),
              _whichWidget(other._whichWidget),
              _inputText(other._inputText) {}

    bool ActivityStateAction::isValid() const {
        return (this->_target == nullptr || !this->_target->getBounds()->isEmpty());
    }

    bool ActivityStateAction::getEnabled() const {
        return (this->_target == nullptr || this->_target->getEnabled());
    }

    uintptr_t ActivityStateAction::hash() const {
        return this->_hashcode;
    }

    /** Equality uses the precomputed identity hash (state + widget + action type mix). */
    bool ActivityStateAction::operator==(const ActivityStateAction &action) const {
        return this->hash() == action.hash();
    }

    bool ActivityStateAction::operator<(const ActivityStateAction &action) const {
        return this->hash() < action.hash();
    }


    bool ActivityStateAction::isEmpty() const {
        auto rect = this->getTarget()->getBounds();
        return rect->isEmpty();
    }

    ActivityStateAction::~ActivityStateAction() {
        this->_state.reset();
        this->_target = nullptr;
        _functionListener.reset();
    }

    void ActivityStateAction::visit(time_t timestamp) {
        Node::visit(timestamp);
        if (_functionListener && getVisitedCount() > 0) {
            ActivityStateActionPtr self = shared_from_this();
            _functionListener->onActionExecuted(self);
        }
    }

    OperatePtr ActivityStateAction::toOperate() const {
        auto opt = Action::toOperate();
        opt->sid = this->getState().expired() ? "" : this->getState().lock()->getId();
        if (this->getTarget()) {
            // Populate geometry and editability for injectors that operate on raw rectangles.
            opt->pos = *(this->getTarget()->getBounds());
            opt->editable = this->getTarget()->isEditable();
        }
        return opt;
    }

    std::string ActivityStateAction::toString() const {
        std::stringstream strs;
        strs << "{" << Action::toString() <<
             ", state: " << (this->_state.expired() ? "" : this->_state.lock()->getId()) <<
             ", node: " << (this->_target ? this->_target->toString() : "") << "}";
        return strs.str();
    }


}

#endif // Action_CPP_H_
