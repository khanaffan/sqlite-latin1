/*
 * test_latin1.c – unit and integration tests for the Latin1 SQLite extension.
 *
 * Linked statically against latin1.c (compiled with -DSQLITE_CORE) and
 * libsqlite3, so no .load_extension() is required.
 *
 * All test strings that exercise Latin1 extended characters use explicit byte
 * escapes (e.g. "\xe9" for é, "\xc9" for É) so the test file compiles cleanly
 * regardless of the source encoding.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "sqlite3.h"
#include "latin1.h"

/* -------------------------------------------------------------------------
 * Minimal test framework
 * ------------------------------------------------------------------------- */

static int g_run    = 0;
static int g_failed = 0;

#define PASS(name)  do { printf("  PASS  %s\n", (name)); g_run++; } while(0)

static void fail_impl(const char *name, const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "  FAIL  %s -- ", name);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    g_failed++;
}
#define FAIL(name, ...) do { fail_impl(name, __VA_ARGS__); g_run++; } while(0)

#define EXPECT_INT(name, actual, expected) \
    do { int _a = (actual), _e = (expected); \
         if (_a == _e) PASS(name); \
         else FAIL(name, "expected %d, got %d", _e, _a); } while(0)

#define EXPECT_STR(name, actual, expected) \
    do { const char *_a = (actual), *_e = (expected); \
         if (_a && strcmp(_a, _e) == 0) PASS(name); \
         else FAIL(name, "expected \"%s\", got \"%s\"", _e, _a ? _a : "(null)"); } while(0)

/* -------------------------------------------------------------------------
 * Helper: run a single-value SQL query and return the int result.
 * Returns -9999 on error.
 * ------------------------------------------------------------------------- */
static int sql_int(sqlite3 *db, const char *sql) {
    sqlite3_stmt *s;
    int val = -9999;
    if (sqlite3_prepare_v2(db, sql, -1, &s, NULL) != SQLITE_OK) return val;
    if (sqlite3_step(s) == SQLITE_ROW) val = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    return val;
}

/*
 * Helper: run a parameterised query with one TEXT parameter (raw bytes,
 * length len) and return the int result.
 */
static int sql_int_bind1(sqlite3 *db, const char *sql,
                         const char *val, int len) {
    sqlite3_stmt *s;
    int result = -9999;
    if (sqlite3_prepare_v2(db, sql, -1, &s, NULL) != SQLITE_OK) return result;
    sqlite3_bind_text(s, 1, val, len, SQLITE_STATIC);
    if (sqlite3_step(s) == SQLITE_ROW) result = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    return result;
}

/*
 * Helper: run a parameterised query with one TEXT parameter and return the
 * text result as a malloc'd C string (caller must free).
 */
static char *sql_text_bind1(sqlite3 *db, const char *sql,
                             const char *val, int len) {
    sqlite3_stmt *s;
    char *result = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &s, NULL) != SQLITE_OK) return NULL;
    sqlite3_bind_text(s, 1, val, len, SQLITE_STATIC);
    if (sqlite3_step(s) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(s, 0);
        int bytes = sqlite3_column_bytes(s, 0);
        result = (char *)malloc(bytes + 1);
        if (result) { memcpy(result, txt, bytes); result[bytes] = '\0'; }
    }
    sqlite3_finalize(s);
    return result;
}

/* Helper: compare two raw byte strings via a parameterised collation query. */
static int collation_cmp(sqlite3 *db, const char *collation,
                          const char *a, int alen,
                          const char *b, int blen) {
    char sql[256];
    sqlite3_stmt *s;
    int result = -9999;
    snprintf(sql, sizeof(sql),
             "SELECT CASE WHEN ?1 < ?2 COLLATE %s THEN -1"
             "            WHEN ?1 > ?2 COLLATE %s THEN  1"
             "            ELSE 0 END", collation, collation);
    if (sqlite3_prepare_v2(db, sql, -1, &s, NULL) != SQLITE_OK) return result;
    sqlite3_bind_text(s, 1, a, alen, SQLITE_STATIC);
    sqlite3_bind_text(s, 2, b, blen, SQLITE_STATIC);
    if (sqlite3_step(s) == SQLITE_ROW) result = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    return result;
}

/* =========================================================================
 * Test suites
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * 1. LATIN1 collation – case-sensitive, byte order
 * ------------------------------------------------------------------------- */
static void test_collation_cs(sqlite3 *db) {
    printf("\n[LATIN1 – case-sensitive collation]\n");

    /* Basic ASCII ordering: 'A'(0x41) < 'a'(0x61) */
    EXPECT_INT("ASCII upper < lower",
               collation_cmp(db, "LATIN1", "A", 1, "a", 1), -1);

    /* 'B'(0x42) < 'a'(0x61) is false; 'a' > 'B' */
    EXPECT_INT("'a' > 'B' by byte value",
               collation_cmp(db, "LATIN1", "a", 1, "B", 1), 1);

    /* Same string → 0 */
    EXPECT_INT("identical strings equal",
               collation_cmp(db, "LATIN1", "hello", 5, "hello", 5), 0);

    /* Prefix ordering: "abc" < "abcd" */
    EXPECT_INT("prefix shorter is less",
               collation_cmp(db, "LATIN1", "abc", 3, "abcd", 4), -1);

    /* Latin1 extended: é(0xE9) < ê(0xEA) */
    EXPECT_INT("\\xe9 (é) < \\xea (ê)",
               collation_cmp(db, "LATIN1", "\xe9", 1, "\xea", 1), -1);

    /* Case-sensitive: É(0xC9) < é(0xE9) */
    EXPECT_INT("\\xc9 (É) < \\xe9 (é) – case-sensitive",
               collation_cmp(db, "LATIN1", "\xc9", 1, "\xe9", 1), -1);

    /* ß(0xDF) < à(0xE0) */
    EXPECT_INT("\\xdf (ß) < \\xe0 (à)",
               collation_cmp(db, "LATIN1", "\xdf", 1, "\xe0", 1), -1);
}

/* -------------------------------------------------------------------------
 * 2. LATIN1_CI collation – case-insensitive
 * ------------------------------------------------------------------------- */
static void test_collation_ci(sqlite3 *db) {
    printf("\n[LATIN1_CI – case-insensitive collation]\n");

    /* ASCII: 'a' == 'A' */
    EXPECT_INT("ASCII 'a' == 'A'",
               collation_cmp(db, "LATIN1_CI", "a", 1, "A", 1), 0);

    /* é(0xE9) == É(0xC9) */
    EXPECT_INT("\\xe9 (é) == \\xc9 (É)",
               collation_cmp(db, "LATIN1_CI", "\xe9", 1, "\xc9", 1), 0);

    /* ü(0xFC) == Ü(0xDC) */
    EXPECT_INT("\\xfc (ü) == \\xdc (Ü)",
               collation_cmp(db, "LATIN1_CI", "\xfc", 1, "\xdc", 1), 0);

    /* ø(0xF8) == Ø(0xD8) */
    EXPECT_INT("\\xf8 (ø) == \\xd8 (Ø)",
               collation_cmp(db, "LATIN1_CI", "\xf8", 1, "\xd8", 1), 0);

    /* ß(0xDF) == ß (no uppercase mapping → compares equal to itself) */
    EXPECT_INT("\\xdf (ß) == \\xdf (ß)",
               collation_cmp(db, "LATIN1_CI", "\xdf", 1, "\xdf", 1), 0);

    /* ÿ(0xFF) == ÿ (no uppercase in Latin1) */
    EXPECT_INT("\\xff (ÿ) == \\xff",
               collation_cmp(db, "LATIN1_CI", "\xff", 1, "\xff", 1), 0);

    /* é(0xE9) < ê(0xEA) even case-insensitively */
    EXPECT_INT("\\xe9 (é) < \\xea (ê) CI",
               collation_cmp(db, "LATIN1_CI", "\xe9", 1, "\xea", 1), -1);

    /* Multi-char: "café" == "CAFÉ" */
    EXPECT_INT("\"caf\\xe9\" CI == \"CAF\\xc9\"",
               collation_cmp(db, "LATIN1_CI",
                              "caf\xe9", 4, "CAF\xc9", 4), 0);
}

/* -------------------------------------------------------------------------
 * 3. latin1_upper()
 * ------------------------------------------------------------------------- */
static void test_upper(sqlite3 *db) {
    char *r;
    printf("\n[latin1_upper()]\n");

    /* ASCII */
    r = sql_text_bind1(db, "SELECT latin1_upper(?1)", "hello", 5);
    EXPECT_STR("upper('hello') = 'HELLO'", r, "HELLO"); free(r);

    /* Already upper */
    r = sql_text_bind1(db, "SELECT latin1_upper(?1)", "WORLD", 5);
    EXPECT_STR("upper('WORLD') = 'WORLD'", r, "WORLD"); free(r);

    /* é → É  (0xE9 → 0xC9) */
    r = sql_text_bind1(db, "SELECT latin1_upper(?1)", "\xe9", 1);
    EXPECT_INT("upper(\\xe9) byte = 0xC9", r ? (unsigned char)r[0] : 0, 0xC9); free(r);

    /* ü → Ü  (0xFC → 0xDC) */
    r = sql_text_bind1(db, "SELECT latin1_upper(?1)", "\xfc", 1);
    EXPECT_INT("upper(\\xfc) byte = 0xDC", r ? (unsigned char)r[0] : 0, 0xDC); free(r);

    /* ø → Ø  (0xF8 → 0xD8) */
    r = sql_text_bind1(db, "SELECT latin1_upper(?1)", "\xf8", 1);
    EXPECT_INT("upper(\\xf8) byte = 0xD8", r ? (unsigned char)r[0] : 0, 0xD8); free(r);

    /* ß → ß  (0xDF – no uppercase in ISO-8859-1) */
    r = sql_text_bind1(db, "SELECT latin1_upper(?1)", "\xdf", 1);
    EXPECT_INT("upper(\\xdf) (ß) unchanged = 0xDF",
               r ? (unsigned char)r[0] : 0, 0xDF); free(r);

    /* ÿ → ÿ  (0xFF – Ÿ is U+0178, outside Latin1) */
    r = sql_text_bind1(db, "SELECT latin1_upper(?1)", "\xff", 1);
    EXPECT_INT("upper(\\xff) (ÿ) unchanged = 0xFF",
               r ? (unsigned char)r[0] : 0, 0xFF); free(r);

    /* Multi-char: "héllo" → "HÉLLO" */
    r = sql_text_bind1(db, "SELECT latin1_upper(?1)", "h\xe9llo", 5);
    {
        int ok = r && r[0]=='H' && (unsigned char)r[1]==0xC9
                 && r[2]=='L' && r[3]=='L' && r[4]=='O';
        if (ok) PASS("upper(\"h\\xe9llo\") = \"H\\xc9LLO\"");
        else FAIL("upper(\"h\\xe9llo\") = \"H\\xc9LLO\"", "mismatch");
    }
    free(r);

    /* NULL input → NULL output */
    EXPECT_INT("upper(NULL) is NULL",
               sql_int(db, "SELECT latin1_upper(NULL) IS NULL"), 1);
}

/* -------------------------------------------------------------------------
 * 4. latin1_lower()
 * ------------------------------------------------------------------------- */
static void test_lower(sqlite3 *db) {
    char *r;
    printf("\n[latin1_lower()]\n");

    /* ASCII */
    r = sql_text_bind1(db, "SELECT latin1_lower(?1)", "HELLO", 5);
    EXPECT_STR("lower('HELLO') = 'hello'", r, "hello"); free(r);

    /* É → é  (0xC9 → 0xE9) */
    r = sql_text_bind1(db, "SELECT latin1_lower(?1)", "\xc9", 1);
    EXPECT_INT("lower(\\xc9) byte = 0xE9", r ? (unsigned char)r[0] : 0, 0xE9); free(r);

    /* Ü → ü  (0xDC → 0xFC) */
    r = sql_text_bind1(db, "SELECT latin1_lower(?1)", "\xdc", 1);
    EXPECT_INT("lower(\\xdc) byte = 0xFC", r ? (unsigned char)r[0] : 0, 0xFC); free(r);

    /* Ø → ø  (0xD8 → 0xF8) */
    r = sql_text_bind1(db, "SELECT latin1_lower(?1)", "\xd8", 1);
    EXPECT_INT("lower(\\xd8) byte = 0xF8", r ? (unsigned char)r[0] : 0, 0xF8); free(r);

    /* × (0xD7) unchanged – not a letter */
    r = sql_text_bind1(db, "SELECT latin1_lower(?1)", "\xd7", 1);
    EXPECT_INT("lower(\\xd7) (×) unchanged = 0xD7",
               r ? (unsigned char)r[0] : 0, 0xD7); free(r);

    /* round-trip: lower(upper(x)) == lower(x) */
    r = sql_text_bind1(db,
        "SELECT latin1_lower(latin1_upper(?1))", "\xe9", 1);
    EXPECT_INT("lower(upper(\\xe9)) round-trip = 0xE9",
               r ? (unsigned char)r[0] : 0, 0xE9); free(r);

    /* NULL input */
    EXPECT_INT("lower(NULL) is NULL",
               sql_int(db, "SELECT latin1_lower(NULL) IS NULL"), 1);
}

/* -------------------------------------------------------------------------
 * 5. latin1_like()
 * ------------------------------------------------------------------------- */
static void test_like(sqlite3 *db) {
    printf("\n[latin1_like()]\n");

    /* Basic ASCII exact match */
    EXPECT_INT("'hello' like 'hello'",
               sql_int(db, "SELECT latin1_like('hello','hello')"), 1);

    /* ASCII case-insensitive */
    EXPECT_INT("'HELLO' like 'hello' (CI)",
               sql_int(db, "SELECT latin1_like('hello','HELLO')"), 1);

    /* No match */
    EXPECT_INT("'world' not like 'hello'",
               sql_int(db, "SELECT latin1_like('hello','world')"), 0);

    /* % wildcard – prefix */
    EXPECT_INT("'%llo' matches 'hello'",
               sql_int(db, "SELECT latin1_like('%llo','hello')"), 1);

    /* % wildcard – suffix */
    EXPECT_INT("'hel%' matches 'hello'",
               sql_int(db, "SELECT latin1_like('hel%','hello')"), 1);

    /* % wildcard – middle */
    EXPECT_INT("'h%o' matches 'hello'",
               sql_int(db, "SELECT latin1_like('h%o','hello')"), 1);

    /* % matches empty */
    EXPECT_INT("'%' matches ''",
               sql_int(db, "SELECT latin1_like('%','')"), 1);

    /* %% consecutive */
    EXPECT_INT("'%%' matches 'anything'",
               sql_int(db, "SELECT latin1_like('%%','anything')"), 1);

    /* _ wildcard */
    EXPECT_INT("'h_llo' matches 'hello'",
               sql_int(db, "SELECT latin1_like('h_llo','hello')"), 1);

    EXPECT_INT("'h_llo' does not match 'hllo'",
               sql_int(db, "SELECT latin1_like('h_llo','hllo')"), 0);

    /* Latin1 CI: pattern 'caf\xc9' matches text 'caf\xe9' */
    EXPECT_INT("latin1_like: É matches é",
               sql_int_bind1(db, "SELECT latin1_like('caf\xc9', ?1)",
                             "caf\xe9", 4), 1);

    /* Latin1 CI: wildcard + accented */
    EXPECT_INT("latin1_like: '%\xdc%' matches 'M\xfcller'",
               sql_int_bind1(db, "SELECT latin1_like('%\xdc%', ?1)",
                             "M\xfcller", 7), 1);

    /* Escape character: treat % literally */
    EXPECT_INT("escape: '50!%' matches '50%' with escape='!'",
               sql_int(db, "SELECT latin1_like('50!%','50%','!')"), 1);

    EXPECT_INT("escape: '50!%' does not match '50x' with escape='!'",
               sql_int(db, "SELECT latin1_like('50!%','50x','!')"), 0);

    /* NULL arguments */
    EXPECT_INT("like(NULL, 'x') is NULL",
               sql_int(db, "SELECT latin1_like(NULL,'x') IS NULL"), 1);

    EXPECT_INT("like('x', NULL) is NULL",
               sql_int(db, "SELECT latin1_like('x',NULL) IS NULL"), 1);
}

/* -------------------------------------------------------------------------
 * 6. Table-level collation and ORDER BY
 * ------------------------------------------------------------------------- */
static void test_table_collation(sqlite3 *db) {
    sqlite3_stmt *s;
    int rc;
    printf("\n[Table COLLATE LATIN1_CI and ORDER BY]\n");

    sqlite3_exec(db, "DROP TABLE IF EXISTS names", NULL, NULL, NULL);
    rc = sqlite3_exec(db,
        "CREATE TABLE names (name TEXT COLLATE LATIN1_CI);"
        "INSERT INTO names VALUES ('alice');"
        "INSERT INTO names VALUES ('Bob');"
        "INSERT INTO names VALUES ('\xc9lodie');"  /* Élodie */
        "INSERT INTO names VALUES ('\xe9mile');"   /* émile  */
        "INSERT INTO names VALUES ('Charlie');",
        NULL, NULL, NULL);

    if (rc != SQLITE_OK) {
        FAIL("create+insert names table", "%s", sqlite3_errmsg(db));
        return;
    }

    /* CI equality via declared column collation */
    EXPECT_INT("column CI: 'alice' = 'ALICE'",
               sql_int(db, "SELECT COUNT(*) FROM names WHERE name = 'ALICE'"),
               1);

    /* CI: Élodie (0xC9...) matches élodie (0xE9...) – length is 6 bytes */
    EXPECT_INT("column CI: \\xc9lodie = \\xe9lodie",
               sql_int_bind1(db,
                   "SELECT COUNT(*) FROM names WHERE name = ?1",
                   "\xe9lodie", 6), 1);

    /* ORDER BY LATIN1_CI: should group É and é together lexicographically */
    rc = sqlite3_prepare_v2(db,
        "SELECT name FROM names ORDER BY name COLLATE LATIN1_CI", -1, &s, NULL);
    if (rc != SQLITE_OK) {
        FAIL("ORDER BY LATIN1_CI prepare", "%s", sqlite3_errmsg(db));
        return;
    }

    /* Verify É and é are adjacent (both map to same upper 0xC9...) */
    {
        char *rows[5];
        int n = 0;
        /* Copy each row before calling sqlite3_step() again; column text
         * pointers are only valid until the next sqlite3_step() call. */
        while (sqlite3_step(s) == SQLITE_ROW && n < 5) {
            const char *txt = (const char *)sqlite3_column_text(s, 0);
            rows[n++] = txt ? strdup(txt) : NULL;
        }
        EXPECT_INT("ORDER BY returns 5 rows", n, 5);

        /* alice < Bob < Charlie < É* / é* order */
        if (n == 5) {
            int alice_pos = -1, elodie_pos = -1, emile_pos = -1;
            int i;
            for (i = 0; i < n; i++) {
                if (!rows[i]) continue;
                if (strcmp(rows[i], "alice")   == 0) alice_pos   = i;
                if ((unsigned char)rows[i][0] == 0xC9) elodie_pos = i;
                if ((unsigned char)rows[i][0] == 0xE9) emile_pos  = i;
            }
            EXPECT_INT("alice sorts before É/é", alice_pos < elodie_pos, 1);
            /* É and é are adjacent under CI sort */
            EXPECT_INT("É and é are adjacent",
                       abs(elodie_pos - emile_pos) == 1, 1);
        }

        for (n--; n >= 0; n--) free(rows[n]);
    }
    sqlite3_finalize(s);

    sqlite3_exec(db, "DROP TABLE names", NULL, NULL, NULL);
}

/* -------------------------------------------------------------------------
 * 7. Demonstrate built-in SQLite limitations with Latin1 data
 * ------------------------------------------------------------------------- */
static void test_builtin_limitations(sqlite3 *db) {
    char *r;
    printf("\n[Built-in SQLite UPPER/LIKE limitations with Latin1 bytes]\n");

    /* SQLite UPPER only handles ASCII: UPPER('é') returns 'é' unchanged */
    r = sql_text_bind1(db, "SELECT UPPER(?1)", "\xe9", 1);
    {
        int unchanged = r && (unsigned char)r[0] == 0xE9;
        if (unchanged)
            PASS("UPPER(\\xe9) is unchanged (ASCII-only – expected limitation)");
        else
            FAIL("UPPER(\\xe9) unexpected result", "byte 0x%02X", r ? (unsigned char)r[0] : 0);
    }
    free(r);

    /* Contrast: latin1_upper does convert it */
    r = sql_text_bind1(db, "SELECT latin1_upper(?1)", "\xe9", 1);
    EXPECT_INT("latin1_upper(\\xe9) = 0xC9 (correct)",
               r ? (unsigned char)r[0] : 0, 0xC9);
    free(r);

    /* SQLite's built-in LIKE with raw Latin1 bytes: bytes >= 0x80 that do not
     * form valid UTF-8 sequences are normalised to the Unicode replacement
     * character U+FFFD.  This means DIFFERENT Latin1 extended bytes can
     * appear equal to built-in LIKE – a critical problem for Latin1 data.
     * Here \\xc9 (lone UTF-8 2-byte lead) and \\xe9 (lone 3-byte lead) both
     * become U+FFFD, so built-in LIKE unexpectedly returns 1. */
    EXPECT_INT("built-in LIKE: \\xc9 LIKE \\xe9 = 1 (both →U+FFFD, wrong for Latin1)",
               sql_int_bind1(db,
                   "SELECT (?1 LIKE '\xe9')", "\xc9", 1), 1);

    /* Contrast: latin1_like handles it */
    EXPECT_INT("latin1_like: '\\xe9' ~ '\\xc9' = 1 (correct)",
               sql_int_bind1(db,
                   "SELECT latin1_like('\xc9', ?1)", "\xe9", 1), 1);

    /* BINARY collation: É != é */
    EXPECT_INT("BINARY: \\xc9 != \\xe9",
               sql_int_bind1(db,
                   "SELECT (?1 = '\xe9' COLLATE BINARY)", "\xc9", 1), 0);

    /* LATIN1_CI: É == é */
    EXPECT_INT("LATIN1_CI: \\xc9 == \\xe9",
               sql_int_bind1(db,
                   "SELECT (?1 = '\xe9' COLLATE LATIN1_CI)", "\xc9", 1), 1);
}

/* -------------------------------------------------------------------------
 * 8. NOCASE override – latin1_override_nocase()
 *
 * Opens a fresh in-memory connection so the override does not interfere
 * with the collation state of the shared db used by other test suites.
 * ------------------------------------------------------------------------- */
static void test_nocase_override(void) {
    sqlite3 *db2;
    int rc;
    printf("\n[NOCASE override – latin1_override_nocase()]\n");

    rc = sqlite3_open(":memory:", &db2);
    if (rc != SQLITE_OK) {
        FAIL("open fresh db for NOCASE test", "%s", sqlite3_errmsg(db2));
        sqlite3_close(db2);
        return;
    }

    rc = latin1_register(db2);
    if (rc != SQLITE_OK) {
        FAIL("latin1_register on fresh db", "%s", sqlite3_errmsg(db2));
        sqlite3_close(db2);
        return;
    }

    /* Before override: built-in NOCASE treats é != É */
    EXPECT_INT("before override: NOCASE \\xe9 != \\xc9",
               collation_cmp(db2, "NOCASE", "\xe9", 1, "\xc9", 1), 1);

    rc = latin1_override_nocase(db2);
    if (rc != SQLITE_OK) {
        FAIL("latin1_override_nocase()", "%s", sqlite3_errmsg(db2));
        sqlite3_close(db2);
        return;
    }
    EXPECT_INT("latin1_override_nocase() returns SQLITE_OK", rc, SQLITE_OK);

    /* After override: NOCASE now folds Latin1 accents */
    EXPECT_INT("after override: NOCASE \\xe9 == \\xc9 (é == É)",
               collation_cmp(db2, "NOCASE", "\xe9", 1, "\xc9", 1), 0);

    /* ASCII case-folding still works */
    EXPECT_INT("after override: NOCASE 'a' == 'A'",
               collation_cmp(db2, "NOCASE", "a", 1, "A", 1), 0);
    EXPECT_INT("after override: NOCASE 'abc' == 'ABC'",
               collation_cmp(db2, "NOCASE", "abc", 3, "ABC", 3), 0);

    /* Ordering is preserved: 'a'/'A' < 'b'/'B' */
    EXPECT_INT("after override: NOCASE 'a' < 'B'",
               collation_cmp(db2, "NOCASE", "a", 1, "B", 1), -1);

    /* Table with COLLATE NOCASE column picks up the override */
    sqlite3_exec(db2,
        "CREATE TABLE words (w TEXT COLLATE NOCASE);"
        "INSERT INTO words VALUES ('caf\xe9');"   /* café */
        "INSERT INTO words VALUES ('na\xefve');", /* naïve */
        NULL, NULL, NULL);

    EXPECT_INT("NOCASE column: caf\\xe9 == CAF\\xc9",
               sql_int_bind1(db2,
                   "SELECT COUNT(*) FROM words WHERE w = ?1",
                   "CAF\xc9", 4), 1);

    EXPECT_INT("NOCASE column: na\\xefve == NA\\xcfVE",
               sql_int_bind1(db2,
                   "SELECT COUNT(*) FROM words WHERE w = ?1",
                   "NA\xcfVE", 5), 1);

    sqlite3_close(db2);
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void) {
    sqlite3 *db;
    int rc;

    printf("=== sqlite-latin1 test suite ===\n");

    rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open in-memory database: %s\n",
                sqlite3_errmsg(db));
        return 1;
    }

    rc = latin1_register(db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "latin1_register() failed: %s\n",
                sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    test_collation_cs(db);
    test_collation_ci(db);
    test_upper(db);
    test_lower(db);
    test_like(db);
    test_table_collation(db);
    test_builtin_limitations(db);
    test_nocase_override();

    sqlite3_close(db);

    printf("\n=== Results: %d/%d passed", g_run - g_failed, g_run);
    if (g_failed)
        printf(", %d FAILED", g_failed);
    printf(" ===\n");

    return g_failed ? 1 : 0;
}
