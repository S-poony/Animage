// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "document.h"
#include "tile.h"

// Everything that turns a project on disk into a document in memory, and back.
//
// A project is a folder, not a file:
//
//   the-shot.animage/
//     scene.json            the structure, in text
//     cels/cel-000007.acel  one per cel that any drawing refers to
//
// This is the application's half of saving and loading, and it is all here, in
// one class with one job. The library (`animage_core`) is the model -- a
// document in memory -- and knows nothing of bytes on disk. Two layers make up
// the format, and both are exposed so they can be tested on their own:
//
//   - the scene text, JSON read and written with Qt's QJsonDocument (keys are
//     written sorted, which is what keeps a saved scene the same bytes twice
//     running whatever order the writer used);
//   - the cel bytes, a little-endian layout of the very half-floats the tiles
//     hold -- the layout is the table further down this header, so a drawing is
//     recoverable with nothing but zlib -- deflated behind eight bytes of
//     magic in the folder. Uncompressed a tile is 128 KB, so a shot would run
//     to gigabytes.
//
// Saving builds alongside the folder and swaps at the end, so an interrupted
// save leaves the last good project where it was; opening builds a whole
// document before adopting it, so a project that will not open cannot take the
// open one down with it.
class ProjectIO {
public:
    // The version `scene.json` writes and understands. Bump when the structure
    // changes in a way old builds would misread; a file with a higher version
    // is refused with a message saying the build is older than the file.
    //
    // **3 is soundtracks.** A build that does not know `audio_tracks` drops the
    // key on load and then autosaves over the project two minutes later without
    // it -- and unlike a cel, there is nothing in the document to write it back
    // from. The soundtrack file would still be sitting in `audio/`, orphaned,
    // with nothing naming it and the placement that timed the shot to it gone.
    // Same standard as the bump below: old builds would get it wrong, not
    // merely fail to understand it.
    //
    // **2 is imports.** A build that does not know `LayerKind::Reference` reads
    // one as a raster layer -- `kindFromName` falls through to raster for any
    // word it does not recognise -- and would then find no cels on it, conclude
    // the layer is empty, and autosave over the project two minutes later
    // having dropped the import. Silent data loss, which is exactly the
    // standard this number exists to enforce: bump when old builds would get it
    // wrong, not merely when they would not understand.
    static constexpr int kSceneFormatVersion = 3;

    // What the last successful save or load left on disk, so the next save can
    // tell which cel files are still current. A cel's revision is bumped by
    // every write to it, undo included, so equality here means the bytes in
    // the folder are the bytes this cel would encode to -- and the file can be
    // carried forward instead of re-encoded.
    //
    // It is a hint and never a promise: a carried-forward file that has gone
    // missing falls back to being written out in full. Correctness depends on
    // the revisions being right about the pixels, not on the map being right
    // about the disk.
    struct SaveState {
        QString folder;  // empty, or where these revisions were written
        std::unordered_map<animage::CelId, std::uint64_t> revisions;
    };

    // What a load could not read: one entry per cel file that was missing or
    // would not decode. The drawing comes back empty rather than the whole
    // project not coming back at all.
    //
    // **Only ever filled in for a caller that asks for it.** A `load` given no
    // `Damage*` refuses a project with a bad cel in it exactly as it always
    // did, and that is deliberate rather than a convenience: opening what can
    // be read is only safe where somebody is going to *say* that drawings are
    // missing, and an empty cel is indistinguishable from one that was erased
    // on purpose. A caller with nowhere to report is a caller that must not
    // silently accept a damaged project.
    struct Damage {
        struct Lost {
            animage::CelId cel = animage::kNoId;
            QString file;  // the cel file, for a report somebody technical reads
            QString why;   // what the reader said was wrong with it
        };
        std::vector<Lost> lost;
        bool any() const { return !lost.empty(); }
    };

    // Where the bytes of an imported file can be found right now.
    //
    // **A save has to carry `imports/` forward or it deletes it**, and that is
    // not obvious from anywhere else in this class. The build-alongside-and-swap
    // replaces every directory entry in the project, so a folder assembled
    // without the imported files is a folder that has lost them -- and unlike a
    // cel, whose pixels are in memory and can always be written out again,
    // there is nothing in the document to write. A reference layer holds a
    // *name*.
    //
    // So the bytes have to be found on disk, and there are three places they
    // can be, asked in this order:
    //
    //   1. the folder the last successful save wrote them to, which is
    //      `SaveState::folder` and is where they are for every save after the
    //      first, Save As included;
    //   2. `pending`, for a file imported this session into a project that has
    //      never been saved -- the only moment an import is not yet inside a
    //      project folder, and the reason this parameter exists at all;
    //   3. `folder` itself, for a re-save whose state was lost or never taken.
    //
    // A name that is in none of them is reported rather than skipped. Losing
    // the picture quietly is the failure this whole arrangement exists to
    // prevent, and a save that cannot find it has to say so while the original
    // is still wherever the person imported it from.
    struct Imports {
        // Import name -- as it appears in `Layer::reference_source` -- to an
        // absolute path holding its bytes today.
        std::unordered_map<std::string, QString> pending;

        // The same, for soundtracks, which live in `audio/` rather than
        // `imports/`. A second map and not a second key format, because the two
        // folders are two namespaces: a picture and a sound may both be called
        // `take-3.x` without either shadowing the other.
        std::unordered_map<std::string, QString> pending_audio;
    };

    // The conventional suffix for a project folder. Not enforced anywhere; a
    // project is a folder and works under any name.
    static QString folderSuffix();

    // Writes the whole project. Nothing in `folder` is disturbed unless the
    // save succeeds completely: it is built alongside and swapped in at the
    // end, so an interrupted save leaves the last good one where it was.
    static bool save(const animage::Document& doc, const QString& folder,
                     QString* error = nullptr);

    // The same, skipping the cels that have not moved since `state` was taken.
    // Encoding and deflating the pixels is nearly all of a save's cost, so a
    // save where little changed costs little -- which is what makes autosave
    // affordable.
    //
    // `state` is read for what was written last time and, if the save
    // succeeds, replaced with what was written this time. On failure it is
    // left alone. Saving somewhere other than `state.folder` re-encodes
    // everything, so Save As always produces a project that stands on its own.
    static bool save(const animage::Document& doc, const QString& folder, SaveState& state,
                     QString* error = nullptr, const Imports& imports = {});

    // Renaming one folder to another, as the save's last step performs it.
    // Handed in rather than called directly so that a test can supply one that
    // refuses -- see `swapIntoPlace`.
    using RenameFn = std::function<bool(const QString& from, const QString& to)>;

    // The swap at the end of a save: the old project moved aside, the new one
    // moved into place, and only then the old one deleted. True when `folder`
    // holds the new project.
    //
    // **Nothing here deletes the only copy of anything**, which is the whole of
    // what it is for. On every path out either `folder` holds a complete
    // project, or `error` names every folder that does -- because a message
    // that names the path is the difference between a recoverable scare and a
    // lost shot.
    //
    // Exposed because the failure it exists to survive is a rename the
    // filesystem refuses at one exact instant -- a sync client or a virus
    // scanner holding the path for a moment, which Windows does -- and there is
    // no way to arrange that from outside the function. Everything but the
    // rename is the real filesystem in the test too.
    static bool swapIntoPlace(const QString& scratch, const QString& folder,
                              const RenameFn& rename, QString* error);

    // Replaces everything in `doc`. On failure `doc` is untouched and `error`
    // says what was wrong -- a project that will not open must not take the
    // open one down with it. That holds however this returns: the document is
    // built whole, in isolation, and adopted only at the end.
    //
    // With a `Damage*`, a cel that cannot be read costs that drawing rather
    // than the project: it is recorded there, left empty, and the load goes on.
    // A missing or unreadable `scene.json` still refuses outright, because
    // there is no document without it.
    static bool load(animage::Document& doc, const QString& folder, QString* error = nullptr,
                     Damage* damage = nullptr);

    // The same, recording what was on disk so the first save after an open is
    // incremental too. Everything just read matches its file by definition --
    // which is exactly why a caller that took damage must **not** keep this
    // state for the folder it read. A cel that could not be read is recorded at
    // the revision of the empty cel standing in for it, so the state claims a
    // file matches pixels that file has never held. The next save would then
    // carry that file forward as current, which is the damage being propagated
    // rather than noticed.
    static bool load(animage::Document& doc, const QString& folder, SaveState& state,
                     QString* error = nullptr, Damage* damage = nullptr);

    // --- the cel bytes, uncompressed ----------------------------------------
    //
    // Lossless, and not by being careful -- by construction. What a tile holds
    // is IEEE binary16, and what this writes is those same sixteen bits. There
    // is no conversion in either direction and so nothing to lose.
    //
    // That rules out the obvious choice: a 16-bit PNG cannot hold a half-float.
    // Half spends its precision relatively, finely near zero and coarsely near
    // one, while integers are evenly spaced; of the 15362 half values in [0,1]
    // a 16-bit integer image keeps 7169 of them, and some non-zero values
    // quantise to zero. sRGB-encoding first keeps 10871, which is better and
    // still not all of them. A save that loses pixels is not a save.
    //
    // So the format is ours. It is deliberately dull and self-describing:
    // magic bytes, a version, and the numbers needed to read the rest, all
    // little-endian so the file means the same thing on any machine.
    //
    //   offset  0   "ANIMCEL2"                    magic and format version
    //           8   uint32  tile size in pixels   128; a change is detectable
    //          12   uint32  channels              4, RGBA
    //          16   uint32  sample format         0 = IEEE binary16, little-endian
    //          20   uint32  tile count
    //          24   int32 x, int32 y  per tile    where each tile sits, in tiles
    //         ...   per tile, in the same order:
    //                 128 * (uint16 begin, uint16 end)   the occupied span of each
    //                                                    row; begin == end is empty
    //                 then (end - begin) * 4 * uint16 for each row, in row order
    //
    // Pixels are premultiplied and in linear light, exactly as they are in
    // memory. Anything outside a row's span is transparent. Tiles are written
    // in a fixed order so that saving an unchanged drawing twice produces
    // identical bytes.
    //
    // Only the occupied span of each row is stored, which is the difference
    // between a usable save and an unusable one. A three-pixel line crossing a
    // 128x128 tile leaves it 99% empty, so writing tiles whole meant handing
    // the compressor 457 MB to produce 3.3 MB. Spans cost 512 bytes a tile and
    // remove almost all of it.
    static std::vector<std::uint8_t> encodeCel(const animage::TileGrid& tiles);

    // The header fields, for checking a file without decoding a megabyte of it.
    struct CelFileInfo {
        int tile_size = 0;
        int channels = 0;
        std::size_t tile_count = 0;
    };

    // False on anything malformed, with `error` saying what. `out` is
    // untouched on failure, so a corrupt cel loses that drawing rather than
    // the project.
    static bool decodeCel(const std::vector<std::uint8_t>& bytes, animage::TileGrid& out,
                          std::string* error = nullptr);
    static bool readCelFileInfo(const std::vector<std::uint8_t>& bytes, CelFileInfo& out,
                                std::string* error = nullptr);

    // --- the scene text, JSON ------------------------------------------------

    // The whole document as scene.json text: the structure, without the
    // pixels. What a CTG cel stores is its scribbles; the fill is derived and
    // is never written, so a file cannot carry a fill that disagrees with the
    // drawing.
    static std::string writeSceneJson(const animage::Document& doc);

    // False on anything malformed or of a version this build does not
    // understand, with `error` saying why. `doc` is left untouched unless the
    // whole scene reads.
    static bool readSceneJson(std::string_view text, animage::Document& doc,
                              std::string* error = nullptr);

    // Every cel id the scene refers to, once, in the order the file lists
    // them: the manifest a save writes and a load reads, one file per cel.
    static std::vector<animage::CelId> celsReferencedBy(const animage::Document& doc);

    // Every imported file the scene names, once. The same manifest idea for
    // `imports/` that `celsReferencedBy` is for `cels/`, and it exists for the
    // same reason: a save assembles a folder rather than editing one, so it
    // needs the complete list of what belongs in it.
    //
    // Sorted, so that two saves of the same scene do the same work in the same
    // order and a failure is reproducible.
    static std::vector<std::string> importsReferencedBy(const animage::Document& doc);

    // The same for `audio/`: every file the scene's soundtracks name. A save
    // carries these forward exactly as it carries imports, and for the identical
    // reason -- nothing in the document can rebuild a sound. The decoded samples
    // are derived and are not written anywhere.
    static std::vector<std::string> audioReferencedBy(const animage::Document& doc);
};
