#!/bin/bash

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Keep tests independent of the user's Tree-sitter configuration and cache.
export XDG_CACHE_HOME="$PWD/build/tree-sitter-cache"
shopt -s nullglob
files=(full_pyrope/*.prp)
if ((${#files[@]} == 0)); then
  echo "test-grammar: no full_pyrope/*.prp (run make corpus first)" >&2
  exit 1
fi

# One invocation loads the parser once and fails if ANY file has an error.
exec ./node_modules/tree-sitter-cli/tree-sitter parse \
  --config-path scripts/tree-sitter-config.json -q "${files[@]}"
