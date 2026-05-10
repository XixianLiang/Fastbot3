/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @file DeviceOperateWrapper.h
 *
 * Serializable operation envelope passed from the native agent layer to the device injector: action kind,
 * geometry, timing, optional text payload, and JSON interchange for remote or scripted drivers.
 *
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef Operate_H_
#define Operate_H_

#include <string>
#include "../Base.h"

namespace fastbotx {

    /**
     * Mutable POD bundle carried by `OperatePtr` through scheduling and execution.
     * `Action::toOperate()` fills the core fields; hosts may extend via JSON (`DeviceOperateWrapper(std::string)`).
     */
    class DeviceOperateWrapper {
    public:
        /** Gesture or meta-command (`CLICK`, `BACK`, shell hooks, etc.). */
        ActionType act;

        /** Screen rectangle for spatial actions (left/top/right/bottom in device pixels). */
        Rect pos;

        /** Abstract state identifier this step was chosen from (for logging / replay correlation). */
        std::string sid;

        /** Action identifier within that state (stable id from the action model). */
        std::string aid;

        /** Millisecond delay before injecting the gesture (also randomized on first dispatch in `Action::toOperate`). */
        float throttle;

        /** Millisecond pause after the operation completes before sampling the next UI tree. */
        int waitTime;

        /** True when the node supports text insertion (drives `setText` validation). */
        bool editable{};

        /** When false, downstream fuzzing stages skip mutating this step. */
        bool allowFuzzing{true};

        /** For text actions: clear existing field content before committing `_text`. */
        bool clear{};

        /** Optional human-readable label for diagnostics or UI overlays. */
        std::string name;

        /** Optional serialized widget context (structure-specific JSON). */
        std::string widget;

        DeviceOperateWrapper();

        /** Copies a subset of fields (`act`, `pos`, `sid`, `aid`, timing, `_text`, `extra0`, `name`). */
        DeviceOperateWrapper(const DeviceOperateWrapper &opt);

        /** Parses a JSON blob produced by `toString()` or compatible remote payloads (`act`, `pos`, `throttle`, …). */
        explicit DeviceOperateWrapper(const std::string &optJsonStr);

        DeviceOperateWrapper &operator=(const DeviceOperateWrapper &node);

        /** Stores keyboard payload; truncates beyond 999 chars; warns if `editable` is false. */
        std::string setText(const std::string &text);

        const std::string &getText() const { return this->_text; }

        /** When true, inject raw keystrokes instead of composed commit paths where supported. */
        bool getRawInput() const { return this->rawInput; }

        /** Optional planner-specific JSON fragment mirrored in serialization (extension hook). */
        const std::string &getJAction() const { return this->jAction; }

        std::string toString() const;

        virtual ~DeviceOperateWrapper() = default;

        static std::shared_ptr<DeviceOperateWrapper> OperateNop;
    protected:
        bool rawInput{};
        std::string _text;
        std::string extra0;
        std::string jAction;
    };

    typedef std::shared_ptr<DeviceOperateWrapper> OperatePtr;

}

#endif
