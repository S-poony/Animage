// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctg.h"


namespace animage {

// Everything the answer depends on, without working the answer out.
//
// Its own function because the cache asks "has anything moved" before paying
// for a solve, and the canvas asks it again before deciding to ask for one. A
// second copy of the rule for what a fill depends on is a stale fill nobody can
// explain, so there is one.
CtgInputs ctgInputsFor(const Document& doc, TrackId track, ImageId image, LayerId layer_id,
                       const CtgSettings& settings) {
    CtgInputs out;

    const Track* line = doc.scene().findTrack(track);
    if (!line) return out;
    const Layer* layer = line->findLayer(layer_id);
    if (!layer || layer->kind != LayerKind::Ctg) return out;
    const Image* record = line->findImage(image);
    if (!record) return out;

    ImageId from = kNoId;
    const Cel* scribbles = doc.ctgScribblesAt(track, image, layer_id, &from);
    if (!scribbles) return out;

    // Cheaper than hashing the pixels and exact enough: a cel bumps its
    // revision on every write, so nothing can change without this changing.
    std::uint64_t inputs = scribbles->revision() * 0x9e3779b97f4a7c15ull;

    // Which cel, and not only how many times it has been written. Inheritance
    // is the reason: reordering drawings changes the cel a scribble is read
    // from and touches no revision anywhere, so a key made of revisions alone
    // would go on serving the colour from the drawing that used to precede
    // this one. Revisions collide freely -- every cel in a project straight off
    // disk is at revision 1 -- so this is not a long shot, it is the ordinary
    // case for a reorder made before anything has been drawn.
    inputs = inputs * 31 + scribbles->id();
    for (LayerId source : layer->ctg_sources) {
        const Cel* cel = doc.cel(record->celFor(source));
        inputs = inputs * 31 + (cel ? cel->revision() : 0) + source;
    }

    // The line art of the drawing the marks came from, when the layer moves
    // them to follow it: the fill then depends on that drawing too, because how
    // far the marks are moved is measured between the two. Redrawing the
    // *source* drawing's ink changes the answer here, and nothing else in this
    // hash would have noticed.
    if (layer->ctg_follow_motion && from != image) {
        const Image* origin = line->findImage(from);
        for (LayerId source : layer->ctg_sources) {
            const Cel* cel = origin ? doc.cel(origin->celFor(source)) : nullptr;
            inputs = inputs * 31 + (cel ? cel->revision() : 0) + source + 1;
        }
    }
    inputs = inputs * 31 + static_cast<std::uint64_t>(settings.downscale);
    // The canvas bounds the solve, so resizing it changes the answer.
    inputs = inputs * 31 + static_cast<std::uint64_t>(doc.scene().width);
    inputs = inputs * 31 + static_cast<std::uint64_t>(doc.scene().height);

    out.hash = inputs;
    out.valid = true;
    out.inherited = from != image;
    out.from = from;
    out.scribbles = scribbles;
    return out;
}

// The one place the document is read for a solve.
//
// Everything past this point works from the copy, which is what lets the solve
// run on a thread that has no business looking at a document being edited. The
// copy is handles and layer properties: a 1080p line-art cel is a hundred and
// thirty-five shared_ptr copies, and a tile is immutable once shared, so a
// stroke drawn a moment later clones the tiles it touches and leaves this
// describing the drawing as it was.
CtgJob ctgJobFor(const Document& doc, TrackId track, ImageId image, LayerId layer_id,
                 const CtgSettings& settings, long long budget) {
    CtgJob job;

    const Track* line = doc.scene().findTrack(track);
    if (!line) return job;
    const Layer* layer = line->findLayer(layer_id);
    if (!layer || layer->kind != LayerKind::Ctg) return job;
    const Image* record = line->findImage(image);
    if (!record) return job;

    // The scribbles may belong to an earlier drawing: absence on a CTG layer
    // means inherited. Only the scribbles inherit -- the barrier below is read
    // from this drawing's own line art, which is the point of the feature. The
    // two are always the same moment in time, because layers belong to the
    // track and timing belongs to the image, so "the previous drawing" needs no
    // qualification here the way it would in TVPaint.
    const CtgInputs depends = ctgInputsFor(doc, track, image, layer_id, settings);
    if (!depends.valid) return job;

    job.canvas = doc.scene().canvas();
    job.scribbles = depends.scribbles->tiles();
    for (LayerId source : layer->ctg_sources) {
        if (!line->findLayer(source)) continue;  // a source that has been deleted
        const Cel* cel = doc.cel(record->celFor(source));
        if (cel) job.sources.push_back(cel->tiles());
    }

    // And the same layers on the drawing the marks were made on, if they were
    // made on another one and this layer moves them to follow the animation.
    // The solve estimates the motion between the two; handing over both is all
    // it needs, and it is why nothing about the motion has to be stored.
    if (layer->ctg_follow_motion && depends.inherited) {
        if (const Image* origin = line->findImage(depends.from)) {
            for (LayerId source : layer->ctg_sources) {
                if (!line->findLayer(source)) continue;
                const Cel* cel = doc.cel(origin->celFor(source));
                if (cel) job.origin_sources.push_back(cel->tiles());
            }
        }
    }
    job.settings = settings;
    job.budget = budget;
    job.inputs = depends.hash;
    job.inherited = depends.inherited;
    job.valid = true;
    return job;
}

CtgFill solveCtgFill(const Document& doc, TrackId track, ImageId image, LayerId layer_id,
                     const CtgSettings& settings, bool want_tiles) {
    return solveCtgJob(ctgJobFor(doc, track, image, layer_id, settings), want_tiles);
}

const CtgFill& ctgFill(Document& doc, TrackId track, ImageId image, LayerId layer_id,
                       const CtgSettings& settings) {
    static const CtgFill kNothing;

    const CtgKey key{image, layer_id};
    CtgFillCache& cache = doc.ctgCache();

    const CtgInputs depends = ctgInputsFor(doc, track, image, layer_id, settings);
    if (!depends.valid) return kNothing;
    if (const CtgFill* hit = cache.find(key); hit && hit->valid && hit->inputs == depends.hash) {
        return *hit;
    }
    CtgFill built = solveCtgFill(doc, track, image, layer_id, settings, true);
    // Where the marks ended up, for everything that has to agree with this fill
    // about that and cannot afford to work it out. See Document::ctgShiftAt.
    doc.ctgShifts()[key] = built.carried_by;
    return cache.store(key, std::move(built));
}

}  // namespace animage
