#include "frontend/catalog.h"

// ── get_table (mutable + const overloads) ────────────────────────────────────

TableSchema* Catalog::get_table(const std::string& name) {
    auto it = tables_.find(name);
    return (it != tables_.end()) ? &it->second : nullptr;
}

const TableSchema* Catalog::get_table(const std::string& name) const {
    auto it = tables_.find(name);
    return (it != tables_.end()) ? &it->second : nullptr;
}

// ── add_table ─────────────────────────────────────────────────────────────────

void Catalog::add_table(TableSchema schema) {
    if (tables_.count(schema.name)) {
        throw CatalogError("Table '" + schema.name + "' already exists.");
    }

    // Assign ordinals based on the order columns appear in the definition.
    int ord = 0;
    for (auto& col : schema.columns) {
        col.ordinal = ord++;
    }

    std::string name = schema.name;
    tables_.emplace(std::move(name), std::move(schema));
}

// ── add_index ─────────────────────────────────────────────────────────────────

void Catalog::add_index(const std::string& table_name, IndexInfo index) {
    auto it = tables_.find(table_name);
    if (it == tables_.end()) {
        throw CatalogError(
            "Cannot add index: table '" + table_name + "' does not exist.");
    }
    TableSchema& schema = it->second;

    // Check the column being indexed actually exists
    bool found = false;
    for (const auto& col : schema.columns) {
        if (col.name == index.column) { found = true; break; }
    }
    if (!found) {
        throw CatalogError(
            "Cannot add index on '" + index.column +
            "': column does not exist in table '" + table_name + "'.");
    }

    // Check for duplicate index name in this table
    for (const auto& existing : schema.indexes) {
        if (existing.name == index.name) {
            throw CatalogError(
                "Index '" + index.name +
                "' already exists on table '" + table_name + "'.");
        }
    }

    schema.indexes.push_back(std::move(index));
}

// ── Predicates ───────────────────────────────────────────────────────────────

bool Catalog::table_exists(const std::string& name) const {
    return tables_.count(name) > 0;
}

bool Catalog::column_exists(const std::string& table_name,
                             const std::string& column_name) const {
    const auto* schema = get_table(table_name);
    if (!schema) return false;
    for (const auto& col : schema->columns) {
        if (col.name == column_name) return true;
    }
    return false;
}

// ── Convenience helpers ───────────────────────────────────────────────────────

std::optional<DataType> Catalog::column_type(const std::string& table_name,
                                               const std::string& column_name) const {
    const auto* schema = get_table(table_name);
    if (!schema) return std::nullopt;
    for (const auto& col : schema->columns) {
        if (col.name == column_name) return col.type;
    }
    return std::nullopt;
}

int Catalog::column_ordinal(const std::string& table_name,
                              const std::string& column_name) const {
    const auto* schema = get_table(table_name);
    if (!schema) return -1;
    for (const auto& col : schema->columns) {
        if (col.name == column_name) return col.ordinal;
    }
    return -1;
}

const std::unordered_map<std::string, TableSchema>& Catalog::all_tables() const {
    return tables_;
}
