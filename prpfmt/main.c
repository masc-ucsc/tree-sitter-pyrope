#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tree_sitter/api.h>

#include "prpfmt.h"

double get_time_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

void print_help() {
  printf("Usage: ./prpfmt <input_file> [-o <output_file>] [-i <indent_size>] [-w <max_width>] [-v] [-b]\n");
  printf("       ./prpfmt [-h | --help]\n\n");
  printf("Options:\n");
  printf("  -o <output_file>  Specify an output file. If not provided, output to stdout.\n");
  printf("  -i, --indent <n>  Specify the indentation size (default: 4).\n");
  printf("  -w, --width <n>   Specify the maximum line width (default: 80).\n");
  printf("  -v, --verify      Verify that the formatted output is still parseable.\n");
  printf("  -b, --bench       Run in benchmark mode and print timing statistics.\n");
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

  // Read file content into buffer. fread returns 0 for a zero-length file,
  // which is not an error: an empty source is a valid (empty) program.
  if (l_size > 0 && fread(buffer, l_size, 1, fp) != 1) {
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
  int max_width = 80;
  bool verify_output = false;
  bool run_benchmark = false;

  // Parse for -o, -i, -w options
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
    } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--width") == 0) {
      if (i + 1 < argc) {
        max_width = atoi(argv[i + 1]);
        i++;
      } else {
        fprintf(stderr, "Error: -w/--width requires an integer value.\n");
        print_help();
        exit(1);
      }
    } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verify") == 0) {
      verify_output = true;
    } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bench") == 0) {
      run_benchmark = true;
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
  
  double parse_start = 0, parse_end = 0;
  if (run_benchmark) parse_start = get_time_ms();
  
  TSTree *tree =
      ts_parser_parse_string(parser, NULL, source_code, strlen(source_code));

  if (run_benchmark) parse_end = get_time_ms();

  // Check if tree has any ERROR or MISSING nodes
  TSNode root = ts_tree_root_node(tree);

  if (ts_node_has_error(root)) {
    fprintf(stderr, "Error: the provided code was unable to be parsed.\n"
                    "Run: `tree-sitter parse -c /path/to/file` and look"
                    " for MISSING or ERROR nodes.\n");
    cleanup(source_code, tree, parser, outfile);
    exit(2);
  }
// Initialize state
PrpfmtState state = {
  .source_code = source_code,
  .outfile = outfile,
  .indent_size = indent_size,
  .max_width = max_width,
  .in_assert = false,
  .allow_inline = false,
  .nesting_level = 0,
  .fmt_on = true,
  .inline_exp = false,
  .buffer = { .data = NULL, .size = 0, .capacity = 0 }
};

  double format_start = 0, format_end = 0;
  if (run_benchmark) format_start = get_time_ms();

  print_description(tree, &state);
  prpfmt_solve(&state);

  bool parse_error = false;
  if (verify_output) {
    // Render to a memory buffer first to verify the output
    char *formatted_buf = NULL;
    size_t formatted_size = 0;
    FILE *mem_stream = open_memstream(&formatted_buf, &formatted_size);
    if (!mem_stream) {
      perror("open_memstream failed");
      exit(1);
    }

    // Temporary swap outfile to capture render
    state.outfile = mem_stream;
    prpfmt_render(&state);
    if (run_benchmark) format_end = get_time_ms();
    fclose(mem_stream);

    // Verify the formatted output
    TSTree *verify_tree = ts_parser_parse_string(parser, NULL, formatted_buf, formatted_size);
    TSNode verify_root = ts_tree_root_node(verify_tree);
    parse_error = ts_node_has_error(verify_root);
    
    // Now write to the actual destination
    if (outfile == stdout) {
      printf("%s", formatted_buf);
    } else {
      fwrite(formatted_buf, 1, formatted_size, outfile);
    }

    if (parse_error) {
      fprintf(stderr, "\n----------------------------------------------------------------\n");
      fprintf(stderr, "WARNING: Formatted output contains PARSE ERRORS!\n");
      fprintf(stderr, "This suggests an unsafe break or a bug in the formatter logic.\n");
      fprintf(stderr, "Please check the output carefully.\n");
      fprintf(stderr, "----------------------------------------------------------------\n\n");
    }

    ts_tree_delete(verify_tree);
    free(formatted_buf);
  } else {
    // Direct rendering to outfile
    prpfmt_render(&state);
    if (run_benchmark) format_end = get_time_ms();
  }

  prpfmt_free_buffer(&state);

  if (run_benchmark) {
    double parse_time = parse_end - parse_start;
    double format_time = format_end - format_start;
    double total_time = parse_time + format_time;
    size_t file_bytes = strlen(source_code);
    double mb_per_sec = (file_bytes / 1024.0 / 1024.0) / (total_time / 1000.0);
    
    fprintf(stderr, "\nBenchmark Results:\n");
    fprintf(stderr, "------------------\n");
    fprintf(stderr, "File Size:   %.2f KB\n", file_bytes / 1024.0);
    fprintf(stderr, "Parse Time:  %.3f ms\n", parse_time);
    fprintf(stderr, "Format Time: %.3f ms\n", format_time);
    fprintf(stderr, "Total Time:  %.3f ms\n", total_time);
    fprintf(stderr, "Throughput:  %.2f MB/s\n\n", mb_per_sec);
  }

  // Free memory
  cleanup(source_code, tree, parser, outfile);

  return parse_error ? 3 : 0;
}
