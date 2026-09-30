// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "prp_diag.hpp"
#include "source_buffer.hpp"
#include "token.hpp"

namespace prpparse {

struct Comment_span {
  uint32_t start_byte = 0;
  uint32_t end_byte   = 0;
  uint32_t line       = 1;
};

// Single-pass buffer lexer producing a flat std::vector<Token> (last = eof).
// Records comment/trivia spans on the side (never in the token stream) and
// candidate depth-0 statement-start split points. Computes terminator_before
// (the virtual-semicolon handshake) for every token, replicating src/scanner.c.
class Lexer {
public:
  explicit Lexer(const Source_buffer& buf) : buf_(buf) {}

  std::vector<Token> tokenize();
  // Lex only the byte window [lo, hi) of the buffer (absolute offsets). For
  // sub-parsing interpolated-string holes.
  std::vector<Token> tokenize_range(uint32_t lo, uint32_t hi);

  // End (exclusive, past the matching '}') of the interpolation hole whose '{'
  // sits at `open_brace`. The ONE hole scanner, shared by the lexer (to find
  // where an interpolated string ends) and Parser::parse_istring (to find the
  // hole's expression window), so both skip the same comments, nested strings
  // and backtick names: `"{a /* } */ + 1}"` is ONE hole holding `a + 1`.
  [[nodiscard]] uint32_t istring_hole_end(uint32_t open_brace) const { return lex_istring_hole(open_brace); }

  // End of the string escape at b[j] == '\\' (`\n`, `\xNN`, `\u{1F600}`, ...),
  // or `j` when it is not a valid escape. The ONE escape matcher, shared by the
  // lexer ("..." strings, backticked names) and Parser::parse_istring.
  static uint32_t escape_end(const char* b, uint32_t n, uint32_t j);

  // Where the format spec of the hole whose code spans [lo, hi) starts: the
  // first `:` at bracket depth 0 outside strings, backtick names and comments
  // that does not open an attribute write `::[`; `hi` when there is none. The
  // spec is text, never code (`{x:'}` holds the spec `'`), so the parser only
  // lexes [lo, spec) as the hole's expression.
  [[nodiscard]] uint32_t hole_spec_colon(uint32_t lo, uint32_t hi) const;

  [[nodiscard]] const std::vector<Comment_span>& comments() const { return comments_; }
  [[nodiscard]] const std::vector<uint32_t>&      split_points() const { return split_points_; }

private:
  const Source_buffer&      buf_;
  std::vector<Comment_span> comments_;
  std::vector<uint32_t>     split_points_;

  // Advances `i` past whitespace + comments, recording comment spans.
  void skip_trivia(uint32_t& i);

  // Sub-lexers: take the offset of the opening char, return the end offset
  // (exclusive). Throw Parse_error on unterminated input.
  uint32_t lex_word(uint32_t i) const;
  uint32_t lex_backtick(uint32_t i) const;
  uint32_t lex_string(uint32_t i) const;
  uint32_t lex_istring(uint32_t i) const;
  uint32_t lex_istring_hole(uint32_t i) const;  // from '{' to matching '}'
  void skip_comment_in(uint32_t& i, uint32_t hi) const;
  [[noreturn]] void bad_escape(uint32_t j) const;  // invalid escape at b[j] == '\\'
  // An `@` that begins a line (after a real newline, not one inside a comment).
  bool gap_has_newline(uint32_t gap_start, uint32_t gap_end) const;

  // terminator_before computation (scanner.c handshake).
  void compute_terminators(std::vector<Token>& toks) const;
  bool gap_terminates(uint32_t gap_start, const Token& cur) const;

  // Matches Parser::error's convention: code is always a string literal, message
  // may be built by the caller. Throws Parse_error (fail-fast).
  [[noreturn]] void error(const char* code, const std::string& message, uint32_t start, uint32_t end) const;
  Span make_span(uint32_t start, uint32_t end) const;
};

}  // namespace prpparse
