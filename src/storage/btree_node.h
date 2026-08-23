#pragma once
#include "page.h"
#include "../common/row.h"
#include <cstring>
#include <cassert>
#include <vector>

// ─────────────────────────────────────────────────────────────────
//  What is this file?
//
//  A Page is just 4096 raw bytes — it has no meaning on its own.
//  BTreeNode wraps a Page and gives those bytes STRUCTURE:
//
//  "bytes 0-0 are the node type"
//  "bytes 2-3 are the number of keys"
//  "bytes 16+ are the actual row data"
//
//  Think of BTreeNode like a "lens" that lets you read/write
//  a Page as if it were a structured B-tree node.
//  The Page still owns the bytes; BTreeNode just interprets them.
// ─────────────────────────────────────────────────────────────────

// ── Header byte offsets (do not change — these are baked into every .db file) ──
static constexpr size_t OFFSET_NODE_TYPE   = 0;   // 1 byte:  0=internal, 1=leaf
static constexpr size_t OFFSET_IS_ROOT     = 1;   // 1 byte:  0 or 1
static constexpr size_t OFFSET_NUM_KEYS    = 2;   // 2 bytes: uint16_t
static constexpr size_t OFFSET_PARENT_PAGE = 4;   // 4 bytes: uint32_t
static constexpr size_t OFFSET_NEXT_LEAF   = 8;   // 4 bytes: uint32_t (leaf→leaf chain)
// bytes 12-15: reserved/padding
static constexpr size_t HEADER_SIZE        = 16;  // cells start here

// ── Cell sizes ──
// Leaf cell  = [key: 8 bytes][row_size: 2 bytes][row_bytes: row_size bytes]
// Internal cell = [key: 8 bytes][child_page: 4 bytes]
static constexpr size_t LEAF_CELL_KEY_SIZE      = 8;
static constexpr size_t LEAF_CELL_SIZE_FIELD    = 2;  // stores the row payload size
static constexpr size_t INTERNAL_CELL_KEY_SIZE  = 8;
static constexpr size_t INTERNAL_CELL_CHILD_SIZE = 4;
static constexpr size_t INTERNAL_CELL_SIZE = INTERNAL_CELL_KEY_SIZE + INTERNAL_CELL_CHILD_SIZE;

// Max rows per leaf page.
// We calculate this: (4096 - 16 header) / (8 key + 2 size + ~64 avg row payload)
// For now we use a conservative fixed max of 32 — we'll make this dynamic later.
static constexpr uint16_t LEAF_MAX_CELLS = 32;

enum class NodeType : uint8_t {
    Internal = 0,
    Leaf     = 1,
};

// ─────────────────────────────────────────────────────────────────
//  BTreeNode
//
//  How to use it:
//    Page* p = pager.get_page(some_id);
//    BTreeNode node(p);           // wrap the page
//    node.set_node_type(NodeType::Leaf);
//    node.set_is_root(true);
//    node.set_num_keys(0);
//    // now write cells into it
// ─────────────────────────────────────────────────────────────────
class BTreeNode {
public:
    // Wrap an existing page. Does NOT modify any bytes — just gives
    // you the lens to read/write them.
    explicit BTreeNode(Page* page) : page_(page) {
        assert(page_ != nullptr);
    }

    // ── Header getters ──

    NodeType node_type() const {
        return static_cast<NodeType>(read_byte(OFFSET_NODE_TYPE));
    }
    bool is_root() const {
        return read_byte(OFFSET_IS_ROOT) != 0;
    }
    uint16_t num_keys() const {
        return read_u16(OFFSET_NUM_KEYS);
    }
    PageId parent_page() const {
        return read_u32(OFFSET_PARENT_PAGE);
    }
    PageId next_leaf() const {
        return read_u32(OFFSET_NEXT_LEAF);
    }

    // ── Header setters ──

    void set_node_type(NodeType t) {
        write_byte(OFFSET_NODE_TYPE, static_cast<uint8_t>(t));
    }
    void set_is_root(bool r) {
        write_byte(OFFSET_IS_ROOT, r ? 1 : 0);
    }
    void set_num_keys(uint16_t n) {
        write_u16(OFFSET_NUM_KEYS, n);
    }
    void set_parent_page(PageId id) {
        write_u32(OFFSET_PARENT_PAGE, id);
    }
    void set_next_leaf(PageId id) {
        write_u32(OFFSET_NEXT_LEAF, id);
    }

    // ── Initialize a brand new empty node ──
    // Call this right after allocating a fresh page — zeroes the header.
    void init_leaf(bool is_root_node = false) {
        set_node_type(NodeType::Leaf);
        set_is_root(is_root_node);
        set_num_keys(0);
        set_parent_page(0);
        set_next_leaf(0);
    }

    void init_internal(bool is_root_node = false) {
        set_node_type(NodeType::Internal);
        set_is_root(is_root_node);
        set_num_keys(0);
        set_parent_page(0);
        set_next_leaf(0);
    }

    // ── Leaf cell access ──
    //
    // A leaf cell lives at a byte offset inside the page.
    // Cells are packed together from byte 16 onwards:
    //
    //   [cell 0][cell 1][cell 2]...
    //
    // Each cell: [int64 key][uint16 row_size][row_bytes × row_size]
    //
    // Because row sizes can vary, we have to scan from the start
    // to find cell N (or pre-compute offsets — we keep it simple for now).

    // Returns the byte offset of leaf cell at index `slot`.
    // This has to scan through all prior cells because rows can be variable-length.
    size_t leaf_cell_offset(uint16_t slot) const {
        size_t offset = HEADER_SIZE;
        for (uint16_t i = 0; i < slot; i++) {
            // skip over key (8) + size field (2) + payload
            int64_t key;
            std::memcpy(&key, page_->data.data() + offset, 8);
            uint16_t row_size;
            std::memcpy(&row_size, page_->data.data() + offset + 8, 2);
            offset += 8 + 2 + row_size;
        }
        return offset;
    }

    // Read the key at leaf slot `slot`.
    int64_t leaf_key(uint16_t slot) const {
        size_t off = leaf_cell_offset(slot);
        int64_t key;
        std::memcpy(&key, page_->data.data() + off, 8);
        return key;
    }

    // Read the serialized row bytes at leaf slot `slot`.
    std::vector<std::byte> leaf_row_bytes(uint16_t slot) const {
        size_t off = leaf_cell_offset(slot) + 8; // skip key
        uint16_t row_size;
        std::memcpy(&row_size, page_->data.data() + off, 2);
        off += 2;
        return std::vector<std::byte>(
            page_->data.data() + off,
            page_->data.data() + off + row_size
        );
    }

    // Write a key + serialized row bytes into leaf slot `slot`.
    // IMPORTANT: call this in sorted key order, and only if the page isn't full.
    void leaf_write_cell(uint16_t slot, int64_t key, const std::vector<std::byte>& row_bytes) {
        size_t off = leaf_cell_offset(slot);
        // write key
        std::memcpy(page_->data.data() + off, &key, 8);
        // write row size
        uint16_t sz = static_cast<uint16_t>(row_bytes.size());
        std::memcpy(page_->data.data() + off + 8, &sz, 2);
        // write row payload
        std::memcpy(page_->data.data() + off + 10, row_bytes.data(), sz);
    }

    // Is this leaf page full? (simple threshold for now)
    bool leaf_is_full() const {
        return num_keys() >= LEAF_MAX_CELLS;
    }

    // ── Internal cell access ──
    //
    // Internal node layout:
    //   [key0: 8][child0: 4][key1: 8][child1: 4]...[keyN: 8][rightmost_child: 4]
    //
    // The rightmost child (the one for keys > all stored keys) is stored
    // as a trailing entry after all key-child pairs. We'll implement
    // that fully in M2 when we add splitting. For now just the basics.

    size_t internal_cell_offset(uint16_t slot) const {
        return HEADER_SIZE + static_cast<size_t>(slot) * INTERNAL_CELL_SIZE;
    }

    int64_t internal_key(uint16_t slot) const {
        int64_t key;
        std::memcpy(&key, page_->data.data() + internal_cell_offset(slot), 8);
        return key;
    }

    PageId internal_child(uint16_t slot) const {
        return read_u32(internal_cell_offset(slot) + 8);
    }

    // The rightmost child pointer (child for keys > all stored keys)
    // stored right after the last key-child pair
    PageId internal_rightmost_child() const {
        return read_u32(internal_cell_offset(num_keys()));
    }

    void internal_write_cell(uint16_t slot, int64_t key, PageId child_page) {
        size_t off = internal_cell_offset(slot);
        std::memcpy(page_->data.data() + off, &key, 8);
        write_u32(off + 8, child_page);
    }

    void internal_set_rightmost_child(PageId child_page) {
        write_u32(internal_cell_offset(num_keys()), child_page);
    }

    // Expose the underlying page id (useful for logging/debugging)
    PageId page_id() const { return page_->id; }

private:
    Page* page_;

    // ── Low-level byte read/write helpers ──
    // We use memcpy instead of reinterpret_cast to avoid undefined behaviour
    // from strict-aliasing rules. It compiles to the same thing.

    uint8_t read_byte(size_t offset) const {
        return static_cast<uint8_t>(page_->data[offset]);
    }
    void write_byte(size_t offset, uint8_t val) {
        page_->data[offset] = static_cast<std::byte>(val);
    }

    uint16_t read_u16(size_t offset) const {
        uint16_t val;
        std::memcpy(&val, page_->data.data() + offset, 2);
        return val;
    }
    void write_u16(size_t offset, uint16_t val) {
        std::memcpy(page_->data.data() + offset, &val, 2);
    }

    uint32_t read_u32(size_t offset) const {
        uint32_t val;
        std::memcpy(&val, page_->data.data() + offset, 4);
        return val;
    }
    void write_u32(size_t offset, uint32_t val) {
        std::memcpy(page_->data.data() + offset, &val, 4);
    }
};
