// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/LayoutModel.h>

#include <QtCore/QHash>
#include <QtCore/QRect>
#include <QtCore/QSize>

#include <functional>
#include <vector>

namespace QFlexDock {

/// "No upper bound" for a size, same value Qt Widgets uses.
inline constexpr int UnboundedSize = (1 << 24) - 1;

struct SizeLimits
{
    QSize min{0, 0};
    QSize max{UnboundedSize, UnboundedSize};
};

/// Supplies the pixel limits of a tab group (taken from its widgets).
using LimitsProvider = std::function<SizeLimits(const LayoutNode &tabs)>;

/// A boundary between child `index` and `index + 1` of a split.
struct SolvedHandle
{
    NodeId split;
    int index = 0;
    /// Orientation of the split: Horizontal means the handle is a vertical bar
    /// that moves along x.
    Qt::Orientation orientation = Qt::Horizontal;
    QRect rect;
};

/// Pixel geometry computed from a layout tree.
struct SolvedLayout
{
    QHash<NodeId, QRect> rects; // every node
    QHash<NodeId, SizeLimits> limits; // every node, limits of its whole subtree
    std::vector<SolvedHandle> handles;
    int handleWidth = 0;
};

/// Turns model weights into pixel rectangles while honouring minimum and
/// maximum sizes. Pure functions: no widgets, no state.
class QFLEXDOCK_EXPORT LayoutSolver
{
public:
    /// Limits of the subtree rooted at `node`.
    [[nodiscard]] static SizeLimits limits(const LayoutNode &node, int handleWidth,
                                           const LimitsProvider &provider);

    /// With `linesKept`, handles that the weights put in one line (the ones
    /// SplitterCoordinator links) stay in one line where a minimum or a
    /// maximum holds one of them back: all of them go to the place nearest
    /// to their weights that every split can live with. If there is no such
    /// place, each split is solved on its own.
    [[nodiscard]] static SolvedLayout solve(const LayoutTree &tree, const QRect &bounds,
                                            int handleWidth, const LimitsProvider &provider,
                                            bool linesKept = true);

    /// Splits `total` pixels by `weights`, clamped to [mins, maxs]. The result
    /// sums to `total` unless the limits make that impossible: below the sum
    /// of the minimums everything shrinks proportionally, above the sum of the
    /// maximums the surplus stays unused.
    [[nodiscard]] static std::vector<int> distribute(int total, const std::vector<double> &weights,
                                                     const std::vector<int> &mins,
                                                     const std::vector<int> &maxs);
};

} // namespace QFlexDock
