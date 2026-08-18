-- ============================================================
-- select_basic.sql  —  single-table SELECT with WHERE / ORDER BY / LIMIT
-- Run AFTER ddl.sql tables exist (pipe both files if needed, or
-- create tables first then run this).
--
-- Expected operator trees:
--   WHERE id=1       → Project → Filter → IndexScan   (index used!)
--   ORDER BY name    → Project → Sort → SeqScan
--   LIMIT 5          → Project → Sort → SeqScan  (LIMIT carried in stmt)
--   WHERE age > 18   → Project → Filter → SeqScan
-- ============================================================

CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER NOT NULL);
CREATE INDEX idx_users_id ON users (id);

-- IndexScan expected (id has index, equality predicate)
SELECT * FROM users WHERE id = 1;

-- SeqScan + Sort
SELECT name FROM users ORDER BY name ASC;

-- SeqScan + Sort + LIMIT in stmt (planner doesn't wrap LIMIT yet — it's in SelectStmt)
SELECT name FROM users ORDER BY name DESC LIMIT 5;

-- SeqScan + Filter (age has no index)
SELECT name, age FROM users WHERE age > 18;

-- Qualified column name
SELECT users.name FROM users WHERE users.age > 21;

.exit
