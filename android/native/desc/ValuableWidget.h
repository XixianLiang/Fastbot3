/**
 * @authors Zhao Zhang, Tianming Liu, Chenxu Wang
 *
 * @file ValuableWidget.h
 * @brief Declares `ValuableWidget`, an aggregate of widgets merged by on-screen bounds identity for activity-level
 *        brief descriptions.
 */

#ifndef ValuableWidget_H_
#define ValuableWidget_H_

#include "Action.h"
#include "Base.h"
#include "Widget.h"

namespace fastbotx {

/**
 * Buckets one or more `Widget` nodes that share the same bounds identity (`Rect::hash2` via `computeHash`).
 * `Activity::fillValuableWidget` creates instances and calls `fillDetails` when additional widgets match the same
 * region or remap rules. Text output is built by `toDescription()` for sorted activity summaries.
 */
class ValuableWidget {
public:
    /** Initializes aggregates from `widget`: layout hash, vertical position, class/actions or informational text. */
    explicit ValuableWidget(WidgetPtr widget);

    /** One-line human-readable summary (classes, count, resource id, nested markup, actions or text). */
    std::string toDescription();

    /** Bounds-derived bucket key used when merging widgets into this aggregate. */
    uintptr_t hash() { return _hashcode; }

    /** Top coordinate of the widget bounds (ordering key for brief descriptions). */
    int getTop() { return _top; }

    /** Merges another widget’s class, actions, or non-action text into this bucket. */
    void fillDetails(WidgetPtr widget);

    ~ValuableWidget();

private:
    /** Space-separated distinct class names accumulated in this bucket. */
    std::string generateClass();

    /** Phrase listing supported `ActionType` names when the bucket has actionable widgets. */
    std::string generateAction();

    /** Resource id suffix when the first widget in `_widgets` exposes a non-empty id. */
    std::string generateResId();

    /** Sets `_hashcode` from `rect` using `Rect::hash2()` (zero when bounds are missing). */
    void computeHash(RectPtr rect);

    WidgetPtrSet _widgets;
    std::set<ActionType> _actions;
    /** Text + content-description snippets for widgets without declared actions. */
    std::set<std::string> _info;
    std::set<std::string> _classes;
    int _top;
    uintptr_t _hashcode;
    /** Reserved for future cached description (currently unused). */
    std::string _description;
};

typedef std::shared_ptr<ValuableWidget> ValuableWidgetPtr;

} // namespace fastbotx

#endif
