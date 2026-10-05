# prpparse

A fast, hand-written recursive-descent Pyrope parser that produces an
[`hhds`](../../hhds) tree, intended to replace tree-sitter as the front-end for
`../livehd/inou/prp`. tree-sitter-pyrope stays in this repo as the **differential
oracle**: prpparse accepts exactly what `grammar.js` accepts.

See [`plan.md`](plan.md) for the full design, status, and the streaming
front-end plan. This README covers what is built and how to run it.

## Status (Phases 0–3 complete)

- **Phase 0 — scaffold.** Repo-root `MODULE.bazel` / `.bazelrc` (C++23, hhds via
  `local_path_override` to `../hhds`), `prpparse/BUILD` (library + CLI + test).
- **Phase 1 — lexer.** Buffer-based, `std::string_view`-only, flat `Token` vector.
  Longest-match operators, all integer radix forms, single/interpolated strings
  (one brace-aware token), backtick identifiers, soft keywords, comment/trivia
  side-list, and the exact `src/scanner.c` virtual-semicolon rule precomputed per
  token (`terminator_before`). Lexes all 428 corpus files cleanly.
- **Phase 2 — tree schema.** `prp_nodes.def` (141 kinds) + `prp_fields.def` (57
  fields) generated from `src/node-types.json`; packed into hhds `Type`
  (uint16 = kind:10 | field:6). `Prp_tree` materialises the AST into hhds,
  records each spanned node's raw byte span (srcid is minted lazily, off the
  critical path — see below), and renders a tree-sitter-style s-expression.
- **Phase 3 — parser.** Precedence-climbing expressions with flat same-tier
  chains, the paren-list classifier (tuple / arg_tuple / paren_group / lvalue),
  suffix chains (`.f` / `[i]` / `#[..]` / `.[a]` / `::[..]` / calls / generics),
  statements, lambdas, types, match/for/while/loop, and interpolated holes.

**Result: 428/428 corpus files parse (accept-parity with tree-sitter).**
Reject-parity holds on spot checks; tree-shape closeness is ~83% of tree-sitter's
node count, the gap being intentional wrapper omissions (see below).

## Build & test

```bash
bazel build //prpparse:prpparse_cli      # the standalone driver
bazel test  //prpparse:prpparse_test     # lexer + parser + hhds unit tests
make test-prpparse                       # unit tests + corpus accept-parity gate
make fuzz-prpparse                        # syntax-error fuzzer (see below)
```

### Syntax-error fuzzer (`prpparse/tests/fuzz.py`)

Mutates valid corpus files and uses **tree-sitter as the syntax oracle** (it
decides whether a mutation actually broke the grammar). Each mutation is
classified: *caught* (both reject), *missed* (tree-sitter rejects, prpparse
accepts — acceptable, inou/prp catches it from the parse tree), *benign* (still
valid), or *false positive* (prpparse rejected valid syntax — a real bug). The
run **fails on any false positive**; it also reports the syntax-error capture
rate and error-span localization (distance from the injected edit).

Mutations include token edits, bracket drops/inserts, truncation, and comment
injection (`/* */` / `//`). The harness also extracts tree-sitter's own error
line from its `-q` output and reports **localization vs tree-sitter** — the
achievable bound, since deleted openers / unclosed brackets are inherently
non-local for both parsers. Use `--show N` to inspect missed cases and the cases
where prpparse localizes worse than tree-sitter.

Adversarial sweeps (5 seeds, ~30k mutations) hold **0 false positives** with
~98% syntax-error capture; ~80% of caught errors land on the same line as
tree-sitter (or earlier). Unclosed brackets that reach end-of-input are blamed on
the **opening** bracket (matching tree-sitter), with a note at the failure point.
The fuzzer found and drove fixes for several real over-rejections (assignment
terminators, keyword-as-identifier, lambda-as-operand, complex decl lvalues,
array-type base crossing a newline) and a multi-line-comment terminator bug.

CLI (`--help` for the full list):

```bash
bazel-bin/prpparse/prpparse_cli a.prp b.prp     # parse; report diagnostics only (no tree)
bazel-bin/prpparse/prpparse_cli --sexp   FILE   # parse -> s-expression
bazel-bin/prpparse/prpparse_cli --tokens FILE   # dump the token stream
bazel-bin/prpparse/prpparse_cli --lex    FILE   # lex only
bazel-bin/prpparse/prpparse_cli --help          # usage
prpparse/tests/diff_oracle.sh            FILE   # side-by-side vs tree-sitter
```

The default mode parses each file and prints only clang-style syntax diagnostics
on stderr (nothing for a clean parse); exit status is non-zero if any file fails.

## Architecture

```
source_buffer.hpp   owns file bytes + line table; everything downstream is a view
prp_tokens.def      X-macro operator/punctuation table (longest-match order)
prp_keywords.def    X-macro soft-keyword table
token.hpp           Token_kind / Keyword enums + Token
lexer.{hpp,cpp}     buffer lexer; computes terminator_before (scanner.c handshake)
diag.hpp            Diag/Span/Severity/Parse_error (shape-compatible with livehd::diag)
prp_nodes.def       GENERATED: 141 node kinds       (tools/gen_nodes.py)
prp_fields.def      GENERATED: 57 field roles
node_kind.hpp       Kind/Field enums + hhds Type packing + name tables
ast.hpp             arena-allocated intermediate Ast (offsets only, no strings)
prp_tree.{hpp,cpp}  materialise Ast -> hhds, record spans, lazy srcid, to_sexp
parser.{hpp,cpp}    recursive-descent parser (mirrors grammar.js rule names)
tools/gen_nodes.py  regenerate prp_nodes.def / prp_fields.def from node-types.json
tests/              gtest units + cli.cpp + corpus scripts
```

The parser builds an intermediate `Ast` (so "parse-expression-then-reinterpret-
as-lvalue" cases are trivial), then materialises it into hhds. The virtual
semicolon is honoured only at statement level: a `Bracket_guard` suppresses it
inside `(...)`/`[...]`, a `Scope_guard` restores it inside `{...}` blocks and
in the header of an `if`/`match` expression (a header ends at a newline, also
inside brackets: grammar.js `_line_end`).

## Known intentional divergences from tree-sitter

Exact `--sexp` equality is **not** a goal (plan decision 1). prpparse flattens
several single-child wrapper nodes that do not help the `prp2lnast` port:

- `assignment_operator`, `binary_*_op`, `constant` wrappers are dropped (the
  concrete `assign` / `op_add` / `integer_literal` node is emitted directly).
- comments are recorded in a side-list, not as `(comment)` tree nodes (decision 2).
- `-N` is lexed as `-` + number and folded by the parser into a unary node
  (decision 7), rather than a single negative-literal token.

### RD-vs-GLR residual (very rare over-rejection)

tree-sitter is GLR and keeps ambiguous parses alive until a later token
disambiguates them; prpparse is a single-pass recursive-descent parser. On a tiny
fraction of *semantically meaningless* mutated inputs (~0.02% in fuzzing), this
shows up as prpparse rejecting something tree-sitter accepts — e.g. a match case
`3 == { z = 1 }` where the `{…}` is simultaneously a comparison operand and the
case body, resolved by tree-sitter only via the surrounding cases. These never
occur in real code (the corpus is 100% parity) and would be rejected downstream
anyway, so they are left as-is. The fuzzer's default seed has zero false
positives; adversarial multi-seed sweeps surface this residual.

### Names and reserved words

Every ordinary name uses the same rule: keywords and reserved type spellings
require backticks, matched by exact spelling (case-sensitive). `if` is reserved
but `IF` and `If` are ordinary names; `Clock` and `Reset` are type words but
`clock` and `reset` are ordinary names. The old lowercase type spellings
(`u8`, `s2`, `i32`, `bool`, `unsigned`, ...) are ordinary names too (owner ruling
2026-09-30). This includes fields, methods,
enum members, attributes, named arguments, generic bindings, and both sides of
renaming. Names containing characters outside the identifier alphabet also
require backticks; an escaped name must be nonempty. Name lookup remains
case-sensitive, so `` `if` `` and `` `IF` `` are distinct names.

For example, write ``x.`if` ``, ``f(`in`=x)``, ``x.[`comptime`]``, and
``const cfg = (const `type` = 1)``. Actual syntax keeps its normal spelling:
`if` starts a conditional, `comptime` modifies a declaration, and `import(...)`
is the import construct. Type words in type positions and conversions keep
that meaning (`U8`, `U8(x)`); using their text as an ordinary name requires
backticks (`` `U8` ``, `` `Clock` ``); `u8` needs none.

`require_plain_name` enforces this rule at identifier leaves and binding sites,
with a source-located diagnostic and a backtick hint. The lexer separately
rejects invalid identifier characters. Tree-sitter may
accept a syntax superset, including `_field_word` in field-like positions;
acceptance by the editor grammar does not make a name legal in `lhd compile`.
There are no keyword-name exceptions for fields or attributes.

### Syntax rules of the 2026-09-29 spec (both parsers agree)

- **Old lowercase type spellings are plain names.** `u8`/`s2`/`i32` (`u`, `s`
  or `i` followed by digits), `bool`, `boolean`, `unsigned`, `signed` and
  `string` lex as ordinary identifiers and are legal in every name position (a
  variable, port, parameter, field, lambda name) and syntactically in a type
  position (`x:u8`) or as a cast callee (`u8(x)`). The helpful "`u8` was renamed
  `U8`" error is lhd's (it fires when such a word is used as a type or cast
  callee and is not a declared name); a backticked spelling is the same name.
  The exact new type words `U<N>`/`S<N>` (`U8`, `S2`), `Unsigned`, `Signed`,
  `Bool`, `String`, `Clock` and `Reset` stay reserved.
- **Identifier characters** are letters (`\p{L}`, non-ASCII too), decimal
  digits (`\p{Nd}`) and `_` (unicode_ident.hpp, generated by
  tools/gen_unicode_ident.py); a name starts with a letter or `_`. Emoji,
  symbols, superscripts, roman numerals and combining marks are errors. The
  non-ASCII blanks (NBSP, the BOM, ZWSP, U+2060, the Unicode spaces) are an
  error outside a string or a comment (`non-ascii-space`, owner ruling 108):
  only ASCII blanks separate tokens. A lone `_` is no name (`bare-underscore`);
  `` `_` `` is. The one exception is the anonymous tuple-TYPE entry `_:T`
  (`v:(_:U4, _:U8)`): a `_` right after `(` or `,` and before a single `:`.
- **Strings.** The escapes are exactly `\n \t \r \\ \" \' \0 \xNN` (at most 7F)
  `\u{1..6 hex}` (a Unicode scalar value: at most 10FFFF, no surrogate) and
  `` \` `` (also inside backticked names);
  every other `\c` is `bad-escape`
  (`\{`, `\}`, `\uNNNN`, `\q`). `\u{..}` never opens a hole. `{{`/`}}` are
  literal braces; an empty hole (`{}`, `{ }`, `{ /* c */ }`) is
  `empty-interpolation`, a lone `}` is `lone-brace`, `"{:b}"` has no
  expression, and a format spec holds no backslash. The spec after a hole's
  `:` is text (lexer.cpp `hole_spec_colon`), never lexed as code.
- **`enum(...)` is gone**: only `enum E = (...)` / `enum E:T = (...)`
  (`enum-expression`).
- **Assignment targets** are rooted at a name: `f(x).a = 3`, `(a+b).c = 3`,
  `f(x)[0] = 3`, `(x)#[0] = 1` are `bad-assignment-target`. A destructuring
  assignment is a STATEMENT (never in an expression or init clause) with `=`
  only, whose slots are bare names or `name = path` renames with a dotted name
  path, never typed: `(a.b, c[1]) = f()`, `const (a:U32, b) = f()`,
  `(a, b) += f()` are errors (`check_destructuring`). A declaration list
  without `=` (`mut (a, b)`) is unchanged.
- **Declarations.** `wrap`/`sat` need an assignment (`wrap const y:U8` is an
  error); a timed name declares without `=` (`stage[1] out@[4]`,
  `mut z@[1]:U8`); a bare `value:U8` statement is an error.
- **A line never starts with `@`** (lexer: `line-starts-with-at`), also inside
  a condition or a bracket.
- **A line starting with `(` is a new statement**, also after a type
  annotation: `mut r:Foo` newline `(a)` is a declaration plus a tuple
  statement (never the type call `Foo(a)`), and `reg r:Unsigned` newline
  `(bits=3) = 0` is an error. Inside brackets newlines do not matter.
- **Selectors are never empty**: `a[]`, `a#[]`, `f[](c)`, `mut x[] = 1`,
  `pipe[]` are `empty-select`; only an array length (`x:[]U8`), `stage[]` and
  a timing slot (`x@[]`) may be empty. An array length never starts with a
  storage word, `comptime` or `ref` (`x:[mut]U8`, `x:[stage]`).
- **Lists of only commas** (`(,)`, `[,]`, `f(,)`, `::[,]`, `comb f(,)`) are
  `empty-list`, as is an empty destructuring (`() = f()`, `const () = f()`);
  an empty generic list `f<>(x)` is no generic call. `()`, `[]`, `(,a)` are
  fine.
- **An instance attribute takes only a call**: `a::[x](1)` is legal, but
  `a::[x][1]`, `a::[x]#[1]`, `a::[x].b`, `a::[x].[bits]`, `a::[x]::[y]` are
  `attribute-set-suffix` (parenthesize: `(a::[x]).b`).
- **A generic list needs a glued `<`** (owner ruling 107): `f<N=3>(x)` is a
  generic call, but a `<` after a blank (space, tab or newline) is always a
  comparison, so `a < b > (c)` is a comparison chain and `f <N=3>(x)` is
  `spaced-generic`. Only the character right before the `<` counts: a
  comment glued to it (`f /* c */<T>(x)`) leaves it glued. tree-sitter
  agrees (scanner.c `scan_spaced_lt`).

## Deferred (post-milestone)

Performance pass (Phase 4), large-file splitting + threads (Phase 5 — moot for the
current corpus, all files <2 KB), the hhds rapidhash `span_minter` switch, and the
LiveHD integration (Phase 6). The syntax-error fuzzer (`make fuzz-prpparse`) is in
place and can drive further error-message / capture-rate improvements.
