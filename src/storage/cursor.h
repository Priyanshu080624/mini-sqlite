#pragma once
#include "../common/cursor_interface.h"
#include "btree.h"
#include "btree_node.h"
#include "pager.h"

// ─────────────────────────────────────────────────────────────────
//  BTreeCursor
//
//  This is the bridge between Sarthak's executor and your B-tree.
//  It implements the abstract Cursor interface from cursor_interface.h
//  using BTree + Pager underneath.
//
//  Think of it like a bookmark inside the B-tree:
//    - It remembers which page it's on (current_page_id_)
//    - It remembers which slot within that page (current_slot_)
//    - It can move forward (next), jump (seek), read (current),
//      write (insert), or delete (remove)
//
//  For M1: only one leaf page exists, so page-to-page navigation
//  (following next_leaf pointers) is implemented but trivially exits.
//  In M2 when splits create multiple leaf pages, this code already
//  handles it correctly via the next_leaf pointer chain.
// ─────────────────────────────────────────────────────────────────

class BTreeCursor : public Cursor {
public:
    // `pager`      — the I/O layer
    // `root_page`  — which page is this B-tree's root
    // `num_cols`   — number of columns per row (for deserialization)
    BTreeCursor(Pager& pager, PageId root_page, size_t num_cols)
        : pager_(pager)
        , root_page_id_(root_page)
        , num_cols_(num_cols)
        , current_page_id_(root_page)
        , current_slot_(0)
        , exhausted_(true)   // starts exhausted — must call seek() first
    {}

    // ── seek(key) ──
    // Jump to the row with the given primary key.
    // Returns true if found, false if not.
    // After a successful seek(), current() returns that row,
    // and next() advances from there.
    //
    // For M1 (single leaf): just binary search the root page.
    // For M2+: we'd descend internal nodes first, then reach the leaf.
    bool seek(Key key) override {
        int64_t target = std::get<int64_t>(key);

        // Start at the root and find the right leaf page
        // (M1: root IS the leaf, so this is trivial)
        PageId leaf_page = find_leaf(target);
        Page* page = pager_.get_page(leaf_page);
        BTreeNode node(page);

        uint16_t n = node.num_keys();

        // Binary search within the leaf for target key
        uint16_t lo = 0, hi = n;
        while (lo < hi) {
            uint16_t mid = lo + (hi - lo) / 2;
            int64_t mid_key = node.leaf_key(mid);
            if (mid_key == target) {
                // Found — position cursor here
                current_page_id_ = leaf_page;
                current_slot_     = mid;
                exhausted_        = false;
                return true;
            } else if (mid_key < target) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        // Not found — position at where it would be (useful for range scans later)
        current_page_id_ = leaf_page;
        current_slot_    = lo;
        exhausted_       = (lo >= n);
        return false;
    }

    // ── next() ──
    // Advance to the next row in key order.
    // Returns true if there is a next row (current() is now valid).
    // Returns false when exhausted.
    //
    // Two cases:
    //   1. More slots in this leaf page → just increment slot
    //   2. End of this leaf page → follow next_leaf pointer to the next page
    bool next() override {
        if (exhausted_) return false;

        Page* page = pager_.get_page(current_page_id_);
        BTreeNode node(page);

        current_slot_++;

        if (current_slot_ < node.num_keys()) {
            // Still within the same leaf — we're good
            return true;
        }

        // Reached end of this leaf — try following the next_leaf pointer
        PageId next_page = node.next_leaf();
        if (next_page == 0) {
            // No next leaf — we're done
            exhausted_ = true;
            return false;
        }

        // Move to the first slot of the next leaf page
        current_page_id_ = next_page;
        current_slot_    = 0;

        Page* next = pager_.get_page(next_page);
        BTreeNode next_node(next);
        if (next_node.num_keys() == 0) {
            exhausted_ = true;
            return false;
        }

        return true;
    }

    // ── current() ──
    // Return the row at the current cursor position.
    // Only call this after a successful seek() or next().
    Row current() override {
        if (exhausted_) {
            throw std::runtime_error("BTreeCursor::current() called on exhausted cursor");
        }
        Page* page = pager_.get_page(current_page_id_);
        BTreeNode node(page);
        auto bytes = node.leaf_row_bytes(current_slot_);
        return RowSerializer::deserialize(bytes, num_cols_);
    }

    // ── insert(row) ──
    // Insert a new row. Delegates entirely to BTree which handles
    // sorted placement and (in M2) splitting.
    void insert(Row row) override {
        BTree tree(pager_, root_page_id_, num_cols_);
        tree.insert(row);
        // After insert the cursor position is unspecified (as documented
        // in cursor_interface.h) — caller must seek() again if needed.
        exhausted_ = true;
    }

    // ── remove() ──
    // Delete the row at the current cursor position.
    // M1: not yet implemented (arrives in M2 with merge logic).
    void remove() override {
        throw std::runtime_error(
            "BTreeCursor::remove() — not implemented until M2 (needs merge logic)");
    }

    // ── Helper: position cursor at the very first row ──
    // Useful for SeqScan: call rewind() then loop next().
    void rewind() {
        // Find the leftmost leaf (M1: just the root page)
        current_page_id_ = root_page_id_;
        current_slot_    = 0;

        Page* page = pager_.get_page(current_page_id_);
        BTreeNode node(page);
        exhausted_ = (node.num_keys() == 0);
    }

    bool is_exhausted() const { return exhausted_; }

private:
    Pager&   pager_;
    PageId   root_page_id_;
    size_t   num_cols_;

    PageId   current_page_id_;   // which leaf page we're on
    uint16_t current_slot_;      // which cell within that page
    bool     exhausted_;         // true = no more rows to read

    // Find the leaf page that should contain `key`.
    // M1: root is always a leaf, so this just returns root_page_id_.
    // M2+: this will descend internal nodes.
    PageId find_leaf(int64_t /*key*/) {
        // For now always return root — M2 will make this traverse
        // internal nodes by comparing keys and following child pointers.
        return root_page_id_;
    }
};
