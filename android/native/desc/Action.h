/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @file Action.h
 *
 * Core action model: abstract `Action` (type, Q-value, priority, hash) and `ActivityStateAction`, which binds
 * an action to a `State` and optional target `Widget` for concrete UI operations.
 *
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Action_H_
#define Action_H_

#include "Node.h"
#include "Base.h"
#include "Widget.h"
#include "DeviceOperateWrapper.h"

#include <utility>
#include <vector>
#include <set>
#include <string>

namespace fastbotx {


    class State;

    /**
     * Abstract base for executable intents: combines `Node` (visit counts), `PriorityNode` (selection bias),
     * and `HashNode` (deduplication). Subclasses map to `DeviceOperateWrapper` payloads for the runtime.
     */
    class Action : public Node, public PriorityNode, public HashNode {
    public:
        /** Builds a `NOP` action with zero Q-value. */
        Action();

        /**
         * @param actionType Kind of gesture or meta-action (`CLICK`, `BACK`, etc.).
         */
        explicit Action(ActionType actionType);

        /** Copies type, Q-value, hash, priority, visit count, and stable id (used when cloning shared actions). */
        Action(const Action &other);

        std::string toString() const override;

        /** One-line summary for logs and graph tooling; default implementation forwards to `toString()`. */
        virtual std::string toDescription() const;

        virtual bool getEnabled() const { return true; }

        ActionType getActionType() const { return this->_actionType; }

        void setPriority(int priority);

        int getPriorityByActionType() const;

        bool isBack() const { return this->_actionType == ActionType::BACK; }

        bool isClick() const { return this->_actionType == ActionType::CLICK; }

        bool isNop() const { return this->_actionType == ActionType::NOP; }

        virtual bool isValid() const;

        virtual OperatePtr toOperate() const;

        uintptr_t _hashcode{};

        uintptr_t hash() const override { return _hashcode; }

        bool isModelAct() const;

        bool requireTarget() const;

        bool canStartTestApp() const;

        static std::shared_ptr<Action> NOP;
        static std::shared_ptr<Action> ACTIVATE;
        static std::shared_ptr<Action> RESTART;
        static std::shared_ptr<Action> CLEAN_RESTART;
        static std::shared_ptr<Action> FUZZ;
        static std::shared_ptr<Action> DEEP_LINK;

        FuncGetID(Action);

        bool operator==(const Action &action);

        virtual ~Action() = default;


        virtual void setQValue(double value) { this->_qValue = static_cast<float>(value); }

        virtual double getQValue() const { return this->_qValue; }

        static int getThrottle() { return _throttle; }

        static const int DefaultValue;

    protected:

        ActionType _actionType;
        /** Minimum spacing between physical operations on device (milliseconds); base `toOperate` randomizes below this cap. */
        static int _throttle;
    private:
        float _qValue;
        PropertyIDPrefix(Action);
    };

    typedef std::shared_ptr<Action> ActionPtr;

    /** Parameters for remote / networked action dispatch (algorithm id, package, auth tokens). */
    typedef struct NetActionParameter_ {
        int throttle;
        int netActionTaskid;
        std::string algorithmString;
        std::string packageName;
        std::string token;
        std::string deviceid;
    } NetActionParam;

    class ActivityStateAction;

    typedef std::shared_ptr<ActivityStateAction> ActivityStateActionPtr;

    /**
     * Optional hook after an `ActivityStateAction` records a visit (see `ActivityStateAction::visit`).
     * Used for higher-level bookkeeping such as merged-state or coverage tracking.
     */
    class FunctionListener {
    public:
        virtual ~FunctionListener() = default;
        virtual void onActionExecuted(ActivityStateActionPtr action) = 0;
    };

    typedef std::shared_ptr<FunctionListener> FunctionListenerPtr;

    /**
     * Concrete action bound to a UI state snapshot and an optional widget target (bounds, editability, etc.).
     * Identity hash mixes action type, state hash, and target widget hash at construction.
     */
    class ActivityStateAction : public Action, public std::enable_shared_from_this<ActivityStateAction> {
    public:
        /**
         * @param state Current page state (weakly held after construction).
         * @param targetWidget Widget to act on; may be null for non-spatial actions (e.g. `BACK`).
         * @param actionType Gesture or command to perform on `targetWidget`.
         */
        ActivityStateAction(const std::shared_ptr<State> &state, WidgetPtr targetWidget,
                            ActionType actionType);

        ActivityStateAction(const ActivityStateAction &other);

        std::weak_ptr<State> getState() const { return this->_state; }

        std::shared_ptr<Widget> getTarget() const { return this->_target; }

        bool getEnabled() const override;

        bool isValid() const override;

        /** Rebinds the target without recomputing `_hashcode`; callers must preserve intended identity semantics. */
        void setTarget(WidgetPtr widget) { this->_target = std::move(widget); }

        OperatePtr toOperate() const override;

        /** True when the target widget has empty bounds (ResolveNode compatibility). */
        bool isEmpty() const;


        uintptr_t hash() const override;

        bool operator==(const ActivityStateAction &action) const;

        bool operator<(const ActivityStateAction &action) const;

        std::string toString() const override;

        void visit(time_t timestamp) override;

        void setListener(const FunctionListenerPtr &listener) { _functionListener = listener; }

        /** Index within duplicate-hash widget groups when multiple nodes collapse to one hash (`ReuseState`). */
        void setWhichWidget(int which) { _whichWidget = which; }

        int getWhichWidget() const { return _whichWidget; }

        void setInputText(std::string text) { _inputText = std::move(text); }

        const std::string &getInputText() const { return _inputText; }

        bool hasInput() const { return !_inputText.empty(); }

        std::weak_ptr<State> _state;
        std::shared_ptr<Widget> _target;
        uintptr_t _hashcode{};

        ~ActivityStateAction() override;

    protected:
        /** Sentinel construction; leaves state/target unset until a concrete ctor runs. */
        ActivityStateAction();

        FunctionListenerPtr _functionListener;
        int _whichWidget{-1};
        std::string _inputText;

    private:
    };
    typedef std::vector<ActivityStateActionPtr> ActivityStateActionPtrVec;
    typedef std::set<ActivityStateActionPtr, Comparator<ActivityStateAction>> ActivityStateActionPtrSet;


}

#endif //Action_H_
