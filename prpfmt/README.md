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

## References

- tree-sitter-pyrope: https://github.com/masc-ucsc/tree-sitter-pyrope
- tree-sitter docs: https://tree-sitter.github.io/tree-sitter/index.html
