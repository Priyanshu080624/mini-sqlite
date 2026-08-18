// ============================================================================
// main.cpp  —  Mini SQLite REPL
//
// Runs the full frontend pipeline:
//   raw SQL text
//     → Tokenizer  → vector<Token>
//     → Parser     → Statement (AST)
//     → SemanticAnalyzer (validates + updates Catalog)
//     → Planner    → OperatorNode tree  (only for SELECT)
//
// The operator tree is printed to stdout so you can verify correctness
// before Priyanshu's executor is wired in.
//
// Special REPL commands:
//   .exit   — quit
//   .tables — list all tables currently in the in-memory catalog
//   .schema <table> — show a table's column definitions
// ============================================================================

#include <iostream>
#include <string>
#include <variant>

#include "common/token.h"
#include "common/ast.h"
#include "frontend/tokenizer.h"
#include "frontend/parser.h"
#include "frontend/catalog.h"
#include "frontend/sema.h"
#include "frontend/planner.h"

// ── REPL helper: print info about a statement type ───────────────────────────

static void printStatementKind(const Statement& stmt) {
    std::visit([](const auto& s) {
        using T = std::decay_t<decltype(s)>;
        if      constexpr (std::is_same_v<T, CreateTableStmt>)
            std::cout << "[DDL] CREATE TABLE " << s.table_name << "\n";
        else if constexpr (std::is_same_v<T, CreateIndexStmt>)
            std::cout << "[DDL] CREATE INDEX " << s.index_name
                      << " ON " << s.table_name << "(" << s.column_name << ")\n";
        else if constexpr (std::is_same_v<T, InsertStmt>)
            std::cout << "[DML] INSERT INTO " << s.table_name << "\n";
        else if constexpr (std::is_same_v<T, TxnStmt>) {
            switch (s.kind) {
                case TxnStmt::Kind::BEGIN:    std::cout << "[TXN] BEGIN\n";    break;
                case TxnStmt::Kind::COMMIT:   std::cout << "[TXN] COMMIT\n";   break;
                case TxnStmt::Kind::ROLLBACK: std::cout << "[TXN] ROLLBACK\n"; break;
            }
        }
        else if constexpr (std::is_same_v<T, SelectStmt>)
            std::cout << "[QUERY] SELECT\n";
    }, stmt);
}

// ── Main REPL loop ────────────────────────────────────────────────────────────

int main() {
    std::cout << "╔══════════════════════════════════════════╗\n"
              << "║        mini-sqlite REPL  (M1)            ║\n"
              << "║  .exit | .tables | .schema <table>       ║\n"
              << "╚══════════════════════════════════════════╝\n\n";

    Catalog          catalog;
    SemanticAnalyzer sema(catalog);
    Planner          planner(catalog);

    std::string line;
    while (true) {
        std::cout << "mini-sqlite> ";
        if (!std::getline(std::cin, line)) break;

        // Trim leading/trailing whitespace
        size_t s = line.find_first_not_of(" \t\r\n");
        size_t e = line.find_last_not_of(" \t\r\n");
        if (s == std::string::npos) continue;
        line = line.substr(s, e - s + 1);

        // ── Dot commands ──────────────────────────────────────────────────────
        if (line == ".exit") break;

        if (line == ".tables") {
            const auto& tables = catalog.all_tables();
            if (tables.empty()) {
                std::cout << "  (no tables)\n";
            } else {
                for (const auto& [name, _] : tables)
                    std::cout << "  " << name << "\n";
            }
            continue;
        }

        if (line.substr(0, 7) == ".schema") {
            std::string tbl = line.substr(7);
            size_t ts = tbl.find_first_not_of(" \t");
            if (ts == std::string::npos) {
                std::cout << "Usage: .schema <table_name>\n";
                continue;
            }
            tbl = tbl.substr(ts);
            const TableSchema* schema = catalog.get_table(tbl);
            if (!schema) {
                std::cout << "  No table '" << tbl << "' in catalog.\n";
            } else {
                std::cout << "  Table: " << schema->name << "\n";
                for (const auto& col : schema->columns) {
                    std::cout << "    [" << col.ordinal << "] "
                              << col.name << " "
                              << dataTypeToString(col.type);
                    if (schema->primary_key == col.name) std::cout << " PRIMARY KEY";
                    if (col.is_not_null) std::cout << " NOT NULL";
                    std::cout << "\n";
                }
                if (!schema->indexes.empty()) {
                    std::cout << "  Indexes:\n";
                    for (const auto& idx : schema->indexes)
                        std::cout << "    " << idx.name
                                  << " ON " << idx.column << "\n";
                }
            }
            continue;
        }

        // ── SQL pipeline ──────────────────────────────────────────────────────
        try {
            // Stage 1: Tokenize
            Tokenizer tok(line);
            std::vector<Token> tokens = tok.tokenize();

            // Stage 2: Parse
            Parser parser(std::move(tokens));
            Statement stmt = parser.parse();

            // Stage 3: Semantic analysis (also updates catalog for DDL)
            sema.analyze(stmt);

            // Stage 4: Print what kind of statement this was
            printStatementKind(stmt);

            // Stage 5: Plan (SELECT only — DDL/DML go to Priyanshu's executor)
            if (std::holds_alternative<SelectStmt>(stmt)) {
                const SelectStmt& sel = std::get<SelectStmt>(stmt);
                auto plan = planner.plan(sel);
                std::cout << "\nOperator tree:\n";
                std::cout << Planner::printTree(*plan);
                std::cout << "\n";
            } else {
                std::cout << "  OK — catalog updated.\n";
            }

        } catch (const ParseError& e) {
            std::cerr << "Parse error: " << e.what() << "\n";
        } catch (const SemanticError& e) {
            std::cerr << e.what() << "\n";
        } catch (const CatalogError& e) {
            std::cerr << "Catalog error: " << e.what() << "\n";
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }

    std::cout << "bye.\n";
    return 0;
}
