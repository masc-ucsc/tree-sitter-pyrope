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
# BAZEL_FLAGS (optional, word-split on purpose) reaches the build, e.g. an
# xcode_config override on a machine whose Xcode install is broken. The build
# log is shown only when the build fails; the script used to exit silently.
# shellcheck disable=SC2086
if ! build_log=$(bazel build ${BAZEL_FLAGS:-} //prpparse:prpparse_cli 2>&1); then
  echo "prpparse: bazel build failed:" >&2
  printf '%s\n' "$build_log" | tail -n 40 >&2
  exit 1
fi

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
