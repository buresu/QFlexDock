// SPDX-License-Identifier: MIT
#include "core/SplitterCoordinator.h"

#include <algorithm>
#include <iterator>

namespace QFlexDock {

namespace {

// Position of a handle on its movement axis, and its extent along the line.
struct Segment
{
    int position;
    int start;
    int end; // exclusive
};

Segment segmentOf(const SolvedHandle &h)
{
    if (h.orientation == Qt::Horizontal)
        return {h.rect.x(), h.rect.y(), h.rect.y() + h.rect.height()};
    return {h.rect.y(), h.rect.x(), h.rect.x() + h.rect.width()};
}

int extentAlong(const QRect &rect, Qt::Orientation o)
{
    return o == Qt::Horizontal ? rect.width() : rect.height();
}

int limitAlong(const QSize &size, Qt::Orientation o)
{
    return o == Qt::Horizontal ? size.width() : size.height();
}

} // namespace

std::vector<int> SplitterCoordinator::linkedHandles(const SolvedLayout &layout, int handleIndex)
{
    std::vector<int> group;
    const int count = int(layout.handles.size());
    if (handleIndex < 0 || handleIndex >= count)
        return group;

    const SolvedHandle &origin = layout.handles[size_t(handleIndex)];
    const Segment originSegment = segmentOf(origin);
    const int reach = std::max(1, layout.handleWidth);

    group.push_back(handleIndex);
    std::vector<bool> taken(size_t(count), false);
    taken[size_t(handleIndex)] = true;

    // Grow the run: keep adding aligned handles that touch one already in it.
    for (size_t next = 0; next < group.size(); ++next) {
        const Segment current = segmentOf(layout.handles[size_t(group[next])]);
        for (int i = 0; i < count; ++i) {
            if (taken[size_t(i)])
                continue;
            const SolvedHandle &candidate = layout.handles[size_t(i)];
            if (candidate.orientation != origin.orientation)
                continue;
            const Segment s = segmentOf(candidate);
            if (std::abs(s.position - originSegment.position) >= reach)
                continue;
            const int gap = std::max(s.start, current.start) - std::min(s.end, current.end);
            if (gap > reach)
                continue;
            const bool sameSplit = std::any_of(group.begin(), group.end(), [&](int g) {
                return layout.handles[size_t(g)].split == candidate.split;
            });
            if (sameSplit)
                continue;
            taken[size_t(i)] = true;
            group.push_back(i);
        }
    }
    return group;
}

std::vector<SplitterCoordinator::Corner> SplitterCoordinator::corners(const SolvedLayout &layout)
{
    std::vector<Bar> bars;
    bars.reserve(layout.handles.size());
    for (const SolvedHandle &handle : layout.handles)
        bars.push_back(Bar{handle.rect, handle.orientation, 0});
    return corners(bars);
}

std::vector<SplitterCoordinator::Corner> SplitterCoordinator::corners(const std::vector<Bar> &bars)
{
    std::vector<int> upright; // vertical bars: handles that move along x
    std::vector<int> level;
    for (int i = 0; i < int(bars.size()); ++i)
        (bars[size_t(i)].orientation == Qt::Horizontal ? upright : level).push_back(i);

    const auto add = [](std::vector<int> &list, int value) {
        if (std::find(list.begin(), list.end(), value) == list.end())
            list.push_back(value);
    };

    std::vector<Corner> result;
    for (int u : upright) {
        const QRect a = bars[size_t(u)].rect;
        const int reachA = bars[size_t(u)].reach;
        for (int l : level) {
            const QRect b = bars[size_t(l)].rect;
            const int reachB = bars[size_t(l)].reach;
            // Where the two lines cross. The bars meet there if each of them
            // reaches that patch (covers or touches it) and one runs through.
            const QRect patch(a.x(), b.y(), a.width(), b.height());
            const bool uprightReaches = a.top() <= patch.bottom() + 1 + reachA
                && a.bottom() + 1 + reachA >= patch.top();
            const bool levelReaches = b.left() <= patch.right() + 1 + reachB
                && b.right() + 1 + reachB >= patch.left();
            if (!uprightReaches || !levelReaches || !(a.intersects(patch) || b.intersects(patch)))
                continue;

            auto existing = std::find_if(result.begin(), result.end(), [&](const Corner &c) {
                return c.rect.intersects(patch);
            });
            if (existing == result.end()) {
                result.push_back(Corner{patch, {}, {}});
                existing = std::prev(result.end());
            } else {
                existing->rect |= patch;
            }
            add(existing->columns, u);
            add(existing->rows, l);
        }
    }
    return result;
}

std::vector<SplitterCoordinator::Squeezed>
SplitterCoordinator::squeezed(const LayoutTree &tree, const SolvedLayout &layout,
                              const std::vector<int> &group, int delta,
                              const std::function<bool(const LayoutNode &)> &mayGo)
{
    std::vector<Squeezed> result;
    if (delta == 0 || !mayGo)
        return result;
    for (int index : group) {
        if (index < 0 || index >= int(layout.handles.size()))
            continue;
        const SolvedHandle &handle = layout.handles[size_t(index)];
        const LayoutNode *split = tree.findNode(handle.split);
        if (!split || handle.index + 1 >= int(split->children.size()))
            continue;
        const LayoutNode &before = split->children[size_t(handle.index)];
        const LayoutNode &after = split->children[size_t(handle.index) + 1];
        // Moving by +d shrinks `after`, by -d `before`.
        const LayoutNode &shrinking = delta > 0 ? after : before;
        const LayoutNode &growing = delta > 0 ? before : after;
        if (!shrinking.isTabs() || !mayGo(shrinking))
            continue;
        const Qt::Orientation o = handle.orientation;
        const int left = extentAlong(layout.rects.value(shrinking.id), o) - std::abs(delta);
        const int minimum = limitAlong(layout.limits.value(shrinking.id).min, o);
        if (2 * left < minimum || left < 0)
            result.push_back(Squeezed{shrinking.id, growing.id});
    }
    return result;
}

std::pair<int, int> SplitterCoordinator::deltaRange(const LayoutTree &tree,
                                                    const SolvedLayout &layout,
                                                    const std::vector<int> &group)
{
    int lowest = -UnboundedSize;
    int highest = UnboundedSize;
    for (int index : group) {
        if (index < 0 || index >= int(layout.handles.size()))
            continue;
        const SolvedHandle &handle = layout.handles[size_t(index)];
        const LayoutNode *split = tree.findNode(handle.split);
        if (!split || handle.index + 1 >= int(split->children.size()))
            return {0, 0};
        const Qt::Orientation o = handle.orientation;
        const LayoutNode &before = split->children[size_t(handle.index)];
        const LayoutNode &after = split->children[size_t(handle.index) + 1];
        const int beforeSize = extentAlong(layout.rects.value(before.id), o);
        const int afterSize = extentAlong(layout.rects.value(after.id), o);
        const SizeLimits beforeLimits = layout.limits.value(before.id);
        const SizeLimits afterLimits = layout.limits.value(after.id);

        // Moving by +d grows `before` and shrinks `after`.
        lowest = std::max({lowest, limitAlong(beforeLimits.min, o) - beforeSize,
                           afterSize - limitAlong(afterLimits.max, o)});
        highest = std::min({highest, limitAlong(beforeLimits.max, o) - beforeSize,
                            afterSize - limitAlong(afterLimits.min, o)});
    }
    // A layout already squeezed below its minimums must not be able to move
    // further into the violation, but must stay put-able.
    lowest = std::min(lowest, 0);
    highest = std::max(highest, 0);
    return {lowest, highest};
}

std::vector<SplitterCoordinator::WeightUpdate>
SplitterCoordinator::moveHandles(const LayoutTree &tree, const SolvedLayout &layout,
                                 const std::vector<int> &group, int delta)
{
    const auto [lowest, highest] = deltaRange(tree, layout, group);
    delta = std::clamp(delta, lowest, highest);

    std::vector<WeightUpdate> updates;
    for (int index : group) {
        if (index < 0 || index >= int(layout.handles.size()))
            continue;
        const SolvedHandle &handle = layout.handles[size_t(index)];
        const LayoutNode *split = tree.findNode(handle.split);
        if (!split || handle.index + 1 >= int(split->children.size()))
            continue;

        WeightUpdate update;
        update.split = split->id;
        for (const auto &child : split->children)
            update.weights.push_back(extentAlong(layout.rects.value(child.id), handle.orientation));
        update.weights[size_t(handle.index)] += delta;
        update.weights[size_t(handle.index) + 1] -= delta;
        // Weights must stay positive even for a child squeezed to zero pixels.
        for (double &w : update.weights)
            w = std::max(w, 0.01);
        updates.push_back(std::move(update));
    }
    return updates;
}

} // namespace QFlexDock
