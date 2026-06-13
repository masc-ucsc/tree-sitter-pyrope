// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ast.hpp"
#include "diag.hpp"
#include "lexer.hpp"
#include "prp_tree.hpp"
#include "source_buffer.hpp"
#include "token.hpp"

namespace prpparse {

// Recursive-descent Pyrope parser. Mirrors grammar.js rule structure; accepts
// what grammar.js accepts (overparse parity). Fail-fast: the first syntax error
// throws Parse_error. parse() returns the materialized hhds Prp_tree.
class Parser {
public:
  explicit Parser(const Source_buffer& buf);

  Prp_tree& parse();

  // Parse to an Ast without materializing (used by tests).
  Ast* parse_ast();

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
  // statements). Managed via Bracket_guard / Scope_guard (exception-safe).
  int  ebd_ = 0;
  bool term_stop() const { return ebd_ == 0 && cur().terminator_before; }
  struct Bracket_guard {
    Parser* p;
    explicit Bracket_guard(Parser* pp) : p(pp) { ++p->ebd_; }
    ~Bracket_guard() { --p->ebd_; }
  };
  struct Scope_guard {
    Parser* p;
    int     saved;
    explicit Scope_guard(Parser* pp) : p(pp), saved(pp->ebd_) { pp->ebd_ = 0; }
    ~Scope_guard() { p->ebd_ = saved; }
  };

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
  [[noreturn]] void error(const char* code, const std::string& message) const;
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
  Ast* parse_test();
  Ast* parse_type_statement();
  Ast* parse_impl();
  Ast* parse_spawn();
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
  Ast* parse_postfix();
  Ast* parse_postfix_from(Ast* e);     // suffix chain starting from a parsed operand
  Ast* consume_binary_tail(Ast* lhs);  // continue a binary expression from a parsed operand
  Ast* parse_atom();
  Ast* parse_paren();
  Ast* parse_tuple_sq();
  Ast* parse_tuple_item(bool* plain = nullptr);
  Ast* parse_lvalue_item();
  Ast* parse_arg_tuple();
  Ast* parse_arg_item();
  Ast* parse_constant();
  Ast* parse_complex_identifier();
  Ast* parse_if_expression();
  Ast* parse_match_expression();
  Ast* parse_ref_identifier();
  Ast* try_generic_call(Ast* fn);

  // ---- types ----
  Ast* parse_type_cast();
  Ast* parse_type();
  Ast* parse_primitive_type();
  Ast* parse_typed_identifier();
  Ast* parse_typed_identifier_list();
  Ast* parse_arg_list();
  Ast* parse_function_definition_decl();
  Ast* parse_attribute_sq();
  Ast* parse_attribute_list();
  Ast* parse_select();
  Ast* parse_selection_range_or_index(Kind container);
  Ast* parse_timing_slot();
  Ast* parse_stmt_list();
  Ast* parse_init_clause();

  // ---- predicates ----
  bool is_decl_keyword(const Token& t) const;
  bool is_lambda_kind(const Token& t) const;
  bool looks_like_lambda();
  bool at_assignment_operator() const;
  bool at_constant() const;
  bool is_primitive_type_word(const Token& t) const;
  // True when the current if/unique/match token actually starts that construct
  // (a condition follows). When false, the word is used as a plain identifier,
  // which tree-sitter permits where the keyword token is not grammatically valid
  // (e.g. `wrap if.total = x`, `match.field`).
  bool kw_is_construct_start() const;
};

}  // namespace prpparse
