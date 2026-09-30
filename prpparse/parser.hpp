// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ast.hpp"
#include "prp_diag.hpp"
#include "lexer.hpp"
#include "prp_tree.hpp"
#include "source_buffer.hpp"
#include "token.hpp"

namespace prpparse {

// Recursive-descent Pyrope parser. Mirrors grammar.js rule structure and also
// validates ordinary names uniformly: keywords/type spellings require backticks
// in every name position, matched case-insensitively. The editor grammar may
// accept a syntax superset; lhd compile reports these name errors here.
// Fail-fast: the first syntax error throws Parse_error. parse() returns the
// materialized hhds Prp_tree.
class Parser {
public:
  explicit Parser(const Source_buffer& buf);

  Prp_tree& parse();

  // Parse to an Ast without materializing (used by tests).
  Ast* parse_ast();

  // Streaming seam (2f-stream): parse and return ONE top-level construct's Ast
  // (link_parents applied), or nullptr at end of input. The arena is reset
  // before each construct, so resident parse-tree memory stays bounded to one
  // construct — the consumer must fully lower the returned Ast before the next
  // call (the reset invalidates every prior Ast*). The whole-file `parse()` /
  // `parse_ast()` builders are unchanged (still used by the CLI / --sexp / the
  // differential oracle, which need a complete tree).
  Ast* parse_next();

  // When enabled, parse() prints a one-line phase breakdown to stderr
  // (parse_ast / materialize times + span count). Off by default; the CLI
  // turns it on for `--time`.
  static void set_phase_timing(bool on);

  // Live node count of the CST arena — an observable for the streaming
  // memory-bound test (2f-stream): under parse_next() it tracks ONE construct
  // (reset between constructs); under parse_ast()/parse() it is the whole file.
  [[nodiscard]] size_t arena_node_count() const { return arena_.size(); }

private:
  const Source_buffer& buf_;
  std::vector<Token>   toks_;
  size_t               pos_ = 0;
  Ast_arena            arena_;
  Prp_tree             tree_;

  // Expression-bracket depth. >0 means we are inside (...) / [...] / arg lists,
  // where the virtual-semicolon handshake does NOT apply (so `(a\nand b)` keeps
  // continuing the expression). At depth 0 (statement level / inside a scope),
  // a terminator ends an expression. Scopes reset this to 0 (their bodies are
  // statements), and so do `if`/`match` expressions (their headers end at a
  // newline, also inside brackets). Managed via Bracket_guard / Scope_guard
  // (exception-safe).
  int  ebd_ = 0;
  bool term_stop() const { return ebd_ == 0 && cur().terminator_before; }
  struct Bracket_guard {
    Parser& p;
    explicit Bracket_guard(Parser& pp) : p(pp) { ++p.ebd_; }
    ~Bracket_guard() { --p.ebd_; }
    Bracket_guard(const Bracket_guard&)            = delete;
    Bracket_guard& operator=(const Bracket_guard&) = delete;
  };
  struct Scope_guard {
    Parser& p;
    int     saved;
    explicit Scope_guard(Parser& pp) : p(pp), saved(pp.ebd_) { pp.ebd_ = 0; }
    ~Scope_guard() { p.ebd_ = saved; }
    Scope_guard(const Scope_guard&)            = delete;
    Scope_guard& operator=(const Scope_guard&) = delete;
  };

  // Index into toks_ of the first token of the statement being parsed (see
  // blame()). Set by parse_statement, restored on the way out.
  size_t stmt_start_ = 0;
  struct Stmt_start_guard {
    Parser& p;
    size_t  saved;
    explicit Stmt_start_guard(Parser& pp) : p(pp), saved(pp.stmt_start_) { pp.stmt_start_ = pp.pos_; }
    ~Stmt_start_guard() { p.stmt_start_ = saved; }
    Stmt_start_guard(const Stmt_start_guard&)            = delete;
    Stmt_start_guard& operator=(const Stmt_start_guard&) = delete;
  };

  // Index into toks_ of a statement-opening reserved word that is being used
  // the way a plain identifier would be (`stage[0] = a`, `tick = 3`,
  // `in.bits = 1`); kNoKw when the current statement does not open that way.
  // An unescaped keyword used as a name is always a syntax error -- but the caret lands wherever the keyword
  // construct gave up (on the `=` of `stage[0] = a`), which reads as "your
  // expression is broken" when the real answer is "that name needs backticks".
  // Verilog imports hit this constantly, so error() turns the remembered token
  // into the escape hint. Set by parse_statement, cleared once an assignment
  // operator is consumed (errors in the RVALUE are not about the keyword).
  static constexpr size_t kNoKw = static_cast<size_t>(-1);
  size_t                  kw_as_ident_ = kNoKw;
  struct Kw_as_ident_guard {
    Parser& p;
    size_t  saved;
    Kw_as_ident_guard(Parser& pp, size_t v) : p(pp), saved(pp.kw_as_ident_) { pp.kw_as_ident_ = v; }
    ~Kw_as_ident_guard() { p.kw_as_ident_ = saved; }
    Kw_as_ident_guard(const Kw_as_ident_guard&)            = delete;
    Kw_as_ident_guard& operator=(const Kw_as_ident_guard&) = delete;
  };
  // Does `t` continue a word the way a plain identifier would (assignment,
  // index, field/bit select, timing read, type annotation)? `if x {`,
  // `for i in ..`, `test foo {` never do, which keeps the hint off the
  // constructs that really are keywords.
  bool starts_ident_use(const Token& t) const;
  // Attach the backtick escape for reserved word `kw` to `d`.
  void set_kw_hint(Diag& d, const Token& kw) const;
  // Same, for the statement-opening word remembered in kw_as_ident_ (if armed).
  void add_kw_as_ident_hint(Diag& d) const;
  // error(), but with the escape pinned to `kw` instead of to the statement --
  // for a reserved word standing where a name was required somewhere other than
  // the statement's first token (`pub reg[0] = a`, `o = ref[1]`).
  [[noreturn]] void error_reserved_name(const Token& kw, const char* code,
                                        const std::string& message) const;
  // Enforce that the current token is a plain name, not a bare reserved word.
  // `role` completes "'reg' is a reserved word, so it cannot be <role>".
  void require_plain_name(const char* role) const;
  // A reserved type word (`U4`, `S20`, `Bool`, `Clock`, ...) where a NAME is required.
  [[noreturn]] void error_type_word_name(const Token& t, const std::string& role) const;
  // `e` is the bare identifier a type word used as a value parses to
  // (`x does U8`): an expression that may not be reinterpreted as a name
  // (an assignment target, a tuple field, a named argument). Throws if so.
  void reject_type_word_name(const Ast* e, const char* role) const;
  // An assignment target (grammar.js `_single_assignment`: a
  // `_complex_identifier`, optionally typed): a name, a field, a selector, a
  // bit-select, an attribute read or a timed name, possibly carrying a
  // `::[attr]`. Throws `bad-assignment-target` for anything else (`f(x) = 3`,
  // `a + b = 3`, `(1) = 2`, `[a, b] = f()`).
  void require_lvalue(const Ast* e) const;
  // A NAMED binding's name (`f(n = 1)`, `x::[n = 1]`, `(n = local) = f()`):
  // the grammar admits a plain identifier there (plus a dotted field for a call
  // argument, `dotted`). Throws `bad-assignment-target` otherwise.
  void require_binding_name(const Ast* e, bool dotted, const char* role) const;
  // The condition of an if/elif/while/match (the last init-clause item) is an
  // expression (grammar.js `condition: $._expression`), never an assignment,
  // a declaration, a typed field or a `ref`: `if a = 1 { }` is an error.
  void require_condition(const Ast* c) const;

  // ---- cursor ----
  const Token& cur() const { return toks_[pos_ < toks_.size() ? pos_ : toks_.size() - 1]; }
  const Token& peek(size_t k = 1) const {
    size_t i = pos_ + k;
    return i < toks_.size() ? toks_[i] : toks_.back();
  }
  bool at(Token_kind k) const { return cur().kind == k; }
  bool at_kw(Keyword k) const { return cur().is_kw(k); }
  bool eof() const { return cur().kind == Token_kind::eof; }
  // Returns the current token and advances, never moving past the eof sentinel.
  const Token& advance() {
    const Token& t = cur();
    if (pos_ + 1 < toks_.size()) ++pos_;
    return t;
  }
  bool accept(Token_kind k) {
    if (at(k)) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool accept_kw(Keyword k) {
    if (at_kw(k)) {
      ++pos_;
      return true;
    }
    return false;
  }
  const Token& expect(Token_kind k, const char* code, const std::string& what);
  uint32_t     prev_end() const { return pos_ > 0 ? toks_[pos_ - 1].end_byte : 0; }
  void         finish(Ast* a, uint32_t start) const {
    a->start_byte = start;
    a->end_byte   = prev_end();
  }
  // The token a syntax error points at: the current one, unless the statement
  // being parsed already ended before it (the current token opens the next
  // logical line, or the input ended), so the error is about what is missing
  // after the previous token: `enum E:U8 (a, b)` newline `x` points at `)`.
  const Token&      blame() const;
  [[noreturn]] void error(const char* code, const std::string& message) const;
  [[noreturn]] void error_enum_expression() const;  // `const C = enum(a, b)`
  // Like error() but attaches a secondary note pointing at an opening bracket
  // (`[open_start, open_end)`), so unclosed-bracket diagnostics show where the
  // bracket was opened. The primary span stays at the current token.
  [[noreturn]] void error_unclosed(const char* code, const std::string& message, const char* note,
                                    uint32_t open_start, uint32_t open_end) const;
  Span              span_bytes(uint32_t start_byte, uint32_t end_byte) const;
  void              expect_semicolon();

  // ---- arena helpers ----
  Ast* node(Kind k, uint32_t start = 0) { return arena_.make(k, start, start); }
  Ast* leaf(Kind k) {
    const Token& t = cur();
    // One rule for every ordinary name: bindings, fields, attributes,
    // arguments, methods and enum members all require the same escaping.
    // Literal/type operands build their language nodes outside this helper.
    if (k == Kind::identifier) require_plain_name("a name");
    // ... and it is a word: `enum 3 = (a)`, `import 3 as x`, `x.[=]` name nothing.
    if (k == Kind::identifier && t.kind != Token_kind::ident) error("expected-identifier", "expected a name");
    Ast*         a = arena_.make(k, t.start_byte, t.end_byte);
    advance();
    return a;
  }
  // Like leaf(identifier) but errors (at the current token) if it is not one.
  Ast* ident_leaf(const char* code, const std::string& what) {
    if (!at(Token_kind::ident)) error(code, what);
    return leaf(Kind::identifier);
  }

  // ---- statements ----
  Ast* parse_description();
  Ast* parse_statement();
  Ast* parse_scope();
  Ast* parse_import();
  Ast* parse_control();
  Ast* parse_while();
  Ast* parse_for();
  Ast* parse_loop();
  Ast* parse_tick_statement();
  Ast* parse_step_statement();
  Ast* parse_test();
  Ast* parse_formal();
  Ast* parse_type_statement();
  Ast* parse_impl();
  Ast* parse_enum_assignment();
  Ast* parse_lambda();
  Ast* parse_decl_or_assign_or_expr();
  Ast* parse_var_or_let_or_reg();
  Ast* finish_assignment(uint32_t start, Ast* overflow, Ast* decl, Ast* lvalue, Ast* type_cast);

  // ---- expressions ----
  Ast* parse_expression();
  Ast* parse_logical();
  Ast* parse_compare();
  Ast* parse_step();
  Ast* parse_other();
  Ast* parse_times();
  Ast* parse_unary();
  Ast* parse_type_word_operand();
  // `name_ctx`: the head is a NAME position only (a `ref` target, the target
  // after `wrap`/`sat`: grammar.js `_complex_identifier`), where tree-sitter
  // reads `if`/`unique`/`match` as identifiers (`wrap if.total = x`). Elsewhere
  // (a value) they always start that construct. Everywhere `comb`/`mod`/`pipe`/
  // `fluid`/`pub` start a lambda and `true`/`false` are the literals, exactly
  // like the grammar (a suffix head may be a lambda or a literal).
  Ast* parse_postfix(bool name_ctx = false);
  Ast* parse_postfix_from(Ast* e);     // suffix chain starting from a parsed operand
  Ast* consume_binary_tail(Ast* lhs);  // continue a binary expression from a parsed operand
  Ast* parse_atom(bool name_ctx = false);
  Ast* parse_paren(const char* name_role = nullptr);
  Ast* parse_tuple_sq();
  Ast* parse_tuple_item();             // common form; classification discarded
  // Sets `plain` = item was a bare expression. `stmt`: the item is an init-clause
  // STATEMENT (`if x = f(); x { }`). A destructuring assignment is legal in
  // neither (`if (a, b) = f(); a { }`, `const q = ((a, b) = g())` are errors).
  Ast* parse_tuple_item(bool& plain, bool stmt = false);
  Ast* parse_stmt_item();              // parse_tuple_item(stmt=true)
  // Add one parsed tuple item to `parent`, splicing a decl-keyword no-`=` field
  // into separate decl: / lvalue:|value: siblings (tree-sitter shape).
  void add_tuple_child(Ast* parent, Ast* it);
  // `binding`: the item of a destructuring DECLARATION (`const (a, b) = f()`),
  // whose names are bound (no reserved word); else a statement's destructuring
  // targets (`(a, b) = f()`).
  Ast* parse_lvalue_item(bool binding = false);
  Ast* parse_slot_path();  // the `dox.b` of a rename slot `x = dox.b`
  // A reserved word used as a FIELD name: a keyword followed by `=` or `:` at
  // the start of a tuple entry, a named argument, an attribute or generic
  // binding (src/scanner.c scan_field_word).
  bool at_field_word() const;
  // The type word / field-name checks on the DIRECT entries of an enum body.
  void check_enum_members(const Ast* values) const;
  // A format spec (`{x:b}`) inside an interpolation hole: the text from the
  // `:` at byte `colon` to the hole's end `hi` (grammar.js `_format_spec`).
  void check_format_spec(uint32_t colon, uint32_t hi) const;
  Ast* tuple_to_lvalue_list(Ast* tup);
  // The slots and operator of a destructuring assignment (names or `name =
  // path` renames, untyped, `=` only). Throws on anything else.
  void check_destructuring(const Ast* list) const;
  Ast* parse_arg_tuple();
  Ast* parse_arg_item();
  Ast* parse_constant();
  // Interpolated string: build the literal node and sub-parse each `{expr}`
  // hole into a child expression (absolute spans), matching tree-sitter.
  Ast* parse_istring();
  // Lex+parse the byte window [lo, hi) of the buffer as an expression, into the
  // current arena (absolute spans). Used by parse_istring for the holes.
  Ast* parse_subexpr(uint32_t lo, uint32_t hi);
  Ast* parse_complex_identifier();
  Ast* parse_if_expression();
  Ast* parse_match_expression();
  Ast* parse_ref_identifier();
  Ast* try_generic_call(Ast* fn);
  // True when a blank (space, tab, newline, CR, FF, VT) sits right before `t`.
  [[nodiscard]] bool blank_before(const Token& t) const {
    if (t.start_byte == 0) return false;
    const char c = buf_.data()[t.start_byte - 1];
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
  }

  // ---- types ----
  Ast* parse_type_cast();
  Ast* parse_type();
  // A generic ARGUMENT (call-site bind or generic-parameter default): a type,
  // or a bare postfix attribute read of a (dotted) name (`x.[bits]`).
  Ast* parse_generic_value();
  Ast* parse_primitive_type();
  Ast* parse_typed_identifier(bool allow_default = false, const char* bind_role = "a name");
  Ast* parse_typed_identifier_list(bool allow_default = false, const char* bind_role = "a name");
  // `bind_role` completes "'reg' is a reserved word, so it cannot be <role>".
  // nullptr = this list does NOT bind names.
  Ast* parse_arg_list(const char* bind_role = "a parameter name");
  Ast* parse_function_definition_decl();
  Ast* parse_attribute_sq();
  Ast* parse_attribute_list();
  // `allow_empty`: an array LENGTH (`x:[]U8`); every other selector needs an index.
  Ast* parse_select(bool allow_empty = false);
  Ast* parse_selection_range_or_index(Kind container);
  Ast* parse_timing_slot();
  Ast* parse_stmt_list();
  Ast* parse_init_clause();

  // ---- predicates ----
  bool is_decl_keyword(const Token& t) const;
  // `const`/`mut`/`reg`/`wire`/`stage`/`comptime`/`ref`: never an array length (`x:[mut]U8`).
  bool is_array_length_keyword(const Token& t) const;
  // Skip a list's leading commas; commas alone (`(,)`, `f(,)`, `[,]`) are an
  // error (grammar.js `listseq1`: a present list holds at least one item).
  void skip_leading_commas(Token_kind close);
  bool is_lambda_kind(const Token& t) const;
  bool looks_like_lambda();
  bool at_assignment_operator() const;
  bool at_constant() const;
  bool is_primitive_type_word(const Token& t) const;
  // Is `e` a bare (not backticked) identifier spelled like a reserved word?
  bool is_keyword_name(const Ast* e) const;
};

}  // namespace prpparse
