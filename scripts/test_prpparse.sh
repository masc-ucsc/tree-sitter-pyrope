#!/bin/bash
# prpparse accept-parity regression: build the CLI and parse every corpus file.
# Exits non-zero if any full_pyrope/*.prp fails to parse (the accept gate that
# must match tree-sitter's `scripts/test.sh`).
set -euo pipefail

cd "$(dirname "$0")/.."

CORPUS=full_pyrope
CLI=bazel-bin/prpparse/prpparse_cli

if ! ls "$CORPUS"/*.prp >/dev/null 2>&1; then
  echo "prpparse: no corpus ($CORPUS/*.prp) — run 'make corpus' first" >&2
  exit 1
fi

echo "prpparse: building CLI..."
bazel build //prpparse:prpparse_cli >/dev/null 2>&1

total=0
fail=0
for f in "$CORPUS"/*.prp; do
  total=$((total + 1))
  if ! "$CLI" --parse "$f" >/dev/null 2>&1; then
    fail=$((fail + 1))
    echo "FAIL: $f"
    "$CLI" --parse "$f" 2>&1 >/dev/null | sed 's/^/    /'
  fi
done

echo "========================================"
echo "prpparse accept-parity"
echo "Total Files:  $total"
echo "Passed:       $((total - fail))"
echo "Failed:       $fail"
echo "========================================"
[ "$fail" -eq 0 ]
