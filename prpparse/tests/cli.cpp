// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// prpparse_cli — standalone driver.
//
//   prpparse_cli --tokens FILE...   dump the token stream
//   prpparse_cli --lex    FILE...   lex only; report errors, else silent
//   prpparse_cli --sexp   FILE...   parse and dump an s-expression tree
//   prpparse_cli          FILE...   parse; report errors, else silent
//
// Exit status is non-zero if any file fails.

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "lexer.hpp"
#include "parser.hpp"
#include "source_buffer.hpp"

namespace {

using namespace prpparse;

int dump_tokens(const std::string& path) {
  Source_buffer buf = Source_buffer::from_file(path);
  try {
    Lexer              lex(buf);
    std::vector<Token> toks = lex.tokenize();
    std::printf("=== %s (%zu tokens) ===\n", path.c_str(), toks.size());
    for (const Token& t : toks) {
      std::printf("  %4u:%-3u %-12s %-10s %s'%.*s'\n", t.line, buf.col_of(t.start_byte),
                  std::string(to_string(t.kind)).c_str(),
                  t.kw == Keyword::none ? "" : std::string(keyword_spelling(t.kw)).c_str(),
                  t.terminator_before ? "; " : "", static_cast<int>(t.text.size()), t.text.data());
    }
    return 0;
  } catch (const Parse_error& e) {
    std::fprintf(stderr, "%s\n", e.rendered.c_str());
    return 1;
  }
}

int lex_only(const std::string& path) {
  Source_buffer buf = Source_buffer::from_file(path);
  try {
    Lexer lex(buf);
    (void)lex.tokenize();
    return 0;
  } catch (const Parse_error& e) {
    std::fprintf(stderr, "%s\n", e.rendered.c_str());
    return 1;
  }
}

int parse_file(const std::string& path, bool sexp) {
  Source_buffer buf = Source_buffer::from_file(path);
  try {
    Parser    parser(buf);
    Prp_tree& tree = parser.parse();
    if (sexp) {
      std::printf("%s\n", tree.to_sexp().c_str());
    }
    return 0;
  } catch (const Parse_error& e) {
    std::fprintf(stderr, "%s: %s\n", path.c_str(), e.rendered.c_str());
    return 1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> files;
  std::string_view         mode = "parse";
  for (int i = 1; i < argc; ++i) {
    std::string_view a = argv[i];
    if (a == "--tokens")
      mode = "tokens";
    else if (a == "--lex")
      mode = "lex";
    else if (a == "--sexp")
      mode = "sexp";
    else if (a == "--parse")
      mode = "parse";
    else
      files.emplace_back(a);
  }

  int rc = 0;
  for (const std::string& f : files) {
    int r = 0;
    if (mode == "tokens")
      r = dump_tokens(f);
    else if (mode == "lex")
      r = lex_only(f);
    else
      r = parse_file(f, mode == "sexp");
    if (r != 0) rc = r;
  }
  return rc;
}
