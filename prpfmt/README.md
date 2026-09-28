# Overview

Pyrope code currently lacks a robust, standardized formatter. Without consistent formatting (indentation, spacing, alignment), code style varies widely and becomes harder to read and review.

This tool is a formatter (like `clang-format`) for Pyrope which traverses the Pyrope tree-sitter grammar and emits standardized formatted Pyrope.

The formatter is written in **C++20**. The generated tree-sitter parser
(`../src/parser.c`) and the hand-written scanner (`../src/scanner.c`) are C and
are compiled as C, then linked with the C++ objects. The C++ build enables
hardening checks (`-D_GLIBCXX_ASSERTIONS`, `-fstack-protector-strong`,
`-D_FORTIFY_SOURCE=2`) so container/buffer misuse traps instead of corrupting
memory, plus a `make debug` target that adds AddressSanitizer + UBSan.

> **Runtime version:** the formatter dispatches on `ts_node_grammar_symbol()`,
> which requires a tree-sitter runtime **≥ 0.20.8** (older runtimes, including
> some sibling `../../tree-sitter` checkouts, lack that symbol and will fail to
> link). Prefer a pkg-config-installed runtime.

## File Structure
This project depends on the tree-sitter library and expects the following directory structure:
```
project-root/
├── prpfmt (executable)
├── tree-sitter/
│   └── lib/
│       ├── include/
│       │   └── tree_sitter/
│       │       └── api.h
│       └── libtree-sitter.a
└── tree-sitter-pyrope/
    ├── src/
    │   ├── parser.c       # generated tree-sitter parser (compiled as C)
    │   └── scanner.c      # hand-written scanner (compiled as C)
    └── prpfmt/
        ├── Makefile
        ├── README.md
        ├── main.cc        # CLI entry point, file I/O (C++20)
        ├── prpfmt.cc      # AST -> IR token hooking (C++20)
        ├── prpfmt.h
        ├── ir.cc          # IR token buffer + layout solver/renderer (C++20)
        └── ir.h
```

### Usage

#### Build
To build the formatter, navigate to the `prpfmt` directory and run:
```bash
make          # optimized, hardened build -> ./prpfmt
make debug    # AddressSanitizer + UBSan build (for development)
```
This will create the `prpfmt` executable in the `prpfmt` directory. Requires a
C++20 compiler (clang++ or g++).

#### Run
You can run the formatter from the `prpfmt` directory:
```bash
./prpfmt [options] <input_file> [options]
```

**Options:**
- `-i, --inplace`   : Rewrite the input file in place (verified before writing).
- `-o, --output <file>`        : Specify an output file (default: stdout).
- `--indent <n>` : Specify indentation size (default: 2).
- `--mode ai|human` : Select layout (default: `ai`).
- `--width <n>`  : Human-mode soft width target (default: 132; ignored in AI mode).
- `-v, --verify`     : Verify that the formatted output is still valid Pyrope.
- `-b, --bench`      : Run in benchmark mode and print timing statistics.
- `-h, --help`       : Display the help message.

Options can precede or follow the file. Long value options also accept `--indent=2`, `--width=132`, and `--mode=human`. Use `--` before a filename beginning with `-`. `-i` and `-o` are mutually exclusive; numeric values must be positive integers.

## Grammar Updates
If the Pyrope grammar (`grammar.js`) is updated, the following steps must be taken to synchronize the formatter:

1. **Re-generate Parser**: Run `tree-sitter generate` in the `tree-sitter-pyrope` directory (the `make` rule does this automatically when `grammar.js` changes).
2. **Symbol IDs (automatic)**: `ts_symbols.h` is regenerated from `src/parser.c` by `gen_symbols.sh` on every build, so the formatter's node-dispatch ids always match the grammar. Never hand-edit `ts_symbols.h`.
3. **Handle Structural Changes**: If grammar rules were renamed or their structure changed (e.g., tiered binary expressions), update the corresponding `print_` functions in `prpfmt.cc`.
4. **Rebuild**: Run `make` in the `prpfmt` directory to recompile the tool with the updated parser and symbol definitions.

## Formatting policy

AI mode keeps statements and argument lists on one line regardless of width,
without vertical alignment. Statement and declaration blocks always break after
`{`, including empty and single-statement bodies. Expression blocks may stay
inline; comments and multi-statement expression blocks require newlines.

Human mode uses 132 columns (or `--width`) as a soft readability target. A modest
overflow is allowed when it avoids splitting an expression or disrupting an
alignment group. Long trailing comments stay with their code without forcing
an otherwise fitting expression or call to wrap. Long calls, tuples, generic lists,
and headers break at commas, with one argument per line. Header inputs split
first, and `) -> (` stays together. Selectors and individual arguments remain
intact even if an indivisible argument exceeds the width; comments, strings and
identifiers are likewise never cut apart. A source break after `{` in an
expression is retained. Consecutive assignments of the same kind (such as
`const`, `mut`, `wrap`, or `type`) align unless padding causes a substantial
overflow. Another statement kind or a blank line ends the group.

Both modes use `name=value` inside tuples, calls, defaults and attributes,
`, ` between flat list items, and precedence-based expression spacing such as
`i*N + j`. Commas are trailing rather than leading; the one trailing comma
that makes `(x,)` a one-element tuple (as opposed to the grouping `(x)`) is
kept. Statement assignments use `name = value`. Trailing comments remain
attached to their item, and statement branches use `} elif` / `} else`.

Comparisons follow the same precedence rule: as direct operands of a looser
`and`, `or` or `implies` (also through grouping parens and a logical `not`/`!`)
they drop the spaces around `==`, `!=`, `<`, `<=`, `>` and `>=`, as in
`if foo!=bar or bar==foo {`, `N==0 or (z==0)` and `x==-1 and not (y<=0)`. A
comparison anywhere else keeps its spaces (`if a == b {`, `x = a == b`, a call
argument, the operand of another comparison). The comparisons of one logical
chain decide together: if any of them has an operand that is not tight
(`a + 1`, `-b`, `not x`, an `if`, a generic call such as `f<N=1>(x)`), a `<`
before a negative literal (`a < -1`, not the arrow-like `a<-1`), or a comment
in its parens, every comparison in that chain keeps its spaces. Word
comparators (`in`, `has`, `case`, `does`, `equals`) always keep their spaces
and do not hold back the others (`a in b or c==d`). A chain nested inside
another (in parens or a call argument) decides for itself.

All-named call and tuple lists are sorted alphabetically when their values can
be reordered safely from syntax alone. Mixed positional/named lists, spreads,
references, calls with possible side effects, comments, and tuple-field
cross-references preserve source order. Interface declarations and generic
parameter declarations preserve their order.

A plain same-name call binding becomes shorthand: `f(arg=arg)` becomes
`f(arg)`. Tuple fields, attributes, generic bindings and named leftovers of
locally declared variadic functions keep their explicit names. Imported or
indirect callees are not resolved by this syntax-only formatter; same-name
shorthand at those sites assumes the argument names a parameter.

```bash
./prpfmt input.prp                         # AI layout
./prpfmt --mode human input.prp            # 132-column Human layout
./prpfmt --mode human --width 100 input.prp
```

## Embedding

`prpfmt_api.h` has no tree-sitter dependency. The existing
`prpfmt_format_string(...)` entry point now defaults to AI mode, like the CLI.
Use `prpfmt_format_string_mode(..., PRPFMT_HUMAN, ...)` to request wrapping and
alignment explicitly. The width argument is used only in Human mode.

## Testing
`make test` at the repository root runs the grammar, API and CLI regressions,
then verifies parseability and idempotency of every docs-corpus file in both
modes. See [tests/README.md](./tests/README.md).

## References

- tree-sitter-pyrope: https://github.com/masc-ucsc/tree-sitter-pyrope
- tree-sitter docs: https://tree-sitter.github.io/tree-sitter/index.html
