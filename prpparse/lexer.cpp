// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lexer.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

#include "unicode_ident.hpp"

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

// Decode the UTF-8 sequence at b[i] (i < n). Returns its length (1..4) and sets
// `cp`, or 0 for a malformed / truncated / overlong sequence.
uint32_t decode_utf8(const char* b, uint32_t n, uint32_t i, uint32_t& cp) {
  const auto u = [&](uint32_t k) { return static_cast<unsigned char>(b[k]); };
  const unsigned char c = u(i);
  if (c < 0x80) {
    cp = c;
    return 1;
  }
  uint32_t len;
  if ((c & 0xE0) == 0xC0) {
    len = 2;
    cp  = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    len = 3;
    cp  = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    len = 4;
    cp  = c & 0x07;
  } else {
    return 0;
  }
  if (i + len > n) return 0;
  for (uint32_t k = 1; k < len; ++k) {
    if ((u(i + k) & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (u(i + k) & 0x3F);
  }
  static constexpr uint32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
  if (cp < kMin[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  return len;
}

// The non-ASCII blanks (Unicode White_Space, the BOM, the word joiner and the
// zero-width space). Only ASCII blanks separate tokens (owner ruling 108;
// grammar.js `_space`): outside a string or a comment one of these is an error,
// also a BOM at the start of the file. They are never part of a name.
bool is_space_cp(uint32_t cp) {
  return cp == 0x85 || cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200B) || cp == 0x2028 ||
         cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x2060 || cp == 0x3000 || cp == 0xFEFF;
}

// Identifier characters (grammar.js `identifier`): letters (\p{L}, non-ASCII
// included), decimal digits (\p{Nd}) and `_` -- nothing else. `$`, emoji,
// symbols (`×`, `·`, `€`, `→`, a fullwidth `＋`), superscripts, roman numerals
// and combining marks are not, so `a😀b`, `a×b` are errors (a name holding one
// is written in backticks). A name starts with a letter or `_`.
// Returns the byte length of the identifier character at b[i], or 0.
uint32_t ident_char_len(const char* b, uint32_t n, uint32_t i, bool start) {
  const char c = b[i];
  if (!is_high(c)) return (is_alpha(c) || c == '_' || (!start && is_digit(c))) ? 1 : 0;
  uint32_t cp  = 0;
  uint32_t len = decode_utf8(b, n, i, cp);
  if (len == 0) return 0;
  if (unicode::is_letter(cp) || (!start && unicode::is_digit(cp))) return len;
  return 0;
}
inline bool is_ident_start_at(const char* b, uint32_t n, uint32_t i) { return ident_char_len(b, n, i, true) > 0; }
// A name character or a backtick right after a word (src/scanner.c
// is_ident_char): `andé`, `and_x`, ``and`x` `` are names, not the operator.
inline bool ident_cont_or_tick_at(const char* b, uint32_t n, uint32_t i) {
  return i < n && (b[i] == '`' || ident_char_len(b, n, i, false) > 0);
}

// Future placeholder names: an underscore, a decimal digit, then only
// letters or decimal digits. Match grammar.js using the same Unicode tables
// as ordinary identifiers; a later underscore does not match this pattern.
bool is_reserved_placeholder(std::string_view w) {
  if (w.size() < 2 || w[0] != '_') return false;
  const auto n = static_cast<uint32_t>(w.size());
  uint32_t i = 1;
  bool first = true;
  while (i < n) {
    uint32_t cp = 0;
    const uint32_t len = decode_utf8(w.data(), n, i, cp);
    if (len == 0 || !(unicode::is_digit(cp) || (!first && unicode::is_letter(cp)))) return false;
    first = false;
    i += len;
  }
  return true;
}

// The old lowercase type spellings are BANNED words (spec 2026-09-29 §7): an
// identifier token spelled `u8`/`s20`/`i32` (`u`, `s` or `i` followed by
// digits), `bool`, `boolean`, `unsigned`, `signed` or `string` is an error in
// every position (a name, a type, a cast callee, a field, a lambda name); the
// backticked `` `u8` `` is an ordinary name. Returns the new spelling, or "" when
// `w` is not banned.
std::string banned_word_replacement(std::string_view w) {
  if (w.size() >= 2 && (w[0] == 'u' || w[0] == 's' || w[0] == 'i')) {
    bool digits = true;
    for (size_t k = 1; k < w.size() && digits; ++k) digits = is_digit(w[k]);
    if (digits) return std::string(1, w[0] == 'u' ? 'U' : 'S') + std::string(w.substr(1));
  }
  if (w == "bool" || w == "boolean") return "Bool";
  if (w == "unsigned") return "Unsigned";
  if (w == "signed") return "Signed";
  if (w == "string") return "String";
  return "";
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
// The sign letters `u`/`s` combine with every radix letter (owner ruling 109):
// `0ub` `0uo` `0ud` `0ux` and `0sb` `0so` `0sd` `0sx`; a sign without a radix
// (`0s12`) is an error (see the lexer loop).
bool is_sign_letter(char c) { return c == 's' || c == 'S' || c == 'u' || c == 'U'; }
uint32_t m_hex(const char* b, uint32_t n, uint32_t i) {
  if (b[i] != '0') return i;
  uint32_t j = i + 1;
  if (j < n && is_sign_letter(b[j])) ++j;
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
  if (j + 1 < n && is_sign_letter(b[j]) && (b[j + 1] == 'd' || b[j + 1] == 'D')) j += 2;
  else if (j < n && (b[j] == 'd' || b[j] == 'D')) ++j;
  if (j >= n || !is_digit(b[j])) return i;
  ++j;
  while (j < n && (is_digit(b[j]) || b[j] == '_')) ++j;
  return j;
}
uint32_t m_octal(const char* b, uint32_t n, uint32_t i) {
  if (b[i] != '0') return i;
  uint32_t j = i + 1;
  if (j < n && is_sign_letter(b[j])) ++j;
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
  // Binary literals require explicit signedness, matching grammar.js.
  if (j >= n || !is_sign_letter(b[j])) return i;
  ++j;
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
  if (c0 == '+' && c1 == '+' && c2 == '=') return {K::assign_concat, 3};
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
  if (c0 == '+' && c1 == '+') return {K::concat, 2};
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
    // (a backtick counts as a name character, like src/scanner.c is_ident_char)
    return !ident_cont_or_tick_at(b, n, end);
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

// Does `prev` end an operand, so that a following `and=` / `or=` is the
// compound assignment (`x and= y`)? Only then: after `(`, `,`, `const`, ... the
// word is a NAME followed by `=` (`(and=1)`, `f(or=1)`, `(const and=1)`), which
// is how tree-sitter reads it (its `and=` token is only valid after an
// operand). A keyword ends an operand only as a `.field` (`x.and and= y`) or as
// the literals `true`/`false`.
bool ends_operand(const std::vector<Token>& toks) {
  if (toks.empty()) return false;
  const Token& t = toks.back();
  switch (t.kind) {
    case Token_kind::ident:
      if (t.kw == Keyword::none || t.kw == Keyword::kw_true || t.kw == Keyword::kw_false) return true;
      return toks.size() >= 2 && toks[toks.size() - 2].kind == Token_kind::dot;
    case Token_kind::type_word:
    case Token_kind::integer:
    case Token_kind::string:
    case Token_kind::istring:
    case Token_kind::question:
    case Token_kind::rparen:
    case Token_kind::rbracket:
    case Token_kind::rbrace:
      return true;
    default:
      return false;
  }
}

// scanner.c continuation FSM (lines 87-126), applied to the first byte(s) of
// the next token after a newline. Returns true => emit terminator.
bool scanner_switch(const char* b, uint32_t n, uint32_t off) {
  auto at = [&](uint32_t k) -> char { return (off + k < n) ? b[off + k] : '\0'; };
  if (word_op_continues(b, n, off)) return false;
  switch (at(0)) {
    case ',': case '=': case '.': case '>': case '<': case '&':
    case '^': case '|': case '*': case '/': case '%': case '+': case '-':
    // `:` (type annotation / `::[attr]`) and `#` (bit selector `#[..]`,
    // `#|[..]`, `#sext[..]`, ...) at line start continue the previous
    // statement (src/scanner.c); no statement starts with either.
    case ':': case '#':
      return false;
    case 'e': {  // else / elif: WHOLE word (scanner.c), so `els`, `eli`, `elsev`,
                 // `elifx`, `elsewhere` are names that start a new statement
      auto word = [&](const char* w, uint32_t len) {
        for (uint32_t k = 0; k < len; ++k)
          if (at(k) != w[k]) return false;
        return !ident_cont_or_tick_at(b, n, off + len);
      };
      return !(word("else", 4) || word("elif", 4));
    }
    case '!':
      return at(1) != '=';
    default:
      return true;
  }
}

}  // namespace

void Lexer::error(const char* code, const std::string& message, uint32_t start, uint32_t end) const {
  Diag d;
  d.code     = code;
  d.category = std::string(kCategorySyntax);
  d.message  = message;
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
    // A non-ASCII blank (NBSP, the BOM, zero-width spaces, ...: is_space_cp)
    // separates nothing (grammar.js `_space`).
    if (is_high(c)) {
      uint32_t cp  = 0;
      uint32_t len = decode_utf8(b, n, i, cp);
      if (len > 0 && is_space_cp(cp)) {
        char hex[16];
        std::snprintf(hex, sizeof(hex), "U+%04X", cp);
        error("non-ascii-space", std::string("non-ASCII space (") + hex + "); use a plain space", i, i + len);
      }
    }
    if (c == '/' && i + 1 < n && b[i + 1] == '/') {
      uint32_t cs = i;
      i += 2;
      while (i < n && b[i] != '\n') ++i;
      comments_.push_back({cs, i, buf_.line_of(cs)});
      continue;
    }
    if (c == '/' && i + 1 < n && b[i + 1] == '*') {
      // Block comments NEST: track depth so `/* a /* b */ c */` is ONE comment
      // (the inner `*/` closes the inner `/*`, not the outer). A commented-out
      // region therefore stays fully commented even if it contains a comment.
      uint32_t cs    = i;
      i += 2;
      int      depth = 1;
      while (i < n && depth > 0) {
        if (b[i] == '/' && i + 1 < n && b[i + 1] == '*') {
          depth += 1;
          i += 2;
        } else if (b[i] == '*' && i + 1 < n && b[i + 1] == '/') {
          depth -= 1;
          i += 2;
        } else {
          ++i;
        }
      }
      if (depth > 0) error("unterminated-comment", "unterminated block comment", cs, n);
      comments_.push_back({cs, i, buf_.line_of(cs)});
      continue;
    }
    break;
  }
}

uint32_t Lexer::lex_word(uint32_t i) const {
  const char* b = buf_.data();
  uint32_t    n = static_cast<uint32_t>(buf_.size());
  uint32_t    j = i + ident_char_len(b, n, i, /*start=*/true);  // i is known to start a name
  while (j < n) {
    uint32_t len = ident_char_len(b, n, j, /*start=*/false);
    if (len == 0) break;
    j += len;
  }
  return j;
}

// grammar.js `_escape_sequence` (spec 2026-09-29 §8): exactly `\n` `\t` `\r`
// `\\` `\"` `\'` `\0` `\xNN` (two hex digits, at most 7F: an ASCII byte, owner
// ruling 95) `\u{N}` (one to six hex digits naming a Unicode scalar value: at
// most 10FFFF and no surrogate D800-DFFF, ruling 96) and `` \` ``, in "..."
// strings and in backticked names alike. Every other `\c` is an error: `\{`/`\}`
// (literal braces are `{{`/`}}`), `\uNNNN` without braces, `\q`, ... Returns the
// end of the escape at b[j] == '\\', or j.
uint32_t Lexer::escape_end(const char* b, uint32_t n, uint32_t j) {
  if (j + 1 >= n) return j;
  switch (b[j + 1]) {
    case 'n': case 't': case 'r': case '\\': case '"': case '\'': case '0': case '`':
      return j + 2;
    case 'x':
      return (j + 3 < n && b[j + 2] >= '0' && b[j + 2] <= '7' && is_hex(b[j + 3])) ? j + 4 : j;
    case 'u': {
      if (j + 2 >= n || b[j + 2] != '{') return j;
      uint32_t k  = j + 3;
      uint32_t cp = 0;
      while (k < n && k - (j + 3) < 7 && is_hex(b[k])) {
        cp = cp * 16 + static_cast<uint32_t>(b[k] <= '9' ? b[k] - '0' : (b[k] | 0x20) - 'a' + 10);
        ++k;
      }
      const uint32_t digits = k - (j + 3);
      if (digits < 1 || digits > 6 || k >= n || b[k] != '}') return j;
      if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return j;  // not a Unicode scalar value
      return k + 1;
    }
    default:
      return j;
  }
}

void Lexer::bad_escape(uint32_t j) const {
  const char* b    = buf_.data();
  uint32_t    n    = static_cast<uint32_t>(buf_.size());
  const char  c    = (j + 1 < n) ? b[j + 1] : '\0';
  std::string what = (j + 1 < n && c != '\n') ? std::string("\\") + c : std::string("\\");
  std::string msg  = "invalid escape `" + what + "` (the escapes are \\n \\t \\r \\\\ \\\" \\' \\0 \\xNN \\u{N..} and \\`)";
  if (c == '{' || c == '}') msg += "; a literal brace is written `{{` or `}}`";
  else if (c == 'u')
    msg += "; a code point is written `\\u{hex}` (1 to 6 hex digits, at most 10FFFF, not a surrogate D800-DFFF)";
  else if (c == 'x') msg += "; `\\x` takes exactly two hex digits, at most 7F (write `\\u{..}` for other characters)";
  error("bad-escape", msg, j, std::min(n, j + 2));
}

uint32_t Lexer::lex_backtick(uint32_t i) const {
  const char* b = buf_.data();
  uint32_t    n = static_cast<uint32_t>(buf_.size());
  uint32_t    j = i + 1;  // past opening backtick
  while (j < n) {
    char c = b[j];
    if (c == '`') {
      if (j == i + 1) error("empty-identifier", "a backticked identifier must not be empty", i, j + 1);
      return j + 1;
    }
    if (c == '\n') error("unterminated-ident", "newline in backtick identifier", i, j);
    if (c == '\\') {
      uint32_t e = escape_end(b, n, j);  // the string escapes apply here too
      if (e == j) bad_escape(j);
      j = e;
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
// The hole holds ordinary code, so this skips exactly what tokenize_range
// skips or lexes as one token: comments (line, and NESTING block comments as in
// skip_trivia), nested strings and backtick names. A '}' inside any of them
// does not close the hole: `"{a /* } */ + 1}"` and `"{f("}")}"` are one hole.
// Parser::parse_istring finds each hole's window through istring_hole_end(), so
// the lexer and the parser always agree on where a hole ends. A hole holding
// nothing but blanks and comments (`{}`, `{ }`, `{ /* c */ }`) is an error:
// there are no positional placeholders (spec 2026-09-29 §8).
uint32_t Lexer::lex_istring_hole(uint32_t i) const {
  const char* b = buf_.data();
  uint32_t    n = static_cast<uint32_t>(buf_.size());
  uint32_t    j = i + 1;  // past '{'
  int         depth = 1;
  int         nest  = 0;      // ( [ depth, to find the format spec's `:`
  bool        code  = false;  // saw something other than blanks and comments
  while (j < n && depth > 0) {
    char c = b[j];
    if (c == ':' && depth == 1 && nest == 0 && !(j + 1 < n && b[j + 1] == ':') && b[j - 1] != ':') {
      // The format spec (`{x:08b}`) is TEXT up to `}` `"` `{` a newline or a
      // comment opener (grammar.js `_format_spec`): a `'` or `` ` `` there
      // opens nothing. (`::[attr]` is code.) Parser::check_format_spec
      // validates it.
      code = true;
      ++j;
      while (j < n && b[j] != '}' && b[j] != '"' && b[j] != '{' && b[j] != '\n' &&
             !(b[j] == '/' && j + 1 < n && (b[j + 1] == '/' || b[j + 1] == '*')))
        ++j;
      continue;
    }
    if (c == '(' || c == '[') {
      ++nest;
      code = true;
      ++j;
    } else if (c == ')' || c == ']') {
      --nest;
      code = true;
      ++j;
    } else if (c == '{') {
      code = true;
      ++depth;
      ++j;
    } else if (c == '}') {
      --depth;
      ++j;
    } else if (c == '"') {
      code = true;
      j    = lex_istring(j);  // nested interpolated string
    } else if (c == '\'') {
      code = true;
      j    = lex_string(j);  // nested single-quoted string
    } else if (c == '`') {
      code = true;
      j    = lex_backtick(j);  // backtick name: `a}b` is one identifier
    } else if (c == '/' && j + 1 < n && b[j + 1] == '/') {
      j += 2;
      while (j < n && b[j] != '\n') ++j;
    } else if (c == '/' && j + 1 < n && b[j + 1] == '*') {
      // Block comments NEST (match skip_trivia): count depth to the OUTER close.
      uint32_t cs = j;
      j += 2;
      int cdepth = 1;
      while (j < n && cdepth > 0) {
        if (b[j] == '/' && j + 1 < n && b[j + 1] == '*') {
          cdepth += 1;
          j += 2;
        } else if (b[j] == '*' && j + 1 < n && b[j + 1] == '/') {
          cdepth -= 1;
          j += 2;
        } else {
          ++j;
        }
      }
      if (cdepth > 0) error("unterminated-comment", "unterminated block comment", cs, n);
    } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
      ++j;
    } else {
      uint32_t cp  = 0;
      uint32_t len = is_high(c) ? decode_utf8(b, n, j, cp) : 1;
      if (!(is_high(c) && len > 0 && is_space_cp(cp))) code = true;
      j += len > 0 ? len : 1;
    }
  }
  if (depth > 0) error("unterminated-string", "unterminated interpolation in string", i, n);
  if (!code)
    error("empty-interpolation",
          "empty interpolation hole: a `{...}` in a string needs an expression (a literal brace is `{{` or `}}`)", i,
          j);
  return j;
}

uint32_t Lexer::hole_spec_colon(uint32_t lo, uint32_t hi) const {
  const char* b     = buf_.data();
  int         depth = 0;
  uint32_t    j     = lo;
  while (j < hi) {
    const char c = b[j];
    if (c == '(' || c == '[' || c == '{') {
      ++depth;
      ++j;
    } else if (c == ')' || c == ']' || c == '}') {
      --depth;
      ++j;
    } else if (c == '"') {
      j = lex_istring(j);
    } else if (c == '\'') {
      j = lex_string(j);
    } else if (c == '`') {
      j = lex_backtick(j);
    } else if (c == '/' && j + 1 < hi && (b[j + 1] == '/' || b[j + 1] == '*')) {
      uint32_t k = j;
      skip_comment_in(k, hi);
      j = k;
    } else if (c == ':' && depth == 0) {
      if (j + 1 < hi && b[j + 1] == ':') {  // `::[attr]` belongs to the expression
        uint32_t k = j + 2;
        while (k < hi && (b[k] == ' ' || b[k] == '\t' || b[k] == '\r' || b[k] == '\n')) ++k;
        if (k < hi && b[k] == '[') {
          j = k;
          continue;
        }
      }
      return j;
    } else {
      ++j;
    }
  }
  return hi;
}

// Skip the comment at b[i] ('//' or a NESTING '/* */') within [.., hi).
void Lexer::skip_comment_in(uint32_t& i, uint32_t hi) const {
  const char* b = buf_.data();
  if (b[i + 1] == '/') {
    while (i < hi && b[i] != '\n') ++i;
    return;
  }
  i += 2;
  int depth = 1;
  while (i < hi && depth > 0) {
    if (b[i] == '/' && i + 1 < hi && b[i + 1] == '*') {
      ++depth;
      i += 2;
    } else if (b[i] == '*' && i + 1 < hi && b[i + 1] == '/') {
      --depth;
      i += 2;
    } else {
      ++i;
    }
  }
}

uint32_t Lexer::lex_istring(uint32_t i) const {
  const char* b = buf_.data();
  uint32_t    n = static_cast<uint32_t>(buf_.size());
  uint32_t    j = i + 1;  // past opening '"'
  while (j < n) {
    char c = b[j];
    if (c == '"') return j + 1;
    if (c == '\\') {
      uint32_t e = escape_end(b, n, j);
      if (e == j) bad_escape(j);
      j = e;
      continue;
    }
    if (c == '\n')
      error("unterminated-string", "newline in interpolated string", i, j);
    if (c == '{') {
      // `{{` (and `}}`) are ESCAPED LITERAL BRACES, as in Python/Rust format
      // strings: `"{{x}}"` is the text `{x}`, no hole (src/scanner.c
      // scan_string_content; Parser::parse_istring skips them the same way).
      if (j + 1 < n && b[j + 1] == '{') {
        j += 2;
        continue;
      }
      j = lex_istring_hole(j);
      continue;
    }
    if (c == '}') {
      // Outside a hole a `}` is only literal when doubled: `"a }} b"`.
      if (j + 1 < n && b[j + 1] == '}') {
        j += 2;
        continue;
      }
      error("lone-brace", "a lone `}` in a string must be written `}}`", j, j + 1);
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
    } else if (is_ident_start_at(b, static_cast<uint32_t>(buf_.size()), i)) {
      uint32_t         end = lex_word(i);
      std::string_view w(b + start, end - start);
      Keyword          kw = classify_keyword(w);
      // A lone `_` is no name (grammar.js `identifier` needs a second
      // character after a leading `_`): `const _ = 1`, `for _ in ..`,
      // `f(_)`, `a._` are errors; the backticked `` `_` `` is a name.
      if (w == "_")
        error("bare-underscore", "`_` alone is not a name (write `` `_` `` to use it as one)", start, end);
      if (is_reserved_placeholder(w))
        error("reserved-placeholder-name",
              "`" + std::string(w) + "` is reserved for a future placeholder (use backticks for a name spelled so)",
              start, end);
      if (std::string repl = banned_word_replacement(w); !repl.empty())
        error("renamed-type-word",
              "`" + std::string(w) + "` was renamed `" + repl + "` (the lowercase type spellings are banned words;"
                  " write `` `" + std::string(w) + "` `` for a name spelled so)",
              start, end);
      if (is_type_word(w)) {
        // `U4` / `S20` / `Bool` / `Clock` ...: a reserved type word, never an
        // identifier (grammar.js `identifier` + `reserved`). The backticked
        // `` `U4` `` is a name.
        tok.kind = Token_kind::type_word;
        tok.kw   = Keyword::none;
      } else if ((kw == Keyword::kw_or || kw == Keyword::kw_and) && end < n && b[end] == '=' &&
                 ends_operand(toks)) {
        // Keyword-prefixed compound assigns: `or=` / `and=` (longest match),
        // only after an operand (`x and= y`). Elsewhere the word is a name
        // (`(and=1)`, `f(or=1)`): spaces never change the meaning.
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
      // `0b1100` lexes as `0` glued to the name `b1100`: a binary literal names
      // its signedness (02-basics).
      if (end == start + 1 && c == '0' && end + 1 < n && (b[end] == 'b' || b[end] == 'B') &&
          (b[end + 1] == '0' || b[end + 1] == '1' || b[end + 1] == '?')) {
        uint32_t e = end + 1;
        while (e < n && (b[e] == '0' || b[e] == '1' || b[e] == '?' || b[e] == '_')) ++e;
        const std::string bits(b + end + 1, e - end - 1);
        error("invalid-binary-prefix",
              "binary literal `0b…` is missing its sign: use `0ub" + bits + "` (unsigned) or `0sb" + bits + "` (signed)",
              start, e);
      }
      // `0s12` / `0u12`: a sign letter needs a radix letter (owner ruling 109).
      if (end == start + 1 && c == '0' && end + 1 < n && is_sign_letter(b[end]) && is_digit(b[end + 1])) {
        uint32_t e = end + 1;
        while (e < n && (is_digit(b[e]) || b[e] == '_')) ++e;
        const std::string sign(1, static_cast<char>(b[end] | 0x20));
        const std::string digits(b + end + 1, e - end - 1);
        error("missing-radix",
              "a sign letter needs a radix letter: write `0" + sign + "d" + digits + "` (or `0" + sign + "b`, `0" + sign +
                  "o`, `0" + sign + "x`)",
              start, e);
      }
      tok.kind     = Token_kind::integer;
      tok.end_byte = end;
      i            = end;
    } else {
      auto [kind, len] = match_operator(b, n, i);
      if (len == 0 && is_high(c)) {
        uint32_t cp = 0;
        uint32_t cl = decode_utf8(b, static_cast<uint32_t>(buf_.size()), i, cp);
        if (cl == 0) error("invalid-utf8", "invalid UTF-8 in input", start, start + 1);
        char hex[16];
        std::snprintf(hex, sizeof(hex), "U+%04X", cp);
        error("unexpected-char",
              "unexpected character '" + std::string(b + start, cl) + "' (" + hex +
                  "): a name holds only letters, digits and `_` (write other characters inside backticks)",
              start, start + cl);
      }
      if (len == 0 && c == '$')  // `foo$bar` is written `` `foo$bar` ``
        error("unexpected-char",
              "unexpected character '$': a name holds only letters, digits and `_` (write other characters inside "
              "backticks)",
              start, start + 1);
      if (len == 0)
        error("unexpected-char", "unexpected character in input", start, start + 1);
      tok.kind     = kind;
      tok.end_byte = start + len;
      i            = start + len;
    }

    tok.line = buf_.line_of(start);
    tok.text = std::string_view(b + tok.start_byte, tok.end_byte - tok.start_byte);

    // A line never starts with `@` (a timing read continues nothing, src/scanner.c):
    // `const a = b` newline `@[1]` is an error, also inside a condition or a
    // bracket (`if x` newline `@[1] == 1 { }`).
    if (tok.kind == Token_kind::at && gap_has_newline(toks.empty() ? lo : toks.back().end_byte, start))
      error("line-starts-with-at", "a line cannot start with '@' (keep `name@[n]` on one line)", start, start + 1);

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

// Does the inter-token gap [gap_start, gap_end) (whitespace and comments only)
// hold a REAL line break? A newline inside a block comment does not count
// (matches tree-sitter: `a /* x\n y */ b` keeps a and b on one logical line);
// block comments NEST (match skip_trivia), so count depth to the OUTER close.
bool Lexer::gap_has_newline(uint32_t gap_start, uint32_t gap_end) const {
  const char* b = buf_.data();
  uint32_t    s = gap_start;
  while (s < gap_end) {
    char c = b[s];
    if (c == '\n') {
      return true;
    } else if (c == '/' && s + 1 < gap_end && b[s + 1] == '/') {
      s += 2;
      while (s < gap_end && b[s] != '\n') ++s;
    } else if (c == '/' && s + 1 < gap_end && b[s + 1] == '*') {
      s += 2;
      int gdepth = 1;
      while (s + 1 < gap_end && gdepth > 0) {
        if (b[s] == '/' && b[s + 1] == '*') {
          gdepth += 1;
          s += 2;
        } else if (b[s] == '*' && b[s + 1] == '/') {
          gdepth -= 1;
          s += 2;
        } else {
          ++s;
        }
      }
    } else {
      ++s;  // whitespace
    }
  }
  return false;
}

bool Lexer::gap_terminates(uint32_t gap_start, const Token& cur) const {
  // The inter-token gap holds only whitespace and comments. tree-sitter skips
  // comments (extras) when deciding the automatic semicolon, so a trailing
  // comment does NOT suppress the terminator. A terminator is emitted when a
  // newline appears anywhere in the gap (then the scanner.c continuation FSM
  // decides based on the next token), or when the next token is '}' / EOF.
  if (gap_has_newline(gap_start, cur.start_byte))
    return scanner_switch(buf_.data(), static_cast<uint32_t>(buf_.size()), cur.start_byte);
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
