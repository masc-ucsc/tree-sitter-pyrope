// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lexer.hpp"

#include <algorithm>
#include <utility>

namespace prpparse {

namespace {

inline bool is_digit(char c) { return c >= '0' && c <= '9'; }
inline bool is_hex(char c) {
  return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
inline bool is_octal(char c) { return c >= '0' && c <= '7'; }
inline bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
inline bool is_high(char c) { return static_cast<unsigned char>(c) >= 0x80; }
// Identifier start: ASCII letter, '_', or a UTF-8 continuation byte (treated
// loosely as part of a \p{L} letter for accept-parity on valid sources).
inline bool is_ident_start(char c) { return is_alpha(c) || c == '_' || is_high(c); }
inline bool is_ident_cont(char c) {
  return is_alpha(c) || is_digit(c) || c == '_' || c == '$' || is_high(c);
}

// --- numeric form matchers (return end offset, or `i` if no match) ----------
uint32_t m_simple(const char* b, uint32_t n, uint32_t i) {
  if (b[i] == '0') return i + 1;
  if (b[i] >= '1' && b[i] <= '9') {
    uint32_t j = i + 1;
    while (j < n && (is_digit(b[j]) || b[j] == '_')) ++j;
    return j;
  }
  return i;
}
uint32_t m_scaled(const char* b, uint32_t n, uint32_t i) {
  uint32_t e = m_simple(b, n, i);
  if (e == i) return i;
  if (e < n && (b[e] == 'K' || b[e] == 'M' || b[e] == 'G' || b[e] == 'T')) return e + 1;
  return i;
}
uint32_t m_hex(const char* b, uint32_t n, uint32_t i) {
  if (b[i] != '0') return i;
  uint32_t j = i + 1;
  if (j < n && (b[j] == 's' || b[j] == 'S')) ++j;
  if (j >= n || !(b[j] == 'x' || b[j] == 'X')) return i;
  ++j;
  if (j >= n || !is_hex(b[j])) return i;
  ++j;
  while (j < n && (is_hex(b[j]) || b[j] == '_')) ++j;
  return j;
}
uint32_t m_decimal(const char* b, uint32_t n, uint32_t i) {
  if (b[i] != '0') return i;
  uint32_t j = i + 1;
  if (j < n && (b[j] == 's' || b[j] == 'S')) ++j;
  if (j < n && (b[j] == 'd' || b[j] == 'D')) ++j;
  if (j >= n || !is_digit(b[j])) return i;
  ++j;
  while (j < n && (is_digit(b[j]) || b[j] == '_')) ++j;
  return j;
}
uint32_t m_octal(const char* b, uint32_t n, uint32_t i) {
  if (b[i] != '0') return i;
  uint32_t j = i + 1;
  if (j < n && (b[j] == 's' || b[j] == 'S')) ++j;
  if (j >= n || !(b[j] == 'o' || b[j] == 'O')) return i;
  ++j;
  if (j >= n || !is_octal(b[j])) return i;
  ++j;
  while (j < n && (is_octal(b[j]) || b[j] == '_')) ++j;
  return j;
}
uint32_t m_binary(const char* b, uint32_t n, uint32_t i) {
  if (b[i] != '0') return i;
  uint32_t j = i + 1;
  if (j < n && (b[j] == 's' || b[j] == 'S' || b[j] == 'u' || b[j] == 'U')) ++j;
  if (j >= n || !(b[j] == 'b' || b[j] == 'B')) return i;
  ++j;
  if (j >= n || !(b[j] == '0' || b[j] == '1' || b[j] == '?')) return i;
  ++j;
  while (j < n && (b[j] == '0' || b[j] == '1' || b[j] == '?' || b[j] == '_')) ++j;
  return j;
}
uint32_t match_number(const char* b, uint32_t n, uint32_t i) {
  uint32_t e = i;
  e = std::max(e, m_simple(b, n, i));
  e = std::max(e, m_scaled(b, n, i));
  e = std::max(e, m_hex(b, n, i));
  e = std::max(e, m_decimal(b, n, i));
  e = std::max(e, m_octal(b, n, i));
  e = std::max(e, m_binary(b, n, i));
  return e;
}

// --- operator longest-match -------------------------------------------------
std::pair<Token_kind, int> match_operator(const char* b, uint32_t n, uint32_t i) {
  char c0 = b[i];
  char c1 = (i + 1 < n) ? b[i + 1] : '\0';
  char c2 = (i + 2 < n) ? b[i + 2] : '\0';
  using K = Token_kind;
  // 3-char
  if (c0 == '.' && c1 == '.' && c2 == '=') return {K::range_incl, 3};
  if (c0 == '.' && c1 == '.' && c2 == '<') return {K::range_excl, 3};
  if (c0 == '.' && c1 == '.' && c2 == '+') return {K::range_count, 3};
  if (c0 == '.' && c1 == '.' && c2 == '.') return {K::ellipsis, 3};
  if (c0 == '<' && c1 == '<' && c2 == '=') return {K::assign_shl, 3};
  if (c0 == '>' && c1 == '>' && c2 == '=') return {K::assign_sra, 3};
  // 2-char
  if (c0 == ':' && c1 == ':') return {K::coloncolon, 2};
  if (c0 == '.' && c1 == '.') return {K::dotdot, 2};
  if (c0 == '-' && c1 == '>') return {K::arrow, 2};
  if (c0 == '<' && c1 == '<') return {K::shl, 2};
  if (c0 == '>' && c1 == '>') return {K::shr, 2};
  if (c0 == '=' && c1 == '=') return {K::eq, 2};
  if (c0 == '!' && c1 == '=') return {K::ne, 2};
  if (c0 == '<' && c1 == '=') return {K::le, 2};
  if (c0 == '>' && c1 == '=') return {K::ge, 2};
  if (c0 == '+' && c1 == '=') return {K::assign_add, 2};
  if (c0 == '-' && c1 == '=') return {K::assign_sub, 2};
  if (c0 == '*' && c1 == '=') return {K::assign_mul, 2};
  if (c0 == '/' && c1 == '=') return {K::assign_div, 2};
  if (c0 == '|' && c1 == '=') return {K::assign_or, 2};
  if (c0 == '&' && c1 == '=') return {K::assign_and, 2};
  if (c0 == '^' && c1 == '=') return {K::assign_xor, 2};
  // 1-char
  switch (c0) {
    case '(': return {K::lparen, 1};
    case ')': return {K::rparen, 1};
    case '{': return {K::lbrace, 1};
    case '}': return {K::rbrace, 1};
    case '[': return {K::lbracket, 1};
    case ']': return {K::rbracket, 1};
    case ',': return {K::comma, 1};
    case ';': return {K::semicolon, 1};
    case ':': return {K::colon, 1};
    case '.': return {K::dot, 1};
    case '@': return {K::at, 1};
    case '#': return {K::hash, 1};
    case '?': return {K::question, 1};
    case '=': return {K::assign, 1};
    case '+': return {K::plus, 1};
    case '-': return {K::minus, 1};
    case '*': return {K::star, 1};
    case '/': return {K::slash, 1};
    case '%': return {K::percent, 1};
    case '<': return {K::lt, 1};
    case '>': return {K::gt, 1};
    case '&': return {K::amp, 1};
    case '^': return {K::caret, 1};
    case '|': return {K::pipe, 1};
    case '~': return {K::tilde, 1};
    case '!': return {K::bang, 1};
    default: return {K::invalid, 0};
  }
}

// A continuation line that begins with a binary WORD-operator (`and`, `or`,
// `implies`, `has`, `in`, `case`, `does`, `equals`) continues the previous
// expression, exactly like a line beginning with a symbolic operator (`+`,
// `|`, `==`) does below. Without this, the word would be mis-lexed as the start
// of a NEW statement, so a long boolean chain split across lines —
//   z = (a and b)
//    or (c and d)
// — would silently drop every term after the first (the leading `or` became a
// stray `or(...)` call). The match MUST be the whole word: `order`, `index`,
// `cases`, `inner` are ordinary identifiers, not `or`/`in`/`case`. `step` is
// deliberately excluded — it also heads a `test`-block `step [n]` statement.
bool word_op_continues(const char* b, uint32_t n, uint32_t off) {
  auto eq = [&](const char* w, uint32_t len) {
    for (uint32_t k = 0; k < len; ++k)
      if (off + k >= n || b[off + k] != w[k]) return false;
    uint32_t end = off + len;  // require a word boundary after the operator
    return end >= n || !is_ident_cont(b[end]);
  };
  switch (off < n ? b[off] : '\0') {
    case 'a': return eq("and", 3);
    case 'o': return eq("or", 2);
    case 'i': return eq("implies", 7) || eq("in", 2);
    case 'h': return eq("has", 3);
    case 'c': return eq("case", 4);
    case 'd': return eq("does", 4);
    case 'e': return eq("equals", 6);  // `else`/`elif` fall through to scanner_switch
    default:  return false;
  }
}

// scanner.c continuation FSM (lines 87-126), applied to the first byte(s) of
// the next token after a newline. Returns true => emit terminator.
bool scanner_switch(const char* b, uint32_t n, uint32_t off) {
  auto at = [&](uint32_t k) -> char { return (off + k < n) ? b[off + k] : '\0'; };
  if (word_op_continues(b, n, off)) return false;
  switch (at(0)) {
    case ',': case '=': case '.': case '>': case '<': case '&':
    case '^': case '|': case '*': case '/': case '+': case '-':
      return false;
    case 'e': {  // else / elif
      if (at(1) != 'l') return true;
      if (at(2) != 's' && at(2) != 'i') return true;
      if (at(2) == 's') {
        if (at(3) == 'e') return false;
      } else {
        if (at(3) == 'f') return false;
      }
      return at(4) != '=';  // scanner.c fall-through into case '!'
    }
    case '!':
      return at(1) != '=';
    default:
      return true;
  }
}

}  // namespace

void Lexer::error(std::string code, std::string message, uint32_t start, uint32_t end) const {
  Diag d;
  d.code     = std::move(code);
  d.category = std::string(kCategorySyntax);
  d.message  = std::move(message);
  d.span     = make_span(start, end);
  throw Parse_error(std::move(d));
}

Span Lexer::make_span(uint32_t start, uint32_t end) const {
  Span s;
  s.file       = buf_.path();
  s.start_byte = start;
  s.end_byte   = end;
  s.start_line = buf_.line_of(start);
  s.start_col  = buf_.col_of(start);
  uint32_t e   = end > start ? end - 1 : start;
  s.end_line   = buf_.line_of(e);
  s.end_col    = buf_.col_of(e) + 1;
  s.valid      = true;
  return s;
}

void Lexer::skip_trivia(uint32_t& i) {
  const char* b = buf_.data();
  uint32_t    n = static_cast<uint32_t>(buf_.size());
  while (i < n) {
    char c = b[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
      ++i;
      continue;
    }
    // UTF-8 BOM / zero-width spaces (rare): treat as whitespace.
    if (is_high(c) && i + 2 < n && static_cast<unsigned char>(c) == 0xEF &&
        static_cast<unsigned char>(b[i + 1]) == 0xBB &&
        static_cast<unsigned char>(b[i + 2]) == 0xBF) {
      i += 3;
      continue;
    }
    if (c == '/' && i + 1 < n && b[i + 1] == '/') {
      uint32_t cs = i;
      i += 2;
      while (i < n && b[i] != '\n') ++i;
      comments_.push_back({cs, i, buf_.line_of(cs)});
      continue;
    }
    if (c == '/' && i + 1 < n && b[i + 1] == '*') {
      uint32_t cs = i;
      i += 2;
      bool closed = false;
      while (i < n) {
        if (b[i] == '*' && i + 1 < n && b[i + 1] == '/') {
          i += 2;
          closed = true;
          break;
        }
        ++i;
      }
      if (!closed) error("unterminated-comment", "unterminated block comment", cs, n);
      comments_.push_back({cs, i, buf_.line_of(cs)});
      continue;
    }
    break;
  }
}

uint32_t Lexer::lex_word(uint32_t i) const {
  const char* b = buf_.data();
  uint32_t    n = static_cast<uint32_t>(buf_.size());
  uint32_t    j = i + 1;  // first char already known to be ident-start
  while (j < n && is_ident_cont(b[j])) ++j;
  return j;
}

uint32_t Lexer::lex_backtick(uint32_t i) const {
  const char* b = buf_.data();
  uint32_t    n = static_cast<uint32_t>(buf_.size());
  uint32_t    j = i + 1;  // past opening backtick
  while (j < n) {
    char c = b[j];
    if (c == '`') return j + 1;
    if (c == '\n') error("unterminated-ident", "newline in backtick identifier", i, j);
    if (c == '\\' && j + 1 < n) {
      j += 2;
      continue;
    }
    ++j;
  }
  error("unterminated-ident", "unterminated backtick identifier", i, n);
}

uint32_t Lexer::lex_string(uint32_t i) const {
  const char* b = buf_.data();
  uint32_t    n = static_cast<uint32_t>(buf_.size());
  uint32_t    j = i + 1;  // past opening quote
  while (j < n) {
    char c = b[j];
    if (c == '\'') return j + 1;
    if (c == '\n') error("unterminated-string", "newline in single-quoted string", i, j);
    ++j;
  }
  error("unterminated-string", "unterminated single-quoted string", i, n);
}

// From the opening '{' of an interpolation hole to (past) its matching '}'.
uint32_t Lexer::lex_istring_hole(uint32_t i) const {
  const char* b = buf_.data();
  uint32_t    n = static_cast<uint32_t>(buf_.size());
  uint32_t    j = i + 1;  // past '{'
  int         depth = 1;
  while (j < n && depth > 0) {
    char c = b[j];
    if (c == '{') {
      ++depth;
      ++j;
    } else if (c == '}') {
      --depth;
      ++j;
    } else if (c == '"') {
      j = lex_istring(j);  // nested interpolated string
    } else if (c == '\'') {
      j = lex_string(j);  // nested single-quoted string
    } else if (c == '/' && j + 1 < n && b[j + 1] == '/') {
      j += 2;
      while (j < n && b[j] != '\n') ++j;
    } else if (c == '/' && j + 1 < n && b[j + 1] == '*') {
      j += 2;
      while (j < n && !(b[j] == '*' && j + 1 < n && b[j + 1] == '/')) ++j;
      if (j < n) j += 2;
    } else {
      ++j;
    }
  }
  if (depth > 0) error("unterminated-string", "unterminated interpolation in string", i, n);
  return j;
}

uint32_t Lexer::lex_istring(uint32_t i) const {
  const char* b = buf_.data();
  uint32_t    n = static_cast<uint32_t>(buf_.size());
  uint32_t    j = i + 1;  // past opening '"'
  while (j < n) {
    char c = b[j];
    if (c == '"') return j + 1;
    if (c == '\\' && j + 1 < n) {
      j += 2;
      continue;
    }
    if (c == '\n')
      error("unterminated-string", "newline in interpolated string", i, j);
    if (c == '{') {
      j = lex_istring_hole(j);
      continue;
    }
    ++j;
  }
  error("unterminated-string", "unterminated interpolated string", i, n);
}

std::vector<Token> Lexer::tokenize() { return tokenize_range(0, static_cast<uint32_t>(buf_.size())); }

// Lex the byte window [lo, hi) of the SAME buffer, with absolute offsets. Used
// to sub-parse an interpolated-string hole (`{expr}`) as an expression: the
// resulting tokens point into the full source, so the parsed Ast carries
// absolute spans. The trailing eof sits at `hi`.
std::vector<Token> Lexer::tokenize_range(uint32_t lo, uint32_t hi) {
  const char* b = buf_.data();
  uint32_t    n = hi;
  std::vector<Token> toks;
  toks.reserve((hi - lo) / 4 + 8);

  uint32_t i     = lo;
  int      depth = 0;  // brace depth, for split-point heuristic

  while (true) {
    skip_trivia(i);
    if (i >= n) break;

    uint32_t   start = i;
    char       c     = b[i];
    Token      tok;
    tok.start_byte = start;

    if (c == '"') {
      uint32_t end = lex_istring(i);
      tok.kind     = Token_kind::istring;
      tok.end_byte = end;
      i            = end;
    } else if (c == '\'') {
      uint32_t end = lex_string(i);
      tok.kind     = Token_kind::string;
      tok.end_byte = end;
      i            = end;
    } else if (c == '`') {
      uint32_t end = lex_backtick(i);
      tok.kind     = Token_kind::ident;
      tok.kw       = Keyword::none;
      tok.end_byte = end;
      i            = end;
    } else if (is_ident_start(c)) {
      uint32_t         end = lex_word(i);
      std::string_view w(b + start, end - start);
      Keyword          kw = classify_keyword(w);
      // Keyword-prefixed compound assigns: `or=` / `and=` (longest match).
      if ((kw == Keyword::kw_or || kw == Keyword::kw_and) && end < n && b[end] == '=') {
        tok.kind     = (kw == Keyword::kw_or) ? Token_kind::assign_log_or : Token_kind::assign_log_and;
        tok.kw       = Keyword::none;
        end += 1;
      } else {
        tok.kind = Token_kind::ident;
        tok.kw   = kw;
      }
      tok.end_byte = end;
      i            = end;
    } else if (is_digit(c)) {
      uint32_t end = match_number(b, n, i);
      if (end == start) error("bad-number", "malformed numeric literal", start, start + 1);
      tok.kind     = Token_kind::integer;
      tok.end_byte = end;
      i            = end;
    } else {
      auto [kind, len] = match_operator(b, n, i);
      if (len == 0)
        error("unexpected-char", "unexpected character in input", start, start + 1);
      tok.kind     = kind;
      tok.end_byte = start + len;
      i            = start + len;
    }

    tok.line = buf_.line_of(start);
    tok.text = std::string_view(b + tok.start_byte, tok.end_byte - tok.start_byte);

    // Split-point heuristic: a token at brace-depth 0 that begins a new line is
    // a candidate top-level statement start.
    if (depth == 0 && !toks.empty() && tok.line > toks.back().line)
      split_points_.push_back(static_cast<uint32_t>(toks.size()));
    if (tok.kind == Token_kind::lbrace) ++depth;
    else if (tok.kind == Token_kind::rbrace && depth > 0) --depth;

    toks.push_back(tok);
  }

  Token eof;
  eof.kind       = Token_kind::eof;
  eof.start_byte = n;
  eof.end_byte   = n;
  eof.line       = n > 0 ? buf_.line_of(n - 1) : 1;
  eof.text       = std::string_view(b + n, 0);
  toks.push_back(eof);

  compute_terminators(toks);
  return toks;
}

bool Lexer::gap_terminates(uint32_t gap_start, const Token& cur) const {
  // The inter-token gap holds only whitespace and comments. tree-sitter skips
  // comments (extras) when deciding the automatic semicolon, so a trailing
  // comment does NOT suppress the terminator. A terminator is emitted when a
  // newline appears anywhere in the gap (then the scanner.c continuation FSM
  // decides based on the next token), or when the next token is '}' / EOF.
  const char* b    = buf_.data();
  uint32_t    n    = static_cast<uint32_t>(buf_.size());
  uint32_t    cend = cur.start_byte;
  uint32_t    s    = gap_start;
  bool        saw_newline = false;
  while (s < cend) {
    char c = b[s];
    if (c == '\n') {
      saw_newline = true;
      ++s;
    } else if (c == '/' && s + 1 < cend && b[s + 1] == '/') {
      s += 2;
      while (s < cend && b[s] != '\n') ++s;
    } else if (c == '/' && s + 1 < cend && b[s + 1] == '*') {
      // Block comment: skipped like whitespace, but newlines INSIDE it do NOT
      // count as a statement terminator — only real line breaks in whitespace
      // do (matches tree-sitter: `a /* x\n y */ b` keeps a and b on one logical
      // line, so it is NOT terminated).
      s += 2;
      while (s + 1 < cend && !(b[s] == '*' && b[s + 1] == '/')) ++s;
      s += 2;
    } else {
      ++s;  // whitespace
    }
  }
  if (saw_newline) return scanner_switch(b, n, cur.start_byte);
  if (cur.kind == Token_kind::rbrace) return true;
  if (cur.kind == Token_kind::eof) return true;
  return false;
}

void Lexer::compute_terminators(std::vector<Token>& toks) const {
  for (size_t k = 0; k < toks.size(); ++k) {
    if (k == 0) {
      toks[k].terminator_before = false;
      continue;
    }
    toks[k].terminator_before = gap_terminates(toks[k - 1].end_byte, toks[k]);
  }
}

}  // namespace prpparse
