// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "cel.h"
#include "command.h"
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

    TimelineId addTimeline(std::string name);
    void removeTimeline(TimelineId timeline);
    void setFramerate(int framerate);

    // index 0 is the top of the stack.
    LayerId addLayer(TimelineId timeline, std::string name, std::size_t index = 0,
                     LayerKind kind = LayerKind::Raster);
    void removeLayer(TimelineId timeline, LayerId layer);
    void moveLayer(TimelineId timeline, std::size_t from, std::size_t to);
    void updateLayer(TimelineId timeline, LayerId layer, const Layer& properties);

    // --- time ------------------------------------------------------------

    // A new image with no cels, inserted at `slot`. Allocates no tiles.
    ImageId insertImage(TimelineId timeline, std::size_t slot);

    // Holds the image already at `slot` for `extra` more frames by repeating
    // its id in the slots list. Touches no cel.
    void extendExposure(TimelineId timeline, std::size_t slot, int extra);

    // Removes one slot. The Image record survives if other slots still show it,
    // so this shortens a hold rather than deleting the drawing.
    void removeSlot(TimelineId timeline, std::size_t slot);

    // Removes the drawing and every slot showing it, in one command.
    void removeDrawing(TimelineId timeline, ImageId image);

    // Moves a drawing, and the whole run of frames it is held over, so that it
    // starts at `destination` in the timeline as it will be once the drawing
    // has been lifted out. Reordering only: no cel is touched.
    void moveDrawing(TimelineId timeline, ImageId image, std::size_t destination);

    // Deep copy: new ImageId and a new CelId per layer. The tiles themselves
    // are shared until one side is drawn on, so the copy is nearly free but the
    // two images are genuinely independent.
    ImageId duplicateImage(TimelineId timeline, std::size_t slot);

    // --- pixels ----------------------------------------------------------

    const Cel* cel(CelId id) const;
    const Cel* celAt(TimelineId timeline, ImageId image, LayerId layer) const;

    // Returns the cel to draw into, creating it on first use. Must be called
    // inside a command; the lazy creation is recorded so undo removes it.
    Cel* celForWriting(TimelineId timeline, ImageId image, LayerId layer);

    // Detaches the cel from this (image, layer). The cel itself survives as
    // long as the history can bring it back.
    void clearCel(TimelineId timeline, ImageId image, LayerId layer);

    std::size_t celCount() const { return cels_.size(); }
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

    // Drops cels that no image references and no history entry mentions. The
    // history counts as a reference: a cel must survive the deletion of the
    // last image showing it for as long as undo can bring that image back.
    std::size_t collectGarbage();

    // --- used by Op implementations --------------------------------------

    Scene& mutableScene() { return scene_; }
    void addCelRef(CelId id);
    void releaseCelRef(CelId id);
    void recordOp(std::unique_ptr<Op> op);

private:
    CelId createCel();
    CelId createCelCopy(const Cel& source);
    bool historyReferences(CelId id) const;

    Scene scene_;
    std::unordered_map<CelId, std::shared_ptr<Cel>> cels_;

    IdGenerator cel_ids_;
    IdGenerator image_ids_;
    IdGenerator layer_ids_;
    IdGenerator timeline_ids_;

    Command pending_;
    int command_depth_ = 0;
    TileJournal journal_;

    std::vector<Command> undo_stack_;
    std::vector<Command> redo_stack_;
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
