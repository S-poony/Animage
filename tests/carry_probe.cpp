// SPDX-License-Identifier: GPL-3.0-or-later
//
// Walks a real project drawing by drawing and says what the colour layer did on
// each one, the way the canvas would: same ladder, same budgets, same solver.
//
// For "the scribble does not reach that drawing" reports, where the question is
// which of the several things between a mark and a fill gave up.
//
//   carry_probe <project folder> -platform offscreen

#include <QApplication>
#include <cstdio>

#include "canvas_widget.h"
#include "ctg.h"
#include "ctg_fill.h"
#include "ctg_job.h"
#include "document.h"
#include "project_io.h"

using namespace animage;

namespace {

// The ink of one drawing, as a rectangle, so a mark can be asked whether it
// landed on the drawing at all.
PixelRect inkBounds(const Document& doc, const Track& track, ImageId image) {
    PixelRect box{};
    const Image* record = track.findImage(image);
    if (!record) return box;
    for (const Layer& layer : track.layers) {
        if (layer.kind == LayerKind::Ctg) continue;
        if (const Cel* cel = doc.cel(record->celFor(layer.id))) {
            box = unite(box, drawnBounds(cel->tiles()));
        }
    }
    return box;
}

QString say(const PixelRect& r) {
    if (r.isEmpty()) return QStringLiteral("empty");
    return QStringLiteral("%1,%2 %3x%4").arg(r.x).arg(r.y).arg(r.width).arg(r.height);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "carry_probe <project folder>\n");
        return 2;
    }

    Document doc;
    QString error;
    if (!ProjectIO::load(doc, QString::fromLocal8Bit(argv[1]), &error)) {
        std::fprintf(stderr, "cannot open: %s\n", qPrintable(error));
        return 1;
    }

    for (const Track& track : doc.scene().tracks) {
        LayerId colour = kNoId;
        for (const Layer& layer : track.layers) {
            if (layer.kind == LayerKind::Ctg) colour = layer.id;
        }
        if (colour == kNoId) continue;

        const Layer& settings = *track.findLayer(colour);
        std::printf("track %llu, colour layer %llu: inherit=%d follow=%d marks_shown=%d\n",
                    static_cast<unsigned long long>(track.id),
                    static_cast<unsigned long long>(colour), settings.ctg_inherit ? 1 : 0,
                    settings.ctg_follow_motion ? 1 : 0, settings.show_scribbles ? 1 : 0);

        CanvasWidget canvas(doc);
        canvas.resize(1200, 800);
        canvas.setTrack(track.id);

        for (std::size_t slot = 0; slot < track.slots.size(); ++slot) {
            const ImageId image = track.imageShownAt(slot);
            const Image* record = track.findImage(image);
            if (!record) continue;

            canvas.setFrame(static_cast<int>(slot));
            canvas.grab();  // the paint is what asks for the colour
            const bool settled = canvas.settleColour(20000);

            ImageId from = kNoId;
            const Cel* scribbles = doc.ctgScribblesAt(track.id, image, colour, &from);
            const CtgInputs inputs = ctgInputsFor(doc, track.id, image, colour, CtgSettings{});
            const CtgFill* fill = doc.ctgFillFor(track.id, image, colour);
            const CtgWarp& warp = doc.ctgCarryAt(image, colour);

            std::printf(
                "  frame %2d  drawing %-3d image %-3llu  from %-3lld  hash %016llx  %s\n",
                static_cast<int>(slot) + 1, record->number,
                static_cast<unsigned long long>(image), static_cast<long long>(from),
                static_cast<unsigned long long>(inputs.hash), settled ? "" : "(TIMED OUT)");

            if (!scribbles) {
                std::printf("            no marks reach this drawing\n");
                continue;
            }
            const PixelRect ink = inkBounds(doc, track, image);
            std::printf("            ink %s\n", qPrintable(say(ink)));
            std::printf("            carried by %d,%d%s\n", warp.overall.x, warp.overall.y,
                        warp.isUniform() ? "" : " (+field)");

            // Where the marks ended up, in the coordinates they are drawn in,
            // and whether that is on the drawing at all. This is the whole
            // promise of carrying and the one thing worth reading first.
            const Document::CarriedMarks shown =
                doc.ctgCarriedMarksAt(track.id, image, colour);
            if (shown.tiles) {
                PixelRect landed = drawnBounds(*shown.tiles);
                landed.x += shown.offset.x;
                landed.y += shown.offset.y;
                const bool on = !intersect(landed, ink).isEmpty();
                std::printf("            marks land %s (offset %d,%d)  %s\n",
                            qPrintable(say(landed)), shown.offset.x, shown.offset.y,
                            on ? "ON THE DRAWING" : "*** ON BARE PAPER ***");
                std::printf("            warp: zero=%d uniform=%d cells=%d step=%d area %s\n",
                            warp.isZero() ? 1 : 0, warp.isUniform() ? 1 : 0,
                            static_cast<int>(warp.cells.size()), warp.step,
                            qPrintable(say(warp.area)));
            }

            // The rungs on their own, over the same two drawings the solve saw.
            // Which one gave up is the whole question when a mark does not
            // move: rung four falls back to rung two only when its own run
            // moved no node, and rung two answering zero is indistinguishable
            // from a lattice that correctly found nothing to do.
            const CtgJob job = ctgJobFor(doc, track.id, image, colour, CtgSettings{});
            if (!job.origin_sources.empty()) {
                PixelRect both;
                for (const TileGrid& g : job.sources) both = unite(both, drawnBounds(g));
                for (const TileGrid& g : job.origin_sources) both = unite(both, drawnBounds(g));
                const CtgShift rung_two =
                    estimateCtgShift(job.origin_sources, job.sources, both);
                std::printf("            over %s: rung two says %d,%d\n", qPrintable(say(both)),
                            rung_two.x, rung_two.y);
            }
            if (!fill) {
                std::printf("            NO FILL\n");
                continue;
            }
            std::printf("            fill: colours %d  spread %.2f  marks %s  step %d\n",
                        fill->colours, fill->spread, qPrintable(say(fill->marks_drawn)),
                        fill->step);
        }
    }
    return 0;
}
