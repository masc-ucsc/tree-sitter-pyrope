/*
 * Reference:
 * https://github.com/tree-sitter/tree-sitter-javascript/blob/4213a6c331d068b67de17e2dac917a90968cd33c/src/scanner.c
 */

#include "tree_sitter/parser.h"
#include <wctype.h>

// Order must match `externals` in grammar.js.
// NEVER is never returned (grammar.js `_never`). ENUM_CALL is the retired
// expression form `enum(...)`, which the grammar always rejects.
// TUPLE_LAMBDA_END is the zero-width end of a lambda tuple entry (grammar.js
// `_tuple_lambda_end`). LAMBDA_BODY is never returned: it is valid only where
// a lambda's body `{` may follow (grammar.js `_lambda_body`), and a `{`
// starting the next line there is the body. LINE_END is never returned either:
// it is valid only where a construct that ends at a newline may end
// (grammar.js `_line_end`; see scan_newline_at).
enum TokenType {
  AUTOMATIC_SEMICOLON,
  COMMENT,
  STRING_CONTENT,
  FIELD_WORD,
  STRING_NEWLINE,
  NEVER,
  ENUM_CALL,
  TUPLE_LAMBDA_END,
  LAMBDA_BODY,
  LINE_END,
  SPACED_LT,
};

void *tree_sitter_pyrope_external_scanner_create() { return NULL; }
void tree_sitter_pyrope_external_scanner_destroy(void *p) {}
void tree_sitter_pyrope_external_scanner_reset(void *p) {}
unsigned tree_sitter_pyrope_external_scanner_serialize(void *p, char *buffer) {
  return 0;
}
void tree_sitter_pyrope_external_scanner_deserialize(void *p, const char *b,
                                                     unsigned n) {}

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

// An identifier character (or a backtick) right after a word operator makes
// the word a name: ASCII letters, digits, `_`, and any non-ASCII character
// except the spaces the grammar skips (`_space`: \p{Zs}, U+FEFF, U+2060,
// U+200B), since `identifier` admits \p{L}\p{Nd} and iswalnum() only knows
// ASCII in the C locale: `andé`, `oré`, `elseé` are names like `andz` (prpparse
// lexer.cpp is_ident_cont treats every non-ASCII byte as a name byte).
static bool is_ident_char(int32_t c) {
  if (c < 0x80) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '`';
  }
  if (c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200B) || c == 0x202F || c == 0x205F || c == 0x2060 ||
      c == 0x3000 || c == 0xFEFF) {
    return false;
  }
  return true;
}

// A BINARY WORD OPERATOR at the start of a continuation line, e.g.
//
//   const r = false
//          or true
//
// The symbol operators are already handled by the switch in scan(); the
// word-spelled ones were not, so the newline terminated the statement and the
// continuation parsed as a fresh one -- `false` alone, silently dropping every
// later term. livehd's comptime/lead_word_op_cont.prp pins exactly that with a
// cassert per operator, and equiv/lead_or_chain.prp is the `and` spelling.
//
// Matching is WHOLE-WORD: `ordinal_step(...)` starts with "or" but is an
// identifier, so the word only counts when the next character cannot continue an
// identifier. `case` is included: a line starting with `case` ALWAYS continues
// the previous expression (`const m = x` newline `case (a=1)` is `x case
// (a=1)`), exactly like prpparse (lexer.cpp word_op_continues). A match arm
// `case (a=1) { ... }` is unaffected: no automatic semicolon is valid between
// match arms (nor after the match's `{`), so the scanner never gets there.
//
// The caller has already dispatched on the first character, so `first_consumed`
// says whether it is still in lookahead (a TSLexer cannot rewind, so `else`/
// `elif` vs `equals` has to share its leading 'e').
static bool scan_word_tail(TSLexer *lexer, const char *tail) {
  for (const char *c = tail; *c != '\0'; ++c) {
    if (lexer->lookahead != (int32_t)*c) {
      return false;
    }
    advance(lexer);
  }
  // Whole-word only: a trailing identifier character means this was a name.
  return !is_ident_char(lexer->lookahead);
}

// Body of a block comment; the opening `/*` is already consumed. Block
// comments NEST, like lhd's lexer and prpparse (lexer.cpp skip_trivia):
// `/* a /* b */ c */` is ONE comment -- each `/*` opens a level and each `*/`
// closes one, scanning pairwise left to right. Returns false when the input
// ends before the outermost `*/` (an unterminated comment).
static bool scan_block_comment_body(TSLexer *lexer) {
  unsigned depth = 1;
  while (!lexer->eof(lexer)) {
    const int32_t c = lexer->lookahead;
    advance(lexer);
    if (c == '/' && lexer->lookahead == '*') {
      advance(lexer);
      ++depth;
    } else if (c == '*' && lexer->lookahead == '/') {
      advance(lexer);
      if (--depth == 0) {
        return true;
      }
    }
  }
  return false;
}

// Skips whitespace and comments (line and nested block) after a newline, so the
// automatic-semicolon rule sees the first real character of the next line.
// Returns false when that character is a `/` that opens no comment: a TSLexer
// cannot rewind, so the `/` is already consumed and the caller cannot switch on
// it. That `/` is the division operator continuing the previous line
// (`r = a` newline `/ b` is `r = a / b`, as in lhd), so the caller suppresses
// the automatic semicolon exactly like its own `case '/'` does. `*blank` is
// set when the last character skipped is a blank, not the end of a comment
// (the caller has just skipped the newline, a blank).
static bool scan_whitespace_and_comments(TSLexer *lexer, bool *blank) {
  *blank = true;
  for (;;) {
    while (iswspace(lexer->lookahead)) {
      advance(lexer);
      *blank = true;
    }

    if (lexer->lookahead == '/') {
      advance(lexer);

      if (lexer->lookahead == '/') {
        advance(lexer);
        while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
          advance(lexer);
        }
      } else if (lexer->lookahead == '*') {
        advance(lexer);
        scan_block_comment_body(lexer);
        *blank = false;
      } else {
        return false;  // a division `/`, already consumed
      }
    } else {
      return true;
    }
  }
}

// A comment starting at the current position: `// ...` to end of line, or a
// nesting `/* ... */`. An unterminated block comment is not a token, so the
// `/` falls back to the internal lexer and the file fails to parse.
static bool scan_comment(TSLexer *lexer) {
  if (lexer->lookahead != '/') {
    return false;
  }
  advance(lexer);
  if (lexer->lookahead == '/') {
    advance(lexer);
    while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
      advance(lexer);
    }
  } else if (lexer->lookahead == '*') {
    advance(lexer);
    if (!scan_block_comment_body(lexer)) {
      return false;
    }
  } else {
    return false;
  }
  lexer->result_symbol = COMMENT;
  lexer->mark_end(lexer);
  return true;
}

// The reserved words (grammar.js KEYWORDS; prpparse prp_keywords.def). The
// type words and `tick`/`step` (grammar.js ALWAYS_RESERVED) are not here: they
// never name a field (`(U8 = 1)` and `(step = 1)` are errors), so they never
// become a FIELD_WORD.
static const char *const reserved_words[] = {
    "import", "as",     "break", "continue", "return",   "if",   "elif",  "else",   "unique", "for",
    "in",     "while",  "loop",  "match",    "test",     "formal", "type", "impl",  "enum",  "ref",
    "pub",    "comptime", "fluid", "const",  "mut",      "reg",  "wire",  "stage",  "wrap",  "sat",
    "comb",   "mod",    "pipe",  "sext",     "zext",     "not",  "and",   "or",     "implies", "has",
    "case",   "does",   "equals", "true",    "false",
};

static bool is_reserved_word(const char *w) {
  for (unsigned i = 0; i < sizeof(reserved_words) / sizeof(reserved_words[0]); ++i) {
    const char *a = reserved_words[i];
    const char *b = w;
    while (*a && *a == *b) {
      ++a;
      ++b;
    }
    if (*a == '\0' && *b == '\0') {
      return true;
    }
  }
  return false;
}

// A reserved word used as a FIELD name (grammar.js `_field_word`): only where
// the grammar takes a field or argument name (a tuple entry, a named argument,
// an attribute or generic binding, a destructuring `name = local`), and only
// when the word is followed -- past blanks and comments -- by `=` (not `==`) or
// `:`. So `(if = 1)`, `f(pub = 1)`, `(const type = 1)`, `(in:U8)` name a field
// while `(if c {1} else {2})` and `(true == x)` keep the keyword. prpparse
// applies the same rule (parser.cpp at_field_word).
//
// The same word scan also catches the retired expression form `enum(...)`
// (grammar.js `_enum_call`, valid only where an expression or a type can
// start): the whole word `enum` followed, past blanks and block comments on the
// same line, by `(`. That token covers only the word, and the grammar then
// requires `_never`, so the parse fails right there. prpparse rejects the same
// form. Any other `enum` stays contextual (`f(enum)`, `x.enum`, or the
// declaration `enum E = (...)`).
static bool scan_enum_call(TSLexer *lexer) {
  for (;;) {
    while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
      advance(lexer);
    }
    if (lexer->lookahead != '/') {
      break;
    }
    advance(lexer);
    if (lexer->lookahead != '*') {
      return false;
    }
    advance(lexer);
    if (!scan_block_comment_body(lexer)) {
      return false;
    }
  }
  if (lexer->lookahead != '(') {
    return false;
  }
  lexer->result_symbol = ENUM_CALL;
  return true;
}

static bool scan_keyword_word(TSLexer *lexer, const bool *valid_symbols) {
  char word[16];
  unsigned n = 0;
  while (lexer->lookahead >= 'a' && lexer->lookahead <= 'z') {
    if (n + 1 >= sizeof(word)) {
      return false;  // longer than any reserved word
    }
    word[n++] = (char)lexer->lookahead;
    advance(lexer);
  }
  word[n] = '\0';
  if (n == 0 || is_ident_char(lexer->lookahead) || !is_reserved_word(word)) {
    return false;
  }
  lexer->mark_end(lexer);
  if (valid_symbols[ENUM_CALL] && word[0] == 'e' && word[1] == 'n' && word[2] == 'u' && word[3] == 'm' &&
      word[4] == '\0') {
    // A word followed by `(` is never a field name, so no FIELD_WORD fallback.
    return scan_enum_call(lexer);
  }
  if (!valid_symbols[FIELD_WORD]) {
    return false;
  }
  for (;;) {
    while (iswspace(lexer->lookahead)) {
      advance(lexer);
    }
    if (lexer->lookahead != '/') {
      break;
    }
    advance(lexer);
    if (lexer->lookahead == '/') {
      while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
        advance(lexer);
      }
    } else if (lexer->lookahead == '*') {
      advance(lexer);
      if (!scan_block_comment_body(lexer)) {
        return false;
      }
    } else {
      return false;
    }
  }
  if (lexer->lookahead == ':') {
    lexer->result_symbol = FIELD_WORD;
    return true;
  }
  if (lexer->lookahead != '=') {
    return false;
  }
  advance(lexer);
  if (lexer->lookahead == '=') {
    return false;  // `==`: a comparison, the word keeps its keyword meaning
  }
  lexer->result_symbol = FIELD_WORD;
  return true;
}

// A run of literal text inside a double-quoted string. Stops (without
// consuming) at an escape, an interpolation `{`, the closing quote or a
// newline, which the grammar's own token.immediate pieces match. Comment
// openers are plain text here: `"//x"` and `"a /* b */ c"` are strings.
//
// `{{` and `}}` are ESCAPED LITERAL BRACES (like Python/Rust format strings):
// `"{{x}}"` is the text `{x}`, no hole. A `{{` is text here, so only a single
// `{` reaches the grammar's hole rule; `"{{{x}}}"` is `{`, the hole `{x}`, `}`.
// A lone `}` outside a hole is an error (write `}}`): the text stops before
// it, and no string piece of the grammar accepts it. There is no `\{` escape
// (spec 2026-09-29 §7). prpparse agrees (lexer.cpp lex_istring, parser.cpp
// parse_istring).
static bool scan_string_content(TSLexer *lexer) {
  bool any = false;
  while (!lexer->eof(lexer)) {
    const int32_t c = lexer->lookahead;
    if (c == '"' || c == '\\' || c == '\n') {
      break;
    }
    if (c == '{' || c == '}') {
      // Peek past the brace; the token ends before it (mark_end) unless a
      // second identical brace makes the pair literal text.
      lexer->mark_end(lexer);
      advance(lexer);
      if (lexer->lookahead != c) {
        if (!any) {
          return false;
        }
        lexer->result_symbol = STRING_CONTENT;
        return true;
      }
    }
    advance(lexer);
    any = true;
  }
  if (!any) {
    return false;
  }
  lexer->result_symbol = STRING_CONTENT;
  lexer->mark_end(lexer);
  return true;
}

// Skips blanks, newlines and comments (a zero-width token's lookahead; the
// caller has already marked the token end). Returns false on an unterminated
// block comment or a `/` that opens no comment.
static bool skip_blanks_and_comments(TSLexer *lexer) {
  for (;;) {
    while (iswspace(lexer->lookahead)) {
      advance(lexer);
    }
    if (lexer->lookahead != '/') {
      return true;
    }
    advance(lexer);
    if (lexer->lookahead == '/') {
      while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
        advance(lexer);
      }
    } else if (lexer->lookahead == '*') {
      advance(lexer);
      if (!scan_block_comment_body(lexer)) {
        return false;
      }
    } else {
      return false;
    }
  }
}

// After a lambda that is a whole tuple entry (grammar.js `_tuple_lambda_end`):
// the entry ends there, so the next token (past blanks, newlines and comments)
// must be `,`, `)` or `]`, and then the zero-width TUPLE_LAMBDA_END ends the
// entry. The mark is also valid while the lambda may still go on (after its
// inputs or its outputs), so a `{` (the body), `->` (the outputs), a single
// `:` (an output's type) or `@` (an output's timing) is left to the grammar.
// Anything else (`+ 1`, `#[0]`, `.x`, `(1)`, `::[a]`) would make the lambda an
// operand or a suffix head inside the tuple, which prpparse rejects, so the
// scanner returns STRING_NEWLINE (a token no rule accepts) and the parse fails.
static bool scan_tuple_lambda_end(TSLexer *lexer) {
  lexer->mark_end(lexer);  // zero width
  if (!skip_blanks_and_comments(lexer)) {
    lexer->result_symbol = STRING_NEWLINE;
    return true;
  }
  const int32_t c = lexer->lookahead;
  if (c == ',' || c == ')' || c == ']') {
    lexer->result_symbol = TUPLE_LAMBDA_END;
    return true;
  }
  if (c == '{' || c == '@') {
    return false;
  }
  if (c == '-' || c == ':') {
    advance(lexer);
    if ((c == '-' && lexer->lookahead == '>') || (c == ':' && lexer->lookahead != ':')) {
      return false;
    }
  }
  lexer->result_symbol = STRING_NEWLINE;
  return true;
}

// A `<` with a blank right before it is always a comparison, never the
// opening of a call-site generic list (owner ruling 107): `f<N=3>(x)` needs
// the `<` glued to the callee, and `a < b > (c)` is a comparison chain. Where
// a comparison may continue (grammar.js `_binary_compare`) the scanner returns
// the zero-width SPACED_LT before such a `<`; the generic call has no use for
// it, so only the comparison reading survives (prpparse: try_generic_call
// requires the glued `<` too). Called at the `<` after the blanks, with the
// token end already marked where they start. `<=`, `<<` and `<<=` are no `<`.
// A comment glued to the `<` (`f /*c*/<T>(x)`) leaves it glued, as in prpparse
// (which looks at the character before the `<`).
static bool scan_spaced_lt(TSLexer *lexer) {
  advance(lexer);  // '<'
  if (lexer->lookahead == '=' || lexer->lookahead == '<') {
    return false;
  }
  lexer->result_symbol = SPACED_LT;
  return true;
}

// A line never starts with `@` (02-basics.md; prpparse lexer.cpp
// "line-starts-with-at"): blanks (and comments) holding a newline and followed
// by `@` make `x` newline `@[1]` an error everywhere -- after a statement, in
// an `if`/`while` condition, inside `(...)`. The scanner returns STRING_NEWLINE
// (a token no rule accepts) there. Where an automatic semicolon is valid the
// semicolon logic runs instead, and no statement starts with `@`.
//
// A line starting with `(` or `[` never continues a construct that ends at a
// newline either (02-basics: it starts a new statement): where such a
// construct may end (`line_end`, grammar.js `_line_end`: a statement's
// assignment target, an `if`/`while`/`for`/`match`/`tick` header) `(` or `[`
// after a newline is refused the same way. So `if a` newline `(b) { }` is an
// error, and the GLR reading of `mut x:U8` newline `(r, x) = (1, 2)` as `mut
// x:U8(r, x) = ...` dies (the declaration reading ends at the newline: the
// semicolon logic). Inside brackets the line continues (`f(a` newline `(b))`).
//
// The same blanks (and comments) followed by a spaced `<` give SPACED_LT where
// it is valid (`spaced_lt`, see scan_spaced_lt).
static bool scan_newline_at(TSLexer *lexer, bool line_end, bool spaced_lt) {
  lexer->mark_end(lexer);  // SPACED_LT is zero width; STRING_NEWLINE re-marks
  bool newline = false;
  bool blank   = true;  // called at a blank
  for (;;) {
    while (iswspace(lexer->lookahead)) {
      if (lexer->lookahead == '\n') {
        newline = true;
      }
      advance(lexer);
      blank = true;
    }
    if (lexer->lookahead != '/') {
      break;
    }
    advance(lexer);
    if (lexer->lookahead == '/') {
      while (!lexer->eof(lexer) && lexer->lookahead != '\n') {
        advance(lexer);
      }
    } else if (lexer->lookahead == '*') {
      advance(lexer);
      if (!scan_block_comment_body(lexer)) {
        return false;
      }
      blank = false;
    } else {
      return false;
    }
  }
  if (spaced_lt && blank && lexer->lookahead == '<') {
    return scan_spaced_lt(lexer);
  }
  if (!newline || !(lexer->lookahead == '@' || (line_end && (lexer->lookahead == '(' || lexer->lookahead == '[')))) {
    return false;
  }
  lexer->mark_end(lexer);
  lexer->result_symbol = STRING_NEWLINE;
  return true;
}

bool tree_sitter_pyrope_external_scanner_scan(void *payload, TSLexer *lexer,
                                              const bool *valid_symbols) {
  // During error recovery tree-sitter marks EVERY external valid. The automatic
  // semicolon and string text are never valid together otherwise (a string
  // holds no statement terminator), so both valid means "recovering": the
  // position may be inside a string, where `/` is text, so neither string text
  // nor a comment is produced; only the automatic-semicolon logic below runs,
  // as it did before `comment` became external.
  const bool recovering = valid_symbols[AUTOMATIC_SEMICOLON] && valid_symbols[STRING_CONTENT];

  if (!recovering) {
    // Inside a double-quoted string, outside any `{...}` hole: only string
    // text can start here. `comment` is an extra, so tree-sitter offers it in
    // these states too (extras are valid everywhere, even between
    // token.immediate pieces), and scanning one would swallow `"//x"` up to
    // the end of the line. Return false at `\\`, `{`, `"` or a newline so the
    // grammar's own immediate tokens take over.
    if (valid_symbols[STRING_CONTENT]) {
      // A raw newline in string text is an error (like in a '...' string;
      // prpparse: "newline in interpolated string"). STRING_NEWLINE is in no
      // grammar rule, so returning it always fails the parse. A newline inside
      // a `{...}` hole is ordinary whitespace (not string text).
      if (lexer->lookahead == '\n') {
        advance(lexer);
        lexer->mark_end(lexer);
        lexer->result_symbol = STRING_NEWLINE;
        return true;
      }
      return scan_string_content(lexer);
    }

    // `comment` is an extra, so it is valid almost everywhere else (including
    // inside a `{...}` interpolation hole). A comment and the automatic
    // semicolon never compete: the semicolon logic below only fires on
    // whitespace (or EOF / `}`), and a comment only starts on `/`. It comes
    // before the zero-width checks below: those skip comments to look ahead,
    // and when they miss, the comment itself must still be scanned.
    if (valid_symbols[COMMENT] && lexer->lookahead == '/') {
      if (scan_comment(lexer)) {
        return true;
      }
      if (valid_symbols[TUPLE_LAMBDA_END]) {
        // A division right after a lambda tuple entry (see scan_tuple_lambda_end).
        lexer->mark_end(lexer);
        lexer->result_symbol = STRING_NEWLINE;
        return true;
      }
      return false;
    }

    // The end of a lambda tuple entry (zero width; see scan_tuple_lambda_end).
    if (valid_symbols[TUPLE_LAMBDA_END]) {
      return scan_tuple_lambda_end(lexer);
    }

    // A line starting with `@`, or with `(`/`[` after a construct that ends at
    // a newline (see scan_newline_at). At blanks no other external token can
    // start, so a miss falls back to the internal lexer.
    if (!valid_symbols[AUTOMATIC_SEMICOLON] && iswspace(lexer->lookahead)) {
      return scan_newline_at(lexer, valid_symbols[LINE_END], valid_symbols[SPACED_LT]);
    }

    // A keyword field name, or the retired `enum(...)`. At a letter the
    // automatic semicolon never applies (it needs whitespace, `}` or EOF), so a
    // miss simply falls back to the internal lexer (the keyword or an
    // identifier).
    if ((valid_symbols[FIELD_WORD] || valid_symbols[ENUM_CALL]) && lexer->lookahead >= 'a' &&
        lexer->lookahead <= 'z') {
      return scan_keyword_word(lexer, valid_symbols);
    }
  }

  // With `comment` external, the scanner now runs in every state; only emit
  // the automatic semicolon where the parser can take it.
  if (!valid_symbols[AUTOMATIC_SEMICOLON]) {
    return false;
  }

  lexer->result_symbol = AUTOMATIC_SEMICOLON;
  lexer->mark_end(lexer);

  const bool spaced_lt = valid_symbols[SPACED_LT] && !recovering;
  bool       blank     = false;
  for (;;) {
    if (lexer->lookahead == 0)
      return true;

    // For code like:
    // a = { d } + 1
    // The 'd' is a statement, so it needs a \n before \}
    if (lexer->lookahead == '}') {
      return true;
    }

    if (lexer->is_at_included_range_start(lexer))
      return true;
    if (!iswspace(lexer->lookahead)) {
      // `a < b` (see scan_spaced_lt)
      return spaced_lt && blank && lexer->lookahead == '<' && scan_spaced_lt(lexer);
    }
    if (lexer->lookahead == '\n')
      break;
    advance(lexer);
    blank = true;
  }

  advance(lexer);

  if (!scan_whitespace_and_comments(lexer, &blank)) {
    return false;  // the next line starts with `/`: a division continues
  }
  // A line starting with a spaced `<` continues as a comparison.
  if (spaced_lt && blank && lexer->lookahead == '<') {
    return scan_spaced_lt(lexer);
  }

  switch (lexer->lookahead) {
  case ',':
  case '=':
  case '.':
  case '>':
  case '<':
  case '&':
  case '^':
  case '|':
  case '*':
  case '/':
  case '%':
  case '+':
  case '-':
  // A line starting with `:` (a type annotation or attribute: `mut acc`
  // newline `:u8 = a`, `f` newline `::[name=u](x)`) or `#` (a bit selector:
  // `#[0..=3]`, `#|[..]`, `#sext[..]`, ...) continues the previous statement;
  // no statement starts with either (prpparse lexer.cpp scanner_switch).
  case ':':
  case '#':
    return false;

  // Binary word operators continuing the previous line: and, or, implies, in,
  // does, equals, has, case. Returning false SUPPRESSES the automatic semicolon.
  case 'a':
    advance(lexer);
    return !scan_word_tail(lexer, "nd");  // and
  case 'o':
    advance(lexer);
    return !scan_word_tail(lexer, "r");  // or
  case 'i':
    advance(lexer);
    if (lexer->lookahead == 'n') {
      advance(lexer);
      return !scan_word_tail(lexer, "");  // in
    }
    return !scan_word_tail(lexer, "mplies");  // implies
  case 'd':
    advance(lexer);
    return !scan_word_tail(lexer, "oes");  // does
  case 'h':
    advance(lexer);
    return !scan_word_tail(lexer, "as");  // has
  case 'c':
    advance(lexer);
    return !scan_word_tail(lexer, "ase");  // case

  case 'e':  // else / elif continue an `if` chain; `equals` is a binary operator
    // WHOLE-WORD matching, like the other word operators: `els`, `eli`,
    // `elsev`, `elifx`, `elsewhere` are plain names that start a new
    // statement (a prefix match used to continue the line, and a missing
    // `return` fell through into the `!=` check below).
    advance(lexer);
    if (lexer->lookahead == 'q') {
      advance(lexer);
      return !scan_word_tail(lexer, "uals");  // equals
    }
    if (lexer->lookahead != 'l')
      return true;
    advance(lexer);
    if (lexer->lookahead == 's') {
      advance(lexer);
      return !scan_word_tail(lexer, "e");  // else
    }
    if (lexer->lookahead == 'i') {
      advance(lexer);
      return !scan_word_tail(lexer, "f");  // elif
    }
    return true;

  // Don't insert a semicolon before `!=`, but do insert one before a unary `!`.
  case '!':
    advance(lexer);
    return lexer->lookahead != '=';

  // A lambda's body on the next line (Allman style) belongs to the lambda.
  case '{':
    return !valid_symbols[LAMBDA_BODY];
  }

  return true;
}
