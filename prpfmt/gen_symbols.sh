#!/bin/sh
# Auto-generate the tree-sitter symbol-id table used by the formatter.
#
# prpfmt dispatches on ts_node_grammar_symbol(), comparing against the numeric
# symbol ids that `tree-sitter generate` bakes into src/parser.c. Those ids are
# RENUMBERED on every grammar change, so hand-maintaining a copy (as prpfmt.h
# used to) silently drifts and makes the formatter emit garbage. Instead we
# lift the authoritative `enum ts_symbol_identifiers` straight out of the
# generated parser at build time.
#
# Usage: gen_symbols.sh <parser.c> <out.h>
set -e

PARSER="${1:-../src/parser.c}"
OUT="${2:-ts_symbols.h}"

if [ ! -f "$PARSER" ]; then
  echo "gen_symbols.sh: parser not found: $PARSER" >&2
  exit 1
fi

{
  echo "/* AUTO-GENERATED from src/parser.c by gen_symbols.sh -- DO NOT EDIT. */"
  echo "#ifndef PRPFMT_TS_SYMBOLS_H"
  echo "#define PRPFMT_TS_SYMBOLS_H"
  sed -n '/^enum ts_symbol_identifiers {/,/^};/p' "$PARSER"
  echo "#endif /* PRPFMT_TS_SYMBOLS_H */"
} > "$OUT"

if ! grep -q 'sym_identifier' "$OUT"; then
  echo "gen_symbols.sh: failed to extract enum from $PARSER" >&2
  exit 1
fi
