#pragma once
#include "pager.h"
#include "btree_node.h"
#include "row_serializer.h"
#include <optional>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────
//  BTree (M1 version — single table, insert + search only)
//
//  This class owns one B-tree: a root page and zero or more leaf
//  pages, accessed via the Pager. For M1 we deliberately skip:
//    - Node splitting (leaves can't overflow yet)
//    - Internal nodes / tree height > 1
//    - Delete
//  These arrive in M2. Get the basics working end-to-end first.
//
//  How insert works (M1, single leaf):
//    1. Get the root page (which is a leaf page for now)
//    2. Find the right slot to insert in sorted key order
//    3. Shift existing cells right to make room
//    4. Write the new cell
//    5. Mark the page dirty
//
//  How search works:
//    1. Get the root page
//    2. Binary search for the key among the leaf's sorted keys
//    3. If found, deserialize the row bytes and return the Row
// ─────────────────────────────────────────────────────────────────
class BTree {
public:
    // `pager`     — the I/O layer (already opened on the .db file)
    // `root_page` — which page is the root of this B-tree
    // `num_cols`  — how many columns each row has (needed for deserialization)
    BTree(Pager& pager, PageId root_page, size_t num_cols)
        : pager_(pager), root_page_id_(root_page), num_cols_(num_cols) {}

    // ── Create a brand new empty B-tree ──
    // Call once when creating a new table.
    // Returns the page id allocated for the root (save this in your catalog).
    static PageId create(Pager& pager) {
        PageId root_id = pager.allocate_page();
        Page* root = pager.get_page(root_id);
        BTreeNode node(root);
        node.init_leaf(/*is_root=*/true);
        pager.mark_dirty(root_id);
        return root_id;
    }

    // ── Insert a row ──
    // The first element of the row (row[0]) must be an int64_t primary key.
    // Rows are kept in sorted key order inside each leaf.
    void insert(const Row& row) {
        // For M1: there's only one leaf page (the root).
        Page* root = pager_.get_page(root_page_id_);
        BTreeNode node(root);

        // Sanity check — M1 only handles a single leaf root
        if (node.node_type() != NodeType::Leaf) {
            throw std::runtime_error("BTree::insert — M1 only supports single-leaf trees");
        }
        if (node.leaf_is_full()) {
            throw std::runtime_error(
                "BTree::insert — leaf is full. Node splitting (M2) not yet implemented. "
                "Max rows per leaf: " + std::to_string(LEAF_MAX_CELLS));
        }

        // Get the primary key (must be int64_t for now)
        int64_t key = std::get<int64_t>(row[0]);

        // Find insertion slot: first slot where existing key >= new key
        uint16_t n = node.num_keys();
        uint16_t slot = find_insertion_slot(node, n, key);

        // Check for duplicate key
        if (slot < n && node.leaf_key(slot) == key) {
            throw std::runtime_error("BTree::insert — duplicate primary key: " + std::to_string(key));
        }

        auto row_bytes = RowSerializer::serialize(row);

        if (slot < n) {
            // Phase 1: read all existing cells + open a gap at `slot`
            std::vector<std::pair<int64_t, std::vector<std::byte>>> cells;
            shift_cells_right(node, slot, n, cells);
            // Phase 2: fill in the new cell at the gap
            cells[slot] = {key, row_bytes};
            // Phase 3: rewrite entire cell region to page in one pass
            rewrite_all_cells(node, cells);
        } else {
            // Appending at the end — no shift needed, just write
            node.leaf_write_cell(slot, key, row_bytes);
        }

        node.set_num_keys(n + 1);
        pager_.mark_dirty(root_page_id_);
    }

    // ── Search for a row by primary key ──
    // Returns the Row if found, std::nullopt if not.
    std::optional<Row> search(int64_t key) {
        Page* root = pager_.get_page(root_page_id_);
        BTreeNode node(root);

        if (node.node_type() != NodeType::Leaf) {
            throw std::runtime_error("BTree::search — M1 only supports single-leaf trees");
        }

        uint16_t n = node.num_keys();
        // Binary search for the key
        uint16_t lo = 0, hi = n;
        while (lo < hi) {
            uint16_t mid = lo + (hi - lo) / 2;
            int64_t mid_key = node.leaf_key(mid);
            if (mid_key == key) {
                // Found it — deserialize and return the row
                auto bytes = node.leaf_row_bytes(mid);
                return RowSerializer::deserialize(bytes, num_cols_);
            } else if (mid_key < key) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return std::nullopt; // not found
    }

    // ── Full scan: return all rows in key order ──
    // Used by SeqScan operator later. For M1, reads the single leaf.
    std::vector<Row> scan_all() {
        std::vector<Row> result;
        Page* root = pager_.get_page(root_page_id_);
        BTreeNode node(root);
        uint16_t n = node.num_keys();
        for (uint16_t i = 0; i < n; i++) {
            auto bytes = node.leaf_row_bytes(i);
            result.push_back(RowSerializer::deserialize(bytes, num_cols_));
        }
        return result;
    }

    PageId root_page_id() const { return root_page_id_; }

private:
    Pager&  pager_;
    PageId  root_page_id_;
    size_t  num_cols_;

    // Find the first slot index where leaf_key(slot) >= key.
    // This is where the new key should be inserted (or where it already is).
    uint16_t find_insertion_slot(BTreeNode& node, uint16_t n, int64_t key) {
        uint16_t lo = 0, hi = n;
        while (lo < hi) {
            uint16_t mid = lo + (hi - lo) / 2;
            if (node.leaf_key(mid) < key) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }

    // Shift cells from `start` to `end-1` one slot to the right.
    //
    // The fundamental problem with cell-by-cell rewrites:
    // leaf_cell_offset(i) scans byte-by-byte through cells 0..i-1.
    // If we write a different-sized cell at slot 1, the computed offset
    // for slot 2 changes — so we can't use leaf_cell_offset as a target
    // address while also changing the cells it scans through.
    //
    // Clean solution: build the entire NEW cell region as a flat byte
    // buffer in memory (cells in their final order, including the gap),
    // then memcpy the whole thing back to the page in one shot.
    // This avoids any dependency on intermediate page state.
    void shift_cells_right(BTreeNode& node, uint16_t start, uint16_t end,
                           std::vector<std::pair<int64_t, std::vector<std::byte>>>& out_cells) {
        // Read all existing cells [0..end-1] into out_cells
        out_cells.clear();
        out_cells.reserve(end + 1);
        for (uint16_t i = 0; i < end; i++) {
            out_cells.emplace_back(node.leaf_key(i), node.leaf_row_bytes(i));
        }
        // Insert a gap at `start` — caller will fill this slot
        out_cells.insert(out_cells.begin() + start, {0LL, {}});
        // Note: we do NOT write anything here yet.
        // The caller writes the new cell at `start`, then calls
        // rewrite_all_cells() to flush the full buffer to the page.
    }

    // Rewrite the entire cell region of the page from a flat buffer.
    // Call this AFTER writing the new cell into cells[start].
    void rewrite_all_cells(BTreeNode& node,
                           const std::vector<std::pair<int64_t, std::vector<std::byte>>>& cells) {
        for (uint16_t i = 0; i < (uint16_t)cells.size(); i++) {
            node.leaf_write_cell(i, cells[i].first, cells[i].second);
        }
    }
};
