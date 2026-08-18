#pragma once

// ============================================================================
// catalog.h  —  In-memory schema registry ("table of contents" for the DB).
//
// Owner: Sarthak (frontend).  Disk persistence is a joint task with Priyanshu
// (M2+) — see catalog_format.md for the plan.
//
// The Catalog is the single source of truth for:
//   • what tables exist
//   • what columns each table has (names, types, ordinal positions)
//   • which column is the primary key
//   • what indexes exist on each table (name, column, root page ID)
//   • the B-tree root page ID for each table (set by Priyanshu's storage layer)
//
// Both semantic analysis and the query planner read from this constantly.
// ============================================================================

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/ast.h" // DataType

// ── Column metadata ───────────────────────────────────────────────────────────
struct ColumnInfo {
    std::string name;
    DataType    type;
    int         ordinal;      // 0-based position in the row — determines byte
                              // layout in storage (set at add_table time)
    bool        is_not_null = false;
};

// ── Index metadata ────────────────────────────────────────────────────────────
struct IndexInfo {
    std::string name;         // index name (e.g. "idx_users_email")
    std::string column;       // the single column being indexed (v1: single-col only)
    int         root_page_id = -1; // -1 until Priyanshu's B-tree allocates it
};

// ── Table schema ──────────────────────────────────────────────────────────────
struct TableSchema {
    std::string            name;
    std::vector<ColumnInfo> columns;
    std::string            primary_key;   // column name of the PK
    std::vector<IndexInfo> indexes;
    int                    root_page_id = -1; // -1 until storage allocates it
};

// ── CatalogError — thrown when a schema-level rule is violated ────────────────
class CatalogError : public std::runtime_error {
public:
    explicit CatalogError(const std::string& msg) : std::runtime_error(msg) {}
};

// ── Catalog ───────────────────────────────────────────────────────────────────
class Catalog {
public:
    // Returns pointer to the schema, or nullptr if the table doesn't exist.
    TableSchema*       get_table(const std::string& name);
    const TableSchema* get_table(const std::string& name) const;

    // Add a new table schema.  Throws CatalogError if name already exists.
    void add_table(TableSchema schema);

    // Add an index to an existing table.
    // Throws CatalogError if table doesn't exist, column doesn't exist,
    // or index name is already taken in this table.
    void add_index(const std::string& table_name, IndexInfo index);

    bool table_exists(const std::string& name)  const;
    bool column_exists(const std::string& table_name,
                       const std::string& column_name) const;

    // Convenience: look up a column's DataType.
    // Returns nullopt if table or column doesn't exist.
    std::optional<DataType> column_type(const std::string& table_name,
                                        const std::string& column_name) const;

    // Convenience: find column ordinal (-1 if not found).
    int column_ordinal(const std::string& table_name,
                       const std::string& column_name) const;

    // Iterator over all table schemas (const — callers shouldn't mutate directly).
    const std::unordered_map<std::string, TableSchema>& all_tables() const;

private:
    std::unordered_map<std::string, TableSchema> tables_;
};
