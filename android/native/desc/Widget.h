/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 *
 * @file Widget.h
 * @brief Declares `Widget`: actionable UI node derived from `Element`, with configurable hashing for state identity.
 */
#ifndef Widget_H_
#define Widget_H_


#include <set>
#include <map>
#include <vector>
#include <string>
#include <memory>
#include "Element.h"
#include "../Base.h"

namespace fastbotx {

    /**
     * @brief Widget class representing an actionable UI widget
     * 
     * Widget is a lightweight representation of a UI element that can be
     * interacted with. It extracts essential properties from Element and
     * computes a hash for state comparison. Widgets are used in State
     * objects to represent the actionable elements on a screen.
     * 
     * Features:
     * - Hash-based identification
     * - Action type tracking
     * - XPath generation
     * - Memory-efficient (can clear details when not needed)
     * 
     * @note When enabled, visible text participates in hashing and widget identification.
     */
    class Widget : Serializable, public HashNode {
    public:
        /**
         * @brief Constructor creates a Widget from an Element
         * 
         * Extracts relevant properties from the Element and computes the
         * widget's hash code. Processes text to remove digits and spaces.
         * 
         * @param parent Parent widget (nullptr for root widgets)
         * @param element The Element to create widget from
         */
        Widget(std::shared_ptr<Widget> parent, const ElementPtr &element);

        std::shared_ptr<Widget> getParent() const { return this->_parent; }

        std::shared_ptr<Rect> getBounds() const { return this->_bounds; }

        /// Returns const reference to avoid copying the set on every call (performance).
        const std::set<ActionType> &getActions() const { return this->_actions; }

        std::string getText() const { return this->_text; }

        bool getEnabled() const { return this->_enabled; }

        bool hasOperate(OperateType opt) const { return this->_operateMask & opt; }

        bool hasAction() const { return !this->_actions.empty(); }

        bool isEditable() const;

        /** Returns `getClassname()` by value (legacy accessor). */
        std::string getClass() const { return this->_clazz; }

        std::string getTextualInfo() const { return this->_info; }

        std::string getDescriptionInfo() const;

        /** Get widget class name (e.g. "android.widget.TextView"). Empty if details cleared. */
        const std::string &getClassname() const { return this->_clazz; }

        /** Get widget resource ID. Empty if not set or details cleared. */
        const std::string &getResourceID() const { return this->_resourceID; }

        /** Get widget content description. Empty if not set or details cleared. */
        const std::string &getContentDesc() const { return this->_contextDesc; }

        uintptr_t hash() const override;

        /// Hash using only the attributes specified by mask (for dynamic state abstraction).
        virtual uintptr_t hashWithMask(WidgetKeyMask mask) const;

        std::string toString() const override;

        std::string toJson() const;

        std::string buildFullXpath() const;

        /**
         * Returns a stable, tab-separated summary (`type`, `class`, `resource-id`, text fields, `hash`, …).
         * The merge/navigation parameters exist for API compatibility and are ignored here.
         */
        std::string toHTML(const std::vector<ElementPtr> &elementToMerge = {},
                          bool noChild = true, int actionId = -1) const;

        /** Optional human-readable role label from planner overlays (e.g. merged-state enrichment). */
        const std::string &getFunctionLabel() const { return _functionLabel; }

        void setFunctionLabel(std::string label) { _functionLabel = std::move(label); }

        /** Alias for `setFunctionLabel` (legacy name). */
        void setFunction(const std::string &function) { _functionLabel = function; }

        /** Alias for `getFunctionLabel` returning by value (legacy name). */
        std::string getFunction() const { return _functionLabel; }

        /** Semantic `hash()` XOR `Rect::hash2` of bounds; used when spatial position matters. */
        uintptr_t getMyHashcode() const { return _myHashcode; }

        /** Clears class, text, id, and bounds to shrink memory; hash component fields zeroed. */
        virtual void clearDetails();

        /** Copies detail and hash parts from `copy` (e.g. after merge or rematerialization). */
        void fillDetails(const std::shared_ptr<Widget> &copy);

        virtual ~Widget();


    protected:
        Widget();

        void enableOperate(OperateType opt) { this->_operateMask |= opt; }

        void initFormElement(const ElementPtr &element);

        uintptr_t _hashcode{};
        uintptr_t _myHashcode{};
        /// Component hashes for hashWithMask (dynamic state abstraction)
        uintptr_t _hashClazz{};
        uintptr_t _hashResourceID{};
        uintptr_t _hashOperateMask{};
        uintptr_t _hashScrollType{};
        uintptr_t _hashText{};
        uintptr_t _hashContentDesc{};
        uintptr_t _hashIndex{};
        std::shared_ptr<Widget> _parent;
        std::string _text;
        int _index{};
        std::string _clazz;
        std::string _resourceID;
        bool _enabled{};
        bool _isEditable{};
        int _operateMask{OperateType::None};
        ElementPtr _element;
    private:
        std::string toXPath() const;

        RectPtr _bounds;
        std::string _contextDesc;
        std::set<ActionType> _actions;
        std::string _functionLabel;
        std::string _info;
    };


    typedef std::shared_ptr<Widget> WidgetPtr;
    typedef std::vector<WidgetPtr> WidgetPtrVec;
    typedef std::set<WidgetPtr, Comparator<Widget>> WidgetPtrSet;
    typedef std::map<uintptr_t, WidgetPtrVec> WidgetPtrVecMap;

}


#endif //Widget_H_
