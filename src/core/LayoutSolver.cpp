// SPDX-License-Identifier: MIT
#include "core/LayoutSolver.h"

#include <algorithm>
#include <cmath>
#include <numeric>

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
    if (node.isTabs()) {
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

void place(const LayoutNode &node, const QRect &rect, SolvedLayout &out)
{
    out.rects.insert(node.id, rect);
    if (node.isTabs())
        return;

    const Qt::Orientation o = node.orientation;
    const int count = int(node.children.size());
    const int available = std::max(0, along(rect.size(), o) - out.handleWidth * (count - 1));

    std::vector<double> weights;
    std::vector<int> mins;
    std::vector<int> maxs;
    for (const auto &child : node.children) {
        const SizeLimits l = out.limits.value(child.id);
        weights.push_back(child.weight);
        mins.push_back(along(l.min, o));
        maxs.push_back(along(l.max, o));
    }
    const std::vector<int> sizes = LayoutSolver::distribute(available, weights, mins, maxs);

    int pos = o == Qt::Horizontal ? rect.x() : rect.y();
    for (int i = 0; i < count; ++i) {
        const QRect childRect = o == Qt::Horizontal
            ? QRect(pos, rect.y(), sizes[size_t(i)], rect.height())
            : QRect(rect.x(), pos, rect.width(), sizes[size_t(i)]);
        place(node.children[size_t(i)], childRect, out);
        pos += sizes[size_t(i)];
        if (i + 1 < count) {
            SolvedHandle handle;
            handle.split = node.id;
            handle.index = i;
            handle.orientation = o;
            handle.rect = o == Qt::Horizontal
                ? QRect(pos, rect.y(), out.handleWidth, rect.height())
                : QRect(rect.x(), pos, rect.width(), out.handleWidth);
            out.handles.push_back(handle);
            pos += out.handleWidth;
        }
    }
}

} // namespace

SizeLimits LayoutSolver::limits(const LayoutNode &node, int handleWidth,
                                const LimitsProvider &provider)
{
    return computeLimits(node, handleWidth, provider, nullptr);
}

SolvedLayout LayoutSolver::solve(const LayoutTree &tree, const QRect &bounds, int handleWidth,
                                 const LimitsProvider &provider)
{
    SolvedLayout out;
    out.handleWidth = std::max(0, handleWidth);
    if (const LayoutNode *root = tree.root()) {
        computeLimits(*root, out.handleWidth, provider, &out.limits);
        place(*root, bounds, out);
    }
    return out;
}

std::vector<int> LayoutSolver::distribute(int total, const std::vector<double> &weights,
                                          const std::vector<int> &mins,
                                          const std::vector<int> &maxs)
{
    const size_t n = weights.size();
    std::vector<int> result(n, 0);
    if (n == 0 || total <= 0)
        return result;

    const qint64 minSum = std::accumulate(mins.begin(), mins.end(), qint64(0));
    std::vector<double> size(n, 0.0);

    if (minSum >= total) {
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

} // namespace QFlexDock
