#ifndef PRP_FMT_API_H
#define PRP_FMT_API_H

/*
 * prpfmt_api.h — the embeddable entry point for the Pyrope formatter.
 *
 * This is the only header an embedder (e.g. LiveHD's `lhd pyrope fmt`) needs:
 * it pulls in no tree-sitter types, so a C++ consumer can call the formatter
 * without taking a dependency on <tree_sitter/api.h>. The standalone CLI
 * (main.c) drives the lower-level prpfmt.h primitives directly.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Format `len` bytes of Pyrope `src`.
 *
 * Returns:
 *   0  success — *out_buf is a malloc'd, NUL-terminated formatted buffer
 *      (the caller frees it with free()) and *out_len its length.
 *   2  the input did not parse (ERROR/MISSING nodes) — *out_buf == NULL.
 *   3  verify != 0 and the formatted output failed to re-parse. The formatted
 *      buffer is still returned (so a caller can print it), but the result is
 *      flagged as unsafe.
 *   1  an internal allocation failure — *out_buf == NULL.
 *
 * `indent_size` (spaces per level) and `max_width` (wrap column) mirror the
 * CLI -i/--indent and -w/--width knobs. Passing 0 or a negative value keeps
 * prpfmt's built-in defaults (4 and 80).
 */
int prpfmt_format_string(const char *src, size_t len, int indent_size, int max_width, int verify, char **out_buf,
                         size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* PRP_FMT_API_H */
