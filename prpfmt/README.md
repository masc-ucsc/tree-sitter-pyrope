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
./prpfmt <input_file> [options]
```

**Options:**
- `-o <file>`        : Specify an output file (default: stdout).
- `-i, --indent <n>` : Specify indentation size (default: 4).
- `-w, --width <n>`  : Specify maximum line width (default: 80).
- `-v, --verify`     : Verify that the formatted output is still valid Pyrope.
- `-b, --bench`      : Run in benchmark mode and print timing statistics.
- `-h, --help`       : Display the help message.

## Grammar Updates
If the Pyrope grammar (`grammar.js`) is updated, the following steps must be taken to synchronize the formatter:

1. **Re-generate Parser**: Run `tree-sitter generate` in the `tree-sitter-pyrope` directory (the `make` rule does this automatically when `grammar.js` changes).
2. **Symbol IDs (automatic)**: `ts_symbols.h` is regenerated from `src/parser.c` by `gen_symbols.sh` on every build, so the formatter's node-dispatch ids always match the grammar. Never hand-edit `ts_symbols.h`.
3. **Handle Structural Changes**: If grammar rules were renamed or their structure changed (e.g., tiered binary expressions), update the corresponding `print_` functions in `prpfmt.cc`.
4. **Rebuild**: Run `make` in the `prpfmt` directory to recompile the tool with the updated parser and symbol definitions.

## Future Development & Improvement Points

### 1. Comma Harmonization (Tuples)
Currently, the formatter is "reactive" to comma placement: it preserves a leading comma only if the user manually placed it on a new line.
- Implement a harmonization pass. If the first comma in a list is leading, force all subsequent commas in that list to the leading position to ensure style consistency.

### 2. Prevent Aggressive Inlining / Modify Solver
Currently, any scope block with 0 or 1 statement is eligible for inline formatting (e.g., `{ a = 1 }`).
- Better respect user intent by keeping blocks vertical if they were originally multi-line in the source, even if they contain only a single statement.
- Modify breakpoint and overflow penalties as necessary

### 3. Configuration Management
- Add support for a `.prpfmt` (JSON or YAML) configuration file to standardize formatting across projects.

## Testing
A Python-based verification suite is located in the `tests/` directory. For details, see [tests/README.md](./tests/README.md).

## References

- tree-sitter-pyrope: https://github.com/masc-ucsc/tree-sitter-pyrope
- tree-sitter docs: https://tree-sitter.github.io/tree-sitter/index.html
