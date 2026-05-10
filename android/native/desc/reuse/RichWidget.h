/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @file RichWidget.h
 *
 * Widget wrapper used on reuse-heavy paths: derives identity hashes from class, resource id,
 * supported actions, and harvestable text (including descendant text when the node is not independently clickable).
 *
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef RichWidget_H_
#define RichWidget_H_

#include "Widget.h"

namespace fastbotx {

    /**
     * Extends `Widget` with a stronger default hash for reuse and matching.
     *
     * The hash mixes class name, resource id, action kinds, optional text-derived material,
     * and (via `hashWithMask`) selectively XORs precomputed attribute hashes from the base `Widget`.
     */
    class RichWidget : virtual public Widget {
    public:
        /**
         * Builds a rich widget from an accessibility `Element` subtree.
         *
         * @param parent Parent widget in the synthetic tree (may be null for the root).
         * @param element Parsed UI element node (bounds, flags, text, children).
         */
        RichWidget(WidgetPtr parent, const ElementPtr &element);

        uintptr_t hash() const override;

        /** Optional coarse-grained identity: toggle text / content-desc / index contributions via `mask`. */
        uintptr_t hashWithMask(WidgetKeyMask mask) const override;

        /** Full rich hash including embedded text signal when present (same as `hash()`). */
        uintptr_t getActHashCode() const { return this->_widgetHashcode; }

    protected:
        /** Default constructor for subclasses; leaves hashes unset until a concrete ctor runs. */
        RichWidget();

        /** Cached rich hash (includes text term when non-empty). */
        uintptr_t _widgetHashcode{};
        /** Hash of class, resource id, and actions only (maskable attributes applied in `hashWithMask`). */
        uintptr_t _widgetHashcodeBase{};

    private:
        /**
         * Collects human-visible text for hashing: prefers `element->validText`, otherwise first non-empty
         * `validText` found under descendants (behavior differs slightly when static reuse abstraction is on).
         *
         * @param element Root of the subtree to search.
         * @return Non-empty display text if any descendant qualifies; otherwise empty string.
         */
        std::string getValidTextFromWidgetAndChildren(const ElementPtr &element) const;
    };

}


#endif //RichWidget_H_
