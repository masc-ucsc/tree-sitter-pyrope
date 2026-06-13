#!/bin/bash
# Differential tree-shape oracle: compare prpparse `--sexp` against tree-sitter
# for a corpus file (or summarise the whole corpus).
#
#   prpparse/tests/diff_oracle.sh full_pyrope/file1.prp   # side-by-side one file
#   prpparse/tests/diff_oracle.sh                         # corpus node-count report
#
# Exact `--sexp` equality is NOT a goal (plan decision 1): prpparse omits some
# tree-sitter wrapper nodes (constant, assignment_operator, binary_*_op, ...).
# This tool measures how close the trees are (named-node counts) and lets you
# eyeball structural differences per file.
set -euo pipefail
cd "$(dirname "$0")/../.."

TS=./node_modules/tree-sitter-cli/tree-sitter
CLI=bazel-bin/prpparse/prpparse_cli
[ -x "$CLI" ] || bazel build //prpparse:prpparse_cli >/dev/null 2>&1

# count named nodes in a tree-sitter `parse` dump (lines like `(node [r,c]...`)
ts_nodes() { $TS parse "$1" 2>/dev/null | grep -oE '\([a-z_]+' | wc -l | tr -d ' '; }
# count nodes in a prpparse --sexp dump (lines like `(kind` or `field: (kind`)
pp_nodes() { $CLI --sexp "$1" 2>/dev/null | grep -oE '\([a-z_]+' | wc -l | tr -d ' '; }

if [ $# -ge 1 ]; then
  f="$1"
  echo "=== tree-sitter: $f ==="; $TS parse "$f" | sed -E 's/ \[[0-9]+, [0-9]+\] - \[[0-9]+, [0-9]+\]//'
  echo "=== prpparse:    $f ==="; $CLI --sexp "$f"
  exit 0
fi

printf '%-22s %8s %8s %7s\n' file ts pp ratio
tot_ts=0; tot_pp=0
for f in full_pyrope/*.prp; do
  ts=$(ts_nodes "$f"); pp=$(pp_nodes "$f")
  tot_ts=$((tot_ts + ts)); tot_pp=$((tot_pp + pp))
done
echo "----------------------------------------------"
printf 'corpus total           %8d %8d %6.2f%%\n' "$tot_ts" "$tot_pp" \
  "$(awk "BEGIN{print ($tot_ts? 100.0*$tot_pp/$tot_ts : 0)}")"
echo "(prpparse omits tree-sitter wrapper nodes by design; <100% is expected)"
