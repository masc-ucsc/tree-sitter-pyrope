// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "diag.hpp"
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

  // terminator_before computation (scanner.c handshake).
  void compute_terminators(std::vector<Token>& toks) const;
  bool gap_terminates(uint32_t gap_start, const Token& cur) const;

  [[noreturn]] void error(std::string code, std::string message, uint32_t start, uint32_t end) const;
  Span make_span(uint32_t start, uint32_t end) const;
};

}  // namespace prpparse
