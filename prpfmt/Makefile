CC = clang
CFLAGS = -Wall -Wextra

# --- tree-sitter runtime -----------------------------------------------------
# `npm run generate` (tree-sitter generate) only emits the generated parser
# (../src/parser.c) and its build headers (../src/tree_sitter/{parser,array,
# alloc}.h). It does NOT provide the tree-sitter *runtime* -- the
# <tree_sitter/api.h> header and libtree-sitter that this tool links against
# (ts_parser_new, ts_parser_parse_string, ...). Locate the runtime via
# pkg-config (e.g. `brew install tree-sitter`), falling back to a sibling
# tree-sitter source checkout at ../../tree-sitter.
TS_PC := $(shell pkg-config --exists tree-sitter 2>/dev/null && echo yes)
ifeq ($(TS_PC),yes)
  TS_CFLAGS := $(shell pkg-config --cflags tree-sitter)
  TS_LIBS   := $(shell pkg-config --libs tree-sitter)
else
  TS_DIR    := ../../tree-sitter
  TS_CFLAGS := -I$(TS_DIR)/lib/include
  TS_LIBS   := $(TS_DIR)/libtree-sitter.a
endif

CFLAGS += $(TS_CFLAGS)

# The generated parser. Regenerated from ../grammar.js by `npm run generate`.
# scanner.c is hand-written (not generated), so it is NOT an output here.
GENERATED_PARSER = ../src/parser.c
GRAMMAR          = ../grammar.js

# Source files (../src/parser.c is produced by `npm run generate`)
SRC = prpfmt.c main.c ir.c $(GENERATED_PARSER) ../src/scanner.c

# Output executable path (placed locally in the prpfmt folder)
TARGET = prpfmt

.PHONY: all clean

all: $(TARGET)

# Editing the grammar regenerates the parser, which in turn forces prpfmt to
# relink -- `make` alone stays in sync with grammar.js, no manual
# `npm run generate` step to forget.
$(GENERATED_PARSER): $(GRAMMAR)
	cd .. && npm run generate

$(TARGET): $(SRC) prpfmt.h
	$(CC) $(CFLAGS) $(SRC) $(TS_LIBS) -o $(TARGET)

clean:
	rm -f $(TARGET)
