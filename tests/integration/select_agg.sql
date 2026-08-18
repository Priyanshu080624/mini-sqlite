-- ============================================================
-- select_agg.sql  —  GROUP BY, HAVING, aggregate functions
-- Expected:
--   GROUP BY only   → Project → Aggregate(group_by=[...]) → SeqScan
--   GROUP BY+HAVING → Project → Aggregate(group_by=[...], having=...) → SeqScan
--   + ORDER BY      → Project → Sort → Aggregate → SeqScan
-- ============================================================

CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER NOT NULL);
CREATE TABLE orders (id INTEGER PRIMARY KEY, user_id INTEGER, total REAL);

-- GROUP BY without HAVING
SELECT name FROM users GROUP BY name;

-- GROUP BY with HAVING
SELECT name FROM users GROUP BY name HAVING COUNT(*) > 1;

-- GROUP BY + HAVING + ORDER BY
SELECT name FROM users GROUP BY name HAVING COUNT(*) > 1 ORDER BY name ASC;

-- Aggregate in HAVING referencing a column
SELECT user_id FROM orders GROUP BY user_id HAVING SUM(total) > 500;

.exit
