-- ============================================================
-- errors.sql  —  intentional bad SQL → verify error messages
-- Each statement should produce a named error, NOT a crash.
-- ============================================================

CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER NOT NULL);

-- 1. Table already exists
CREATE TABLE users (id INTEGER PRIMARY KEY);

-- 2. Column does not exist in SELECT
SELECT nonexistent FROM users;

-- 3. INSERT wrong number of values
INSERT INTO users VALUES (1, 'Alice');

-- 4. INSERT wrong type (text into integer column)
INSERT INTO users VALUES ('not_a_number', 'Bob', 25);

-- 5. HAVING without GROUP BY
SELECT * FROM users HAVING COUNT(*) > 1;

-- 6. LIMIT = 0 (must be positive)
SELECT * FROM users LIMIT 0;

-- 7. ORDER BY on non-existent column
SELECT * FROM users ORDER BY ghost_column;

-- 8. Unknown table in FROM
SELECT * FROM ghost_table;

-- 9. Ambiguous column (same name in two tables)
CREATE TABLE products (id INTEGER PRIMARY KEY, name TEXT);
SELECT name FROM users JOIN products ON users.id = products.id;

.exit
