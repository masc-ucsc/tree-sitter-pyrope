# tree-sitter-pyrope — unified build & test entry point.
#
# Subsystems (each can be built/tested on its own; `make test` runs them all):
#   grammar   tree-sitter grammar (grammar.js -> src/parser.c) + editor queries
#   prpfmt    Pyrope formatter (C, links the generated parser) -> prpfmt/
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
        corpus prpfmt clean

all: generate

## ---------------------------------------------------------------- grammar ---
# Regenerate src/parser.c (and friends) from grammar.js.
generate:
	npm run generate

# Canonical regression check: parse every corpus file. Prints nothing and
# exits 0 when all of full_pyrope/*.prp parse cleanly.
test-grammar: generate
	@scripts/test.sh

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
	python3 prpfmt/tests/verify_all.py $(CORPUS)

## --------------------------------------------------------------- prpparse ---
test-prpparse:
	@if [ -f prpparse/BUILD ] || [ -f prpparse/BUILD.bazel ]; then \
	  bazel test //prpparse/... && scripts/test_prpparse.sh ; \
	else \
	  echo "prpparse: no build yet (design in prpparse/plan.md) — skipping"; \
	fi

## --------------------------------------------------------------- aggregate --
# `test` is the gate that must stay green: the grammar + formatter regressions.
test: test-grammar test-prpfmt

# `test-all` additionally runs prpparse (design-only -> skipped until it builds).
test-all: test test-prpparse

clean:
	$(MAKE) -C prpfmt clean
