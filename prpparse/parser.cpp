// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "parser.hpp"

namespace prpparse {

namespace {

// ---- operator token -> node Kind, per precedence tier ----------------------
Kind times_op(const Token& t) {
  switch (t.kind) {
    case Token_kind::star:    return Kind::op_mul;
    case Token_kind::slash:   return Kind::op_div;
    case Token_kind::percent: return Kind::op_mod;
    default:                  return Kind::invalid;
  }
}
Kind other_op(const Token& t) {
  switch (t.kind) {
    case Token_kind::plus:        return Kind::op_add;
    case Token_kind::minus:       return Kind::op_sub;
    case Token_kind::shl:         return Kind::op_shl;
    case Token_kind::shr:         return Kind::op_sra;
    case Token_kind::amp:         return Kind::op_bit_and;
    case Token_kind::pipe:        return Kind::op_bit_or;
    case Token_kind::caret:       return Kind::op_bit_xor;
    case Token_kind::range_incl:  return Kind::op_range_inclusive;
    case Token_kind::range_excl:  return Kind::op_range_exclusive;
    case Token_kind::range_count: return Kind::op_range_count;
    default:                      return Kind::invalid;
  }
}
Kind compare_op(const Token& t) {
  switch (t.kind) {
    case Token_kind::lt: return Kind::op_lt;
    case Token_kind::le: return Kind::op_le;
    case Token_kind::gt: return Kind::op_gt;
    case Token_kind::ge: return Kind::op_ge;
    case Token_kind::eq: return Kind::op_eq;
    case Token_kind::ne: return Kind::op_ne;
    default: break;
  }
  if (t.kind == Token_kind::ident) {
    switch (t.kw) {
      case Keyword::kw_has:    return Kind::op_has;
      case Keyword::kw_in:     return Kind::op_in;
      case Keyword::kw_case:   return Kind::op_case;
      case Keyword::kw_does:   return Kind::op_does;
      case Keyword::kw_is:     return Kind::op_is;
      case Keyword::kw_equals: return Kind::op_equals;
      default: break;
    }
  }
  return Kind::invalid;
}
Kind logical_op(const Token& t) {
  if (t.kind != Token_kind::ident) return Kind::invalid;
  switch (t.kw) {
    case Keyword::kw_and:     return Kind::op_log_and;
    case Keyword::kw_or:      return Kind::op_log_or;
    case Keyword::kw_implies: return Kind::op_implies;
    default:                  return Kind::invalid;
  }
}
// Any binary operator across all tiers (used to continue an expression from an
// already-parsed operand, e.g. a lambda that turns out to be a binary operand).
Kind binary_op_any(const Token& t) {
  Kind k;
  if ((k = times_op(t)) != Kind::invalid) return k;
  if ((k = other_op(t)) != Kind::invalid) return k;
  if (t.is_kw(Keyword::kw_step)) return Kind::op_step;
  if ((k = compare_op(t)) != Kind::invalid) return k;
  if ((k = logical_op(t)) != Kind::invalid) return k;
  return Kind::invalid;
}
Kind assign_kind(Token_kind k) {
  switch (k) {
    case Token_kind::assign:         return Kind::assign;
    case Token_kind::assign_add:     return Kind::assign_add;
    case Token_kind::assign_sub:     return Kind::assign_sub;
    case Token_kind::assign_mul:     return Kind::assign_mul;
    case Token_kind::assign_div:     return Kind::assign_div;
    case Token_kind::assign_or:      return Kind::assign_bit_or;
    case Token_kind::assign_and:     return Kind::assign_bit_and;
    case Token_kind::assign_xor:     return Kind::assign_bit_xor;
    case Token_kind::assign_shl:     return Kind::assign_shl;
    case Token_kind::assign_sra:     return Kind::assign_sra;
    case Token_kind::assign_log_or:  return Kind::assign_log_or;
    case Token_kind::assign_log_and: return Kind::assign_log_and;
    default:                         return Kind::invalid;
  }
}

// Tokens that can begin an expression (so a construct keyword like `if` is
// genuinely starting its condition rather than being used as an identifier).
bool is_expr_start(const Token& t) {
  switch (t.kind) {
    case Token_kind::integer:
    case Token_kind::string:
    case Token_kind::istring:
    case Token_kind::question:
    case Token_kind::lparen:
    case Token_kind::lbracket:
    case Token_kind::lbrace:
    case Token_kind::bang:
    case Token_kind::tilde:
    case Token_kind::minus:
    case Token_kind::ellipsis:
    case Token_kind::ident:  // identifier, or a keyword-led operand (not/if/...)
      return true;
    default:
      return false;
  }
}

bool all_digits(std::string_view s, size_t from) {
  if (from >= s.size()) return false;
  for (size_t i = from; i < s.size(); ++i)
    if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

}  // namespace

Parser::Parser(const Source_buffer& buf) : buf_(buf), tree_(buf) {
  Lexer lex(buf);
  toks_ = lex.tokenize();
}

Span Parser::span_bytes(uint32_t start_byte, uint32_t end_byte) const {
  Span s;
  s.file       = buf_.path();
  s.start_byte = start_byte;
  s.end_byte   = end_byte;
  s.start_line = buf_.line_of(start_byte);
  s.start_col  = buf_.col_of(start_byte);
  s.end_line   = s.start_line;
  s.end_col    = s.start_col + (end_byte - start_byte);
  s.valid      = true;
  return s;
}

void Parser::error(const char* code, const std::string& message) const {
  const Token& t = cur();
  Diag         d;
  d.code     = code;
  d.category = std::string(kCategorySyntax);
  d.message  = message;
  d.span     = span_bytes(t.start_byte, t.end_byte);
  throw Parse_error(std::move(d));
}

void Parser::error_unclosed(const char* code, const std::string& message, const char* note,
                            uint32_t open_start, uint32_t open_end) const {
  Diag d;
  d.code     = code;
  d.category = std::string(kCategorySyntax);
  if (eof()) {
    // The bracket is never closed before end of input: blame the opener (this
    // matches tree-sitter, which roots the ERROR node at the opening bracket).
    d.message = message + " before end of input";
    d.span    = span_bytes(open_start, open_end);
  } else {
    // An unexpected token appeared where the closer was expected: blame it, and
    // point back at the opener with a note.
    d.message = message;
    d.span    = span_bytes(cur().start_byte, cur().end_byte);
    d.notes.push_back(Note{note, span_bytes(open_start, open_end)});
  }
  throw Parse_error(std::move(d));
}

const Token& Parser::expect(Token_kind k, const char* code, const std::string& what) {
  if (at(k)) return advance();
  error(code, what + " (found '" + std::string(cur().text) + "')");
}

void Parser::expect_semicolon() {
  if (at(Token_kind::semicolon)) {
    advance();
    return;
  }
  if (cur().terminator_before) return;  // virtual semicolon
  if (eof() || at(Token_kind::rbrace)) return;
  error("missing-terminator", "expected newline or ';' to end the statement");
}

// ---- predicates ------------------------------------------------------------
bool Parser::is_decl_keyword(const Token& t) const {
  if (t.kind != Token_kind::ident) return false;
  switch (t.kw) {
    case Keyword::kw_pub:
    case Keyword::kw_comptime:
    case Keyword::kw_fluid:
    case Keyword::kw_const:
    case Keyword::kw_mut:
    case Keyword::kw_reg:
    case Keyword::kw_stage:
      return true;
    default:
      return false;
  }
}
bool Parser::is_lambda_kind(const Token& t) const {
  if (t.kind != Token_kind::ident) return false;
  return t.kw == Keyword::kw_comb || t.kw == Keyword::kw_mod || t.kw == Keyword::kw_pipe ||
         t.kw == Keyword::kw_fluid;
}
bool Parser::at_assignment_operator() const { return assign_kind(cur().kind) != Kind::invalid; }
bool Parser::kw_is_construct_start() const {
  if (at_kw(Keyword::kw_unique)) return peek(1).is_kw(Keyword::kw_if);
  return is_expr_start(peek(1));  // if / match: a condition must follow
}
bool Parser::at_constant() const {
  switch (cur().kind) {
    case Token_kind::integer:
    case Token_kind::string:
    case Token_kind::istring:
    case Token_kind::question:
      return true;
    default:
      break;
  }
  return cur().is_kw(Keyword::kw_true) || cur().is_kw(Keyword::kw_false);
}
bool Parser::is_primitive_type_word(const Token& t) const {
  if (t.kind != Token_kind::ident) return false;
  switch (t.kw) {
    case Keyword::kw_uint:
    case Keyword::kw_unsigned:
    case Keyword::kw_int:
    case Keyword::kw_integer:
    case Keyword::kw_bool:
    case Keyword::kw_string:
      return true;
    default:
      break;
  }
  // u<N> / s<N> / i<N>
  std::string_view s = t.text;
  if (s.size() >= 2 && s[0] == 'u' && all_digits(s, 1)) return true;
  if (s.size() >= 2 && (s[0] == 's' || s[0] == 'i') && all_digits(s, 1)) return true;
  return false;
}

// Lookahead: is this a lambda (vs a fluid/storage declaration or a plain use)?
bool Parser::looks_like_lambda() {
  size_t save = pos_;
  bool   result = false;
  if (at_kw(Keyword::kw_pub)) ++pos_;
  if (at_kw(Keyword::kw_comb) || at_kw(Keyword::kw_mod) || at_kw(Keyword::kw_pipe)) {
    result = true;  // comb/mod/pipe are lambda-only
  } else if (at_kw(Keyword::kw_fluid)) {
    ++pos_;
    // skip an optional [config] bracket (fluid_lambda) before the name
    if (at(Token_kind::lbracket)) {
      int depth = 0;
      do {
        if (at(Token_kind::lbracket)) ++depth;
        else if (at(Token_kind::rbracket)) --depth;
        else if (eof()) break;
        ++pos_;
      } while (depth > 0);
    }
    // fluid_lambda: name '(' ... ; fluid declaration: storage/typed_identifier
    if (cur().kind == Token_kind::ident && !is_decl_keyword(cur()) &&
        peek(1).kind == Token_kind::lparen) {
      result = true;
    }
  }
  pos_ = save;
  return result;
}

// ===========================================================================
// Entry
// ===========================================================================
Ast* Parser::parse_ast() { return parse_description(); }

Prp_tree& Parser::parse() {
  Ast* root = parse_description();
  tree_.materialize(root);
  return tree_;
}

Ast* Parser::parse_description() {
  Ast* root = node(Kind::description, 0);
  while (!eof()) {
    Ast* s = parse_statement();
    root->add(s);
    while (at(Token_kind::semicolon)) advance();
  }
  root->start_byte = 0;
  root->end_byte   = static_cast<uint32_t>(buf_.size());
  return root;
}

// ===========================================================================
// Statements
// ===========================================================================
Ast* Parser::parse_statement() {
  const Token& t = cur();

  if (t.kind == Token_kind::lbrace) return parse_scope();

  if (t.kind == Token_kind::ident) {
    switch (t.kw) {
      case Keyword::kw_import: return parse_import();
      case Keyword::kw_break:
      case Keyword::kw_continue:
      case Keyword::kw_return: return parse_control();
      case Keyword::kw_while: return parse_while();
      case Keyword::kw_for:   return parse_for();
      case Keyword::kw_loop:  return parse_loop();
      case Keyword::kw_test:  return parse_test();
      case Keyword::kw_type:  return parse_type_statement();
      case Keyword::kw_impl:  return parse_impl();
      case Keyword::kw_spawn: return parse_spawn();
      case Keyword::kw_enum: {
        Ast* e = parse_enum_assignment();
        expect_semicolon();
        return e;
      }
      default:
        break;
    }
  }

  if (looks_like_lambda()) {
    // Usually a lambda declaration statement (no terminator needed). But a
    // lambda can also be an expression operand (`comb f(){} . foo`,
    // `comb f(){} * x` — overparse). If a suffix/binary operator follows, treat
    // it as an expression statement instead.
    Ast* lam = parse_lambda();
    Ast* e   = consume_binary_tail(parse_postfix_from(lam));
    if (e == lam) return lam;  // standalone lambda declaration statement
    expect_semicolon();
    return e;
  }

  return parse_decl_or_assign_or_expr();
}

Ast* Parser::parse_scope() {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lbrace, "expected-brace", "expected '{'");
  Scope_guard _sg(this);
  Ast* sc = node(Kind::scope_statement, start);
  while (!at(Token_kind::rbrace) && !eof()) {
    sc->add(parse_statement());
    while (at(Token_kind::semicolon)) advance();
  }
  if (!at(Token_kind::rbrace))
    error_unclosed("unclosed-scope", "expected '}' to close the block", "'{' opened here", start,
                   start + 1);
  advance();  // '}'
  finish(sc, start);
  return sc;
}

Ast* Parser::parse_import() {
  uint32_t start = cur().start_byte;
  advance();  // import
  Ast* imp = node(Kind::import_statement, start);
  if (at(Token_kind::string) || at(Token_kind::istring)) {
    imp->add(leaf(at(Token_kind::string) ? Kind::string_literal : Kind::interpolated_string_literal),
             Field::f_module);
  } else {
    Ast* mod = leaf(Kind::identifier);
    mod->field = Field::f_module;
    imp->add(mod);
    while (at(Token_kind::dot)) {
      advance();
      imp->add(leaf(Kind::identifier));
    }
  }
  if (!accept_kw(Keyword::kw_as)) error("expected-as", "expected 'as' in import statement");
  Ast* alias = leaf(Kind::identifier);
  imp->add(alias, Field::f_alias);
  expect_semicolon();
  finish(imp, start);
  return imp;
}

Ast* Parser::parse_control() {
  uint32_t start = cur().start_byte;
  Kind     k = cur().is_kw(Keyword::kw_break)      ? Kind::break_statement
               : cur().is_kw(Keyword::kw_continue) ? Kind::continue_statement
                                                   : Kind::return_statement;
  advance();
  Ast* c = node(k, start);
  expect_semicolon();
  finish(c, start);
  Ast* wrap = node(Kind::control_statement, start);
  wrap->add(c);
  finish(wrap, start);
  return wrap;
}

Ast* Parser::parse_while() {
  uint32_t start = cur().start_byte;
  advance();  // while
  Ast* w = node(Kind::while_statement, start);
  if (at(Token_kind::coloncolon)) {  // _attr_prefix ::[...]
    advance();
    w->add(parse_attribute_sq(), Field::f_attributes);
  }
  // optional init (stmt_list ';') then condition
  std::vector<Ast*> items;
  items.push_back(parse_tuple_item());
  while (at(Token_kind::semicolon)) {
    advance();
    while (at(Token_kind::semicolon)) advance();
    if (at(Token_kind::lbrace)) break;
    items.push_back(parse_tuple_item());
  }
  Ast* cond = items.back();
  items.pop_back();
  if (!items.empty()) {
    Ast* sl = node(Kind::stmt_list, items.front()->start_byte);
    for (Ast* it : items) sl->add(it, Field::f_item);
    finish(sl, sl->start_byte);
    w->add(sl, Field::f_init);
  }
  cond->field = Field::f_condition;
  w->add(cond);
  w->add(parse_scope(), Field::f_code);
  finish(w, start);
  return w;
}

Ast* Parser::parse_for() {
  uint32_t start = cur().start_byte;
  advance();  // for
  Ast* f = node(Kind::for_statement, start);
  if (at(Token_kind::coloncolon)) {
    advance();
    f->add(parse_attribute_sq(), Field::f_attributes);
  }
  // forBinding: '(' typed_identifier_list ')' | typed_identifier
  if (at(Token_kind::lparen)) {
    advance();
    Ast* til = parse_typed_identifier_list();
    expect(Token_kind::rparen, "unclosed-paren", "expected ')' in for binding");
    til->field = Field::f_index;
    f->add(til);
  } else {
    f->add(parse_typed_identifier(), Field::f_index);
  }
  if (!accept_kw(Keyword::kw_in)) error("expected-in", "expected 'in' in for loop");
  if (at_kw(Keyword::kw_ref)) {
    f->add(parse_ref_identifier(), Field::f_data);
  } else {
    f->add(parse_expression(), Field::f_data);
  }
  f->add(parse_scope(), Field::f_code);
  finish(f, start);
  return f;
}

Ast* Parser::parse_loop() {
  uint32_t start = cur().start_byte;
  advance();  // loop
  Ast* l = node(Kind::loop_statement, start);
  if (at(Token_kind::coloncolon)) {
    advance();
    l->add(parse_attribute_sq(), Field::f_attributes);
  }
  if (!at(Token_kind::lbrace)) l->add(parse_stmt_list(), Field::f_init);
  l->add(parse_scope(), Field::f_code);
  finish(l, start);
  return l;
}

Ast* Parser::parse_test() {
  uint32_t start = cur().start_byte;
  advance();  // test
  Ast* tnode = node(Kind::test_statement, start);
  Ast* args  = node(Kind::expression_list, cur().start_byte);
  args->add(parse_expression(), Field::f_item);
  while (at(Token_kind::comma)) {
    advance();
    if (at(Token_kind::lbrace)) break;
    args->add(parse_expression(), Field::f_item);
  }
  finish(args, args->start_byte);
  tnode->add(args, Field::f_args);
  tnode->add(parse_scope(), Field::f_code);
  finish(tnode, start);
  return tnode;
}

Ast* Parser::parse_type_statement() {
  uint32_t start = cur().start_byte;
  advance();  // type
  Ast* ts = node(Kind::type_statement, start);
  ts->add(leaf(Kind::identifier), Field::f_name);
  if (at(Token_kind::lt)) {
    advance();
    ts->add(parse_typed_identifier_list(), Field::f_generic);
    expect(Token_kind::gt, "expected-gt", "expected '>' to close generics");
  }
  if (at(Token_kind::lparen)) {
    // trait definition: type Name ( ... )
    ts->add(parse_paren(), Field::f_definition);
  } else {
    expect(Token_kind::assign, "expected-eq", "expected '=' or '(' in type statement");
    if (is_lambda_kind(cur())) {
      // func type: comb/mod/pipe/fluid function_definition_decl
      if (at_kw(Keyword::kw_comb)) ts->add(leaf(Kind::comb_lambda), Field::f_func_type);
      else if (at_kw(Keyword::kw_mod)) ts->add(leaf(Kind::mod_lambda), Field::f_func_type);
      else if (at_kw(Keyword::kw_pipe)) ts->add(leaf(Kind::pipe_lambda), Field::f_func_type);
      else ts->add(leaf(Kind::fluid_lambda), Field::f_func_type);
      ts->add(parse_function_definition_decl());
    } else {
      ts->add(parse_type(), Field::f_alias);
    }
  }
  expect_semicolon();
  finish(ts, start);
  return ts;
}

Ast* Parser::parse_impl() {
  uint32_t start = cur().start_byte;
  advance();  // impl
  Ast* im = node(Kind::impl_statement, start);
  im->add(leaf(Kind::identifier), Field::f_trait_name);
  if (!accept_kw(Keyword::kw_for)) error("expected-for", "expected 'for' in impl statement");
  im->add(leaf(Kind::identifier), Field::f_type_name);
  im->add(parse_paren(), Field::f_implementation);
  expect_semicolon();
  finish(im, start);
  return im;
}

Ast* Parser::parse_spawn() {
  uint32_t start = cur().start_byte;
  advance();  // spawn
  Ast* sp = node(Kind::spawn_statement, start);
  sp->add(leaf(Kind::identifier), Field::f_name);
  expect(Token_kind::assign, "expected-eq", "expected '=' in spawn statement");
  sp->add(parse_scope());
  expect_semicolon();
  finish(sp, start);
  return sp;
}

Ast* Parser::parse_enum_assignment() {
  uint32_t start = cur().start_byte;
  advance();  // enum
  Ast* en = node(Kind::enum_assignment, start);
  en->add(leaf(Kind::identifier), Field::f_name);
  if (at(Token_kind::colon) || at(Token_kind::coloncolon)) en->add(parse_type_cast(), Field::f_type);
  if (accept(Token_kind::assign)) {
    en->add(parse_paren(), Field::f_values);
  } else {
    en->add(parse_arg_list(), Field::f_body);
  }
  finish(en, start);
  return en;
}

// ===========================================================================
// Declarations / assignments / expression statements
// ===========================================================================
Ast* Parser::parse_var_or_let_or_reg() {
  uint32_t start = cur().start_byte;
  Ast*     v = node(Kind::var_or_let_or_reg, start);
  if (at_kw(Keyword::kw_pub)) v->add(leaf(Kind::pub_modifier), Field::f_pub);
  if (at_kw(Keyword::kw_comptime)) v->add(leaf(Kind::comptime_modifier), Field::f_comptime);
  if (at_kw(Keyword::kw_fluid)) {
    v->add(leaf(Kind::fluid_decl), Field::f_fluid);
    if (at_kw(Keyword::kw_const)) v->add(leaf(Kind::const_decl), Field::f_storage);
    else if (at_kw(Keyword::kw_mut)) v->add(leaf(Kind::mut_decl), Field::f_storage);
    else if (at_kw(Keyword::kw_reg)) v->add(leaf(Kind::reg_decl), Field::f_storage);
    else if (at_kw(Keyword::kw_stage)) {
      Ast* st = node(Kind::stage_decl, cur().start_byte);
      advance();
      if (at(Token_kind::lbracket)) st->add(parse_timing_slot(), Field::f_timing);
      finish(st, st->start_byte);
      v->add(st, Field::f_storage);
    }
  } else if (at_kw(Keyword::kw_const)) {
    v->add(leaf(Kind::const_decl), Field::f_storage);
  } else if (at_kw(Keyword::kw_mut)) {
    v->add(leaf(Kind::mut_decl), Field::f_storage);
  } else if (at_kw(Keyword::kw_reg)) {
    v->add(leaf(Kind::reg_decl), Field::f_storage);
  } else if (at_kw(Keyword::kw_stage)) {
    Ast* st = node(Kind::stage_decl, cur().start_byte);
    advance();
    if (at(Token_kind::lbracket)) st->add(parse_timing_slot(), Field::f_timing);
    finish(st, st->start_byte);
    v->add(st, Field::f_storage);
  }
  finish(v, start);
  return v;
}

Ast* Parser::finish_assignment(uint32_t start, Ast* overflow, Ast* decl, Ast* lvalue,
                               Ast* type_cast) {
  Ast* a = node(Kind::assignment, start);
  if (overflow) a->add(overflow, Field::f_overflow);
  if (decl) a->add(decl, Field::f_decl);
  if (lvalue) a->add(lvalue, Field::f_lvalue);
  if (type_cast) a->add(type_cast, Field::f_type);
  // assignment operator
  Kind opk = assign_kind(cur().kind);
  Ast* op  = arena_.make(opk, cur().start_byte, cur().end_byte);
  advance();
  a->add(op, Field::f_operator);
  // rvalue: expression | enum_definition | ref_identifier
  if (at_kw(Keyword::kw_enum)) {
    uint32_t es = cur().start_byte;
    advance();
    Ast* ed = node(Kind::enum_definition, es);
    ed->add(parse_arg_list(), Field::f_input);
    finish(ed, es);
    a->add(ed, Field::f_rvalue);
  } else if (at_kw(Keyword::kw_ref)) {
    a->add(parse_ref_identifier(), Field::f_rvalue);
  } else {
    a->add(parse_expression(), Field::f_rvalue);
  }
  finish(a, start);
  return a;
}

Ast* Parser::parse_decl_or_assign_or_expr() {
  uint32_t start = cur().start_byte;

  // overflow modifier on an assignment: wrap / sat (anonymous tokens in the
  // grammar; consumed but not stored as a node).
  Ast* overflow     = nullptr;
  bool has_overflow = false;
  if (at_kw(Keyword::kw_wrap) || at_kw(Keyword::kw_sat)) {
    advance();
    has_overflow = true;
  }

  if (is_decl_keyword(cur())) {
    Ast* decl = parse_var_or_let_or_reg();
    if (at(Token_kind::lparen)) {
      // '(' list ')' then '=' (assignment lvalue_list) or ';' (declaration list)
      advance();
      Ast*              list = node(Kind::lvalue_list, cur().start_byte);
      std::vector<Ast*> items;
      {
        Bracket_guard _bg(this);
        while (at(Token_kind::comma)) advance();
        while (!at(Token_kind::rparen) && !eof()) {
          items.push_back(parse_lvalue_item());
          if (!accept(Token_kind::comma)) break;
          while (at(Token_kind::comma)) advance();
        }
        expect(Token_kind::rparen, "unclosed-paren", "expected ')'");
      }
      for (Ast* it : items) list->add(it, Field::f_item);
      finish(list, list->start_byte);
      if (at_assignment_operator()) {
        Ast* a = finish_assignment(start, overflow, decl, list, nullptr);
        expect_semicolon();
        return a;
      }
      Ast* d = node(Kind::declaration_statement, start);
      d->add(decl, Field::f_decl);
      list->kind = Kind::typed_identifier_list;
      d->add(list, Field::f_lvalue);
      expect_semicolon();
      finish(d, start);
      return d;
    }
    // The lvalue may be a complex location (`x#[i]`, `a.b`, `arr[i]`) when this
    // is an assignment, or a (typed) identifier when it is a declaration.
    Ast* lv = parse_postfix();
    Ast* tc = nullptr;
    if ((at(Token_kind::colon) || at(Token_kind::coloncolon)) && lv->kind == Kind::identifier)
      tc = parse_type_cast();
    // Wrap a bare identifier lvalue as a typed_identifier (mirrors the grammar).
    Ast* ti = lv;
    if (lv->kind == Kind::identifier) {
      ti        = node(Kind::typed_identifier, lv->start_byte);
      lv->field = Field::f_identifier;
      ti->add(lv);
      if (tc) {
        ti->add(tc, Field::f_type);
        tc = nullptr;
      }
      finish(ti, lv->start_byte);
    }
    if (at_assignment_operator()) {
      Ast* a = finish_assignment(start, overflow, decl, ti, tc);
      expect_semicolon();
      return a;
    }
    Ast* d = node(Kind::declaration_statement, start);
    d->add(decl, Field::f_decl);
    d->add(ti, Field::f_lvalue);
    expect_semicolon();
    finish(d, start);
    return d;
  }

  // No declaration keyword: assignment with a complex lvalue, or expr statement.
  Ast* e = parse_expression();
  Ast* type_cast = nullptr;
  if (at(Token_kind::colon) || at(Token_kind::coloncolon)) {
    // `lvalue : Type = ...`
    type_cast = parse_type_cast();
  }
  if (at_assignment_operator()) {
    Ast* a = finish_assignment(start, overflow, nullptr, e, type_cast);
    expect_semicolon();
    return a;
  }
  if (has_overflow || type_cast)
    error("expected-assignment", "expected an assignment operator");
  // expression statement
  expect_semicolon();
  return e;
}

// ===========================================================================
// Lambda
// ===========================================================================
Ast* Parser::parse_lambda() {
  uint32_t start = cur().start_byte;
  Ast*     lam = node(Kind::lambda, start);
  if (at_kw(Keyword::kw_pub)) lam->add(leaf(Kind::pub_modifier), Field::f_pub);
  if (at_kw(Keyword::kw_comb)) {
    lam->add(leaf(Kind::comb_lambda), Field::f_func_type);
  } else if (at_kw(Keyword::kw_mod)) {
    lam->add(leaf(Kind::mod_lambda), Field::f_func_type);
  } else if (at_kw(Keyword::kw_pipe)) {
    Ast* pl = node(Kind::pipe_lambda, cur().start_byte);
    advance();
    if (at(Token_kind::lbracket)) pl->add(parse_select(), Field::f_depth);
    finish(pl, pl->start_byte);
    lam->add(pl, Field::f_func_type);
  } else {  // fluid
    Ast* fl = node(Kind::fluid_lambda, cur().start_byte);
    advance();
    if (at(Token_kind::lbracket)) fl->add(parse_attribute_sq(), Field::f_config);
    finish(fl, fl->start_byte);
    lam->add(fl, Field::f_func_type);
  }
  lam->add(leaf(Kind::identifier), Field::f_name);
  lam->add(parse_function_definition_decl());
  if (at(Token_kind::lbrace)) lam->add(parse_scope(), Field::f_code);
  finish(lam, start);
  return lam;
}

Ast* Parser::parse_function_definition_decl() {
  uint32_t start = cur().start_byte;
  Ast*     fdd = node(Kind::function_definition_decl, start);
  if (at(Token_kind::lt)) {
    advance();
    fdd->add(parse_typed_identifier_list(), Field::f_generic);
    expect(Token_kind::gt, "expected-gt", "expected '>' to close generics");
  }
  if (at(Token_kind::coloncolon)) {  // pipe_config ::[...]
    advance();
    fdd->add(parse_attribute_sq(), Field::f_pipe_config);
  }
  fdd->add(parse_arg_list(), Field::f_input);
  if (accept(Token_kind::arrow)) {
    if (at(Token_kind::lparen)) {
      fdd->add(parse_arg_list(), Field::f_output);
    } else if (at(Token_kind::colon) || at(Token_kind::coloncolon)) {
      fdd->add(parse_type_cast(), Field::f_output);
    } else {
      fdd->add(parse_typed_identifier(), Field::f_output);
    }
  }
  finish(fdd, start);
  return fdd;
}

Ast* Parser::parse_arg_list() {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lparen, "expected-paren", "expected '(' to open argument list");
  Bracket_guard _bg(this);
  Ast* al = node(Kind::arg_list, start);
  while (at(Token_kind::comma)) advance();
  while (!at(Token_kind::rparen) && !eof()) {
    // [mod] typed_identifier [= default]
    if (at(Token_kind::ellipsis) || at_kw(Keyword::kw_ref) || at_kw(Keyword::kw_reg)) advance();
    Ast* ti = parse_typed_identifier();
    ti->field = Field::f_item;
    al->add(ti);
    if (accept(Token_kind::assign)) al->add(parse_expression(), Field::f_definition);
    if (!accept(Token_kind::comma)) break;
    while (at(Token_kind::comma)) advance();
  }
  if (!at(Token_kind::rparen))
    error_unclosed("unclosed-paren", "expected ')' to close argument list", "'(' opened here", start,
                   start + 1);
  advance();  // ')'
  finish(al, start);
  return al;
}

// ===========================================================================
// Expressions (precedence climbing, flat same-tier chains)
// ===========================================================================
Ast* Parser::parse_expression() { return parse_logical(); }

#define PRP_BINARY_TIER(FN, NEXT, OPFN)                                     \
  Ast* Parser::FN() {                                                       \
    Ast* lhs = NEXT();                                                      \
    if (term_stop() || OPFN(cur()) == Kind::invalid) return lhs;            \
    uint32_t start = lhs->start_byte;                                       \
    Ast*     nd    = node(Kind::expression_item, start);                    \
    lhs->field     = Field::f_operand;                                      \
    nd->add(lhs);                                                           \
    Kind opk;                                                               \
    while (!term_stop() && (opk = OPFN(cur())) != Kind::invalid) {          \
      Ast* op = arena_.make(opk, cur().start_byte, cur().end_byte);         \
      advance();                                                            \
      nd->add(op, Field::f_operator);                                       \
      Ast* rhs   = NEXT();                                                  \
      rhs->field = Field::f_operand;                                        \
      nd->add(rhs);                                                         \
    }                                                                       \
    finish(nd, start);                                                      \
    return nd;                                                              \
  }

PRP_BINARY_TIER(parse_logical, parse_compare, logical_op)
PRP_BINARY_TIER(parse_compare, parse_step, compare_op)
PRP_BINARY_TIER(parse_other, parse_times, other_op)
PRP_BINARY_TIER(parse_times, parse_unary, times_op)
#undef PRP_BINARY_TIER

// step tier sits between `other` and `compare`.
Ast* Parser::parse_step() {
  Ast* lhs = parse_other();
  if (term_stop() || !cur().is_kw(Keyword::kw_step)) return lhs;
  uint32_t start = lhs->start_byte;
  Ast*     nd    = node(Kind::expression_item, start);
  lhs->field     = Field::f_operand;
  nd->add(lhs);
  while (!term_stop() && cur().is_kw(Keyword::kw_step)) {
    Ast* op = arena_.make(Kind::op_step, cur().start_byte, cur().end_byte);
    advance();
    nd->add(op, Field::f_operator);
    Ast* rhs   = parse_other();
    rhs->field = Field::f_operand;
    nd->add(rhs);
  }
  finish(nd, start);
  return nd;
}

// tier 1 operand: unary / if / match / scope / restricted-with-suffixes.
Ast* Parser::parse_unary() {
  const Token& t = cur();
  if (t.kind == Token_kind::bang || t.kind == Token_kind::tilde || t.kind == Token_kind::minus ||
      t.kind == Token_kind::ellipsis || t.is_kw(Keyword::kw_not)) {
    uint32_t start = t.start_byte;
    Kind     opk;
    switch (t.kind) {
      case Token_kind::bang:     opk = Kind::op_log_not; break;
      case Token_kind::tilde:    opk = Kind::op_bit_not; break;
      case Token_kind::minus:    opk = Kind::op_unary_minus; break;
      case Token_kind::ellipsis: opk = Kind::op_spread; break;
      default:                   opk = Kind::op_log_not; break;  // `not`
    }
    Ast* op = arena_.make(opk, t.start_byte, t.end_byte);
    advance();
    Ast* un = node(Kind::unary_expression, start);
    un->add(op, Field::f_operator);
    un->add(parse_unary(), Field::f_argument);
    finish(un, start);
    return un;
  }
  if ((t.is_kw(Keyword::kw_if) || t.is_kw(Keyword::kw_unique)) && kw_is_construct_start())
    return parse_if_expression();
  if (t.is_kw(Keyword::kw_match) && kw_is_construct_start()) return parse_match_expression();
  if (t.kind == Token_kind::lbrace) return parse_scope();
  return parse_postfix();
}

Ast* Parser::parse_postfix() { return parse_postfix_from(parse_atom()); }

Ast* Parser::consume_binary_tail(Ast* lhs) {
  if (term_stop() || binary_op_any(cur()) == Kind::invalid) return lhs;
  Ast* nd    = node(Kind::expression_item, lhs->start_byte);
  lhs->field = Field::f_operand;
  nd->add(lhs);
  Kind opk;
  while (!term_stop() && (opk = binary_op_any(cur())) != Kind::invalid) {
    Ast* op = arena_.make(opk, cur().start_byte, cur().end_byte);
    advance();
    nd->add(op, Field::f_operator);
    Ast* rhs   = parse_unary();
    rhs->field = Field::f_operand;
    nd->add(rhs);
  }
  finish(nd, lhs->start_byte);
  return nd;
}

Ast* Parser::parse_postfix_from(Ast* e) {
  uint32_t start = e->start_byte;
  for (;;) {
    if (term_stop()) break;
    Kind ek = e->kind;
    bool callable = ek == Kind::identifier || ek == Kind::dot_expression ||
                    ek == Kind::member_selection || ek == Kind::bit_selection ||
                    ek == Kind::attribute_read || ek == Kind::timed_identifier;
    if (at(Token_kind::lparen) && callable) {
      Ast* call = node(Kind::function_call_expression, start);
      e->field  = Field::f_function;
      call->add(e);
      call->add(parse_arg_tuple(), Field::f_argument);
      finish(call, start);
      e = call;
      continue;
    }
    if (at(Token_kind::lt) && ek == Kind::identifier) {
      Ast* g = try_generic_call(e);
      if (g) {
        e = g;
        continue;
      }
    }
    if (at(Token_kind::dot)) {
      if (peek(1).kind == Token_kind::lbracket) {
        Ast* ar  = node(Kind::attribute_read, start);
        e->field = Field::f_argument;
        ar->add(e);
        while (at(Token_kind::dot) && peek(1).kind == Token_kind::lbracket) {
          advance();  // '.'
          ar->add(parse_attribute_list(), Field::f_attrs);
        }
        finish(ar, start);
        e = ar;
        continue;
      }
      Ast* de  = node(Kind::dot_expression, start);
      e->field = Field::f_item;
      de->add(e);
      while (at(Token_kind::dot) && peek(1).kind != Token_kind::lbracket) {
        advance();  // '.'
        de->add(ident_leaf("expected-field", "expected a field name after '.'"));
      }
      finish(de, start);
      e = de;
      continue;
    }
    if (at(Token_kind::lbracket)) {
      Ast* ms  = node(Kind::member_selection, start);
      e->field = Field::f_argument;
      ms->add(e);
      // Consecutive selects, but '[' is not a continuation token: a '[' that
      // begins a new line (terminator before it) starts a new statement.
      do {
        ms->add(parse_select(), Field::f_select);
      } while (at(Token_kind::lbracket) && !term_stop());
      finish(ms, start);
      e = ms;
      continue;
    }
    if (at(Token_kind::hash)) {
      Ast* bs  = node(Kind::bit_selection, start);
      e->field = Field::f_argument;
      bs->add(e);
      advance();  // '#'
      if (at(Token_kind::pipe)) bs->add(leaf(Kind::reduction_or), Field::f_reduction);
      else if (at(Token_kind::amp)) bs->add(leaf(Kind::reduction_and), Field::f_reduction);
      else if (at(Token_kind::caret)) bs->add(leaf(Kind::reduction_xor), Field::f_reduction);
      else if (at(Token_kind::plus)) bs->add(leaf(Kind::reduction_popcount), Field::f_reduction);
      else if (at_kw(Keyword::kw_sext)) bs->add(leaf(Kind::sign_extend), Field::f_extension);
      else if (at_kw(Keyword::kw_zext)) bs->add(leaf(Kind::zero_extend), Field::f_extension);
      bs->add(parse_select(), Field::f_select);
      finish(bs, start);
      e = bs;
      continue;
    }
    if (at(Token_kind::coloncolon) && peek(1).kind == Token_kind::lbracket) {
      Ast* as  = node(Kind::attribute_set, start);
      e->field = Field::f_argument;
      as->add(e);
      advance();  // '::'
      as->add(parse_attribute_sq(), Field::f_attribute);
      finish(as, start);
      e = as;
      continue;
    }
    break;
  }
  return e;
}

Ast* Parser::try_generic_call(Ast* fn) {
  size_t save = pos_;
  advance();  // '<'
  Bracket_guard _bg(this);
  // generic_type_list: type (',' type)*, then '>' immediately followed by '('
  Ast* list = node(Kind::generic_type_list, cur().start_byte);
  bool ok    = true;
  while (at(Token_kind::comma)) advance();
  while (!at(Token_kind::gt) && !eof()) {
    Ast* ty = parse_type();
    if (!ty) {
      ok = false;
      break;
    }
    list->add(ty, Field::f_item);
    if (!accept(Token_kind::comma)) break;
    while (at(Token_kind::comma)) advance();
  }
  if (ok && at(Token_kind::gt) && peek(1).kind == Token_kind::lparen) {
    advance();  // '>'
    Ast* call = node(Kind::function_call_expression, fn->start_byte);
    fn->field = Field::f_function;
    call->add(fn);
    finish(list, list->start_byte);
    call->add(list, Field::f_generic);
    call->add(parse_arg_tuple(), Field::f_argument);
    finish(call, fn->start_byte);
    return call;
  }
  pos_ = save;  // not a generic call -> treat '<' as comparison
  return nullptr;
}

Ast* Parser::parse_atom() {
  const Token& t = cur();
  if (t.kind == Token_kind::lparen) return parse_paren();
  if (t.kind == Token_kind::lbracket) return parse_tuple_sq();
  // `true(...)` / `false(...)`: a bool literal is not callable, so here the word
  // is used as an identifier (call target) — tree-sitter's soft-keyword rule.
  bool bool_as_call = (t.is_kw(Keyword::kw_true) || t.is_kw(Keyword::kw_false)) &&
                      peek(1).kind == Token_kind::lparen;
  if (at_constant() && !bool_as_call) return parse_constant();
  if ((t.is_kw(Keyword::kw_if) || t.is_kw(Keyword::kw_unique)) && kw_is_construct_start())
    return parse_if_expression();
  if (t.is_kw(Keyword::kw_match) && kw_is_construct_start()) return parse_match_expression();
  if ((is_lambda_kind(t) || t.is_kw(Keyword::kw_pub)) && looks_like_lambda()) return parse_lambda();
  if (t.kind == Token_kind::ident) {
    Ast* id = leaf(Kind::identifier);
    if (at(Token_kind::at)) {  // timed_identifier: ident @[...]
      uint32_t start = id->start_byte;
      Ast*     ti    = node(Kind::timed_identifier, start);
      id->field      = Field::f_identifier;
      ti->add(id);
      advance();  // '@'
      ti->add(parse_timing_slot(), Field::f_timing);
      finish(ti, start);
      return ti;
    }
    return id;
  }
  error("expected-expression", "expected an expression");
}

Ast* Parser::parse_constant() {
  if (at(Token_kind::integer)) return leaf(Kind::integer_literal);
  if (at(Token_kind::string)) return leaf(Kind::string_literal);
  if (at(Token_kind::istring)) return leaf(Kind::interpolated_string_literal);
  if (at(Token_kind::question)) return leaf(Kind::unknown_literal);
  return leaf(Kind::bool_literal);  // true / false
}

Ast* Parser::parse_complex_identifier() { return parse_postfix(); }

Ast* Parser::parse_ref_identifier() {
  uint32_t start = cur().start_byte;
  advance();  // ref
  Ast* r = node(Kind::ref_identifier, start);
  r->add(parse_postfix());
  finish(r, start);
  return r;
}

// ===========================================================================
// Paren / tuple / arg-tuple classification
// ===========================================================================
Ast* Parser::parse_paren() {
  uint32_t start = cur().start_byte;
  advance();  // '('
  Bracket_guard _bg(this);
  while (at(Token_kind::comma)) advance();
  if (at(Token_kind::rparen)) {
    advance();
    Ast* tup = node(Kind::tuple, start);
    finish(tup, start);
    return tup;
  }
  bool plain = false;
  Ast* first = parse_tuple_item(&plain);
  if (plain && at(Token_kind::rparen)) {
    advance();
    Ast* pg = node(Kind::paren_group, start);
    pg->add(first);
    finish(pg, start);
    return pg;
  }
  Ast* tup   = node(Kind::tuple, start);
  first->field = Field::f_item;
  tup->add(first);
  while (accept(Token_kind::comma)) {
    while (at(Token_kind::comma)) advance();
    if (at(Token_kind::rparen)) break;
    tup->add(parse_tuple_item(), Field::f_item);
  }
  if (!at(Token_kind::rparen))
    error_unclosed("unclosed-paren", "expected ')' to close the tuple", "'(' opened here", start,
                   start + 1);
  advance();  // ')'
  finish(tup, start);
  return tup;
}

Ast* Parser::parse_tuple_sq() {
  uint32_t start = cur().start_byte;
  advance();  // '['
  Bracket_guard _bg(this);
  Ast* sq = node(Kind::tuple_sq, start);
  while (at(Token_kind::comma)) advance();
  while (!at(Token_kind::rbracket) && !eof()) {
    sq->add(parse_tuple_item(), Field::f_item);
    if (!accept(Token_kind::comma)) break;
    while (at(Token_kind::comma)) advance();
  }
  if (!at(Token_kind::rbracket))
    error_unclosed("unclosed-bracket", "expected ']' to close the array", "'[' opened here", start,
                   start + 1);
  advance();  // ']'
  finish(sq, start);
  return sq;
}

Ast* Parser::parse_tuple_item(bool* plain) {
  if (plain) *plain = false;
  uint32_t start = cur().start_byte;

  if (at_kw(Keyword::kw_ref)) return parse_ref_identifier();
  if ((is_lambda_kind(cur()) || at_kw(Keyword::kw_pub)) && looks_like_lambda()) return parse_lambda();

  if (is_decl_keyword(cur())) {
    Ast* decl = parse_var_or_let_or_reg();
    Ast* lv   = parse_expression();  // a, a.b, a[i], 3, ...
    Ast* tc   = nullptr;
    if ((at(Token_kind::colon) || at(Token_kind::coloncolon)) && lv->kind == Kind::identifier)
      tc = parse_type_cast();
    if (at_assignment_operator()) return finish_assignment(start, nullptr, decl, lv, tc);
    // no '=' : decl + typed_identifier (named field) | decl + expression (positional)
    if (tc || lv->kind == Kind::identifier) {
      Ast* ti  = node(Kind::typed_identifier, lv->start_byte);
      lv->field = Field::f_identifier;
      ti->add(lv);
      if (tc) ti->add(tc, Field::f_type);
      finish(ti, lv->start_byte);
      decl->add(ti, Field::f_lvalue);
    } else {
      decl->add(lv, Field::f_value);
    }
    finish(decl, start);
    return decl;
  }

  Ast* e  = parse_expression();
  Ast* tc = nullptr;
  if ((at(Token_kind::colon) || at(Token_kind::coloncolon)) && e->kind == Kind::identifier)
    tc = parse_type_cast();
  if (at_assignment_operator()) return finish_assignment(start, nullptr, nullptr, e, tc);
  if (tc) {
    // typed_field: name : type
    Ast* tf  = node(Kind::typed_field, start);
    e->field = Field::f_identifier;
    tf->add(e);
    tc->field = Field::f_type;
    tf->add(tc);
    finish(tf, start);
    return tf;
  }
  if (plain) *plain = true;
  return e;
}

Ast* Parser::parse_lvalue_item() {
  uint32_t start = cur().start_byte;
  // named_lvalue: name '=' (typed_identifier | dot_expression)
  if (cur().kind == Token_kind::ident && peek(1).kind == Token_kind::assign) {
    Ast* name = leaf(Kind::identifier);
    advance();  // '='
    Ast* lv = parse_postfix();
    if (lv->kind == Kind::identifier) {
      Ast* ti   = node(Kind::typed_identifier, lv->start_byte);
      lv->field = Field::f_identifier;
      ti->add(lv);
      if (at(Token_kind::colon) || at(Token_kind::coloncolon)) ti->add(parse_type_cast(), Field::f_type);
      finish(ti, lv->start_byte);
      lv = ti;
    }
    Ast* nl   = node(Kind::named_lvalue, start);
    name->field = Field::f_name;
    nl->add(name);
    lv->field = Field::f_lvalue;
    nl->add(lv);
    finish(nl, start);
    return nl;
  }
  Ast* e  = parse_postfix();
  Ast* tc = nullptr;
  if (at(Token_kind::colon) || at(Token_kind::coloncolon)) tc = parse_type_cast();
  if (e->kind == Kind::identifier) {
    Ast* ti   = node(Kind::typed_identifier, e->start_byte);
    e->field = Field::f_identifier;
    ti->add(e);
    if (tc) ti->add(tc, Field::f_type);
    finish(ti, e->start_byte);
    return ti;
  }
  Ast* li  = node(Kind::lvalue_item, start);
  e->field = Field::f_identifier;
  li->add(e);
  if (tc) li->add(tc, Field::f_type);
  finish(li, start);
  return li;
}

Ast* Parser::parse_arg_tuple() {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lparen, "expected-paren", "expected '(' to open arguments");
  Bracket_guard _bg(this);
  Ast* at_ = node(Kind::arg_tuple, start);
  while (at(Token_kind::comma)) advance();
  while (!at(Token_kind::rparen) && !eof()) {
    at_->add(parse_arg_item(), Field::f_item);
    if (!accept(Token_kind::comma)) break;
    while (at(Token_kind::comma)) advance();
  }
  if (!at(Token_kind::rparen))
    error_unclosed("unclosed-paren", "expected ')' to close arguments", "'(' opened here", start,
                   start + 1);
  advance();  // ')'
  finish(at_, start);
  return at_;
}

Ast* Parser::parse_arg_item() {
  if (at_kw(Keyword::kw_ref)) return parse_ref_identifier();
  uint32_t start = cur().start_byte;
  Ast*     e = parse_expression();
  if (at(Token_kind::assign)) {
    advance();  // '='
    Ast* aa  = node(Kind::arg_assignment, start);
    e->field = Field::f_lvalue;
    aa->add(e);
    if (at_kw(Keyword::kw_ref)) aa->add(parse_ref_identifier(), Field::f_rvalue);
    else aa->add(parse_expression(), Field::f_rvalue);
    finish(aa, start);
    return aa;
  }
  return e;
}

// ===========================================================================
// if / match
// ===========================================================================
Ast* Parser::parse_if_expression() {
  uint32_t start = cur().start_byte;
  Ast*     ife = node(Kind::if_expression, start);
  accept_kw(Keyword::kw_unique);
  if (!accept_kw(Keyword::kw_if)) error("expected-if", "expected 'if'");
  // first branch (inline, no wrapper node): init?, condition, code
  {
    std::vector<Ast*> items;
    items.push_back(parse_tuple_item());
    while (at(Token_kind::semicolon)) {
      advance();
      while (at(Token_kind::semicolon)) advance();
      if (at(Token_kind::lbrace)) break;
      items.push_back(parse_tuple_item());
    }
    Ast* cond = items.back();
    items.pop_back();
    if (!items.empty()) {
      Ast* sl = node(Kind::stmt_list, items.front()->start_byte);
      for (Ast* it : items) sl->add(it, Field::f_item);
      finish(sl, sl->start_byte);
      ife->add(sl, Field::f_init);
    }
    cond->field = Field::f_condition;
    ife->add(cond);
    ife->add(parse_scope(), Field::f_code);
  }
  while (at_kw(Keyword::kw_elif)) {
    advance();
    std::vector<Ast*> items;
    items.push_back(parse_tuple_item());
    while (at(Token_kind::semicolon)) {
      advance();
      while (at(Token_kind::semicolon)) advance();
      if (at(Token_kind::lbrace)) break;
      items.push_back(parse_tuple_item());
    }
    Ast* cond = items.back();
    items.pop_back();
    if (!items.empty()) {
      Ast* sl = node(Kind::stmt_list, items.front()->start_byte);
      for (Ast* it : items) sl->add(it, Field::f_item);
      finish(sl, sl->start_byte);
      ife->add(sl, Field::f_elif);
    }
    cond->field = Field::f_elif;
    ife->add(cond);
    ife->add(parse_scope(), Field::f_elif);
  }
  if (accept_kw(Keyword::kw_else)) {
    ife->add(parse_scope(), Field::f_else);
  }
  finish(ife, start);
  return ife;
}

Ast* Parser::parse_match_expression() {
  uint32_t start = cur().start_byte;
  advance();  // match
  Ast* m = node(Kind::match_expression, start);
  // optional init then condition
  std::vector<Ast*> items;
  items.push_back(parse_tuple_item());
  while (at(Token_kind::semicolon)) {
    advance();
    while (at(Token_kind::semicolon)) advance();
    if (at(Token_kind::lbrace)) break;
    items.push_back(parse_tuple_item());
  }
  Ast* cond = items.back();
  items.pop_back();
  if (!items.empty()) {
    Ast* sl = node(Kind::stmt_list, items.front()->start_byte);
    for (Ast* it : items) sl->add(it, Field::f_item);
    finish(sl, sl->start_byte);
    m->add(sl, Field::f_init);
  }
  cond->field = Field::f_condition;
  m->add(cond);
  expect(Token_kind::lbrace, "expected-brace", "expected '{' to open match body");
  while (!at(Token_kind::rbrace) && !eof() && !at_kw(Keyword::kw_else)) {
    // optional leading case operator
    switch (cur().kind) {
      case Token_kind::amp:
      case Token_kind::caret:
      case Token_kind::pipe:
      case Token_kind::lt:
      case Token_kind::le:
      case Token_kind::gt:
      case Token_kind::ge:
      case Token_kind::eq:
      case Token_kind::ne:
        advance();
        break;
      default:
        if (cur().is_kw(Keyword::kw_and) || cur().is_kw(Keyword::kw_or) ||
            cur().is_kw(Keyword::kw_has) || cur().is_kw(Keyword::kw_case) ||
            cur().is_kw(Keyword::kw_in) || cur().is_kw(Keyword::kw_equals) ||
            cur().is_kw(Keyword::kw_does) || cur().is_kw(Keyword::kw_is))
          advance();
        break;
    }
    m->add(parse_expression(), Field::f_condition);
    m->add(parse_scope(), Field::f_code);
  }
  if (!accept_kw(Keyword::kw_else)) error("expected-else", "expected 'else' clause in match");
  m->add(parse_scope(), Field::f_else_code);
  expect(Token_kind::rbrace, "unclosed-scope", "expected '}' to close match");
  finish(m, start);
  return m;
}

// ===========================================================================
// Types
// ===========================================================================
Ast* Parser::parse_type_cast() {
  uint32_t start = cur().start_byte;
  Ast*     tc = node(Kind::type_cast, start);
  if (at(Token_kind::coloncolon)) {  // attribute-only: ::[...]
    advance();
    tc->add(parse_attribute_sq(), Field::f_attribute);
    finish(tc, start);
    return tc;
  }
  expect(Token_kind::colon, "expected-colon", "expected ':' for type annotation");
  tc->add(parse_type(), Field::f_type);
  if (at(Token_kind::at)) {
    advance();
    tc->add(parse_timing_slot(), Field::f_timing);
  }
  if (at(Token_kind::colon) && peek(1).kind == Token_kind::lbracket) {  // :Type:[attr]
    advance();
    tc->add(parse_attribute_sq(), Field::f_attribute);
  }
  finish(tc, start);
  return tc;
}

Ast* Parser::parse_type() {
  if (at(Token_kind::lbracket)) {
    // array_type: array_length [base]
    uint32_t start = cur().start_byte;
    Ast*     at_   = node(Kind::array_type, start);
    at_->add(parse_select(), Field::f_length);  // reuse [..]; length allows empty -> handled below
    // base (optional) — must not cross a statement terminator (a new line after
    // `[]` begins the next statement, it is not the array's base type).
    if (!term_stop()) {
      if (at(Token_kind::lbracket)) at_->add(parse_type(), Field::f_base);
      else if (is_primitive_type_word(cur())) at_->add(parse_primitive_type(), Field::f_base);
      else if (is_lambda_kind(cur()) && looks_like_lambda()) at_->add(parse_lambda(), Field::f_base);
      else if (cur().kind == Token_kind::ident || at(Token_kind::lparen) || at_constant() ||
               at_kw(Keyword::kw_if) || at_kw(Keyword::kw_match))
        at_->add(parse_type(), Field::f_base);
    }
    finish(at_, start);
    return at_;
  }
  if (at(Token_kind::at)) {  // bare timing sequence as a type
    uint32_t start = cur().start_byte;
    advance();
    Ast* ts = parse_timing_slot();
    ts->start_byte = start;
    return ts;
  }
  if (is_primitive_type_word(cur())) return parse_primitive_type();
  // expression_type: identifier (dotted), constant, tuple, if/match, call
  uint32_t start = cur().start_byte;
  if (at(Token_kind::lparen)) return parse_paren();  // tuple type
  if (at_constant()) return parse_constant();
  if (at_kw(Keyword::kw_if) || at_kw(Keyword::kw_unique)) return parse_if_expression();
  if (at_kw(Keyword::kw_match)) return parse_match_expression();
  if (cur().kind == Token_kind::ident) {
    Ast* et = node(Kind::expression_type, start);
    Ast* id = leaf(Kind::identifier);
    et->add(id);
    while (at(Token_kind::dot) && peek(1).kind == Token_kind::ident) {
      advance();
      et->add(leaf(Kind::identifier), Field::f_item);
    }
    if (at(Token_kind::lparen)) {
      // function_call_type: name ( tuple )
      Ast* fct = node(Kind::function_call_type, start);
      et->field = Field::f_function;
      fct->add(et);
      fct->add(parse_paren(), Field::f_argument);
      finish(fct, start);
      return fct;
    }
    finish(et, start);
    return et;
  }
  error("expected-type", "expected a type");
}

Ast* Parser::parse_primitive_type() {
  uint32_t     start = cur().start_byte;
  const Token& t = cur();
  Kind         k;
  if (t.is_kw(Keyword::kw_bool)) {
    Ast* b = node(Kind::bool_type, start);
    advance();
    finish(b, start);
    return b;
  }
  if (t.is_kw(Keyword::kw_string)) {
    Ast* s = node(Kind::string_type, start);
    advance();
    finish(s, start);
    return s;
  }
  if (t.is_kw(Keyword::kw_uint) || t.is_kw(Keyword::kw_unsigned) ||
      (t.text.size() >= 2 && t.text[0] == 'u'))
    k = Kind::uint_type;
  else
    k = Kind::sint_type;
  Ast* pt = node(k, start);
  advance();
  if (at(Token_kind::lparen)) pt->add(parse_paren(), Field::f_constraint);
  finish(pt, start);
  return pt;
}

Ast* Parser::parse_typed_identifier() {
  uint32_t start = cur().start_byte;
  Ast*     ti = node(Kind::typed_identifier, start);
  ti->add(leaf(Kind::identifier), Field::f_identifier);
  if (at(Token_kind::at)) {
    advance();
    ti->add(parse_timing_slot(), Field::f_timing);
  }
  if (at(Token_kind::colon) || at(Token_kind::coloncolon)) ti->add(parse_type_cast(), Field::f_type);
  finish(ti, start);
  return ti;
}

Ast* Parser::parse_typed_identifier_list() {
  uint32_t start = cur().start_byte;
  Ast*     list = node(Kind::typed_identifier_list, start);
  while (at(Token_kind::comma)) advance();
  list->add(parse_typed_identifier(), Field::f_item);
  while (accept(Token_kind::comma)) {
    while (at(Token_kind::comma)) advance();
    if (at(Token_kind::rparen) || at(Token_kind::gt) || at(Token_kind::rbracket)) break;
    list->add(parse_typed_identifier(), Field::f_item);
  }
  finish(list, start);
  return list;
}

Ast* Parser::parse_attribute_sq() {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lbracket, "expected-bracket", "expected '[' to open attributes");
  Bracket_guard _bg(this);
  Ast* sq = node(Kind::attribute_sq, start);
  while (at(Token_kind::comma)) advance();
  while (!at(Token_kind::rbracket) && !eof()) {
    // _attribute_item: _expression | ref_identifier | attribute_assignment
    if (at_kw(Keyword::kw_ref)) {
      sq->add(parse_ref_identifier(), Field::f_item);
    } else {
      Ast* e = parse_expression();
      if (at(Token_kind::assign)) {
        advance();
        Ast* aa  = node(Kind::attribute_assignment, e->start_byte);
        e->field = Field::f_lvalue;
        aa->add(e);
        if (at_kw(Keyword::kw_ref)) aa->add(parse_ref_identifier(), Field::f_rvalue);
        else aa->add(parse_expression(), Field::f_rvalue);
        finish(aa, e->start_byte);
        sq->add(aa, Field::f_item);
      } else {
        sq->add(e, Field::f_item);
      }
    }
    if (!accept(Token_kind::comma)) break;
    while (at(Token_kind::comma)) advance();
  }
  expect(Token_kind::rbracket, "unclosed-bracket", "expected ']' to close attributes");
  finish(sq, start);
  return sq;
}

Ast* Parser::parse_attribute_list() {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lbracket, "expected-bracket", "expected '[' for attribute");
  Ast* al = node(Kind::attribute_list, start);
  al->add(leaf(Kind::identifier), Field::f_name);
  expect(Token_kind::rbracket, "unclosed-bracket", "expected ']'");
  finish(al, start);
  return al;
}

Ast* Parser::parse_select() {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lbracket, "expected-bracket", "expected '['");
  Bracket_guard _bg(this);
  Ast* sel = node(Kind::select, start);
  if (!at(Token_kind::rbracket)) {
    if (at(Token_kind::dotdot) || at(Token_kind::range_incl) || at(Token_kind::range_excl)) {
      sel->add(parse_selection_range_or_index(Kind::select), Field::f_range);
    } else {
      Ast* e = parse_expression();
      if (at(Token_kind::dotdot)) {  // open_from: expr ..
        Ast* sr  = node(Kind::selection_range, e->start_byte);
        e->field = Field::f_open_from;
        sr->add(e);
        advance();  // '..'
        finish(sr, e->start_byte);
        sel->add(sr, Field::f_range);
      } else {
        sel->add(e, Field::f_index);
      }
    }
  }
  expect(Token_kind::rbracket, "unclosed-bracket", "expected ']'");
  finish(sel, start);
  return sel;
}

Ast* Parser::parse_selection_range_or_index(Kind /*container*/) {
  uint32_t start = cur().start_byte;
  Ast*     sr = node(Kind::selection_range, start);
  if (at(Token_kind::dotdot)) {
    sr->add(leaf(Kind::open_all));
  } else if (at(Token_kind::range_incl)) {
    advance();
    sr->add(parse_expression(), Field::f_from_zero_inclusive);
  } else if (at(Token_kind::range_excl)) {
    advance();
    sr->add(parse_expression(), Field::f_from_zero_exclusive);
  }
  finish(sr, start);
  return sr;
}

Ast* Parser::parse_timing_slot() {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lbracket, "expected-bracket", "expected '[' for timing");
  Bracket_guard _bg(this);
  Ast* ts = node(Kind::timing_slot, start);
  if (!at(Token_kind::rbracket)) {
    if (at(Token_kind::dotdot) || at(Token_kind::range_incl) || at(Token_kind::range_excl)) {
      ts->add(parse_selection_range_or_index(Kind::timing_slot), Field::f_range);
    } else {
      Ast* e = parse_expression();
      if (at(Token_kind::dotdot)) {
        Ast* sr  = node(Kind::selection_range, e->start_byte);
        e->field = Field::f_open_from;
        sr->add(e);
        advance();
        finish(sr, e->start_byte);
        ts->add(sr, Field::f_range);
      } else {
        ts->add(e, Field::f_index);
      }
    }
  }
  expect(Token_kind::rbracket, "unclosed-bracket", "expected ']' for timing");
  finish(ts, start);
  return ts;
}

Ast* Parser::parse_stmt_list() {
  uint32_t start = cur().start_byte;
  Ast*     sl = node(Kind::stmt_list, start);
  sl->add(parse_tuple_item(), Field::f_item);
  while (at(Token_kind::semicolon)) {
    advance();
    while (at(Token_kind::semicolon)) advance();
    if (at(Token_kind::lbrace) || eof()) break;
    sl->add(parse_tuple_item(), Field::f_item);
  }
  finish(sl, start);
  return sl;
}

Ast* Parser::parse_init_clause() { return parse_stmt_list(); }

}  // namespace prpparse
