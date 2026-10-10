# tree-sitter-pyrope — unified build & test entry point.
#
# Subsystems (each can be built/tested on its own; `make test` runs them all):
#   grammar   tree-sitter grammar (grammar.js -> src/parser.c) + editor queries
#   prpfmt    Pyrope formatter (C++20, links the generated parser) -> prpfmt/
#   prpparse  recursive-descent parser for LiveHD (bazel; design only so far)
#
# Quick start:
#   make            # regenerate the parser from grammar.js
#   make test       # canonical regression: grammar + prpfmt (must stay green)
#   make test-all   # also runs prpparse (design-only -> skipped until it builds)
#   make corpus     # rebuild the full_pyrope/ test corpus from ../docs

TS      := ./node_modules/tree-sitter-cli/tree-sitter
DOCS    ?= ../docs/docs/pyrope
CORPUS  := full_pyrope

.PHONY: all generate test test-all test-grammar test-prpfmt test-prpparse \
        fuzz-prpparse corpus prpfmt clean

all: generate

## ---------------------------------------------------------------- grammar ---
# Regenerate src/parser.c (and friends) from grammar.js.
generate:
	npm run generate

# Canonical regression check: parse every corpus file (prints nothing when all
# of full_pyrope/*.prp parse cleanly), then run the grammar's own unit corpus
# (test/corpus/*.txt: pinned trees for accepted forms, `:error` cases for the
# forms that must stay rejected).
test-grammar: generate
	@scripts/test.sh
	@# `tree-sitter test` passes vacuously on an empty/missing test/corpus/.
	@ls test/corpus/*.txt >/dev/null 2>&1 || \
	  { echo "test-grammar: no test/corpus/*.txt (the grammar unit corpus is missing)" >&2; exit 1; }
	@XDG_CACHE_HOME="$(CURDIR)/build/tree-sitter-cache" $(TS) test \
	  --config-path scripts/tree-sitter-config.json --overview-only

# Rebuild the full_pyrope/ corpus from the (non-deprecated) Pyrope docs.
# The leading rm clears stale snippets when the doc set shrinks.
corpus:
	rm -rf ./$(CORPUS)
	./scripts/extract.rb -d ./$(CORPUS)/ $(DOCS)/0*.md $(DOCS)/1*.md

## ----------------------------------------------------------------- prpfmt ---
# Depend on the root `generate` so a parallel `make test` regenerates the parser
# exactly once (shared prerequisite) instead of racing with test-grammar's own
# generate; the prpfmt sub-make then sees parser.c up-to-date and skips its copy.
prpfmt: generate
	$(MAKE) -C prpfmt

# Run prpfmt -v over the whole corpus (errors / AST validity / idempotency).
test-prpfmt: prpfmt
	$(MAKE) -C prpfmt test-api
	python3 prpfmt/tests/cli_test.py
	python3 prpfmt/tests/verify_all.py $(CORPUS)

## --------------------------------------------------------------- prpparse ---
# Extra bazel flags for every prpparse build/test (e.g. an xcode_config override
# when the local Xcode install is broken): make test-prpparse BAZEL_FLAGS='…'
BAZEL_FLAGS ?=

test-prpparse:
	@if [ -f prpparse/BUILD ] || [ -f prpparse/BUILD.bazel ]; then \
	  bazel test $(BAZEL_FLAGS) //prpparse/... && BAZEL_FLAGS='$(BAZEL_FLAGS)' scripts/test_prpparse.sh ; \
	else \
	  echo "prpparse: no build yet (design in prpparse/plan.md) — skipping"; \
	fi

# Syntax-error fuzzer: mutate corpus files, use tree-sitter as the syntax oracle.
# Hard gate (non-zero exit): prpparse must never reject tree-sitter-valid syntax.
# Also reports syntax-error capture rate + error-span localization quality.
fuzz-prpparse:
	bazel build $(BAZEL_FLAGS) //prpparse:prpparse_cli
	python3 prpparse/tests/fuzz.py

## --------------------------------------------------------------- aggregate --
# `test` is the gate that must stay green: the grammar + formatter regressions.
test: test-grammar test-prpfmt

# `test-all` additionally runs prpparse (design-only -> skipped until it builds).
test-all: test test-prpparse

clean:
	$(MAKE) -C prpfmt clean
