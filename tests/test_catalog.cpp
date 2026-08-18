// ============================================================================
// test_catalog.cpp — Tests for the in-memory Catalog
// ============================================================================

#include <cassert>
#include <iostream>
#include <stdexcept>
#include "common/ast.h"
#include "frontend/catalog.h"

static int passed = 0, failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << msg << "] line " << __LINE__ << "\n"; \
            failed++; \
        } else { \
            passed++; \
        } \
    } while(0)

static TableSchema makeUsersSchema() {
    TableSchema s;
    s.name        = "users";
    s.primary_key = "id";
    s.columns     = {
        {"id",    DataType::INTEGER, 0, true},
        {"name",  DataType::TEXT,    1, true},
        {"score", DataType::REAL,    2, false},
    };
    return s;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void testAddAndGet() {
    Catalog cat;
    cat.add_table(makeUsersSchema());

    const TableSchema* s = cat.get_table("users");
    CHECK(s != nullptr,            "get_table returns non-null");
    CHECK(s->name == "users",      "table name correct");
    CHECK(s->primary_key == "id",  "primary key correct");
    CHECK(s->columns.size() == 3,  "3 columns");
}

static void testTableExists() {
    Catalog cat;
    CHECK(!cat.table_exists("users"), "table_exists: false before add");
    cat.add_table(makeUsersSchema());
    CHECK(cat.table_exists("users"),  "table_exists: true after add");
}

static void testDuplicateTable() {
    Catalog cat;
    cat.add_table(makeUsersSchema());
    bool threw = false;
    try { cat.add_table(makeUsersSchema()); }
    catch (const CatalogError&) { threw = true; }
    CHECK(threw, "duplicate add_table throws CatalogError");
}

static void testColumnExists() {
    Catalog cat;
    cat.add_table(makeUsersSchema());
    CHECK( cat.column_exists("users", "id"),       "column_exists: id");
    CHECK( cat.column_exists("users", "name"),     "column_exists: name");
    CHECK( cat.column_exists("users", "score"),    "column_exists: score");
    CHECK(!cat.column_exists("users", "email"),    "column_exists: email not found");
    CHECK(!cat.column_exists("orders", "id"),      "column_exists: unknown table");
}

static void testOrdinals() {
    Catalog cat;
    cat.add_table(makeUsersSchema());
    // ordinals are set by add_table — should be 0,1,2 in order
    CHECK(cat.column_ordinal("users", "id")    == 0, "ordinal id=0");
    CHECK(cat.column_ordinal("users", "name")  == 1, "ordinal name=1");
    CHECK(cat.column_ordinal("users", "score") == 2, "ordinal score=2");
    CHECK(cat.column_ordinal("users", "nope")  == -1, "ordinal missing=-1");
}

static void testColumnType() {
    Catalog cat;
    cat.add_table(makeUsersSchema());
    auto t0 = cat.column_type("users", "id");
    CHECK(t0.has_value() && *t0 == DataType::INTEGER, "type id=INTEGER");
    auto t1 = cat.column_type("users", "name");
    CHECK(t1.has_value() && *t1 == DataType::TEXT,    "type name=TEXT");
    auto t2 = cat.column_type("users", "score");
    CHECK(t2.has_value() && *t2 == DataType::REAL,    "type score=REAL");
    auto t3 = cat.column_type("users", "nope");
    CHECK(!t3.has_value(), "type missing=nullopt");
}

static void testAddIndex() {
    Catalog cat;
    cat.add_table(makeUsersSchema());

    IndexInfo idx{"idx_users_name", "name", -1};
    cat.add_index("users", idx);

    const TableSchema* s = cat.get_table("users");
    CHECK(s->indexes.size() == 1,                  "1 index after add");
    CHECK(s->indexes[0].name == "idx_users_name",  "index name");
    CHECK(s->indexes[0].column == "name",          "index column");
}

static void testAddIndexErrors() {
    Catalog cat;
    cat.add_table(makeUsersSchema());

    // Missing table
    bool threw = false;
    try { cat.add_index("orders", {"idx", "id", -1}); }
    catch (const CatalogError&) { threw = true; }
    CHECK(threw, "add_index on missing table throws");

    // Missing column
    threw = false;
    try { cat.add_index("users", {"idx", "email", -1}); }
    catch (const CatalogError&) { threw = true; }
    CHECK(threw, "add_index on missing column throws");

    // Duplicate index name
    cat.add_index("users", {"idx_name", "name", -1});
    threw = false;
    try { cat.add_index("users", {"idx_name", "score", -1}); }
    catch (const CatalogError&) { threw = true; }
    CHECK(threw, "duplicate index name throws");
}

static void testMissingGet() {
    Catalog cat;
    CHECK(cat.get_table("nope") == nullptr, "get_table on missing = nullptr");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    testAddAndGet();
    testTableExists();
    testDuplicateTable();
    testColumnExists();
    testOrdinals();
    testColumnType();
    testAddIndex();
    testAddIndexErrors();
    testMissingGet();

    std::cout << "Catalog: " << passed << " passed, " << failed << " failed.\n";
    return failed > 0 ? 1 : 0;
}
