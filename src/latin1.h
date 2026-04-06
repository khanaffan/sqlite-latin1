/*
 * latin1.h - SQLite Latin1 (ISO-8859-1) collation and scalar functions
 *
 * What Latin1 affects in SQLite (and what this extension addresses):
 *
 *   1. Collation / ORDER BY / WHERE comparisons
 *      SQLite's default BINARY collation sorts by raw byte value, which is
 *      correct byte-order for Latin1 but does not case-fold accented letters.
 *      → Collations LATIN1 (case-sensitive) and LATIN1_CI (case-insensitive)
 *
 *   2. UPPER / LOWER scalar functions
 *      SQLite's built-in UPPER/LOWER handle only ASCII (A-Z).
 *      UPPER('é') returns 'é' unchanged.
 *      → latin1_upper() and latin1_lower() cover the full ISO-8859-1 range
 *
 *   3. LIKE operator
 *      SQLite's built-in LIKE is case-insensitive only for ASCII letters.
 *      'é' LIKE 'É' is false by default.
 *      → latin1_like(pattern, text [, escape]) covers Latin1 letters
 *
 *   4. = (equality) comparisons
 *      Equality uses the column's declared (or per-expression) collation.
 *      Declaring a column COLLATE LATIN1_CI makes 'é' = 'É' true.
 *
 *   5. Index usage
 *      SQLite uses an index only when the query collation matches the index
 *      collation. Columns declared COLLATE LATIN1_CI benefit from indexes
 *      only when the same collation is in effect.
 *
 * Usage – static linkage:
 *   #include "latin1.h"
 *   int rc = latin1_register(db);
 *
 * Usage – loadable extension:
 *   .load ./latin1
 *   or: SELECT load_extension('./latin1');
 */

#ifndef LATIN1_H
#define LATIN1_H

#include <sqlite3.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register all Latin1 collations and scalar functions with db.
 *
 * Collations registered:
 *   LATIN1     – case-sensitive byte-order comparison for Latin1 data
 *   LATIN1_CI  – case-insensitive comparison for Latin1 data
 *
 * Scalar functions registered:
 *   latin1_upper(text)                  – uppercase using ISO-8859-1 rules
 *   latin1_lower(text)                  – lowercase using ISO-8859-1 rules
 *   latin1_like(pattern, text)          – LIKE matching, Latin1 case-insensitive
 *   latin1_like(pattern, text, escape)  – same with custom escape character
 *
 * Returns SQLITE_OK on success, or a SQLite error code.
 */
int latin1_register(sqlite3 *db);

/*
 * Override SQLite's built-in NOCASE collation with the Latin1
 * case-insensitive collation (LATIN1_CI).
 *
 * SQLite's default NOCASE only folds ASCII A-Z; accented characters such as
 * 'é' and 'É' compare as unequal.  After calling this function, any column
 * declared COLLATE NOCASE, or any expression using COLLATE NOCASE, will use
 * Latin1-aware case folding instead.
 *
 * Must be called after latin1_register() (or at least after the database
 * connection is opened).  The override applies only to the given connection.
 *
 * Returns SQLITE_OK on success, or a SQLite error code.
 */
int latin1_override_nocase(sqlite3 *db);

#ifdef __cplusplus
}
#endif

#endif /* LATIN1_H */
