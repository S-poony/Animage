// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "document.h"

namespace animage {

// A block of linear, premultiplied RGBA. This is what the compositor produces
// and what the display path converts to sRGB at the very last moment.
class Framebuffer {
public:
    Framebuffer() = default;
    Framebuffer(int width, int height) { resize(width, height); }

    void resize(int width, int height);
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
    // `step` samples every nth image pixel, producing a framebuffer that many
    // times smaller. It exists so that zooming out does not ask for a buffer
    // the size of the visible image area: at 10% zoom a viewport covers a
    // hundred times more image pixels than it has screen pixels, and there is
    // no point flattening detail nobody can see. Point sampling, so thin lines
    // do shimmer when zoomed far out.
    void composite(const Document& doc, TrackId track, ImageId image,
                   const PixelRect& region, Framebuffer& out, int step = 1) const;

    // Same, for an arbitrary set of layers in the order given, topmost first.
    // The CTG layer will need this to flatten several line-art layers into one
    // barrier, and onion skin needs it to draw a neighbouring image alone.
    void compositeLayers(const Document& doc, TrackId track, ImageId image,
                         const std::vector<LayerId>& layers, const PixelRect& region,
                         Framebuffer& out, int step = 1) const;
};

// The bounding box of everything drawn on an image, in pixels, or an empty rect
// if nothing has been drawn. Tile-aligned, since that is what it is derived
// from.
PixelRect imageBounds(const Document& doc, TrackId track, ImageId image);

}  // namespace animage
