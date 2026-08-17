# Catalog Format

Sarthak owns the in-memory structure and API. Once we get to disk
persistence (M2+) this needs Priyanshu's input too, since it touches the
pager.

Catalog = the DB's "schema of schemas." Tracks what tables exist, their
columns/types, primary keys, indexes. Both of us read from this constantly
so we're locking the shape down here first.

## In-Memory Catalog (M1 — Sarthak builds this)

```
Catalog {
  tables: Map<string, TableSchema>
}

TableSchema {
  name: string
  columns: List<ColumnInfo>
  primary_key: string          // column name
  indexes: List<IndexInfo>
}

ColumnInfo {
  name: string
  type: DataType                // INTEGER | TEXT | REAL
  ordinal: int                  // position in row, 0-indexed — determines
                                  // serialization order in storage
  is_not_null: bool
}

IndexInfo {
  name: string
  column: string
  root_page_id: int              // set once Priyanshu allocates the index
                                   // B-tree's root page (M3)
}
```

## Catalog API (semantic analysis + planner will call these)

```
Catalog {
  TableSchema? get_table(name: string)
  void         add_table(schema: TableSchema)
  void         add_index(table_name: string, index: IndexInfo)
  bool         table_exists(name: string)
  bool         column_exists(table_name: string, column_name: string)
}
```

## On-Disk Persistence (M2 — need to sit down with Priyanshu for this)

Catalog needs to survive restarts obviously. Two options — pick one together
when we get here, don't decide solo since it's storage format:

**Option A (simpler, recommended for v1):** reserve **page 0** of the
database file as a dedicated "catalog page" (or a small chain of pages if it
grows). Serialize the whole `Catalog` struct (table names, columns, types,
root page IDs for each table's B-tree and each index's B-tree) into this
page using a simple fixed-format or length-prefixed encoding. On startup,
Priyanshu's pager reads page 0 first and Sarthak deserializes it into the
in-memory `Catalog`.

**Option B (more "real"):** treat the catalog itself as a special table
(`sqlite_master`-style, which is literally what SQLite does) stored in its
own B-tree. More elegant, more work — consider as a stretch goal if M1-M4
finish with time to spare.

Going with **Option A** for now. Once it's actually implemented, come back
and write down the exact byte layout here (offsets, field sizes) so we can
both debug catalog corruption independently instead of guessing.

### Root page ID linkage
This is the part most likely to break silently, so paying extra attention
here: every `TableSchema` and `IndexInfo` needs a `root_page_id` pointing to
where its B-tree actually starts in the file. Priyanshu's B-tree allocates
this when a table/index gets created, Sarthak's catalog stores it.

1. Parser produces `CreateTableStmt`
2. Planner/executor calls into Priyanshu's B-tree to allocate a new root page
3. Priyanshu's code returns `root_page_id`
4. Sarthak stores it on the `TableSchema` and persists the catalog

## Change Policy
`root_page_id` linkage and the disk layout are our highest-risk spots for
silent bugs. Talk before touching catalog serialization code, don't assume.

— Sarthak & Priyanshu
