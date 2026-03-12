CC = clang
CFLAGS = -I../../tree-sitter/lib/include -Wall -Wextra
LDFLAGS = ../../tree-sitter/libtree-sitter.a

# Object files to be created in the current directory
OBJ = prpfmt.o main.o parser.o scanner.o

# Output executable path (kept in the project-root folder)
TARGET = ../../prpfmt

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $(TARGET)

# Compile local source files
prpfmt.o: prpfmt.c prpfmt.h
	$(CC) $(CFLAGS) -c prpfmt.c -o prpfmt.o

main.o: main.c prpfmt.h
	$(CC) $(CFLAGS) -c main.c -o main.o

# Compile parser and scanner from the src directory
parser.o: ../src/parser.c
	$(CC) $(CFLAGS) -c ../src/parser.c -o parser.o

scanner.o: ../src/scanner.c
	$(CC) $(CFLAGS) -c ../src/scanner.c -o scanner.o

clean:
	rm -f $(OBJ) $(TARGET)
