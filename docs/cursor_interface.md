# Cursor Interface & Operator Tree

This is the big one. Everything above this line is Sarthak's problem,
everything below the Cursor is Priyanshu's problem, and this doc is where
they meet. Read it together before writing code — don't just skim it solo.

Any change here = message the other person first. This is basically a
function signature the other person's code already calls.

## Row / Value representation

Shared type both sides serialize/deserialize against:

```
Value = Int(i64) | Text(string) | Real(f64) | Null

Row = List<Value>          // ordered, matches column order in catalog schema
```

## Cursor Interface (Priyanshu implements, executor operators call into it)

Low-level interface for reading/writing against a B-tree (table storage or
an index):

```
Cursor {
  bool   next()          // advance to next row in key order; false if exhausted
  Row    current()        // return the row at current position
  bool   seek(Key key)     // jump directly to a key (point lookup / index probe)
  void   insert(Row row)
  void   remove()          // remove row at current cursor position
}
```

- `Key` for a table cursor = primary key value
- `Key` for an index cursor = indexed column's value (cursor then yields the
  primary key to look up the full row in the table B-tree)

## Operator Tree (Sarthak's planner builds this, Priyanshu's executor runs it)

Using the iterator / Volcano model — every operator has `open()`, `next()`,
`close()`. Each `next()` pulls one row from whatever's below it, does its
thing, passes it up. This is literally how Postgres and MySQL do it, so
it's a good thing to be able to explain by name in an interview.

```
Operator {
  void  open()
  Row?  next()        // returns null/None when exhausted
  void  close()
}
```

### Operator types (v1 scope)

```
SeqScan(table_name)
  - wraps a Cursor over the full table B-tree, calls next() repeatedly

IndexScan(table_name, index_name, key_condition)
  - wraps a Cursor over the index B-tree, seeks based on key_condition,
    then looks up full rows in the table B-tree

Filter(child: Operator, predicate: Expr)
  - pulls from child, evaluates predicate per row, only returns matches

NestedLoopJoin(left: Operator, right: Operator, condition: Expr)
  - for each row in left, rescans right (or re-opens right operator),
    evaluates condition, emits matching combined rows
  - v1: no hash join or merge join, nested loop is enough

Sort(child: Operator, order_by: List<OrderByItem>)
  - buffers all rows from child, sorts, then yields in order
  - fine to be in-memory for v1 (no external sort needed at our data sizes)

Aggregate(child: Operator, group_by: List<string>, aggregates: List<AggregateCall>)
  - buffers/groups rows from child, computes COUNT/SUM/AVG/MIN/MAX per group
```

### Example: planner output for a query

```sql
SELECT users.name, orders.total
FROM users JOIN orders ON users.id = orders.user_id
WHERE orders.total > 100
ORDER BY orders.total
```

becomes:

```
Sort(order_by=[orders.total])
  └─ Filter(predicate: orders.total > 100)
       └─ NestedLoopJoin(condition: users.id = orders.user_id)
            ├─ SeqScan(users)
            └─ SeqScan(orders)
```

Sarthak's planner builds this tree structure (plain data, no execution
logic). Priyanshu's executor walks it and actually runs it via the Cursor
interface underneath each `SeqScan`/`IndexScan` leaf.

## Expression Evaluation

`Filter` and join conditions both need to evaluate `Expr` nodes against a
`Row`. Sarthak will write this first since it's built on the `Expr` types
from the AST, but Priyanshu calls it from inside `Filter`/`NestedLoopJoin`,
so it lives in `common/` for both of us.

```
eval(expr: Expr, row: Row, schema: Schema) -> Value
```

## Change Policy
Same as above — this file is the seam between our two halves. Don't change
an operator's fields or the Cursor interface without a heads-up first.

— Sarthak & Priyanshu
