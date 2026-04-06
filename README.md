# sqlite-latin1

A SQLite extension (and static-linkage library) that adds proper **ISO-8859-1 (Latin1)** support to SQLite: case-insensitive collations, `UPPER`/`LOWER` for accented characters, and a Latin1-aware `LIKE` function.

SQLite's built-in string functions only handle ASCII. This extension fills the gap for applications that store Latin1-encoded data.

---

## Features

| What it adds | Name | Description |
|---|---|---|
| Collation | `LATIN1` | Case-sensitive, byte-order comparison for Latin1 data |
| Collation | `LATIN1_CI` | Case-insensitive comparison (`'é' = 'É'`, `'ü' = 'Ü'`, etc.) |
| Scalar function | `latin1_upper(text)` | `UPPER` over the full ISO-8859-1 range |
| Scalar function | `latin1_lower(text)` | `LOWER` over the full ISO-8859-1 range |
| Scalar function | `latin1_like(pattern, text [, escape])` | Case-insensitive `LIKE` for Latin1 letters |
| Override | `latin1_override_nocase(db)` | Replaces SQLite's built-in `NOCASE` collation with Latin1 CI |

---

## Building

### Prerequisites

- CMake ≥ 3.16
- A C compiler (GCC, Clang, or MSVC)
- SQLite3 development headers (`libsqlite3-dev` on Debian/Ubuntu, `sqlite` via Homebrew on macOS)

### Loadable extension + tests (default)

```sh
cmake -B build
cmake --build build
ctest --test-dir build -V
```

This produces:
- `build/latin1.so` (Linux) / `build/latin1.dylib` (macOS) — loadable extension
- `build/test_latin1` — test binary

### Static library only

```sh
cmake -B build -DBUILD_SHARED_EXT=OFF
cmake --build build
```

### Fuzz target (requires Clang + LibFuzzer)

```sh
cmake -B build -DENABLE_FUZZING=ON \
      -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang
cmake --build build --target fuzz_latin1
./build/fuzz_latin1 -max_total_time=60
```

---

## Usage

### As a loadable extension

```sql
.load ./latin1

-- Case-insensitive collation
SELECT * FROM contacts ORDER BY name COLLATE LATIN1_CI;

-- Case-insensitive equality
SELECT * FROM contacts WHERE name = 'Müller' COLLATE LATIN1_CI;

-- Accented UPPER/LOWER
SELECT latin1_upper('café');   -- → 'CAFÉ'
SELECT latin1_lower('ÜBER');   -- → 'über'

-- Latin1-aware LIKE
SELECT * FROM products WHERE name latin1_like('%naïve%', name);
-- or as a function:
SELECT latin1_like('%naïve%', 'Naïve');  -- → 1
```

### Overriding the built-in NOCASE collation

SQLite's built-in `NOCASE` only folds ASCII letters (A–Z). Calling
`latin1_override_nocase()` replaces it with the Latin1 CI collation for the
lifetime of that connection, so any column declared `COLLATE NOCASE` or any
`ORDER BY … COLLATE NOCASE` expression automatically gains full ISO-8859-1
case-folding — no schema changes required.

```sql
.load ./latin1
-- (the extension calls latin1_override_nocase() automatically on load)

-- Now NOCASE folds accented letters too
SELECT 'café' = 'CAFÉ' COLLATE NOCASE;  -- → 1

CREATE TABLE contacts (name TEXT COLLATE NOCASE);
INSERT INTO contacts VALUES ('Müller'), ('müller');
SELECT COUNT(*) FROM contacts WHERE name = 'MÜLLER';  -- → 2
```

From C, call it explicitly after loading the extension or after
`latin1_register()`:

```c
latin1_register(db);
latin1_override_nocase(db);   /* NOCASE is now Latin1 CI for this connection */
```

### Declaring a column with a Latin1 collation

```sql
CREATE TABLE contacts (
    id   INTEGER PRIMARY KEY,
    name TEXT    NOT NULL COLLATE LATIN1_CI
);
-- Indexes on `name` will be used for LATIN1_CI queries automatically.
```

### Embedded / static linkage

```c
#include "latin1.h"

sqlite3 *db;
sqlite3_open(":memory:", &db);
latin1_register(db);   // registers all collations and functions
```

---

## API

```c
/*
 * Register all Latin1 collations and scalar functions with db.
 * Returns SQLITE_OK on success, or a SQLite error code.
 */
int latin1_register(sqlite3 *db);

/*
 * Override SQLite's built-in NOCASE collation with the Latin1 CI collation.
 * Must be called after latin1_register() (or after the connection is opened).
 * The override applies only to the given connection.
 * Returns SQLITE_OK on success, or a SQLite error code.
 */
int latin1_override_nocase(sqlite3 *db);
```

Defined in `src/latin1.h`. Include `src/latin1.c` in your build with `-DSQLITE_CORE` when linking statically against `libsqlite3`.

---

## Project structure

```
src/
  latin1.h   – public API header
  latin1.c   – implementation (collations, scalar functions, extension init)
test/
  test_latin1.c   – unit and integration tests
  fuzz_latin1.c   – LibFuzzer fuzz target
CMakeLists.txt
```

---

## License

MIT
