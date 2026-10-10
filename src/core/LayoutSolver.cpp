// SPDX-License-Identifier: MIT
#include "core/LayoutSolver.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <utility>

namespace QFlexDock {

namespace {

int along(const QSize &size, Qt::Orientation o)
{
    return o == Qt::Horizontal ? size.width() : size.height();
}

int across(const QSize &size, Qt::Orientation o)
{
    return o == Qt::Horizontal ? size.height() : size.width();
}

QSize makeSize(int alongValue, int acrossValue, Qt::Orientation o)
{
    return o == Qt::Horizontal ? QSize(alongValue, acrossValue) : QSize(acrossValue, alongValue);
}

SizeLimits sanitized(SizeLimits l)
{
    l.min = l.min.expandedTo(QSize(0, 0)).boundedTo(QSize(UnboundedSize, UnboundedSize));
    l.max = l.max.boundedTo(QSize(UnboundedSize, UnboundedSize)).expandedTo(l.min);
    return l;
}

SizeLimits computeLimits(const LayoutNode &node, int handleWidth, const LimitsProvider &provider,
                         QHash<NodeId, SizeLimits> *cache)
{
    SizeLimits result;
    if (node.isTabs() || node.iconified) {
        result = sanitized(provider ? provider(node) : SizeLimits{});
    } else {
        const Qt::Orientation o = node.orientation;
        const int handles = handleWidth * int(node.children.size() - 1);
        qint64 minAlong = handles;
        qint64 maxAlong = handles;
        int minAcross = 0;
        int maxAcross = UnboundedSize;
        for (const auto &child : node.children) {
            const SizeLimits c = computeLimits(child, handleWidth, provider, cache);
            minAlong += along(c.min, o);
            maxAlong += along(c.max, o);
            minAcross = std::max(minAcross, across(c.min, o));
            maxAcross = std::min(maxAcross, across(c.max, o));
        }
        result.min = makeSize(int(std::min<qint64>(minAlong, UnboundedSize)), minAcross, o);
        result.max = makeSize(int(std::min<qint64>(maxAlong, UnboundedSize)), maxAcross, o);
        result = sanitized(result);
    }
    if (cache)
        cache->insert(node.id, result);
    return result;
}

int startAlong(const QRect &rect, Qt::Orientation o)
{
    return o == Qt::Horizontal ? rect.x() : rect.y();
}

// Handles held in place: for a split, the position of the bars (by their
// index in the split) on the axis they move along.
using Pins = QHash<NodeId, std::map<int, int>>;

std::vector<int> distributed(int total, const std::vector<double> &weights,
                             const std::vector<int> &mins, const std::vector<int> &maxs,
                             bool *limited);

// Weights and limits of the children of a split, along its axis.
struct Shares
{
    std::vector<double> weights;
    std::vector<int> mins;
    std::vector<int> maxs;
};

Shares sharesOf(const LayoutNode &split, const QHash<NodeId, SizeLimits> &limits)
{
    Shares shares;
    for (const auto &child : split.children) {
        const SizeLimits l = limits.value(child.id);
        shares.weights.push_back(child.weight);
        shares.mins.push_back(along(l.min, split.orientation));
        shares.maxs.push_back(along(l.max, split.orientation));
    }
    return shares;
}

// The sizes of the children of a split with some of its handles held in
// place: every stretch between two of them is handed out on its own. False if
// that leaves a stretch less than its minimums or more than its maximums.
bool heldSizes(const std::map<int, int> &held, int start, int extent, int handleWidth,
               const Shares &shares, std::vector<int> *sizes)
{
    const int count = int(shares.weights.size());
    sizes->clear();
    int from = 0;
    int pos = start;
    for (auto it = held.begin();; ++it) {
        const bool last = it == held.end();
        const int to = last ? count - 1 : it->first; // the last child of this stretch
        const int end = last ? start + extent : it->second;
        if (to < from || (!last && to >= count - 1))
            return false;
        const auto first = size_t(from);
        const auto behind = size_t(to) + 1;
        const std::vector<double> weights(shares.weights.begin() + qsizetype(first),
                                          shares.weights.begin() + qsizetype(behind));
        const std::vector<int> mins(shares.mins.begin() + qsizetype(first),
                                    shares.mins.begin() + qsizetype(behind));
        const std::vector<int> maxs(shares.maxs.begin() + qsizetype(first),
                                    shares.maxs.begin() + qsizetype(behind));
        const int total = end - pos - handleWidth * (to - from);
        if (total < std::accumulate(mins.begin(), mins.end(), qint64(0))
            || total > std::accumulate(maxs.begin(), maxs.end(), qint64(0))) {
            return false;
        }
        const std::vector<int> part = distributed(total, weights, mins, maxs, nullptr);
        if (std::accumulate(part.begin(), part.end(), qint64(0)) != total)
            return false;
        sizes->insert(sizes->end(), part.begin(), part.end());
        if (last)
            break;
        pos = end + handleWidth;
        from = to + 1;
    }
    return true;
}

struct Placing
{
    SolvedLayout &out;
    const Pins *pins = nullptr;
    /// For placing a part of a layout again: where the handles of each split
    /// are in `out.handles`. Without it they are appended.
    const QHash<NodeId, std::vector<int>> *bars = nullptr;
    /// Set when a minimum or maximum made a split differ from its weights.
    bool limited = false;
};

void placeChildren(const LayoutNode &node, const QRect &rect, Placing &placing);

void place(const LayoutNode &node, const QRect &rect, Placing &placing)
{
    SolvedLayout &out = placing.out;
    out.rects.insert(node.id, rect);
    // An iconified column is one thing, whatever is in it.
    if (node.isTabs() || node.iconified)
        return;
    placeChildren(node, rect, placing);
}

void placeChildren(const LayoutNode &node, const QRect &rect, Placing &placing)
{
    SolvedLayout &out = placing.out;

    const Qt::Orientation o = node.orientation;
    const int count = int(node.children.size());
    const int available = std::max(0, along(rect.size(), o) - out.handleWidth * (count - 1));

    const Shares shares = sharesOf(node, out.limits);
    std::vector<int> sizes;
    const auto held = placing.pins ? placing.pins->constFind(node.id) : Pins::const_iterator();
    if (!placing.pins || held == placing.pins->constEnd() || held->empty()
        || !heldSizes(*held, startAlong(rect, o), along(rect.size(), o), out.handleWidth, shares,
                      &sizes)) {
        sizes = distributed(available, shares.weights, shares.mins, shares.maxs, &placing.limited);
    }

    int pos = startAlong(rect, o);
    for (int i = 0; i < count; ++i) {
        const QRect childRect = o == Qt::Horizontal
            ? QRect(pos, rect.y(), sizes[size_t(i)], rect.height())
            : QRect(rect.x(), pos, rect.width(), sizes[size_t(i)]);
        // (Placed again, a child that keeps its place keeps what is in it:
        // a split is placed again whenever one of its handles is held anew.)
        const LayoutNode &child = node.children[size_t(i)];
        if (!placing.bars || out.rects.value(child.id) != childRect)
            place(child, childRect, placing);
        pos += sizes[size_t(i)];
        if (i + 1 < count) {
            SolvedHandle handle;
            handle.split = node.id;
            handle.index = i;
            handle.orientation = o;
            handle.rect = o == Qt::Horizontal
                ? QRect(pos, rect.y(), out.handleWidth, rect.height())
                : QRect(rect.x(), pos, rect.width(), out.handleWidth);
            if (placing.bars)
                out.handles[size_t(placing.bars->value(node.id)[size_t(i)])] = handle;
            else
                out.handles.push_back(handle);
            pos += out.handleWidth;
        }
    }
}

// --- Keeping lines -----------------------------------------------------------
//
// Splits are solved one by one, so two bars that the weights put in one line
// part as soon as a minimum or a maximum holds one of them back. They are
// brought together again afterwards: the bars of a line are held at the place
// nearest to their weights that all of their splits can live with.

// A handle's position on the axis it moves along, and its extent along the line.
struct Run
{
    int position;
    int start;
    int end; // exclusive
};

Run runOf(const SolvedHandle &h)
{
    if (h.orientation == Qt::Horizontal)
        return {h.rect.x(), h.rect.y(), h.rect.y() + h.rect.height()};
    return {h.rect.y(), h.rect.x(), h.rect.x() + h.rect.width()};
}

// The lines of `layout` that are made of several handles (indices into its
// handles): the same rule as SplitterCoordinator::linkedHandles(). Bars of
// one line follow one another with the bar that crosses them in between.
std::vector<std::vector<int>> linesOf(const SolvedLayout &layout)
{
    const int count = int(layout.handles.size());
    const int reach = std::max(1, layout.handleWidth);
    const auto positionOf = [&layout](int i) { return runOf(layout.handles[size_t(i)]).position; };
    QHash<std::pair<int, int>, std::vector<int>> byStart;
    for (int i = 0; i < count; ++i) {
        const SolvedHandle &h = layout.handles[size_t(i)];
        byStart[{int(h.orientation), runOf(h).start}].push_back(i);
    }
    for (std::vector<int> &bars : byStart) {
        std::sort(bars.begin(), bars.end(),
                  [&](int a, int b) { return positionOf(a) < positionOf(b); });
    }

    std::vector<int> line(size_t(count), 0);
    std::iota(line.begin(), line.end(), 0);
    const auto lineOf = [&line](int i) {
        while (line[size_t(i)] != i)
            i = line[size_t(i)] = line[size_t(line[size_t(i)])];
        return i;
    };
    for (int i = 0; i < count; ++i) {
        const SolvedHandle &h = layout.handles[size_t(i)];
        const Run run = runOf(h);
        const auto next = byStart.constFind({int(h.orientation), run.end + layout.handleWidth});
        if (next == byStart.constEnd())
            continue;
        auto other = std::lower_bound(next->begin(), next->end(), run.position - reach + 1,
                                      [&](int j, int position) { return positionOf(j) < position; });
        for (; other != next->end() && positionOf(*other) < run.position + reach; ++other) {
            if (layout.handles[size_t(*other)].split != h.split)
                line[size_t(lineOf(*other))] = lineOf(i);
        }
    }

    QHash<int, int> slot;
    std::vector<std::vector<int>> lines;
    for (int i = 0; i < count; ++i) {
        const int root = lineOf(i);
        if (!slot.contains(root)) {
            slot.insert(root, int(lines.size()));
            lines.emplace_back();
        }
        std::vector<int> &members = lines[size_t(slot.value(root))];
        // (A split has one bar in a line; a second one can only get here
        // through bars that are each a little off the next.)
        const bool sameSplit = std::any_of(members.begin(), members.end(), [&](int m) {
            return layout.handles[size_t(m)].split == layout.handles[size_t(i)].split;
        });
        if (!sameSplit)
            members.push_back(i);
    }
    std::erase_if(lines, [](const std::vector<int> &members) { return members.size() < 2; });
    return lines;
}

// Holds the bars of `line` in one place if they have parted in `now`. Returns
// the splits that have to be placed again for it.
std::vector<NodeId> holdLine(const QHash<NodeId, const LayoutNode *> &splits,
                             const std::vector<int> &line, const SolvedLayout &byWeights,
                             const SolvedLayout &now, Pins &pins)
{
    struct Member
    {
        NodeId split;
        int index = 0;
        int offset = 0; // from the first bar of the line, as the weights have it
    };
    std::vector<Member> members;
    const int origin = runOf(byWeights.handles[size_t(line.front())]).position;
    const auto offsetOf = [&](int k) {
        return runOf(byWeights.handles[size_t(k)]).position - origin;
    };
    const int where = runOf(now.handles[size_t(line.front())]).position;
    const bool together = std::all_of(line.begin(), line.end(), [&](int k) {
        return runOf(now.handles[size_t(k)]).position - offsetOf(k) == where;
    });
    if (together)
        return {};

    qint64 lowest = -UnboundedSize;
    qint64 highest = UnboundedSize;
    qint64 wanted = 0;
    const int handleWidth = now.handleWidth;

    for (int k : line) {
        const SolvedHandle &handle = now.handles[size_t(k)];
        const LayoutNode *split = splits.value(handle.split);
        if (!split || handle.index + 1 >= int(split->children.size()))
            return {};
        const Qt::Orientation o = handle.orientation;
        const int count = int(split->children.size());
        const Shares shares = sharesOf(*split, now.limits);
        const QRect rect = now.rects.value(split->id);
        const int offset = offsetOf(k);

        // The stretch the bar can move in: its split, or what other bars of
        // the split that are held leave of it.
        int from = 0;
        int to = count - 1;
        qint64 start = startAlong(rect, o);
        qint64 end = start + along(rect.size(), o);
        if (const auto held = pins.constFind(split->id); held != pins.constEnd()) {
            for (const auto &[index, at] : *held) {
                if (index < handle.index) {
                    from = index + 1;
                    start = at + handleWidth;
                } else if (index > handle.index) {
                    to = index;
                    end = at;
                    break;
                }
            }
        }
        qint64 minBefore = handleWidth * (handle.index - from);
        qint64 maxBefore = minBefore;
        for (int i = from; i <= handle.index; ++i) {
            minBefore += shares.mins[size_t(i)];
            maxBefore += shares.maxs[size_t(i)];
        }
        qint64 minAfter = handleWidth * (to - handle.index);
        qint64 maxAfter = minAfter;
        for (int i = handle.index + 1; i <= to; ++i) {
            minAfter += shares.mins[size_t(i)];
            maxAfter += shares.maxs[size_t(i)];
        }
        lowest = std::max({lowest, start + minBefore - offset, end - maxAfter - offset});
        highest = std::min({highest, start + maxBefore - offset, end - minAfter - offset});

        // Where the weights alone would have it in the split as it lies now.
        const int available = std::max(0, along(rect.size(), o) - handleWidth * (count - 1));
        const std::vector<int> sizes = distributed(available, shares.weights,
                                                   std::vector<int>(size_t(count), 0),
                                                   std::vector<int>(size_t(count), UnboundedSize),
                                                   nullptr);
        wanted += startAlong(rect, o) + handleWidth * handle.index - offset
            + std::accumulate(sizes.begin(), sizes.begin() + handle.index + 1, qint64(0));

        members.push_back(Member{split->id, handle.index, offset});
    }
    std::vector<NodeId> changed;
    if (lowest > highest) {
        // No place suits them all: each split on its own, as if not in line.
        for (const Member &m : members) {
            const auto held = pins.find(m.split);
            if (held != pins.end() && held->erase(m.index) > 0)
                changed.push_back(m.split);
        }
        return changed;
    }
    const qint64 mean = std::llround(double(wanted) / double(members.size()));
    const int target = int(std::clamp(mean, lowest, highest));
    for (const Member &m : members) {
        std::map<int, int> &held = pins[m.split];
        const auto it = held.find(m.index);
        if (it == held.end() || it->second != target + m.offset) {
            held[m.index] = target + m.offset;
            changed.push_back(m.split);
        }
    }
    return changed;
}

void collectSplits(const LayoutNode &node, QHash<NodeId, const LayoutNode *> &splits)
{
    if (node.isTabs() || node.iconified)
        return;
    splits.insert(node.id, &node);
    for (const auto &child : node.children)
        collectSplits(child, splits);
}

void keepLines(const LayoutNode &root, const QRect &bounds, SolvedLayout &out)
{
    // Which bars belong in one line is what the weights say, limits aside.
    SolvedLayout byWeights;
    byWeights.handleWidth = out.handleWidth;
    Placing unlimited{byWeights};
    place(root, bounds, unlimited);
    const std::vector<std::vector<int>> lines = linesOf(byWeights);
    if (lines.empty())
        return;

    QHash<NodeId, const LayoutNode *> splits;
    collectSplits(root, splits);
    QHash<NodeId, std::vector<int>> bars;
    for (int k = 0; k < int(out.handles.size()); ++k) {
        const SolvedHandle &handle = out.handles[size_t(k)];
        std::vector<int> &ofSplit = bars[handle.split];
        if (ofSplit.empty())
            ofSplit.resize(splits.value(handle.split)->children.size() - 1);
        ofSplit[size_t(handle.index)] = k;
    }

    // Holding one line moves what lies inside the splits around it, so the
    // lines are gone through until nothing moves any more (or, for lines
    // that keep pushing each other about, a few times).
    Pins pins;
    for (int round = 0; round < 4; ++round) {
        bool changed = false;
        for (const std::vector<int> &line : lines) {
            for (const NodeId &split : holdLine(splits, line, byWeights, out, pins)) {
                Placing again{out, &pins, &bars};
                placeChildren(*splits.value(split), out.rects.value(split), again);
                changed = true;
            }
        }
        if (!changed)
            break;
    }
}

} // namespace

SizeLimits LayoutSolver::limits(const LayoutNode &node, int handleWidth,
                                const LimitsProvider &provider)
{
    return computeLimits(node, handleWidth, provider, nullptr);
}

SolvedLayout LayoutSolver::solve(const LayoutTree &tree, const QRect &bounds, int handleWidth,
                                 const LimitsProvider &provider, bool linesKept)
{
    SolvedLayout out;
    out.handleWidth = std::max(0, handleWidth);
    if (const LayoutNode *root = tree.root()) {
        computeLimits(*root, out.handleWidth, provider, &out.limits);
        Placing placing{out};
        place(*root, bounds, placing);
        // Without a limit in the way, every bar is where its weights put it
        // already. And in less room than its minimum the layout only
        // shrinks, everything in proportion: no bar has a place to go to.
        const QSize minimum = out.limits.value(root->id).min;
        if (linesKept && placing.limited && out.handles.size() > 1
            && bounds.width() >= minimum.width() && bounds.height() >= minimum.height()) {
            keepLines(*root, bounds, out);
        }
    }
    return out;
}

std::vector<int> LayoutSolver::distribute(int total, const std::vector<double> &weights,
                                          const std::vector<int> &mins,
                                          const std::vector<int> &maxs)
{
    return distributed(total, weights, mins, maxs, nullptr);
}

namespace {

std::vector<int> distributed(int total, const std::vector<double> &weights,
                             const std::vector<int> &mins, const std::vector<int> &maxs,
                             bool *limited)
{
    const size_t n = weights.size();
    std::vector<int> result(n, 0);
    if (n == 0 || total <= 0)
        return result;

    const qint64 minSum = std::accumulate(mins.begin(), mins.end(), qint64(0));
    std::vector<double> size(n, 0.0);

    if (minSum >= total) {
        if (limited)
            *limited = true;
        // Not enough room for the minimums: shrink everyone in proportion.
        for (size_t i = 0; i < n; ++i)
            size[i] = minSum > 0 ? double(mins[i]) * total / double(minSum) : 0.0;
    } else {
        // Flex-style resolution: hand out by weight, freeze whoever hits a
        // limit, redistribute the rest among the others.
        std::vector<bool> frozen(n, false);
        std::vector<double> violation(n, 0.0);
        double remaining = total;
        for (;;) {
            double weightSum = 0.0;
            for (size_t i = 0; i < n; ++i) {
                if (!frozen[i])
                    weightSum += weights[i];
            }
            if (weightSum <= 0.0)
                break;
            double totalViolation = 0.0;
            for (size_t i = 0; i < n; ++i) {
                if (frozen[i])
                    continue;
                const double ideal = remaining * weights[i] / weightSum;
                size[i] = std::clamp(ideal, double(mins[i]), double(std::max(mins[i], maxs[i])));
                violation[i] = size[i] - ideal;
                totalViolation += violation[i];
                if (limited && violation[i] != 0.0)
                    *limited = true;
            }
            if (std::abs(totalViolation) < 1e-9)
                break;
            bool frozeAny = false;
            for (size_t i = 0; i < n; ++i) {
                if (frozen[i])
                    continue;
                if (totalViolation > 0 ? violation[i] > 0 : violation[i] < 0) {
                    frozen[i] = true;
                    remaining -= size[i];
                    frozeAny = true;
                }
            }
            if (!frozeAny)
                break;
        }
    }

    // Round to whole pixels without losing or inventing any: floor, then give
    // the leftover pixels to the largest fractional parts.
    double exactSum = 0.0;
    qint64 floorSum = 0;
    std::vector<std::pair<double, size_t>> fractions;
    for (size_t i = 0; i < n; ++i) {
        exactSum += size[i];
        result[i] = int(std::floor(size[i] + 1e-9));
        floorSum += result[i];
        fractions.emplace_back(size[i] - result[i], i);
    }
    qint64 leftover = std::min<qint64>(total, std::llround(exactSum)) - floorSum;
    std::stable_sort(fractions.begin(), fractions.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    for (size_t k = 0; k < n && leftover > 0; ++k, --leftover)
        ++result[fractions[k].second];
    return result;
}

} // namespace

} // namespace QFlexDock
