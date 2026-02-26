#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

#include "prpfmt.h"

void print_help() {
  printf("Usage: ./prpfmt <input_file> [-o <output_file>] [-i <indent_size>]\n");
  printf("       ./prpfmt [-h | --help]\n\n");
  printf("Options:\n");
  printf("  -o <output_file>  Specify an output file. If not provided, output to stdout.\n");
  printf("  -i, --indent <n>  Specify the indentation size (default: 4).\n");
  printf("  -h, --help        Display this help message.\n");
}

char *file_to_string(char *infile) {
  char *buffer;
  FILE *fp = fopen(infile, "r");

  // Attempt to open file
  if (!fp) {
    perror(infile);
    exit(1);
  }

  // Determine file size
  fseek(fp, 0L, SEEK_END);
  long l_size = ftell(fp);
  rewind(fp);

  // Allocate memory for file content
  buffer = malloc(l_size + 1);
  if (!buffer) {
    fclose(fp);
    fprintf(stderr, "Memory allocation failed");
    exit(1);
  }

  // Read file content into buffer
  if (fread(buffer, l_size, 1, fp) != 1) {
    fclose(fp);
    free(buffer);
    fprintf(stderr, "File read failed");
    exit(1);
  }

  buffer[l_size] = '\0';
  fclose(fp);

  return buffer;
}

void cleanup(char *source_code, TSTree *tree, TSParser *parser, FILE *outfile) {
  // Free any allocated memory
  if (source_code) {
    free(source_code);
  }
  if (tree) {
    ts_tree_delete(tree);
  }
  if (parser) {
    ts_parser_delete(parser);
  }
  if (outfile && outfile != stdout) {
    fclose(outfile);
  }
}

int main(int argc, char **argv) {
  // Check if input file provided
  if (argc < 2) {
    fprintf(stderr, "Error: Input file path is required.\n");
    print_help();
    exit(1);
  }

  // Parse for -h option
  if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    print_help();
    return 0;
  }
  
  char *infile_path = argv[1];
  char *outfile_path = NULL;
  int indent_size = 4;

  // Parse for -o, -i options
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "-o") == 0) {
      if (i + 1 < argc) {
        outfile_path = argv[i + 1];
        i++;
      } else {
        fprintf(stderr, "Error: -o requires an output file path.\n");
        print_help();
        exit(1);
      }
    } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--indent") == 0) {
      if (i + 1 < argc) {
        indent_size = atoi(argv[i + 1]);
        i++;
      } else {
        fprintf(stderr, "Error: -i/--indent requires an integer value.\n");
        print_help();
        exit(1);
      }
    } else {
      fprintf(stderr, "Error: Invalid argument '%s'.\n", argv[i]);
      print_help();
      exit(1);
    }
  }
  
  // Set output file
  FILE *outfile = stdout;
  if (outfile_path) {
    outfile = fopen(outfile_path, "w");
    if (!outfile) {
      perror("Error opening output file");
      exit(1);
    }
  }

  // Tree-sitter parser initialization
  TSLanguage *tree_sitter_pyrope();
  TSParser *parser = ts_parser_new();
  if (!ts_parser_set_language(parser, tree_sitter_pyrope())) {
    fprintf(stderr, "Error: the language was generated with an "
                    "incompatible version of the tree-sitter CLI.\n");
    cleanup(NULL, NULL, parser, outfile);
    exit(1);
  }

  // Parse the source code
  char *source_code = file_to_string(infile_path);
  TSTree *tree =
      ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));

  // Check if tree has any ERROR or MISSING nodes
  TSNode root = ts_tree_root_node(tree);

  if (ts_node_has_error(root)) {
    fprintf(stderr, "Error: the provided code was unable to be parsed.\n");
    cleanup(source_code, tree, parser, outfile);
    exit(1);
  }

  // Initialize state
  PrpfmtState state = {
    .source_code = source_code,
    .outfile = outfile,
    .indent_level = 0,
    .indent_size = indent_size,
    .fmt_on = true,
    .inline_exp = false
  };

  // test_print_all_nodes(tree, source_code);
  print_tree(tree, &state);

  // Free memory
  cleanup(source_code, tree, parser, outfile);

  return 0;
}
