// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// **A spike, not a feature.** This file exists to answer one question that
// nothing else can: do `windeployqt`, `macdeployqt` and `linuxdeploy-plugin-qt`
// bundle a Qt Multimedia backend into a package built on a runner, and does the
// downloaded result find it on a machine that never had Qt installed?
//
// It has to live in the application rather than in a probe under `tests/`,
// because the three deployment tools are run over `animage` and nothing else.
// They work by reading what the binary imports, so a probe that links Qt
// Multimedia beside the application teaches them nothing at all -- with nothing
// in `animage` importing the module, every one of them correctly bundles
// nothing, and the run comes back green having asked no question.
//
// See docs/importing.md, "which library": *"So the deployment spike comes
// before any audio code is written. If windeployqt does not bundle the FFmpeg
// plugin correctly, that is a fact worth having on day one and a disaster to
// discover after the audio layer exists."*
//
// **Everything here comes out again** once the audio layer proper arrives, or
// once the spike says the answer is no. What replaces it is the `AudioDevice`
// seam that note asks for -- open at rate R, receive a callback asking for N
// frames, report frames consumed, stop -- which is a different shape and is
// keeping Qt's types out of `MainWindow` rather than reporting on them.
//
// --- What the backend is actually for, which is less than it looks ----------
//
// **A missing backend does not cost this program scrubbing.** Delete
// `plugins/multimedia` outright and Qt says so, in as many words:
//
// > No QtMultimedia backends found. Only QMediaDevices, QAudioDevice,
// > QSoundEffect, QAudioSink, and QAudioSource are available.
//
// That list is the whole of what scrub audio needs. `tests/audio_probe` was run
// against exactly that -- a plugin tree with the backend removed -- and opened
// the device, played, and reported the same numbers to the tenth of a
// millisecond. The raw audio path is native inside `Qt6Multimedia` itself
// (WASAPI, CoreAudio, ALSA/PulseAudio); the backend plugin is not in it.
//
// So the FFmpeg payload -- which is where all the bytes and all three
// packaging tools' difficulty live -- buys exactly two things:
//
// | needs the backend | does not |
// |---|---|
// | `QAudioDecoder`: mp3, m4a, opus, anything compressed | `QAudioSink`, and so the whole of scrubbing |
// | `QMediaPlayer`, and so all of video import | `QMediaDevices`, and so device enumeration |
//
// **This is a seam the plan does not currently draw**, and it is worth knowing
// before the shipping cost is argued: reading a WAV and scrubbing it is
// available at the price of one 1.4 MB library, and every megabyte after that
// is bought by decoding a director's `.m4a` and by video.

#include <QString>

namespace audio_check {

// Whether this build has Qt Multimedia at all. False is not an error: the
// module is asked for as its own optional package, so a Qt install without it
// builds the application unchanged and answers no here.
bool built();

// What the running program can find, as lines meant for a person and for a CI
// log. Names the backend Qt loaded and every audio output it can see, or says
// which of those two it failed at -- a backend that did not load and a machine
// with no sound card are the two outcomes to tell apart, and on a runner the
// second one is expected.
QString report();

}  // namespace audio_check
