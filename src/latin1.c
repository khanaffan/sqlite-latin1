/*
 * latin1.c - SQLite Latin1 (ISO-8859-1) collation and scalar functions
 *
 * Compile with -DSQLITE_CORE when linking statically against libsqlite3
 * (e.g. for unit tests).  Omit the flag when building as a loadable extension.
 */

#ifdef SQLITE_CORE
#  include "sqlite3.h"
#else
#  include "sqlite3ext.h"
   SQLITE_EXTENSION_INIT1
#endif

#include "latin1.h"
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * ISO-8859-1 case-conversion lookup tables (256 entries each)
 * ------------------------------------------------------------------------- */

/* Maps each Latin1 byte to its uppercase equivalent.
 * Unchanged:  0x00-0x60, 0x7B-0xBF, 0xD7(×), 0xDF(ß), 0xFF(ÿ – no Latin1 upper)
 * a-z       → A-Z   (0x61-0x7A → 0x41-0x5A)
 * à-ö       → À-Ö   (0xE0-0xF6 → 0xC0-0xD6)
 * ÷         → ÷     (0xF7 – not a letter)
 * ø-þ       → Ø-Þ   (0xF8-0xFE → 0xD8-0xDE)
 */
static const unsigned char latin1_to_upper[256] = {
    /* 0x00-0x0F */  0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    /* 0x10-0x1F */  0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
    /* 0x20-0x2F */  0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,
    /* 0x30-0x3F */  0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
    /* 0x40-0x4F */  0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,
    /* 0x50-0x5F */  0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,
    /* 0x60-0x6F */  0x60,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,
    /* 0x70-0x7F */  0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x7B,0x7C,0x7D,0x7E,0x7F,
    /* 0x80-0x8F */  0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,
    /* 0x90-0x9F */  0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,
    /* 0xA0-0xAF */  0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
    /* 0xB0-0xBF */  0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
    /* 0xC0-0xCF */  0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,
    /* 0xD0-0xDF */  0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,
    /* 0xE0-0xEF */  0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,
    /* 0xF0-0xFF */  0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xFF,
};

/* Maps each Latin1 byte to its lowercase equivalent.
 * Unchanged:  0x00-0x40, 0x5B-0xBF, 0xD7(×), 0xDF(ß already lower), 0xE0-0xFF
 * A-Z         → a-z   (0x41-0x5A → 0x61-0x7A)
 * À-Ö         → à-ö   (0xC0-0xD6 → 0xE0-0xF6)
 * ×           → ×     (0xD7 – not a letter)
 * Ø-Þ         → ø-þ   (0xD8-0xDE → 0xF8-0xFE)
 * ß           → ß     (0xDF – already lowercase, no single-char upper in Latin1)
 */
static const unsigned char latin1_to_lower[256] = {
    /* 0x00-0x0F */  0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    /* 0x10-0x1F */  0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
    /* 0x20-0x2F */  0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,
    /* 0x30-0x3F */  0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
    /* 0x40-0x4F */  0x40,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,
    /* 0x50-0x5F */  0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x5B,0x5C,0x5D,0x5E,0x5F,
    /* 0x60-0x6F */  0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,
    /* 0x70-0x7F */  0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,0x7F,
    /* 0x80-0x8F */  0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,
    /* 0x90-0x9F */  0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,
    /* 0xA0-0xAF */  0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
    /* 0xB0-0xBF */  0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
    /* 0xC0-0xCF */  0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
    /* 0xD0-0xDF */  0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xD7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xDF,
    /* 0xE0-0xEF */  0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
    /* 0xF0-0xFF */  0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF,
};

/* -------------------------------------------------------------------------
 * Collation callbacks
 * ------------------------------------------------------------------------- */

/*
 * LATIN1 – case-sensitive, byte-order comparison.
 * Equivalent to BINARY for Latin1 data; provided for explicit intent.
 */
static int latin1_collate(
    void *p, int nA, const void *zA, int nB, const void *zB
) {
    const unsigned char *a = (const unsigned char *)zA;
    const unsigned char *b = (const unsigned char *)zB;
    int n = nA < nB ? nA : nB;
    int i;
    (void)p;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return nA - nB;
}

/*
 * LATIN1_CI – case-insensitive comparison.
 * Folds both strings to uppercase using the Latin1 table, then compares
 * byte-by-byte.  'é' == 'É', 'ü' == 'Ü', etc.
 */
static int latin1_collate_ci(
    void *p, int nA, const void *zA, int nB, const void *zB
) {
    const unsigned char *a = (const unsigned char *)zA;
    const unsigned char *b = (const unsigned char *)zB;
    int n = nA < nB ? nA : nB;
    int i;
    (void)p;
    for (i = 0; i < n; i++) {
        unsigned char ua = latin1_to_upper[a[i]];
        unsigned char ub = latin1_to_upper[b[i]];
        if (ua != ub) return (int)ua - (int)ub;
    }
    return nA - nB;
}

/* -------------------------------------------------------------------------
 * Scalar function helpers
 * ------------------------------------------------------------------------- */

/* Apply a case table to every byte and return a new TEXT value. */
static void apply_case_table(
    sqlite3_context *ctx,
    sqlite3_value   *val,
    const unsigned char *tbl
) {
    const unsigned char *in;
    unsigned char *out;
    int len, i;

    if (sqlite3_value_type(val) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    /* Use the raw blob pointer to avoid UTF-8 re-encoding side-effects;
     * for Latin1 data stored as raw bytes, blob == text bytes. */
    in  = (const unsigned char *)sqlite3_value_blob(val);
    len = sqlite3_value_bytes(val);

    if (!in || len == 0) {
        sqlite3_result_text(ctx, "", 0, SQLITE_STATIC);
        return;
    }

    out = (unsigned char *)sqlite3_malloc(len + 1);
    if (!out) { sqlite3_result_error_nomem(ctx); return; }

    for (i = 0; i < len; i++) out[i] = tbl[in[i]];
    out[len] = '\0';

    sqlite3_result_text(ctx, (char *)out, len, sqlite3_free);
}

static void latin1_upper_func(
    sqlite3_context *ctx, int argc, sqlite3_value **argv
) {
    (void)argc;
    apply_case_table(ctx, argv[0], latin1_to_upper);
}

static void latin1_lower_func(
    sqlite3_context *ctx, int argc, sqlite3_value **argv
) {
    (void)argc;
    apply_case_table(ctx, argv[0], latin1_to_lower);
}

/* -------------------------------------------------------------------------
 * LIKE implementation
 *
 * latin1_like(pattern, text [, escape])
 *
 * Pattern metacharacters (same as standard SQL LIKE):
 *   %   matches any sequence of zero or more characters
 *   _   matches exactly one character
 *
 * All other characters are compared case-insensitively using the Latin1
 * uppercase table.
 *
 * Note: overriding the built-in 'like' function is possible by registering
 * this as 'like' with sqlite3_create_function().  That is intentionally NOT
 * done here to avoid unintended side-effects; call latin1_like() explicitly.
 * ------------------------------------------------------------------------- */

static int latin1_like_impl(
    const unsigned char *pat, int plen,
    const unsigned char *str, int slen,
    unsigned char esc
) {
    int pi = 0, si = 0;

    while (pi < plen) {
        unsigned char pc = pat[pi];

        if (esc && pc == esc && pi + 1 < plen) {
            /* escaped metacharacter – match literally */
            pc = pat[++pi];
            if (si >= slen) return 0;
            if (latin1_to_upper[str[si]] != latin1_to_upper[pc]) return 0;
            pi++; si++;

        } else if (pc == '%') {
            /* skip consecutive % */
            while (pi < plen && pat[pi] == '%') pi++;
            if (pi >= plen) return 1; /* trailing % matches rest */
            /* try matching the remaining pattern at every position */
            for (; si <= slen; si++) {
                if (latin1_like_impl(pat + pi, plen - pi,
                                     str + si, slen - si, esc))
                    return 1;
            }
            return 0;

        } else if (pc == '_') {
            if (si >= slen) return 0;
            pi++; si++;

        } else {
            if (si >= slen) return 0;
            if (latin1_to_upper[str[si]] != latin1_to_upper[pc]) return 0;
            pi++; si++;
        }
    }
    return si == slen;
}

static void latin1_like_func(
    sqlite3_context *ctx, int argc, sqlite3_value **argv
) {
    const unsigned char *pat, *str;
    int plen, slen;
    unsigned char esc = 0;

    if (sqlite3_value_type(argv[0]) == SQLITE_NULL ||
        sqlite3_value_type(argv[1]) == SQLITE_NULL) {
        sqlite3_result_null(ctx);
        return;
    }

    pat  = (const unsigned char *)sqlite3_value_blob(argv[0]);
    str  = (const unsigned char *)sqlite3_value_blob(argv[1]);
    plen = sqlite3_value_bytes(argv[0]);
    slen = sqlite3_value_bytes(argv[1]);

    if (argc == 3 && sqlite3_value_type(argv[2]) != SQLITE_NULL) {
        int elen = sqlite3_value_bytes(argv[2]);
        if (elen == 1) {
            const unsigned char *ep =
                (const unsigned char *)sqlite3_value_blob(argv[2]);
            esc = ep ? ep[0] : 0;
        }
    }

    sqlite3_result_int(ctx,
        latin1_like_impl(pat  ? pat  : (const unsigned char *)"", plen,
                         str  ? str  : (const unsigned char *)"", slen,
                         esc));
}

/* -------------------------------------------------------------------------
 * Public registration function
 * ------------------------------------------------------------------------- */

int latin1_register(sqlite3 *db) {
    int rc;

    rc = sqlite3_create_collation(db, "LATIN1", SQLITE_UTF8,
                                  NULL, latin1_collate);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_create_collation(db, "LATIN1_CI", SQLITE_UTF8,
                                  NULL, latin1_collate_ci);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_create_function(db, "latin1_upper", 1,
                                 SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                 NULL, latin1_upper_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_create_function(db, "latin1_lower", 1,
                                 SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                 NULL, latin1_lower_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    /* Register latin1_like with 2 and 3 arguments */
    rc = sqlite3_create_function(db, "latin1_like", 2,
                                 SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                 NULL, latin1_like_func, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    rc = sqlite3_create_function(db, "latin1_like", 3,
                                 SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                 NULL, latin1_like_func, NULL, NULL);
    return rc;
}

/* -------------------------------------------------------------------------
 * Loadable extension entry point
 * Not compiled when SQLITE_CORE is defined (static-link builds).
 * ------------------------------------------------------------------------- */

#ifndef SQLITE_CORE
#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_latin1_init(
    sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi
) {
    SQLITE_EXTENSION_INIT2(pApi);
    (void)pzErrMsg;
    return latin1_register(db);
}
#endif /* SQLITE_CORE */
