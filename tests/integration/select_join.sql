-- ============================================================
-- select_join.sql  —  JOIN queries
-- Expected: NestedLoopJoin with SeqScan leaves.
-- ============================================================

CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER NOT NULL);
CREATE TABLE orders (id INTEGER PRIMARY KEY, user_id INTEGER, total REAL);

-- Basic join
SELECT * FROM users JOIN orders ON users.id = orders.user_id;

-- Join + WHERE filter (Filter wraps the join)
SELECT * FROM users JOIN orders ON users.id = orders.user_id WHERE orders.total > 100;

-- Join + ORDER BY
SELECT users.name, orders.total
FROM users JOIN orders ON users.id = orders.user_id
WHERE orders.total > 50
ORDER BY orders.total DESC;

.exit
