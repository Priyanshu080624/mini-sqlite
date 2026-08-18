#include "common/row.h"
#include "common/cursor_interface.h"
#include <iostream>
#include <cassert>

// ── A fake cursor just to prove the interface compiles ──
// In real code this will be BTreeCursor backed by the pager.
// Here we just return two hardcoded rows to prove the contract works.
class FakeCursor : public Cursor {
public:
    // Two fake rows simulating: users(id, name, age)
    //   Row 0: (1, "Alice", 30)
    //   Row 1: (2, "Bob",   25)
    std::vector<Row> rows = {
        { Value(int64_t(1)), Value(std::string("Alice")), Value(int64_t(30)) },
        { Value(int64_t(2)), Value(std::string("Bob")),   Value(int64_t(25)) },
    };
    int pos = -1; // -1 = before first row (seek not called yet)

    bool next() override {
        pos++;
        return pos < (int)rows.size();
    }

    Row current() override {
        return rows[pos];
    }

    bool seek(Key key) override {
        // Find the row whose first column (primary key) matches
        for (int i = 0; i < (int)rows.size(); i++) {
            if (rows[i][0] == key) {
                pos = i;
                return true;
            }
        }
        return false; // key not found
    }

    void insert(Row row) override {
        rows.push_back(row);
    }

    void remove() override {
        rows.erase(rows.begin() + pos);
        pos--;
    }
};

int main() {
    std::cout << "=== Testing Row / Value types ===\n";

    // Build a row: (1, "Alice", 30)
    Row row1 = {
        Value(int64_t(1)),
        Value(std::string("Alice")),
        Value(int64_t(30))
    };

    // Build a row with a NULL value
    Row row2 = {
        Value(int64_t(2)),
        Value(std::monostate{}),  // NULL name
        Value(double(9.99))
    };

    std::cout << "Row 1: " << row1 << "\n";  // should print: (1, Alice, 30)
    std::cout << "Row 2: " << row2 << "\n";  // should print: (2, NULL, 9.99)
    std::cout << "Primary key of row1: " << primary_key(row1) << "\n\n";

    std::cout << "=== Testing Cursor interface ===\n";

    FakeCursor cursor;

    // Test: seek to key=2 (Bob)
    bool found = cursor.seek(Value(int64_t(2)));
    assert(found && "seek(2) should find Bob");
    std::cout << "seek(2) found: " << cursor.current() << "\n";

    // Test: seek to key=1 (Alice)
    found = cursor.seek(Value(int64_t(1)));
    assert(found && "seek(1) should find Alice");
    std::cout << "seek(1) found: " << cursor.current() << "\n";

    // Test: scan all rows with next()
    std::cout << "\nFull table scan:\n";
    cursor.seek(Value(int64_t(1))); // go back to start
    // Reset to before-first position for a clean scan
    FakeCursor cursor2;
    while (cursor2.next()) {
        std::cout << "  " << cursor2.current() << "\n";
    }

    // Test: seek to a key that doesn't exist
    found = cursor.seek(Value(int64_t(99)));
    assert(!found && "seek(99) should return false");
    std::cout << "\nseek(99): correctly returned false (not found)\n";

    std::cout << "\n✓ All tests passed. Files are ready to share with Sarthak.\n";
    return 0;
}
