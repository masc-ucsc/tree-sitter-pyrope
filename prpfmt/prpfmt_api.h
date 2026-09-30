#ifndef PRP_FMT_API_H
#define PRP_FMT_API_H

/*
 * prpfmt_api.h — the embeddable entry point for the Pyrope formatter.
 *
 * This is the only header an embedder (e.g. LiveHD's `lhd pyrope fmt`) needs:
 * it pulls in no tree-sitter types, so a C++ consumer can call the formatter
 * without taking a dependency on <tree_sitter/api.h>. The standalone CLI
 * (main.cc) drives the lower-level prpfmt.h primitives directly.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PrpfmtMode {
  PRPFMT_AI = 0,
  PRPFMT_HUMAN = 1
} PrpfmtMode;

/*
 * Format `len` bytes of Pyrope `src`.
 *
 * Returns:
 *   0  success — *out_buf is a malloc'd, NUL-terminated formatted buffer
 *      (the caller frees it with free()) and *out_len its length.
 *   2  the input did not parse (ERROR/MISSING nodes). No formatting is
 *      attempted, even with verify == 0; *out_buf == NULL, *out_len == 0.
 *      The caller reports the parse failure to the user.
 *   3  verify != 0 and the formatted output failed to re-parse. The formatted
 *      buffer is still returned (so a caller can print it), but the result is
 *      flagged as unsafe.
 *   1  an internal allocation failure — *out_buf == NULL.
 *   4  the input nests too deeply (a tree deeper than 200000 levels) to be
 *      formatted safely — *out_buf == NULL.
 *
 * `indent_size` (spaces per level) and `max_width` (soft width target) mirror the
 * CLI --indent and --width knobs. (There are no short spellings: `-w` is not an
 * option, and `-i` is --inplace, which REWRITES the input file.) Passing 0 or a
 * negative value keeps prpfmt's built-in defaults (2 and 132). This entry point
 * defaults to AI mode (no width limit or vertical alignment). Use the mode-aware
 * entry point for Human layout; max_width only applies in Human mode.
 */
int prpfmt_format_string(const char *src, size_t len, int indent_size, int max_width, int verify, char **out_buf,
                         size_t *out_len);

int prpfmt_format_string_mode(const char *src, size_t len, int indent_size, int max_width, PrpfmtMode mode,
                             int verify, char **out_buf, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* PRP_FMT_API_H */
