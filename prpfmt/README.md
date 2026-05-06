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
This will create the `prpfmt` executable in the `project-root` directory.

#### Run
You can run the formatter from the `project-root` directory:
```bash
./prpfmt <input_file> [-o <output_file>] [-i <indent_size>]
./prpfmt [-h | --help]
```

**Options:**
- `-o <output_file>`: Specify an output file. If not provided, the formatted code will be printed to stdout.
- `-i, --indent <n>`: Specify the indentation size (default: 4).
- `-h, --help`: Display the help message.

## Grammar Updates
If the Pyrope grammar (`grammar.js`) is updated, the following steps must be taken to synchronize the formatter:

1. **Re-generate Parser**: Run `tree-sitter generate` in the `tree-sitter-pyrope` directory.
2. **Update Symbol IDs**: The symbol IDs in `prpfmt.h` must match those in `tree-sitter-pyrope/src/parser.c`.
    - Open `tree-sitter-pyrope/src/parser.c`.
    - Locate the `ts_symbol_identifiers` enum.
    - Copy these values into the `enum` in `prpfmt.h`.
3. **Handle Structural Changes**: If grammar rules were renamed or their structure changed (e.g., tiered binary expressions), update the corresponding `print_` functions in `prpfmt.c`.
4. **Rebuild**: Run `make` in the `prpfmt` directory to recompile the tool with the updated parser and symbol definitions.

## Known Issues/Future Work
- Vertical alignment
- Multi-line statements with proper indentation/formatting
- Handling comments within certain statements (ex: comments in a tuple)

## References

- tree-sitter-pyrope: https://github.com/masc-ucsc/tree-sitter-pyrope
- tree-sitter docs: https://tree-sitter.github.io/tree-sitter/index.html
