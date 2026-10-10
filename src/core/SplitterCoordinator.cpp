// SPDX-License-Identifier: MIT
#include "core/SplitterCoordinator.h"

#include <algorithm>
#include <cstdlib>
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
        if (!shrinking.isTabs() || shrinking.iconified || !mayGo(shrinking))
            continue;
        const Qt::Orientation o = handle.orientation;
        const int left = extentAlong(layout.rects.value(shrinking.id), o) - std::abs(delta);
        const int minimum = limitAlong(layout.limits.value(shrinking.id).min, o);
        if (2 * left < minimum || left < 0)
            result.push_back(Squeezed{shrinking.id, growing.id});
    }
    return result;
}

namespace {

// The children of a split on one side of a handle, nearest first, with how
// much each can give and take along the split.
struct Side
{
    std::vector<int> index;
    std::vector<int> shrink;
    std::vector<int> grow;
    qint64 canShrink = 0;
    qint64 canGrow = 0;
};

Side sideOf(const LayoutNode &split, const SolvedLayout &layout, int handle, bool before,
            Qt::Orientation o, bool push)
{
    Side side;
    const int count = int(split.children.size());
    const int first = before ? handle : handle + 1;
    const int last = push ? (before ? 0 : count - 1) : first;
    for (int i = first; before ? i >= last : i <= last; before ? --i : ++i) {
        const LayoutNode &child = split.children[size_t(i)];
        const int size = extentAlong(layout.rects.value(child.id), o);
        const SizeLimits limits = layout.limits.value(child.id);
        side.index.push_back(i);
        side.shrink.push_back(std::max(0, size - limitAlong(limits.min, o)));
        side.grow.push_back(std::max(0, limitAlong(limits.max, o) - size));
        side.canShrink += side.shrink.back();
        side.canGrow += side.grow.back();
    }
    return side;
}

// Hands `amount` pixels out over `sizes`, in the order of `side`: taken from
// each as far as it can give (`grow` false), or given as far as it can take.
void spread(std::vector<double> &sizes, const Side &side, int amount, bool grow)
{
    for (size_t k = 0; k < side.index.size() && amount > 0; ++k) {
        const int part = std::min(amount, grow ? side.grow[k] : side.shrink[k]);
        sizes[size_t(side.index[k])] += grow ? part : -part;
        amount -= part;
    }
}

} // namespace

std::pair<int, int> SplitterCoordinator::deltaRange(const LayoutTree &tree,
                                                    const SolvedLayout &layout,
                                                    const std::vector<int> &group, bool push)
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
        const Side before = sideOf(*split, layout, handle.index, true, o, push);
        const Side after = sideOf(*split, layout, handle.index, false, o, push);

        // Moving by +d grows what is before the handle and shrinks what is after.
        lowest = int(std::max<qint64>({lowest, -before.canShrink, -after.canGrow}));
        highest = int(std::min<qint64>({highest, before.canGrow, after.canShrink}));
    }
    // A layout already squeezed below its minimums must not be able to move
    // further into the violation, but must stay put-able.
    lowest = std::min(lowest, 0);
    highest = std::max(highest, 0);
    return {lowest, highest};
}

std::vector<SplitterCoordinator::WeightUpdate>
SplitterCoordinator::moveHandles(const LayoutTree &tree, const SolvedLayout &layout,
                                 const std::vector<int> &group, int delta, bool push)
{
    const auto [lowest, highest] = deltaRange(tree, layout, group, push);
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
        if (push) {
            const Side before = sideOf(*split, layout, handle.index, true, handle.orientation, true);
            const Side after = sideOf(*split, layout, handle.index, false, handle.orientation, true);
            spread(update.weights, delta > 0 ? before : after, std::abs(delta), true);
            spread(update.weights, delta > 0 ? after : before, std::abs(delta), false);
        } else {
            update.weights[size_t(handle.index)] += delta;
            update.weights[size_t(handle.index) + 1] -= delta;
        }
        // Weights must stay positive even for a child squeezed to zero pixels.
        for (double &w : update.weights)
            w = std::max(w, 0.01);
        updates.push_back(std::move(update));
    }
    return updates;
}

} // namespace QFlexDock
