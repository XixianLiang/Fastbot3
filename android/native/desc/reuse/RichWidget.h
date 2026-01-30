/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */
/**
 * @authors Jianqiang Guo, Yuhui Su, Zhao Zhang
 */
#ifndef RichWidget_H_
#define RichWidget_H_

#include "Widget.h"

namespace fastbotx {

    /**
     * @brief RichWidget extends Widget with richer hash computation
     * 
     * RichWidget uses actions, class name, resource ID, and text (from itself
     * or its children) to generate a more comprehensive hash code for widget
     * identification. This is used in reuse-based algorithms for better widget matching.
     * 
     * Features:
     * - Enhanced hash computation including actions and children text
     * - Better widget identification for reuse algorithms
     */
    class RichWidget : virtual public Widget {
    public:
        /**
         * @brief Constructor creates RichWidget from Element
         * 
         * Uses supported actions, class name, resource ID, and text (from itself
         * or its children) for embedding and generating hash code to identify the widget.
         * 
         * @param parent Parent widget of this widget
         * @param element XML Element information of this widget
         */
        RichWidget(WidgetPtr parent, const ElementPtr &element);

        uintptr_t hash() const override;

        uintptr_t hashWithMask(WidgetKeyMask mask) const override;

        uintptr_t getActHashCode() const { return this->_widgetHashcode; }

    protected:
        RichWidget();

        uintptr_t _widgetHashcode{};

    private:
        /// Get Element valid text. If parent widget are not clickable, get children's valid text
        /// \param element
        /// \return valid text from widget or its children or offspring.
        std::string getValidTextFromWidgetAndChildren(const ElementPtr &element) const;
    };

}


#endif //RichWidget_H_
