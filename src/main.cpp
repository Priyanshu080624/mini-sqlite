#include "storage/pager.h"
#include "storage/btree.h"
#include "storage/wal.h"
#include "storage/txn_manager.h"
#include "storage/recovery.h"
#include "storage/btree_node.h"
#include "common/row.h"
#include <iostream>
#include <cassert>
#include <fstream>

const char* DB       = "m4_test.db";
const char* WAL_FILE = "m4_test.wal";

// Helper: snapshot all current pages before a write, then log diffs after
// This is the correct WAL integration pattern:
//   1. snapshot before-state
//   2. do the write
//   3. log before+after for every page that changed
void wal_protected_insert(Pager& pager, WAL& wal, uint64_t txn_id,
                          BTree& tree, Row row) {
    uint32_t page_count_before = pager.page_count();

    // Snapshot all existing pages before insert
    std::unordered_map<PageId, std::array<std::byte, PAGE_SIZE>> before_images;
    for (uint32_t pid = 0; pid < page_count_before; pid++) {
        before_images[pid] = pager.get_page(pid)->data;
    }

    // Do the insert
    tree.insert(row);

    // Log every page that changed (or was newly allocated)
    for (uint32_t pid = 0; pid < pager.page_count(); pid++) {
        Page* p = pager.get_page(pid);
        if (pid < page_count_before) {
            // Existing page — check if it changed
            if (p->data != before_images[pid]) {
                wal.write_page(txn_id, pid, before_images[pid], p->data);
            }
        } else {
            // Newly allocated page — before-image is all zeros
            std::array<std::byte, PAGE_SIZE> zeroes{};
            wal.write_page(txn_id, pid, zeroes, p->data);
        }
    }
}

void session1_commit() {
    std::cout << "=== Session 1: committed transaction ===\n";
    remove(DB); remove(WAL_FILE); remove("root.txt");

    Pager pager(DB);
    WAL wal(WAL_FILE);

    PageId root = BTree::create(pager);
    BTree tree(pager, root, 3);

    uint64_t tid = 1;
    wal.write_begin(tid);

    // Snapshot before, insert, log changes
    wal_protected_insert(pager, wal, tid, tree,
        {Value(int64_t(1)), Value(std::string("Alice")), Value(int64_t(30))});

    root = tree.root_page_id();
    wal.write_commit(tid);
    pager.flush_all();

    std::ofstream f("root.txt");
    f << root;
    std::cout << "  Alice (pk=1) committed. Root=" << root << "\n";
}

void session2_crash() {
    std::cout << "\n=== Session 2: crash mid-transaction ===\n";
    PageId root; { std::ifstream f("root.txt"); f >> root; }

    Pager pager(DB);
    WAL wal(WAL_FILE);

    BTree tree(pager, root, 3);

    uint64_t tid = 2;
    wal.write_begin(tid);

    // Snapshot before, insert Bob, log page changes — but NO commit
    wal_protected_insert(pager, wal, tid, tree,
        {Value(int64_t(2)), Value(std::string("Bob")), Value(int64_t(25))});

    // Pager destructor flushes Bob's dirty page to disk — worst-case crash
    std::cout << "  WAL has Bob's PAGE_WRITE but no COMMIT.\n";
    std::cout << "  CRASH (pager destructor flushes Bob to disk)...\n";
}

void session3_recovery() {
    std::cout << "\n=== Session 3: recovery on restart ===\n";
    Pager pager(DB);
    WAL wal(WAL_FILE);
    auto [redone, undone] = Recovery::run(pager, wal, true);
    std::cout << "  Redone=" << redone << " Undone=" << undone << "\n";
}

void session4_verify() {
    std::cout << "\n=== Session 4: verify state ===\n";
    PageId root; { std::ifstream f("root.txt"); f >> root; }

    Pager pager(DB);
    BTree tree(pager, root, 3);

    auto rows = tree.scan_all();
    std::cout << "  Rows: " << rows.size() << "\n";
    for (const auto& r : rows) std::cout << "  " << r << "\n";

    auto alice = tree.search(1);
    auto bob   = tree.search(2);

    assert(alice.has_value() && "Alice should be present");
    assert(!bob.has_value()  && "Bob should be gone");

    std::cout << "\n  Alice (committed): " << *alice << " [present]\n";
    std::cout << "  Bob (crashed):     NOT FOUND [correctly absent]\n";
    std::cout << "\nM4 PASSED.\n";
}

int main() {
    session1_commit();
    session2_crash();
    session3_recovery();
    session4_verify();
    return 0;
}