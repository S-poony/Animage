// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "ids.h"

namespace animage {

enum class LayerKind {
    Raster,
    Ctg,  // stores scribbles, not pixels; the fill is regenerated
};

// Which way a CTG layer carries scribbles to drawings that have none.
enum class CtgDirection {
    Forward,   // a drawing shows the nearest earlier one's marks
    Backward,  // ...or the nearest later one's
    Nearest,   // ...or whichever of the two is fewer drawings away
};

// Only Normal is implemented in the prototype. The rest are here because the
// serialised format has to be able to name them before the compositor can do
// anything with them.
enum class BlendMode {
    Normal,
    Multiply,
    Screen,
    Add,
};

// Properties only. A Layer never holds pixels, and it belongs to the track
// rather than to any one image: adding a layer adds it to every image at once,
// and changing its opacity changes it for the whole track.
struct Layer {
    LayerId id = kNoId;
    std::string name;
    float opacity = 1.0f;
    bool visible = true;
    bool locked = false;
    LayerKind kind = LayerKind::Raster;
    BlendMode blend = BlendMode::Normal;

    // For a CTG layer: the line-art layers used as barriers. More than one is
    // allowed on purpose — combining rough and clean closes most of the gaps
    // that leak with a single source.
    std::vector<LayerId> ctg_sources;

    // Whether a drawing with no scribbles of its own shows an earlier drawing's.
    //
    // On, colouring the first drawing of a run colours the run. Off, absence
    // means what it means on a raster layer — the layer is empty there — and
    // every drawing has to be scribbled. Off is not a lesser mode: a shot where
    // the design changes every drawing gets nothing from carrying marks
    // forward, and the marks it does carry have to be found and removed.
    //
    // This is the switch the other two hang off. With nothing carried there is
    // no direction to carry it in and nothing to move, so both of those mean
    // nothing while this is off.
    bool ctg_inherit = true;

    // Which way the carrying runs, when it runs at all.
    //
    // Forward is how a shot gets coloured and is the default. Backward is for
    // colouring the drawing you have in front of you — often the last of a run,
    // because it is the one you were working on — and having it apply to
    // everything before it. Nearest takes whichever is closer, which is what
    // fills the gaps between drawings you have coloured rather than only the
    // ones after them.
    //
    // None of them is guessed at. Reaching in a direction you did not ask for
    // is sometimes what you want and never what you expect.
    CtgDirection ctg_direction = CtgDirection::Forward;

    // Whether carried marks are moved to follow the animation rather than
    // staying where they were drawn.
    //
    // On by default, which is a change of mind and not an oversight. Left
    // alone, a carried mark holds its region only while the drawing has moved
    // less than about half that region's width — measured, in `bench_carry` —
    // and half a region's width between two drawings is very ordinary. Past it
    // the region takes the wrong colour or none, so the default that does the
    // less surprising thing is the one that moves the marks.
    //
    // Off is still worth having, and not only as an escape hatch: what is
    // estimated is one translation for the whole drawing, so a shot where two
    // things move apart is a shot where the estimate is right about one of them.
    bool ctg_follow_motion = true;

    // Show the scribbles instead of the fill they produce. A view setting, not
    // a property of the drawing: what is on the layer does not change, only
    // which of the two you are looking at.
    bool show_scribbles = false;
};

}  // namespace animage
