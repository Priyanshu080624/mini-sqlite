-- ============================================================
-- ddl.sql  —  CREATE TABLE, CREATE INDEX, dot-commands
-- Expected: 3x "OK — catalog updated", .tables lists both,
--           .schema users shows columns + index.
-- ============================================================

CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER NOT NULL);
CREATE TABLE orders (id INTEGER PRIMARY KEY, user_id INTEGER, total REAL);
CREATE INDEX idx_users_id ON users (id);
.tables
.schema users
.schema orders
.exit
