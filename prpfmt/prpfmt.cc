#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <pthread.h>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <tree_sitter/api.h>

#include "prpfmt.h"

/******************************************************************************
 * Internal Bookkeeping                                                       *
 ******************************************************************************/

// Forward declarations for static helper functions
// The grammar symbol prpfmt dispatches on. A type word used as a value is
// its own grammar rule aliased to a public node (grammar.js `_type_word_name`
// -> `identifier`, `_type_word_call` -> `function_call_expression`); map those
// back so `U8`, `Bool`, `U8(x)` format exactly like any identifier and call.
// The aliased name node reports the token it wraps: the hidden `_UN`/`_SN`, or
// the `Unsigned`/`Signed`/`Bool`/`String`/`Clock`/`Reset` keyword. Only the
// NAMED (aliased) node maps: inside `uint_type`/`bool_type`/... the same
// keyword is an anonymous child (and `_UN`/`_SN` are hidden there).
static inline TSSymbol grammar_symbol_of(TSNode node) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  switch (symbol) {
    case sym__type_word_name:
    case sym__UN:
    case sym__SN:
      return sym_identifier;
    case anon_sym_Unsigned:
    case anon_sym_Signed:
    case anon_sym_Bool:
    case anon_sym_String:
    case anon_sym_Clock:
    case anon_sym_Reset:
      return ts_node_is_named(node) ? sym_identifier : symbol;
    case sym__type_word_call:
      return sym_function_call_expression;
    // A tuple entry `(a=1)` and an init-clause destructuring `(a, b) = f()`
    // are the hidden halves of `assignment` aliased back to it (grammar.js
    // `_single_assignment` / `_destructuring_assignment`).
    case sym__single_assignment:
    case sym__destructuring_assignment:
    case sym__field_assignment:  // a tuple-entry field `(a = 1)`
      return sym_assignment;
    // The binding-name and keyword-field variants of grammar.js (a name being
    // bound, `_binding_*`; a keyword field name, `_field_*`) and the enum body
    // are aliased back to the plain nodes.
    case sym__field_word:
      return sym_identifier;
    case sym__binding_typed_identifier:
    case sym__binding_typed_name:
    case sym__field_typed_word:
    case sym__slot_name:          // a destructuring slot `(a, b) = f()`
    case sym__binding_slot_name:  // a declared one `const (a, b) = f()`
      return sym_typed_identifier;
    // A destructuring's `=` (only `=` is legal there).
    case sym__plain_assign:
      return sym_assignment_operator;
    // An assignment TARGET chain rooted at a name (`a.b = 1`, `a[0] = 1`,
    // `a#[0] = 1`, `a.[attr] = 1`): the same nodes as the expression chains.
    case sym__target_dot:
    // A rename slot's source path `(x=t.p1) = f()` and a bare generic value
    // prefix `<N=cfg.w.[max]>` (same shape as an expression dot_expression).
    case sym_generic_dotted_name:
      return sym_dot_expression;
    case sym__target_member:
      return sym_member_selection;
    case sym__target_bit:
      return sym_bit_selection;
    case sym__target_attr:
      return sym_attribute_read;
    // A constant in a type position (never negative: grammar.js
    // `_type_constant`).
    case sym__type_constant:
      return sym_constant;
    case sym__type_integer:
    case sym__negative_constant:
      return sym_integer_literal;
    case sym__negative_generic_value:
      return sym_constant;
    // A dotted type name (`x:pkg.T`): each part keeps its expression_type.
    case sym__type_name_part:
    case sym__type_name_chain:
      return sym_expression_type;
    case sym__binding_arg_list:
      return sym_arg_list;
    case sym__binding_lvalue_list:
      return sym_lvalue_list;
    case sym__binding_lvalue_item:
      return sym_lvalue_item;
    case sym__binding_named_lvalue:
      return sym_named_lvalue;
    case sym__enum_tuple:
      return sym_tuple;
    default:
      return symbol;
  }
}
static void unwrap_hidden(TSNode &node, TSSymbol &symbol);
static void emit_operator(TSNode node, PrpfmtState &st, SpacingConfig spacing);
static void ensure_match_arm_started(bool seen_lbrace, bool &arm_started);
static void emit_vertical_transition(PrpfmtState &st, TSNode curr, TSNode next, bool force_break);
static std::string_view skip_leading_whitespace(std::string_view str);
bool starts_source_line(TSNode node, const PrpfmtState &st);
static bool mid_line(const PrpfmtState &st);
static bool is_block_comment(TSNode node, const PrpfmtState &st);
// A comment inside an expression that puts what follows it on a new line
// (`const y = // c` newline `a + 1`, `v#[` newline `// c` newline `0..<4]`):
// the rest continues one indent level deeper than the statement, in a fresh
// group (so a Human-mode explosion forced by the comment's break does not
// split the short expression after it), never at the statement column or
// aligned under an `=`. Call before printing each comment child; `open` is
// the caller's state, closed by end_comment_continuation.
static void comment_continuation(TSNode comment, PrpfmtState &st, bool &open, bool after);
static void end_comment_continuation(PrpfmtState &st, bool open);
static std::string_view comment_text(TSNode node, const PrpfmtState &st);
static void check_format_directives(std::string_view node_text, PrpfmtState &st);
// Where a grouping `(e)` sits, which decides what `e` may be without it.
enum class Grouping {
  Whole,    // a whole condition/subject, right-hand side or named value
  Logical,  // a direct operand of `and`/`or`/`implies`
  Not,      // the operand of `not`/`!`
  Operand,  // an operand of another binary operator, or a selector index
};
static TSNode redundant_grouping(TSNode node, Grouping where, std::string_view src = {});
// While > 0, no grouping parentheses drop (see generic_hazard in print_list).
static thread_local int keep_grouping = 0;
// Set for the fallback pass when a first pass changed how the output parses
// (see prpfmt_format_string_mode): no parentheses drop and no list sorts.
static thread_local bool conservative_pass = false;
// The state being formatted on this thread, so helpers without a state
// argument (redundant_grouping) can use its memoized subtree scans.
static thread_local const PrpfmtState *active_state = nullptr;

// Return a view of str with any leading whitespace dropped.
static std::string_view skip_leading_whitespace(std::string_view str) {
  size_t i = 0;
  while (i < str.size() && isspace((unsigned char)str[i])) {
    i++;
  }
  return str.substr(i);
}

// A comment right after `node` on its line that ends the line: a `//`
// comment, or a block comment with nothing after it on that line.
static bool has_trailing_comment(TSNode node, const PrpfmtState &st) {
  TSNode next = ts_node_next_sibling(node);
  if (ts_node_is_null(next) || grammar_symbol_of(next) != sym_comment ||
      ts_node_start_point(next).row != ts_node_end_point(node).row)
    return false;
  if (skip_leading_whitespace(get_node_text(next, st.source_code)).starts_with("//")) return true;
  TSNode after = ts_node_next_sibling(next);
  return ts_node_is_null(after) || ts_node_start_point(after).row > ts_node_end_point(next).row;
}

// Alignment is limited to consecutive statements of the same kind.
static std::string declaration_kind(TSNode node, const PrpfmtState &st) {
  std::string kind(get_node_text(node, st.source_code));
  std::erase_if(kind, [](unsigned char c) { return std::isspace(c); });
  return kind;
}

// A comment before `end` inside `node` that keeps a line break: a `//`
// comment, or a block comment that starts its source line after some of the
// statement's code (`wire a` newline `/* c */` newline `:Unsigned(bits=W) = nil`).
// The last source token before byte `pos` (skipping blanks), e.g. `,`.
static std::string_view token_before(uint32_t pos, const PrpfmtState &st) {
  std::string_view src = st.source_code;
  while (pos > 0 && std::isspace(static_cast<unsigned char>(src[pos - 1]))) --pos;
  uint32_t end = pos;
  auto op = [](char c) { return std::string_view(",+-*/%&|^<>!~([").find(c) != std::string_view::npos; };
  if (pos > 0 && op(src[pos - 1])) {
    while (pos > 0 && op(src[pos - 1])) --pos;
  } else {
    while (pos > 0 && (std::isalnum(static_cast<unsigned char>(src[pos - 1])) || src[pos - 1] == '_')) --pos;
  }
  return src.substr(pos, end - pos);
}

static bool comment_breaks_before(TSNode node, uint32_t begin, uint32_t end, const PrpfmtState &st) {
  if (ts_node_start_byte(node) >= end) return false;
  if (grammar_symbol_of(node) == sym_comment) {
    auto text = skip_leading_whitespace(get_node_text(node, st.source_code));
    if (text.starts_with("//")) return true;
    if (text.find('\n') != std::string_view::npos) return true;  // a multi-line block comment
    if (ts_node_start_byte(node) <= begin || !starts_source_line(node, st)) return false;
    // A block comment that starts its line joins the line before after an
    // opening `(`/`[`, a list comma or a binary operator (`f(a,` newline
    // `/* c */ b)` prints `f(a, /* c */ b)`).
    auto before = token_before(ts_node_start_byte(node), st);
    if (before == "and" || before == "or" || before == "implies") return false;
    // One right before a closing `)`/`]` or a list comma trails the item
    // before it (`f(a` newline `/* c */` newline `)` prints `f(a /* c */ )`).
    for (TSNode next = ts_node_next_sibling(node); !ts_node_is_null(next); next = ts_node_next_sibling(next)) {
      if (grammar_symbol_of(next) == sym_comment) continue;
      auto text = get_node_text(next, st.source_code);
      if (text == ")" || text == "]" || text == ",") return false;
      break;
    }
    return before.empty() || std::string_view(",+-*/%&|^<>!~([").find(before.back()) == std::string_view::npos;
  }
  for (uint32_t i = 0; i < ts_node_child_count(node); ++i)
    if (comment_breaks_before(ts_node_child(node, i), begin, end, st)) return true;
  return false;
}

// The statement always breaks before its `=` (a comment inside its left-hand
// side keeps a line break): it takes no padding and ends the group, like a
// statement whose left-hand side splits at the width.
static bool breaks_before_operator(TSNode node, const PrpfmtState &st) {
  TSNode op = ts_node_child_by_field_name(node, "operator", 8);
  if (ts_node_is_null(op)) return false;
  return comment_breaks_before(node, ts_node_start_byte(node), ts_node_start_byte(op), st);
}

static std::string alignment_kind(TSNode node, const PrpfmtState &st) {
  if (st.mode != PRPFMT_HUMAN || ts_node_is_null(node)) return {};
  auto symbol = grammar_symbol_of(node);
  if (symbol == sym_assignment && breaks_before_operator(node, st)) return {};
  if (symbol == anon_sym_wrap) return "wrap";
  if (symbol == anon_sym_sat) return "sat";
  if (symbol == sym_assignment) {
    auto prev = ts_node_prev_sibling(node);
    if (!ts_node_is_null(prev)) {
      auto prefix = grammar_symbol_of(prev);
      if (prefix == anon_sym_wrap) return "wrap";
      if (prefix == anon_sym_sat) return "sat";
    }
    auto decl = ts_node_child_by_field_name(node, "decl", 4);
    if (!ts_node_is_null(decl)) return declaration_kind(decl, st);
    // Kind keywords may precede the lvalue without a field label.
    for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
      auto child = ts_node_child(node, i);
      if (grammar_symbol_of(child) == sym_var_or_let_or_reg)
        return declaration_kind(child, st);
    }
    return "assignment";
  }
  if (symbol == sym_type_statement) return "type";
  return {};
}

static bool is_alignable(TSNode node, const PrpfmtState &st) {
  return !alignment_kind(node, st).empty();
}

static bool same_alignment_kind(TSNode a, TSNode b, const PrpfmtState &st) {
  auto kind = alignment_kind(a, st);
  return !kind.empty() && kind == alignment_kind(b, st);
}

// Detect standalone line comments within a node to force vertical wrapping
enum ScanKind : uint32_t { scan_line_comment, scan_vertical, scan_must_break, scan_any_comment, scan_lt, scan_gt, scan_order_above };

// Look up / store a memoized subtree scan (see PrpfmtState::scan_cache).
template <class F>
static bool cached_scan(TSNode node, ScanKind kind, const PrpfmtState &st, F compute) {
  PrpfmtState::NodeKey key{ts_node_start_byte(node), ts_node_end_byte(node), grammar_symbol_of(node), kind};
  if (auto it = st.scan_cache.find(key); it != st.scan_cache.end()) return it->second;
  bool result = compute();
  st.scan_cache[key] = result;
  return result;
}

bool has_recursive_line_comment(TSNode node, const PrpfmtState &st) {
  TSSymbol symbol = grammar_symbol_of(node);
  if (symbol == sym_comment) {
    std::string_view text = get_node_text(node, st.source_code);
    return skip_leading_whitespace(text).starts_with("//");
  }
  uint32_t child_count = ts_node_child_count(node);
  if (child_count == 0) return false;
  return cached_scan(node, scan_line_comment, st, [&] {
    TSTreeCursor cursor = ts_tree_cursor_new(node);
    bool found = false;
    if (ts_tree_cursor_goto_first_child(&cursor)) {
      do {
        if (has_recursive_line_comment(ts_tree_cursor_current_node(&cursor), st)) {
          found = true;
          break;
        }
      } while (ts_tree_cursor_goto_next_sibling(&cursor));
    }
    ts_tree_cursor_delete(&cursor);
    return found;
  });
}

// Only expression scopes may be inline; comments require real line breaks.
static bool is_inline_eligible(TSNode node, const PrpfmtState &st) {
  if (has_recursive_line_comment(node, st)) {
    return false;
  }

  uint32_t named_child_count = ts_node_named_child_count(node);
  return named_child_count <= 1;
}

static bool scope_must_break(TSNode scope, const PrpfmtState &st);
static bool is_statement_symbol(TSSymbol symbol);

// A comment among an `if` expression's own children (`if c { 1 } /* b */
// else { 2 }`, `if /* c */ x {`) gives the chain the statement layout (see
// print_if_expression), so it spans lines wherever it sits.
static bool if_header_comment(TSNode node) {
  if (grammar_symbol_of(node) != sym_if_expression) return false;
  for (uint32_t i = 0; i < ts_node_child_count(node); ++i)
    if (grammar_symbol_of(ts_node_child(node, i)) == sym_comment) return true;
  return false;
}

// Something inside `node` (not `node` itself) that always spans lines: a
// `match` expression (its arms go one per line), a block that must break, a
// lambda with a statement block, or an `if` expression with a comment in its
// header.
static bool holds_vertical_layout(TSNode node, const PrpfmtState &st) {
  if (ts_node_child_count(node) == 0) return false;
  return cached_scan(node, scan_vertical, st, [&] {
    for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
      auto child = ts_node_child(node, i);
      auto symbol = grammar_symbol_of(child);
      if (symbol == sym_match_expression) return true;
      if (symbol == sym_lambda && !ts_node_is_null(ts_node_child_by_field_name(child, "code", 4))) return true;
      if (if_header_comment(child)) return true;
      // In Human mode a comment that keeps a line break inside an `if`
      // condition (`if b` newline `/* c */` newline `== 0 {`) gives the chain
      // the statement layout, so every branch block breaks.
      if (symbol == sym_if_expression && st.mode == PRPFMT_HUMAN) {
        for (uint32_t k = 0; k < ts_node_child_count(child); ++k) {
          auto part = ts_node_child(child, k);
          if (grammar_symbol_of(part) != sym_scope_statement &&
              comment_breaks_before(part, ts_node_start_byte(child), UINT32_MAX, st))
            return true;
        }
      }
      if (symbol == sym_scope_statement) {
        if (scope_must_break(child, st)) return true;
        continue;  // scope_must_break looked inside it already
      }
      if (holds_vertical_layout(child, st)) return true;
    }
    return false;
  });
}

// A block that cannot print inline: comments or several statements, content
// that always spans lines (a `match` expression, a nested block that must
// break), and in Human mode a source break after its `{`.
static bool scope_must_break(TSNode scope, const PrpfmtState &st) {
  return cached_scan(scope, scan_must_break, st, [&] {
    if (!is_inline_eligible(scope, st)) return true;
    if (st.mode == PRPFMT_HUMAN && ts_node_named_child_count(scope) &&
        ts_node_start_point(ts_node_named_child(scope, 0)).row > ts_node_start_point(scope).row)
      return true;
    return holds_vertical_layout(scope, st);
  });
}

// Check if at least one blank line between nodes in original text
static bool has_blank_line_between(TSNode prev, TSNode curr) {
  if (ts_node_is_null(prev) || ts_node_is_null(curr)) {
    return false;
  }

  return ts_node_start_point(curr).row > ts_node_end_point(prev).row + 1;
}

// Index of the first child at or after `i` that is not a `;` terminator.
// A comment that starts on the line where the previous sibling ends.
static bool is_trailing_comment(TSNode node) {
  if (grammar_symbol_of(node) != sym_comment) return false;
  auto prev = ts_node_prev_sibling(node);
  return !ts_node_is_null(prev) && ts_node_start_point(node).row == ts_node_end_point(prev).row;
}

// The next sibling that takes part in alignment: `;` terminators and
// trailing comments (`mut p = 1 // c`) are transparent to alignment groups.
static uint32_t next_non_semicolon(TSNode parent, uint32_t i) {
  uint32_t count = ts_node_child_count(parent);
  while (i < count) {
    auto child = ts_node_child(parent, i);
    if (grammar_symbol_of(child) != anon_sym_SEMI && !is_trailing_comment(child)) break;
    ++i;
  }
  return i;
}

// A destructuring or `for` index list (`const (x1, y1) = f(...)`) is rarely
// the long part of its line: it lets what follows split first, so only the
// text up to that part's first break counts against it.
static void yield_list_from(PrpfmtState &st, size_t from) {
  for (size_t k = from; k < st.buffer.size(); ++k) {
    if (st.buffer[k].type == TOKEN_GROUP_START && st.buffer[k].list) {
      st.buffer[k].yield_next = true;
      st.buffer[k].yield_deep = true;
      return;
    }
  }
}

// Emit appropriate spacing (space, newline, or blank line) between nodes based on original text formatting
static void emit_vertical_transition(PrpfmtState &st, TSNode curr, TSNode next, bool force_break) {
  if (ts_node_start_point(next).row > ts_node_end_point(curr).row) {
    if (force_break) {
      emit_force_break(st);
    } else {
      emit_line_break(st);
    }

    if (has_blank_line_between(curr, next)) {
      emit_blank_line(st);
    }
  } else {
    emit_space(st);
  }
}

// Toggle formatting state if node contains a 'prpfmt on' or 'prpfmt off' directive
void check_format_directives(std::string_view node_text, PrpfmtState &st) {
  std::string_view ptr = skip_leading_whitespace(node_text);

  if (ptr.starts_with("//")) {
    ptr = skip_leading_whitespace(ptr.substr(2));

    if (ptr.starts_with("prpfmt off")) {
      st.fmt_on = false;
    } else if (ptr.starts_with("prpfmt on")) {
      st.fmt_on = true;
    }
  }
}

/******************************************************************************
 * 1. Entry & High-Level Dispatch
 ******************************************************************************/

// A name's text without its backticks: only the SORT ORDER of named items
// uses it (`` `in` `` sorts as `in`). Name comparisons use name_identity: a
// backticked reserved word or type word (`` `U4` ``) is not the bare word.
static std::string_view unbacktick(std::string_view s) {
  if (s.size() >= 2 && s.front() == '`' && s.back() == '`') return s.substr(1, s.size() - 2);
  return s;
}

// Words that do not lex as an identifier: the keywords of grammar.js and of
// prpparse's lexer (prpparse/prp_keywords.def, which lhd uses). A backticked
// keyword keeps its backticks (tests/cli_test.py checks this list against
// both sources). `nil` is no keyword in either parser, but lhd reads the RAW
// text of an identifier: a bare `true`/`false`/`nil` is the literal, while
// `` `nil` `` is a plain name (prp2lnast identifier_to_node), so dropping the
// backticks would silently turn a variable into the nil literal.
//
// The old lowercase type names (`bool`, `unsigned`, `string`, `u8`, ...) are
// BANNED words (see is_banned_word) and the capitalized type words (`Bool`,
// `U8`, ...) are reserved (see is_type_word): both keep their backticks too.
// Matching is case-sensitive: only the exact spellings are reserved, so `IF`,
// `clock` and `reset` are ordinary names and print without backticks.
static bool is_reserved_word(std::string_view w) {
  static const std::unordered_set<std::string_view> words = {
      "and",     "as",      "break",  "case",   "comb",   "comptime", "const",  "continue", "does",   "elif",
      "else",    "enum",    "equals", "false",  "fluid",  "for",      "formal", "has",      "if",     "impl",
      "implies", "import",  "in",     "loop",   "match",  "mod",      "mut",    "nil",      "not",    "or",
      "pipe",    "pub",     "ref",    "reg",    "return", "sat",      "sext",   "stage",    "step",   "test",
      "tick",    "true",    "type",   "unique", "while",  "wire",     "wrap",   "zext"};
  return words.contains(w);
}

extern "C" const TSLanguage *tree_sitter_pyrope(void);

// Whether `w`, written bare, lexes as ONE identifier in the grammar: `x = w`
// parses with no error and its rvalue is an identifier spanning all of `w`.
// Only asked for words with non-ASCII bytes, where the grammar's \p{L}\p{Nd}
// classes decide (`é` and `café2` are names; `a·b`, a non-breaking space or an
// emoji are not). prpparse and lhd's canonical_escaped_ident treat every
// non-ASCII byte as a name byte, so they accept whatever this accepts.
static bool bare_word_is_identifier(std::string_view w) {
  static thread_local std::unordered_map<std::string, bool> cache;
  auto found = cache.find(std::string(w));
  if (found != cache.end()) return found->second;
  bool ok = false;
  std::string src = "x = " + std::string(w) + "\n";
  if (TSParser *parser = ts_parser_new()) {
    if (ts_parser_set_language(parser, tree_sitter_pyrope())) {
      if (TSTree *tree = ts_parser_parse_string(parser, NULL, src.data(), (uint32_t)src.size())) {
        TSNode root = ts_tree_root_node(tree);
        if (!ts_node_has_error(root) && ts_node_named_child_count(root) == 1) {
          TSNode rvalue = ts_node_child_by_field_name(ts_node_named_child(root, 0), "rvalue", 6);
          ok = !ts_node_is_null(rvalue) && std::string_view(ts_node_type(rvalue)) == "identifier" &&
               ts_node_start_byte(rvalue) == 4 && ts_node_end_byte(rvalue) == 4 + w.size();
        }
        ts_tree_delete(tree);
      }
    }
    ts_parser_delete(parser);
  }
  cache.emplace(std::string(w), ok);
  return ok;
}

// A built-in type word: `U` or `S` followed by ASCII digits only (`U4`,
// `S20`), or `Unsigned`, `Signed`, `Bool`, `String`, `Clock`, `Reset`. These
// are RESERVED in the grammar and in prpparse (token.hpp is_type_word): a bare
// `U4` is always the type, and `` `U4` `` is an ordinary name (never the type)
// in every position.
static bool is_type_word(std::string_view w) {
  if (w.size() >= 2 && (w[0] == 'U' || w[0] == 'S') &&
      std::all_of(w.begin() + 1, w.end(), [](char c) { return c >= '0' && c <= '9'; }))
    return true;
  return w == "Unsigned" || w == "Signed" || w == "Bool" || w == "String" || w == "Clock" || w == "Reset";
}

// A BANNED old spelling of a type word (spec 2026-09-29 §7; grammar.js
// BANNED_WORDS, prpparse "`u8` was renamed `U8`"): `u`, `s` or `i` followed by
// ASCII digits only (`u8`, `s20`, `i32`, `u0`), or `bool`, `boolean`,
// `unsigned`, `signed`, `string`. Bare, it is a syntax error in every position;
// backticked (`` `u8` ``) it is an ordinary name, so its backticks stay.
static bool is_banned_word(std::string_view w) {
  if (w.size() >= 2 && (w[0] == 'u' || w[0] == 's' || w[0] == 'i') &&
      std::all_of(w.begin() + 1, w.end(), [](char c) { return c >= '0' && c <= '9'; }))
    return true;
  return w == "bool" || w == "boolean" || w == "unsigned" || w == "signed" || w == "string";
}

// A reserved placeholder spelling (grammar.js `_reserved_placeholder_word`):
// `_` followed by a digit and then only letters/digits (`_0`, `_12`, `_1a`).
// Bare it is not a name; `` `_0` `` is an ordinary name, so its backticks stay.
// `_1_a` (a later underscore) is an ordinary name. Non-ASCII spellings are
// left to the grammar (bare_word_is_identifier).
static bool is_placeholder_word(std::string_view w) {
  auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
  return w.size() >= 2 && w[0] == '_' && w[1] >= '0' && w[1] <= '9' && std::all_of(w.begin() + 2, w.end(), alnum);
}

// `` `name` `` and `name` are the same identifier when the name is made only
// of identifier characters (letters, including non-ASCII letters such as `é`,
// digits and `_`), does not start with a digit and is no reserved word, so the
// backticks drop (`` `foo` `` prints `foo`, `` `é` `` prints `é`).
// The backticks stay when dropping them would change how the source lexes or
// what it means: a name with any other character (`` `foo[bar]` ``,
// `` `foo$bar` ``, `` `a b` ``, `` `x.y` ``), one that starts with a digit,
// `_` alone, a reserved placeholder (`` `_0` ``, `` `_12` ``), a keyword
// (`` `in` ``, `` `else` ``), a type word (`` `U4` `` is a variable, `U4` the
// type) or a banned old spelling (`` `u8` ``, `` `bool` ``, `` `string` ``:
// bare they are syntax errors). Words starting
// with `els`/`eli` (`` `elsewhere` ``) drop them too: the grammar scanner and
// prpparse (which lhd builds from this tree) match `else`/`elif` as whole
// words, so `elsewhere` at line start is a name that starts a statement.
static bool backticks_needed(std::string_view w) {
  auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  auto digit = [](char c) { return c >= '0' && c <= '9'; };
  auto high  = [](char c) { return static_cast<unsigned char>(c) >= 0x80; };
  if (w.empty() || !(alpha(w[0]) || w[0] == '_' || high(w[0]))) return true;
  if (w == "_") return true;
  bool non_ascii = false;
  for (char c : w) {
    if (high(c)) non_ascii = true;
    else if (!alpha(c) && !digit(c) && c != '_') return true;
  }
  // Reserved-word matching is CASE-SENSITIVE: only the exact spelling of a
  // keyword, a type word (`Clock`, `U8`) or a banned old type spelling (`u8`)
  // keeps its backticks. `clock`, `reset`, `IF` and `If` are ordinary names.
  if (is_reserved_word(w) || is_type_word(w) || is_banned_word(w) || is_placeholder_word(w)) return true;
  // Reserved placeholder spellings must retain their escapes too. Use the
  // grammar for both ASCII digit-led names and all Unicode names.
  return (non_ascii || (w.size() > 1 && w[0] == '_' && digit(w[1]))) && !bare_word_is_identifier(w);
}

// The identity of a name for comparisons (same-name shorthand, parameter
// lookup, duplicate keys): its text with droppable backticks removed, so
// `` `foo` `` and `foo` compare equal, while `` `U4` `` (a name) differs from
// `U4` (the type) and `` `nil` `` from `nil` (the literal).
static std::string_view name_identity(std::string_view text) {
  if (text.size() >= 2 && text.front() == '`' && text.back() == '`' && !backticks_needed(text.substr(1, text.size() - 2)))
    return text.substr(1, text.size() - 2);
  return text;
}

static std::string_view identifier_text(TSNode node, const PrpfmtState &st) {
  return name_identity(get_node_text(node, st.source_code));
}

// O(1) parent lookup (ts_node_parent is O(depth)); nodes outside the map,
// e.g. in a probe state, fall back to tree-sitter.
static TSNode parent_of(TSNode node, const PrpfmtState &st) {
  if (st.parents) {
    auto found = st.parents->find(NodeKey{node.id, ts_node_start_byte(node)});
    if (found != st.parents->end()) return found->second;
  }
  return ts_node_parent(node);
}

static void build_parent_map(TSNode root, ParentMap &parents) {
  TSTreeCursor cursor = ts_tree_cursor_new(root);
  std::vector<TSNode> stack{root};
  for (;;) {
    if (ts_tree_cursor_goto_first_child(&cursor)) {
      TSNode node = ts_tree_cursor_current_node(&cursor);
      if (ts_node_is_named(node)) parents.emplace(NodeKey{node.id, ts_node_start_byte(node)}, stack.back());
      stack.push_back(node);
      continue;
    }
    for (;;) {
      stack.pop_back();
      if (ts_tree_cursor_goto_next_sibling(&cursor)) {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        if (ts_node_is_named(node)) parents.emplace(NodeKey{node.id, ts_node_start_byte(node)}, stack.back());
        stack.push_back(node);
        break;
      }
      if (!ts_tree_cursor_goto_parent(&cursor)) {
        ts_tree_cursor_delete(&cursor);
        return;
      }
    }
  }
}

static bool is_callee_position(TSNode id, const PrpfmtState &st) {
  auto parent = parent_of(id, st);
  auto psym = grammar_symbol_of(parent);
  if (psym == sym_attribute_set) {
    // `f::[name=u](...)`: the callee under an instance-name attribute.
    auto arg = ts_node_child_by_field_name(parent, "argument", 8);
    if (ts_node_is_null(arg) || !ts_node_eq(arg, id)) return false;
    id = parent;
    parent = parent_of(parent, st);
    psym = grammar_symbol_of(parent);
  }
  if (psym != sym_function_call_expression) return false;
  auto fn = ts_node_child_by_field_name(parent, "function", 8);
  return !ts_node_is_null(fn) && ts_node_eq(fn, id);
}

// Record statement-level lambda declarations and every other occurrence of an
// identifier. A lambda name that appears anywhere except as its own
// declaration, as a direct callee `f(...)`, or as a named-argument key
// `g(f=...)` (an alias `const g = f`, an overload set `[f1, f2]`, a parameter
// or variable of the same name, a UFCS member, an import binding, ...) counts
// as another binding and makes the name unresolved.
static void collect_callees(TSNode node, PrpfmtState &st) {
  auto symbol = grammar_symbol_of(node);
  if (symbol == sym_function_call_expression) {
    // Record the unnamed arguments (see bound_by_position).
    auto fn = ts_node_child_by_field_name(node, "function", 8);
    if (!ts_node_is_null(fn) && grammar_symbol_of(fn) == sym_attribute_set)
      fn = ts_node_child_by_field_name(fn, "argument", 8);
    if (!ts_node_is_null(fn) && grammar_symbol_of(fn) == sym_dot_expression && ts_node_named_child_count(fn) > 0)
      fn = ts_node_named_child(fn, ts_node_named_child_count(fn) - 1);
    auto args = ts_node_child_by_field_name(node, "argument", 8);
    if (!ts_node_is_null(fn) && grammar_symbol_of(fn) == sym_identifier && !ts_node_is_null(args) &&
        grammar_symbol_of(args) == sym_arg_tuple) {
      auto &unnamed = st.calls->unnamed_args[std::string(name_identity(get_node_text(fn, st.source_code)))];
      for (uint32_t i = 0; i < ts_node_named_child_count(args); ++i) {
        auto item = ts_node_named_child(args, i);
        auto isym = grammar_symbol_of(item);
        if (isym == sym_comment || isym == sym_arg_assignment) continue;
        if (isym == sym_ref_identifier && ts_node_named_child_count(item) > 0) item = ts_node_named_child(item, 0);
        unnamed.push_back(grammar_symbol_of(item) == sym_identifier
                              ? std::string(name_identity(get_node_text(item, st.source_code)))
                              : std::string());
      }
    }
  }
  if (symbol == sym_identifier) {
    auto parent = parent_of(node, st);
    auto psym = grammar_symbol_of(parent);
    auto name = std::string(name_identity(get_node_text(node, st.source_code)));
    bool key = (psym == sym_arg_assignment || psym == sym_generic_assignment || psym == sym_attribute_assignment) &&
               ts_node_eq(ts_node_child_by_field_name(parent, "lvalue", 6), node);
    bool decl = false;
    if (psym == sym_lambda && ts_node_eq(ts_node_child_by_field_name(parent, "name", 4), node)) {
      auto gsym = grammar_symbol_of(parent_of(parent, st));
      decl = gsym == sym_description || gsym == sym_scope_statement;
      if (decl) {
        auto &sig = st.calls->callees[name];
        sig.decls++;
        sig.scope = parent_of(parent, st);
        sig.params.clear();
        for (uint32_t i = 0; i < ts_node_child_count(parent); ++i) {
          auto fdecl = ts_node_child(parent, i);
          if (grammar_symbol_of(fdecl) != sym_function_definition_decl) continue;
          auto input = ts_node_child_by_field_name(fdecl, "input", 5);
          for (uint32_t j = 0; !ts_node_is_null(input) && j < ts_node_child_count(input); ++j) {
            auto child = ts_node_child(input, j);
            if (grammar_symbol_of(child) == anon_sym_DOT_DOT_DOT) sig.plain = false;
            if (grammar_symbol_of(child) != sym_typed_identifier) continue;
            auto id = ts_node_child_by_field_name(child, "identifier", 10);
            auto param = std::string(name_identity(get_node_text(id, st.source_code)));
            if (sig.params.empty() && param == "self") sig.plain = false;
            sig.params.push_back(std::move(param));
          }
        }
      }
    }
    if (!decl && !key && !is_callee_position(node, st)) st.calls->other_bindings[name]++;
  }
  TSTreeCursor cursor = ts_tree_cursor_new(node);
  if (ts_tree_cursor_goto_first_child(&cursor)) {
    do collect_callees(ts_tree_cursor_current_node(&cursor), st);
    while (ts_tree_cursor_goto_next_sibling(&cursor));
  }
  ts_tree_cursor_delete(&cursor);
}

// The same-file lambda a direct call `f(...)` / `f<...>(...)` certainly
// reaches, or null: exactly one statement-level declaration whose block
// encloses the call, no `self` receiver, no `...` gather, and no other use of
// the name anywhere in the file.
static const PrpfmtState::CalleeSig *resolve_callee(TSNode call, const PrpfmtState &st) {
  if (ts_node_is_null(call) || grammar_symbol_of(call) != sym_function_call_expression) return nullptr;
  auto fn = ts_node_child_by_field_name(call, "function", 8);
  if (!ts_node_is_null(fn) && grammar_symbol_of(fn) == sym_attribute_set)
    fn = ts_node_child_by_field_name(fn, "argument", 8);
  if (ts_node_is_null(fn) || grammar_symbol_of(fn) != sym_identifier) return nullptr;
  auto name = std::string(name_identity(get_node_text(fn, st.source_code)));
  auto found = st.calls->callees.find(name);
  if (found == st.calls->callees.end() || found->second.decls != 1 || !found->second.plain) return nullptr;
  if (st.calls->other_bindings.contains(name)) return nullptr;
  // The declaring block encloses the call: nodes nest, so byte-range
  // containment is ancestry (and costs no parent walk).
  const TSNode scope = found->second.scope;
  if (ts_node_start_byte(scope) <= ts_node_start_byte(call) && ts_node_end_byte(call) <= ts_node_end_byte(scope))
    return &found->second;
  return nullptr;
}

static bool declares_param(const PrpfmtState::CalleeSig *sig, std::string_view name) {
  return sig && std::find(sig->params.begin(), sig->params.end(), name) != sig->params.end();
}

// Whether a caller may bind the input list `list` of a lambda by position, so
// the list keeps its order. Named tuples are unordered (owner ruling 105) and
// lhd rejects an unnamed argument, except a leftover unnamed `ref` one, which
// it still binds to the parameter at its position (`addby(ref m, by=2)`):
// sorting `addby(ref x:U8, by:U8)` would then bind `m` to `by`. Such a caller
// may be
// - in this file: a call passes the lambda (by name, also as the member
//   `x.f(...)`) an unnamed argument that is no same-name pun of one of the
//   list's parameters;
// - behind another name: the lambda's name appears elsewhere than in its
//   declaration and its direct calls (an alias `const g = f`, a type's `const
//   init = f`, a value argument), so those calls are not visible here;
// - in another file: a `pub` or tuple-member lambda with a `ref` parameter.
// The list keeps its order until lhd rejects such calls (ruling 64).
static bool bound_by_position(TSNode list, const PrpfmtState &st) {
  auto decl = parent_of(list, st);
  if (grammar_symbol_of(decl) != sym_function_definition_decl ||
      !ts_node_eq(ts_node_child_by_field_name(decl, "input", 5), list))
    return false;
  auto lam  = parent_of(decl, st);
  auto name = grammar_symbol_of(lam) == sym_lambda ? ts_node_child_by_field_name(lam, "name", 4) : TSNode{};
  if (ts_node_is_null(name) || grammar_symbol_of(name) != sym_identifier) return true;
  std::vector<std::string> params;
  bool has_ref = false;
  for (uint32_t i = 0; i < ts_node_child_count(list); ++i) {
    auto item = ts_node_child(list, i);
    if (grammar_symbol_of(item) == anon_sym_ref) has_ref = true;
    if (grammar_symbol_of(item) != sym_typed_identifier) continue;
    auto id = ts_node_child_by_field_name(item, "identifier", 10);
    if (!ts_node_is_null(id)) params.emplace_back(name_identity(get_node_text(id, st.source_code)));
  }
  // A statement-level declaration is no other binding of its name; a
  // tuple-member lambda's own name counts once (see collect_callees).
  auto scope     = grammar_symbol_of(parent_of(lam, st));
  bool statement = scope == sym_description || scope == sym_scope_statement;
  if (has_ref && (!statement || !ts_node_is_null(ts_node_child_by_field_name(lam, "pub", 3)))) return true;
  auto key   = std::string(name_identity(get_node_text(name, st.source_code)));
  auto other = st.calls->other_bindings.find(key);
  if (other != st.calls->other_bindings.end() && other->second > (statement ? 0 : 1)) return true;
  auto found = st.calls->unnamed_args.find(key);
  if (found == st.calls->unnamed_args.end()) return false;
  for (const auto &arg : found->second)
    if (arg.empty() || std::find(params.begin(), params.end(), arg) == params.end()) return true;
  return false;
}

// Top-level entry point for the formatter
// Manage file-level spacing, statement transitions, and vertical alignment groups
void print_description(TSTree *tree, PrpfmtState &st) {
  TSNode root_node = ts_tree_root_node(tree);
  ParentMap parents;
  build_parent_map(root_node, parents);
  st.parents = &parents;
  collect_callees(root_node, st);
  uint32_t root_child_count = ts_node_child_count(root_node);

  TSNode prev_child = {};
  bool in_align_group = false;
  bool printed = false;  // a statement or comment printed

  for (uint32_t i = 0; i < root_child_count; i++) {
    TSNode child = ts_node_child(root_node, i);
    // A `;` means a newline (owner ruling 100), so a run of them before the
    // first statement or comment prints nothing, not even a blank line.
    if (!printed && grammar_symbol_of(child) == anon_sym_SEMI) continue;
    printed = true;

    // Lookahead logic to determine if we should start/end a vertical alignment group
    bool current_alignable = is_alignable(child, st);
    bool next_alignable = false;
    bool blank_line_after = false;

    // A `;` terminator is transparent to alignment: it is usually dropped, and
    // `a = 1;` must align like `a = 1` (or the output is not idempotent).
    const bool is_semi = grammar_symbol_of(child) == anon_sym_SEMI || is_trailing_comment(child);
    uint32_t next_i = next_non_semicolon(root_node, i + 1);
    if (!is_semi && next_i < root_child_count) {
      TSNode next = ts_node_child(root_node, next_i);
      next_alignable = same_alignment_kind(child, next, st);
      blank_line_after = has_blank_line_between(child, next);
    }

    // Start new alignment group if multiple alignable items follow sequentially
    if (current_alignable && next_alignable && !blank_line_after && !in_align_group) {
      emit_align_group_start(st);
      in_align_group = true;
    }

    print__statement(child, st, prev_child, false);

    // Close alignment group if sequence of alignable items is broken
    if (in_align_group && !is_semi && (!next_alignable || blank_line_after)) {
      emit_align_group_end(st);
      in_align_group = false;
    }

    if (i + 1 < root_child_count) {
      TSNode next = ts_node_child(root_node, i + 1);
      if (st.fmt_on) {
        TSSymbol next_symbol = grammar_symbol_of(next);
        if (next_symbol == anon_sym_SEMI) {
          // `const x = 1;`: the terminator is glued (or dropped).
        } else if (next_symbol != sym_comment && ts_node_start_point(next).row == ts_node_end_point(child).row &&
                   (grammar_symbol_of(child) == anon_sym_SEMI ||
                    (ts_node_child_count(child) > 0 &&
                     grammar_symbol_of(ts_node_child(child, ts_node_child_count(child) - 1)) == anon_sym_SEMI))) {
          // (A declaration keeps its `;` inside: `mut x:U8; y = 1`.)
          // `const a = 1; const b = 2`: one statement per line.
          emit_force_break(st);
        } else {
          emit_vertical_transition(st, child, next, true);
        }
      }
    }
    prev_child = child;
  }
  // EOF: trailing newline (an empty, blank-only or `;`-only file prints nothing)
  if (printed) emit_line_break(st);
  st.parents = nullptr;
}


// Route a statement node to its specific formatting handler or print raw text if formatting is disabled
// Returns true if raw text was printed, false if the node was formatted
bool print__statement(TSNode node, PrpfmtState &st, TSNode prev_node, bool is_inline) {
  emit_group_start(st, false, false);
  TSSymbol symbol = grammar_symbol_of(node);
  bool was_fmt_on = st.fmt_on;

  // Check if comment is a format directive
  if (symbol == sym_comment) {
    check_format_directives(get_node_text(node, st.source_code), st);
  }

  // Check both to ensure that directives are printed as raw text
  if (!was_fmt_on || !st.fmt_on) {
    uint32_t start_byte = 0;

    // If we just entered raw mode, only print the current node's own text
    // Otherwise, use the end of the previous node to preserve raw internal spacing
    if (was_fmt_on) {
      start_byte = ts_node_start_byte(node);
    } else if (ts_node_is_null(prev_node)) {
      start_byte = ts_node_start_byte(node);
    } else {
      start_byte = ts_node_end_byte(prev_node);
    }

    uint32_t end_byte = ts_node_end_byte(node);
    uint32_t length = end_byte - start_byte;
    
    if (length > 0) {
      emit_token(st, st.source_code.substr(start_byte, length));
    }
    emit_group_end(st);

    // If comment, don't trigger a statement separator transition
    if (symbol == sym_comment) {
      return false;
    }
    return true;
  }

  // Comments handle their own transition logic
  // Format directives already prechecked
  if (symbol == sym_comment) {
    print_comment(node, st, true);
    emit_group_end(st);
    return false;
  }

  switch (symbol) {
    case anon_sym_wrap:
      emit_token(st, "wrap");
      emit_space(st);
      break;
    case anon_sym_sat:
      emit_token(st, "sat");
      emit_space(st);
      break;
    case sym_assignment:
      print_assignment(node, st, SPACE_BOTH);
      break;
    case sym_control_statement:
      print_control_statement(node, st);
      break;
    case sym_declaration_statement:
      print_declaration_statement(node, st);
      break;
    case sym_enum_assignment:
      print_enum_assignment(node, st);
      break;
    case sym_step_statement:
      print_step_statement(node, st);
      break;
    case sym_for_statement:
      print_for_statement(node, st);
      break;
    case sym_impl_statement:
      print_impl_statement(node, st);
      break;
    case sym_import_statement:
      print_import_statement(node, st);
      break;
    case sym_lambda:
      print_lambda(node, st);
      break;
    case sym_loop_statement:
      print_loop_statement(node, st);
      break;
    case sym_scope_statement:
      print_scope_statement(node, st, is_inline);
      break;
    case sym_test_statement:
      print_test_statement(node, st);
      break;
    case sym_formal_statement:
      print_formal_statement(node, st);
      break;
    case sym_tick_statement:
      print_tick_statement(node, st);
      break;
    case sym_type_statement:
      print_type_statement(node, st);
      break;
    case sym_while_statement:
      print_while_statement(node, st);
      break;
    case anon_sym_SEMI:
    case sym__automatic_semicolon:
      print__semicolon(node, st, SPACE_NONE);
      break;
    default:
      print__expression(node, st, is_inline);
      break;
  }

  emit_group_end(st);
  return false;
}

/******************************************************************************
 * 2. Structural (Scopes, Lists, Tuples)
 ******************************************************************************/

static bool prev_sym_is_lbrace(TSNode prev) {
  return !ts_node_is_null(prev) && grammar_symbol_of(prev) == anon_sym_LBRACE;
}

// A block comment right after a block's `{` that shares its source line with
// the first statement or value (`{ /* d */ r = a }`, `{ /* a */ /* b */ x`):
// it documents that item, so it opens the body line with it
// (`{` newline `/* d */ r = a`) instead of trailing the `{`, which would leave
// the block's content on the brace line.
static bool leads_block_item(TSNode scope, uint32_t i) {
  uint32_t n = ts_node_child_count(scope);
  TSNode prev = ts_node_child(scope, i);
  for (uint32_t k = i + 1; k < n; ++k) {
    TSNode next = ts_node_child(scope, k);
    if (ts_node_start_point(next).row != ts_node_end_point(prev).row) return false;
    auto symbol = grammar_symbol_of(next);
    if (symbol == anon_sym_RBRACE) return false;
    if (symbol != sym_comment) return true;
    prev = next;
  }
  return false;
}

// Format a scoped block: inline vs vertical layout, inner statement alignment, and transitions
void print_scope_statement(TSNode node, PrpfmtState &st, bool is_inline, bool value_block) {
  uint32_t child_count = ts_node_child_count(node);
  
  // Statements and declarations always use vertical blocks. Only expression
  // scopes may be compact; Human mode preserves a break after the opening brace.
  bool can_inline = is_inline && !scope_must_break(node, st);
  // Comments moved here from before an `elif`/`else` (print_if_expression)
  // trail this block's `{`.
  std::vector<TSNode> header_comments = std::move(st.header_comments);
  st.header_comments.clear();
  if (!header_comments.empty()) can_inline = false;
  std::vector<TSNode> tail_comments = std::move(st.tail_comments);
  st.tail_comments.clear();
  if (!tail_comments.empty()) can_inline = false;
  
  emit_group_start(st, false, !can_inline);
  emit_anchor_off(st);
  st.nesting_level++;
  
  TSNode prev_child = {};
  bool in_align_group = false;
  bool moved_header = false;  // header comments already trail the `{`

  // The lone value of an `if`/`match` branch (`{ (a) }`) is a whole value:
  // its grouping parentheses drop like those of a right-hand side.
  bool lone_value = false;
  if (TSNode parent = parent_of(node, st); !ts_node_is_null(parent)) {
    auto psym = grammar_symbol_of(parent);
    if (psym == sym_if_expression || psym == sym_match_expression) {
      int values = 0;
      for (uint32_t i = 0; i < child_count; i++) {
        auto c = ts_node_child(node, i);
        if (!ts_node_is_named(c) || grammar_symbol_of(c) == sym_comment) continue;
        ++values;
        lone_value = grammar_symbol_of(c) == sym_tuple;
      }
      if (values != 1) lone_value = false;
    }
  }

  // The value of an expression block (its last item, when that is no
  // statement) is laid out the same whether this block breaks or stays
  // inline: a nested `if`/`match` value does not take the statement layout
  // just because its enclosing block broke, so a second pass (which sees
  // the break after `{` in the source) prints it the same way.
  uint32_t value_index = child_count;
  if (is_inline || value_block) {
    for (uint32_t i = child_count; i-- > 0;) {
      auto c = ts_node_child(node, i);
      auto csym = grammar_symbol_of(c);
      if (!ts_node_is_named(c) || csym == sym_comment || csym == sym__automatic_semicolon) continue;
      if (!is_statement_symbol(csym)) value_index = i;
      break;
    }
  }

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    // Scope attribute prefix ('::' attribute_sq right after '{'): glue it to
    // the opening brace with no separator (`{::[...]`), then let the normal
    // transition logic break before the first statement.
    bool is_attr_prefix = (symbol == anon_sym_COLON_COLON || symbol == sym_attribute_sq);

    // Alignment logic for statements within scope
    bool current_alignable = is_alignable(child, st);
    bool next_alignable = false;
    bool blank_line_after = false;

    const bool is_semi = symbol == anon_sym_SEMI || is_trailing_comment(child);
    uint32_t next_i = next_non_semicolon(node, i + 1);
    if (!is_semi && next_i < child_count) {
      TSNode next = ts_node_child(node, next_i);
      next_alignable = same_alignment_kind(child, next, st);
      blank_line_after = has_blank_line_between(child, next);
    }

    if (!can_inline &&
        current_alignable &&
        next_alignable &&
        !blank_line_after &&
        !in_align_group) {
      emit_align_group_start(st);
      in_align_group = true;
    }

    // Comments that trailed this one-line branch's `}` end its last line.
    if (symbol == anon_sym_RBRACE && !tail_comments.empty()) {
      for (auto c : tail_comments) {
        emit_space(st);
        emit_align_comment(st, comment_text(c, st));
      }
      tail_comments.clear();
    }

    // Vertical transitions from previous node
    if (i > 0 && is_attr_prefix && !can_inline && grammar_symbol_of(prev_child) == anon_sym_LBRACE)
      emit_force_break(st);
    if (i > 0 && !is_attr_prefix) {
      TSSymbol prev_sym = grammar_symbol_of(prev_child);

      // Skip mandatory break if we are about to print a trailing comment
      bool has_trailing_comment = false;
      if (symbol == sym_comment &&
          ts_node_start_point(child).row == ts_node_end_point(prev_child).row) {
        has_trailing_comment = true;
      }
      // A comment on the `{` line trails the `{` unless comments moved from
      // the header already trail it (they came first in the source, so this
      // one follows them on the next line) or it leads the block's first item.
      if (has_trailing_comment && prev_sym == anon_sym_LBRACE && (moved_header || leads_block_item(node, i)))
        has_trailing_comment = false;

      // Apply correct transition (space, soft break, or newline) based on context between items
      if (st.fmt_on) {
        if (has_trailing_comment && prev_sym == anon_sym_LBRACE) {
          // `if c {  // note`: a comment trailing the `{` stays on the header
          // line; moved into the body it would document the first statement.
          emit_space(st);
        } else if (!can_inline && prev_sym == anon_sym_LBRACE) {
          emit_force_break(st);
        } else if (has_trailing_comment) {
          emit_space(st);
        } else if (is_block_comment(prev_child, st) && symbol != anon_sym_RBRACE && symbol != sym_comment &&
                   ts_node_start_point(child).row == ts_node_end_point(prev_child).row) {
          // `/* c */ x = 1`: a statement after a block comment on its line
          // stays there, as at the top level.
          emit_space(st);
        } else if (can_inline) {
          emit_break_point(st, 10);
        } else if (prev_sym == anon_sym_LBRACE ||
                   symbol == anon_sym_RBRACE) {
          emit_line_break(st);
        } else if (symbol != anon_sym_SEMI && symbol != sym__automatic_semicolon &&
                   prev_sym != anon_sym_wrap && prev_sym != anon_sym_sat) {
          emit_force_break(st);
          if (has_blank_line_between(prev_child, child)) emit_blank_line(st);
        }
      }
    }

    switch (symbol) {
      case anon_sym_LBRACE:
        emit_token(st, "{");
        emit_indent_inc(st);
        break;
      case anon_sym_RBRACE:
        emit_indent_dec(st);
        emit_token(st, "}");
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_sq:
        // Isolate in a non-propagating group so the exploded scope block does
        // not force a short attribute list onto its own lines; it wraps only
        // when it genuinely overflows the line width.
        emit_group_start(st, false, false);
        print_attribute_sq(child, st);
        emit_group_end(st);
        break;
      case sym_comment:
        // `{ /* c */ }` in a block that breaks: the comment ends the `{`
        // line, so it trails like it will on the next pass (it never counts
        // against the width of the header it follows).
        //
        // Likewise `x /* c */ }`: in a block that breaks the `}` gets its own
        // line, so the comment ends the last item's line and trails it (not
        // counting against its width) as on the next pass.
        if (!can_inline && prev_sym_is_lbrace(prev_child) &&
            ts_node_start_point(child).row == ts_node_end_point(prev_child).row && !moved_header &&
            !leads_block_item(node, i))
          print_comment_trailing(child, st, false);
        else if (!can_inline && i + 1 < child_count && i > 0 &&
                 grammar_symbol_of(ts_node_child(node, i + 1)) == anon_sym_RBRACE &&
                 ts_node_start_point(ts_node_child(node, i + 1)).row == ts_node_end_point(child).row &&
                 ts_node_start_point(child).row == ts_node_end_point(prev_child).row && !prev_sym_is_lbrace(prev_child))
          print_comment_trailing(child, st, false);
        else
          print_comment(child, st, false);
        break;
      default:
        print__statement(lone_value && symbol == sym_tuple ? redundant_grouping(child, Grouping::Whole) : child, st,
                         prev_child, i == value_index || can_inline);
        break;
    }

    // Moved header comments (before this `{`, or between the previous `}` and
    // `elif`/`else`): they come first in the source, so they come first here.
    // The first trails the `{`, the others open the body on their own lines,
    // and a comment the block itself has on its `{` line follows them.
    if (!header_comments.empty() && symbol == anon_sym_LBRACE) {
      for (size_t c = 0; c < header_comments.size(); ++c) {
        if (c == 0) {
          // It trails the `{` now: like any trailing comment it does not
          // count against the header's width (it did not on the next pass).
          emit_space(st);
          emit_align_comment(st, comment_text(header_comments[c], st));
          continue;
        }
        emit_force_break(st);
        emit_token(st, comment_text(header_comments[c], st));
      }
      header_comments.clear();
      moved_header = true;
    }

    if (in_align_group && !is_semi &&
        (!next_alignable || blank_line_after)) {
      emit_align_group_end(st);
      in_align_group = false;
    }

    prev_child = child;
  }
  st.nesting_level--;
  emit_group_end(st);
}

// Format a semicolon-separated list of items
// A `;` means a newline, so in a header (the init clause of an `if`,
// `while`, `for` or `match`, and a `loop`'s statements) a run of them is one
// separator: every `;` of a run after the first prints nothing.
static bool semicolon_run_tail(TSNode semi) {
  TSNode prev = ts_node_prev_sibling(semi);
  while (!ts_node_is_null(prev) && grammar_symbol_of(prev) == sym_comment) prev = ts_node_prev_sibling(prev);
  return !ts_node_is_null(prev) && grammar_symbol_of(prev) == anon_sym_SEMI;
}

// A header `;` run right before the block (`if c; {`, `loop a = 1; {`) means
// a newline before the `{`, so it prints nothing either (`if c {`).
static bool semicolon_before_block(TSNode semi) {
  TSNode next = ts_node_next_sibling(semi);
  while (!ts_node_is_null(next) &&
         (grammar_symbol_of(next) == anon_sym_SEMI || grammar_symbol_of(next) == sym_comment))
    next = ts_node_next_sibling(next);
  return !ts_node_is_null(next) &&
         (grammar_symbol_of(next) == sym_scope_statement || grammar_symbol_of(next) == anon_sym_LBRACE);
}

void print_stmt_list(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        if (!semicolon_run_tail(child)) print__semicolon(child, st, SPACE_AFTER);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__tuple_item(child, st, SPACE_NONE);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

// Optional breaks inside a selector or a single argument are never useful:
// choose the enclosing list's comma boundaries instead. Mandatory comment and
// statement breaks survive, including scopes inside tuple-valued arguments.
// keep_lists: a nested list (call arguments, tuple, generic or attribute
// list) inside a list item keeps its comma breaks, so a Human-mode item that
// does not fit splits its nested list one item per line (selectors pass
// false: they never split).
static void keep_expression_together(PrpfmtState &st, size_t begin, bool keep_lists = false) {
  for (size_t i = begin; i < st.buffer.size(); ++i) {
    auto &t = st.buffer[i];
    if (t.type == TOKEN_GROUP_START &&
        (t.chain_role == 1 || t.chain_role == 3 || t.exploded || (keep_lists && t.list))) {
      // An `if` chain keeps its own layout (continuation lines, then the
      // statement layout, which an inline chain may also take once a branch
      // must break), also as an argument or tuple-item value. So
      // does a nested list that must go one item per line (a comment or a
      // block member), with its trailing commas, and any nested list.
      // A closed group jumps to its end (a depth scan per nesting level made
      // `(a + (a + ...))` quadratic).
      if (t.match > (int)i) {
        i = t.match;
        continue;
      }
      int depth = 0;
      for (; i < st.buffer.size(); ++i) {
        if (st.buffer[i].type == TOKEN_GROUP_START) ++depth;
        else if (st.buffer[i].type == TOKEN_GROUP_END && --depth == 0) break;
      }
      continue;
    }
    if (t.type == TOKEN_BREAK_POINT) t.type = TOKEN_SPACE;
    else if (t.type == TOKEN_SOFT_BREAK || t.type == TOKEN_SOFT_SPACE || t.type == TOKEN_SOFT_TEXT) {
      t.type = TOKEN_TEXT;
      t.text.clear();
    }
  }
}

enum class ListStyle { Tuple, Parameters, Attributes, Generics };
struct ListEntry {
  std::vector<TSNode> nodes;
  size_t inline_from = SIZE_MAX;  // trailing block comments from here print inline
  std::string key;
  std::vector<TSNode> values;  // what must stay reorderable (a value, a type, a default)
};

// `U1(x)`, `S8(x)`, `Unsigned(x)`, `Bool(x)`, ...: a builtin conversion, pure
// like an operator. The callee is a bare type word, which nothing can rebind
// (a same-file lambda so named must be backticked, `` `U9` ``, and is an
// ordinary call).
static bool builtin_conversion(TSNode call, const PrpfmtState &st) {
  auto fn = ts_node_child_by_field_name(call, "function", 8);
  if (ts_node_is_null(fn) || grammar_symbol_of(fn) != sym_identifier) return false;
  return is_type_word(get_node_text(fn, st.source_code));
}

static bool is_statement_symbol(TSSymbol symbol) {
  switch (symbol) {
    case sym_assignment: case sym_declaration_statement: case sym_control_statement: case sym_lambda:
    case sym_for_statement: case sym_while_statement: case sym_loop_statement: case sym_enum_assignment:
    case sym_type_statement: case sym_import_statement: case sym_step_statement: case sym_tick_statement:
    case sym_test_statement: case sym_formal_statement: case sym_impl_statement: case anon_sym_wrap:
    case anon_sym_sat:
      return true;
    default:
      return false;
  }
}

// A value that may move relative to its neighbours: no calls with possible
// side effects, no refs, spreads, comments or nested bindings. Builtin
// conversions are pure, and so are `if`/`match` expressions whose branch
// blocks hold only such values. `keys` lists sibling field names a tuple
// value must not reference.
using KeySet = std::set<std::string, std::less<>>;
static bool reorderable_value(TSNode node, const PrpfmtState &st, const KeySet &keys,
                              bool in_branch = false) {
  auto symbol = grammar_symbol_of(node);
  // A generic value spells a call `g(x)` as a type-position call.
  if ((symbol == sym_function_call_expression || symbol == sym_function_call_type) && !builtin_conversion(node, st))
    return false;
  if (symbol == sym_if_expression || symbol == sym_match_expression) in_branch = true;
  if (symbol == sym_scope_statement) {
    if (!in_branch) return false;
    for (uint32_t i = 0; i < ts_node_child_count(node); ++i)
      if (is_statement_symbol(grammar_symbol_of(ts_node_child(node, i)))) return false;
  }
  // These may mutate, introduce bindings, or splice positional entries.
  if (symbol == sym_lambda || symbol == sym_ref_identifier || symbol == sym_comment || symbol == anon_sym_DOT_DOT_DOT)
    return false;
  if (symbol == sym_identifier && keys.contains(name_identity(get_node_text(node, st.source_code)))) return false;
  // Only a sibling's NAME references it: a field after `.` (`x.b`), an
  // attribute name (`x.[b]`) and a nested field or argument name (`(b=1)`,
  // `U8(b=1)`) do not.
  if (symbol == sym_attribute_list) return true;
  const bool dotted = symbol == sym_dot_expression;
  const bool binding = symbol == sym_assignment || symbol == sym_arg_assignment;
  for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
    if (dotted && i > 0) break;
    if (binding) {
      const char *field = ts_node_field_name_for_child(node, i);
      if (field && std::string_view(field) == "lvalue") continue;
    }
    if (!reorderable_value(ts_node_child(node, i), st, keys, in_branch)) return false;
  }
  return true;
}

// The name an all-named list entry sorts under (a null node when the entry is
// no single named item), and the parts that must stay reorderable: a binding
// `name=value` (also declared or typed: `const name=v`, `name:T=v`), a typed or
// declared field without a value (`name:T`, `mut name:T`), a parameter
// (`name:T`, `name:T = default`, `ref name`), or a bare argument naming a
// parameter of the resolved callee (`f(x)` for `f(x=x)`).
static TSNode sort_key_of(const std::vector<TSNode> &items, const PrpfmtState::CalleeSig *callee,
                          std::vector<TSNode> &values, const PrpfmtState &st) {
  auto typed_name = [&](TSNode n) -> TSNode {
    auto symbol = grammar_symbol_of(n);
    if (symbol == sym_identifier) return n;
    if (symbol != sym_typed_identifier && symbol != sym_typed_field) return TSNode{};
    auto type = ts_node_child_by_field_name(n, "type", 4);
    if (!ts_node_is_null(type)) values.push_back(type);
    return ts_node_child_by_field_name(n, "identifier", 10);
  };
  if (items.size() == 1) {
    auto item   = items[0];
    auto symbol = grammar_symbol_of(item);
    if (symbol == sym_assignment || symbol == sym_arg_assignment || symbol == sym_attribute_assignment ||
        symbol == sym_generic_assignment) {
      auto value = ts_node_child_by_field_name(item, "rvalue", 6);
      if (ts_node_is_null(value)) return TSNode{};
      values.push_back(value);
      return typed_name(ts_node_child_by_field_name(item, "lvalue", 6));
    }
    if (symbol == sym_identifier) {
      if (!declares_param(callee, name_identity(get_node_text(item, st.source_code)))) return TSNode{};
      values.push_back(item);
      return item;
    }
    if (symbol == sym_typed_field || symbol == sym_typed_identifier) return typed_name(item);
    return TSNode{};
  }
  // `ref name`, `mut name:T`: a leading kind.
  auto lead = grammar_symbol_of(items[0]);
  if (items.size() == 2 && (lead == anon_sym_ref || lead == sym_var_or_let_or_reg)) return typed_name(items[1]);
  // A parameter default: `name:T = default`.
  if (items.size() == 3 && grammar_symbol_of(items[1]) == anon_sym_EQ &&
      grammar_symbol_of(items[0]) == sym_typed_identifier) {
    values.push_back(items[2]);
    return typed_name(items[0]);
  }
  return TSNode{};
}

static bool has_recursive_comment(TSNode node, const PrpfmtState &st);
static bool has_recursive_comment(TSNode node) {
  if (grammar_symbol_of(node) == sym_comment) return true;
  for (uint32_t i = 0; i < ts_node_child_count(node); ++i)
    if (has_recursive_comment(ts_node_child(node, i))) return true;
  return false;
}


// `(e)` (no comma) is grouping, unlike the one-element tuple `(e,)`. Return
// `e` when the parentheses are redundant in position `where`, else `node`.
// Nested `((e))` unwraps fully or not at all. Parentheses that carry a
// comment, hold a tuple, a binding, a spread or a block/lambda/`if`/`match`
// stay. A `Logical` operand drops them only around an atom (identifier,
// selector, literal, call) or a comparison, which binds tighter than the
// logical operators; `not` binds tighter than a comparison, so its operand
// drops them only around an atom. Mixed operator tiers (`(a + b) * c`,
// `3 & (4*4)`, `(a == b) == c`) are never touched.
static TSNode redundant_grouping(TSNode node, Grouping where, std::string_view src) {
  if (keep_grouping > 0 || conservative_pass) return node;
  TSNode inner = node;
  while (grammar_symbol_of(inner) == sym_tuple) {
    // Memoized when a state is active: a scan per layer of `((((e))))` or
    // `(a + (a + ...))` made deep nesting quadratic.
    if (ts_node_child_count(inner) != 3 ||
        (active_state ? has_recursive_comment(inner, *active_state) : has_recursive_comment(inner)))
      return node;
    inner = ts_node_child(inner, 1);
    if (!ts_node_is_named(inner)) return node;
  }
  if (ts_node_eq(inner, node)) return node;
  auto symbol = grammar_symbol_of(inner);
  unwrap_hidden(inner, symbol);
  switch (symbol) {
    case sym_constant:
      // `a*(-1)`: a negative literal would glue to the operator.
      if (where == Grouping::Operand &&
          (src.empty() || get_node_text(inner, src).starts_with('-')))
        return node;
      return inner;
    case sym_identifier: case sym__complex_identifier: case sym_dot_expression: case sym_member_selection:
    case sym_bit_selection: case sym_attribute_read: case sym_timed_identifier:
    case sym_function_call_expression:
      return inner;
    case sym__binary_compare:
      return where == Grouping::Not || where == Grouping::Operand ? node : inner;
    case sym__binary_times: case sym__binary_other: case sym__binary_step: case sym__binary_logical:
    case sym_tuple_sq:  // a whole array-literal value: `([x, y])` is `[x, y]`
      return where == Grouping::Whole ? inner : node;
    case sym_unary_expression:
      if (where != Grouping::Whole || ts_node_child_count(inner) == 0 ||
          grammar_symbol_of(ts_node_child(inner, 0)) == anon_sym_DOT_DOT_DOT)
        return node;
      return inner;
    default:
      return node;
  }
}

// A generic value or generic parameter default is a type expression:
// `g<W=N + 1>` does not parse, so only an atom (identifier, dotted name,
// literal, also a negative one: `<N=(-3)>` becomes `<N=-3>`) drops its
// parentheses (`g<A=(M)>` becomes `g<A=M>`). A call (`<N=(k(a=2))>`, a bare
// call reads as a type) keeps them.
static TSNode redundant_type_grouping(TSNode type) {
  if (grammar_symbol_of(type) != sym_expression_type || ts_node_named_child_count(type) != 1) return type;
  auto tuple = ts_node_named_child(type, 0);
  if (grammar_symbol_of(tuple) != sym_tuple) return type;
  auto inner = redundant_grouping(tuple, Grouping::Whole);
  switch (grammar_symbol_of(inner)) {
    case sym_constant:
    case sym_identifier: case sym_dot_expression:
      return inner;
    default:
      return type;
  }
}

// A named tuple is unordered (owner ruling 105), so declarations sort too,
// except in two kinds of list that give their entries an order:
// - an `enum` member list: the order gives the members their values (`enum E
//   = (b, a)`), also for the nested lists of a hierarchical enum;
// - a type layout: a `type` body (with its member lambdas' parameter lists)
//   and a tuple type (`mut t:(b:U8, a:U8)`). lhd still builds an unnamed
//   tuple into a typed target in declaration order (`const t:T = (x, 7)`, a
//   constructor call `T(5, 9)` through an `init` member), so a sorted layout
//   would build a different value. Ruling 105 makes such a construction an
//   error; until lhd rejects it, a layout keeps its order.
// Memoized: each list asks, which was quadratic on nesting.
static bool in_ordered_list(TSNode node, const PrpfmtState &st) {
  auto symbol = grammar_symbol_of(node);
  if (symbol == sym_enum_assignment || symbol == sym_enum_definition || symbol == sym_type_statement) return true;
  if (symbol == sym_scope_statement || symbol == sym_description) return false;
  TSNode parent = parent_of(node, st);
  if (ts_node_is_null(parent)) return false;
  // A tuple type, not a type call's arguments (`:Signed(max=3, min=0)`).
  if (symbol == sym_tuple && grammar_symbol_of(parent) == sym_expression_type) return true;
  return cached_scan(node, scan_order_above, st, [&] { return in_ordered_list(parent, st); });
}

static bool is_block_comment(TSNode node, const PrpfmtState &st) {
  return grammar_symbol_of(node) == sym_comment &&
         !skip_leading_whitespace(get_node_text(node, st.source_code)).starts_with("//");
}

static void print_list_node(TSNode node, PrpfmtState &st, ListStyle style) {
  auto symbol = grammar_symbol_of(node);
  if (is_block_comment(node, st)) {
    // A block comment leading its item: `/* c */ item`, one space after.
    emit_token(st, get_node_text(node, st.source_code));
    emit_space(st);
  } else if (symbol == sym_comment) print_comment(node, st, false);
  else if (symbol == sym_attribute_assignment) print_attribute_assignment(node, st);
  else if (symbol == sym_arg_list) print_arg_list(node, st);
  else if (symbol == sym_generic_identifier) print_typed_identifier(node, st);
  else if (symbol == sym_lvalue_item) print_lvalue_item(node, st);
  else if (symbol == anon_sym_ref || symbol == anon_sym_const || symbol == anon_sym_mut ||
           symbol == anon_sym_reg || symbol == alias_sym_reg_decl) {
    emit_node_text(node, st);
    emit_space(st);
  } else if (style == ListStyle::Generics && symbol != sym_arg_assignment && symbol != sym_typed_identifier) {
    print__type(node, st);
  } else if (!ts_node_is_named(node)) emit_node_text(node, st);
  else print__tuple_item(node, st, SPACE_NONE);
}

// `a, /* c */ b`: a block comment followed by an item on its line documents
// that item, not the previous one.
static bool leads_next_item(TSNode comment, const PrpfmtState &st) {
  if (!is_block_comment(comment, st)) return false;
  auto next = ts_node_next_sibling(comment);
  while (!ts_node_is_null(next) && grammar_symbol_of(next) == sym_comment) next = ts_node_next_sibling(next);
  return !ts_node_is_null(next) && ts_node_is_named(next) &&
         ts_node_start_point(next).row == ts_node_end_point(comment).row;
}

// Comments between an undelimited list's brackets (a generic list's `<`
// `>`, a destructuring or `for` index list's `(` `)`) that are children of
// the list's parent (before its first item or after its trailing comma).
struct Bracket_comments {
  uint32_t open = UINT32_MAX, close = 0;  // child indices of `<` and `>`
  std::vector<TSNode> lead, tail;
  bool inside(uint32_t i) const { return open < i && i < close; }
};
static Bracket_comments generic_bracket_comments(TSNode parent, TSSymbol list_symbol,
                                                 TSSymbol open_symbol = anon_sym_LT,
                                                 TSSymbol close_symbol = anon_sym_GT) {
  Bracket_comments r;
  uint32_t count = ts_node_child_count(parent);
  bool seen_list = false;
  for (uint32_t i = 0; i < count; ++i) {
    auto child = ts_node_child(parent, i);
    auto symbol = grammar_symbol_of(child);
    if (symbol == open_symbol && r.open == UINT32_MAX) r.open = i;
    else if (symbol == list_symbol && r.open != UINT32_MAX) seen_list = true;
    else if (symbol == close_symbol && seen_list) {
      r.close = i;
      break;
    } else if (symbol == sym_comment && r.open != UINT32_MAX) (seen_list ? r.tail : r.lead).push_back(child);
  }
  if (r.close == 0) return Bracket_comments{};
  return r;
}

// A `<` (less) or `>` comparison somewhere in a subtree. Memoized: every
// list checks its whole subtree, which was quadratic on nested calls.
static bool has_angle_compare(TSNode node, const PrpfmtState &st, bool less) {
  if (grammar_symbol_of(node) == sym_binary_compare_op)
    return get_node_text(node, st.source_code) == (less ? "<" : ">");
  if (ts_node_child_count(node) == 0) return false;
  return cached_scan(node, less ? scan_lt : scan_gt, st, [&] {
    TSTreeCursor cursor = ts_tree_cursor_new(node);
    bool found = false;
    if (ts_tree_cursor_goto_first_child(&cursor)) {
      do found = has_angle_compare(ts_tree_cursor_current_node(&cursor), st, less);
      while (!found && ts_tree_cursor_goto_next_sibling(&cursor));
    }
    ts_tree_cursor_delete(&cursor);
    return found;
  });
}

// Any comment (`//` or `/* */`) in a subtree, memoized like has_angle_compare.
static bool has_recursive_comment(TSNode node, const PrpfmtState &st) {
  if (grammar_symbol_of(node) == sym_comment) return true;
  if (ts_node_child_count(node) == 0) return false;
  return cached_scan(node, scan_any_comment, st, [&] {
    TSTreeCursor cursor = ts_tree_cursor_new(node);
    bool found = false;
    if (ts_tree_cursor_goto_first_child(&cursor)) {
      do found = has_recursive_comment(ts_tree_cursor_current_node(&cursor), st);
      while (!found && ts_tree_cursor_goto_next_sibling(&cursor));
    }
    ts_tree_cursor_delete(&cursor);
    return found;
  });
}

// One group owns every separator, so overflowing lists explode one item per
// line. Delay separators until after trailing comments have been classified.
static void print_list(TSNode node, PrpfmtState &st, ListStyle style,
                       std::string_view open, std::string_view close, bool delimited = true,
                       bool type_call = false, const std::vector<TSNode> *lead = nullptr,
                       const std::vector<TSNode> *tail = nullptr) {
  std::vector<ListEntry> entries(1);
  bool comments = has_recursive_line_comment(node, st);
  // `lead`/`tail`: comments inside an undelimited list's brackets that the
  // grammar hangs on the parent (`g<` newline `// c` newline `W=1,` newline
  // `>`). They stay inside the list: before the first item, after the last.
  bool extra_comments = false;
  for (auto *extra : {lead, tail})
    if (extra)
      for (auto c : *extra) {
        extra_comments = true;
        if (!is_block_comment(c, st)) comments = true;
      }
  if (lead) entries.back().nodes.assign(lead->begin(), lead->end());
  TSNode comma{};
  uint32_t count = ts_node_child_count(node);
  // Children in order, and each item's index among them (ts_node_child and
  // ts_node_next_sibling are not O(1): scanning them per item was quadratic
  // on long lists).
  std::vector<TSNode> children;
  children.reserve(count);
  {
    TSTreeCursor cursor = ts_tree_cursor_new(node);
    if (ts_tree_cursor_goto_first_child(&cursor)) {
      do children.push_back(ts_tree_cursor_current_node(&cursor));
      while (ts_tree_cursor_goto_next_sibling(&cursor));
    }
    ts_tree_cursor_delete(&cursor);
  }
  std::unordered_map<NodeKey, uint32_t, NodeKeyHash> child_index;
  for (uint32_t i = 0; i < children.size(); ++i) child_index.emplace(NodeKey{children[i].id, ts_node_start_byte(children[i])}, i);
  for (uint32_t i = delimited ? 1 : 0; i < count - (delimited ? 1 : 0); ++i) {
    auto child = children[i];
    auto symbol = grammar_symbol_of(child);
    if (symbol == anon_sym_COMMA) {
      comma = child;
      // Comments alone are no item: in `(/* c */, 1,)` they lead the next item
      // instead of printing as an empty slot before a separator (which also
      // lost the one-element tuple's trailing comma).
      auto &nodes = entries.back().nodes;
      bool item = std::any_of(nodes.begin(), nodes.end(),
                              [](TSNode n) { return grammar_symbol_of(n) != sym_comment; });
      if (item) entries.emplace_back();
    } else if (symbol == sym_comment && entries.size() > 1 && entries.back().nodes.empty() &&
               !ts_node_is_null(comma) && ts_node_start_point(child).row == ts_node_end_point(comma).row &&
               !leads_next_item(child, st)) {
      entries[entries.size() - 2].nodes.push_back(child);
    } else entries.back().nodes.push_back(child);
  }
  // A comma after the last item opened an entry that stayed empty.
  if (entries.back().nodes.empty()) entries.pop_back();
  auto is_line_comment = [&](TSNode n) { return grammar_symbol_of(n) == sym_comment && !is_block_comment(n, st); };
  auto item_end = [](const std::vector<TSNode> &nodes) {
    size_t end = nodes.size();
    while (end > 0 && grammar_symbol_of(nodes[end - 1]) == sym_comment) --end;
    return end;
  };
  // A last entry holding only block comments (`b,` newline `/* c */` newline
  // `)`) is no item: they trail the previous item inline (`f(a, b /* c */)`),
  // as they do when written on its line (`f(a, b, /* c */)`), unless that
  // item ends with a line comment.
  if (entries.size() > 1 &&
      std::all_of(entries.back().nodes.begin(), entries.back().nodes.end(),
                  [&](TSNode n) { return is_block_comment(n, st); })) {
    auto &prev = entries[entries.size() - 2];
    if (prev.nodes.empty() || !is_line_comment(prev.nodes.back())) {
      prev.inline_from = prev.nodes.size();
      prev.nodes.insert(prev.nodes.end(), entries.back().nodes.begin(), entries.back().nodes.end());
      entries.pop_back();
    }
  }
  // A block comment after an item's trailing line comment (`a // x` newline
  // `/* c */ , b`) cannot stay on the item's line: it leads the next item
  // (`/* c */ b`), where the next pass finds it as well.
  for (size_t i = 0; i + 1 < entries.size(); ++i) {
    auto &nodes = entries[i].nodes;
    size_t k = item_end(nodes);
    while (k < nodes.size() && !is_line_comment(nodes[k])) ++k;
    std::vector<TSNode> moved;
    for (size_t n = k; n < nodes.size();) {
      if (is_block_comment(nodes[n], st)) {
        moved.push_back(nodes[n]);
        nodes.erase(nodes.begin() + n);
      } else ++n;
    }
    auto &next = entries[i + 1].nodes;
    next.insert(next.begin(), moved.begin(), moved.end());
  }
  if (tail && !entries.empty()) entries.back().nodes.insert(entries.back().nodes.end(), tail->begin(), tail->end());
  // `(x,)` is a one-element tuple; `(x)` is just `x` in parentheses. Both parse
  // as the same single-item `tuple` (the comma is an anonymous token), so the
  // comma alone carries the meaning and must survive formatting.
  // A one-item destructuring or `for` index list keeps it too: `(a,) = f()`.
  // Any comma makes it a tuple, wherever it sits: the leading-comma spelling
  // `(,x)` (or `(,,x,,)`) is the one-element tuple `(x,)`, not the grouping
  // `(x)`, so it prints with the trailing comma.
  auto own_symbol = grammar_symbol_of(node);
  bool keep_trailing_comma = !ts_node_is_null(comma) && entries.size() == 1 &&
                             (own_symbol == sym_tuple ||
                              (open == "(" && (own_symbol == sym_lvalue_list || own_symbol == sym_typed_identifier_list)));
  // The argument list of a type call (`Signed(bits=W + 1,)`) is no tuple
  // value: like a value call's, a trailing comma after one named item drops.
  // A single positional item (`Unsigned(8,)`) keeps it.
  if (keep_trailing_comma && type_call) {
    int items = 0;
    bool named = false;
    for (auto child : entries.front().nodes) {
      auto symbol = grammar_symbol_of(child);
      if (symbol == sym_comment) continue;
      ++items;
      named = symbol == sym_assignment || symbol == sym_arg_assignment;
    }
    if (items == 1 && named) keep_trailing_comma = false;
  }

  // Sort only all-named, independent bindings: a named tuple is unordered
  // (owner ruling 105), so call and tuple lists (also declaring ones),
  // parameter lists (a `self` stays first), call-site generic bindings `<W=M,
  // A=K>` and attribute lists sort. Spreads, refs, calls with possible side
  // effects, any comment, and cross-field dependencies retain their order, and
  // so do unnamed (positional) lists, generic parameter lists, `enum` members
  // and type layouts (see in_ordered_list), and a parameter list that a call
  // may bind by position (see bound_by_position).
  auto list_symbol = grammar_symbol_of(node);
  bool params = style == ListStyle::Parameters && list_symbol == sym_arg_list;
  bool call = list_symbol == sym_arg_tuple;
  // A generic list holding a `<` or `>` comparison keeps its source order and
  // every grouping parenthesis inside it (`g<N=(a > b)>`: a bare `>` would
  // close the list). Elsewhere the comparisons need no care: a `<` after a
  // blank never opens a generic list (owner ruling 107) and every comparison
  // is printed spaced, so `h(y=c > (d + 1), x=a < b)` sorts to `h(x=a < b,
  // y=c > d + 1)` and still reads as two comparisons.
  bool generic_hazard = style == ListStyle::Generics &&
                        (has_angle_compare(node, st, true) || has_angle_compare(node, st, false));
  bool sort = entries.size() > 1 && !extra_comments && !has_recursive_comment(node, st) && !generic_hazard &&
              !conservative_pass &&
              (style == ListStyle::Tuple || style == ListStyle::Attributes || params ||
               (style == ListStyle::Generics && list_symbol == sym_generic_type_list));
  // A bare `x` passed to a resolved same-file lambda that declares `x` is the
  // shorthand for `x=x`: it sorts under key `x` like its explicit spelling.
  const PrpfmtState::CalleeSig *callee = call ? resolve_callee(parent_of(node, st), st) : nullptr;
  // A `...` gather collects named arguments in call order, so sorting them
  // changes what the callee sees. Call arguments sort only when the call
  // certainly reaches a same-file lambda without a gather (resolve_callee)
  // or a builtin conversion; imported, UFCS and namespaced callees keep
  // source order, since their signature is not visible here.
  if (sort && call && !callee && !builtin_conversion(parent_of(node, st), st)) sort = false;
  if (sort && params && bound_by_position(node, st)) sort = false;
  KeySet keys;
  for (auto &entry : entries) {
    if (!sort) break;
    std::vector<TSNode> items;
    for (auto child : entry.nodes)
      if (grammar_symbol_of(child) != sym_comment) items.push_back(child);
    TSNode lhs = items.empty() ? TSNode{} : sort_key_of(items, callee, entry.values, st);
    if (ts_node_is_null(lhs) || grammar_symbol_of(lhs) != sym_identifier) {
      sort = false;
      break;
    }
    // The sort ORDER ignores backticks (`` `in` `` sorts as `in`); a repeated
    // name (by identity: `` `U4` `` is not `U4`) keeps the source order.
    entry.key = std::string(unbacktick(get_node_text(lhs, st.source_code)));
    if (!keys.insert(std::string(name_identity(get_node_text(lhs, st.source_code)))).second) sort = false;
  }
  // Checked last: it walks the ancestors.
  if (sort && ((style == ListStyle::Tuple && !call) || params) && in_ordered_list(node, st)) sort = false;
  // Tuple-literal values and parameter defaults may reference sibling names
  // (declare before use); call arguments, generic bindings and attribute
  // values are evaluated in the outer scope.
  const KeySet no_keys;
  bool literal = (style == ListStyle::Tuple && !call) || params;
  for (const auto &entry : entries)
    for (auto value : entry.values)
      if (sort && !reorderable_value(value, st, literal ? keys : no_keys)) sort = false;
  if (sort)
    std::stable_sort(entries.begin(), entries.end(), [params](const auto &a, const auto &b) {
      const bool a_self = params && a.key == "self", b_self = params && b.key == "self";
      if (a_self != b_self) return a_self;  // `self` stays the first parameter
      return a.key < b.key;
    });

  // A member lambda with a statement block (`comb read(self) -> (v:U8) {
  // ... }`), or a block item that must break, puts the list one item per line, so each header starts its own
  // line and the body indents one level below its item.
  // An item holding a `match` expression or a block that must break (an
  // `if` expression with such a branch included) counts the same.
  bool block_member = false;
  for (uint32_t i = 0; i < count && !block_member; ++i) {
    auto child = children[i];
    auto symbol = grammar_symbol_of(child);
    if (symbol == sym_comment || !ts_node_is_named(child)) continue;
    block_member = symbol == sym_match_expression || if_header_comment(child) ||
                   (symbol == sym_lambda && !ts_node_is_null(ts_node_child_by_field_name(child, "code", 4))) ||
                   (symbol == sym_scope_statement ? scope_must_break(child, st) : holds_vertical_layout(child, st));
  }
  if (generic_hazard) ++keep_grouping;
  emit_group_start(st, comments || block_member, false);
  st.buffer.back().list = true;
  if (style == ListStyle::Generics) st.buffer.back().yield_next = true;
  emit_anchor_off(st);
  emit_token(st, open);
  emit_indent_inc(st);
  if (!entries.empty()) emit_soft_break(st, 0);
  for (size_t i = 0; i < entries.size(); ++i) {
    auto &nodes = entries[i].nodes;
    size_t trailing = nodes.size();
    while (trailing > 0 && grammar_symbol_of(nodes[trailing - 1]) == sym_comment) --trailing;
    size_t start = st.buffer.size();
    for (size_t n = 0; n < trailing; ++n) {
      // A parameter default `a:U8=(3 + 1)` is a whole value.
      bool default_value = own_symbol == sym_arg_list && n > 0 && grammar_symbol_of(nodes[n - 1]) == anon_sym_EQ;
      print_list_node(default_value ? redundant_grouping(nodes[n], Grouping::Whole) : nodes[n], st, style);
    }
    keep_expression_together(st, start, true);
    // Block comments written before this item's comma stay before it:
    // `a=x /* c */, b=y`, not `a=x, /* c */ b=y`.
    size_t pre = trailing;
    TSNode sep{};
    if (trailing > 0) {
      // The first non-comment child of `node` after this item's last node
      // (ts_node_next_sibling walks down from the root: O(depth)).
      auto found = child_index.find(NodeKey{nodes[trailing - 1].id, ts_node_start_byte(nodes[trailing - 1])});
      for (uint32_t k = found == child_index.end() ? count : found->second + 1; k < count; ++k) {
        if (grammar_symbol_of(children[k]) != sym_comment) {
          sep = children[k];
          break;
        }
      }
      bool has_sep = !ts_node_is_null(sep) && grammar_symbol_of(sep) == anon_sym_COMMA;
      while (pre < nodes.size() && is_block_comment(nodes[pre], st) &&
             (!has_sep || ts_node_start_byte(nodes[pre]) < ts_node_start_byte(sep))) {
        emit_space(st);
        emit_token(st, get_node_text(nodes[pre], st.source_code));
        ++pre;
      }
    }
    if (i + 1 < entries.size() || (keep_trailing_comma && trailing > 0)) emit_token(st, ",");
    // One item per line: the last of several items ends with `,` as well, so
    // appending an item leaves the previous line alone. Inline lists drop it.
    else if (entries.size() > 1 && trailing > 0) emit_soft_text(st, ",");
    for (size_t n = pre; n < nodes.size(); ++n) {
      // A comment right after a comma on its line (the leading-comma
      // `a` newline `, /* c */` newline `b`) stays after the comma too.
      bool after_comma = false;
      for (uint32_t b = ts_node_start_byte(nodes[n]); b > 0; --b) {
        char c = st.source_code[b - 1];
        if (c == ' ' || c == '\t') continue;
        after_comma = c == ',';
        break;
      }
      if (n > 0 && (n >= entries[i].inline_from || after_comma ||
                    ts_node_start_point(nodes[n]).row == ts_node_end_point(nodes[n - 1]).row))
        emit_space(st);
      // A list holding only a block comment stays inline: `f(/* c */)`.
      else if (n == 0 && trailing == 0 && is_block_comment(nodes[n], st)) {}
      else emit_force_break(st);
      // A trailing block comment is followed by the separator or the closing
      // delimiter; the inline comment printer's trailing space would double
      // the gap (`/* c */  b`) or detach the delimiter (`/* c */ )`).
      if (is_block_comment(nodes[n], st)) emit_token(st, get_node_text(nodes[n], st.source_code));
      else print_comment(nodes[n], st, false);
    }
    if (i + 1 < entries.size()) emit_break_point(st, 0);
  }
  emit_indent_dec(st);
  if (!entries.empty()) emit_soft_break(st, 0);
  emit_token(st, close);
  emit_group_end(st);
  if (generic_hazard) --keep_grouping;
}

// `((e))`: the outer parentheses have no comma, so they only group the inner
// ones (a grouping or a tuple) and never change what `e` binds to.
static TSNode collapse_nested_grouping(TSNode node) {
  while (grammar_symbol_of(node) == sym_tuple && ts_node_child_count(node) == 3 &&
         grammar_symbol_of(ts_node_child(node, 0)) == anon_sym_LPAREN &&
         grammar_symbol_of(ts_node_child(node, 2)) == anon_sym_RPAREN &&
         grammar_symbol_of(ts_node_child(node, 1)) == sym_tuple)
    node = ts_node_child(node, 1);
  return node;
}

void print_tuple(TSNode node, PrpfmtState &st) {
  print_list(collapse_nested_grouping(node), st, ListStyle::Tuple, "(", ")");
}

void print_type_call_args(TSNode node, PrpfmtState &st) {
  print_list(node, st, ListStyle::Tuple, "(", ")", true, true);
}

void print_assertion_args(TSNode node, PrpfmtState &st) {
  print_tuple(node, st);
}

void print_tuple_sq(TSNode node, PrpfmtState &st) {
  print_list(node, st, ListStyle::Tuple, "[", "]");
}

void print_attribute_sq(TSNode node, PrpfmtState &st) {
  print_list(node, st, ListStyle::Attributes, "[", "]");
}

// Format an attribute assignment within an attribute list
void print_attribute_assignment(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_EQ:
        emit_token(st, "=");
        break;
      case sym_comment:
        // `[async // c` newline `=true]`: the comment keeps its line break.
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          // `[initial=(N + 1)]`: a whole attribute value needs no grouping.
          print__expression(redundant_grouping(child, Grouping::Whole), st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print__tuple_list(TSNode node, PrpfmtState &st, SpacingConfig spacing) {
  TSSymbol symbol = grammar_symbol_of(node);

  switch (symbol) {
    case anon_sym_COMMA:
      {
        TSNode prev = ts_node_prev_sibling(node);
        if (!ts_node_is_null(prev) &&
            ts_node_start_point(node).row > ts_node_end_point(prev).row) {
          // Leading comma: glue to the next item by omitting the break point
          emit_token(st, ",");
        } else {
          // Normal comma: add a break point after
          emit_token(st, ",");
          emit_break_point(st, 10);
        }
      }
      break;
    default:
      print__tuple_item(node, st, spacing);
      break;
  }
}

// `name = value` / `name = ref value` inside a call's argument tuple. The rvalue
// is routed back through print__tuple_item so a `ref x` rvalue reaches
// print_ref_identifier (which is what keeps the space after `ref`).
void print_arg_assignment(TSNode node, PrpfmtState &st, SpacingConfig spacing) {
  auto parent = parent_of(node, st);
  auto lhs = ts_node_child_by_field_name(node, "lvalue", 6);
  auto rhs = ts_node_child_by_field_name(node, "rvalue", 6);
  // `f(x=(a + b))`: parentheses around a whole named value are redundant.
  auto value = ts_node_is_null(rhs) ? rhs
               : grammar_symbol_of(rhs) == sym_expression_type ? redundant_type_grouping(rhs)
                                                                    : redundant_grouping(rhs, Grouping::Whole);
  // Same-name shorthand `f(x=x)` -> `f(x)`, only when the callee resolves to
  // one same-file lambda that declares parameter `x` (see resolve_callee);
  // otherwise a bare `x` could bind by position or type, or turn an unknown
  // argument error into valid code.
  if (grammar_symbol_of(parent) == sym_arg_tuple &&
      grammar_symbol_of(lhs) == sym_identifier && grammar_symbol_of(value) == sym_identifier &&
      !has_recursive_comment(node, st)) {
    auto name = name_identity(get_node_text(lhs, st.source_code));
    if (name == name_identity(get_node_text(value, st.source_code)) &&
        declares_param(resolve_callee(parent_of(parent, st), st), name)) {
      print_identifier(value, st);
      return;
    }
  }
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode   child  = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    switch (symbol) {
      case anon_sym_EQ:
        emit_token(st, "=");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        print__tuple_item(ts_node_eq(child, rhs) ? value : child, st, spacing);
        break;
    }
  }
}

// Dispatch tuple contents to proper handlers
void print__tuple_item(TSNode node, PrpfmtState &st, SpacingConfig spacing) {
  TSSymbol symbol = grammar_symbol_of(node);

  switch (symbol) {
    case sym_var_or_let_or_reg:
      print_var_or_let_or_reg(node, st);
      emit_space(st);
      break;
    case sym_ref_identifier:
      print_ref_identifier(node, st);
      break;
    // `f(reset_pin = ref reset)` — a named call argument. Without this case the
    // whole item went to print__expression, which has no ref_identifier arm, so
    // `ref reset` was emitted as the glued `refreset`: a different identifier,
    // silently, at exit 0.
    case sym_arg_assignment:
      print_arg_assignment(node, st, spacing);
      break;
    case sym_typed_identifier:
    case sym_typed_field:  // bare `name:type` tuple-TYPE field — same shape
      print_typed_identifier(node, st);
      break;
    case sym_assignment:
      print_assignment(node, st, spacing);
      break;
    case sym_lambda:
      print_lambda(node, st);
      break;
    default:
      print__expression(node, st, true);
      break;
  }
}

/******************************************************************************
 * 3. Control Flow
 ******************************************************************************/

// A comment between a branch's `}` and the next `elif`/`else` (`} // c`
// newline `elif`): it moves to the end of that header line, so the chain
// still prints `} elif ... {  // c`.
static bool precedes_else_branch(TSNode comment) {
  auto next = ts_node_next_sibling(comment);
  while (!ts_node_is_null(next) && grammar_symbol_of(next) == sym_comment) next = ts_node_next_sibling(next);
  if (ts_node_is_null(next)) return false;
  auto symbol = grammar_symbol_of(next);
  return symbol == anon_sym_elif || symbol == anon_sym_else;
}

// A comment between a branch or loop header and an Allman `{` (`} else`
// newline `// c` newline `{`): it moves after the ` {` like the comments
// before `elif`/`else`, so the header still ends with ` {`.
static bool precedes_block(TSNode comment) {
  auto next = ts_node_next_sibling(comment);
  while (!ts_node_is_null(next) && grammar_symbol_of(next) == sym_comment) next = ts_node_next_sibling(next);
  return !ts_node_is_null(next) && grammar_symbol_of(next) == sym_scope_statement;
}

// The `elif` at child `i` has a comment in its header, before its block
// (`elif /* c */ x {`, `elif x /* c */ == 1 {`); a comment right before the
// `{` does not count, it moves into the block after any others.
static bool elif_header_has_comment(TSNode node, uint32_t i, const PrpfmtState &st) {
  for (uint32_t j = i + 1; j < ts_node_child_count(node); ++j) {
    TSNode c = ts_node_child(node, j);
    auto symbol = grammar_symbol_of(c);
    if (symbol == sym_scope_statement) return false;
    if (symbol == sym_comment) {
      if (!precedes_block(c)) return true;
    } else if (has_recursive_comment(c, st)) {
      return true;
    }
  }
  return false;
}

void print_if_expression(TSNode node, PrpfmtState &st, bool is_inline) {
  bool old_allow = st.allow_inline;

  st.allow_inline = false;

  uint32_t child_count = ts_node_child_count(node);
  bool header_open = false;
  // One branch that must break (a comment, several statements) breaks them
  // all: `if c {` newline body newline `} elif d {` ..., never a mixed
  // `} elif d { a } else { b }` tail.
  bool branches_inline = is_inline;
  for (uint32_t i = 0; branches_inline && i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    if (symbol == sym_comment || (symbol == sym_scope_statement && scope_must_break(child, st)))
      branches_inline = false;
  }
  // Decide wrapping for the whole chain, independently of its enclosing
  // statement. An inline chain that does not fit (Human width) first puts
  // each `elif`/`else` branch on its own continuation line with its block
  // inline; when a branch still does not fit, the solver switches the whole
  // chain to the statement layout `} elif d {` / `} else {` (see
  // prpfmt_solve), never a `}` newline `elif` staircase.
  emit_group_start(st, false, false);
  st.buffer.back().chain_role = branches_inline ? 1 : 3;
  bool continuation_indent = false;
  std::vector<TSNode> deferred;
  std::vector<TSNode> tail_owned;  // comments printed inside the branch before them

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_unique:
        emit_token(st, "unique");
        emit_space(st);
        break;
      case anon_sym_if:
        emit_token(st, "if");
        emit_space(st);
        emit_anchor(st);
        emit_group_start(st, false, false); // Isolated Header
        st.buffer.back().yield_next = true;  // its block breaks first
        header_open = true;
        break;
      case anon_sym_elif:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_anchor_off(st);
        if (branches_inline && !continuation_indent) {
          emit_indent_inc(st);
          continuation_indent = true;
        }
        if (branches_inline) emit_break_point(st, 0); else emit_space(st);
        emit_token(st, "elif");
        emit_space(st);
        emit_anchor(st);
        emit_group_start(st, false, false); // Isolated Header
        st.buffer.back().yield_next = true;  // its block breaks first
        header_open = true;
        // Block comments before `elif` (`} /* c */ elif /* d */ x {`) move
        // into its block, past the condition: when the condition has comments
        // of its own that would reorder them, they print right after `elif`
        // instead (`} elif /* c */ /* d */ x {`), which keeps source order.
        if (st.fmt_on && !deferred.empty() && elif_header_has_comment(node, i, st) &&
            std::all_of(deferred.begin(), deferred.end(), [&](TSNode c) { return is_block_comment(c, st); })) {
          for (auto c : deferred) print_comment_inline(c, st, false);
          deferred.clear();
        }
        break;
      case anon_sym_else:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_anchor_off(st);
        if (branches_inline && !continuation_indent) {
          emit_indent_inc(st);
          continuation_indent = true;
        }
        if (branches_inline) emit_break_point(st, 0); else emit_space(st);
        emit_token(st, "else");
        break;
      case sym_stmt_list:
        // This is the promoted 'init' stmt_list
        print_stmt_list(child, st);
        break;
      case sym_scope_statement:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        st.header_comments = std::move(deferred);
        deferred.clear();
        // `if c { a } // c` newline `else`: a comment after the `}` of a
        // one-line branch documents that branch; it stays with its last
        // statement (`a // c`) instead of moving to the `else` header.
        // An empty one-line branch (`if en { } // c` newline `else`) has no
        // last statement: the comment trails its `{` instead (`if en { // c`
        // newline `} else {`), still documenting that branch.
        for (uint32_t j = i + 1; j < child_count; ++j) {
          TSNode c = ts_node_child(node, j);
          if (grammar_symbol_of(c) != sym_comment || !st.fmt_on || !precedes_else_branch(c) ||
              ts_node_start_point(child).row != ts_node_end_point(child).row ||
              ts_node_start_point(c).row != ts_node_end_point(child).row)
            break;
          TSNode after = ts_node_next_sibling(c);
          if (ts_node_is_null(after) || ts_node_start_point(after).row == ts_node_end_point(c).row) break;
          if (ts_node_named_child_count(child) == 0) st.header_comments.push_back(c);
          else st.tail_comments.push_back(c);
          tail_owned.push_back(c);
        }
        {
          size_t scope_group = st.buffer.size();
          print_scope_statement(child, st, branches_inline, is_inline);
          if (branches_inline && st.buffer[scope_group].type == TOKEN_GROUP_START)
            st.buffer[scope_group].chain_role = 2;
        }
        break;
      case anon_sym_SEMI:
      case sym__semicolon:
      case sym__automatic_semicolon:
        // Semicolon from the init clause or statement terminator
        if (semicolon_run_tail(child) || semicolon_before_block(child)) break;
        emit_token(st, ";");
        emit_space(st);
        break;
      case sym_comment:
        if (std::any_of(tail_owned.begin(), tail_owned.end(), [&](TSNode c) { return ts_node_eq(c, child); })) break;
        if (st.fmt_on && (precedes_else_branch(child) || precedes_block(child))) deferred.push_back(child);
        else print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "condition") == 0) {
          print__expression(redundant_grouping(child, Grouping::Whole), st, is_inline);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, is_inline);
        }
        break;
    }
  }

  if (header_open) {
    emit_group_end(st);
  }

  if (continuation_indent) {
    emit_indent_dec(st);
  }
  emit_anchor_off(st);
  emit_group_end(st);
  st.allow_inline = old_allow;
}

void print_match_expression(TSNode node, PrpfmtState &st, bool is_inline) {
  bool old_allow = st.allow_inline;
  st.allow_inline = false;

  emit_group_start(st, false, true);
  // Arms indent from the statement, not from an assignment's `=` anchor.
  emit_anchor_off(st);
  // Arms go one per line. In a value `match`, arm blocks stay inline
  // (`== 1 { a }`) unless one must break (a comment inside it, several
  // statements); then all of them break. A comment between arms (after the
  // header `{`, after an arm, or on its own line) is in no arm block: it
  // keeps its line and the arms stay inline.
  // A Human-mode source break after an arm's `{` breaks that arm only, like
  // an arm that does not fit (the next pass sees that break in its source).
  bool arms_inline = is_inline;
  for (uint32_t i = 0; arms_inline && i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    if (symbol == sym_scope_statement && (!is_inline_eligible(child, st) || holds_vertical_layout(child, st)))
      arms_inline = false;
  }
  uint32_t child_count = ts_node_child_count(node);
  bool seen_lbrace = false;
  bool arm_started = false;
  bool header_open = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_match:
        emit_token(st, "match");
        emit_space(st);
        emit_group_start(st, false, false); // Isolated Header
        header_open = true;
        break;
      case anon_sym_LBRACE:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_space(st);
        emit_token(st, "{");
        emit_indent_inc(st);
        st.nesting_level++;
        if (!has_trailing_comment(child, st)) {
          emit_line_break(st);
        }
        seen_lbrace = true;
        break;
      case anon_sym_RBRACE:
        st.nesting_level--;
        emit_indent_dec(st);
        emit_line_break(st);
        emit_token(st, "}");
        break;
      case sym_stmt_list:
        print_stmt_list(child, st);
        break;
      case sym_scope_statement:
        emit_space(st);
        print_scope_statement(child, st, arms_inline, is_inline);
        // The next arm starts on its own line; a same-line comment trails.
        if (i + 1 < child_count) {
          TSNode next = ts_node_child(node, i + 1);
          if (grammar_symbol_of(next) == sym_comment &&
              ts_node_start_point(next).row == ts_node_end_point(child).row) {
            emit_space(st);
          } else if (grammar_symbol_of(next) != anon_sym_RBRACE) {
            emit_force_break(st);
            if (has_blank_line_between(child, next)) emit_blank_line(st);
          }
        }
        arm_started = false;
        break;
      case anon_sym_else:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        ensure_match_arm_started(seen_lbrace, arm_started);
        emit_token(st, "else");
        emit_space(st);
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        // The init clause separator reads like `if`/`while`: `match const
        // t=(a=1); t {`.
        if (!seen_lbrace && (semicolon_run_tail(child) || semicolon_before_block(child))) break;
        print__semicolon(child, st, seen_lbrace ? SPACE_BOTH : SPACE_AFTER);
        break;
      case anon_sym_in:
      case anon_sym_has:
      case anon_sym_case:
      case anon_sym_equals:
      case anon_sym_does:
      case anon_sym_EQ_EQ:
      case anon_sym_BANG_EQ:
      case anon_sym_LT:
      case anon_sym_LT_EQ:
      case anon_sym_GT:
      case anon_sym_GT_EQ:
      case anon_sym_and:
      case anon_sym_or:
      case anon_sym_AMP:
      case anon_sym_CARET:
      case anon_sym_PIPE:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        ensure_match_arm_started(seen_lbrace, arm_started);
        emit_node_text(child, st);
        emit_space(st);
        break;
      case sym_comment: {
        // A comment after an arm's `}` on its line trails that arm; the next
        // arm still starts its own line (`== 2 { 5 } /* b */` newline `else`).
        TSNode prev = ts_node_prev_sibling(child);
        if (!ts_node_is_null(prev) && grammar_symbol_of(prev) == sym_scope_statement &&
            ts_node_start_point(child).row == ts_node_end_point(prev).row) {
          print_comment_trailing(child, st, false);
          TSNode next = ts_node_next_sibling(child);
          if (!ts_node_is_null(next) && grammar_symbol_of(next) != anon_sym_RBRACE &&
              !(grammar_symbol_of(next) == sym_comment &&
                ts_node_start_point(next).row == ts_node_end_point(child).row))
            emit_force_break(st);
        } else {
          print_comment(child, st, false);
          // A block comment that ends its line (after the header `{`, or on
          // a line of its own) keeps the next arm on its own line.
          TSNode next = ts_node_next_sibling(child);
          if (seen_lbrace && !ts_node_is_null(next) && ts_node_start_point(next).row > ts_node_end_point(child).row)
            emit_force_break(st);
        }
        break;
      }
      default:
        if (fn && strcmp(fn, "condition") == 0) {
          ensure_match_arm_started(seen_lbrace, arm_started);
          // An arm condition breaks only when it does not fit on its own,
          // not because the (always exploded) `match` around it did:
          // `in 4..<6 {` stays whole in Human mode. Its arm block breaks
          // first (only its `{` counts against the condition's line).
          emit_group_start(st, false, false);
          st.buffer.back().yield_next = true;
          print__expression(seen_lbrace ? child : redundant_grouping(child, Grouping::Whole), st, false);
          emit_group_end(st);
        } else if (ts_node_is_named(child)) {
          print__expression(child, st, false);
        } else {
          std::string_view text = get_node_text(child, st.source_code);
          bool is_comma = (text == ",");

          if (!is_comma) {
            ensure_match_arm_started(seen_lbrace, arm_started);
          }

          emit_token(st, text);
          // Add space after operators that aren't punctuation
          if (!is_comma &&
              text != "{" && text != "}" &&
              text != "(" && text != ")") {
            emit_space(st);
          }
        }
        break;
    }
  }
  if (header_open) {
    emit_group_end(st);
  }
  emit_group_end(st);
  st.allow_inline = old_allow;
}

void print_for_statement(TSNode node, PrpfmtState &st) {
  std::vector<TSNode> deferred;  // comments before an Allman `{`
  bool old_allow = st.allow_inline;
  st.allow_inline = false;

  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);
  bool has_init = false;
  bool header_open = false;

  // `(a, b)`: the list prints its own parentheses and the comments in them.
  auto parens = generic_bracket_comments(node, sym_typed_identifier_list, anon_sym_LPAREN, anon_sym_RPAREN);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (symbol == sym_comment && parens.inside(i)) continue;

    switch (symbol) {
      case anon_sym_for:
        emit_token(st, "for");
        emit_space(st);
        emit_anchor(st); // Anchor after keyword
        emit_group_start(st, false, false); // Isolated Header
        header_open = true;
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_sq:
        print_attribute_sq(child, st);
        break;
      case sym_stmt_list:
        emit_space(st);
        print_stmt_list(child, st);
        has_init = true;
        break;
      case anon_sym_SEMI:
      case sym__semicolon:
      case sym__automatic_semicolon:
        if (semicolon_run_tail(child) || semicolon_before_block(child)) break;
        if (fn && strcmp(fn, "init") == 0) {
          print__semicolon(child, st, SPACE_AFTER);
          has_init = true;
        } else {
          print__semicolon(child, st, SPACE_NONE);
        }
        break;
      case anon_sym_LPAREN:
        if (!has_init) {
          emit_space(st);
        }
        if (i != parens.open) emit_token(st, "(");
        break;
      case sym_typed_identifier_list: {
        size_t from = st.buffer.size();
        print_list(child, st, ListStyle::Parameters, "(", ")", false, false, &parens.lead, &parens.tail);
        yield_list_from(st, from);
        break;
      }
      case anon_sym_RPAREN:
        if (i != parens.close) emit_token(st, ")");
        break;
      case sym_typed_identifier:
        if (!has_init) {
          emit_space(st);
        }
        print_typed_identifier(child, st);
        break;
      case anon_sym_in:
        emit_space(st);
        emit_token(st, "in");
        emit_space(st);
        break;
      case sym_ref_identifier:
        print_ref_identifier(child, st);
        break;
      case sym_scope_statement:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        st.header_comments = std::move(deferred);
        deferred.clear();
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        if (st.fmt_on && precedes_block(child)) deferred.push_back(child);
        else print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "data") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  if (header_open) {
    emit_group_end(st);
  }
  emit_group_end(st);
  st.allow_inline = old_allow;
}

void print_while_statement(TSNode node, PrpfmtState &st) {
  std::vector<TSNode> deferred;  // comments before an Allman `{`
  bool old_allow = st.allow_inline;
  st.allow_inline = false;

  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);
  bool has_init = false;
  bool header_open = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_while:
        emit_token(st, "while");
        emit_space(st);
        emit_anchor(st); // Anchor after keyword
        emit_group_start(st, false, false); // Isolated Header
        header_open = true;
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_sq:
        print_attribute_sq(child, st);
        break;
      case sym_stmt_list:
        emit_space(st);
        print_stmt_list(child, st);
        has_init = true;
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        if (semicolon_run_tail(child) || semicolon_before_block(child)) break;
        if (fn && strcmp(fn, "init") == 0) {
          print__semicolon(child, st, SPACE_AFTER);
          has_init = true;
        } else {
          print__semicolon(child, st, SPACE_NONE);
        }
        break;
      case sym_scope_statement:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        st.header_comments = std::move(deferred);
        deferred.clear();
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        if (st.fmt_on && precedes_block(child)) deferred.push_back(child);
        else print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "condition") == 0) {
          if (!has_init) {
            emit_space(st);
          }
          print__expression(redundant_grouping(child, Grouping::Whole), st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        break;
    }
  }
  if (header_open) {
    emit_group_end(st);
  }
  emit_group_end(st);
  st.allow_inline = old_allow;
}

void print_loop_statement(TSNode node, PrpfmtState &st) {
  std::vector<TSNode> deferred;  // comments before an Allman `{`
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_loop:
        emit_token(st, "loop");
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_sq:
        print_attribute_sq(child, st);
        break;
      case sym_stmt_list:
        emit_space(st);
        print_stmt_list(child, st);
        break;
      case anon_sym_SEMI:
        // `loop a = 1; {`: a `;` run before the block means a newline.
        if (!semicolon_before_block(child)) emit_node_text(child, st);
        break;
      case sym_scope_statement:
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        st.header_comments = std::move(deferred);
        deferred.clear();
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        if (st.fmt_on && precedes_block(child)) deferred.push_back(child);
        else print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_control_statement(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    switch (symbol) {
      case sym_return_statement:
        print_return_statement(child, st);
        break;
      case sym_break_statement:
        print_break_statement(child, st);
        break;
      case sym_continue_statement:
        print_continue_statement(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        emit_node_text(child, st);
        break;
    }
  }
  emit_group_end(st);
}

void print_return_statement(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_return:
        emit_token(st, "return");
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          emit_space(st);
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_break_statement(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_break:
        emit_token(st, "break");
        break;
      case sym__semicolon:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_continue_statement(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_continue:
        emit_token(st, "continue");
        break;
      case sym__semicolon:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

/******************************************************************************
 * 4. Assignments & Declarations
 ******************************************************************************/

// Format an assignment statement, managing lvalues, alignment operators, and rvalues
// A repeated operand can be laid out as fixed syntax plus right-aligned fields.
// Use the normal printers to normalize the operand first; never pad source text
// directly, since its whitespace may already contain an earlier layout.
struct Aligned_part {
  std::string text;
  bool field = false;
};

static bool aligned_operand_parts(TSNode node, const PrpfmtState &st, std::vector<Aligned_part> &parts) {
  PrpfmtState probe{};
  probe.source_code = st.source_code;
  probe.indent_size = st.indent_size;
  probe.max_width = st.max_width;
  probe.fmt_on = true;
  probe.mode = st.mode;
  probe.parents = st.parents;
  print__expression(node, probe, true);

  std::vector<std::string> tokens;
  for (const auto &token : probe.buffer) {
    switch (token.type) {
      case TOKEN_TEXT:
      case TOKEN_ALIGN_OPERATOR:
      case TOKEN_ALIGN_RELATIONAL:
      case TOKEN_ALIGN_MATH:
        if (token.text.find('\n') != std::string::npos || token.text == "{" || token.text == "}") {
          return false;
        }
        tokens.push_back(token.text);
        break;
      case TOKEN_SPACE:
      case TOKEN_BREAK_POINT:
        tokens.emplace_back(" ");
        break;
      case TOKEN_NEWLINE:
      case TOKEN_FORCE_BREAK:
      case TOKEN_ALIGN_COMMENT:
        return false;
      case TOKEN_GROUP_START:
        if (token.exploded) {
          return false;
        }
        break;
      default:
        break;
    }
  }

  auto fixed = [&](const std::string &text) {
    if (parts.empty() || parts.back().field) {
      parts.push_back({text, false});
    } else {
      parts.back().text += text;
    }
  };
  bool has_selector = false;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i] == "[") {
      fixed(tokens[i]);
      std::string field;
      int depth = 1;
      while (++i < tokens.size()) {
        if (tokens[i] == "[") {
          ++depth;
        } else if (tokens[i] == "]" && --depth == 0) {
          break;
        }
        field += tokens[i];
      }
      if (depth != 0 || field.empty()) {
        return false;
      }
      parts.push_back({field, true});
      fixed("]");
      has_selector = true;
    } else if (!tokens[i].empty() && std::isdigit(static_cast<unsigned char>(tokens[i][0]))) {
      parts.push_back({tokens[i], true});
    } else {
      fixed(tokens[i]);
    }
  }
  return has_selector;
}

// Align a homogeneous assignment RHS only when its operands share the same
// syntax. Unrelated expressions, comments and blocks use ordinary wrapping.
static bool print_aligned_assignment_rhs(TSNode node, PrpfmtState &st) {
  if (st.mode == PRPFMT_AI) return false;
  TSSymbol symbol = grammar_symbol_of(node);
  unwrap_hidden(node, symbol);
  if (symbol != sym__binary_other || has_recursive_line_comment(node, st)) {
    return false;
  }

  std::string op;
  std::vector<TSNode> operands;
  for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
    TSNode child = ts_node_child(node, i);
    if (grammar_symbol_of(child) == sym_binary_other_op) {
      auto text = get_node_text(child, st.source_code);
      if (text.size() != 1 || std::string_view("|&^+-").find(text) == std::string_view::npos || (!op.empty() && op != text)) {
        return false;
      }
      op = text;
    } else {
      const char *field = ts_node_field_name_for_child(node, i);
      if (!field || std::strcmp(field, "operand") != 0) {
        return false;
      }
      operands.push_back(child);
    }
  }
  if (operands.size() < 3 || op.empty()) {
    return false;
  }
  std::vector<std::vector<Aligned_part>> rows;
  for (TSNode operand : operands) {
    rows.emplace_back();
    if (!aligned_operand_parts(operand, st, rows.back())) {
      return false;
    }
  }

  std::vector<size_t> widths(rows[0].size(), 0);
  for (const auto &row : rows) {
    if (row.size() != widths.size()) {
      return false;
    }
    for (size_t i = 0; i < row.size(); ++i) {
      if (row[i].field != rows[0][i].field || (!row[i].field && row[i].text != rows[0][i].text)) {
        return false;
      }
      widths[i] = std::max(widths[i], size_t(text_width(row[i].text)));
    }
  }
  for (const auto &row : rows) {
    for (size_t i = 0; i < row.size(); ++i) {
      // Avoid an isolated huge field stretching every other row.
      if (row[i].field && widths[i] - text_width(row[i].text) > 24) {
        return false;
      }
    }
  }

  // Keep these tokens in one group: padding is present only in the vertical
  // layout, and the inherited assignment anchor aligns the leading operators.
  emit_group_start(st, false, false);
  for (size_t r = 0; r < rows.size(); ++r) {
    if (r != 0) {
      emit_break_point(st, 0);
      emit_token(st, op);
      emit_space(st);
    }
    for (size_t i = 0; i < rows[r].size(); ++i) {
      if (rows[r][i].field) {
        for (size_t pad = text_width(rows[r][i].text); pad < widths[i]; ++pad) {
          emit_soft_space(st);
        }
      }
      emit_token(st, rows[r][i].text);
    }
  }
  emit_group_end(st);
  return true;
}

void print_assignment(TSNode node, PrpfmtState &st, SpacingConfig spacing) {
  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);
  bool continuation = false;  // see comment_continuation

  // `(a, b)`: the list prints its own parentheses and the comments in them.
  auto parens = generic_bracket_comments(node, sym_lvalue_list, anon_sym_LPAREN, anon_sym_RPAREN);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    if (symbol == sym_comment && parens.inside(i)) continue;

    switch (symbol) {
      case sym_var_or_let_or_reg:
        print_var_or_let_or_reg(child, st);
        break;
      case anon_sym_LPAREN:
        if (i != parens.open) emit_token(st, "(");
        break;
      case anon_sym_RPAREN:
        if (i != parens.close) emit_token(st, ")");
        break;
      case sym_lvalue_list: {
        size_t from = st.buffer.size();
        print_list(child, st, ListStyle::Parameters, "(", ")", false, false, &parens.lead, &parens.tail);
        yield_list_from(st, from);
        break;
      }
      case sym_typed_identifier:
      case sym__complex_identifier:
        print_lvalue_item(child, st);
        break;
      case sym_type_cast:
        print_type_cast(child, st);
        break;
      case sym_assignment_operator:
        {
          std::string_view op_text = get_node_text(child, st.source_code);
          if (spacing & SPACE_BEFORE) {
            emit_space(st);
          }
          if (spacing == SPACE_NONE) {
            emit_token(st, op_text);
          } else {
            emit_align_operator(st, op_text);
          }
          if (spacing & SPACE_AFTER) {
            emit_space(st);
          }
        }
        break;
      case sym_enum_definition:
        print_enum_definition(child, st);
        break;
      case sym_ref_identifier:
        print_ref_identifier(child, st);
        break;
      case sym_comment:
        comment_continuation(child, st, continuation, false);
        print_comment(child, st, false);
        comment_continuation(child, st, continuation, true);
        break;
      default:
        if (ts_node_is_named(child)) {
          // `x = (a + b)`: parentheses around a whole right-hand side (or
          // tuple-field value) are redundant.
          const char *fn = ts_node_field_name_for_child(node, i);
          if (fn && strcmp(fn, "rvalue") == 0) child = redundant_grouping(child, Grouping::Whole);
          // Repeated operand shapes can share a fully aligned vertical layout.
          if (spacing != SPACE_BOTH || !print_aligned_assignment_rhs(child, st)) {
            print__expression(child, st, true);
          }
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  end_comment_continuation(st, continuation);
  emit_group_end(st);
}

// Format an individual lvalue item, handling identifiers, selections, and type casts
void print_lvalue_item(TSNode node, PrpfmtState &st) {
  TSSymbol symbol = grammar_symbol_of(node);

  switch (symbol) {
    case sym_typed_identifier:
      print_typed_identifier(node, st);
      break;
    case sym_named_lvalue:
      print_named_lvalue(node, st);
      break;
    case sym_type_cast:
      print_type_cast(node, st);
      break;
    case sym_dot_expression:
    case sym_member_selection:
    case sym_bit_selection:
    case sym_attribute_read:
    case sym_timed_identifier:
    case sym_identifier:
      print__complex_identifier(node, st);
      break;
    default:
      if (ts_node_child_count(node) > 0) {
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
          TSNode child = ts_node_child(node, i);
          if (ts_node_is_named(child)) {
            print_lvalue_item(child, st);
          } else {
            emit_node_text(child, st);
          }
        }
      } else {
        emit_node_text(node, st);
      }
      break;
  }
}

// Format a comma-separated list of lvalues used in destructuring assignments.
// The parentheses belong to the parent statement; see print_paren_list.
void print_lvalue_list(TSNode node, PrpfmtState &st) {
  print_list(node, st, ListStyle::Parameters, "(", ")", false);
}

// Format a named lvalue within a destructuring assignment
void print_named_lvalue(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_EQ:
        // A destructuring list is a list: `(x=t.p1, y=t.p2) = f()`, like
        // `name=value` in a tuple.
        emit_token(st, "=");
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_dot_expression:
        print_dot_expression(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_declaration_statement(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  // `(a, b)`: the list prints its own parentheses and the comments in them.
  auto parens = generic_bracket_comments(node, sym_typed_identifier_list, anon_sym_LPAREN, anon_sym_RPAREN);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    if (symbol == sym_comment && parens.inside(i)) continue;

    switch (symbol) {
      case sym_var_or_let_or_reg:
        print_var_or_let_or_reg(child, st);
        emit_anchor(st);
        break;
      case anon_sym_LPAREN:
        if (i != parens.open) emit_token(st, "(");
        break;
      case anon_sym_RPAREN:
        if (i != parens.close) emit_token(st, ")");
        break;
      case sym_typed_identifier_list:
        print_list(child, st, ListStyle::Parameters, "(", ")", false, false, &parens.lead, &parens.tail);
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_anchor_off(st);
}

void print_stage_decl(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_stage:
        emit_token(st, "stage");
        break;
      case sym_timing_slot:
        print_timing_slot(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        break;
    }
  }
}

// Format an enum assignment (e.g., enum Color = (Red, Blue))
void print_enum_assignment(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_enum:
        emit_token(st, "enum");
        emit_space(st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      // `enum V6:Signed = (a, b, c)` — the grammar has
      // `field('type', optional($.type_cast))`, but with no case here the NAMED
      // node fell through the unnamed-only default arm and the annotation was
      // dropped at exit 0. That is a semantic loss: per the Pyrope rule an
      // integer-typed enum switches OFF one-hot numbering, so `enum V6:Signed`
      // and `enum V6` encode their variants differently (the same aliased-node
      // trap as `pub`/`wire` in print_lambda / print_var_or_let_or_reg).
      case sym_type_cast:
        print_type_cast(child, st);
        break;
      case anon_sym_EQ:
        emit_space(st);
        emit_token(st, "=");
        emit_space(st);
        break;
      case sym_tuple:
        print_tuple(child, st);
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

// Format an enum definition (e.g., enum(Red, Blue))
void print_enum_definition(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_enum:
        emit_token(st, "enum");
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_assignment_operator(TSNode node, PrpfmtState &st, SpacingConfig spacing) {
  emit_operator(node, st, spacing);
}

// `step`, `step 5`, `step(1000)`. With no printer at all the generic fallback
// emitted the children back to back, so `step 3` became the single identifier
// `step3` -- silently, at exit 0.
void print_step_statement(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode   child  = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    switch (symbol) {
      case anon_sym_step:
        emit_token(st, "step");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      case sym__semicolon:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        // `step;` newline: the line-ending `;` drops like any terminator.
        print__semicolon(child, st, SPACE_NONE);
        break;
      default:
        if (ts_node_is_named(child)) {
          // `step 5`: the count is a separate token; a parenthesized count
          // stays glued like a call (`step(3)`, also written `step (3)`).
          if (grammar_symbol_of(child) != sym_tuple) emit_space(st);
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

/******************************************************************************
 * 5. Functions & Parameters
 ******************************************************************************/

// Format a lambda expression, managing function types (comb/mod/pipe), names, and headers
void print_lambda(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      // `pub` is grammar-ALIASED (`alias('pub', $.pub_modifier)`), which makes the
      // node NAMED -- so the `default:` arm below, which only re-emits unnamed
      // nodes, dropped it silently and exit 0. That rewrote `pub comb f` as
      // `comb f`, i.e. it changed the module's visibility: with `-i` it is silent
      // source corruption. Every one of the 325 `pub`-declaring files in livehd's
      // Pyrope corpus lost its `pub` this way.
      case anon_sym_pub:
        emit_token(st, "pub");
        emit_space(st);
        break;
      case anon_sym_comb:
      case anon_sym_mod:
        emit_node_text(child, st);
        emit_space(st);
        break;
      case sym_pipe_lambda:
        print_pipe_lambda(child, st);
        emit_space(st);
        break;
      case sym_fluid_lambda:
        print_fluid_lambda(child, st);
        emit_space(st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_function_definition_decl:
        print_function_definition_decl(child, st);
        break;
      case sym_scope_statement:
        emit_space(st);
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

// A comment between `pipe`/`fluid` and its `[...]` latency slot keeps one
// space on each side (`pipe /* lat */ [1]`); a line comment ends its line and
// the slot continues one indent deeper (`pipe // lat` newline `[1] f(...)`),
// so the comment never swallows the header.
static void print_lambda_kind_comment(TSNode comment, PrpfmtState &st, bool &open) {
  comment_continuation(comment, st, open, false);
  if (skip_leading_whitespace(get_node_text(comment, st.source_code)).starts_with("//")) {
    print_comment(comment, st, false);
    emit_force_break(st);
  } else {
    print_comment_inline(comment, st, false);
  }
  comment_continuation(comment, st, open, true);
}

void print_pipe_lambda(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);
  bool open = false;
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    if (symbol == sym_select) {
      print_select(child, st);
    } else if (symbol == sym_comment) {
      print_lambda_kind_comment(child, st, open);
    } else {
      emit_node_text(child, st);
    }
  }
  end_comment_continuation(st, open);
}

// `fluid` (optionally with a `[...]` config, e.g. fluid[lat=1..=70]) -- the
// func_type of a fluid lambda. Keep `fluid` glued to its config bracket.
void print_fluid_lambda(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);
  bool open = false;
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    if (grammar_symbol_of(child) == sym_attribute_sq) {
      print_attribute_sq(child, st);
    } else if (grammar_symbol_of(child) == sym_comment) {
      print_lambda_kind_comment(child, st, open);
    } else {
      emit_node_text(child, st);
    }
  }
  end_comment_continuation(st, open);
}

void print_function_definition_decl(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);
  bool continuation_indent = false;

  // The generic parameter list is aliased to typed_identifier_list.
  auto brackets = generic_bracket_comments(node, sym_typed_identifier_list);
  if (brackets.close == 0) brackets = generic_bracket_comments(node, sym_generic_identifier_list);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    if (symbol == sym_comment && brackets.inside(i)) continue;  // printed inside `<...>`

    switch (symbol) {
      case anon_sym_LT:

        break;
      case anon_sym_GT:

        break;
      case sym_typed_identifier_list:
      case sym_generic_identifier_list:  // `<T, K=1>` (aliased to typed_identifier_list)
        print_list(child, st, ListStyle::Generics, "<", ">", false, false, &brackets.lead, &brackets.tail);
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_sq:
        print_attribute_sq(child, st);
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case anon_sym_DASH_GT:
        emit_space(st);
        emit_token(st, "->");
        emit_space(st);
        break;
      case sym_type_cast:
        print_type_cast(child, st);
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_comment:
        // An own-line comment before `-> (...)` continues the header, and so
        // does the `-> (...)` after a line comment (`comb f(a) // c` newline).
        if (!continuation_indent && starts_source_line(child, st)) {
          emit_indent_inc(st);
          continuation_indent = true;
        }
        print_comment(child, st, false);
        if (!continuation_indent && skip_leading_whitespace(get_node_text(child, st.source_code)).starts_with("//") &&
            !ts_node_is_null(ts_node_next_sibling(child))) {
          emit_indent_inc(st);
          continuation_indent = true;
        }
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  if (continuation_indent) emit_indent_dec(st);
  emit_group_end(st);
}

void print_arg_list(TSNode node, PrpfmtState &st) {
  print_list(node, st, ListStyle::Parameters, "(", ")");
}

void print_function_call_expression(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);
  bool old_in_assert = st.in_assert;

  // Check if this is an assertion call to enable vertical alignment
  TSNode func_name = ts_node_child(node, 0);
  if (!ts_node_is_null(func_name)) {
    std::string_view name = get_node_text(func_name, st.source_code);
    if (name == "cassert" || name == "assert" || name == "always") {
      st.in_assert = true;
    }
  }

  auto brackets = generic_bracket_comments(node, sym_generic_type_list);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (symbol == sym_comment && brackets.inside(i)) continue;  // printed inside `<...>`

    switch (symbol) {
      case sym_tuple:
      case sym_arg_tuple:  // call arguments parse as arg_tuple, same `( items )` shape
        if (st.in_assert) {
          print_assertion_args(child, st);
        } else {
          print_tuple(child, st);
        }
        break;
      case anon_sym_LT:
      case anon_sym_GT:
        break;
      case sym_generic_type_list:
        print_list(child, st, ListStyle::Generics, "<", ">", false, false, &brackets.lead, &brackets.tail);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      // Attributed callees are dispatched through print__complex_identifier,
      // including the same canonical attribute-list formatting as declarations.
      default:
        if (fn && strcmp(fn, "function") == 0) {
          print__complex_identifier(child, st);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__complex_identifier(child, st);
        }
        break;
    }
  }
  st.in_assert = old_in_assert;
}

/******************************************************************************
 * 6. Expressions & Selection
 ******************************************************************************/

/* 
 * Note: The print__pri1_operand through print__pri4_operand functions from 
 * previous versions have been omitted. These were empty proxies that simply 
 * called print__expression. In this unified architecture, tiered dispatchers 
 * call print__expression directly for all operand types to reduce redundant 
 * function calls and code clutter.
 */

// Central expression dispatcher: unwrap hidden nodes and route to specialized handlers
void print__expression(TSNode node, PrpfmtState &st, bool is_inline) {
  TSSymbol symbol = grammar_symbol_of(node);
  unwrap_hidden(node, symbol);

  switch (symbol) {
    case sym__binary_times:
      print__binary_times(node, st);
      break;
    case sym__binary_other:
      print__binary_other(node, st);
      break;
    case sym__binary_step:
      print__binary_step(node, st);
      break;
    case sym__binary_compare:
      print__binary_compare(node, st);
      break;
    case sym__binary_logical:
      print__binary_logical(node, st);
      break;
    case sym_attribute_read:
      print_attribute_read(node, st);
      break;
    case sym_dot_expression:
      print_dot_expression(node, st);
      break;
    case sym_if_expression:
      print_if_expression(node, st, is_inline);
      break;
    case sym_match_expression:
      print_match_expression(node, st, is_inline);
      break;
    case sym_bit_selection:
      print_bit_selection(node, st);
      break;
    case sym_member_selection:
      print_member_selection(node, st);
      break;
    case sym_unary_expression:
      print_unary_expression(node, st);
      break;
    case sym_ref_identifier:
      print_ref_identifier(node, st);
      break;
    case sym__complex_identifier:
      print__complex_identifier(node, st);
      break;
    case sym_constant:
      print_constant(node, st);
      break;
    case sym_function_call_expression:
      print_function_call_expression(node, st);
      break;
    case sym_lambda:
      print_lambda(node, st);
      break;
    case sym_paren_group:
      print_paren_group(node, st);
      break;
    case sym_tuple:
      print_tuple(node, st);
      break;
    case sym_scope_statement:
      print_scope_statement(node, st, is_inline);
      break;
    case sym__restricted_expression:
      print__restricted_expression(node, st);
      break;
    default:
      print__restricted_expression(node, st);
      break;
  }
}

// Format a restricted expression (no top-level binary operators)
void print__restricted_expression(TSNode node, PrpfmtState &st) {
  TSSymbol symbol = grammar_symbol_of(node);
  unwrap_hidden(node, symbol);

  switch (symbol) {
    case sym_comment:  // reached through a generic recursion below
      print_comment(node, st, false);
      break;
    case sym__complex_identifier:
    case sym_identifier:
    case sym_dot_expression:
    case sym_member_selection:
    case sym_bit_selection:
    case sym_attribute_read:
    case sym_timed_identifier:
    case sym_attribute_set:
      print__complex_identifier(node, st);
      break;
    case sym_constant:
      print_constant(node, st);
      break;
    case sym_function_call_expression:
      print_function_call_expression(node, st);
      break;
    case sym_lambda:
      print_lambda(node, st);
      break;
    case sym_tuple:
      print_tuple(node, st);
      break;
    case sym_tuple_sq:
      print_tuple_sq(node, st);
      break;
    default:
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print__restricted_expression(ts_node_child(node, i), st);
        }
      }
      break;
  }
}

void print_expression_item(TSNode node, PrpfmtState &st) {
  print__expression(node, st, true);
}

// The operand before operator child `i` ends with a block's `}` (`{ ... }`,
// an `if`/`match` expression): the operator stays after the `}` (`} + 1`),
// also when the block breaks in Human mode, instead of a line of its own.
static bool after_block(TSNode node, uint32_t i) {
  for (uint32_t k = i; k-- > 0;) {
    TSNode prev = ts_node_child(node, k);
    TSSymbol symbol = grammar_symbol_of(prev);
    if (symbol == sym_comment) return false;
    unwrap_hidden(prev, symbol);
    return symbol == sym_scope_statement || symbol == sym_if_expression || symbol == sym_match_expression;
  }
  return false;
}

// A comment among the operands of an operator chain. A block comment that
// trails an operand and has code after it (`+ x /* c */ + y`) stays inline and
// counts toward the width, also when a Human-mode break put the next operator
// on a new line: the chain breaks the same way on every pass.
// One that starts its source line before an operator keeps starting a line
// (`a` newline `/* c */ + b`), whether or not code follows it there.
static void print_operand_comment(TSNode comment, PrpfmtState &st) {
  bool block = get_node_text(comment, st.source_code).starts_with("/*");
  if (block && !ts_node_is_null(ts_node_next_sibling(comment))) {
    TSNode prev = ts_node_prev_sibling(comment);
    auto psym = ts_node_is_null(prev) ? TSSymbol(0) : grammar_symbol_of(prev);
    bool after_operator = psym == sym_binary_times_op || psym == sym_binary_other_op || psym == sym_binary_step_op ||
                          psym == sym_binary_compare_op || psym == sym_binary_logical_op;
    if (!after_operator && starts_source_line(comment, st) && mid_line(st)) emit_force_break(st);
    print_comment_inline(comment, st, false);
  } else {
    print_comment(comment, st, false);
  }
}

void print__binary_times(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, true); // Symmetrical unit
  // Continuations are relative to the surrounding indentation, never the
  // assignment or condition column inherited from the parent group.
  emit_anchor_off(st);
  bool continuation_indent = false;
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_binary_times_op:
        {
          if (!continuation_indent) {
            emit_indent_inc(st);
            continuation_indent = true;
          }
          std::string_view op_text = get_node_text(child, st.source_code);
          // Next to a block comment the operator is spaced like the comment
          // (`x / /* d */ (b + 1)`, never `x/ /* d */`).
          const bool by_comment =
              (i > 0 && grammar_symbol_of(ts_node_child(node, i - 1)) == sym_comment) ||
              (i + 1 < child_count && grammar_symbol_of(ts_node_child(node, i + 1)) == sym_comment);
          // A line starting with any of `*`, `/`, `%` continues the statement
          // (src/scanner.c, prpparse lexer.cpp), so the chain may break before
          // each of them. A continuation line reads `* c` / `/ c` / `% c` in
          // both modes: after a Human-mode wrap (the soft space) and after an
          // own-line comment (a forced break, also in AI mode); unbroken the
          // operator stays tight (`a*b`, `a/b`, `a%b`). Next to a comment `/`
          // and `%` keep their line (`/* q */ / b`).
          if ((op_text == "/" || op_text == "%") && by_comment) {
            emit_space(st);
            emit_token(st, op_text);
            emit_space(st);
            break;
          }
          bool line_start = !mid_line(st);
          bool block = after_block(node, i);  // `}*3`
          if (!block) emit_soft_break(st, 100);
          if (by_comment && !line_start && !block) emit_space(st);
          emit_token(st, op_text);
          if (line_start || by_comment) emit_space(st);
          else if (!block) emit_soft_space(st);
        }
        break;
      case sym_comment:
        // A comment before the operator already sits on the continuation.
        if (!continuation_indent) {
          emit_indent_inc(st);
          continuation_indent = true;
        }
        print_operand_comment(child, st);
        break;
      default:
        emit_group_start(st, false, false); // FIREWALL
        if (ts_node_is_named(child)) {
          print__expression(redundant_grouping(child, Grouping::Operand, st.source_code), st, true);
        } else {
          emit_node_text(child, st);
        }
        emit_group_end(st);
        break;
    }
  }
  if (continuation_indent) {
    emit_indent_dec(st);
  }
  emit_group_end(st);
}

// `..=`, `..<`, `..+`
static bool is_range_op(std::string_view op_text) {
  return op_text == "..=" || op_text == "..<" || op_text == "..+";
}

void print__binary_other(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, true); // Symmetrical unit
  // Continuations are relative to the surrounding indentation, never the
  // assignment or condition column inherited from the parent group.
  emit_anchor_off(st);
  bool continuation_indent = false;
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_binary_other_op:
        {
          if (!continuation_indent) {
            emit_indent_inc(st);
            continuation_indent = true;
          }
          std::string_view op_text = get_node_text(child, st.source_code);
          // Ranges print compact (`1..<n`); Pyrope rejects mixing them with
          // other priority-3 operators unless parenthesized.
          if (is_range_op(op_text)) {
            if (!after_block(node, i)) emit_soft_break(st, 50);
            emit_token(st, op_text);
          } else {
            if (after_block(node, i)) emit_space(st);
            else emit_break_point(st, 50);
            emit_token(st, op_text);
            emit_space(st);
          }
        }
        break;
      case sym_comment:
        // A comment before the operator already sits on the continuation.
        if (!continuation_indent) {
          emit_indent_inc(st);
          continuation_indent = true;
        }
        print_operand_comment(child, st);
        break;
      default:
        emit_group_start(st, false, false); // FIREWALL
        if (ts_node_is_named(child)) {
          print__expression(redundant_grouping(child, Grouping::Operand, st.source_code), st, true);
        } else {
          emit_node_text(child, st);
        }
        emit_group_end(st);
        break;
    }
  }
  if (continuation_indent) {
    emit_indent_dec(st);
  }
  emit_group_end(st);
}

// `(a..=b) step c` -- its own tier (looser than the range ops), same layout
// as _binary_other but with the `step` word operator.
void print__binary_step(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, true); // Symmetrical unit
  // Continuations are relative to the surrounding indentation, never the
  // assignment or condition column inherited from the parent group.
  emit_anchor_off(st);
  bool continuation_indent = false;
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_binary_step_op:
        {
          if (!continuation_indent) {
            emit_indent_inc(st);
            continuation_indent = true;
          }
          std::string_view op_text = get_node_text(child, st.source_code);
          // A leading `step` starts a simulation statement. Keep the range
          // operator attached to its left operand so re-parsing preserves it.
          emit_space(st);
          emit_token(st, op_text);
          emit_space(st);
        }
        break;
      case sym_comment:
        // A comment before the operator already sits on the continuation.
        if (!continuation_indent) {
          emit_indent_inc(st);
          continuation_indent = true;
        }
        print_operand_comment(child, st);
        break;
      default:
        emit_group_start(st, false, false); // FIREWALL
        if (ts_node_is_named(child)) {
          print__expression(redundant_grouping(child, Grouping::Operand, st.source_code), st, true);
        } else {
          emit_node_text(child, st);
        }
        emit_group_end(st);
        break;
    }
  }
  if (continuation_indent) {
    emit_indent_dec(st);
  }
  emit_group_end(st);
}

void print__binary_compare(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  // Continuations are relative to the surrounding indentation, never the
  // assignment or condition column inherited from the parent group.
  emit_anchor_off(st);
  bool continuation_indent = false;
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_binary_compare_op:
        {
          if (!continuation_indent) {
            emit_indent_inc(st);
            continuation_indent = true;
          }
          std::string_view op_text = get_node_text(child, st.source_code);
          // Every comparison word continues a statement from the start of a
          // line (`case` always does), so the chain may break before any of them.
          if (after_block(node, i)) emit_space(st);
          else emit_break_point(st, 30);
          if (st.in_assert && (op_text == "==" || op_text == "!=")) {
            emit_align_relational(st, op_text);
          } else {
            emit_token(st, op_text);
          }
          emit_space(st);
        }
        break;
      case sym_comment:
        // A comment before the operator already sits on the continuation.
        if (!continuation_indent) {
          emit_indent_inc(st);
          continuation_indent = true;
        }
        print_operand_comment(child, st);
        break;
      default:
        emit_group_start(st, false, false); // FIREWALL
        if (ts_node_is_named(child)) {
          // `a < (b) > (c)` becomes `a < b > c`: the spaced `<` is no generic
          // list (owner ruling 107).
          print__expression(redundant_grouping(child, Grouping::Operand, st.source_code), st, true);
        } else {
          emit_node_text(child, st);
        }
        emit_group_end(st);
        break;
    }
  }
  if (continuation_indent) {
    emit_indent_dec(st);
  }
  emit_group_end(st);
}

// Comparisons always keep the spaces around their comparators, also as
// operands of `and`/`or`/`implies` (`a == b or c != d`).
void print__binary_logical(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, true); // Symmetrical unit
  // Continuations are relative to the surrounding indentation, never the
  // assignment or condition column inherited from the parent group.
  emit_anchor_off(st);
  bool continuation_indent = false;
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_binary_logical_op:
        {
          if (!continuation_indent) {
            emit_indent_inc(st);
            continuation_indent = true;
          }
          std::string_view op_text = get_node_text(child, st.source_code);
          if (after_block(node, i)) emit_space(st);
          else emit_break_point(st, 20);
          emit_token(st, op_text);
          emit_space(st);
        }
        break;
      case sym_comment:
        // A comment before the operator already sits on the continuation.
        if (!continuation_indent) {
          emit_indent_inc(st);
          continuation_indent = true;
        }
        print_operand_comment(child, st);
        break;
      default:
        emit_group_start(st, false, false); // FIREWALL
        if (ts_node_is_named(child)) {
          // `(a == b) or (c)`: a comparison or an atom needs no grouping.
          print__expression(redundant_grouping(child, Grouping::Logical), st, true);
        } else {
          emit_node_text(child, st);
        }
        emit_group_end(st);
        break;
    }
  }
  if (continuation_indent) {
    emit_indent_dec(st);
  }
  emit_group_end(st);
}

void print_unary_expression(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);
  // `not (a)` -> `not a`, only around an atom (`not a == b` is `(not a) == b`).
  bool log_not = child_count > 0 && (grammar_symbol_of(ts_node_child(node, 0)) == anon_sym_not ||
                                     grammar_symbol_of(ts_node_child(node, 0)) == anon_sym_BANG);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_not:
        emit_token(st, "not");
        emit_space(st);
        break;
      case anon_sym_BANG:
        emit_token(st, "!");
        break;
      case anon_sym_TILDE:
        emit_token(st, "~");
        break;
      case anon_sym_DASH:
        emit_token(st, "-");
        break;
      case anon_sym_DOT_DOT_DOT:
        emit_token(st, "...");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__expression(log_not ? redundant_grouping(child, Grouping::Not) : child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_dot_expression(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);
  bool continuation = false;  // see comment_continuation

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_DOT:
        // Keep the member access intact, even beyond the soft width limit.
        emit_token(st, ".");
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_constant:
        print_constant(child, st);
        break;
      case sym_comment:
        comment_continuation(child, st, continuation, false);
        print_comment(child, st, false);
        comment_continuation(child, st, continuation, true);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__suffix_head(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  end_comment_continuation(st, continuation);
  emit_group_end(st);
}

void print_type_cast(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_COLON:
        emit_token(st, ":");
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_sq:
        print_attribute_sq(child, st);
        break;
      case sym_timing_slot:
        print_timing_slot(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__type(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

// Dotted test selector (`counter.foo.bar`): identifiers joined by `.` with no
// surrounding spaces. The `.` separators are anonymous tokens, emitted verbatim.
void print_test_name(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);  // the '.' separators
        }
        break;
    }
  }
}

void print_member_selection(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);
  bool continuation = false;  // see comment_continuation

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_select:
        print_select(child, st);
        break;
      case sym_comment:
        comment_continuation(child, st, continuation, false);
        print_comment(child, st, false);
        comment_continuation(child, st, continuation, true);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__suffix_head(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  end_comment_continuation(st, continuation);
}

void print_paren_group(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_LPAREN:
        emit_token(st, "(");
        break;
      case anon_sym_RPAREN:
        emit_token(st, ")");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_bit_selection(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);
  bool continuation = false;  // see comment_continuation

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_POUND:
        emit_token(st, "#");
        break;
      case sym_select:
        print_select(child, st);
        break;
      case alias_sym_reduction_or:
      case alias_sym_reduction_and:
      case alias_sym_reduction_xor:
        emit_node_text(child, st);
        break;
      case sym_comment:
        comment_continuation(child, st, continuation, false);
        print_comment(child, st, false);
        comment_continuation(child, st, continuation, true);
        break;
      default:
        if (ts_node_is_named(child)) {
          // This catches the 'head' argument
          print__suffix_head(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  end_comment_continuation(st, continuation);
}

// Format the head of a selector chain (e.g., the 'a' in a.b or (a+b).c)
void print__suffix_head(TSNode node, PrpfmtState &st) {
  TSSymbol symbol = grammar_symbol_of(node);
  unwrap_hidden(node, symbol);

  switch (symbol) {
    case sym_identifier:
    case sym_dot_expression:
    case sym_member_selection:
    case sym_bit_selection:
    case sym_attribute_read:
    case sym_timed_identifier:
    case sym__complex_identifier:
      print__complex_identifier(node, st);
      break;
    case sym_constant:
      print_constant(node, st);
      break;
    case sym_function_call_expression:
      print_function_call_expression(node, st);
      break;
    case sym_lambda:
      print_lambda(node, st);
      break;
    case sym_paren_group:
      print_paren_group(node, st);
      break;
    case sym_tuple:
      print_tuple(node, st);
      break;
    case sym_tuple_sq:
      print_tuple_sq(node, st);
      break;
    default:
      print__expression(node, st, true);
      break;
  }
}

void print_attribute_read(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);
  bool continuation = false;  // see comment_continuation

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_DOT:
        // Keep the member access intact, even beyond the soft width limit.
        emit_token(st, ".");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
      case sym_comment:
        comment_continuation(child, st, continuation, false);
        print_comment(child, st, false);
        comment_continuation(child, st, continuation, true);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__suffix_head(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  end_comment_continuation(st, continuation);
  emit_group_end(st);
}

void print_select(TSNode node, PrpfmtState &st) {
  size_t begin = st.buffer.size();
  uint32_t child_count = ts_node_child_count(node);
  bool continuation = false;  // see comment_continuation

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_LBRACK:
        emit_token(st, "[");
        break;
      case sym_selection_range:
        print_selection_range(child, st);
        break;
      case sym_comment:
        comment_continuation(child, st, continuation, false);
        print_comment(child, st, false);
        comment_continuation(child, st, continuation, true);
        break;
      case anon_sym_RBRACK:
        end_comment_continuation(st, continuation);
        continuation = false;
        emit_token(st, "]");
        break;
      default:
        if (ts_node_is_named(child)) {
          print__expression(redundant_grouping(child, Grouping::Operand, st.source_code), st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  end_comment_continuation(st, continuation);
  keep_expression_together(st, begin);
}

void print_selection_range(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t j = 0; j < child_count; j++) {
    TSNode child = ts_node_child(node, j);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case alias_sym_open_all:
        emit_token(st, "..");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

/******************************************************************************
 * 7. Types & Identifiers
 ******************************************************************************/

void print__type(TSNode node, PrpfmtState &st) {
  TSSymbol symbol = grammar_symbol_of(node);
  unwrap_hidden(node, symbol);

  switch (symbol) {
    case sym_uint_type:
    case sym_sint_type:
    case sym_bool_type:
    case sym_string_type:
    case sym_clock_type:
    case sym_reset_type:
    case sym__primitive_type:
      print__primitive_type(node, st);
      break;
    case sym_array_type:
      print_array_type(node, st);
      break;
    case sym_expression_type:
      print_expression_type(node, st);
      break;
    case sym_lambda_type:
      print_lambda_type(node, st);
      break;
    case sym_timed_identifier:
      print_timed_identifier(node, st);
      break;
    case sym_comment:
      // `g<W // c` newline `=1>`: a comment inside a type keeps its spacing
      // and its line break, or it would swallow the code after it.
      print_comment(node, st, false);
      break;
    case sym_timing_slot:
      print_timing_slot(node, st);
      break;
    case sym_generic_assignment:
      // `g<A=(M)>`: a generic binding value drops an atom's parentheses.
      for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        auto child = ts_node_child(node, i);
        if (grammar_symbol_of(child) == sym_comment) {
          print_comment(child, st, false);
          continue;
        }
        auto value = redundant_type_grouping(child);
        if (ts_node_eq(value, child)) print__type(child, st);
        else print__expression(value, st, true);
      }
      break;
    default:
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print__type(ts_node_child(node, i), st);
        }
      }
      break;
  }
}

// Body-less lambda SIGNATURE in type position:
// `call_method1: comb(a:U8, b:U3) -> (foo:U8, bar:U33)`. The func_type
// keyword joins its arg list directly (no space), unlike the
// `type X = comb (…)` statement form.
void print_lambda_type(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_comb:
      case anon_sym_mod:
        emit_node_text(child, st);
        break;
      case sym_pipe_lambda:
        print_pipe_lambda(child, st);
        break;
      case sym_fluid_lambda:
        print_fluid_lambda(child, st);
        break;
      case sym_function_definition_decl:
        print_function_definition_decl(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_expression_type(TSNode node, PrpfmtState &st) {
  TSSymbol symbol = grammar_symbol_of(node);

  switch (symbol) {
    case sym_comment:  // reached through a generic recursion below
      print_comment(node, st, false);
      break;
    case sym_identifier:
      print_identifier(node, st);
      break;
    case sym_constant:
      print_constant(node, st);
      break;
    case sym_if_expression:
      print_if_expression(node, st, true);
      break;
    case sym_match_expression:
      print_match_expression(node, st, true);
      break;
    case sym_dot_expression_type:
      print_dot_expression_type(node, st);
      break;
    case sym_function_call_type:
      print_function_call_type(node, st);
      break;
    case sym_tuple:
      print_tuple(node, st);
      break;
    default:
      if (ts_node_child_count(node) > 0) {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print_expression_type(ts_node_child(node, i), st);
        }
      } else {
        emit_node_text(node, st);
      }
      break;
  }
}

void print_dot_expression_type(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_DOT:
        // Keep the member access intact, even beyond the soft width limit.
        emit_token(st, ".");
        break;
      case sym_expression_type:
        print_expression_type(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print_expression_type(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_function_call_type(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_tuple:
        print_type_call_args(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__complex_identifier(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_array_type(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_array_length:
        print_array_length(child, st);
        break;
      case sym_lambda:
        print_lambda(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__type(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_array_length(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_LBRACK:
        emit_token(st, "[");
        break;
      case anon_sym_RBRACK:
        emit_token(st, "]");
        break;
      case sym_selection_range:
        print_selection_range(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print__primitive_type(TSNode node, PrpfmtState &st) {
  TSSymbol symbol = grammar_symbol_of(node);
  unwrap_hidden(node, symbol);

  switch (symbol) {
    case sym_comment:  // reached through a generic recursion below
      print_comment(node, st, false);
      break;
    case sym_uint_type:
      print_uint_type(node, st);
      break;
    case sym_sint_type:
      print_sint_type(node, st);
      break;
    case sym_string_type:
      print_string_type(node, st);
      break;
    case sym_bool_type:
    case sym_clock_type:
    case sym_reset_type:
      print_bool_type(node, st);  // a bare keyword
      break;
    default:
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print__primitive_type(ts_node_child(node, i), st);
        }
      }
      break;
  }
}

// Format a type statement, managing trait definitions and type aliases
void print_type_statement(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  auto brackets = generic_bracket_comments(node, sym_typed_identifier_list);
  if (brackets.close == 0) brackets = generic_bracket_comments(node, sym_generic_identifier_list);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (symbol == sym_comment && brackets.inside(i)) continue;  // printed inside `<...>`

    switch (symbol) {
      case anon_sym_pub:
        emit_token(st, "pub");
        emit_space(st);
        break;
      case anon_sym_type:
        emit_token(st, "type");
        emit_space(st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_LT:

        break;
      case anon_sym_GT:

        break;
      case sym_typed_identifier_list:
      case sym_generic_identifier_list:  // `type Name<T, K=1>` generic params
        print_list(child, st, ListStyle::Generics, "<", ">", false, false, &brackets.lead, &brackets.tail);
        break;
      case anon_sym_EQ:
        emit_space(st);
        emit_align_operator(st, "=");
        emit_space(st);
        break;
      case sym_tuple:
        emit_space(st);
        print_tuple(child, st);
        break;
      case anon_sym_comb:
      case anon_sym_mod:
        emit_node_text(child, st);
        emit_space(st);
        break;
      case sym_pipe_lambda:
        print_pipe_lambda(child, st);
        emit_space(st);
        break;
      case sym_function_definition_decl:
        print_function_definition_decl(child, st);
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && (strcmp(fn, "alias") == 0 || strcmp(fn, "type") == 0)) {
          print__type(child, st);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__type(child, st);
        }
        break;
    }
  }
}

void print_typed_identifier(TSNode node, PrpfmtState &st) {
  TSSymbol symbol = grammar_symbol_of(node);
  if (symbol == sym_identifier) {
    print_identifier(node, st);
    return;
  }
  uint32_t child_count = ts_node_child_count(node);
  // `mut d // c` newline `:U8 = 3`: the type after a trailing comment is a
  // continuation line, one indent level deeper (like `+ b` or `#[0]`).
  bool continuation = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol s2 = grammar_symbol_of(child);

    switch (s2) {
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_AT:
        emit_token(st, "@");
        break;
      case sym_timing_slot:
        print_timing_slot(child, st);
        break;
      case sym_type_cast:
        print_type_cast(child, st);
        break;
      // Generic-parameter default (`<T, K=1>`): the `=` plus a type-grammar
      // node (never a full expression — see grammar.js generic_identifier).
      case anon_sym_EQ:
        emit_token(st, "=");
        break;
      case sym_expression_type:
        // A generic parameter default `<A=(M)>` drops an atom's parentheses.
        if (auto value = redundant_type_grouping(child); !ts_node_eq(value, child)) {
          print__expression(value, st, true);
          break;
        }
        print__type(child, st);
        break;
      case sym_uint_type:
      case sym_sint_type:
      case sym_bool_type:
      case sym_string_type:
      case sym_clock_type:
      case sym_reset_type:
      case sym_array_type:
      case sym_lambda_type:
        print__type(child, st);
        break;
      case sym_comment:
        comment_continuation(child, st, continuation, false);
        print_comment(child, st, false);
        comment_continuation(child, st, continuation, true);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  end_comment_continuation(st, continuation);
}

void print_typed_identifier_list(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_typed_identifier:
      case sym_generic_identifier:  // `T`, `K=1` (aliased to typed_identifier)
        print_typed_identifier(child, st);
        break;
      case anon_sym_COMMA:
        emit_token(st, ",");
        emit_space(st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print_typed_identifier(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_ref_identifier(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_ref:
        emit_token(st, "ref");
        emit_space(st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__complex_identifier(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print__complex_identifier(TSNode node, PrpfmtState &st) {
  TSSymbol symbol = grammar_symbol_of(node);
  unwrap_hidden(node, symbol);

  switch (symbol) {
    case sym_comment:  // reached through a generic recursion below
      print_comment(node, st, false);
      break;
    case sym_identifier:
      print_identifier(node, st);
      break;
    case sym_dot_expression:
      print_dot_expression(node, st);
      break;
    case sym_member_selection:
      print_member_selection(node, st);
      break;
    case sym_bit_selection:
      print_bit_selection(node, st);
      break;
    case sym_attribute_read:
      print_attribute_read(node, st);
      break;
    case sym_timed_identifier:
      print_timed_identifier(node, st);
      break;
    case sym_attribute_set:
      for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
        auto child = ts_node_child(node, i);
        TSSymbol csym = grammar_symbol_of(child);
        if (csym == sym_attribute_sq) print_attribute_sq(child, st);
        else if (csym == sym_comment) print_comment(child, st, false);
        // The argument is any suffix head, e.g. `(a or b)`: print it as an
        // expression so its operators keep their spacing.
        else if (ts_node_is_named(child)) print__suffix_head(child, st);
        else emit_node_text(child, st);
      }
      break;
    default:
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print__complex_identifier(ts_node_child(node, i), st);
        }
      }
      break;
  }
}

void print_timed_identifier(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_AT:
        emit_token(st, "@");
        break;
      case sym_timing_slot:
        print_timing_slot(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_var_or_let_or_reg(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_comptime:
        emit_token(st, "comptime");
        emit_space(st);
        break;
      case anon_sym_const:
        emit_token(st, "const");
        emit_space(st);
        break;
      case anon_sym_mut:
        emit_token(st, "mut");
        emit_space(st);
        break;
      case anon_sym_reg:
      case alias_sym_reg_decl:
        emit_token(st, "reg");
        emit_space(st);
        break;
      // Same aliased-and-therefore-named trap as `pub` in print_lambda: `wire` is
      // `alias('wire', $.wire_decl)`, so it fell through `default:` and vanished.
      case anon_sym_wire:
        emit_token(st, "wire");
        emit_space(st);
        break;
      // ... and `fluid` is `alias('fluid', $.fluid_decl)`. Dropping it turned a
      // fluid HANDSHAKE declaration (`fluid mut req:Req`) into a plain one, which
      // is a different interface, at exit 0.
      case anon_sym_fluid:
      case alias_sym_fluid_decl:
        emit_token(st, "fluid");
        emit_space(st);
        break;
      case anon_sym_pub:
        emit_token(st, "pub");
        emit_space(st);
        break;
      case sym_stage_decl:
        print_stage_decl(child, st);
        emit_space(st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_identifier(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

// `U8`, `Unsigned`, `Signed(bits=N + 1)`: the keyword stays verbatim, and the
// optional constraint list formats like the arguments of a type call.
static void print_int_type(TSNode node, PrpfmtState &st) {
  TSNode constraint = ts_node_child_by_field_name(node, "constraint", 10);
  if (ts_node_is_null(constraint)) {
    emit_node_text(node, st);
    return;
  }
  // The `U8` keyword is a hidden regex token, so take it from the source. A
  // line comment between the keyword and `(` keeps the node verbatim; block
  // comments there print spaced like anywhere else (`Unsigned /* c */ (bits=4)`).
  uint32_t keyword_end = ts_node_start_byte(constraint);
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (grammar_symbol_of(child) != sym_comment) continue;
    if (!get_node_text(child, st.source_code).starts_with("/*")) {
      emit_node_text(node, st);
      return;
    }
    keyword_end = std::min(keyword_end, ts_node_start_byte(child));
  }
  std::string_view keyword = st.source_code.substr(ts_node_start_byte(node), keyword_end - ts_node_start_byte(node));
  while (!keyword.empty() && std::isspace(static_cast<unsigned char>(keyword.back()))) keyword.remove_suffix(1);
  emit_token(st, keyword);
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (grammar_symbol_of(child) != sym_comment) continue;
    // One that starts its source line keeps starting a line; the type
    // arguments follow it there (`Unsigned` newline `/* c */ (bits=W)`).
    if (st.fmt_on && starts_source_line(child, st) && mid_line(st)) emit_force_break(st);
    print_comment_inline(child, st, false);
  }
  // Past a comment, a `(` that starts its source line keeps starting one:
  // the grammar may also read that line as a statement of its own
  // (`mut a:Unsigned` newline `/* c */` newline `(bits=W) = 0`), which
  // prints the same way.
  if (st.fmt_on && keyword_end < ts_node_start_byte(constraint) && starts_source_line(constraint, st))
    emit_force_break(st);
  print_type_call_args(constraint, st);
}

void print_uint_type(TSNode node, PrpfmtState &st) {
  print_int_type(node, st);
}

void print_sint_type(TSNode node, PrpfmtState &st) {
  print_int_type(node, st);
}

void print_bool_type(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print_string_type(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

/******************************************************************************
 * 8. Literals & Numbers
 ******************************************************************************/

void print_constant(TSNode node, PrpfmtState &st) {
  TSSymbol symbol = grammar_symbol_of(node);

  switch (symbol) {
    case sym_comment:  // reached through a generic recursion below
      print_comment(node, st, false);
      break;
    case sym_integer_literal:
      print_integer_literal(node, st);
      break;
    case sym_bool_literal:
      print_bool_literal(node, st);
      break;
    case sym_unknown_literal:
      print_unknown_literal(node, st);
      break;
    case sym_string_literal:
      print_string_literal(node, st);
      break;
    case sym_interpolated_string_literal:
      print_interpolated_string_literal(node, st);
      break;
    default:
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
          print_constant(ts_node_child(node, i), st);
        }
      }
      break;
  }
}

// One spelling per number: the radix/sign prefix letters (`0X`, `0SB`, `0UB`,
// `0D`, `0O`) and hex digits print in lower case (`0XfF` -> `0xff`). The radix,
// leading zeros, `_` separators and `?` digits are kept as written, and a
// K/M/G/T magnitude (`2K`, upper case only in the grammar) is left verbatim.
void print_integer_literal(TSNode node, PrpfmtState &st) {
  std::string text(get_node_text(node, st.source_code));
  if (!text.empty() && std::string_view("KMGT").find(text.back()) != std::string_view::npos) {
    emit_node_text(node, st);
    return;
  }
  for (auto &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  emit_token(st, text);
}

void print__simple_number(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print__scaled_number(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print__hex_number(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print__decimal_number(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print__octal_number(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print__binary_number(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print__typed_number(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print_bool_literal(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print_unknown_literal(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print__string_literal(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

void print_string_literal(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

// A hole's expression printed on its own, like any expression (Q2 spacing
// `a == b`, sorted named arguments, dropped grouping), or nullopt when it
// would not fit one line (a block, a multi-line `if`).
static std::optional<std::string> format_hole_expression(TSNode expr, const PrpfmtState &st) {
  PrpfmtState sub = {
      .source_code   = st.source_code,
      .outfile       = NULL,
      .indent_size   = st.indent_size,
      .max_width     = 1 << 20,  // never wrap: a hole stays on its line
      .in_assert     = false,
      .allow_inline  = false,
      .nesting_level = 0,
      .fmt_on        = true,
      .inline_exp    = true,
      .buffer        = {},
      .mode          = st.mode,
  };
  sub.calls   = st.calls;  // shared, not copied: a file holds many holes
  sub.parents = st.parents;
  char  *buf = NULL;
  size_t sz  = 0;
  FILE  *mem = open_memstream(&buf, &sz);
  if (!mem) return std::nullopt;
  sub.outfile                     = mem;
  const PrpfmtState *outer_active = active_state;
  active_state                    = &sub;
  print__expression(expr, sub, true);
  active_state = outer_active;
  prpfmt_solve(sub);
  prpfmt_render(sub);
  fclose(mem);
  std::string out(buf, sz);
  free(buf);
  while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
  if (out.empty() || out.find('\n') != std::string::npos) return std::nullopt;
  return out;
}

// A double-quoted string: its text, escapes, braces and format specs print
// verbatim, and each `{expr}` / `{expr:spec}` hole's expression prints like any
// expression (`"{a==b}"` -> `"{a == b}"`, `"{ a }"` -> `"{a}"`). A hole holding
// a comment, or whose expression does not fit one line, stays verbatim.
void print_interpolated_string_literal(TSNode node, PrpfmtState &st) {
  const std::string_view src = st.source_code;
  std::string            out;
  uint32_t               pos   = ts_node_start_byte(node);
  const uint32_t         count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    TSNode expr = ts_node_child(node, i);
    if (!ts_node_is_named(expr) || grammar_symbol_of(expr) == sym_comment) continue;
    // The hole's `{` and `}` are the expression's neighbors unless a comment
    // sits between them (the format spec is a hidden token).
    TSNode open  = ts_node_prev_sibling(expr);
    TSNode close = ts_node_next_sibling(expr);
    if (ts_node_is_null(open) || ts_node_is_null(close) || ts_node_is_named(open) || ts_node_is_named(close) ||
        has_recursive_comment(expr, st))
      continue;
    auto formatted = format_hole_expression(expr, st);
    if (!formatted) continue;
    std::string_view spec = src.substr(ts_node_end_byte(expr), ts_node_start_byte(close) - ts_node_end_byte(expr));
    spec                  = skip_leading_whitespace(spec);  // `:spec` (verbatim) or nothing
    out.append(src.substr(pos, ts_node_end_byte(open) - pos));
    out += *formatted;
    out.append(spec);
    pos = ts_node_start_byte(close);
  }
  out.append(src.substr(pos, ts_node_end_byte(node) - pos));
  emit_token(st, out);
}

void print__format_spec(TSNode node, PrpfmtState &st) {
  emit_node_text(node, st);
}

/******************************************************************************
 * 9. Comments
 ******************************************************************************/

// A `//` comment's text without its trailing whitespace (spaces and tabs).
static std::string_view comment_text(TSNode node, const PrpfmtState &st) {
  std::string_view text = get_node_text(node, st.source_code);
  if (skip_leading_whitespace(text).starts_with("//"))
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
  return text;
}

void print_comment(TSNode node, PrpfmtState &st, bool is_prechecked) {
  TSNode prev = ts_node_prev_sibling(node);
  TSNode next = ts_node_next_sibling(node);

  bool at_start = ts_node_is_null(prev) || (ts_node_end_point(prev).row < ts_node_start_point(node).row);
  bool at_end = ts_node_is_null(next) || (ts_node_start_point(next).row > ts_node_end_point(node).row);

  // A block comment that ends its source line: code the caller keeps on its
  // output line reads `/* c */ x`, as on the next pass (where the comment no
  // longer ends its line). A break the caller emits drops that space.
  const bool block_then_code =
      !ts_node_is_null(next) && st.fmt_on && get_node_text(node, st.source_code).starts_with("/*");
  if (at_start && at_end) {
    print_comment_newline(node, st, is_prechecked);
    if (block_then_code) emit_space(st);
  } else if (at_end) {
    print_comment_trailing(node, st, is_prechecked);
    if (block_then_code) emit_space(st);
  } else if (at_start) {
    // A block comment that starts its source line, with code after it there
    // (`tick` newline `/* c */ N {`), keeps starting its line, as it does
    // when it was on a line of its own: the next pass prints it the same.
    if (st.fmt_on && starts_source_line(node, st) && mid_line(st)) emit_force_break(st);
    print_comment_inline(node, st, is_prechecked);
  } else {
    print_comment_inline(node, st, is_prechecked);
  }

  // INVARIANT: nothing may follow a `//` line comment on the same output line --
  // it would be COMMENTED OUT. Only print_comment_trailing enforced that, and
  // only for its own case, so a comment that reached the newline/inline paths with
  // a following sibling silently swallowed it. That is invisible to `--verify`
  // (the result still parses, just with fewer statements) and it needed two
  // passes to appear: pass 1 moved a trailing comment onto its own line, pass 2
  // then glued the next item back onto it. With `-i` that DELETES code.
  // A `/* … */` block comment is exempt -- code legitimately follows it inline.
  // emit_force_break() is itself a no-op when a break was just emitted, so the
  // trailing path's own break is not doubled.
  if (!ts_node_is_null(next) && skip_leading_whitespace(get_node_text(node, st.source_code)).starts_with("//")) {
    emit_force_break(st);
  }
}

void print_comment_inline(TSNode node, PrpfmtState &st, bool is_prechecked) {
  std::string_view node_text = comment_text(node, st);
  if (!is_prechecked) {
    check_format_directives(node_text, st);
  }
  emit_space(st);
  emit_token(st, node_text);
  emit_space(st);
}

void print_comment_trailing(TSNode node, PrpfmtState &st, bool is_prechecked) {
  std::string_view node_text = comment_text(node, st);
  if (!is_prechecked) {
    check_format_directives(node_text, st);
  }
  emit_space(st);
  emit_align_comment(st, node_text);

  // If it's a line comment and more code follows on a new line in source,
  // we MUST ensure a newline exists so the next token isn't swallowed.
  TSNode next = ts_node_next_sibling(node);

  if (!ts_node_is_null(next) &&
      skip_leading_whitespace(node_text).starts_with("//") &&
      ts_node_start_point(next).row > ts_node_end_point(node).row) {
    emit_line_break(st);
  }
}

// Only blanks precede `node` on its source line.
bool starts_source_line(TSNode node, const PrpfmtState &st) {
  for (uint32_t i = ts_node_start_byte(node); i > 0; --i) {
    char c = st.source_code[i - 1];
    if (c == '\n') return true;
    if (c != ' ' && c != '\t' && c != '\r') return false;
  }
  return true;
}

// Some text (not a line break) was already emitted on the current line.
static bool mid_line(const PrpfmtState &st) {
  for (auto it = st.buffer.rbegin(); it != st.buffer.rend(); ++it) {
    switch (it->type) {
      case TOKEN_NEWLINE: case TOKEN_FORCE_BREAK: case TOKEN_BREAK_POINT: case TOKEN_SOFT_BREAK:
        return false;
      case TOKEN_TEXT: case TOKEN_SPACE: case TOKEN_SOFT_TEXT: case TOKEN_ALIGN_OPERATOR:
      case TOKEN_ALIGN_RELATIONAL: case TOKEN_ALIGN_MATH: case TOKEN_ALIGN_COMMENT:
        if (it->type == TOKEN_TEXT && it->text.empty()) continue;
        return true;
      default:
        continue;
    }
  }
  return false;
}

static void comment_continuation(TSNode comment, PrpfmtState &st, bool &open, bool after) {
  if (open || !st.fmt_on) return;
  bool starts = !after && starts_source_line(comment, st) && mid_line(st);
  bool breaks = after && skip_leading_whitespace(get_node_text(comment, st.source_code)).starts_with("//") &&
                !ts_node_is_null(ts_node_next_sibling(comment));
  if (!starts && !breaks) return;
  emit_anchor_off(st);
  emit_indent_inc(st);
  emit_group_start(st, false, false);
  open = true;
}

static void end_comment_continuation(PrpfmtState &st, bool open) {
  if (!open) return;
  emit_group_end(st);
  emit_indent_dec(st);
}

void print_comment_newline(TSNode node, PrpfmtState &st, bool is_prechecked) {
  std::string_view node_text = comment_text(node, st);
  if (!is_prechecked) {
    check_format_directives(node_text, st);
  }
  // An own-line comment reached mid-expression (`a` newline `// c` newline
  // `or b`) keeps its own line; one that only looks own-line because it is
  // its node's first child (it trails code in the source) keeps one space.
  if (mid_line(st)) {
    if (starts_source_line(node, st)) emit_force_break(st);
    else emit_space(st);
  }
  emit_token(st, node_text);
}

/******************************************************************************
 * 10. Special Statements & Attributes
 ******************************************************************************/

void print_import_statement(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_import:
        emit_token(st, "import");
        emit_space(st);
        break;
      case anon_sym_as:
        emit_space(st);
        emit_token(st, "as");
        emit_space(st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_string_literal:
      case sym_interpolated_string_literal:
      case sym__string_literal:
        print__string_literal(child, st);
        break;
      case anon_sym_DOT:
        emit_token(st, ".");
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_impl_statement(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_impl:
        emit_token(st, "impl");
        emit_space(st);
        break;
      case anon_sym_for:
        emit_space(st);
        emit_token(st, "for");
        emit_space(st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_tuple:
        emit_space(st);
        print_tuple(child, st);
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print_identifier(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

// `test name.path [(params)] { ... }`. The dotted name and the optional
// parameter `arg_list` (a lambda-style input list, no return) bind tightly:
// `test counter.foo(max_cycles:U32 = 10000) {`. The body is spaced off like
// any other block.
void print_test_statement(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_test:
        emit_token(st, "test");
        emit_space(st);
        break;
      case sym_test_name:
        print_test_name(child, st);
        break;
      case sym_arg_list:
        print_arg_list(child, st);  // (params) attached to the name, no space
        break;
      case sym_scope_statement:
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

// `formal name.path { ... }` — a test-shaped declarative verification block,
// but with no parameter list. The name is `formal_name` in the tree, an alias
// of `test_name`, so its grammar symbol is still sym_test_name here.
void print_formal_statement(TSNode node, PrpfmtState &st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_formal:
        emit_token(st, "formal");
        emit_space(st);
        break;
      case sym_test_name:
        print_test_name(child, st);
        break;
      case sym_scope_statement:
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

// `tick [N] { ... }` — cycle-driven test loop (mirrors loop_statement). The
// optional count is an expression child (field `value`); the body is `code`.
void print_tick_statement(TSNode node, PrpfmtState &st) {
  std::vector<TSNode> deferred;  // comments before an Allman `{`
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_tick:
        emit_token(st, "tick");
        break;
      case sym_scope_statement:
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        st.header_comments = std::move(deferred);
        deferred.clear();
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        if (st.fmt_on && precedes_block(child)) deferred.push_back(child);
        else print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          emit_space(st);  // the cycle count: `tick 4 {`
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_attribute_list(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_LBRACK:
        emit_token(st, "[");
        break;
      case anon_sym_RBRACK:
        emit_token(st, "]");
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        emit_node_text(child, st);
        break;
    }
  }
}

// A statement-terminating `;` is redundant when the statement ends its line:
// the newline inserts the same (virtual) terminator. The scanner (see
// src/scanner.c) suppresses that virtual terminator when the next token
// continues an expression (`, = . < > & ^ | * / + -`, a type annotation or
// attribute `:`, a bit selector `#`, `!=`, a binary word operator, the whole
// word `else`/`elif`), so a `;` before such a token stays, as does one ending
// a body-less lambda before a `{` (that `{` would become the lambda's body). A
// line starting with `(` or `[` is always a new statement (even after a
// declaration's type: `mut x:U8` newline `(r, x) = (b, a)`), so a `;` before
// one drops. A block comment right after the `;` on its line also keeps it.
static bool terminator_needed(TSNode semi, const PrpfmtState &st) {
  std::string_view src = st.source_code;
  size_t pos = ts_node_end_byte(semi);
  while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\r')) ++pos;
  if (src.substr(pos).starts_with("/*")) return true;
  for (;;) {
    while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
    if (src.substr(pos).starts_with("//")) {
      while (pos < src.size() && src[pos] != '\n') ++pos;
    } else if (src.substr(pos).starts_with("/*")) {
      // Block comments nest: `/* a /* b */ c */` is one comment.
      int depth = 0;
      do {
        if (src.substr(pos).starts_with("/*")) ++depth, pos += 2;
        else if (src.substr(pos).starts_with("*/")) --depth, pos += 2;
        else ++pos;
      } while (depth > 0 && pos < src.size());
    } else {
      break;
    }
  }
  if (pos >= src.size()) return false;
  std::string_view rest = src.substr(pos);
  if (std::string_view(",=.><&^|*/+-:#").find(rest[0]) != std::string_view::npos) return true;
  if (rest.starts_with("!=")) return true;
  if (rest[0] == '{') {
    TSNode prev = ts_node_prev_sibling(semi);
    while (!ts_node_is_null(prev) && grammar_symbol_of(prev) == sym_comment) prev = ts_node_prev_sibling(prev);
    if (!ts_node_is_null(prev) && grammar_symbol_of(prev) == sym_lambda &&
        ts_node_is_null(ts_node_child_by_field_name(prev, "code", 4)))
      return true;
  }
  // Whole words only (scanner.c scan_word_tail): a following ASCII name
  // character makes the word a name. A non-ASCII byte after the word keeps
  // the `;` (`andé` is a name, but a non-breaking space is not; a redundant
  // `;` is harmless, a missing one is not).
  for (std::string_view word : {"and", "or", "in", "implies", "does", "has", "equals", "else", "elif"}) {
    if (!rest.starts_with(word)) continue;
    char next = rest.size() > word.size() ? rest[word.size()] : ' ';
    if (!std::isalnum(static_cast<unsigned char>(next)) && next != '_' && next != '`') return true;
  }
  return false;
}

// Statement finisher: emit the terminating semicolon. A statement terminator
// (SPACE_NONE) that ends its line is dropped (see terminator_needed); list and
// clause separators always print.
void print__semicolon(TSNode node, PrpfmtState &st, SpacingConfig spacing) {
  TSSymbol symbol = grammar_symbol_of(node);
  if (spacing == SPACE_NONE && symbol == anon_sym_SEMI && !terminator_needed(node, st)) return;

  if (symbol == anon_sym_SEMI || symbol == sym__automatic_semicolon) {
    if (spacing & SPACE_BEFORE) emit_space(st);
    emit_token(st, ";");
    if (spacing & SPACE_AFTER) {
      emit_space(st);
    }
  }
}

/******************************************************************************
 * 11. Utilities
 ******************************************************************************/

void print_timing_slot(TSNode node, PrpfmtState &st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = grammar_symbol_of(child);

    switch (symbol) {
      case anon_sym_LBRACK:
        emit_token(st, "[");
        break;
      case anon_sym_RBRACK:
        emit_token(st, "]");
        break;
      case sym_selection_range:
        print_selection_range(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

// Return the source slice spanned by `node` as a non-owning view. The byte
// range always lies within `source_code`, which outlives all formatting, so a
// view avoids copying on the hot path (callers needing ownership copy it).
std::string_view get_node_text(TSNode node, std::string_view source_code) {
  uint32_t start_byte = ts_node_start_byte(node);
  uint32_t end_byte = ts_node_end_byte(node);
  return source_code.substr(start_byte, end_byte - start_byte);
}

void emit_node_text(TSNode node, PrpfmtState &st) {
  if (grammar_symbol_of(node) == sym_identifier) emit_token(st, identifier_text(node, st));
  else emit_token(st, get_node_text(node, st.source_code));
}

/******************************************************************************
 * Private Static Helpers
 ******************************************************************************/

/*
 * Utility to "unwrap" hidden choice nodes in the AST.
 *
 * USE CRITERIA:
 * - DO use for "Choice-Only" hidden rules (e.g., _expression, _type). These rules
 *   act only as labels for a list of options. By unwrapping, we jump straight
 *   to the actual content for processing in a Unified Switch.
 *
 * - DO NOT use for "Structural" hidden rules (e.g., _binary_times, _semicolon).
 *   These rules have internal components (sequences/operators) that must be
 *   preserved as a group for correct formatting.
 */
static void unwrap_hidden(TSNode &node, TSSymbol &symbol) {
  while (ts_node_child_count(node) > 0) {
    TSSymbol s = grammar_symbol_of(node);
    switch (s) {
      case sym__expression:
      case sym__type:
      case sym__restricted_expression:
      case sym__complex_identifier:
      case sym__suffix_head:
        {
          node = ts_node_child(node, 0);
          symbol = grammar_symbol_of(node);
          break;
        }
      default:
        {
          return;
        }
    }
  }
}

/*
 * Internal helper for formatting operator tokens.
 * NOT a direct grammar node printer.
 *
 * Helps nodes:
 * - assignment_operator (=, +=, etc.)
 * - binary_times_op (*, /, %)
 * - binary_other_op (+, -, bitwise, etc.)
 * - binary_compare_op (==, !=, in, etc.)
 * - binary_logical_op (and, or, implies)
 */
static void emit_operator(TSNode node, PrpfmtState &st, SpacingConfig spacing) {
  std::string_view text = get_node_text(node, st.source_code);
  if (spacing & SPACE_BEFORE) {
    emit_space(st);
  }
  emit_token(st, text);
  if (spacing & SPACE_AFTER) {
    emit_space(st);
  }
}

/* Helper for print_match_expression to handle arm indentation and state tracking */
static void ensure_match_arm_started(bool seen_lbrace, bool &arm_started) {
  if (seen_lbrace && !arm_started) {
    arm_started = true;
  }
}

/******************************************************************************
 * Embeddable entry point (see prpfmt_api.h)                                  *
 ******************************************************************************/

#include "prpfmt_api.h"

/* The generated parser's language hook (../src/parser.c). */
extern "C" const TSLanguage *tree_sitter_pyrope(void);

// Node counts that no formatting rule may change: dropping parentheses,
// sorting, shorthand and layout never add or remove a call, a generic list
// or a comparison. A difference means the output reparses differently, e.g.
// `h(x=a < b, y=c > (d + 1))` read as the generic call `a<b, y=c>(d + 1)`.
struct Parse_shape {
  int calls = 0, generics = 0, compares = 0;
  bool error = false;
  bool operator==(const Parse_shape &) const = default;
};
static Parse_shape parse_shape(TSNode root) {
  Parse_shape shape;
  shape.error = ts_node_has_error(root);
  TSTreeCursor cursor = ts_tree_cursor_new(root);
  for (;;) {
    auto symbol = grammar_symbol_of(ts_tree_cursor_current_node(&cursor));
    if (symbol == sym_function_call_expression || symbol == sym_function_call_type) ++shape.calls;
    else if (symbol == sym_generic_type_list || symbol == sym_generic_identifier_list) ++shape.generics;
    else if (symbol == sym_binary_compare_op) ++shape.compares;
    if (ts_tree_cursor_goto_first_child(&cursor)) continue;
    while (!ts_tree_cursor_goto_next_sibling(&cursor))
      if (!ts_tree_cursor_goto_parent(&cursor)) {
        ts_tree_cursor_delete(&cursor);
        return shape;
      }
  }
}

// The printers recurse once or more per tree level, so a deeply nested file
// (thousands of nested `if` expressions or calls) overflows the default 8 MB
// main-thread stack. Formatting runs on its own thread with a large stack
// (virtual memory, only the touched pages are committed), and a tree deeper
// than that stack can hold is refused instead of crashing.
static constexpr size_t format_stack_bytes = size_t(1) << 30;
static constexpr uint32_t max_tree_depth = 200000;

static uint32_t tree_depth(TSNode root) {
  TSTreeCursor cursor = ts_tree_cursor_new(root);
  uint32_t depth = 0, deepest = 0;
  for (;;) {
    if (ts_tree_cursor_goto_first_child(&cursor)) {
      deepest = std::max(deepest, ++depth);
      continue;
    }
    while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
      if (!ts_tree_cursor_goto_parent(&cursor)) {
        ts_tree_cursor_delete(&cursor);
        return deepest;
      }
      --depth;
    }
  }
}

static std::string format_on_this_thread(TSParser *parser, TSTree *tree, std::string_view src, int indent_size,
                                         int max_width, PrpfmtMode mode, bool *parse_error);

std::string prpfmt_format(TSParser *parser, TSTree *tree, std::string_view src, int indent_size, int max_width,
                          PrpfmtMode mode, bool *parse_error, bool *too_deep) {
  if (too_deep) *too_deep = false;
  if (tree_depth(ts_tree_root_node(tree)) > max_tree_depth) {
    if (too_deep) *too_deep = true;
    if (parse_error) *parse_error = true;
    return {};
  }
  struct Job {
    TSParser *parser;
    TSTree *tree;
    std::string_view src;
    int indent_size, max_width;
    PrpfmtMode mode;
    bool *parse_error;
    std::string out;
  } job{parser, tree, src, indent_size, max_width, mode, parse_error, {}};
  auto run = [](void *arg) -> void * {
    auto *j = static_cast<Job *>(arg);
    j->out = format_on_this_thread(j->parser, j->tree, j->src, j->indent_size, j->max_width, j->mode, j->parse_error);
    return nullptr;
  };
  pthread_attr_t attr;
  pthread_t thread;
  bool threaded = pthread_attr_init(&attr) == 0;
  if (threaded) {
    threaded = pthread_attr_setstacksize(&attr, format_stack_bytes) == 0 &&
               pthread_create(&thread, &attr, run, &job) == 0;
    pthread_attr_destroy(&attr);
  }
  if (threaded) pthread_join(thread, nullptr);
  else run(&job);  // no thread available: format on the caller's stack
  return std::move(job.out);
}

static std::string format_on_this_thread(TSParser *parser, TSTree *tree, std::string_view src, int indent_size,
                                         int max_width, PrpfmtMode mode, bool *parse_error) {
  std::string out;
  bool broken = false;
  for (bool conservative : {false, true}) {
    conservative_pass = conservative;
    keep_grouping = 0;
    PrpfmtState state = {
        .source_code   = src,
        .outfile       = NULL,
        .indent_size   = indent_size > 0 ? indent_size : 2,
        .max_width     = max_width > 0 ? max_width : 132,
        .in_assert     = false,
        .allow_inline  = false,
        .nesting_level = 0,
        .fmt_on        = true,
        .inline_exp    = false,
        .buffer        = {},
        .mode          = mode,
    };
    char  *buf = NULL;
    size_t sz  = 0;
    FILE  *mem = open_memstream(&buf, &sz);
    if (!mem) {
      conservative_pass = false;
      if (parse_error) *parse_error = true;
      return {};
    }
    state.outfile = mem;
    active_state = &state;
    print_description(tree, state);
    active_state = nullptr;
    prpfmt_solve(state);
    prpfmt_render(state);
    fclose(mem);  // NUL-terminates buf at sz (terminator not counted)
    out.assign(buf, sz);
    free(buf);

    TSTree *vtree = ts_parser_parse_string(parser, NULL, out.data(), (uint32_t)out.size());
    auto shape = parse_shape(ts_tree_root_node(vtree));
    ts_tree_delete(vtree);
    broken = shape.error;
    // The fallback pass keeps every grouping parenthesis and source order.
    if (shape == parse_shape(ts_tree_root_node(tree))) break;
  }
  conservative_pass = false;
  if (parse_error) *parse_error = broken;
  return out;
}

int prpfmt_format_string(const char *src, size_t len, int indent_size, int max_width, int verify, char **out_buf,
                         size_t *out_len) {
  return prpfmt_format_string_mode(src, len, indent_size, max_width, PRPFMT_AI, verify, out_buf, out_len);
}

int prpfmt_format_string_mode(const char *src, size_t len, int indent_size, int max_width, PrpfmtMode mode,
                             int verify, char **out_buf, size_t *out_len) {
  if (out_buf) {
    *out_buf = NULL;
  }
  if (out_len) {
    *out_len = 0;
  }
  if (!src || (mode != PRPFMT_AI && mode != PRPFMT_HUMAN)) {
    return 1;
  }

  TSParser *parser = ts_parser_new();
  if (!parser) {
    return 1;
  }
  if (!ts_parser_set_language(parser, tree_sitter_pyrope())) {
    ts_parser_delete(parser);
    return 1;  // grammar/runtime ABI mismatch — treat as an internal error
  }

  TSTree *tree = ts_parser_parse_string(parser, NULL, src, (uint32_t)len);
  TSNode  root = ts_tree_root_node(tree);
  if (ts_node_has_error(root) || has_leading_bom(std::string_view(src, len))) {
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 2;  // the input did not parse cleanly
  }

  bool broken = false, too_deep = false;
  std::string out =
      prpfmt_format(parser, tree, std::string_view(src, len), indent_size, max_width, mode, &broken, &too_deep);
  if (too_deep) {
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 4;  // nested too deeply to format safely
  }
  char *buf = static_cast<char *>(malloc(out.size() + 1));
  if (!buf) {
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return 1;
  }
  memcpy(buf, out.data(), out.size());
  buf[out.size()] = '\0';
  size_t sz = out.size();
  int rc = verify && broken ? 3 : 0;  // formatted output no longer parses — flag it, still return the buffer

  ts_tree_delete(tree);
  ts_parser_delete(parser);

  if (out_buf) {
    *out_buf = buf;
  } else {
    free(buf);
  }
  if (out_len) {
    *out_len = sz;
  }
  return rc;
}
