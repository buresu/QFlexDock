// SPDX-License-Identifier: MIT
#pragma once

#include "core/LayoutSolver.h"

#include <utility>
#include <functional>
#include <vector>

namespace QFlexDock {

/// Linked splitter logic: which boundaries move together, how far they can
/// move, and what the model weights become. Pure functions over a solved
/// layout, so the rules can be tested without widgets.
///
/// Linking rule. Two handles are linked when
///  1. they belong to different splits of the same orientation,
///  2. they lie on the same line: their positions on the movement axis differ
///     by less than one handle width (their bars visually overlap), and
///  3. they are contiguous along that line: the gap between their ends is at
///     most one handle width (the thickness of a crossing handle).
/// The relation is closed transitively, so a whole run of aligned handles
/// moves as one. Handles that are aligned but separated by a panel are not
/// linked.
///
/// Corners. Where a boundary between columns meets one between rows, the two
/// can be taken hold of at once and moved in both directions. In a layout
/// made of nested splits such a meeting is always a T or a cross: one bar
/// runs through, the other ends on it (from one side or from both).
class QFLEXDOCK_EXPORT SplitterCoordinator
{
public:
    struct WeightUpdate
    {
        NodeId split;
        std::vector<double> weights;
    };

    /// A place where bars of both orientations meet.
    struct Corner
    {
        /// The patch the bars share: as wide as the upright bar, as high as
        /// the level one.
        QRect rect;
        /// Handles (indices into `layout.handles`) moving along x that meet here.
        std::vector<int> columns;
        /// Handles moving along y that meet here.
        std::vector<int> rows;
    };

    /// Every corner of `layout`. Bars that meet at (nearly) the same place,
    /// like the two halves of a cross, are reported as one corner.
    [[nodiscard]] static std::vector<Corner> corners(const SolvedLayout &layout);

    /// A split handle as far as corners are concerned.
    struct Bar
    {
        QRect rect;
        /// Of its split: Qt::Horizontal is an upright bar moving along x.
        Qt::Orientation orientation = Qt::Horizontal;
        /// How far its ends may be from a bar they meet. More than nothing
        /// for the bars of a dock area that lies within another one: there
        /// is the frame of a tab group between them and the bars around.
        int reach = 0;
    };
    /// The same for any set of bars; the indices are into `bars`.
    [[nodiscard]] static std::vector<Corner> corners(const std::vector<Bar> &bars);

    /// A child of a split that a drag would leave less than half of its
    /// minimum size, and the sibling across the dragged handle.
    struct Squeezed
    {
        NodeId node;
        NodeId heir;
        friend bool operator==(const Squeezed &, const Squeezed &) = default;
    };
    /// The tab groups that moving `group` by `delta` pixels (not clamped,
    /// relative to `layout`) squeezes that far, among those `mayGo` allows.
    [[nodiscard]] static std::vector<Squeezed>
    squeezed(const LayoutTree &tree, const SolvedLayout &layout, const std::vector<int> &group,
             int delta, const std::function<bool(const LayoutNode &)> &mayGo);

    /// Indices (into `layout.handles`) of the handles linked with
    /// `handleIndex`, itself included and listed first.
    [[nodiscard]] static std::vector<int> linkedHandles(const SolvedLayout &layout,
                                                        int handleIndex);

    /// Range [min, max] of pixel offsets by which every handle in `group` can
    /// move without breaking any minimum or maximum size. Always contains 0.
    [[nodiscard]] static std::pair<int, int> deltaRange(const LayoutTree &tree,
                                                        const SolvedLayout &layout,
                                                        const std::vector<int> &group);

    /// Weights that move every handle in `group` by `delta` pixels relative to
    /// `layout` (the layout at the start of the drag). `delta` is clamped to
    /// deltaRange().
    [[nodiscard]] static std::vector<WeightUpdate> moveHandles(const LayoutTree &tree,
                                                               const SolvedLayout &layout,
                                                               const std::vector<int> &group,
                                                               int delta);
};

} // namespace QFlexDock
