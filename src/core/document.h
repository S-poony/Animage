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

namespace animage {

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
