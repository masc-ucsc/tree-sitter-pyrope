/*
 * Reference:
 * https://github.com/tree-sitter/tree-sitter-javascript/blob/4213a6c331d068b67de17e2dac917a90968cd33c/src/scanner.c
 */

#include "tree_sitter/parser.h"
#include <wctype.h>

enum TokenType { AUTOMATIC_SEMICOLON };

void *tree_sitter_pyrope_external_scanner_create() { return NULL; }
void tree_sitter_pyrope_external_scanner_destroy(void *p) {}
void tree_sitter_pyrope_external_scanner_reset(void *p) {}
unsigned tree_sitter_pyrope_external_scanner_serialize(void *p, char *buffer) {
  return 0;
}
void tree_sitter_pyrope_external_scanner_deserialize(void *p, const char *b,
                                                     unsigned n) {}

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

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
// identifier. `case` is deliberately EXCLUDED -- it doubles as the match-case
// clause keyword, where suppressing a terminator would change how the enclosing
// match parses; every operator below can only ever continue an expression.
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
  return !(iswalnum(lexer->lookahead) || lexer->lookahead == '_' || lexer->lookahead == '`');
}

static bool scan_whitespace_and_comments(TSLexer *lexer) {
  for (;;) {
    while (iswspace(lexer->lookahead)) {
      advance(lexer);
    }

    if (lexer->lookahead == '/') {
      advance(lexer);

      if (lexer->lookahead == '/') {
        advance(lexer);
        while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
          advance(lexer);
        }
      } else if (lexer->lookahead == '*') {
        advance(lexer);
        while (lexer->lookahead != 0) {
          if (lexer->lookahead == '*') {
            advance(lexer);
            if (lexer->lookahead == '/') {
              advance(lexer);
              break;
            }
          } else {
            advance(lexer);
          }
        }
      } else {
        return false;
      }
    } else {
      return true;
    }
  }
}

bool tree_sitter_pyrope_external_scanner_scan(void *payload, TSLexer *lexer,
                                              const bool *valid_symbols) {
  lexer->result_symbol = AUTOMATIC_SEMICOLON;
  lexer->mark_end(lexer);

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
    if (!iswspace(lexer->lookahead))
      return false;
    if (lexer->lookahead == '\n')
      break;
    advance(lexer);
  }

  advance(lexer);

  scan_whitespace_and_comments(lexer);

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
  case '+':
  case '-':
    return false;

  // Binary word operators continuing the previous line: and, or, implies, in,
  // does, equals, has. Returning false SUPPRESSES the automatic semicolon.
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

  case 'e': {  // else / elif (existing), or the `equals` binary operator
    advance(lexer);
    if (lexer->lookahead == 'q') {
      advance(lexer);
      return !scan_word_tail(lexer, "uals");  // equals
    }
    if (lexer->lookahead != 'l')
      return true;
    advance(lexer);
    if (lexer->lookahead != 's' && lexer->lookahead != 'i')
      return true;
    if (lexer->lookahead == 's') {
      advance(lexer);
      if (lexer->lookahead == 'e')
        return false;
    } else {
      advance(lexer);
      if (lexer->lookahead == 'f')
        return false;
    }
  }

  // Don't insert a semicolon before `!=`, but do insert one before a unary `!`.
  case '!':
    advance(lexer);
    return lexer->lookahead != '=';
  }

  return true;
}
