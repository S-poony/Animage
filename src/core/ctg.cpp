// SPDX-License-Identifier: GPL-3.0-or-later
#include "ctg.h"

#include <algorithm>
#include <unordered_map>

namespace animage {

// Everything the answer depends on, without working the answer out.
//
// Its own function because three callers need it and only one of them wants the
// fill: the cache asks "has anything moved" before paying for a solve, and the
// audit asks the same before paying for a coarse one. A second copy of the rule
// for what a fill depends on is a stale fill nobody can explain, so there is
// one.
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
    inputs = inputs * 31 + static_cast<std::uint64_t>(settings.downscale);
    // The canvas bounds the solve, so resizing it changes the answer.
    inputs = inputs * 31 + static_cast<std::uint64_t>(doc.scene().width);
    inputs = inputs * 31 + static_cast<std::uint64_t>(doc.scene().height);

    out.hash = inputs;
    out.valid = true;
    out.inherited = from != image;
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
    return cache.store(key, solveCtgFill(doc, track, image, layer_id, settings, true));
}

std::vector<CtgToJudge> ctgAuditWork(Document& doc, TrackId track,
                                     const CtgSettings& settings) {
    std::vector<CtgToJudge> wanted;

    const Track* line = doc.scene().findTrack(track);
    if (!line) return wanted;

    std::vector<LayerId> layers;
    for (const Layer& layer : line->layers) {
        if (layer.kind == LayerKind::Ctg && layer.visible) layers.push_back(layer.id);
    }
    if (layers.empty()) {
        doc.ctgVerdicts().clear();
        return wanted;
    }

    // Distinct drawings, not frames: a drawing held over five of them is one
    // solve, and the verdict belongs to the drawing.
    std::vector<ImageId> drawings;
    for (ImageId id : line->slots) {
        if (std::find(drawings.begin(), drawings.end(), id) == drawings.end()) {
            drawings.push_back(id);
        }
    }

    // Rebuilt rather than added to, so a verdict about a drawing that has been
    // deleted, or a layer that is no longer a colour layer, goes away with it.
    //
    // A verdict that has merely gone out of date is *kept* until the new one
    // arrives, and that is the difference between a flag and a flicker. One
    // stroke on a drawing that others inherit from moves the hash of every one
    // of them, so dropping the stale verdicts would take the flags off the
    // whole timeline and put them back a moment later, on every stroke. A flag
    // that is a fifth of a second out of date says what it said before; a flag
    // that blinks teaches you to stop reading it.
    auto& verdicts = doc.ctgVerdicts();
    Document::CtgVerdicts kept;
    for (ImageId image : drawings) {
        for (LayerId layer : layers) {
            const CtgKey key{image, layer};

            const CtgInputs depends = ctgInputsFor(doc, track, image, layer, settings);
            if (!depends.valid) continue;  // nothing on this layer here

            auto known = verdicts.find(key);
            const bool current = known != verdicts.end();
            if (current) kept.emplace(key, known->second);
            if (current && known->second.inputs == depends.hash) continue;

            wanted.push_back({key, depends.hash});
        }
    }
    verdicts = std::move(kept);
    return wanted;
}

void auditCtgFills(Document& doc, TrackId track, const CtgSettings& settings) {
    for (const CtgToJudge& todo : ctgAuditWork(doc, track, settings)) {
        const CtgFill probe =
            solveCtgFill(doc, track, todo.key.image, todo.key.layer, settings, false);
        if (!probe.valid) continue;
        doc.ctgVerdicts()[todo.key] = verdictFrom(probe);
    }
}

}  // namespace animage
