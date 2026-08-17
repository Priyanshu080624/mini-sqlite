# AST Spec

Owner: Sarthak (frontend side) — this is what the parser spits out.
Priyanshu doesn't touch this directly — he works off the operator tree in
`cursor_interface.md` instead.

Writing this down now so we're not guessing each other's data shapes later.
If this changes after we start coding, ping before merging — the planner
builds directly on top of it.

## Supported Data Types (v1)
- `INTEGER`
- `TEXT`
- `REAL`

(Stretch: `BOOLEAN`, `NULL` handling — add later if time permits)

## Token Categories
- Keywords: `SELECT`, `FROM`, `WHERE`, `INSERT`, `INTO`, `VALUES`, `CREATE`,
  `TABLE`, `INDEX`, `ON`, `JOIN`, `AND`, `OR`, `NOT`, `ORDER`, `BY`, `GROUP`,
  `BEGIN`, `COMMIT`, `ROLLBACK`, `PRIMARY`, `KEY`
- Identifiers: table names, column names
- Literals: integer literals, string literals (single-quoted), real/float literals
- Operators: `=`, `!=`, `<`, `>`, `<=`, `>=`
- Punctuation: `,` `;` `(` `)` `*` `.`

## AST Node Types

### Statement nodes (top level)

```
CreateTableStmt {
  table_name: string
  columns: List<ColumnDef>
}

ColumnDef {
  name: string
  type: DataType        // INTEGER | TEXT | REAL
  is_primary_key: bool
  is_not_null: bool
}

CreateIndexStmt {
  index_name: string
  table_name: string
  column_name: string
}

InsertStmt {
  table_name: string
  values: List<Literal>     // positional, matches column order for v1
}

SelectStmt {
  columns: List<string>       // ["*"] or explicit column list, may be
                                // qualified as "table.column"
  from: List<TableRef>          // multiple entries = implicit join source
  joins: List<JoinClause>        // explicit JOIN ... ON clauses
  where: Expr | null
  group_by: List<string> | null
  order_by: List<OrderByItem> | null
}

TableRef {
  table_name: string
  alias: string | null
}

JoinClause {
  table: TableRef
  on: Expr              // join condition, e.g. orders.user_id = users.id
}

OrderByItem {
  column: string
  direction: ASC | DESC   // default ASC
}

TxnStmt {
  kind: BEGIN | COMMIT | ROLLBACK
}
```

### Expression nodes (used in WHERE, ON, and aggregate args)

```
Expr =
  | ColumnRef { table: string | null, column: string }
  | Literal { value: int | string | float }
  | BinaryOp { left: Expr, op: string, right: Expr }   // op: =, !=, <, >, <=, >=, AND, OR
  | UnaryOp { op: NOT, operand: Expr }
  | AggregateCall { func: COUNT | SUM | AVG | MIN | MAX, arg: Expr | "*" }
```

## Grammar (informal EBNF, v1 scope)

```
statement       := create_table | create_index | insert | select | txn_stmt

create_table    := "CREATE" "TABLE" identifier "(" column_def ("," column_def)* ")"
column_def      := identifier data_type ("PRIMARY" "KEY")? ("NOT" "NULL")?
data_type       := "INTEGER" | "TEXT" | "REAL"

create_index    := "CREATE" "INDEX" identifier "ON" identifier "(" identifier ")"

insert          := "INSERT" "INTO" identifier "VALUES" "(" literal ("," literal)* ")"

select          := "SELECT" select_list "FROM" table_ref (join_clause)*
                    ("WHERE" expr)?
                    ("GROUP" "BY" identifier_list)?
                    ("ORDER" "BY" order_item_list)?

select_list     := "*" | column_ref ("," column_ref)*
table_ref       := identifier (identifier)?          // optional alias
join_clause     := "JOIN" table_ref "ON" expr

expr            := or_expr
or_expr         := and_expr ("OR" and_expr)*
and_expr        := comparison ("AND" comparison)*
comparison      := operand comp_op operand
comp_op         := "=" | "!=" | "<" | ">" | "<=" | ">="
operand         := column_ref | literal

txn_stmt        := "BEGIN" | "COMMIT" | "ROLLBACK"
```

## Change Policy
If any of the node shapes above change, tell Priyanshu — even though he
doesn't touch the AST directly, schema-related changes (column types, table
names) ripple into the catalog format he's reading off disk.

— Sarthak
