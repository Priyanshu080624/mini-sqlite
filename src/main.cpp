// ═══════════════════════════════════════════════════════════════
//  minidb — entry point
//
//  Startup sequence:
//    1. Open database file + WAL
//    2. Run crash recovery (replay WAL if needed)
//    3. Load catalog (which tables exist, their root page IDs)
//    4. Start REPL — accept commands, execute, print results
//
//  Supported commands:
//    CREATE TABLE name (col type, ...)
//    INSERT INTO name VALUES (v1, v2, ...)
//    SELECT * FROM name
//    SELECT * FROM name WHERE col = value
//    SELECT * FROM name WHERE col > value
//    SELECT * FROM name ORDER BY col
//    BEGIN / COMMIT / ROLLBACK
//    .tables        — list all tables
//    .quit / .exit  — exit
// ═══════════════════════════════════════════════════════════════

#include "storage/pager.h"
#include "storage/btree.h"
#include "storage/wal.h"
#include "storage/txn_manager.h"
#include "storage/recovery.h"
#include "exec/seq_scan.h"
#include "exec/filter.h"
#include "exec/sort.h"
#include "exec/aggregate.h"
#include "common/row.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <optional>
#include <fstream>

// ── Catalog: track tables across restarts ──
struct ColumnDef {
    std::string name;
    std::string type; // "INTEGER", "TEXT", "REAL"
};

struct TableSchema {
    std::string          name;
    std::vector<ColumnDef> columns;
    PageId               root_page;
};

// Catalog stored in a plain text file (simple, robust)
std::map<std::string, TableSchema> catalog;
const char* CATALOG_FILE = "catalog.txt";

void save_catalog() {
    std::ofstream f(CATALOG_FILE);
    for (auto& [name, schema] : catalog) {
        f << schema.name << " " << schema.root_page
          << " " << schema.columns.size() << "\n";
        for (auto& col : schema.columns)
            f << col.name << " " << col.type << "\n";
    }
}

void load_catalog() {
    std::ifstream f(CATALOG_FILE);
    if (!f) return;
    while (f) {
        std::string tname;
        PageId root;
        size_t ncols;
        if (!(f >> tname >> root >> ncols)) break;
        TableSchema s;
        s.name = tname;
        s.root_page = root;
        for (size_t i = 0; i < ncols; i++) {
            ColumnDef c;
            f >> c.name >> c.type;
            s.columns.push_back(c);
        }
        catalog[tname] = s;
    }
}

// ── Simple string utilities ──
std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string p;
    while (std::getline(ss, p, delim)) parts.push_back(trim(p));
    return parts;
}

// ── Parse a literal value into a Value ──
Value parse_value(const std::string& s, const std::string& type) {
    if (s == "NULL") return Value(std::monostate{});
    if (type == "INTEGER") return Value(int64_t(std::stoll(s)));
    if (type == "REAL")    return Value(double(std::stod(s)));
    // TEXT: strip surrounding quotes if present
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'')
        return Value(s.substr(1, s.size() - 2));
    return Value(s);
}

// ── Print a row ──
void print_row(const Row& row, const TableSchema& schema) {
    for (size_t i = 0; i < row.size(); i++) {
        if (i > 0) std::cout << " | ";
        std::visit([](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) std::cout << "NULL";
            else std::cout << v;
        }, row[i]);
    }
    std::cout << "\n";
}

void print_header(const TableSchema& schema) {
    for (size_t i = 0; i < schema.columns.size(); i++) {
        if (i > 0) std::cout << " | ";
        std::cout << schema.columns[i].name;
    }
    std::cout << "\n";
    for (size_t i = 0; i < schema.columns.size(); i++) {
        if (i > 0) std::cout << "-+-";
        std::cout << std::string(schema.columns[i].name.size(), '-');
    }
    std::cout << "\n";
}

// ══════════════════════════════════════════════════════════════
//  Command handlers
// ══════════════════════════════════════════════════════════════

bool handle_create_table(const std::string& cmd, Pager& pager) {
    // CREATE TABLE name (col1 TYPE, col2 TYPE, ...)
    size_t lp = cmd.find('('), rp = cmd.rfind(')');
    if (lp == std::string::npos || rp == std::string::npos) {
        std::cout << "Syntax: CREATE TABLE name (col type, ...)\n";
        return false;
    }
    std::string header = trim(cmd.substr(13, lp - 13)); // after "CREATE TABLE "
    std::string cols_str = cmd.substr(lp + 1, rp - lp - 1);

    TableSchema schema;
    schema.name = header;

    auto col_defs = split(cols_str, ',');
    for (auto& def : col_defs) {
        auto parts = split(def, ' ');
        if (parts.size() < 2) continue;
        ColumnDef c;
        c.name = parts[0];
        c.type = to_upper(parts[1]);
        schema.columns.push_back(c);
    }

    if (catalog.count(schema.name)) {
        std::cout << "Table '" << schema.name << "' already exists.\n";
        return false;
    }

    PageId root = BTree::create(pager);
    BTree tree(pager, root, schema.columns.size());
    schema.root_page = tree.root_page_id();
    catalog[schema.name] = schema;
    save_catalog();
    std::cout << "Table '" << schema.name << "' created.\n";
    return true;
}

bool handle_insert(const std::string& cmd, Pager& pager) {
    // INSERT INTO name VALUES (v1, v2, ...)
    size_t into_pos = cmd.find("INTO ");
    size_t vals_pos = cmd.find("VALUES");
    if (into_pos == std::string::npos || vals_pos == std::string::npos) {
        std::cout << "Syntax: INSERT INTO name VALUES (v1, v2, ...)\n";
        return false;
    }
    std::string tname = trim(cmd.substr(into_pos + 5, vals_pos - into_pos - 5));
    if (!catalog.count(tname)) {
        std::cout << "Table '" << tname << "' does not exist.\n";
        return false;
    }

    size_t lp = cmd.find('(', vals_pos);
    size_t rp = cmd.rfind(')');
    std::string vals_str = cmd.substr(lp + 1, rp - lp - 1);
    auto val_parts = split(vals_str, ',');

    auto& schema = catalog[tname];
    if (val_parts.size() != schema.columns.size()) {
        std::cout << "Expected " << schema.columns.size()
                  << " values, got " << val_parts.size() << "\n";
        return false;
    }

    Row row;
    for (size_t i = 0; i < val_parts.size(); i++) {
        // Strip quotes
        std::string v = trim(val_parts[i]);
        if (v.size() >= 2 && v.front() == '\'' && v.back() == '\'')
            v = v.substr(1, v.size() - 2);
        row.push_back(parse_value(v, schema.columns[i].type));
    }

    BTree tree(pager, schema.root_page, schema.columns.size());
    tree.insert(row);
    schema.root_page = tree.root_page_id();
    save_catalog();
    std::cout << "1 row inserted.\n";
    return true;
}

bool handle_select(const std::string& cmd, Pager& pager) {
    // SELECT * FROM name [WHERE col op value] [ORDER BY col [DESC]]
    size_t from_pos = cmd.find("FROM ");
    if (from_pos == std::string::npos) {
        std::cout << "Syntax: SELECT * FROM name [WHERE ...] [ORDER BY ...]\n";
        return false;
    }

    // Parse table name
    std::string rest = trim(cmd.substr(from_pos + 5));
    std::string tname;
    size_t space = rest.find(' ');
    if (space == std::string::npos) {
        tname = rest;
        rest = "";
    } else {
        tname = rest.substr(0, space);
        rest = trim(rest.substr(space));
    }

    if (!catalog.count(tname)) {
        std::cout << "Table '" << tname << "' does not exist.\n";
        return false;
    }
    auto& schema = catalog[tname];

    // Parse optional WHERE
    std::string where_col, where_op, where_val;
    bool has_where = false;
    size_t where_pos = rest.find("WHERE ");
    if (where_pos != std::string::npos) {
        has_where = true;
        std::string where_str = trim(rest.substr(where_pos + 6));
        // Take everything before ORDER BY if present
        size_t ob_pos = where_str.find("ORDER ");
        if (ob_pos != std::string::npos) where_str = trim(where_str.substr(0, ob_pos));
        // Parse: col op value
        std::istringstream ws(where_str);
        ws >> where_col >> where_op >> where_val;
        if (where_val.size() >= 2 && where_val.front() == '\'' && where_val.back() == '\'')
            where_val = where_val.substr(1, where_val.size() - 2);
    }

    // Parse optional ORDER BY
    std::string order_col;
    bool order_desc = false;
    bool has_order = false;
    size_t ob_pos = rest.find("ORDER BY ");
    if (ob_pos != std::string::npos) {
        has_order = true;
        std::string ob_str = trim(rest.substr(ob_pos + 9));
        std::istringstream obs(ob_str);
        obs >> order_col;
        std::string dir;
        if (obs >> dir) order_desc = (to_upper(dir) == "DESC");
    }

    // Find column index by name
    auto col_idx = [&](const std::string& name) -> size_t {
        for (size_t i = 0; i < schema.columns.size(); i++)
            if (schema.columns[i].name == name) return i;
        return 0;
    };

    // Build operator tree
    auto scan = std::make_unique<SeqScan>(pager, schema.root_page, schema.columns.size());

    std::unique_ptr<Operator> root_op;

    if (has_where) {
        size_t wcol = col_idx(where_col);
        std::string wtype = schema.columns[wcol].type;
        Value wval = parse_value(where_val, wtype);
        std::string wop = where_op;

        auto pred = [wcol, wval, wop](const Row& r) -> bool {
            const Value& rv = r[wcol];
            // Compare as doubles for simplicity
            auto to_d = [](const Value& v) -> double {
                return std::visit([](const auto& x) -> double {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, int64_t>) return (double)x;
                    if constexpr (std::is_same_v<T, double>)  return x;
                    return 0.0;
                }, v);
            };
            // String equality
            if (std::holds_alternative<std::string>(rv) &&
                std::holds_alternative<std::string>(wval)) {
                auto& a = std::get<std::string>(rv);
                auto& b = std::get<std::string>(wval);
                if (wop == "=")  return a == b;
                if (wop == "!=") return a != b;
                return false;
            }
            double a = to_d(rv), b = to_d(wval);
            if (wop == "=")  return a == b;
            if (wop == "!=") return a != b;
            if (wop == ">")  return a >  b;
            if (wop == ">=") return a >= b;
            if (wop == "<")  return a <  b;
            if (wop == "<=") return a <= b;
            return false;
        };

        root_op = std::make_unique<Filter>(std::move(scan), pred);
    } else {
        root_op = std::move(scan);
    }

    if (has_order) {
        size_t ocol = col_idx(order_col);
        auto sort = std::make_unique<Sort>(std::move(root_op),
                                          std::vector<SortKey>{{ocol, order_desc}});
        root_op = std::move(sort);
    }

    // Execute and print
    print_header(schema);
    int count = 0;
    root_op->open();
    while (auto row = root_op->next()) {
        print_row(*row, schema);
        count++;
    }
    root_op->close();
    std::cout << "(" << count << " row" << (count == 1 ? "" : "s") << ")\n";
    return true;
}

// ══════════════════════════════════════════════════════════════
//  Main REPL
// ══════════════════════════════════════════════════════════════
int main(int argc, char* argv[]) {
    const char* db_file  = (argc > 1) ? argv[1] : "minidb.db";
    const char* wal_file = (argc > 2) ? argv[2] : "minidb.wal";

    std::cout << "minidb — a tiny relational database\n";
    std::cout << "Database: " << db_file << "\n\n";

    // Open storage layer
    Pager pager(db_file);
    WAL   wal(wal_file);

    // Recovery: replay WAL if we crashed last time
    auto [redone, undone] = Recovery::run(pager, wal, false);
    if (redone > 0 || undone > 0)
        std::cout << "[Recovery] Redone=" << redone << " Undone=" << undone << "\n";

    // Load catalog (what tables exist)
    load_catalog();

    std::cout << "Type SQL commands or .help for help.\n\n";

    std::string line;
    bool in_txn = false;
    uint64_t txn_id = 0;
    uint64_t next_txn = 1;

    while (true) {
        std::cout << (in_txn ? "minidb* " : "minidb> ");
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;

        // Remove trailing semicolon
        if (!line.empty() && line.back() == ';') line.pop_back();
        line = trim(line);
        if (line.empty()) continue;

        std::string upper = to_upper(line);

        // ── Meta commands ──
        if (line == ".quit" || line == ".exit") {
            std::cout << "Bye.\n";
            break;
        }
        if (line == ".help") {
            std::cout << "Commands:\n"
                      << "  CREATE TABLE name (col type, ...)\n"
                      << "  INSERT INTO name VALUES (v1, v2, ...)\n"
                      << "  SELECT * FROM name [WHERE col op val] [ORDER BY col [DESC]]\n"
                      << "  BEGIN / COMMIT / ROLLBACK\n"
                      << "  .tables   — list tables\n"
                      << "  .quit     — exit\n";
            continue;
        }
        if (line == ".tables") {
            if (catalog.empty()) std::cout << "(no tables)\n";
            for (auto& [name, schema] : catalog) {
                std::cout << name << " (";
                for (size_t i = 0; i < schema.columns.size(); i++) {
                    if (i > 0) std::cout << ", ";
                    std::cout << schema.columns[i].name
                              << " " << schema.columns[i].type;
                }
                std::cout << ")\n";
            }
            continue;
        }

        // ── Transaction commands ──
        if (upper == "BEGIN") {
            if (in_txn) { std::cout << "Already in a transaction.\n"; continue; }
            txn_id = next_txn++;
            wal.write_begin(txn_id);
            in_txn = true;
            std::cout << "BEGIN\n";
            continue;
        }
        if (upper == "COMMIT") {
            if (!in_txn) { std::cout << "Not in a transaction.\n"; continue; }
            wal.write_commit(txn_id);
            pager.flush_all();
            in_txn = false;
            std::cout << "COMMIT\n";
            continue;
        }
        if (upper == "ROLLBACK") {
            if (!in_txn) { std::cout << "Not in a transaction.\n"; continue; }
            wal.write_rollback(txn_id);
            in_txn = false;
            std::cout << "ROLLBACK\n";
            continue;
        }

        // ── SQL commands ──
        try {
            if (upper.substr(0, 12) == "CREATE TABLE") {
                handle_create_table(line, pager);
            } else if (upper.substr(0, 11) == "INSERT INTO") {
                handle_insert(line, pager);
            } else if (upper.substr(0, 6) == "SELECT") {
                handle_select(line, pager);
            } else {
                std::cout << "Unknown command. Type .help for help.\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    // Auto-commit any open transaction on exit
    if (in_txn) {
        wal.write_commit(txn_id);
        pager.flush_all();
    }

    return 0;
}