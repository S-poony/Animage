// SPDX-License-Identifier: GPL-3.0-or-later
#include "exr_writer.h"

// tinyexr is compiled here and nowhere else, with miniz as its compressor --
// both vendored in third_party rather than linked, because Qt bundling zlib
// does not make zlib available to link against and Windows has none of its own.
// See the build file. It is also why this one translation unit is compiled with
// the warnings turned off.
#define TINYEXR_USE_MINIZ 1
#define TINYEXR_USE_STB_ZLIB 0
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

#include <cstring>
#include <string>
#include <vector>

#include "half.h"

using namespace animage;

namespace exporting {
namespace {

// EXR stores channels separately, one plane each, where a Framebuffer stores
// them interleaved -- so this is a transpose and a float-to-half, and there is
// no arrangement of QImage that would save either. `Rgba` is float32 even
// though the tiles it came from are half, because compositing happens in
// float; the narrowing here is the only conversion in the whole writer, and it
// is exact for anything that came off a tile.
struct Planes {
    std::vector<unsigned short> a, b, g, r;
};

Planes toPlanes(const Framebuffer& frame) {
    const std::size_t count =
        static_cast<std::size_t>(frame.width()) * static_cast<std::size_t>(frame.height());
    Planes planes;
    planes.a.resize(count);
    planes.b.resize(count);
    planes.g.resize(count);
    planes.r.resize(count);

    std::size_t at = 0;
    for (int y = 0; y < frame.height(); ++y) {
        const Rgba* in = frame.row(y);
        for (int x = 0; x < frame.width(); ++x, ++at) {
            planes.r[at] = Half(in[x].r).bits;
            planes.g[at] = Half(in[x].g).bits;
            planes.b[at] = Half(in[x].b).bits;
            planes.a[at] = Half(in[x].a).bits;
        }
    }
    return planes;
}

}  // namespace

bool writeExrFrame(const Framebuffer& frame, const QString& path, QString* error) {
    if (frame.isEmpty()) {
        if (error) *error = QStringLiteral("nothing to write to %1").arg(path);
        return false;
    }

    Planes planes = toPlanes(frame);

    // Alphabetical, which is what the format says and what readers assume. Get
    // this wrong and the picture comes back with its channels swapped rather
    // than failing to load, which is the kind of bug a round trip through the
    // same library will not notice.
    unsigned char* channels[4] = {
        reinterpret_cast<unsigned char*>(planes.a.data()),
        reinterpret_cast<unsigned char*>(planes.b.data()),
        reinterpret_cast<unsigned char*>(planes.g.data()),
        reinterpret_cast<unsigned char*>(planes.r.data()),
    };

    EXRImage image;
    InitEXRImage(&image);
    image.num_channels = 4;
    image.images = channels;
    image.width = frame.width();
    image.height = frame.height();

    EXRHeader header;
    InitEXRHeader(&header);
    header.num_channels = 4;
    header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP;

    std::vector<EXRChannelInfo> infos(4);
    const char* names[4] = {"A", "B", "G", "R"};
    for (int i = 0; i < 4; ++i) {
        std::memset(infos[i].name, 0, sizeof infos[i].name);
        std::strncpy(infos[i].name, names[i], sizeof infos[i].name - 1);
    }
    header.channels = infos.data();

    // HALF on both sides: what the pixels are, and what to store them as. A
    // mismatch here is where a "lossless" writer quietly becomes a converting
    // one.
    std::vector<int> pixel_types(4, TINYEXR_PIXELTYPE_HALF);
    std::vector<int> requested(4, TINYEXR_PIXELTYPE_HALF);
    header.pixel_types = pixel_types.data();
    header.requested_pixel_types = requested.data();

    const char* why = nullptr;
    const int status = SaveEXRImageToFile(&image, &header, path.toUtf8().constData(), &why);
    if (status != TINYEXR_SUCCESS) {
        if (error) {
            *error = QStringLiteral("cannot write %1: %2")
                         .arg(path, QString::fromUtf8(why ? why : "unknown error"));
        }
        if (why) FreeEXRErrorMessage(why);
        return false;
    }
    return true;
}

}  // namespace exporting
