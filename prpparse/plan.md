# prpparse — implementation plan

A hand-written recursive-descent Pyrope parser, built to replace tree-sitter as
the LiveHD front-end. Goal: **fast AND very good error messages**. It uses the
in-repo `grammar.js` / generated tree-sitter parser as a **differential oracle**
(prpparse accepts exactly what grammar.js accepts), which stays in this repo
throughout.

The end consumer is `../livehd/inou/prp/prp2lnast.cpp`, which lowers the parse
tree into LNAST (LiveHD's IR). The central design realization (see
"Architecture" below) is that **prp2lnast reads the parse tree in a single
forward walk**, so the parse tree never needs to exist in full — it can be
streamed one top-level construct at a time.

---

## Current status — built and verified

Phases 0–3 are done; the parser is complete and regression-green.

- **Builds** under bazel (repo-root `MODULE.bazel`; `bazel test //prpparse/...`).
  hhds is a `bazel_dep` with `git_override` (pinned) / commented
  `local_path_override(../hhds)` for co-development.
- **Lexer prepass** (`lexer.{hpp,cpp}`, X-macro `*.def` token tables): single
  pass → flat `std::vector<Token>` (string_view only), line-start table, comment
  spans, and depth-0 split points. Lexer errors go through the same `Diag` path.
- **Tree schema** (`prp_nodes.def` 141 kinds, `prp_fields.def` 57 fields, from
  `src/node-types.json`): packed into the hhds node `Type` (uint16 = kind:10 |
  field:6 — a node has one role under its parent). Text is never stored; spans
  recover `string_view`s from the buffer.
- **Recursive-descent parser** (`parser.{hpp,cpp}`): mirrors grammar.js rule
  names; precedence-climbing expressions with flat same-tier chains; the
  paren-list classifier (tuple / arg_tuple / paren_group / lvalue); suffix
  chains; control flow; lambdas / `type` / `impl` / `enum` / `import` / `test` /
  `spawn`; `parse_type` mode; interpolated-string sub-parsing.
- **Accept-parity**: all **428** `full_pyrope/*.prp` parse (oracle parity with
  tree-sitter). `make test` (grammar + prpfmt 428/428) and `make test-all`
  (+ prpparse unit tests + accept-parity) are green.
- **Fuzzing** (`make fuzz-prpparse`): mutate valid corpus files, tree-sitter as
  the syntax oracle — hard gate of 0 false positives (never reject valid
  syntax); ~98% syntax-error capture, median 1 B span localization. Catching
  *every* syntax error is not required (inou/prp catches the rest).
- **Performance** (1M-line / 9M-node `../livehd/yy.prp`): materialize
  **3.36 s → ~0.14 s** (~24×), end-to-end **4.84 s → ~0.65 s**. Achieved by
  making srcid lazy (see below). hhds gained two **additive** helpers used off
  the critical path: `Source_locator::span_minter(path)` (precomputed path hash +
  interned file id; ids byte-identical to `mint`) and `reserve()`.
- **CLI** (`tests/cli.cpp`): default parse (diagnostics only), `--sexp`,
  `--srcmap`, `--tokens`, `--lex`, `--fuzz`, `--time`.

Deferred: large-file splitting / parallelism (Phase S); in-parser semantic
narrowing diagnostics for the `grammar_overparse.md` cases (default stays
tree-sitter-parity).

---

## Design decisions (stable)

1. **Tree shape = CST, not LNAST.** Node kinds mirror the grammar node names
   (`assignment`, `tuple`, `arg_tuple`, `if_expression`, …). Exact tree-sitter
   `--sexp` equality is not required; close-enough CST shape keeps the
   prp2lnast port mechanical. `prp_nodes.def` was generated once from
   `src/node-types.json` (regen via `tools/gen_nodes.py`) and hand-maintained.
2. **Node payload = `Type` (uint16) only.** kind (10b) + field (6b); the field
   is the node's edge role under its parent (replaces
   `ts_node_child_by_field_name`). No text stored.
3. **Fail-fast errors.** The first syntax error builds one rich `Diag`
   (code / category / message / span / hint / notes, shape-compatible with
   `../livehd/core/diag.hpp`) and aborts via `Parse_error`. No recovery, no
   ERROR/MISSING nodes. Unclosed brackets carry an "opened here" note. The CLI
   renders clang-style text from the lexer line table.
   *Consequence used below:* a prpparse tree is always well-formed, so
   prp2lnast's tree-sitter-era `check_parse_errors()` pre-walk is moot.
4. **Virtual semicolons in the parser, not the lexer.** The lexer tags each
   token `newline_before`; the parser, only at statement-end positions, applies
   the exact `src/scanner.c` rule (terminator at EOF, before `}`, or at a
   newline unless the next token continues the expression). Reproduces the
   tree-sitter valid_symbols handshake.
5. **Soft keywords.** The lexer marks every word `identifier` + a keyword id;
   the parser decides contextually (`enum(...)` call, `const type = 1`,
   `.[comptime]`). Hard-reserved list in `prp_tokens.def`.
6. **Overparse parity.** Accept exactly what grammar.js accepts (incl.
   `grammar_overparse.md`); semantic narrowing stays in livehd. grammar.js is
   the fixed differential oracle (targeted bug fixes only).
7. **Negative literals**: lexer always emits `-` as an operator; the parser
   folds the sign into the literal at operand position.
8. **Interpolated strings**: lexer emits one brace-depth-aware raw token; the
   parser sub-lexes `{expr}` regions on demand with absolute offsets.
9. **Source provenance is lazy.** `srcid = rapidhash(path, start, end)` is a
   pure function of the span, and diagnostics need a *span*, not a srcid — so
   nothing mints eagerly. `materialize` records each spanned node's raw byte
   span in a flat side-table; `Prp_tree::build_srcmap_async()` mints every span
   into the `Source_locator` on one worker thread meant to overlap the next
   pass, and `locator()` joins it (or builds synchronously). Parse-only runs pay
   nothing. (Replaces the original eager per-node mint — ~24× slower.)

---

## Architecture — stream statements, don't build a whole parse tree

**The finding.** In `prp2lnast.cpp`, the parse tree is consumed in exactly one
place — `walk_statement_block(root)` inside `process_description()`. Everything
else operates on the **LNAST**, not the parse tree:

```
walk_statement_block(root);   // the ONLY parse-tree read (single forward walk)
check_undeclared_writes();    // on the LNAST being built
check_undefined_reads();      // on the LNAST
rewrite_decls_to_declare();   // folds declaration clusters in the LNAST
builder.stabilize_fallback_tmps();  // "2p": on the LNAST
```

and `check_parse_errors()` (the up-front whole-tree error scan) only exists
because tree-sitter returns a tree full of ERROR/MISSING nodes for bad input —
prpparse fails fast (decision 3), so it is moot. **Therefore the parse tree only
needs to exist one top-level construct at a time.** Building the whole tree —
let alone two (the intermediate `Ast` and the materialized hhds tree) — is
avoidable.

**The design.** A *statement-stream seam*: the parser yields one top-level
construct at a time; the consumer processes it and the chunk is freed (arena
reset). One parser, two consumers:

- **Oracle / standalone** (`--sexp`, the differential test vs grammar.js,
  prpfmt): a consumer that appends each construct to the hhds tree — exactly
  today's `materialize`. The full tree is still available for testing and any
  tool that wants it.
- **LiveHD** (`prp2lnast`): `walk_statement_block` pulls the next construct's
  small `Ast` directly (a thin facade over `Ast` — it already exposes kind /
  field / children / span / text), emits LNAST, and drops the chunk.
  **No persistent parse tree at all** — no `materialize`, no global `Ast`.
  Resident memory = the current construct's `Ast` (tiny) + the growing LNAST
  (needed regardless). Cross-construct state lives in the LNAST/builder scope
  stack, not the parse tree.

**Why this respects the constraints.**

- The recursive-descent *algorithm* is untouched — the parser already loops over
  top-level statements; the seam is "yield each one" + reset the arena. The
  `Ast` stays (it is the parser's natural scratch for the cheap restructuring it
  does — reinterpreting an expression as an lvalue, etc. — and the small
  random-access tree the per-construct walk needs).
- prp2lnast's per-node walk logic and all its LNAST passes are unchanged; only
  its *driver* changes (pull constructs from the stream instead of iterating a
  root's children) — and that driver is rewritten for the tree-sitter→prpparse
  port anyway.
- Diagnostics do not get worse: spans come from byte offsets as today, the
  LNAST-side scope checks are unaffected, and fail-fast is preserved (the parser
  throws mid-stream; prp2lnast's existing try/catch handles it).

Net effect on the LiveHD path: drops `materialize` entirely, never builds a
global parse tree (Ast *or* hhds), bounds resident memory to the largest single
construct, and makes teardown trivial.

---

## Plan going forward

### Phase A — streaming seam (in prpparse)

- Make `Ast_arena` **resettable** (per-construct reset/free; reuse storage).
- Add a top-level **stream seam**: `parse_next()` / `for_each_top_level(consumer)`
  that parses one top-level construct and hands it to the consumer.
- Keep the **hhds-tree builder as the default consumer** so the CLI and every
  current test behave identically (full tree, `--sexp` unchanged).
- Stabilize the `Ast` facade surface (kind / field / child iteration / span /
  text) that prp2lnast will bind to.

Test gate: all current tests green (accept-parity 428/428, prpfmt 428/428,
`--sexp` byte-identical); a streaming-consumer unit test asserting resident
parse-tree memory stays bounded (one construct) on a large concatenated input.

### Phase B — prp2lnast port onto the stream (in ../livehd, Phase-6 integration)

Land **after** the pending livehd bug-fixes so the full regression is green
before the front-end is replaced.

- Swap the tree-sitter front-end for prpparse: `walk_statement_block` pulls
  constructs from the parser stream; the node facade is over `Ast` (not hhds).
- Keep tree-sitter behind a flag during the transition; run both and diff LNAST
  on `inou/prp` tests until parity soaks, then remove the tree-sitter path.
- Diag bridge: `prpparse::Diag` → `livehd::diag::Sink` (fields map 1:1).
- **inou/slang is unaffected**: it has its own front-end (slang → LNAST) using
  `livehd::diag` + `Source_locator::mint` exactly as today. No refactor expected
  (minor one possible, not anticipated).
- Acceptance: all `inou/prp` tests green with prpparse; perf + error-message
  comparison recorded; tree-sitter path removed once parity has soaked.

Sequencing: pending livehd bug-fixes → full green regression → swap the
front-end → soak → delete tree-sitter from the prp path.

### Phase S — large-file splitting / parallelism (deferred)

Streaming reduces the urgency: resident memory is already bounded per construct,
so splitting is only a *throughput* lever, not a memory one. The lexer already
records depth-0 split points. If needed later, parse partitions concurrently
(one hhds writer per tree, per hhds's model) while keeping LNAST emission
ordered. Moot for the current corpus (all but `yy.prp` are < 2 KB; `yy.prp`
parses in ~0.65 s serially).

---

## Differential oracle / testing

- **Accept parity**: all `full_pyrope/*.prp` parse (CI mirrors `./test.sh`).
- **Tree-shape check**: `prpparse_cli --sexp` vs normalized `tree-sitter parse`;
  documented differences are fine when they don't make the prp2lnast port
  harder.
- **Reject parity + message quality**: negative suite (removed constructs,
  unbalanced braces, bad literals) with golden diagnostics; each case also
  rejected by tree-sitter, with prpparse's message the better one.
- **Fuzzing**: mutate valid corpus files; require fail-fast; check the
  diagnostic span lands near the injected mutation. Hard gate: 0 false
  positives.
