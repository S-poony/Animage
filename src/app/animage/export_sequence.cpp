// SPDX-License-Identifier: GPL-3.0-or-later
#include "export_sequence.h"

#include <QDir>
#include <QImage>
#include <algorithm>
#include <cmath>

#include "color.h"
#include "compositor.h"
#include "ctg.h"

using namespace animage;

namespace exporting {
namespace {

// Anything a filesystem might object to, or that would make the frame number
// hard to find by eye, becomes an underscore. Layers are named by people and
// "rough / clean" is a perfectly reasonable thing to call one.
QString sanitise(const std::string& name) {
    QString out;
    out.reserve(static_cast<qsizetype>(name.size()));
    for (const char c : name) {
        const QChar ch(c);
        out += (ch.isLetterOrNumber() || ch == QLatin1Char('-')) ? ch : QLatin1Char('_');
    }
    while (out.endsWith(QLatin1Char('_'))) out.chop(1);
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

}  // namespace

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

// Makes sure every visible CTG layer of this image has its fill built, because
// the compositor will draw whatever is cached and nothing if nothing is. Cheap
// when the fill is current -- ctgFill is keyed on the scribbles and the barrier
// and returns the cached one untouched if neither has moved.
bool needsFill(const Layer& layer) {
    return layer.kind == LayerKind::Ctg && layer.visible && !layer.show_scribbles;
}

void ensureFill(Document& doc, TrackId track_id, ImageId image, LayerId layer_id) {
    if (image == kNoId) return;
    const Track* track = doc.scene().findTrack(track_id);
    if (!track) return;
    const Layer* layer = track->findLayer(layer_id);
    if (!layer || !needsFill(*layer)) return;
    ctgFill(doc, track_id, image, layer_id, CtgSettings{});
}

void ensureAllFills(Document& doc, TrackId track_id, ImageId image) {
    if (image == kNoId) return;
    const Track* track = doc.scene().findTrack(track_id);
    if (!track) return;

    // Collected before solving: ctgFill writes to the document, and holding a
    // reference into the scene across that is how a reference goes stale.
    std::vector<LayerId> ctg_layers;
    for (const Layer& layer : track->layers) {
        if (needsFill(layer)) ctg_layers.push_back(layer.id);
    }
    for (const LayerId layer : ctg_layers) ctgFill(doc, track_id, image, layer, CtgSettings{});
}

bool write(Document& doc, const Options& options, const Progress& progress, QString* error) {
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

    const Compositor compositor;
    Framebuffer frame(canvas.width, canvas.height);
    const int total = fileCount(doc, options);
    int done = 0;

    const auto step = [&]() {
        ++done;
        return !progress || progress(done, total);
    };
    const auto cancelled = [&]() {
        if (error) *error = QStringLiteral("export cancelled");
        return false;
    };

    if (options.layers) {
        // Resolved before anything solves or writes, for the same reason
        // ensureAllFills collects first: building a fill writes to the document.
        struct Sequence {
            TrackId track;
            LayerId layer;
            QString name;
        };
        std::vector<Sequence> sequences;
        for (const Track& track : doc.scene().tracks) {
            for (const Layer& layer : track.layers) {
                if (!layer.visible) continue;
                sequences.push_back({track.id, layer.id, sequenceName(track.name, layer.name)});
            }
        }

        for (const Sequence& sequence : sequences) {
            if (!root.mkpath(options.folder + QLatin1Char('/') + sequence.name)) {
                if (error) *error = QStringLiteral("cannot create %1").arg(sequence.name);
                return false;
            }

            for (std::size_t slot = 0; slot < frames; ++slot) {
                // The compositor clears the buffer itself, but only when it
                // runs. A slot this track does not reach has to come out empty
                // rather than repeating the frame before it.
                frame.clear();
                const Track* track = doc.scene().findTrack(sequence.track);
                const ImageId image = track ? track->imageAtSlot(slot) : kNoId;
                if (image != kNoId) {
                    ensureFill(doc, sequence.track, image, sequence.layer);
                    compositor.compositeLayers(doc, sequence.track, image, {sequence.layer},
                                               canvas, frame);
                }
                if (!writeFrame(frame, framePath(options.folder, sequence.name, slot), error)) {
                    return false;
                }
                if (!step()) return cancelled();
            }
        }
    }

    if (options.flattened) {
        const QString sequence = QStringLiteral("composite");
        if (!root.mkpath(options.folder + QLatin1Char('/') + sequence)) {
            if (error) *error = QStringLiteral("cannot create %1").arg(sequence);
            return false;
        }

        // Each track is composited on its own and then laid over the result so
        // far. It cannot go straight into one buffer: the compositor clears
        // what it is given, so the second track would erase the first.
        Framebuffer one(canvas.width, canvas.height);
        // Bottom track upwards, because index 0 composites on top. Ids rather
        // than references, again because solving a fill writes to the document.
        std::vector<TrackId> bottom_first;
        for (const Track& track : doc.scene().tracks) bottom_first.push_back(track.id);
        std::reverse(bottom_first.begin(), bottom_first.end());

        for (std::size_t slot = 0; slot < frames; ++slot) {
            frame.clear();
            for (const TrackId track_id : bottom_first) {
                const Track* track = doc.scene().findTrack(track_id);
                const ImageId image = track ? track->imageAtSlot(slot) : kNoId;
                if (image == kNoId) continue;
                ensureAllFills(doc, track_id, image);
                compositor.composite(doc, track_id, image, canvas, one);
                for (int y = 0; y < frame.height(); ++y) {
                    const Rgba* above = one.row(y);
                    Rgba* below = frame.row(y);
                    for (int x = 0; x < frame.width(); ++x) below[x] = over(above[x], below[x]);
                }
            }
            if (!writeFrame(frame, framePath(options.folder, sequence, slot), error)) return false;
            if (!step()) return cancelled();
        }
    }

    return true;
}

}  // namespace exporting
