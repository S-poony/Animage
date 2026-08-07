// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
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
    void setFramerate(int framerate);

    // How long the shot is, in frames. Zero means "as long as the longest
    // track", which is the default and what happened before it could be said.
    // Clamped rather than validated, like the canvas: a negative length has
    // nothing to mean.
    void setSceneLength(int frames);

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

    // How far the marks a drawing is carrying were moved to get there, as the
    // last solve of it worked out.
    //
    // Derived like everything else about a fill, and kept here because two
    // other things have to agree with the fill about where a mark ended up and
    // neither can afford to work it out: the Marks column draws the scribbles
    // where they were used, and the first stroke on a carrying drawing copies
    // what it was showing. Both would otherwise put the mark somewhere the fill
    // it produced says it is not.
    //
    // Zero when nothing has solved this drawing yet, which is the same answer
    // as "it did not move" and is the behaviour it replaced.
    using CtgShifts = std::unordered_map<CtgKey, CtgShift, CtgKeyHash>;
    CtgShifts& ctgShifts() { return ctg_shifts_; }
    CtgShift ctgShiftAt(ImageId image, LayerId layer) const;

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

    // The identity of the edit on top of the undo stack, or 0 when the history
    // is empty. Two calls agree exactly when the top edit is the same command.
    // Unlike undoDepth(), it stays distinct after undo-and-edit branches the
    // history back to the same depth, so "is the document back where I saved
    // it" can be answered by comparing tokens instead of depths.
    std::size_t historyToken() const {
        return undo_stack_.empty() ? 0 : undo_stack_.back().id;
    }

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
    std::size_t next_command_id_ = 1;

    std::vector<Command> undo_stack_;
    std::vector<Command> redo_stack_;

    CtgFillCache ctg_cache_;
    CtgShifts ctg_shifts_;
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
