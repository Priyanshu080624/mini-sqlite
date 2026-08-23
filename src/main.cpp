#include "storage/pager.h"
#include "storage/btree.h"
#include "storage/cursor.h"
#include "common/row.h"
#include <iostream>
#include <cassert>

Row make_user(int64_t id, const std::string& name, int64_t age) {
    return { Value(id), Value(name), Value(age) };
}

int main() {
    const char* db_file = "cursor_test.db";
    remove(db_file);

    // ── Setup: create table and insert rows ──
    PageId root_id;
    {
        Pager pager(db_file);
        root_id = BTree::create(pager);
        BTree tree(pager, root_id, 3);
        tree.insert(make_user(1, "Alice", 30));
        tree.insert(make_user(2, "Bob",   25));
        tree.insert(make_user(3, "Carol", 28));
        tree.insert(make_user(4, "Dan",   22));
        tree.insert(make_user(5, "Eve",   35));
    }

    // ── Test 1: SeqScan pattern ──
    std::cout << "=== Test 1: Full table scan (SeqScan pattern) ===\n";
    {
        Pager pager(db_file);
        BTreeCursor cursor(pager, root_id, 3);
        cursor.rewind();
        int count = 0;
        while (!cursor.is_exhausted()) {
            Row r = cursor.current();
            std::cout << "  " << r << "\n";
            count++;
            cursor.next();
        }
        assert(count == 5);
        std::cout << "  Scanned " << count << " rows\n";
    }

    // ── Test 2: Point lookup ──
    std::cout << "\n=== Test 2: Point lookups (seek) ===\n";
    {
        Pager pager(db_file);
        BTreeCursor cursor(pager, root_id, 3);

        bool found = cursor.seek(Key(int64_t(3)));
        assert(found);
        Row r = cursor.current();
        assert(std::get<std::string>(r[1]) == "Carol");
        std::cout << "  seek(3) = " << r << "\n";

        found = cursor.seek(Key(int64_t(1)));
        assert(found);
        r = cursor.current();
        assert(std::get<std::string>(r[1]) == "Alice");
        std::cout << "  seek(1) = " << r << "\n";

        found = cursor.seek(Key(int64_t(99)));
        assert(!found);
        std::cout << "  seek(99) = not found\n";
    }

    // ── Test 3: seek then scan forward ──
    std::cout << "\n=== Test 3: Seek then scan forward ===\n";
    {
        Pager pager(db_file);
        BTreeCursor cursor(pager, root_id, 3);
        cursor.seek(Key(int64_t(2)));
        std::cout << "  Starting from key=2:\n";
        while (!cursor.is_exhausted()) {
            Row r = cursor.current();
            std::cout << "    " << r << "\n";
            cursor.next();
        }
    }

    // ── Test 4: insert via cursor ──
    std::cout << "\n=== Test 4: Insert via cursor ===\n";
    {
        Pager pager(db_file);
        BTreeCursor cursor(pager, root_id, 3);
        cursor.insert(make_user(6, "Frank", 29));
        cursor.rewind();
        int count = 0;
        while (!cursor.is_exhausted()) {
            Row r = cursor.current();
            std::cout << "  " << r << "\n";
            count++;
            cursor.next();
        }
        assert(count == 6);
        std::cout << "  Total rows after insert: " << count << "\n";
    }

    std::cout << "\nAll cursor tests passed.\n";
    return 0;
}