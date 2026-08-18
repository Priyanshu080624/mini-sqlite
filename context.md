# Project Context — Mini SQLite (Sarthak's side)

## What this project is
A relational database engine built from scratch in C++ — not a wrapper around
an existing database. Two-person college project (DBMS course + resume/GitHub
portfolio piece). Real B-tree storage, disk persistence, multi-table SQL
support, transactions with crash recovery via write-ahead logging.

## Team split (work is divided by architectural layer, not by feature)

**Sarthak (me, working in this Antigravity session) owns:**
- Tokenizer — done already, working, tested
- Parser (SQL text → AST) — building this next
- Semantic analysis (validate AST against catalog: table/column existence, type checks)
- Catalog (in-memory schema registry: table names, columns, types, primary keys, index metadata)
- Query planner (AST → operator tree: SeqScan, IndexScan, Filter, NestedLoopJoin, Sort, Aggregate)

**Priyanshu owns (do not implement this — just call into it via the agreed interfaces):**
- Pager (disk I/O in fixed 4KB pages, page cache)
- B-tree (on-disk sorted storage, node splitting/merging, used for tables and indexes)
- Cursor implementation (I only call `next()`, `seek()`, `insert()`, `remove()` — never implement these)
- Write-Ahead Log + crash recovery
- Transaction manager (locking, BEGIN/COMMIT/ROLLBACK internals)
- Executor operators that actually run against storage

## Language & build
- C++17
- CMake build (`CMakeLists.txt` at repo root)
- Repo structure:
```
src/
  common/      # shared types: token.h/cpp, AST types, cursor interface types
  frontend/    # Sarthak: tokenizer, parser, semantic analysis, planner, catalog
  storage/     # Priyanshu: pager, btree, wal, txn
  exec/        # Priyanshu: executor operators
  main.cpp
```

## Git workflow
- Private GitHub repo, `main` = stable, `dev` = integration branch
- Feature branches off `dev`, PR back into `dev`, other person reviews before merge
- Commit daily to keep contribution graph active
- Any change to a shared contract doc (below) gets flagged to Priyanshu before merging

## The three contract docs (already written, live in `docs/`)
These define the interfaces between my code and Priyanshu's code. Treat them
as authoritative — if implementation needs to diverge from them, update the
doc and note it, don't just silently drift.

1. **`docs/ast_spec.md`** — the AST node shapes my parser must produce
   (CreateTableStmt, InsertStmt, SelectStmt, JoinClause, Expr types, etc.)
   and the SQL grammar (EBNF) I'm parsing against.

2. **`docs/cursor_interface.md`** — the Cursor interface I call (`next()`,
   `current()`, `seek()`, `insert()`, `remove()`) and the Operator tree shape
   my planner must emit (SeqScan, IndexScan, Filter, NestedLoopJoin, Sort,
   Aggregate — each with `open()`/`next()`/`close()`, Volcano/iterator model).
   This is the single most important contract in the project.

3. **`docs/catalog_format.md`** — the in-memory Catalog structure I own
   (TableSchema, ColumnInfo, IndexInfo) and how it eventually persists to
   disk (coordinated with Priyanshu, since that touches his pager).

## Milestone plan (sequence matters more than calendar dates)
- **M1 — Skeleton:** REPL, tokenizer (done), parser for single-table
  CREATE TABLE / INSERT / SELECT with one WHERE condition, in-memory catalog,
  stub-wired to Priyanshu's B-tree for a working end-to-end demo
- **M2 — Multi-table:** parser extended for JOIN, compound WHERE (AND/OR/NOT),
  ORDER BY; semantic analysis pass; real query planner emitting operator trees
- **M3 — Indexes:** CREATE INDEX parsing; planner chooses IndexScan when a
  WHERE predicate matches an indexed column
- **M4 — Transactions:** parse BEGIN/COMMIT/ROLLBACK, route to Priyanshu's
  transaction manager; harden error handling across the board
- **M5 — Aggregates & polish:** GROUP BY, COUNT/SUM/AVG/MIN/MAX; integration
  test scripts; README contributions (grammar supported, example queries,
  frontend architecture diagram)

## Current status
Tokenizer is complete and verified working (handles keywords case-insensitively,
identifiers, integer/real/string literals, operators, punctuation, `--` line
comments). Files: `src/common/token.h/.cpp`, `src/frontend/tokenizer.h/.cpp`,
`src/main.cpp` (REPL driver), `CMakeLists.txt`.

**Next task: build the recursive-descent parser** that consumes the tokenizer's
output and produces AST nodes matching `docs/ast_spec.md`, starting with
CREATE TABLE, INSERT, and single-condition SELECT (M1 scope).

## How I like to work
- Explain concepts in plain language with analogies before diving into code —
  I'm learning database internals as I build this, not just copy-pasting.
- Call out the CS terminology explicitly when it comes up (e.g., "recursive
  descent," "AST," "Volcano model") since I want to be able to explain this
  project confidently in interviews.
- Working from a base of TypeScript/Express/PostgreSQL/AWS experience — C++
  and manual memory/pointer work is comparatively new to me, so don't assume
  deep familiarity with it.
