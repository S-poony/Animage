// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cel.h"
#include "command.h"
#include "ctg_fill.h"
#include "scene.h"
#include "transform.h"

namespace animage {

// A bounded store of what imported files decode to: one entry per drawing per
// reference layer, holding the already-placed pixels.
//
// **`CtgFillCache` is the template and the resemblance is not an accident.**
// Both are derived data, bounded, kept on the Document rather than on the thing
// they describe, precisely because losing an entry costs a rebuild and nothing
// else -- a recompute there, a decode here. Read that class before changing
// this one; four of its decisions are repeated below for the same reasons.
//
// It lives here rather than in a file of its own because the key it needs is
// `CtgKey`, which document.h already has, and because Document is its only
// owner. Nothing outside asks it a question that Document does not forward.
//
// **Absent beats stale, which is the one thing here that is not `CtgFillCache`
// with the words changed.** An entry records the placement it was derived at,
// and a lookup at any other placement reports *nothing here*. A frame derived
// under an old placement is not a slightly out-of-date picture, it is a picture
// of where the import used to be, and it would go on being drawn convincingly
// until something happened to refresh it.
//
// **Eviction may only happen where the document may be edited.** This does not
// look like an invariant, which is why it is written down: `LayerPass` holds
// raw pointers into these grids and `compositeGrids` reads them from several
// threads, so dropping an entry while a paint is in flight is a dangling read.
// It is the same rule the document already has -- it simply does not read as
// "editing the document" to whoever writes the eviction.
class ReferenceCache {
public:
    // Defined in document.cpp, where the default budget is, so that the number
    // and the arithmetic that argues for it stay in one place.
    ReferenceCache();

    // Null when there is no entry, and null when the entry was derived under a
    // different placement. The caller cannot tell the two apart and does not
    // need to: both mean "ask for this again".
    //
    // References into the store survive an insertion, because the map is
    // node-based and the entry being returned is never the one evicted.
    const TileGrid* find(const CtgKey& key, const Transform& under) const;

    // Whether a frame is here, **without counting it as used**.
    //
    // For counting how much of a sequence is ready, which is a question asked
    // about every drawing of it at once. Asking is not using: going through
    // `find` would renew all hundred and fifty of them on every status update,
    // so every frame would look equally recent and the eviction order -- the
    // whole of what keeps a scrub's own frames resident -- would be flattened
    // by the thing that merely wanted to report on it.
    bool has(const CtgKey& key, const Transform& under) const;

    void store(const CtgKey& key, const Transform& under, TileGrid tiles);

    void clear();

    // Moves the bound, and takes effect immediately. The default is half a
    // gigabyte; see the definition in document.cpp for the arithmetic behind
    // that and what would change it.
    //
    // Reachable so that the rule above -- a *lookup* keeps an entry alive, a
    // store does not -- can be tested against a budget a test can fill, rather
    // than by building half a gigabyte of tiles. That rule is the one worth
    // pinning: it is what stops a scrub evicting the frames being scrubbed
    // over, and it is invisible until somebody has already made it wrong.
    void setByteBudget(std::size_t bytes);

    // How many times the store has been emptied.
    //
    // The same counter `CtgFillCache` has and for the same reason, which is
    // worth stating because it is the one piece here that exists before the bug
    // rather than after it. What a decoded frame depends on is not all in its
    // key: the source list is on the layer and so is the placement, and the way
    // both say "that is all wrong now" is by emptying this. That reaches the
    // shelf and not the answers in the air -- a decode started before a
    // placement changed lands after it and would be installed as current.
    // Anything with a decode in flight records this alongside what it asked.
    std::uint64_t generation() const { return generation_; }

    std::size_t size() const { return entries_.size(); }

    // What the frames held weigh, which is every tile of every one of them.
    //
    // Unlike a fill's marks, none of this is shared with anything else: each
    // frame is decoded on its own, so a tile in here is a tile this cache is
    // the reason for. Two drawings pointed at the same source frame decode
    // twice and are counted twice, which is what actually happens.
    std::size_t bytes() const { return bytes_; }

    // How many frames have been put in, which is how many decodes have
    // happened. Exposed for the reason CtgFillCache counts its stores: "did
    // that decode again?" has no honest answer but a count, and a wrong key
    // does not fail, it only gets slow.
    std::uint64_t storeCount() const { return stores_; }

private:
    struct Entry {
        // What the pixels were derived at. Held rather than assumed, because a
        // placement is stored on the layer and can be changed and undone, and
        // nothing else about the layer moves when it does.
        Transform under;
        TileGrid tiles;
        // Touched by a *lookup* and not by a store, so the order reflects what
        // is being looked at rather than what was last decoded. That is the
        // whole point on this cache: scrubbing back and forth over a syllable
        // must not evict the frames being scrubbed over.
        mutable std::uint64_t used = 0;
    };

    void evictDownToBudget(const CtgKey& keep);

    std::unordered_map<CtgKey, Entry, CtgKeyHash> entries_;
    mutable std::uint64_t clock_ = 0;
    std::size_t bytes_ = 0;
    std::size_t budget_;
    std::uint64_t stores_ = 0;
    std::uint64_t generation_ = 0;
};

// Owns the scene, every cel, the id counters and the undo history. Editing goes
// through this class rather than through the structs directly, because that is
// the only way an operation can be recorded for undo.
class Document {
public:
    Document();

    const Scene& scene() const { return scene_; }

    // --- structure -------------------------------------------------------

    TrackId addTrack(std::string name);
    void removeTrack(TrackId track);
    void updateTrack(TrackId track, const TrackProperties& properties);

    // --- soundtracks -----------------------------------------------------
    //
    // Their own list on the Scene rather than a kind of Track, and so their own
    // three functions here rather than a flag threaded through the ones above.
    // See audio_track.h.
    //
    // Ids come from the same counter as a drawing track's, which is what stops
    // the two ever colliding; what stops them being *confused* is that they are
    // looked up by different functions in different lists, and that an id
    // handed to the wrong one answers nothing rather than something plausible.

    // Adds a soundtrack naming a file inside the project's `audio/` folder.
    // An edit, journaled and undoable like adding a track.
    TrackId addAudioTrack(std::string name, std::string source);
    void removeAudioTrack(TrackId track);

    // Where the sound sits in the shot, and how loud. One command for both,
    // because the timeline row's drag changes gain and the dialog's box changes
    // the offset, and neither wants half a struct.
    void setAudioTrackPlacement(TrackId track, int offset_frames, double gain);

    // Installs decoded samples. **Not an edit**: no command, no journal entry,
    // no undo step. The document is not changed by this, only the memo of what
    // a file decodes to -- exactly as setReferenceFrame is not an edit, and for
    // the same reason. `core` never opens a file; what decodes is the
    // application, which knows about `audio/` and about Qt.
    void setAudioSamples(TrackId track, AudioClip clip);

    // What that file decoded to, or null if nothing has decoded it yet. A
    // caller that finds nothing draws nothing and plays silence -- it must not
    // start a decode, for the reason compositing may not start one.
    const AudioClip* audioSamplesFor(TrackId track) const;

    // Throws the decoded samples away. Everything a clip depends on that is not
    // in its key says so by calling this: a track being repointed at another
    // file, a document being replaced by another whose tracks answer to the
    // same ids.
    void forgetAudioSamples();

    // Restacks a track: index 0 is the top row of the timeline and the top group
    // of the composite. `to` is counted in the list with the track already taken
    // out of it, which is the same convention moveLayer uses and is what makes
    // moveTrack(to, from) the exact inverse.
    //
    // Reordering only: no image, no cel and no slot is touched, so a track keeps
    // its own time whatever it is stacked against.
    void moveTrack(std::size_t from, std::size_t to);

    void setFramerate(int framerate);

    // How long the shot is. `fixed` off takes it from the tracks, which is the
    // default; on makes `frames` the shot whatever the tracks do, and a track is
    // then allowed to run past it. Clamped rather than validated, like the
    // canvas: a negative length has nothing to mean.
    //
    // Nothing a track does may call this. The scene sits above the tracks, so
    // adding a drawing lengthens the track and never the shot -- a setting that
    // edits itself when you draw is not a setting.
    void setSceneLength(bool fixed, int frames);

    // The exported rectangle. Clamped to something sane rather than validated:
    // a scene with a zero-width canvas has nothing to show and nothing to
    // write, and there is no reason for the rest of the code to carry the case.
    void setCanvasSize(int width, int height);

    // index 0 is the top of the stack.
    LayerId addLayer(TrackId track, std::string name, std::size_t index = 0,
                     LayerKind kind = LayerKind::Raster);
    void removeLayer(TrackId track, LayerId layer);
    void moveLayer(TrackId track, std::size_t from, std::size_t to);
    void updateLayer(TrackId track, LayerId layer, const Layer& properties);

    // --- time ------------------------------------------------------------

    // A new image with no cels, inserted at `slot`. Allocates no tiles.
    ImageId insertImage(TrackId track, std::size_t slot);

    // Holds the image already at `slot` for `extra` more frames by repeating
    // its id in the slots list. Touches no cel.
    void extendExposure(TrackId track, std::size_t slot, int extra);

    // Removes one slot. The Image record survives if other slots still show it,
    // so this shortens a hold rather than deleting the drawing.
    void removeSlot(TrackId track, std::size_t slot);

    // Removes the drawing and every slot showing it, in one command.
    void removeDrawing(TrackId track, ImageId image);

    // Moves a drawing, and the whole run of frames it is held over, so that it
    // starts at `destination` in the track as it will be once the drawing
    // has been lifted out. Reordering only: no cel is touched.
    void moveDrawing(TrackId track, ImageId image, std::size_t destination);

    // The same drag on a track that overwrites, where `slot` is the frame the
    // drawing was dropped on rather than a position between drawings -- the two
    // are different questions and neither can be derived from the other, which
    // is why this is a second function and not an argument.
    //
    // The drawing takes over the rest of the hold it lands in, and the frames it
    // came from are absorbed by whichever drawing is next to them, so the track
    // keeps its length and every drawing stays one contiguous run. With no room
    // where it was dropped -- a hold of one frame -- this reorders instead,
    // which is length-preserving too and is the nearest thing to what was asked.
    //
    // Dropped inside its own hold it does nothing. A drag that lands on the
    // drawing you picked up has not retimed anything, and shortening a hold from
    // the front is what Hold - is for.
    void moveDrawingOver(TrackId track, ImageId image, std::size_t slot);

    // Deep copy: new ImageId and a new CelId per layer. The tiles themselves
    // are shared until one side is drawn on, so the copy is nearly free but the
    // two images are genuinely independent.
    ImageId duplicateImage(TrackId track, std::size_t slot);

    // What the two buttons do, with the playhead's slot: a new drawing, or a
    // copy of the one you are standing on, placed the way the track says.
    //
    // Without overwrite that is after the whole hold, lengthening the track --
    // landing one in the middle of a ten-frame hold splits it in two, which is
    // never what pressing the button meant. With overwrite it is on the playhead
    // itself, spending the rest of the hold. Where the drawing ended up is
    // `Track::firstSlotOf` on what comes back; it is not always `slot`.
    ImageId addDrawing(TrackId track, std::size_t slot);
    ImageId duplicateDrawing(TrackId track, std::size_t slot);

    // --- pixels ----------------------------------------------------------

    const Cel* cel(CelId id) const;
    const Cel* celAt(TrackId track, ImageId image, LayerId layer) const;

    // The scribbles a CTG layer shows at this drawing: its own, or -- when it
    // has none -- the nearest earlier drawing's. Colour once and the colour
    // carries forward until somebody changes it, which is the whole of
    // "scribbles through time" part one.
    //
    // Not the same as celAt, and deliberately a different function rather than
    // a flag on it: on any other kind of layer an absent cel still means the
    // layer is empty here, and that must stay true. Returns nullptr for a layer
    // that is not CTG, so a caller cannot get inheritance by accident.
    //
    // `source`, if given, receives the drawing the scribbles belong to. Equal
    // to `image` when they are its own, which is how anything showing the
    // distinction -- and anything keyed on it -- tells the two apart.
    const Cel* ctgScribblesAt(TrackId track, ImageId image, LayerId layer,
                              ImageId* source = nullptr) const;

    // Returns the cel to draw into, creating it on first use. Must be called
    // inside a command; the lazy creation is recorded so undo removes it.
    Cel* celForWriting(TrackId track, ImageId image, LayerId layer);

    // Detaches the cel from this (image, layer). The cel itself survives as
    // long as the history can bring it back.
    void clearCel(TrackId track, ImageId image, LayerId layer);

    // What a layer bake did, which is two facts and not one: how many drawings
    // it wrote, and whether it ran out of memory partway and put them back.
    //
    // A count alone could not say that. Nothing written is the answer for an
    // identity, for a layer with no drawings on it and for a layer that would
    // not fit -- and the last of those is the only one anybody has to be told
    // about.
    struct LayerBake {
        std::size_t drawings = 0;
        bool ran_out_of_memory = false;
    };

    // Every drawing of one layer, moved by the same numbers, in one command.
    // Issue #25.
    //
    // **It bakes, and that is the decision.** The other shape -- an affine
    // stored on the Layer and applied at composite time -- is free, lossless
    // and adjustable afterwards, and it makes every other thing that reads a
    // layer's pixels read them through a matrix: the brush, the eyedropper,
    // ctgBarrier, celBounds, fit-to-drawing and export. Baking leaves all of
    // those alone, and it leaves the saved format alone, at the price of one
    // resample per drawing and a command that journals the layer twice over.
    // The user chose the price. See "transforming a layer through time" in
    // docs/handover.md.
    //
    // **Distinct drawings and not slots.** A drawing held over ten frames is
    // one cel and is resampled once; walking `slots` would resample it ten
    // times over, each pass reading what the last one left. That is the trap
    // docs/lasso-and-transform.md parked here, and it is the whole of why this
    // walks `images`.
    //
    // An identity writes nothing, for the same reason it writes nothing on one
    // drawing: a commit softens line art, so one that changed nothing would be
    // a pure loss. Nothing here asks whether the layer is locked, hidden or a
    // colour layer -- those are refusals about a gesture and belong where the
    // gesture is.
    //
    // **It survives running out of memory, and that is why it is written this
    // way.** commitFitsInBudget bounds the ask against what the layer already
    // occupies, which is a bound measured against something this machine has
    // proved it can hold -- but a bake's peak is the new tiles plus the old
    // ones the journal is keeping for undo, and a large enough layer can still
    // exhaust a machine. Everywhere else in this program an allocation failure
    // on the interface thread is a crash, and here it would be a crash with the
    // layer half rewritten inside an open command. So the failure is caught,
    // every drawing already written is put back, and the caller is told. The
    // one other place that swallows a bad_alloc is CtgSolver, and its comment
    // says why it could afford to: nothing in the document is half written when
    // a solve throws. This is the opposite case, which is why it undoes rather
    // than merely shrugging.
    LayerBake transformLayer(TrackId track, LayerId layer, const Transform& t);

    // **A rescued bake leaves the history exactly as it found it**, which is
    // what its refusal message promises and what the first version of it did
    // not do. Closing a command trims the history to its byte budget, and one
    // bake is most of that budget on its own -- so an ordinary close drops
    // every older command to make room, and the rescue then throws away the
    // command the room was made for. The session's undo history went with it.
    // The trim is held until the outcome is known instead. See endCommand.
    //
    // Makes the next call to transformLayer fail after `drawings` of them, so
    // the rescue above can be asserted rather than merely written.
    //
    // A hook in `core` for a test to pull is not free, and it is here because
    // the alternative is worse: the rescue is what makes bounding the growth
    // rather than the total defensible, and an untested rescue is a rescue that
    // stops working quietly. Running out of memory on purpose is not something
    // a test can arrange -- a machine with room to swap succeeds and is merely
    // slow, and one without it takes the test process down with it.
    //
    // Nothing in the interface reaches this, and the next call to
    // transformLayer takes it whether or not that call gets as far as writing
    // anything -- so a hook armed before an identity, or before a layer with no
    // drawings on it, cannot go off on some later bake instead.
    void failLayerBakeAfterForTesting(std::size_t drawings) { fail_bake_after_ = drawings; }

    // The drawings `transformLayer` would move, in the order it would move
    // them: distinct images with a cel on this layer, sorted.
    //
    // Exposed because everything else that has to agree with the bake about
    // exactly which drawings are in it must not work it out for itself -- the
    // budget, the count the status bar promises, and the ghost picture the live
    // gesture shows. Sorted, and that is not tidiness: `images` is an
    // unordered_map, so its walk order is not the timeline's and is not even
    // stable between two runs of the same program. A bake that journalled its
    // tiles in a different order each time would be a bake no test could assert
    // anything about, and a ghost picture merged in that order would be a
    // different picture each time it was drawn.
    std::vector<ImageId> layerDrawings(TrackId track, LayerId layer) const;

    // The same list as grids, for the budget. The pointers are the document's
    // and are good for exactly as long as it is not edited.
    std::vector<const TileGrid*> layerGrids(TrackId track, LayerId layer) const;

    std::size_t celCount() const { return cels_.size(); }

    // Regenerated fills for CTG layers, keyed by (drawing, layer). Kept here
    // rather than on the Cel because it is derived data: losing it costs a
    // recompute and nothing else, which is also why the store is allowed to be
    // bounded. See CtgFillCache.
    CtgFillCache& ctgCache() { return ctg_cache_; }
    const CtgFillCache& ctgCache() const { return ctg_cache_; }

    // How the marks a drawing is carrying were moved to get there, as the last
    // solve of it worked out.
    //
    // Derived like everything else about a fill, and kept here because two
    // other things have to agree with the fill about where a mark ended up and
    // neither can afford to work it out: the Marks column draws the scribbles
    // where they were used, and the first stroke on a carrying drawing copies
    // what it was showing. Both would otherwise put the mark somewhere the fill
    // it produced says it is not.
    //
    // The warp and not the marks it produced. A warp is a few hundred bytes and
    // this map is unbounded -- one entry per drawing anybody has looked at --
    // where a carried mark grid is pixels, and would make looking through a
    // coloured shot cost the marks of every drawing in it twice over. What
    // costs pixels is bounded instead: see ctgCarriedMarksAt.
    //
    // Zero when nothing has solved this drawing yet, which is the same answer
    // as "it did not move" and is the behaviour it replaced.
    // Written through a function rather than by handing the map out, because
    // the pixels memoised below are derived from it and a warp replaced behind
    // their back is a drawing showing the marks of a solve that has been
    // superseded.
    using CtgCarries = std::unordered_map<CtgKey, CtgWarp, CtgKeyHash>;
    void setCtgCarry(const CtgKey& key, const CtgWarp& warp);
    const CtgWarp& ctgCarryAt(ImageId image, LayerId layer) const;

    // The marks of one drawing, where that drawing shows them.
    //
    // The one answer to "where is this mark", for everything that is not
    // holding a fill. A fill carries its own marks already carried; this is
    // for the Marks column, which is shown whether or not there is a fill, and
    // for the copy the first stroke on a carrying drawing takes.
    //
    // Two shapes of answer, because the cheap one is the ordinary one. A
    // uniform warp -- no motion, one region, or every region agreeing -- comes
    // back as the cel's own tiles and an offset, which the compositor draws by
    // reading a moved rectangle and costs nothing at all. Only a warp with a
    // field materialises pixels, and those are memoised, because compositing
    // happens per repaint and warping does not.
    struct CarriedMarks {
        const TileGrid* tiles = nullptr;
        CtgShift offset;  // zero when `tiles` are already where they are drawn
    };
    CarriedMarks ctgCarriedMarksAt(TrackId track, ImageId image, LayerId layer) const;

    // The regenerated fill for a CTG layer, or null if it has not been built.
    // Const, so the compositor can draw one but never trigger a solve: the
    // caller decides when it is worth paying for.
    const CtgFill* ctgFillFor(TrackId track, ImageId image, LayerId layer) const;
    std::size_t totalTileCount() const;

    // --- imported pictures -----------------------------------------------

    // Which frame of its source a Reference layer shows at one drawing.
    //
    // **An edit, unlike everything else in this section.** The pixels below are
    // derived and cost a decode to lose; this is a fact about the shot that
    // only the file remembers, so it is journaled, undone and saved like any
    // other. `Image::kNoSourceFrame` removes the entry, which is what makes the
    // layer empty at that drawing.
    //
    // It rides on ImageOp, which swaps a whole Image record: the map lives on
    // the Image and there is nothing smaller to swap. One command per call, so
    // an import of two hundred frames wraps the lot in a ScopedCommand rather
    // than leaving two hundred steps on the stack.
    void setSourceFrame(TrackId track, ImageId image, LayerId layer, int frame);

    // The derived pixels of a Reference layer at one drawing, or null if they
    // have not been built yet. Const for exactly the reason `ctgFillFor` is:
    // **compositing is not the place to start a decode**, so a paint draws
    // whatever is here and asks for what is missing somewhere it is allowed to
    // wait. See docs/importing.md.
    //
    // `core` never opens a file. What derives these is the application, which
    // knows about `imports/` and about QImage; this class only holds the
    // answer, exactly as it holds a fill somebody else solved.
    // `under` is the placement the answer has to have been derived at. A frame
    // derived under a different one is not stale-and-usable, it is **the wrong
    // picture** -- a modelsheet at the size and angle it used to be -- so it is
    // reported as absent and the layer draws nothing until the right one
    // arrives. That is the same choice the fill cache makes and for the same
    // reason: see "why a cache key of cel revisions serves wrong fills, not slow
    // ones" in docs/handover.md, which is this mistake made the other way round.
    const TileGrid* referenceFrameFor(TrackId track, ImageId image, LayerId layer,
                                      const Transform& under) const;

    // Installs derived pixels. Not an edit: no command, no journal, no undo
    // entry and no cel -- the document is not changed by this, only the
    // memo of what a file decodes to. That is why it is allowed to be called
    // from a paint.
    void setReferenceFrame(TrackId track, ImageId image, LayerId layer, const Transform& under,
                           TileGrid tiles);

    // Throws the derived pixels away. Everything a reference frame depends on
    // that is not in its key says so by calling this -- the layer's source
    // being repointed, a document being replaced by another whose drawings
    // answer to the same ids.
    void forgetReferenceFrames();

    // The store itself, for what only it can answer: how many frames are
    // resident, what they weigh, and the generation anything with a decode in
    // flight has to record. Const, because installing a frame goes through
    // setReferenceFrame -- which is the one place the eviction rule above is
    // honoured.
    const ReferenceCache& referenceCache() const { return reference_frames_; }

    // Moves the cache's bound. Here rather than on a non-const accessor,
    // because lowering it evicts, and the rule is that evicting happens only
    // where the document may be edited -- an accessor that handed the cache out
    // by reference would put that decision at every call site instead.
    void setReferenceCacheBudget(std::size_t bytes) { reference_frames_.setByteBudget(bytes); }

    // --- history ---------------------------------------------------------

    // Nested calls join the outermost command, so a high-level edit built from
    // several primitives still undoes in one step.
    void beginCommand(std::string label);
    void endCommand();

    TileJournal& journal() { return journal_; }

    // Forgets the history without touching the document. Used after building a
    // starting scene: the first Ctrl+Z should not undo the existence of the
    // layer you are drawing on.
    void clearHistory();

    bool canUndo() const { return !undo_stack_.empty(); }
    bool canRedo() const { return !redo_stack_.empty(); }
    bool undo();
    bool redo();
    std::size_t undoDepth() const { return undo_stack_.size(); }
    std::string undoLabel() const;

    // Which state the document is in: the stamp of the command on top of the
    // undo stack, and 0 when there is nothing on it. Undo and redo move along
    // the same stamps, so coming back to a state gives the number it had.
    //
    // This is what "changed since the last save" is asked with, and a depth is
    // not: undoing one step and doing a different one leaves the count where it
    // was over a document that has moved, and a trimmed history can lose steps
    // off the bottom while the work only grows.
    std::uint64_t historyStamp() const;

    // --- what the history is allowed to cost ------------------------------

    // A command retains the tiles it displaced, and nothing else can free them
    // while it holds them. One command is worth forty of another -- a stroke
    // crosses two to six tiles and pins a quarter to three quarters of a
    // megabyte, a full-cel 4K transform replaces every tile the cel has and
    // pins about seventy-five -- so a cap counting steps caps an amount of
    // memory nobody chose. This one counts bytes.
    //
    // Past the budget the oldest commands go, oldest first, and the cels they
    // were the last reference to go with them. The newest command is never
    // dropped: what you have just done stays undoable even when it is larger
    // than the whole budget on its own.
    //
    // 512 MB is about a thousand strokes, or six 4K transforms, on top of
    // whatever the document itself is holding.
    static constexpr std::size_t kDefaultHistoryBudget = std::size_t{512} * 1024 * 1024;

    // And a ceiling on the number of steps, which is deliberately not the
    // budget: at half a megabyte a stroke the bytes bind ten times sooner, so
    // this never ends a drawing session's history. It is here because a command
    // that displaces no tile at all -- retiming, restacking, renaming, every
    // one of which copies a vector -- is not free either, and a byte budget
    // that only sees pixels would let a day of them grow without limit.
    static constexpr std::size_t kHistoryStepCap = 10000;

    // Lowering it takes effect immediately, which is what makes it testable.
    void setHistoryBudget(std::size_t bytes);
    std::size_t historyBudget() const { return history_budget_; }

    // What the history is retaining now, in tile bytes. Both stacks: undoing
    // moves a command from one to the other and frees nothing.
    std::size_t historyBytes() const;

    // Drops cels that no image references and no history entry mentions. The
    // history counts as a reference: a cel must survive the deletion of the
    // last image showing it for as long as undo can bring that image back.
    std::size_t collectGarbage();

    // --- loading ---------------------------------------------------------

    // Replaces the document wholesale: the scene, the cels it refers to, and
    // nothing else. The history goes, because an undo across a file load has
    // nothing to mean.
    //
    // Every cel the scene names is created empty, with its image refcount taken
    // from the scene rather than trusted from the file, and the id counters
    // resume past the highest id in it. That last part is not housekeeping: ids
    // are never reused, and a counter that restarted at one would hand out an
    // id a loaded cel already answers to.
    void loadScene(Scene scene);

    // Gives a loaded cel its pixels. Only valid for a cel loadScene created;
    // false if the id is not one of them.
    bool setCelTiles(CelId id, TileGrid tiles);

    // --- used by Op implementations --------------------------------------

    Scene& mutableScene() { return scene_; }
    void addCelRef(CelId id);
    void releaseCelRef(CelId id);
    void recordOp(std::unique_ptr<Op> op);

private:
    CelId createCel();
    CelId createCelCopy(const Cel& source);
    bool historyReferences(CelId id) const;

    // Drops the tiles this command emptied, and the journal entries that then
    // record no change at all. Called once, when the outermost command closes.
    void releaseEmptiedTiles(std::vector<TileSnapshot>& tiles);

    // Drops the oldest commands until the history is inside its budget, and
    // collects the cels they were the last thing holding.
    void trimHistory();

    // A new drawing with its own copy of every cel of `source`, refcounted and
    // numbered, but in no slot yet. Shared by both ways of duplicating one.
    std::optional<Image> copyOfImage(Track& track, ImageId source);

    Scene scene_;
    std::unordered_map<CelId, std::shared_ptr<Cel>> cels_;

    IdGenerator cel_ids_;
    IdGenerator image_ids_;
    IdGenerator layer_ids_;
    IdGenerator track_ids_;

    Command pending_;
    int command_depth_ = 0;
    TileJournal journal_;

    std::vector<Command> undo_stack_;
    std::vector<Command> redo_stack_;
    std::uint64_t command_stamps_ = 0;
    std::size_t history_budget_ = kDefaultHistoryBudget;

    // Armed only by failLayerBakeAfterForTesting, and taken by the next call to
    // transformLayer whatever that call does. Absent means what it says:
    // nothing is going to be made to fail.
    std::optional<std::size_t> fail_bake_after_;

    // Set only by transformLayer, and only for as long as its own command is
    // open: a bake that runs out of memory undoes itself, and a history trimmed
    // to make room for a command that is then thrown away has spent the
    // session's undo on nothing. See endCommand.
    bool defer_trim_ = false;

    CtgFillCache ctg_cache_;
    CtgCarries ctg_carries_;

    // Warped mark grids, kept only for the drawings whose regions disagreed.
    //
    // Small on purpose and cleared rather than aged: what reads it is the
    // handful of drawings on screen at once -- the one being worked on and
    // whatever onion skin is showing -- and a miss costs a pass over a few mark
    // tiles. The alternative is holding pixels for every drawing anybody
    // scrubbed past, which is the memory the warp map above exists not to
    // spend.
    //
    // Checked against the cel it was made from and not only against the warp.
    // The warp is rewritten by a solve, and a mark drawn on the source drawing
    // changes the pixels before any solve of *this* drawing has run -- so an
    // entry that trusted the warp alone would go on showing marks that had been
    // rubbed out, until something unrelated re-solved.
    struct CarriedMarksEntry {
        CelId cel = kNoId;
        std::uint64_t revision = 0;
        TileGrid tiles;
    };
    static constexpr std::size_t kCarriedMarksKept = 16;
    mutable std::unordered_map<CtgKey, CarriedMarksEntry, CtgKeyHash> carried_marks_;

    // What imported files decoded to, per drawing and layer. Derived: nothing
    // here is written to disk and losing it costs a decode. Keyed the same way
    // a fill is -- see "why a cache key of cel revisions serves wrong fills,
    // not slow ones" in docs/handover.md for why the key names the drawing and
    // the layer rather than anything with a revision in it.
    ReferenceCache reference_frames_;

    // What soundtrack files decoded to, by track. Derived, like the cache
    // above, and unbounded unlike it -- a shot's worth of PCM is single-digit
    // megabytes where one HD picture frame is 17, so there is nothing here to
    // spend a budget on.
    std::unordered_map<TrackId, AudioClip> audio_samples_;
};

// RAII wrapper: begins a command on construction, ends it on destruction.
class ScopedCommand {
public:
    ScopedCommand(Document& doc, std::string label) : doc_(doc) {
        doc_.beginCommand(std::move(label));
    }
    ~ScopedCommand() { doc_.endCommand(); }

    ScopedCommand(const ScopedCommand&) = delete;
    ScopedCommand& operator=(const ScopedCommand&) = delete;

private:
    Document& doc_;
};

}  // namespace animage
