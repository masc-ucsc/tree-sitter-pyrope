CC = clang
CFLAGS = -I../../tree-sitter/lib/include -Wall -Wextra
LDFLAGS = ../../tree-sitter/libtree-sitter.a

# Source files
SRC = prpfmt.c main.c ../src/parser.c ../src/scanner.c

# Output executable path (kept in the capstone root folder)
TARGET = ../../prpfmt

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) prpfmt.h
	$(CC) $(CFLAGS) $(SRC) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)
