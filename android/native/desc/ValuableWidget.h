/**
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 */

#ifndef ValuableWidget_H_
#define ValuableWidget_H_

#include "Action.h"
#include "Base.h"
#include "Widget.h"

namespace fastbotx {

class ValuableWidget {
public:
    explicit ValuableWidget(WidgetPtr widget);

    std::string toDescription();

    uintptr_t hash() { return _hashcode; }

    int getTop() { return _top; }

    void fillDetails(WidgetPtr widget);

    ~ValuableWidget();

private:
    std::string generateClass();

    std::string generateAction();

    std::string generateResId();

    void computeHash(RectPtr rect);

    WidgetPtrSet _widgets;
    std::set<ActionType> _actions;
    std::set<std::string> _info;
    std::set<std::string> _classes;
    int _top;
    uintptr_t _hashcode;
    std::string _description;
};

typedef std::shared_ptr<ValuableWidget> ValuableWidgetPtr;

} // namespace fastbotx

#endif
