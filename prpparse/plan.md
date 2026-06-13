# prpparse — implementation plan

A recursive-descent Pyrope parser producing an hhds tree. See `todo.md` for
the requirement list. This plan is organized as phases; each phase has its own
test gate so progress is verifiable at every step. tree-sitter stays in this
repo throughout and serves as the **differential oracle**.

## Key design decisions (made up front)

1. **Tree shape = CST, not LNAST.** Node kinds mirror the tree-sitter-pyrope
   grammar node names (`assignment`, `tuple`, `arg_tuple`, `if_expression`,
   `binary_other_op`, …) where that helps migration. Exact tree-sitter
   `--sexp` equality is not a hard requirement; close-enough CST shape is
   acceptable when it keeps `../livehd/inou/prp/prp2lnast.cpp` mechanical to
   port.
   A `prp_nodes.def` X-macro file is generated once from `src/node-types.json`
   (regen script checked in) and hand-maintained afterwards.

2. **Node payload.** hhds nodes carry `Type` (uint16). Proposal: pack
   `kind` (10 bits, ~150 node kinds) + `field` (6 bits, ~30 tree-sitter field
   names like `lvalue`, `rvalue`, `condition`, `code`) — the field describes
   the edge role of this node under its parent, replacing
   `ts_node_child_by_field_name`. Text is never stored: required nodes'
   `attrs::srcid` spans recover `std::string_view`s from the attached file
   buffer. (Decided 2026-06-10: pack field into `Type` — kind 10b + field 6b;
   a node has exactly one role under its parent in the current grammar.)

3. **Fail-fast errors.** First syntax error builds one rich `Diag` record
   (code/category/message/span/hint/notes, shape-compatible with
   `livehd::diag::Diagnostic`) and aborts via exception. No error recovery,
   no multiple-diagnostic mode, and no ERROR nodes in the tree. The standalone
   CLI renders clang-style text using the lexer's line table.

4. **Virtual semicolons live in the parser, not the lexer.** The lexer tags
   each token with a `newline_before` bit; the parser, *only at statement-end
   positions*, applies the exact `src/scanner.c` rule: terminator at EOF,
   before `}`, or at a newline unless the next token starts with
   `, = . > < & ^ | * / + -`, `else`, `elif`, or `!=`. This reproduces the
   tree-sitter valid_symbols handshake.

5. **Soft keywords.** The lexer classifies every word as `identifier` and
   attaches a keyword id when the spelling matches a known keyword; the
   parser decides contextually (so `enum(...)` as a call, `const type = 1`,
   `.[comptime]` keep working). Hard-reserved list is written down once in
   `prp_tokens.def`.

6. **Overparse parity.** The parser accepts exactly what grammar.js accepts,
   including the `grammar_overparse.md` cases — semantic narrowing stays in
   livehd. This keeps `./test.sh` corpus parity meaningful. (Optional later:
   `--strict` flag adding narrowing diagnostics.)

   `grammar.js` is treated as mostly fixed and used as the differential
   oracle, with only targeted bug fixes expected during this project.

7. **Negative literals**: lexer always emits `-` as an operator token; the
   parser folds the sign into the literal at operand position (matches the
   grammar's token-level behavior without lexer/parser state coupling).

8. **Interpolated strings**: lexer emits one raw token for the whole string
   (brace-depth aware, like the grammar); the parser sub-lexes the `{expr}`
   regions on demand with absolute offsets so spans stay correct.

9. **Initial srcid scope.** The first milestone requires `attrs::srcid` for
   every variable destination / definitely-modified location. Broader node
   span coverage is deferred until fuzzing diagnostics and LiveHD integration
   show which additional spans are valuable.

## First usable milestone

Before performance, splitting, or LiveHD integration, the parser should:
- parse all `full_pyrope/*.prp` examples into HHDS trees,
- fail fast on the first syntax error with a useful diagnostic,
- populate `attrs::srcid` for variable destinations / definitely-modified
  locations,
- keep the tree shape close enough to tree-sitter to make `prp2lnast.cpp`
  migration straightforward, without requiring exact `--sexp` equality.

After that milestone, add a fuzzer based on valid `full_pyrope` inputs:
mutate syntax, expect a parse failure, and score whether the diagnostic span
lands close to the injected diff. Use those results to decide which additional
node spans and diagnostic improvements matter most. Parallel parsing and
speed work come after this; LiveHD integration follows once the standalone
parser/fuzzer loop is useful.

---

## Phase 0 — Scaffold and build plumbing

Deliverables:
- `MODULE.bazel` at the repo root (`module(name = "prpparse")`), with
  `bazel_dep` + `git_override` for `hhds` (commented `local_path_override`
  to `../hhds` for local debug, mirroring `../livehd/MODULE.bazel` style),
  plus `iassert`, `googletest`. `.bazelrc` mirroring hhds (C++20, asan/tsan
  configs).
- `prpparse/BUILD` with `prpparse` (cc_library), `prpparse_cli` (cc_binary),
  `prpparse_test` (cc_test).
- Smoke test: build an hhds `Tree` + `Forest`, add a few typed nodes, mint a
  `Source_locator` span, print with `tree_print`.

Test gate: `bazel test //prpparse/...` green on a hello-tree test.

## Phase 1 — Lexer prepass

Deliverables:
- `prp_tokens.def` (X-macro, lnast_lexer style): punctuation, operators
  (longest-match: `..=`, `..<`, `..+`, `..`, `::`, `++=`, `<<=`, …),
  literals (all integer forms, strings, bool, `?`), identifier (incl.
  backtick form), keyword table (soft), comment handling.
- `Source_buffer`: loads the file once (mmap or read); owns the bytes for
  the lifetime of the tree wrapper. Everything downstream is
  `std::string_view`.
- `Lexer`: single pass producing a flat `std::vector<Token>` where
  `Token = {kind:u16, flags:u16 (newline_before, …), offset:u32, len:u32}`.
  Also produces:
  - line-start offsets table (for line/col in diagnostics),
  - comment/trivia span list (kept out of the token stream),
  - **split-point list**: token indices at depth-0 statement starts,
    flagged stronger when they begin a lambda/`type` declaration.
- Lexer-level errors (unterminated string/comment, bad literal) go through
  the same Diag path.

Test gate:
- Token-dump golden tests (`prpparse_cli --tokens`) for ~15 representative
  corpus files.
- Lex all 491 `full_pyrope/*.prp` with zero errors; assert every byte is
  covered by tokens+trivia+whitespace (no silent gaps).
- Unit tests for line/col mapping and each tricky token (negative numbers
  vs minus, `::`, ranges, interpolated strings, backtick identifiers).

## Phase 2 — Tree schema and srcid plumbing

Deliverables:
- `prp_nodes.def` generated from `src/node-types.json` (script:
  `tools/gen_nodes.py`), defining the kind enum + kind→name strings.
- Field-name enum (from grammar.js field names) and the Type packing
  (decision 2).
- `Prp_tree` wrapper owning: `std::shared_ptr<hhds::Forest>`, the root
  `Tree`, the `Source_buffer`, and a `Source_locator`; helpers:
  `make_node(kind, field, parent)`, `set_span(pos, start, end)`
  (mints srcid eagerly for required nodes), `text(pos)` → string_view,
  `kind(pos)`, `field(pos)`, child iteration by field.
- hhds side (owned by Jose, not prpparse): switch `Source_locator` hashing
  to vendored, pinned rapidhash with per-file `path_hash` + fixed-width
  per-node mint, ideally exposed as `Source_locator::span_minter(path)`
  (see resolved question 3). Until it lands, prpparse calls the existing
  `mint()` — the API is the only coupling, ids regenerate transparently.
- S-expression dump (`prpparse_cli --sexp`) emitting a tree-sitter-inspired
  output (named nodes + field labels) — this is a migration/debugging aid, not
  an exact equality contract.

Test gate: hand-built mini-trees round-trip through `--sexp` and
`tree_print`; srcid → `Source_locator::resolve` returns the right
file/span/line.

## Phase 3 — Parser core (serial, fail-fast)

The RD parser, structured to mirror grammar.js rule names (one function per
rule where sensible). Sub-milestones, each gated on the subset of the corpus
it unlocks:

3a. Expressions: precedence climbing over the 5 Pyrope priority tiers with
    flat same-tier chains; operands (constants, identifiers, dot chains,
    selects `[i]`, bit selects `#[…]`, attribute reads `.[…]`,
    `attribute_set ::[…]`, paren group, tuples, `ref`).
    The **paren-list classifier**: one `parse_paren_list` accepting the
    union of item shapes (expr, `name = expr`, kind-keyword declaration,
    `name:type`, spread, lambda), classified at `)` / `=` into
    tuple / arg_tuple / paren_group / lvalue list.
3b. Statements: assignment (parse-expression-then-reinterpret-as-lvalue,
    incl. destructuring and `wrap`/`sat`), declarations, expression
    statements, virtual-semicolon termination.
3c. Control flow: `if`/`elif`/`else` (with `init;` clauses), `match` +
    case operators, `for`/`while`/`loop`, `break`/`continue`/`return`.
3d. Lambdas (`comb/mod/pipe/fluid`, generics, pipe/fluid bracket slots),
    `type`, `impl`, `enum`, `import`, `test`, `spawn`.
3e. Types: `parse_type` mode (expression-shaped types, `typed_field` tuple
    items, array types, `@[…]` timing), the `enum X:uint(...)` bounded
    lookahead.
3f. Interpolated-string sub-parsing.

Diagnostics: every `expect(token)` failure produces a Diag with a stable
kebab-case code, the span of the offending token, and a context-aware
message ("expected ')' to close the argument tuple opened here" with a note
pointing at the opening paren). Error-message quality is a first-class
deliverable of this phase, reviewed file-by-file in the negative suite.

Test gate (the big one):
- **Accept parity**: all 491 `full_pyrope/*.prp` parse (CI script mirroring
  `./test.sh`).
- **Tree-shape migration check**: `prpparse_cli --sexp` output is compared
  against `tree-sitter parse` output (normalized) for the full corpus — script
  `tests/diff_oracle.sh`. Differences are acceptable when documented and when
  they do not make the future `prp2lnast.cpp` port materially harder.
- **Reject parity + message quality**: `tests/err/*.prp` negative suite
  (removed constructs: `a = b:u8`, `when`/`unless`, `f(mut a=1)`,
  `return X`, `x.0`, unbalanced braces, bad literals …) with golden
  diagnostics (code + span + message). Each case must also be rejected by
  tree-sitter (parity) — and prpparse's message must be the better one.
  Add a fuzzing suite after accept parity: mutate valid `full_pyrope` files,
  require fail-fast parse failure, and check that the diagnostic span is close
  to the known injected mutation.

## Phase 4 — Performance pass (still serial)

- Benchmarks (`prpparse_cli --time`, plus a google_benchmark target):
  parse the whole corpus and large concatenated inputs; compare against
  `tree-sitter parse` (repo `bench.sh`).
- Verify the string_view discipline: zero heap allocations per token/node in
  steady state (count with a test allocator); reserve token/tree vectors
  from byte-size heuristics.
- srcid minting cost: measure for the required destination spans first. If
  broader span coverage becomes useful and minting shows up, evaluate lazy
  minting (span stored raw, srcid minted on first query) — keep the
  `Source_locator` requirement intact for required nodes either way.
- Target: ≥10× tree-sitter throughput on the corpus (tune after first
  measurements; record numbers in this file).

Test gate: phase-3 parity suites still green + recorded benchmark table.

## Phase 5 — Splitting and 1–8 thread parallelism

- Splitter: when `Source_buffer` > 200KB, choose up to 8 cut points from the
  lexer split-point list so partitions land ~200–300KB (flexible; fall back
  to fewer/zero cuts when no good points exist).
- Each partition parses into its own `Tree` inside the shared hhds `Forest`
  (one writer thread per tree — hhds's supported concurrency model). The
  root tree replaces each cut region with a subtree reference; consumers
  traverse with `pre_order_with_subtrees` and never see the seams.
- Per-partition `Source_locator`s merge into the root locator at join
  (`merge()` returns the remap to apply to `attrs::srcid`).
- Threading: `std::jthread` pool sized min(8, partitions, hw threads).
- Determinism: identical `--sexp` output for any thread count.

Test gate:
- Synthetic big inputs (concatenate corpus files to 0.5–4MB) parse split vs
  serial with byte-identical `--sexp` dumps.
- A `--force-split=N --split-threshold=K` debug flag exercises splitting on
  small files so the whole corpus runs through the split path in CI.
- tsan config run (`bazel test --config tsan //prpparse/...`).
- Scaling benchmark: 1 vs 4 vs 8 threads on the 4MB input.

## Phase 6 — livehd integration

(Work lands mostly in `../livehd`, planned here for completeness.)

- livehd swaps its `http_archive` on this repo for `bazel_dep(prpparse)` +
  `git_override` (same pattern as hhds), or updates the archive BUILD to
  expose `//prpparse`.
- Port `inou/prp/prp2lnast.cpp`: a thin facade gives it the TSNode-ish API it
  already uses (`kind`, `child_by_field`, `named_children`, `text`, spans
  from srcid) over `Prp_tree`. Keep tree-sitter behind a flag during the
  transition; run both and diff LNAST output on `inou/prp/tests`.
- Diag bridge: `prpparse::Diag` → `livehd::diag::Sink::emit` (fields map
  1:1 by construction); spans flow through `Source_locator`/`attrs::srcid`
  instead of the pre-sourcemap best-effort path.
- Acceptance: all livehd `inou/prp` tests green with prpparse; perf and
  error-message comparison recorded; tree-sitter path removed in a follow-up
  once parity has soaked.

---

## Open questions — all resolved 2026-06-10

1. ~~Field-role encoding~~ — pack into node `Type` (kind 10 bits +
   field 6 bits).
2. ~~Comment/trivia~~ — side list only: lexer records comment spans in a
   separate vector; the tree never contains comment nodes. A future
   formatter re-associates by byte position and any additional spans added
   after fuzzing/LiveHD integration.
3. ~~srcid minting~~ — **eager for required spans, final, and switch hhds to
   rapidhash** (https://github.com/Nicoshev/rapidhash). Required srcids are
   minted during the parse. The hash is the hhds cross-artifact identity
   convention (persisted in srcmap.txt, append-only; merge dedups by id),
   so the switch is an hhds-wide change — do it now, while hhds is v0.2.x
   and no serious srcmaps are deployed. Upstream hhds PR (Phase 2):
   - Vendor a **pinned** `rapidhash.h` (rapidhash output changed across
     major versions; ids must never drift with upstream updates) and bump
     a version line in the srcmap format so loaders detect FNV-era maps.
   - rapidhash is one-shot, not streaming, so the construction becomes:
     `path_hash = rapidhash(path)` once per file, then per required span
     `id = rapidhash({path_hash, start_byte, end_byte}, seed=Anchor_kind)`
     over a fixed 24-byte buffer (line-only: `{path_hash, line}`;
     combine: over the parent-id array). One short rapidhash call per
     node — faster than any FNV variant and path-length independent.
   - Expose it as `locator.span_minter(path)` returning a handle holding
     `path_hash`, with `mint(start, end, line)` per required span.
   Phase 4 verifies the cost profiled away. rapidhash is also the default
   hash for any prpparse-internal tables that need one.
