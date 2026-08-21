// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "document.h"

namespace animage {

// How many image pixels one composited entry covers.
//
// Fixed point rather than an integer, and that is the whole of issue #11. An
// integer step cannot follow a continuous zoom, so there was always a zoom at
// which it doubled -- and a 2x change in sampling density for a 1% change in
// zoom is what made a stroke crisp on one side of it and jagged on the other.
// Where that boundary landed was decided by a memory budget rather than by any
// judgement about resolution, so it also moved with the size of the window.
//
// The grid is anchored at the image origin rather than at whatever region is
// being asked for. That is what lets a partial refresh -- one dab of a brush --
// land on exactly the entries a full refresh would have produced, and it keeps
// the sampling phase still under the drawing while the view pans across it.
class SampleStep {
public:
    static constexpr int kFractionBits = 16;
    static constexpr std::int64_t kOne = std::int64_t{1} << kFractionBits;

    SampleStep() = default;

    // Never below one image pixel per entry: magnifying is the blit's job, and
    // a cache finer than the drawing would only cost memory to hold detail
    // that is not there.
    static SampleStep fromRatio(double image_pixels_per_entry);

    double ratio() const { return static_cast<double>(raw_) / static_cast<double>(kOne); }
    bool isOne() const { return raw_ == kOne; }

    friend bool operator==(const SampleStep&, const SampleStep&) = default;

    // Where an entry starts, exactly, in 16.16 image pixels. The grid is
    // continuous -- an entry boundary falls between two image pixels far more
    // often than on one -- and pretending otherwise is what makes a reduction
    // ragged: at 1.4 image pixels an entry, rounding the boundaries gives some
    // entries one pixel and some two, and that alternation is a worse artefact
    // than the shimmer it was meant to remove.
    long long entryTop(long long entry) const { return entry * raw_; }

    // The same edge in whole image pixels, for the blit. A run of entries
    // covers entryEdge(first) to entryEdge(first + count) and *not* the integer
    // rectangle around it: the two differ by up to a pixel, which stretches the
    // cache by a part in a thousand over the width of a window and slides a
    // curve off where the drawing says it is.
    double entryEdge(long long entry) const {
        return static_cast<double>(entryTop(entry)) / static_cast<double>(kOne);
    }

    // The first image pixel belonging to an entry, and the entry an image pixel
    // starts inside. Inverses: entryAt(entryBegin(e)) == e.
    int entryBegin(long long entry) const;
    long long entryAt(int image_coordinate) const;

    // How many entries it takes to cover `extent` image pixels starting at
    // `origin`, counting the entries that origin and its far end fall inside.
    int entriesAcross(int origin, int extent) const;

private:
    explicit SampleStep(std::int64_t raw) : raw_(raw) {}
    std::int64_t raw_ = kOne;
};

// Grows a rectangle out to the entry boundaries around it, so that its corners
// are entries rather than fractions of one. Every region handed to the
// compositor should be snapped: an entry clipped by the edge of the region is
// averaged from fewer samples than it covers.
PixelRect snapToSampleGrid(const SampleStep& step, const PixelRect& region);

// How far apart the image pixels read inside one entry are. A full box filter
// reads every pixel under the entry, which at 10% zoom is a hundred of them for
// one entry nobody can see a hundredth of; sampling the block on a lattice
// bounds that while staying exact wherever it matters. Exposed so a benchmark
// can reproduce the reduction rather than guess at it.
int boxSampleStride(const SampleStep& step);

// A block of linear, premultiplied RGBA. This is what the compositor produces
// and what the display path converts to sRGB at the very last moment.
class Framebuffer {
public:
    Framebuffer() = default;
    Framebuffer(int width, int height) { resizeCleared(width, height); }

    // Grows or shrinks, and says nothing about what is in it afterwards.
    //
    // It used to empty the buffer as well, and `compositeGrids` calls `clear()`
    // straight after it -- so every composite wrote a viewport-sized buffer to
    // zero twice before compositing anything into it. At 1642x777 entries that
    // is 20 MB written twice, and the onion skin pays it once per neighbouring
    // drawing on every pan step.
    void resize(int width, int height);

    // Both at once, in one pass rather than two, for the callers that need an
    // empty buffer of a given size. What `resize` used to be.
    void resizeCleared(int width, int height);

    void clear();

    int width() const { return width_; }
    int height() const { return height_; }
    bool isEmpty() const { return width_ <= 0 || height_ <= 0; }

    Rgba* row(int y) { return pixels_.data() + static_cast<std::size_t>(y) * width_; }
    const Rgba* row(int y) const { return pixels_.data() + static_cast<std::size_t>(y) * width_; }

    Rgba pixel(int x, int y) const { return row(y)[x]; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<Rgba> pixels_;
};

// One layer's pixels and the properties to draw them with.
//
// Resolving a layer id to these two is the first thing compositeLayers does,
// and it needs the document to do it. A caller that has already resolved them
// hands them over instead -- which is what a solve running on another thread
// has to do, because the document belongs to the thread that edits it and the
// tiles, being immutable once shared, do not.
struct LayerPass {
    const TileGrid* tiles = nullptr;
    const Layer* layer = nullptr;

    // Drawn this far from where its pixels are stored. Only one thing uses it:
    // a colour layer showing its marks has to show them where they were used,
    // and on a layer that moves carried marks that is not where they were
    // drawn. Zero for everything else, and zero is the path that existed
    // before this.
    CtgShift offset{};

    // ...and the second kind of source: a colour layer's fill, which is not a
    // picture. Set instead of `tiles`, never as well, and everything else here
    // applies to it exactly as it does to a grid -- a colour layer has an
    // opacity like any other.
    //
    // A fill is drawn where it is: the shift is applied inside the accessor,
    // when the marks are read, so this pass always carries `offset` zero and
    // takes the ordinary column plan unchanged. That is why "which rectangle
    // counts the columns, and which sizes the buffer" does not apply to it --
    // the corner is not entered rather than being guarded against.
    const CtgFill* fill = nullptr;
};

// A layer whose pixels are not what the document says, for one composite.
//
// One caller: a live transform. What was picked up is being drawn somewhere
// else, so what stands in the layer's own place is the rest of it -- and it has
// to stand *there*, in the layer's own order, rather than being painted over the
// top where it would cover the layers above it.
//
// Not a visibility flag and not a temporary write to the cel. Visibility is a
// property of the layer and is saved with the project; a temporary write is the
// "lift into the document" the design refused, which makes Escape unwind a
// command and leaves an undo entry for a thing that did not happen.
struct SubstitutedLayer {
    LayerId layer = kNoId;
    // Null means the layer is not drawn at all, which is what a transform with
    // no selection wants: the whole cel was picked up.
    const TileGrid* tiles = nullptr;
};

// Flattens the layers of one image.
//
// This is the CPU reference implementation. The plan calls for QRhi doing this
// on the GPU, and it should -- but the reference is worth keeping afterwards:
// it is what a test can compare against when a shader starts disagreeing.
class Compositor {
public:
    // `region` is in image coordinates; the result fills the framebuffer from
    // its top-left corner. Layers composite in list order, index 0 on top.
    //
    // `step` says how many image pixels each entry stands for, so that zooming
    // out does not ask for a buffer the size of the visible image area: at 10%
    // zoom a viewport covers a hundred times more image pixels than it has
    // screen pixels. Each entry is the *average* of the block it covers, which
    // is the reconstruction a screen pixel wants; taking one pixel of the block
    // and discarding the rest is what made thin lines shimmer when zoomed out,
    // and is the same trap the CTG barrier already records from the other end.
    //
    // `region` should be snapped to the grid -- see snapToSampleGrid -- or the
    // entries at its edges are averaged from part of their block.
    void composite(const Document& doc, TrackId track, ImageId image,
                   const PixelRect& region, Framebuffer& out, SampleStep step = {}) const;

    // Same, for an arbitrary set of layers in the order given, topmost first.
    // The CTG layer will need this to flatten several line-art layers into one
    // barrier, and onion skin needs it to draw a neighbouring image alone.
    void compositeLayers(const Document& doc, TrackId track, ImageId image,
                         const std::vector<LayerId>& layers, const PixelRect& region,
                         Framebuffer& out, SampleStep step = {}) const;

    // The whole picture at one frame of the timeline: every track, stacked.
    //
    // Tracks stack as flat groups -- every layer of track 0 over every layer of
    // track 1 -- so this is one list of layers composited once, and not several
    // tracks composited apart and then blended. That is not only cheaper, it is
    // the definition: a group that could be blended separately would need an
    // opacity of its own applied to it, and track opacity is stored and not yet
    // applied. If it ever is, this is the function that stops being a flat list.
    //
    // Which drawing each track shows at `slot` is Track::imageShownAt's answer
    // and nobody else's -- tracks are not all the same length, and what a track
    // does past its end is a decision that lives there. A track showing nothing
    // contributes nothing rather than clearing what is under it.
    //
    // `substituted` stands in for one layer's pixels. See SubstitutedLayer.
    void compositeScene(const Document& doc, std::size_t slot, const PixelRect& region,
                        Framebuffer& out, SampleStep step = {},
                        const SubstitutedLayer& substituted = {}) const;

    // The same again with the layers already resolved to pixels, topmost first,
    // and every one of them drawn -- visibility, CTG fills and absent cels have
    // all been decided by whoever built the list. This is the whole of the
    // compositor that a background thread may use, because it names nothing that
    // the interface thread can edit underneath it.
    void compositeGrids(const std::vector<LayerPass>& passes, const PixelRect& region,
                        Framebuffer& out, SampleStep step = {}) const;
};

// The bounding box of everything drawn on an image, in pixels, or an empty rect
// if nothing has been drawn. Tile-aligned, since that is what it is derived
// from.
PixelRect imageBounds(const Document& doc, TrackId track, ImageId image);

}  // namespace animage
