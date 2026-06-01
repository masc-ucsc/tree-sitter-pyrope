CC = clang
CFLAGS = -I../../tree-sitter/lib/include -Wall -Wextra
LDFLAGS = ../../tree-sitter/libtree-sitter.a

# Source files
SRC = prpfmt.c main.c ir.c ../src/parser.c ../src/scanner.c

# Output executable path (placed locally in the prpfmt folder)
TARGET = prpfmt

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) prpfmt.h
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)
