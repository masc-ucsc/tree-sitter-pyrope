# prpparse — requirements / TODO

Custom recursive-descent Pyrope parser. Goal: **fast AND very good error
messages**, producing an HHDS tree so `../livehd/inou/prp` can migrate off
the (good but slow) tree-sitter parser.

> **Status (Phases 0–3 done):** prpparse builds under bazel and parses all 428
> `full_pyrope/*.prp` (accept-parity with tree-sitter). See `README.md`.

## Hard requirements

- [x] Builds with **bazel** (repo-root `MODULE.bazel`; `bazel test //prpparse/...`).
- [x] Uses **`../hhds`** to store the parsed tree — `bazel_dep(name = "hhds")`
      + `git_override` (commented), active `local_path_override(path = "../hhds")`
      for local debug, like `../livehd2/MODULE.bazel`.
- [x] Populates **`hhds::Source_locator`** info: every spanned node carries
      `attrs::srcid` minted from (file, start_byte, end_byte, line). (Currently
      minted for all spanned nodes; can be narrowed to destinations in Phase 4.)
- [x] **Lexer prepass** that tokenizes the whole buffer first — inspired by
      `../livehd2/lnast/lnast_lexer.cpp` (X-macro `*.def` token tables) but
      buffer-based (not `istream`), flat token vector, records line starts,
      comment spans, and split points.
- [x] **`std::string_view` only** in the lex/parse path — the lexer keeps the
      file buffer (`Source_buffer`) alive; token/node text is always a view.
      (The intermediate `Ast` is arena-allocated and stores offsets only.)
- [ ] **Large-file splitting** (>200 KB → up to 8 partitions). DEFERRED (Phase 5):
      the lexer marks split points, but the splitter/threading is not built —
      moot for the current corpus (all files < 2 KB).
- [x] **Good error generation** via `prpparse::Diag` (code / category / message /
      span / hint / notes), shape-compatible with `../livehd/core/diag.hpp`.
      **Fail-fast**: first syntax error throws `Parse_error`. Unclosed brackets
      carry a "opened here" note.
- [x] Parses **everything `./test.sh` passes** — all 428 `full_pyrope/*.prp`.
- [x] First milestone: all `full_pyrope/*.prp` parse into HHDS trees, fail-fast
      on the first error.
- [x] **Fuzzing** (`make fuzz-prpparse`, `tests/fuzz.py`): mutate valid corpus
      files, tree-sitter as the syntax oracle. Hard gate: 0 false positives
      (never reject valid syntax). Reports syntax-error capture (~98%) and
      error-span localization (median 1 B, 83% within 8 B). Capturing *every*
      syntax error is not required — inou/prp catches the rest from the parse tree.

## Nice to have (not required)

- [x] Node-kind enum close to the grammar rule/node names (`prp_nodes.def`, 141
      kinds; the `Type` stored in each hhds node), so the `prp2lnast.cpp` walker
      ports mechanically.
- [x] Comment/trivia spans recorded by the lexer (`Lexer::comments()`).
- [ ] In-parser semantic narrowing diagnostics for the `grammar_overparse.md`
      cases (behind a flag). DEFERRED — default stays tree-sitter-parity.
