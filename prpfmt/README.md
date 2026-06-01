# Overview

Pyrope code currently lacks a robust, standardized formatter. Without consistent formatting (indentation, spacing, alignment), code style varies widely and becomes harder to read and review.

This tool is a formatter (like `clang-format`) for Pyrope which traverses the Pyrope tree-sitter grammar and emits standardized formatted Pyrope.

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
    │   ├── parser.c
    │   └── scanner.c
    └── prpfmt/
        ├── Makefile
        ├── README.md
        ├── main.c
        ├── prpfmt.c
        └── prpfmt.h
```

### Usage

#### Build
To build the formatter, navigate to the `prpfmt` directory and run:
```bash
make
```
This will create the `prpfmt` executable in the `prpfmt` directory.

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

1. **Re-generate Parser**: Run `tree-sitter generate` in the `tree-sitter-pyrope` directory.
2. **Update Symbol IDs**: The symbol IDs in `prpfmt.h` must match those in `tree-sitter-pyrope/src/parser.c`.
    - Open `tree-sitter-pyrope/src/parser.c`.
    - Locate the `ts_symbol_identifiers` enum.
    - Copy these values into the `enum` in `prpfmt.h`.
3. **Handle Structural Changes**: If grammar rules were renamed or their structure changed (e.g., tiered binary expressions), update the corresponding `print_` functions in `prpfmt.c`.
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
