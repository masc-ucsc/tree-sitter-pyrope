#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>

#include "prpfmt.h"

// The generated parser exports this with C linkage; declare it as such so the
// C++ linker resolves the unmangled symbol from parser.o.
extern "C" const TSLanguage *tree_sitter_pyrope(void);

// RAII deleters so the tree-sitter parser/tree free themselves instead of
// needing a hand-rolled cleanup() reached from every error path.
struct TSParserDelete {
  void operator()(TSParser *p) const noexcept { ts_parser_delete(p); }
};
struct TSTreeDelete {
  void operator()(TSTree *t) const noexcept { ts_tree_delete(t); }
};
using ParserPtr = std::unique_ptr<TSParser, TSParserDelete>;
using TreePtr = std::unique_ptr<TSTree, TSTreeDelete>;

double get_time_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

void print_help() {
  printf("Usage: prpfmt [options] <input_file> [options]\n\n");
  printf("Options:\n");
  printf("  -i, --inplace     Rewrite the input file in place.\n");
  printf("  -o, --output FILE Write to FILE instead of stdout.\n");
  printf("      --indent N   Spaces per indent level (default: 2).\n");
  printf("      --mode MODE  ai (default) or human.\n");
  printf("      --width N    Human-mode soft width target (default: 132; ignored in AI).\n");
  printf("  -v, --verify     Verify that the formatted output is still parseable.\n");
  printf("  -b, --bench      Print timing statistics.\n");
  printf("  -h, --help       Display this help message.\n");
  printf("      --           Treat following arguments as file names.\n");
}

// Read the whole file into an owned std::string (RAII; no manual malloc/free).
std::string file_to_string(const char *infile) {
  FILE *fp = fopen(infile, "r");
  if (!fp) {
    perror(infile);
    exit(1);
  }

  // Determine file size
  fseek(fp, 0L, SEEK_END);
  long l_size = ftell(fp);
  rewind(fp);

  // Read file content. fread returns 0 for a zero-length file, which is not an
  // error: an empty source is a valid (empty) program.
  std::string buffer;
  if (l_size > 0) {
    buffer.resize((size_t)l_size);
    if (fread(buffer.data(), (size_t)l_size, 1, fp) != 1) {
      fclose(fp);
      fprintf(stderr, "File read failed");
      exit(1);
    }
  }

  fclose(fp);
  return buffer;
}

int main(int argc, char **argv) {
  const char *infile_path = nullptr;
  const char *outfile_path = nullptr;
  int indent_size = 2;
  int max_width = 132;
  PrpfmtMode mode = PRPFMT_AI;
  bool verify_output = false;
  bool run_benchmark = false;
  bool inplace = false;
  bool positional_only = false;
  const auto error = [](const std::string &message) {
    fprintf(stderr, "Error: %s\n", message.c_str());
    return 1;
  };

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (!positional_only && arg == "--") {
      positional_only = true;
    } else if (!positional_only && (arg == "-h" || arg == "--help")) {
      print_help();
      return 0;
    } else if (!positional_only && (arg == "-i" || arg == "--inplace")) {
      inplace = true;
    } else if (!positional_only && (arg == "-v" || arg == "--verify")) {
      verify_output = true;
    } else if (!positional_only && (arg == "-b" || arg == "--bench")) {
      run_benchmark = true;
    } else if (!positional_only &&
               (arg == "-o" || arg == "--output" || arg == "--indent" || arg == "--width" || arg == "--mode" ||
                arg.starts_with("--output=") || arg.starts_with("--indent=") || arg.starts_with("--width=") ||
                arg.starts_with("--mode="))) {
      const auto eq = arg.find('=');
      const auto option = arg.substr(0, eq);
      const char *value = nullptr;
      if (eq != std::string_view::npos) {
        value = argv[i] + eq + 1;
      } else if (i + 1 < argc) {
        value = argv[++i];
      } else {
        return error(std::string(option) + " requires a value");
      }
      if (option == "-o" || option == "--output") {
        if (*value == '\0') return error("output path must not be empty");
        outfile_path = value;
      } else if (option == "--mode") {
        if (std::string_view(value) == "ai") mode = PRPFMT_AI;
        else if (std::string_view(value) == "human") mode = PRPFMT_HUMAN;
        else return error("--mode expects ai or human");
      } else {
        const std::string_view text(value);
        int number = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), number);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || number <= 0) {
          return error(std::string(option) + " expects a positive integer");
        }
        (option == "--indent" ? indent_size : max_width) = number;
      }
    } else if (!positional_only && arg.starts_with('-')) {
      return error("unknown option '" + std::string(arg) + "'");
    } else if (infile_path) {
      return error("expected one input file");
    } else {
      infile_path = argv[i];
    }
  }
  if (!infile_path) return error("input file path is required");
  if (inplace && outfile_path) return error("-i and -o are mutually exclusive");
  if (inplace) outfile_path = infile_path;
  FILE *outfile = stdout;

  // Tree-sitter parser initialization
  ParserPtr parser(ts_parser_new());
  if (!ts_parser_set_language(parser.get(), tree_sitter_pyrope())) {
    fprintf(stderr, "Error: the language was generated with an "
                    "incompatible version of the tree-sitter CLI.\n");
    exit(1);
  }

  // Parse the source code
  std::string source_code = file_to_string(infile_path);

  double parse_start = 0, parse_end = 0;
  if (run_benchmark) parse_start = get_time_ms();

  TreePtr tree(ts_parser_parse_string(parser.get(), nullptr,
                                      source_code.data(), source_code.size()));

  if (run_benchmark) parse_end = get_time_ms();

  // Check if tree has any ERROR or MISSING nodes
  TSNode root = ts_tree_root_node(tree.get());

  if (ts_node_has_error(root)) {
    fprintf(stderr, "Error: the provided code was unable to be parsed.\n"
                    "Run: `tree-sitter parse -c /path/to/file` and look"
                    " for MISSING or ERROR nodes.\n");
    if (outfile != stdout) fclose(outfile);
    return 2;  // RAII frees parser/tree/source_code on the way out
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
    .buffer = {},
    .mode = mode,
  };

  double format_start = 0, format_end = 0;
  if (run_benchmark) format_start = get_time_ms();

  print_description(tree.get(), state);
  prpfmt_solve(state);

  bool parse_error = false;
  {
    // Render completely before opening an output path, especially for -i.
    char *formatted_buf = nullptr;
    size_t formatted_size = 0;
    FILE *mem_stream = open_memstream(&formatted_buf, &formatted_size);
    if (!mem_stream) {
      perror("open_memstream failed");
      exit(1);
    }

    // Temporary swap outfile to capture render
    state.outfile = mem_stream;
    prpfmt_render(state);
    if (run_benchmark) format_end = get_time_ms();
    fclose(mem_stream);

    // In-place edits always verify before touching the source file.
    if (verify_output || inplace) {
      TreePtr verify_tree(ts_parser_parse_string(parser.get(), nullptr,
                                                 formatted_buf, formatted_size));
      parse_error = ts_node_has_error(ts_tree_root_node(verify_tree.get()));
    }
    // A failed verify must never clobber a FILE destination (-o/-i): that is the
    // whole point of verifying before writing. stdout is different -- it destroys
    // nothing, and the broken text is exactly what the user (and
    // prpfmt/tests/prpfmt_debug.py, which reads stdout from `prpfmt <f> -v`
    // regardless of exit code) needs in order to see the bug.
    if (!parse_error || !outfile_path) {
      if (outfile_path) {
        outfile = fopen(outfile_path, "w");
        if (!outfile) {
          perror(outfile_path);
          free(formatted_buf);
          return 1;
        }
      }
      if (fwrite(formatted_buf, 1, formatted_size, outfile) != formatted_size) {
        perror("writing formatted output");
        free(formatted_buf);
        if (outfile != stdout) fclose(outfile);
        return 1;
      }
    }

    if (parse_error) {
      fprintf(stderr, "\n----------------------------------------------------------------\n");
      fprintf(stderr, "WARNING: Formatted output contains PARSE ERRORS!\n");
      fprintf(stderr, "This suggests an unsafe break or a bug in the formatter logic.\n");
      if (outfile_path) {
        fprintf(stderr, "%s was left UNCHANGED; re-run without -o/-i to inspect the broken output.\n", outfile_path);
      } else {
        fprintf(stderr, "Please check the output carefully.\n");
      }
      fprintf(stderr, "----------------------------------------------------------------\n\n");
    }

    free(formatted_buf);  // open_memstream allocates with malloc; free is required
  }

  if (run_benchmark) {
    double parse_time = parse_end - parse_start;
    double format_time = format_end - format_start;
    double total_time = parse_time + format_time;
    size_t file_bytes = source_code.size();
    double mb_per_sec = (file_bytes / 1024.0 / 1024.0) / (total_time / 1000.0);

    fprintf(stderr, "\nBenchmark Results:\n");
    fprintf(stderr, "------------------\n");
    fprintf(stderr, "File Size:   %.2f KB\n", file_bytes / 1024.0);
    fprintf(stderr, "Parse Time:  %.3f ms\n", parse_time);
    fprintf(stderr, "Format Time: %.3f ms\n", format_time);
    fprintf(stderr, "Total Time:  %.3f ms\n", total_time);
    fprintf(stderr, "Throughput:  %.2f MB/s\n\n", mb_per_sec);
  }

  if (outfile != stdout && fclose(outfile) != 0) {
    perror("closing formatted output");
    return 1;
  }

  return parse_error ? 3 : 0;
}
