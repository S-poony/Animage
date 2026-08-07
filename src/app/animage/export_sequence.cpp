// SPDX-License-Identifier: GPL-3.0-or-later
#include "export_sequence.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <vector>

#include "color.h"
#include "compositor.h"
#include "ctg.h"

using namespace animage;

namespace exporting {
namespace {

// Anything a filesystem might object to, or that would make the frame number
// hard to find by eye, becomes a hyphen. Layers are named by people and
// "rough / clean" is a perfectly reasonable thing to call one.
//
// A hyphen and not an underscore, and an underscore in the name given here
// becomes one too, because the underscore has a job: it is what separates the
// track from the layer from the frame number. If a layer called "layer 1" came
// out as `layer_1` there would be two underscores in `main_layer_1_0007` and
// nothing to say which of the three numbers was the frame.
QString sanitise(const std::string& name) {
    QString out;
    out.reserve(static_cast<qsizetype>(name.size()));
    for (const char c : name) {
        const QChar ch(c);
        if (ch.isLetterOrNumber()) {
            out += ch;
        } else if (!out.endsWith(QLatin1Char('-'))) {
            out += QLatin1Char('-');  // a run of junk is one separator, not five
        }
    }
    while (out.endsWith(QLatin1Char('-'))) out.chop(1);
    while (out.startsWith(QLatin1Char('-'))) out.remove(0, 1);
    return out.isEmpty() ? QStringLiteral("unnamed") : out;
}

quint16 toShort(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<quint16>(std::lround(clamped * 65535.0f));
}

// Linear premultiplied half-float to 16-bit sRGB, straight. Alpha is *not*
// sRGB-encoded -- it is coverage, not light, and running it through the curve
// is a classic way to make everything semi-transparent look wrong.
QImage toSrgb16(const Framebuffer& frame) {
    QImage image(frame.width(), frame.height(), QImage::Format_RGBA64);
    if (image.isNull()) return image;

    for (int y = 0; y < frame.height(); ++y) {
        const Rgba* in = frame.row(y);
        auto* out = reinterpret_cast<quint16*>(image.scanLine(y));
        for (int x = 0; x < frame.width(); ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            unpremultiply(in[x], r, g, b, a);
            out[4 * x + 0] = toShort(linearToSrgb(r));
            out[4 * x + 1] = toShort(linearToSrgb(g));
            out[4 * x + 2] = toShort(linearToSrgb(b));
            out[4 * x + 3] = toShort(a);
        }
    }
    return image;
}

std::size_t frameCount(const Document& doc) {
    std::size_t frames = 0;
    for (const Track& track : doc.scene().tracks) frames = std::max(frames, track.frameCount());
    return frames;
}

QString framePath(const QString& folder, const QString& sequence, std::size_t frame) {
    // One-based, because it is the frame number an animator says out loud.
    return QStringLiteral("%1/%2/%2_%3.png")
        .arg(folder, sequence,
             QString::number(frame + 1).rightJustified(4, QLatin1Char('0')));
}

bool writeFrame(const Framebuffer& frame, const QString& path, QString* error) {
    const QImage image = toSrgb16(frame);
    if (image.isNull() || !image.save(path, "PNG")) {
        if (error) *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    return true;
}

// One layer's sequence: where it comes from and what it is called.
struct Sequence {
    TrackId track;
    LayerId layer;
    QString name;
};

// Which layers the compositor will need a fill for. A CTG layer showing its
// scribbles is drawing the marks rather than the fill, so solving one would be
// computing a picture and then not using it.
bool needsFill(const Layer& layer) {
    return layer.kind == LayerKind::Ctg && layer.visible && !layer.show_scribbles;
}

// The fills one drawing needs before it can be composited, and whether each of
// them is already in the document.
//
// The test for "already" is the canvas's: the same inputs, and either solved at
// full resolution or solved with at least the allowance we would ask for. The
// second half is what stops an export handed a solver from settling for the
// capped fill the screen was showing.
bool fillIsCurrent(const Document& doc, TrackId track, ImageId image, LayerId layer,
                   const CtgInputs& wanted, long long budget) {
    const CtgFill* held = doc.ctgFillFor(track, image, layer);
    if (!held || !held->valid || held->inputs != wanted.hash) return false;
    return held->step <= 1 || held->budget >= budget;
}

// What a file browser leaves lying about. Ignored when deciding whether a
// folder is an export, and deleted along with it -- otherwise a folder anybody
// had so much as looked at would stop being recognisable as one.
bool isBrowserJunk(const QString& name) {
    static const QStringList kJunk = {QStringLiteral(".DS_Store"), QStringLiteral("Thumbs.db"),
                                      QStringLiteral("desktop.ini")};
    return kJunk.contains(name, Qt::CaseInsensitive);
}

// One frame of the sequence its folder is named after: `{stem}_{0007}.png`,
// and the stem has to be the folder's own name. Add to the extensions here when
// the format list in export_sequence.h grows -- a TIFF or EXR export this did
// not recognise would be refused as somebody else's folder, which is a
// confusing way to find out.
bool isFrameOf(const QString& sequence, const QString& file) {
    static const QStringList kExtensions = {QStringLiteral("png")};
    const QRegularExpression pattern(
        QStringLiteral("^%1_\\d{4}\\.(%2)$")
            .arg(QRegularExpression::escape(sequence), kExtensions.join(QLatin1Char('|'))),
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(file).hasMatch();
}

}  // namespace

Occupant occupantOf(const QString& folder) {
    const QDir root(folder);
    if (!root.exists()) return Occupant::Nothing;

    const auto flags = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;
    const QFileInfoList entries = root.entryInfoList(flags);

    int sequences = 0;
    for (const QFileInfo& entry : entries) {
        if (entry.isFile() && isBrowserJunk(entry.fileName())) continue;
        // A loose file, a symlink, anything that is not a folder of frames.
        if (!entry.isDir() || entry.isSymLink()) return Occupant::SomethingElse;

        const QString sequence = entry.fileName();
        for (const QFileInfo& inside : QDir(entry.absoluteFilePath()).entryInfoList(flags)) {
            if (inside.isFile() && isBrowserJunk(inside.fileName())) continue;
            if (!inside.isFile() || inside.isSymLink()) return Occupant::SomethingElse;
            if (!isFrameOf(sequence, inside.fileName())) return Occupant::SomethingElse;
        }
        ++sequences;
    }

    // Nothing but junk in it is nothing in it. An empty folder is somewhere to
    // write, not something to ask about.
    return sequences == 0 ? Occupant::Nothing : Occupant::AnExport;
}

bool removeExport(const QString& folder, QString* error) {
    QDir root(folder);
    if (!root.exists()) return true;
    if (!root.removeRecursively()) {
        if (error) *error = QStringLiteral("cannot empty %1").arg(folder);
        return false;
    }
    return true;
}

QString sequenceName(const std::string& track, const std::string& layer) {
    return sanitise(track) + QLatin1Char('_') + sanitise(layer);
}

int fileCount(const Document& doc, const Options& options) {
    const std::size_t frames = frameCount(doc);
    std::size_t sequences = 0;
    if (options.layers) {
        for (const Track& track : doc.scene().tracks) {
            for (const Layer& layer : track.layers) {
                if (layer.visible) ++sequences;
            }
        }
    }
    if (options.flattened) ++sequences;
    return static_cast<int>(sequences * frames);
}

bool write(Document& doc, const Options& options, const Progress& progress, const Solve& solve,
           QString* error) {
    const PixelRect canvas = doc.scene().canvas();
    if (canvas.width <= 0 || canvas.height <= 0) {
        if (error) *error = QStringLiteral("the canvas has no size to export");
        return false;
    }
    const std::size_t frames = frameCount(doc);
    if (frames == 0) {
        if (error) *error = QStringLiteral("there are no frames to export");
        return false;
    }
    if (!options.layers && !options.flattened) {
        if (error) *error = QStringLiteral("nothing was selected to export");
        return false;
    }

    QDir root;
    if (!root.mkpath(options.folder)) {
        if (error) *error = QStringLiteral("cannot create %1").arg(options.folder);
        return false;
    }

    // What is going to be written, resolved before anything solves or writes:
    // building a fill writes to the document, and a reference into the scene
    // does not survive that. Ids only, from here down.
    std::vector<Sequence> sequences;
    if (options.layers) {
        for (const Track& track : doc.scene().tracks) {
            for (const Layer& layer : track.layers) {
                if (!layer.visible) continue;
                sequences.push_back({track.id, layer.id, sequenceName(track.name, layer.name)});
            }
        }
    }
    const QString composite = QStringLiteral("composite");

    // Bottom track upwards for the flattened pass, because index 0 composites
    // on top.
    std::vector<TrackId> bottom_first;
    for (const Track& track : doc.scene().tracks) bottom_first.push_back(track.id);
    std::reverse(bottom_first.begin(), bottom_first.end());

    // The colour layers of each track, in the same order.
    std::vector<std::vector<LayerId>> ctg_layers;
    for (const TrackId track_id : bottom_first) {
        std::vector<LayerId> layers;
        if (const Track* track = doc.scene().findTrack(track_id)) {
            for (const Layer& layer : track->layers) {
                if (needsFill(layer)) layers.push_back(layer.id);
            }
        }
        ctg_layers.push_back(std::move(layers));
    }

    // A solve nobody is waiting on wants the whole allowance; one the caller is
    // standing in front of gets the interactive cap, because that is the deal
    // the cap exists to keep.
    const CtgSettings settings;
    const long long budget = solve ? kFullSolveBudget : kInteractiveSolveBudget;

    // How many max-flows this is going to be. Counted here, before anything has
    // moved, and it is exact because of the order the frames are written in:
    // each drawing is visited once, contiguously, so nothing is solved twice
    // and nothing is evicted before it is used.
    int solves = 0;
    for (std::size_t track_index = 0; track_index < bottom_first.size(); ++track_index) {
        ImageId previous = kNoId;
        for (std::size_t slot = 0; slot < frames; ++slot) {
            const Track* track = doc.scene().findTrack(bottom_first[track_index]);
            const ImageId image = track ? track->imageAtSlot(slot) : kNoId;
            if (image == kNoId || image == previous) continue;
            previous = image;
            for (const LayerId layer : ctg_layers[track_index]) {
                const CtgInputs wanted =
                    ctgInputsFor(doc, bottom_first[track_index], image, layer, settings);
                if (!wanted.valid) continue;
                if (!fillIsCurrent(doc, bottom_first[track_index], image, layer, wanted, budget)) {
                    ++solves;
                }
            }
        }
    }

    const int estimate = fileCount(doc, options) + solves;
    int done = 0;
    int solved = 0;

    // Never less than what has been done. The solve count is exact for the
    // order the frames are written in, and a total that a step could walk past
    // would be a bar that finished twice.
    const auto total = [&]() { return std::max(estimate, done); };

    // Says what is being waited for without counting it as finished, so the
    // label is right while the slow thing runs rather than after it.
    const auto announce = [&](const QString& what) {
        return !progress || progress(done, total(), what);
    };
    const auto step = [&](const QString& what) {
        ++done;
        return !progress || progress(done, total(), what);
    };
    const auto cancelled = [&]() {
        if (error) *error = QStringLiteral("export cancelled");
        return false;
    };

    // Builds whatever fills this drawing is missing. The solve itself happens
    // wherever `solve` says it does; without one it happens here, capped.
    const auto ensureFills = [&](TrackId track_id, const std::vector<LayerId>& layers,
                                 ImageId image) {
        for (const LayerId layer : layers) {
            const CtgInputs wanted = ctgInputsFor(doc, track_id, image, layer, settings);
            if (!wanted.valid) continue;
            if (fillIsCurrent(doc, track_id, image, layer, wanted, budget)) continue;

            ++solved;
            const QString what =
                QStringLiteral("colouring %1 of %2...").arg(solved).arg(std::max(solves, solved));
            if (!announce(what)) return false;

            if (solve) {
                CtgFill fill;
                if (!solve({image, layer}, ctgJobFor(doc, track_id, image, layer, settings, budget),
                           fill)) {
                    return false;
                }
                // The same two writes the canvas makes when a solve lands: the
                // fill, and where the marks it was made from ended up. Anything
                // that draws the marks reads the second one.
                doc.ctgShifts()[{image, layer}] = fill.carried_by;
                doc.ctgCache().store({image, layer}, std::move(fill));
            } else {
                ctgFill(doc, track_id, image, layer, settings);
            }
            if (!step(what)) return false;
        }
        return true;
    };

    for (const Sequence& sequence : sequences) {
        if (!root.mkpath(options.folder + QLatin1Char('/') + sequence.name)) {
            if (error) *error = QStringLiteral("cannot create %1").arg(sequence.name);
            return false;
        }
    }
    if (options.flattened && !root.mkpath(options.folder + QLatin1Char('/') + composite)) {
        if (error) *error = QStringLiteral("cannot create %1").arg(composite);
        return false;
    }

    const Compositor compositor;
    Framebuffer frame(canvas.width, canvas.height);
    // The flattened picture cannot go straight into one buffer: the compositor
    // clears what it is given, so the second track would erase the first.
    Framebuffer one(canvas.width, canvas.height);

    for (std::size_t slot = 0; slot < frames; ++slot) {
        // Every fill this frame needs, once, before anything is composited.
        // Both passes want the same ones and this is the only place they are
        // asked for, which is why a colour layer is solved once however many
        // sequences it appears in.
        for (std::size_t track_index = 0; track_index < bottom_first.size(); ++track_index) {
            if (ctg_layers[track_index].empty()) continue;
            const Track* track = doc.scene().findTrack(bottom_first[track_index]);
            const ImageId image = track ? track->imageAtSlot(slot) : kNoId;
            if (image == kNoId) continue;
            if (!ensureFills(bottom_first[track_index], ctg_layers[track_index], image)) {
                return cancelled();
            }
        }

        const QString writing =
            QStringLiteral("writing frame %1 of %2...").arg(slot + 1).arg(frames);

        for (const Sequence& sequence : sequences) {
            // The compositor clears the buffer itself, but only when it runs. A
            // slot this track does not reach has to come out empty rather than
            // repeating the frame before it.
            frame.clear();
            const Track* track = doc.scene().findTrack(sequence.track);
            const ImageId image = track ? track->imageAtSlot(slot) : kNoId;
            if (image != kNoId) {
                compositor.compositeLayers(doc, sequence.track, image, {sequence.layer}, canvas,
                                           frame);
            }
            if (!writeFrame(frame, framePath(options.folder, sequence.name, slot), error)) {
                return false;
            }
            if (!step(writing)) return cancelled();
        }

        if (!options.flattened) continue;

        frame.clear();
        for (const TrackId track_id : bottom_first) {
            const Track* track = doc.scene().findTrack(track_id);
            const ImageId image = track ? track->imageAtSlot(slot) : kNoId;
            if (image == kNoId) continue;
            compositor.composite(doc, track_id, image, canvas, one);
            for (int y = 0; y < frame.height(); ++y) {
                const Rgba* above = one.row(y);
                Rgba* below = frame.row(y);
                for (int x = 0; x < frame.width(); ++x) below[x] = over(above[x], below[x]);
            }
        }
        if (!writeFrame(frame, framePath(options.folder, composite, slot), error)) return false;
        if (!step(writing)) return cancelled();
    }

    return true;
}

}  // namespace exporting
