// SPDX-License-Identifier: GPL-3.0-or-later
#include "cel.h"

namespace animage {

Tile* Cel::writableTile(TileCoord c, TileJournal& journal) {
    if (!journal.alreadyRecorded(id_, c)) {
        const TileRef* slot = grid_.findSlot(c);
        journal.record(id_, c, slot ? *slot : TileRef{});
    }

    // Re-read after journalling: recording the tile took a reference to it, so
    // the use count below now correctly reports "someone else still needs the
    // old contents".
    const TileRef* slot = grid_.findSlot(c);

    std::shared_ptr<Tile> writable;
    if (!slot || !*slot) {
        writable = std::make_shared<Tile>();
    } else if (slot->use_count() > 1) {
        // Shared with the undo journal, with another cel after a duplicate
        // link, or with a render in flight. Clone before writing.
        writable = std::make_shared<Tile>(**slot);
    } else {
        writable = std::const_pointer_cast<Tile>(*slot);
    }

    Tile* raw = writable.get();
    grid_.set(c, std::move(writable));
    ++revision_;
    return raw;
}

void Cel::replaceTiles(TileGrid tiles, TileJournal& journal) {
    // Both sides: a tile the new grid does not have is a tile that is going
    // away, and its absence has to be undoable too.
    for (const auto& [coord, tile] : grid_.tiles()) {
        (void)tile;
        if (!journal.alreadyRecorded(id_, coord)) journal.record(id_, coord, grid_.find(coord));
    }
    for (const auto& [coord, tile] : tiles.tiles()) {
        (void)tile;
        if (!journal.alreadyRecorded(id_, coord)) journal.record(id_, coord, grid_.find(coord));
    }

    grid_ = std::move(tiles);
    ++revision_;
}

bool Cel::releaseIfEmpty(TileCoord c) {
    const TileRef* slot = grid_.findSlot(c);
    if (!slot || !*slot || !(*slot)->isFullyTransparent()) return false;
    grid_.set(c, TileRef{});
    return true;
}

void Cel::swapTile(TileCoord c, TileRef& other) {
    TileRef current = grid_.find(c);
    grid_.set(c, std::move(other));
    other = std::move(current);
    ++revision_;
}

}  // namespace animage
