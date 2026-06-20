CXX = clang++
CC  = clang

# prpfmt is C++20. The generated tree-sitter parser (../src/parser.c) and the
# hand-written scanner (../src/scanner.c) are C and are compiled as C, then
# linked with the C++ objects. Splitting the build into per-file objects keeps
# the (large, slow) generated parser from recompiling on every formatter edit.
CXXSTD  = -std=c++20
WARN    = -Wall -Wextra -Wpedantic
# Hardening: turn buffer/iterator misuse into trapping checks instead of silent
# corruption. _GLIBCXX_ASSERTIONS bounds-checks std::vector/array/string_view,
# the stack protector and FORTIFY catch classic C-style overruns. Cheap at -O2.
HARDEN  = -D_GLIBCXX_ASSERTIONS -fstack-protector-strong -D_FORTIFY_SOURCE=2
OPT     = -O2
CXXFLAGS = $(CXXSTD) $(WARN) $(HARDEN) $(OPT)
# The generated/external C is not ours to lint; -Wall only (no -Wextra) keeps
# the scanner's unused-parameter noise out of the build.
CFLAGS   = $(OPT) -Wall

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

CXXFLAGS += $(TS_CFLAGS)
CFLAGS   += $(TS_CFLAGS)

# The generated parser. Regenerated from ../grammar.js by `npm run generate`.
# scanner.c is hand-written (not generated), so it is NOT an output here.
GENERATED_PARSER = ../src/parser.c
SCANNER          = ../src/scanner.c
GRAMMAR          = ../grammar.js

# C++ formatter sources and their objects.
CXX_SRC = prpfmt.cc main.cc ir.cc
CXX_OBJ = $(CXX_SRC:.cc=.o)
# C tree-sitter objects (compiled as C, linked in).
C_OBJ   = parser.o scanner.o
OBJ     = $(CXX_OBJ) $(C_OBJ)

# Output executable path (placed locally in the prpfmt folder)
TARGET = prpfmt

# Symbol-id table lifted from the generated parser. prpfmt.h #includes this;
# regenerating it whenever parser.c changes keeps the formatter's node dispatch
# in sync with the grammar (tree-sitter renumbers the ids on every grammar edit).
SYMBOLS_HDR = ts_symbols.h
HEADERS     = prpfmt.h ir.h $(SYMBOLS_HDR)

.PHONY: all clean debug

all: $(TARGET)

# `make debug` rebuilds with AddressSanitizer + UndefinedBehaviorSanitizer so
# any out-of-bounds access or UB in the formatter (or the C parser/scanner it
# links) traps at runtime. Use it when changing the IR/solver.
#
# `clean` and the build must NOT be same-level prerequisites: make would decide
# the target's staleness against the pre-clean timestamps, possibly skip the
# build, and then `clean` would delete the binary -- leaving nothing. Run clean
# and a fresh sanitizer build as ordered recipe steps via recursive make (the
# OPT override on the command line propagates and forces the sanitizer flags).
debug:
	$(MAKE) clean
	$(MAKE) $(TARGET) OPT='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'

# Editing the grammar regenerates the parser, which in turn forces prpfmt to
# relink -- `make` alone stays in sync with grammar.js, no manual
# `npm run generate` step to forget.
$(GENERATED_PARSER): $(GRAMMAR)
	cd .. && npm run generate

$(SYMBOLS_HDR): $(GENERATED_PARSER) gen_symbols.sh
	sh gen_symbols.sh $(GENERATED_PARSER) $(SYMBOLS_HDR)

# C++ formatter objects.
$(CXX_OBJ): %.o: %.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# C tree-sitter objects (parser is generated, scanner is hand-written).
parser.o: $(GENERATED_PARSER)
	$(CC) $(CFLAGS) -c $< -o $@
scanner.o: $(SCANNER)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) $(TS_LIBS) -o $(TARGET)

clean:
	rm -f $(TARGET) $(SYMBOLS_HDR) $(OBJ)
