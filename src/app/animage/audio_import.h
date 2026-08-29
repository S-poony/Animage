// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include "audio_track.h"

// Turning a soundtrack file into the samples a device is fed.
//
// It lives in `src/app/` for the reason `image_import` does: `core` is the
// model and knows nothing about files or about Qt. What `core` holds is the
// answer -- `AudioClip` -- exactly as it holds a `TileGrid` somebody else
// decoded.
//
// **The whole of this is behind `ANIMAGE_HAVE_AUDIO`**, and a build without Qt
// Multimedia gets a `decode` that fails with a message saying so rather than a
// missing symbol. The module is asked for as its own optional package -- never
// the root `find_package`, see src/app/CMakeLists.txt -- so a Qt install that
// lacks it must still build a working program, minus this.
namespace audio_import {

// Whether this build can decode anything at all.
bool available();

// What came out of a file, or why nothing did.
//
// **Decoding is the one place a file the user chose can fail for reasons that
// are the file's rather than ours**, so the trouble is a sentence meant to be
// shown, not a code. A director's `.m4a` that turns out to be a video
// container, a `.wav` truncated by a transfer, a codec this Qt was not built
// with -- each of those is a different sentence and the user can act on the
// difference.
struct Decoded {
    bool ok = false;
    animage::AudioClip clip;
    QString trouble;
};

// Reads the whole file into memory.
//
// **Synchronous, and it runs a nested event loop to get there.**
// `QAudioDecoder` is asynchronous by construction: there is no call that
// returns samples. What there is instead is a stream of buffers and a finished
// signal, so something has to wait -- and doing that inside one function keeps
// the asynchrony from spreading into every caller.
//
// That makes it **unsafe to call from the interface thread on a long file**, and
// the reason is not only the wait: a nested event loop delivers paints and
// input while it spins, so the window stays alive but the document can be
// edited underneath the caller. Callers on the interface thread must be
// short-lived and must not hold anything across it. The import path does this
// once, behind a modal dialog, on a file whose length the user has just been
// shown -- see MainWindow::importAudioFrom.
//
// Ten seconds of 48 kHz stereo is about 2 MB of float and tens of milliseconds
// to decode, which is what makes the simple shape affordable. A soundtrack long
// enough for that to hurt is a soundtrack somebody should be cutting down
// before it reaches a shot.
Decoded decode(const QString& path);

// A bound on what will be read, in frames of audio, so that a file nobody meant
// to import cannot fill memory before the dialog that would have warned about
// it. Ten minutes at 48 kHz.
//
// A cap and not a refusal: decoding stops here and says so, and what came out
// is still usable. Somebody who really has a ten-minute soundtrack gets the
// first ten minutes and a sentence, which beats both a hang and a blank refusal.
inline constexpr int kMaxFrames = 48000 * 60 * 10;

}  // namespace audio_import
