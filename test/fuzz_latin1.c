/*
 * fuzz_latin1.c – LibFuzzer entry point for the Latin1 SQLite extension.
 *
 * Build (standalone):
 *   clang -fsanitize=fuzzer,address -DSQLITE_CORE \
 *         -Isrc -I$(sqlite3-include) \
 *         src/latin1.c test/fuzz_latin1.c $(sqlite3.a) -o fuzz_latin1
 *
 * Build via CMake (requires clang with LibFuzzer):
 *   cmake -B build -DENABLE_FUZZING=ON \
 *         -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang
 *   cmake --build build --target fuzz_latin1
 *   ./build/fuzz_latin1 -max_total_time=60
 *
 * The fuzzer splits the fuzz input at the first 0x00 byte to produce two
 * independent byte strings fed as (pattern, text) to latin1_like().  Both
 * halves and the raw input are also exercised with latin1_upper/lower and
 * the LATIN1 / LATIN1_CI collations.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sqlite3.h"
#include "latin1.h"

/* Shared in-memory database, opened once per process. */
static sqlite3 *g_db = NULL;

/* Prepared statements reused across iterations for performance. */
static sqlite3_stmt *g_like_stmt       = NULL;
static sqlite3_stmt *g_upper_stmt      = NULL;
static sqlite3_stmt *g_lower_stmt      = NULL;
static sqlite3_stmt *g_collate_cs_stmt = NULL;
static sqlite3_stmt *g_collate_ci_stmt = NULL;

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;

    if (sqlite3_open(":memory:", &g_db) != SQLITE_OK)
        return 0;
    if (latin1_register(g_db) != SQLITE_OK)
        return 0;

    sqlite3_prepare_v2(g_db,
        "SELECT latin1_like(?1, ?2)", -1, &g_like_stmt, NULL);
    sqlite3_prepare_v2(g_db,
        "SELECT latin1_upper(?1), latin1_lower(?1)", -1, &g_upper_stmt, NULL);
    sqlite3_prepare_v2(g_db,
        "SELECT latin1_lower(?1)", -1, &g_lower_stmt, NULL);
    sqlite3_prepare_v2(g_db,
        "SELECT ?1 < ?2 COLLATE LATIN1", -1, &g_collate_cs_stmt, NULL);
    sqlite3_prepare_v2(g_db,
        "SELECT ?1 = ?2 COLLATE LATIN1_CI", -1, &g_collate_ci_stmt, NULL);

    return 0;
}

/* Run a prepared statement with up to two TEXT parameters then reset it. */
static void run_stmt(sqlite3_stmt *stmt,
                     const char *a, int alen,
                     const char *b, int blen) {
    if (!stmt) return;
    sqlite3_bind_text(stmt, 1, a, alen, SQLITE_STATIC);
    if (b)
        sqlite3_bind_text(stmt, 2, b, blen, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (!g_db || size == 0)
        return 0;

    const char *buf = (const char *)data;

    /* Find the first NUL byte to split into (pattern, text). */
    size_t split = size;
    for (size_t i = 0; i < size; i++) {
        if (data[i] == 0x00) { split = i; break; }
    }
    /* If no NUL, bisect at midpoint. */
    if (split == size) split = size / 2;

    const char *pat  = buf;
    int         plen = (int)split;
    const char *str  = buf + split + (split < size ? 1 : 0);
    int         slen = (int)(size - (size_t)plen - (split < size ? 1u : 0u));

    /* latin1_like(pattern, text) */
    run_stmt(g_like_stmt, pat, plen, str, slen);

    /* latin1_upper(?), latin1_lower(?) – full input */
    run_stmt(g_upper_stmt, buf, (int)size, NULL, 0);
    run_stmt(g_lower_stmt, buf, (int)size, NULL, 0);

    /* LATIN1 (case-sensitive) collation comparison */
    run_stmt(g_collate_cs_stmt, pat, plen, str, slen);

    /* LATIN1_CI (case-insensitive) collation comparison */
    run_stmt(g_collate_ci_stmt, pat, plen, str, slen);

    /* Also exercise each half independently through upper/lower. */
    run_stmt(g_upper_stmt, pat, plen, NULL, 0);
    run_stmt(g_upper_stmt, str, slen, NULL, 0);

    return 0;
}
