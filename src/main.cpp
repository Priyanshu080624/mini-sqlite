#include "storage/pager.h"
#include "storage/btree.h"
#include "common/row.h"
#include <iostream>
#include <cassert>

Row make_user(int64_t id, const std::string& name, int64_t age) {
    return { Value(id), Value(name), Value(age) };
}

int main() {
    remove("m2_test.db");

    // ── Test 1: insert 50 rows (forces 2 leaf splits) ──
    std::cout << "=== Test 1: Insert 50 rows (forces splits) ===\n";
    PageId root_id;
    {
        Pager pager("m2_test.db");
        root_id = BTree::create(pager);
        BTree tree(pager, root_id, 3);

        for (int64_t i = 1; i <= 50; i++)
            tree.insert(make_user(i, "user" + std::to_string(i), i + 20));

        // IMPORTANT: root_id changes when splits create a new root page
        // Always save tree.root_page_id(), not the original create() id
        root_id = tree.root_page_id();

        std::cout << "Inserted 50 rows. Final root page = " << root_id << "\n";
        auto rows = tree.scan_all();
        assert(rows.size() == 50);
        for (size_t i = 0; i < rows.size(); i++)
            assert(std::get<int64_t>(rows[i][0]) == (int64_t)(i+1));
        std::cout << "In-memory scan: " << rows.size() << " rows, sorted correctly\n";
    }

    // ── Test 2: reopen and verify all 50 rows survive ──
    std::cout << "\n=== Test 2: Reopen and verify persistence ===\n";
    {
        Pager pager("m2_test.db");
        BTree tree(pager, root_id, 3);

        auto rows = tree.scan_all();
        std::cout << "After restart: " << rows.size() << " rows\n";
        assert(rows.size() == 50);

        auto r1  = tree.search(1);
        auto r25 = tree.search(25);
        auto r50 = tree.search(50);
        assert(r1.has_value()  && std::get<int64_t>((*r1)[0])  == 1);
        assert(r25.has_value() && std::get<int64_t>((*r25)[0]) == 25);
        assert(r50.has_value() && std::get<int64_t>((*r50)[0]) == 50);
        std::cout << "search(1)  = " << *r1  << "\n";
        std::cout << "search(25) = " << *r25 << "\n";
        std::cout << "search(50) = " << *r50 << "\n";
    }

    // ── Test 3: reverse-order inserts across split boundary ──
    std::cout << "\n=== Test 3: Reverse-order inserts ===\n";
    {
        remove("m2_test2.db");
        Pager pager("m2_test2.db");
        PageId rid = BTree::create(pager);
        BTree tree(pager, rid, 3);

        for (int64_t i = 50; i >= 1; i--)
            tree.insert(make_user(i, "user" + std::to_string(i), i));

        rid = tree.root_page_id();
        auto rows = tree.scan_all();
        assert(rows.size() == 50);
        for (size_t i = 0; i < rows.size(); i++)
            assert(std::get<int64_t>(rows[i][0]) == (int64_t)(i+1));
        std::cout << "50 reverse-order inserts: all sorted correctly\n";
    }

    std::cout << "\nAll M2 split tests passed.\n";
    return 0;
}
