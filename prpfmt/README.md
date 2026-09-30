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
This project depends on the tree-sitter runtime. The Makefile finds it with
`pkg-config` (e.g. `brew install tree-sitter`); without it, it falls back to a
built sibling tree-sitter checkout (`TS_DIR = ../../tree-sitter`, linking
`$(TS_DIR)/libtree-sitter.a` with headers from `$(TS_DIR)/lib/include`):
```
project-root/
├── tree-sitter/               # fallback runtime (only without pkg-config)
│   ├── libtree-sitter.a
│   └── lib/
│       └── include/
│           └── tree_sitter/
│               └── api.h
└── tree-sitter-pyrope/
    ├── src/
    │   ├── parser.c       # generated tree-sitter parser (compiled as C)
    │   └── scanner.c      # hand-written scanner (compiled as C)
    └── prpfmt/
        ├── prpfmt         # the executable (built here)
        ├── Makefile
        ├── README.md
        ├── main.cc        # CLI entry point, file I/O (C++20)
        ├── prpfmt.cc      # AST -> IR token hooking (C++20), embeddable API
        ├── prpfmt.h
        ├── prpfmt_api.h   # C API for embedding (no tree-sitter dependency)
        ├── ir.cc          # IR token buffer + layout solver/renderer (C++20)
        ├── ir.h
        ├── gen_symbols.sh # regenerates ts_symbols.h from ../src/parser.c
        ├── ts_symbols.h   # generated node-dispatch ids (never hand-edit)
        └── tests/         # regressions and tools, see tests/README.md
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
inline; comments and multi-statement expression blocks require newlines, and
so does a block whose content always spans lines (a `match` expression, or a
nested block that must break, or an `if` expression with a comment in its
header). The branches of an `if` or `match` expression
break together: when one branch must break (a comment, several statements, a
`match` value), every branch does (`if c {` newline body newline `} elif d {`
newline body ... `}`), never a mixed `} elif d { a } else { b }` or
`} } else { 3 }` tail. `match` arms go one per line (`== 1 { a }`); a
statement `match` keeps its arm blocks vertical. A comment between arms is
in no arm block, so it does not break them: one trailing the header `{`
stays there (`y = match z { // c`), one trailing an arm stays after that
arm's `}` (`== 1 { 4 } // d`, also a block comment, with the next arm on
its own line), and an own-line comment keeps its line. The value of a branch block
keeps its own layout when that block breaks: a nested value `if` or `match`
prints as it would inline (`== 1 {` newline `match a {` newline `== 0 { 0 }`
...), so a later pass that sees the break after `{` prints the same.

A list (tuple, call arguments, header ports, generic or attribute list) that
contains a lambda with a statement block, or an item that must break (a
`match` expression, a block that must break, or an `if` expression with such
a branch or with a comment in its header, `a=if c { 1 } /* b */ else { 2 }`,
which takes the statement layout), goes one item per line: each lambda header or item starts its own
line, a body indents one level below the item, and its `}` closes at the
item's indentation (`const iface = (` newline `mut value:U8=0,` newline
`comb read(self) -> (v:U8) {` ... `},` newline `)`; `f(` newline `b=2,`
newline `a=if c {` newline body newline `} else {` ... `},` newline `)`).
This holds at every nesting level: a list nested as an argument or
tuple-field value that holds such an item, or a line comment, goes one item
per line as well, with trailing commas and its `)` at its item's indentation
(`req=(` newline `a=p,` newline `// c` newline `c=r,` newline `),`).

Whenever a list of two or more items is laid out one item per line (forced by
a comment or a block member, or by the Human width), every item ends with
`,`, including the last, so appending an item leaves the previous line alone.
Inline lists drop that trailing comma, and a single item never gets one
(`(x)` is grouping, `(x,)` a one-element tuple). This covers destructuring
lists and `for` index lists too (`const (` newline `a, // first` newline
`b,` newline `) = t`; `const (a, b,) = t` becomes `const (a, b) = t`); a
one-item destructuring or index list keeps its comma (`(a,) = f()`,
`for (k,) in t`), and so does a one-item tuple written with it, also when
its item is a binding (`(a=1,)` stays as written). Any comma makes a tuple,
so a leading-comma one-item tuple prints the same way (`(,x)` and `(,,x,,)`
become `(x,)`, `(,ff=1)` becomes `(ff=1,)`). A list that holds only a
block comment stays inline (`f( /* c */ )`).

Comments inside a generic list's `<...>` stay inside it, like in a call's
argument list: a line comment before the first item or after the last puts
the list one item per line (`g<` newline `// lead` newline `W=1` newline
`>(a)`), and a block comment stays inline (`g<W=1 /* b */ >(a)`). A line
comment inside a generic binding or an attribute (`:[...]`) item keeps its
line break like one in a call argument, so it never swallows the code after
it: the list goes one item per line and the item continues on the next line
(`g<` newline `W // c` newline `=1` newline `>(a)`; `:[` newline `async= //
c` newline `true,` newline `posclk=false,` newline `]`). In a timing slot
(`@[...]`) the break stays where it was (`@[ // c` newline `1]`). A comment
between `pipe`/`fluid` and its `[...]` latency slot keeps one space on each
side (`pipe /* lat */ [1] f(...)`); a line comment there ends the line and the
slot continues one indent deeper (`pipe // lat` newline `[1] f(...) {`).

Human mode uses 132 columns (or `--width`) as a soft readability target. A modest
overflow is allowed when it avoids splitting an expression or disrupting an
alignment group. Long trailing comments stay with their code without forcing
an otherwise fitting expression or call to wrap. Long calls, tuples, generic lists,
and headers break at commas, with one argument per line. This holds at every
nesting level: an argument or tuple field that still does not fit on its
line splits its own nested call or tuple the same way (`aw=g(` newline one
argument per line newline `),`). Header inputs split
first, and `) -> (` stays together. A destructuring or `for` index list is
rarely the long part of its line: it stays inline and what follows it splits
(`const (x1, y1) = (` newline one item per line newline `)`, `for (i, j) in
zip(` newline ...), unless the list itself does not fit. With generics, the argument list or the
header inputs split before the generic list: `<...>` stays on the callee line
when it fits there (`br_flow_reg_fwd<T=U1>(` newline one argument per line),
and breaks one binding per line only when it does not fit on its own. Selectors and individual arguments (apart
from their nested lists) remain intact even if an indivisible argument
exceeds the width; comments, strings and
identifiers are likewise never cut apart. A source break after `{` in an
expression block is retained for that block (it does not force the blocks
nested in its value to break, nor the other arms of a `match`: like an arm
that does not fit, it breaks that arm only). A block nested as the value of
an expression block (`{ { 1 } }`) is laid out as a value too, so it does not
break just because its enclosing block did. A line starting with any binary
operator continues the statement (`/`, `%` and `case` included; `case` always
continues), so a chain that does not fit breaks before any of its operators:
`/` and `%` stay tight like `*` when the chain fits (`a/b`, `a%b`) and start the
continuation line when it breaks (`% b`), and a long `case` chain breaks before
each `case`. A `/` or `%` next to a comment keeps the comment's line
(`/* q */ / b`). Long `#[..]` selector chains are not split yet (a selector
stays with its operand). An `if` expression that does not fit puts each
`elif`/`else` branch on its own continuation line, one indent deeper, with its
block inline (`elif c { x }`); when a branch still does not fit, the whole
chain takes the statement layout `if c {` newline body newline `} elif d {`
... `} else {` ... `}`, closing at the indentation of the line that opened
it, never `}` newline `elif`. A chain that fits on its line, also within the
modest overflow, keeps everything it holds on that line: no operator chain
inside a branch breaks on its own (`else { x` newline `+ y }`). The same
holds for an `if` expression used as a
call argument or tuple-item value (`b=if c { x }` newline `elif d { y }`
...). Since the chain can break itself, nothing of it counts against what precedes
it on the line (also once it has the statement layout), so a declaration's
`:Unsigned(bits=N)` annotation never splits to make room for it
(`mut r:Unsigned(bits=N) = if c {` newline ...), even when that head line
overflows a little. Consecutive assignments of the same kind (such as
`const`, `mut`, `wrap`, or `type`) align unless padding causes a substantial
overflow. Another statement kind or a blank line ends the group, and so does
a statement that breaks before its `=` (`mut f:Unsigned(` newline ... `) =
0`, or a left-hand side holding a comment that keeps its line break, `reg
d // c` newline `:Unsigned(bits=W) = 0` or `wire a` newline `/* c */`
newline `:Unsigned(bits=W) = nil`): it takes no padding. A trailing comment does not end a group
(`mut p   = 1 // c` newline `mut arr = 2`). A line starting with `(` is a
statement of its own, also right after a declaration whose type could take a
`(...)` (`mut a:U8 // c` newline `(r, a) = (1, 2)`: lhd and the grammar
agree), so it aligns like any other statement. Widths and alignment count
display columns (UTF-8 code points), not bytes, so a backticked non-ASCII
name aligns like an ASCII one. A `match` arm condition breaks only when it
does not fit (`in 4..<6 {` stays whole; its arm block breaks first), and an operator after an
expression block's `}` stays on its line (`}` ` + 1`, never `}` newline
`+ 1`).

Both modes use `name=value` inside tuples, calls, defaults, attributes and
destructuring lists (`const (x=t.p1, y=t.p2) = f(a=1)`),
`, ` between flat list items, and precedence-based expression spacing such as
`i*N + j`. Commas are trailing rather than leading; the one trailing comma
that makes `(x,)` a one-element tuple (as opposed to the grouping `(x)`) is
kept. Statement assignments use `name = value`. Trailing comments remain
attached to their item, and statement branches use `} elif` / `} else`.

A comment that trails a block-opening `{` (a lambda header, `if`, `elif`,
`else`, `for`, `while`, `tick`, ...) stays on that header line
(`) -> (o:U4@[]) { // registered`); only a comment on its own line after the
`{` opens the body. A block comment on the `{` line that is followed there by
the block's first item documents that item: the block breaks and the comment
opens the body line with it (`if a > b { /* d */ r = a }` becomes `if a > b {`
newline `/* d */ r = a` newline `}`). A block comment that is a block's whole body
(`) -> (r) { /* c */ }`) also ends the header line when the block breaks, and
like any trailing comment it does not count against the header's width; so
does one right before the `}` of a block that breaks (`x /* c */ }`). A
comment between a branch's `}` and the next `elif`/`else`
(`} // after if` newline `elif q == 1 {`) does not block `} elif`: the first
such comment moves, text unchanged, to the end of that `elif`/`else` header
line (`} elif q == 1 { // after if`), and any further ones open that
branch's body on their own lines. When the `}` closes a one-line branch
(`if en { o = a } // take a` newline `else { ... }`), the comment documents
that branch instead: it stays after the branch's last statement
(`o = a // take a`), or, for an empty branch, trails its `{`
(`if en { } // idle` newline `else {` becomes `if en { // idle` newline
`} else {`). A comment between an `if`/`elif`/`else`,
`for`, `while`, `loop` or `tick` header and an Allman `{` on a later line
moves the same way (`} else` newline `// c` newline `{` becomes
`} else { // c`), and so does a block comment right before a `{` on its
line (`for x in a /* c */ {` becomes `for x in a { /* c */`). Once moved it
trails the `{`, so, like any trailing comment, it does not count against the
header's width. Moved comments keep their source order: they come first
(the first trails the `{`, the others open the body on their own lines), and
a comment the block has after its own `{` follows them on the next line
(`if a == 1 /*A*/{ /*B*/1 }` becomes `if a == 1 { /*A*/` newline `/*B*/ 1`
newline `}`). A block comment before an `elif` whose condition carries
comments of its own would pass them when moved, so it prints right after
`elif` instead (`} /*c1*/ elif /*c2*/ q {` becomes `} elif /*c1*/ /*c2*/ q {`).

A `//` comment on its own line keeps its own line also inside an
expression, at the continuation's indentation: before a continuation
operator (`const r = a` newline `  // c` newline `  or b`), before a range
operator in a selector (`v#[f` newline `// c` newline `..+4]`), before a
dotted member (`x = f(a)` newline `  // c` newline `  .g()`), or before a
lambda's `-> (...)`. What follows a comment that ends its line inside an
expression continues one indent level deeper than the statement, in both
modes and never aligned under the `=` (`const y = // c` newline `  a + 1`,
`p = a#[ // d` newline `  0..<4]`, `comb f(a:U8) // x` newline `  -> (r:U8)
{`), and a continuation line that starts with `*` reads `* c` in both modes. A block comment inside an expression keeps one space on
each side (`a /* x */ + b`), and so does a tight operator next to one
(`x / /* d */ (b + 1)`, `x /* d */ * y`). A block comment that starts its
source line keeps starting a line, also when code follows it there (`tick`
newline `/* c */ N {`, `const r = a` newline `/* c */ + b`), and code the
formatter keeps after it reads `/* c */ x`. Two places are the exception: a
block comment right after a binary operator that ended the line before, and
one right after a list comma, joins that line and leads the operand or item
it precedes (`const t = a +` newline `/* c */ b` prints `const t = a + /* c
*/ b`, `x = f(a,` newline `/* c */ b)` prints `x = f(a, /* c */ b)`); in Human mode, such a line break
in a branch of an `if` expression breaks every branch (the statement layout),
like any branch that must break. A block comment between the
operands of an operator chain counts toward the line width, also when it
ended its source line, so the chain breaks the same way on every pass
(`const b = a` newline `+ x /* c */` newline `+ y`).

`tick` prints `tick 4 {`: one space before the cycle count (mandatory; a tick
takes no `clocks=`/`resets=` clauses).

Type positions format like values: the arguments of `Unsigned(...)`,
`Signed(...)`, `U<N>(...)`/`S<N>(...)` and other type calls,
and generic type arguments, use the same `name=value`, `, ` and precedence
spacing (`Unsigned( bits = N * N )` becomes `Unsigned(bits=N*N)`, `bits=N+1`
becomes `bits=N + 1`), print on one line in AI mode, and drop the trailing
comma after a single named argument (`Signed(bits=W + 1,)` becomes
`Signed(bits=W + 1)`); a single positional argument keeps it (`Unsigned(8,)`).
Their named arguments sort like any tuple, so not inside a parameter list or a
`type` declaration (`mut t:U8(max=3, bits=4)` becomes `U8(bits=4, max=3)`).

A `;` that terminates a statement at the end of its line is dropped, since the
newline terminates it as well; statements that shared a line through `;` go
one per line, at any nesting level (`const a = 1; const b = 2` becomes two
lines). This includes
`step;` (printed `step`; a parenthesized count stays glued, `step (3)` prints
`step(3)`). A `;` stays when the next token would otherwise continue the
statement (it starts with `,` `=` `.` `<` `>` `&` `^` `|` `*` `/` `+` `-`,
`:` (a type annotation or attribute), `#` (a bit selector), `!=`,
a binary word operator, or the whole word `else`/`elif`; the grammar scanner
and prpparse, which lhd builds from this tree, match words whole, so a name
such as `els`, `elsev` or `elsewhere` starts a new statement and the `;`
before it drops; a line starting with `(` or `[` is always a new statement,
also after a declaration's type, so `mut x:U8;` newline `(r, x) = (b, a)`
drops its `;`), when it ends a body-less lambda before a `{` line (that `{`
would become the lambda's body: `comb ext(a) -> (b);` newline `{`), when a
block comment follows it on its line, and as a clause separator, which prints
as `; ` in `if`, `while`, `match` and `loop` alike (`match const t=(a=1); t
{`). A `;` means a newline, so a run of them prints once (`;;` is one `;`),
and a run between a header and its block prints nothing (`if c; {` becomes
`if c {`).

`comb`/`mod`/`pipe`/`fluid` headers always end with ` {` on the header line
(`) -> (r:U8) {`); an Allman `{` on the next line is joined, unless a comment
sits between the header and the `{`. A block comment that trails the header
with the `{` after it on its line stays there, at any nesting level
(`) -> (r:U8) /* c */ {`).

A string prints its text, escapes, `{{`/`}}` braces and format specs exactly
as written; the expression of each `{expr}` / `{expr:spec}` hole prints like
any expression (`"{a==b}"` becomes `"{a == b}"`, `"{ a }"` becomes `"{a}"`),
unless the hole holds a comment.

Integer literals print their prefix letters and hex digits in lower case
(`0XfF` becomes `0xff`, `0UB1?0` becomes `0ub1?0`). The radix, leading zeros,
`_` separators, `?` digits and `K`/`M`/`G`/`T` magnitudes are kept as written.

Trailing whitespace (spaces and tabs) at the end of a `//` comment is removed.

Redundant grouping parentheses are removed. `(e)` without a comma is only
grouping (`(e,)` is a one-element tuple and keeps its parentheses), so the
parentheses drop when they wrap: a whole `if`/`elif`/`while` condition or
`match` subject (`if (rst == 1) {` becomes `if rst == 1 {`); a whole
right-hand side of an assignment or declaration (`wrap v = (v + 1)` becomes
`wrap v = v + 1`, also after `+=`, `|=`, ..., and `const a = ([x, y])`
becomes `const a = [x, y]`: an array literal is one value); a whole named-argument or
tuple-field value (`f(update=(en and x))` becomes `f(update=en and x)`), attribute value
(`:[initial=(N + 1)]` becomes `:[initial=N + 1]`) or value-parameter default
(`a:U8=(3 + 1)` becomes `a:U8=3 + 1`); an atom (identifier, dotted name,
literal, also a negative one: `g<N=(-3)>` becomes `g<N=-3>`) that is a whole
generic binding value or generic parameter default (`g<A=(M)>` becomes
`g<A=M>`; a generic value is a type, so `g<W=(N + 1)>` and a call
`g<N=(k(a=2))>`, which would read as a type, keep them); the
lone value of an `if`/`match` branch block (`{ (a) }` becomes `{ a }`); a
comparison or an atom (identifier, selector, literal, call) that is a direct
operand of `and`/`or`/`implies` (`(a == 0) or (en)` becomes `a == 0 or en`);
an atom under `not`/`!` (`not (en)` becomes `not en`); and an atom that is an
operand of any other binary operator or a selector index
(`(a#[0..<4]) + (b)` becomes `a#[0..<4] + b`, `a#[(i)]` becomes `a#[i]`),
except a negative literal (`a*(-1)`). A chained comparison drops them too
(`(a) < (b) < (c)` becomes `a < b < c`): a `<` after a blank never opens a
generic list (owner ruling 107). Nested
`((e))` unwraps fully where the parentheses drop, and collapses to one layer
wherever the inner ones stay, whether required or a tuple (`not ((a == b))`
becomes `not (a == b)`, `((a + b))*c` becomes `(a + b)*c`, `f(x=((a, b)))`
becomes `f(x=(a, b))`, `((x,))` becomes `(x,)`, `foo(((a)))` becomes
`foo((a))`), since parentheses without a comma only group. They stay around
anything with a top-level comma, a binding (`(a=1)`), a spread, a typed
operand (`(a:U8)`), a block, lambda, `if` or `match` expression, around a
comparison under `not` (`not (a == b)`), around a unary operand of
`and`/`or` (`x or (not y)`), and wherever operator tiers mix (`(a + b)*c`,
`3 & (4*4)`, `(a << 1) | b`, `(a == b) == c`, `a and (b or c)`), since Pyrope
rejects mixing some operators without parentheses. Parentheses that contain
a comment, a positional call argument (`f((a))`), array items (`[(a)]`) and
`match` arm conditions (`== (1) {`) also keep one layer. A generic list that
holds a `<` or `>` comparison keeps every grouping parenthesis inside it and
its source order (`g<N=(a > b)>`: a bare `>` would close the list). Any
other list drops and sorts as usual: every comparison prints spaced, and a
`<` after a blank is always a comparison (owner ruling 107), so
`h(y=(c > (d + 1)), x=(a < b))` becomes `h(x=a < b, y=c > (d + 1))`, never
the generic call `a<b, y=c>(d + 1)` (which needs the `<` glued to the
callee). As a last guard, when the formatted text
would parse with a different number of calls, generic lists or comparisons
than the input, or not at all, the file is formatted again with no
parenthesis removal and no sorting.

Comparisons are always spaced: `==`, `!=`, `<`, `<=`, `>` and `>=` keep one
space on each side everywhere, also as operands of `and`, `or` and `implies`
(`a == b or c != d`, `staged_items < BUFFER_DEPTH or pop_beat`). Word
comparators (`in`, `has`, `case`, `does`, `equals`) are spaced as well.

All-named call argument lists, tuple literals, call-site generic bindings
(`f<W=M, A=K>` becomes `f<A=K, W=M>`) and attribute lists (`:[posclk=false,
async=true]` becomes `:[async=true, posclk=false]`) are sorted by name when
their values can be reordered safely from syntax alone. A `...` gather
parameter collects named arguments in call order, so call arguments sort only
when the callee resolves (as for the shorthand below) to a same-file lambda
without a gather, or is a builtin conversion on a reserved type word
(`U8(max=3, bits=4)` becomes `U8(bits=4, max=3)`; no binding can shadow a
type word, so a conversion always resolves); a call to a gathering lambda,
an imported, UFCS (`v.f(...)`), namespaced (`lib.f`) or otherwise unresolved
callee keeps source order, since its signature is not visible. A callee
counts as unresolved when its name is also used in the file as anything but
a callee (a value, an alias, a binding: `const s = f` also stops `f(b=1,
a=2)` from sorting), and a same-file lambda whose first parameter is `self`
keeps source order too. A backticked old lowercase spelling
(`` `u8`(max=3, bits=4) ``; bare `u8` is a banned word) is an ordinary,
unresolved name, so it keeps source order. The sort key ignores
backticks, so `` `in` `` sorts as `in` (between `clk` and `zed`). Values that
keep source order: spreads, `ref`s, calls with possible side effects (also a
type-position call in a generic value: `f<W=2, A=g(x)>` stays), and
tuple-field cross-references (a value that reads a sibling field by name,
`(b=1, a=b)`; a field after `.` or a nested field name is no reference, so
`(b=x.b, a=x.a)` sorts). Builtin conversions (`U1(x)`, `S<N>(x)`,
`Unsigned(x)`, `Signed(x)`, `Bool(x)`, `String(x)`: a call on any type word) and `if` /
`match` expressions whose branches hold only such values are pure and do not
block sorting. A list that contains any comment (`//` or `/* */`), a
positional item, or a repeated name keeps its source order.

A named tuple is unordered: no binding is positional (owner ruling 105), so
declarations sort too. A tuple that declares fields (`(const b=1, const
a=2)`, `(b:U8=1, a:U8=2)`), tuples in value parameter defaults
(`comb g(p=(b=1, a=2))`) and a lambda's parameter and output lists
(`comb f(ref self, b:U8, a:U8=1) -> (y:U8, x:U8)` becomes `comb f(ref self,
a:U8=1, b:U8) -> (x:U8, y:U8)`: a `self` parameter stays first) all sort.
A LAYOUT keeps its order: the right-hand side of `type X = (...)`, a tuple
type (`mut t:(b=U4, a=U4)`, also as a generic default or binding
`f<T=(b=1, a=2)>`) and an `enum` member list, because a typed tuple is
constructed positionally by type, so reordering could rebind a value.
A lambda's parameter list keeps its order while a call in the file passes
the lambda an unnamed argument that is no same-name pun (`addby(ref m,
by=2)` into `comb addby(ref x:U8, by:U8)`; `addby(ref x, by=2)` is a pun):
such a call is an error in Pyrope, but lhd still binds that argument to the
first free parameter, so sorting would rebind it. A
declaration whose initializer or default reads a sibling (`(mut z=3, a=z)`,
`f(z:U8, a:U8=z)`) keeps its order (declare before use), and so do generic
parameter lists (`<T, N>`, bound positionally at `f<U8, 3>`), `enum` member
lists (their order gives the values) and unnamed (positional) lists.

Same-name shorthand `f(x=x)` becomes `f(x)` only when the callee resolves, in
the same file, to exactly one `comb`/`mod`/`pipe`/`fluid` declared at
statement level in a block that encloses the call, and that declaration has
a parameter `x` (compared by identity: `` `x` `` is `x`, but `` `U4` `` is not the type `U4`). The name must not otherwise appear in the file:
UFCS calls (`v.f(...)`), namespaces (`lib.f`), aliases (`const g = f`),
overload sets (`[f1, f2]`), parameters or variables of that name keep
`name=value`. A `self` first parameter or a `...` gather also keeps it. So
`f(q=q)` into `comb f(a:U4)` stays `f(q=q)` and still reports the unknown
argument. For a resolved callee, a bare argument that names one of its
parameters counts as `x=x` and sorts under its name, so `f(rst=rst, a=1)`,
`f(rst, a=1)` and `f(a=1, rst)` all print `f(a=1, rst)`. For any other
callee, a bare argument keeps the whole list in source order. The formatter
never expands a bare argument or removes one.

Backticks: `` `foo` `` and `foo` are the same identifier whenever the text
inside the backticks is made only of identifier characters (letters,
including non-ASCII letters such as `é`, digits and `_`) and does not start
with a digit, so the formatter drops them (`` `foo` `` prints `foo`,
``Unsigned(`bits`=2)`` prints `Unsigned(bits=2)`, `` `assume`(c) `` prints
`assume(c)`, `` x.`e0` `` prints `x.e0`, `` `é` `` prints `é`, `` `_1_a` ``
prints `_1_a`, `` `elsewhere` `` prints `elsewhere`). They stay when dropping
them would change how the source lexes or what it means: any other character
(`` `foo[bar]` `` is not `foo[bar]`, nor `` `foo$bar` ``, `` `a b` ``,
`` `x.y` `` or `` `a·b` ``; `$` is no identifier character), a leading digit
(`` `3a` ``), `_` alone, a reserved placeholder (`_` then a digit, then only
letters/digits: `` `_0` ``, `` `_12` ``, `` `_1a` ``), a keyword of the grammar or of prpparse's lexer
(`` `in` ``, `` `stage` ``, `` `else` ``), a type word (`` `U4` ``,
`` `S20` ``, `` `Unsigned` ``, `` `Signed` ``, `` `Bool` ``, `` `String` ``,
`` `Clock` ``, `` `Reset` ``: every `U<N>`/`S<N>` and those six words are
reserved, so the bare `U4` is always the type and `` `U4` `` an ordinary
name),
and `` `true` ``/`` `false` ``/`` `nil` `` (lhd reads the bare words as the
literals, the backticked ones as names). Whether a non-ASCII word is a name
is decided by the grammar itself (`\p{L}` letters and `\p{Nd}` digits). The
sort ORDER ignores backticks; the `x=x` shorthand, parameter lookup and
repeated-name checks compare names by identity, so `` `foo` `` matches `foo`
but `` `U4` `` never matches the type `U4` (`f(`U4`=U4)` stays as written).
Reserved-word matching is CASE-SENSITIVE: only the exact spelling is
reserved. `` `clock` ``, `` `reset` ``, `` `IF` ``, `` `If` `` and `` `I32` ``
are ordinary names and lose their backticks (`clock`, `reset`, `IF`, `If`,
`I32`), while `` `Clock` ``, `` `Reset` ``, `` `U8` ``, `` `if` `` and
`` `u8` `` keep them.
The old lowercase type spellings (`u`/`s`/`i` followed by digits such as
`u8`, `s4`, `i32`, and `bool`, `boolean`, `unsigned`, `signed`, `string`) are
BANNED words: bare they are a syntax error in every position, backticked they
are ordinary names, so `` `u8` `` keeps its backticks. Words that only look
like them (`int`, `uint`, `u8x`, `booleans`) are ordinary names.

Block comments: a block comment always has whitespace on both sides, a space
or a line break; the formatter never glues one to a neighboring token. Inside
a line it takes one space on each side, also next to brackets, commas, `;` and
tight operators: `f( /* c */ a)`, `a#[0..<2 /* c */ ]`, `g<W=1 /* b */ >(a)`,
`a:U4 /* g */ = /* h */ 3`, `x /* c */ , y`, `mut x = a /* c */ ;`,
`Unsigned /* c */ (bits=4)`. A block comment inside a list stays with the
item it follows or leads: `f(b=y, a=x /* c */ )`, `f(b=y, /* c */ a=x)`, and
`f(a=x /* c */ , b=y)`. A block comment on its own line before the closing
`)` trails the last item (`f(` newline `a,` newline `/* c */` newline `)`
prints `f(a /* c */ )`, as `f(a, /* c */)` does), unless that item ends with a
line comment; one after an item's line comment (`a // x` newline `/* c */ ,
b`) leads the next item (`a, // x` newline `/* c */ b,`). One right after a
comma on its line stays after that comma, also in a leading-comma list (`a`
newline `, /* c */` newline `b` prints `a, /* c */ b`). Inside a block, a
statement that follows a block comment on its line stays on that line
(`/* c */ x = 1`), as at the top level.

Blank lines: a run of blank lines between two statements (or two `match`
arms) prints as one blank line. Blank lines at the start and the end of the
file, right after a block's `{` and right before its `}`, and inside a list
(call arguments, tuple, parameters) are removed, so an empty file, or one of
only blank lines, formats to an empty file. Both modes handle blank
lines alike.

Block comments nest, in the grammar as in lhd: `/* a /* b */ c */` is one
comment, and an inner `*/` does not end it (`/* old /* note */ if a == 0 */`
is all comment). A nested comment formats like any other block comment, and
its text, inner comments included, prints byte for byte.

```bash
./prpfmt input.prp                         # AI layout
./prpfmt --mode human input.prp            # 132-column Human layout
./prpfmt --mode human --width 100 input.prp
```

Formatting runs on its own thread with a large stack, so deeply nested input
(thousands of nested `if` expressions, calls, tuples or grouping parentheses)
formats instead of overflowing the stack, in time linear in the nesting
depth. In Human mode an expression that does not fit breaks at every level,
one indent deeper each time, so the output of thousands of nested levels
(`(a + (a + ...))`) grows with the square of the depth, and so does the time
to write and re-check it; a file whose parse tree is more than 200000 levels
deep is refused (`Error: the input nests too deeply ...`, exit 1, `-i`/`-o`
leave their file untouched; the API returns 4).

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
