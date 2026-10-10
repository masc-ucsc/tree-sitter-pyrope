// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "parser.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace prpparse {

namespace {
bool g_phase_timing = false;
}

void Parser::set_phase_timing(bool on) { g_phase_timing = on; }

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
    case Token_kind::concat:      return Kind::op_tuple_concat;
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
    case Token_kind::assign_concat:  return Kind::assign_tuple_concat;
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
    case Token_kind::lparen:
    case Token_kind::lbracket:
    case Token_kind::lbrace:
    case Token_kind::bang:
    case Token_kind::tilde:
    case Token_kind::minus:
    case Token_kind::ellipsis:
    case Token_kind::ident:  // identifier, or a keyword-led operand (not/if/...)
    case Token_kind::type_word:  // `U8`, `U8(x)` as a value
      return true;
    default:
      return false;
  }
}

// Precedence-tier wrapper kind for a binary operator kind. tree-sitter wraps
// each operator token in a tier node (binary_times_op / binary_other_op /
// binary_step_op / binary_compare_op / binary_logical_op); the LiveHD consumer
// (prp2lnast) reads the operator's precedence tier from this wrapper, so the
// materialized tree must reproduce it.
Kind op_tier(Kind op) {
  switch (op) {
    case Kind::op_mul:
    case Kind::op_div:
    case Kind::op_mod:
      return Kind::binary_times_op;
    case Kind::op_add:
    case Kind::op_sub:
    case Kind::op_tuple_concat:
    case Kind::op_shl:
    case Kind::op_sra:
    case Kind::op_bit_and:
    case Kind::op_bit_or:
    case Kind::op_bit_xor:
    case Kind::op_range_inclusive:
    case Kind::op_range_exclusive:
    case Kind::op_range_count:
      return Kind::binary_other_op;
    case Kind::op_step:
      return Kind::binary_step_op;
    case Kind::op_lt:
    case Kind::op_le:
    case Kind::op_gt:
    case Kind::op_ge:
    case Kind::op_eq:
    case Kind::op_ne:
    case Kind::op_has:
    case Kind::op_in:
    case Kind::op_case:
    case Kind::op_does:
    case Kind::op_equals:
      return Kind::binary_compare_op;
    case Kind::op_log_and:
    case Kind::op_log_or:
    case Kind::op_implies:
      return Kind::binary_logical_op;
    default:
      return Kind::invalid;
  }
}

// Build the `operator` child for a binary expression: a tier wrapper node
// (`binary_*_op`) whose single child is the operator-kind leaf (`op_add`, …),
// both spanning the operator token [s, e). Matches tree-sitter's CST.
Ast* make_binop(Ast_arena& arena, Kind opk, uint32_t s, uint32_t e) {
  Ast* inner = arena.make(opk, s, e);
  Kind tier  = op_tier(opk);
  if (tier == Kind::invalid) return inner;  // defensive: emit the bare op
  Ast* w = arena.make(tier, s, e);
  w->add(inner);
  return w;
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

bool Parser::starts_ident_use(const Token& t) const {
  if (assign_kind(t.kind) != Kind::invalid) return true;
  switch (t.kind) {
    case Token_kind::lbracket:    // `stage[0]`, `reg[3]`
    case Token_kind::dot:         // `in.bits`
    case Token_kind::hash:        // `type#[0]`
    case Token_kind::at:          // `step@[1]`
    case Token_kind::colon:       // `mut:u8`
    case Token_kind::coloncolon:  // `wire::[attr]`
      return true;
    default:
      return false;
  }
}

// The backtick escape IS how Pyrope spells an identifier that collides with a
// keyword ("Using the backtick, Pyrope can use any string as an identifier,
// even reserved keywords" -- docs/pyrope/02-basics.md), and any sequence may sit
// between the backticks. Say so on the error, because the caret alone never
// points there: `stage[0] = a` (a Verilog shift register named `stage` -- see
// bedrock's br_delay_valid) reports "expected an expression" ON THE `=`.
void Parser::set_kw_hint(Diag& d, const Token& kw) const {
  d.hint = std::string("wrap it in backticks (`") + std::string(kw.text)
           + "`) to use it as an identifier -- that is how Pyrope spells a name whose text "
             "collides with a keyword";
  d.notes.push_back(Note{std::string("'") + std::string(kw.text) + "' is reserved",
                         span_bytes(kw.start_byte, kw.end_byte)});
}

void Parser::add_kw_as_ident_hint(Diag& d) const {
  if (kw_as_ident_ == kNoKw || kw_as_ident_ >= toks_.size()) return;
  set_kw_hint(d, toks_[kw_as_ident_]);
}

// A NAME being bound -- a declaration, a parameter -- must be a plain identifier.
// A reserved word is legal there only in its BACKTICKED form, and the lexer makes
// the test exact: a backticked word is an ident token with `kw == Keyword::none`.
// Verilog imports make this routine. A design with a signal named `in` or `reg`
// becomes `` `in` `` / `` `reg` `` in Pyrope (and `in` is not even a Verilog
// reserved word, so it comes back out of cgen as a bare `in`), which is exactly
// why someone hand-writing the Pyrope reaches for the unquoted spelling. Bound
// bare it used to declare a name nothing else in the file could refer to.
void Parser::require_plain_name(const char* role) const {
  const Token& t = cur();
  if (t.kind == Token_kind::type_word) error_type_word_name(t, role);
  if (t.kind != Token_kind::ident || t.text.starts_with('`')) return;
  // Exact-spelling rule (owner ruling 2026-09-30): only the EXACT text of a
  // keyword or `nil` is reserved; `IF`, `TiCk`, `clock`, `reset` are plain names.
  // The type words arrive as Token_kind::type_word (handled above); the old
  // lowercase type spellings (`u8`, `s2`, `bool`, ...) are ordinary names (owner
  // ruling 2026-09-30) and pass through here like any other identifier.
  if (classify_keyword(t.text) == Keyword::none && t.text != "nil") return;
  error_reserved_name(t, "reserved-word-as-name",
                      "'" + std::string(t.text) + "' is reserved, so it cannot be " + role);
}

// The type words (`U<N>`, `S<N>`, `Unsigned`, `Signed`, `Bool`, `String`,
// `Clock`, `Reset`) are reserved: a bare `U4` is always the type, so it can not
// name a variable, port, parameter or field. The backticked `` `U4` `` is an
// ordinary name (never the type) in every position.
void Parser::error_type_word_name(const Token& t, const std::string& role) const {
  Diag d;
  d.code     = "reserved-type-name";
  d.category = std::string(kCategorySyntax);
  d.message  = "'" + std::string(t.text) + "' is a reserved type word, so it cannot be " + role;
  d.span     = span_bytes(t.start_byte, t.end_byte);
  set_kw_hint(d, t);
  throw Parse_error(std::move(d));
}

void Parser::reject_type_word_name(const Ast* e, const char* role) const {
  if (!e || e->kind != Kind::identifier) return;
  std::string_view text(buf_.data() + e->start_byte, e->end_byte - e->start_byte);
  if (!is_type_word(text) && text != "nil") return;  // a plain or backticked name
  Token t;
  t.kind       = Token_kind::type_word;
  t.start_byte = e->start_byte;
  t.end_byte   = e->end_byte;
  t.text       = text;
  if (text == "nil")
    error_reserved_name(t, "reserved-word-as-name", "'nil' is a literal, so it cannot be " + std::string(role));
  error_type_word_name(t, role);
}

namespace {
bool is_lvalue_kind(Kind k) {
  switch (k) {
    case Kind::identifier:
    case Kind::typed_identifier:
    case Kind::dot_expression:
    case Kind::member_selection:
    case Kind::bit_selection:
    case Kind::attribute_read:
    case Kind::timed_identifier:
      return true;
    default:
      return false;
  }
}
}  // namespace

// The head of an assignment target's suffix chain: `a` in `a.b[1]#[2]`,
// `a@[1]`, `a.[x]`. A target is rooted at a NAME (spec 2026-09-29 §7):
// `f(x).a = 3`, `(a+b).c = 3`, `f(x)[0] = 3`, `true.x = 1` are errors.
static const Ast* lvalue_root(const Ast* x) {
  while (x && !x->kids.empty()) {
    switch (x->kind) {
      case Kind::dot_expression:
      case Kind::member_selection:
      case Kind::bit_selection:
      case Kind::attribute_read:
      case Kind::attribute_set:
      case Kind::timed_identifier:
      case Kind::typed_identifier:
        x = x->kids.front();
        continue;
      default:
        return x;
    }
  }
  return x;
}

void Parser::require_lvalue(const Ast* e) const {
  const Ast* x = e;
  // `a::[attr] = v`: the grammar reads the `::[attr]` as the target's type_cast.
  if (x && x->kind == Kind::attribute_set && !x->kids.empty()) x = x->kids.front();
  if (x && is_lvalue_kind(x->kind)) {
    const Ast* root = lvalue_root(x);
    if (root && root->kind == Kind::identifier) {
      reject_type_word_name(root, "an assignment target");  // `U8 = 3`, `U8.[max] = 1`
      return;
    }
  }
  Diag d;
  d.code     = "bad-assignment-target";
  d.category = std::string(kCategorySyntax);
  d.message  = "this cannot be assigned: an assignment target is a name, or a field, a selector or a bit-select"
               " of a name (or, as a statement, a destructuring `(a, b) = ...` of names)";
  d.span     = e ? span_bytes(e->start_byte, e->end_byte) : span_bytes(cur().start_byte, cur().end_byte);
  throw Parse_error(std::move(d));
}

void Parser::require_condition(const Ast* c) const {
  switch (c->kind) {
    case Kind::assignment:
    case Kind::var_or_let_or_reg:
    case Kind::typed_field:
    case Kind::ref_identifier: {
      Diag d;
      d.code     = "expected-condition";
      d.category = std::string(kCategorySyntax);
      d.message  = "expected a condition expression (an assignment or declaration belongs in the init clause: "
                   "`if x = f(); x > 3 { ... }`)";
      d.span     = span_bytes(c->start_byte, c->end_byte);
      throw Parse_error(std::move(d));
    }
    default:
      return;
  }
}

void Parser::require_binding_name(const Ast* e, bool dotted, const char* role) const {
  if (e && (e->kind == Kind::identifier || (dotted && e->kind == Kind::dot_expression))) {
    reject_type_word_name(e, role);
    return;
  }
  Diag d;
  d.code     = "bad-assignment-target";
  d.category = std::string(kCategorySyntax);
  d.message  = std::string("expected a name before '=' (") + role + ")";
  d.span     = e ? span_bytes(e->start_byte, e->end_byte) : span_bytes(cur().start_byte, cur().end_byte);
  throw Parse_error(std::move(d));
}

void Parser::error_reserved_name(const Token& kw, const char* code,
                                 const std::string& message) const {
  const Token& t = cur();
  Diag         d;
  d.code     = code;
  d.category = std::string(kCategorySyntax);
  d.message  = message;
  d.span     = span_bytes(t.start_byte, t.end_byte);
  set_kw_hint(d, kw);
  throw Parse_error(std::move(d));
}

// The expression form `enum(a, b)` was removed (spec 2026-09-29 §7): an enum
// is only the declaration `enum E = (a, b)` / `enum E:T = (a, b)`.
void Parser::error_enum_expression() const {
  Diag d;
  d.code     = "enum-expression";
  d.category = std::string(kCategorySyntax);
  d.message  = "the expression form `enum(...)` was removed: declare the enum as `enum E = (a, b)`";
  d.span     = span_bytes(cur().start_byte, cur().end_byte);
  throw Parse_error(std::move(d));
}

const Token& Parser::blame() const {
  if (pos_ > stmt_start_ && (eof() || term_stop())) return toks_[pos_ - 1];
  return cur();
}

void Parser::error(const char* code, const std::string& message) const {
  const Token& t = blame();
  Diag         d;
  d.code     = code;
  d.category = std::string(kCategorySyntax);
  d.message  = message;
  d.span     = span_bytes(t.start_byte, t.end_byte);
  add_kw_as_ident_hint(d);
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
  add_kw_as_ident_hint(d);
  throw Parse_error(std::move(d));
}

const Token& Parser::expect(Token_kind k, const char* code, const std::string& what) {
  if (at(k)) return advance();
  // error() blames the statement's last token when the statement ended early
  // (see blame()), so name what came next the same way.
  std::string found = eof()                ? "the end of input"
                      : &blame() != &cur() ? "the end of the line"
                                           : "'" + std::string(cur().text) + "'";
  error(code, what + " (found " + found + ")");
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
    case Keyword::kw_wire:
    case Keyword::kw_stage:
      return true;
    default:
      return false;
  }
}
bool Parser::is_array_length_keyword(const Token& t) const {
  if (t.kind != Token_kind::ident) return false;
  switch (t.kw) {
    case Keyword::kw_const:
    case Keyword::kw_mut:
    case Keyword::kw_reg:
    case Keyword::kw_wire:
    case Keyword::kw_stage:
    case Keyword::kw_comptime:
    case Keyword::kw_ref:
      return true;
    default:
      return false;
  }
}
void Parser::skip_leading_commas(Token_kind close) {
  if (!at(Token_kind::comma)) return;
  while (at(Token_kind::comma)) advance();
  if (at(close))
    error("empty-list", "a list of only commas is not allowed (an empty list is written with no comma: `()`, `[]`)");
}
bool Parser::is_lambda_kind(const Token& t) const {
  if (t.kind != Token_kind::ident) return false;
  return t.kw == Keyword::kw_comb || t.kw == Keyword::kw_mod || t.kw == Keyword::kw_pipe ||
         t.kw == Keyword::kw_fluid;
}
bool Parser::at_assignment_operator() const { return assign_kind(cur().kind) != Kind::invalid; }
bool Parser::is_keyword_name(const Ast* e) const {
  if (!e || e->kind != Kind::identifier || e->end_byte <= e->start_byte) return false;
  std::string_view text(buf_.data() + e->start_byte, e->end_byte - e->start_byte);
  return text.front() != '`' && classify_keyword(text) != Keyword::none;
}
// Keywords that start a value (grammar.js `_expression`): `if`/`unique`/
// `match`, `not`, the literals `true`/`false`, and a lambda (`comb`, `mod`,
// `pipe`, `fluid`, `pub`). Where a value may start, these are never a name.
static bool is_value_start_kw(const Token& t) {
  if (t.kind != Token_kind::ident) return false;
  switch (t.kw) {
    case Keyword::kw_if:
    case Keyword::kw_unique:
    case Keyword::kw_match:
    case Keyword::kw_not:
    case Keyword::kw_true:
    case Keyword::kw_false:
    case Keyword::kw_comb:
    case Keyword::kw_mod:
    case Keyword::kw_pipe:
    case Keyword::kw_fluid:
    case Keyword::kw_pub:
      return true;
    default:
      return false;
  }
}

bool Parser::at_field_word() const {
  const Token& t = cur();
  if (t.kind != Token_kind::ident || t.kw == Keyword::none) return false;
  const Token_kind n = peek(1).kind;
  return n == Token_kind::assign || n == Token_kind::colon || n == Token_kind::coloncolon;
}
bool Parser::at_constant() const {
  switch (cur().kind) {
    case Token_kind::integer:
    case Token_kind::string:
    case Token_kind::istring:
      return true;
    default:
      break;
  }
  return cur().is_kw(Keyword::kw_true) || cur().is_kw(Keyword::kw_false);
}
bool Parser::is_primitive_type_word(const Token& t) const {
  // U<N> / S<N> / Unsigned / Signed / Bool / String / Clock / Reset: the
  // reserved type words (token.hpp is_type_word).
  return t.kind == Token_kind::type_word;
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
Ast* Parser::parse_ast() {
  Ast* root = parse_description();
  link_parents(root);  // the node facade navigates parent / next-sibling
  return root;
}

Ast* Parser::parse_next() {
  // Skip statement separators between top-level constructs (parse_description
  // does the same between iterations of its loop).
  while (at(Token_kind::semicolon)) advance();
  if (eof()) {
    return nullptr;
  }
  // Recycle the previous construct's nodes before parsing the next one. Every
  // Ast* returned by an earlier parse_next() is invalidated here — the consumer
  // must have fully lowered it already.
  arena_.reset();
  Ast* s = parse_statement();
  link_parents(s);  // navigation (parent / next-sibling) is within this construct
  return s;
}

Prp_tree& Parser::parse() {
  if (!g_phase_timing) {
    Ast* root = parse_description();
    tree_.materialize(root, arena_.size());
    return tree_;
  }
  const auto t0   = std::chrono::steady_clock::now();
  Ast*       root = parse_description();
  const auto t1   = std::chrono::steady_clock::now();
  tree_.materialize(root, arena_.size());
  const auto t2 = std::chrono::steady_clock::now();
  const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  std::fprintf(stderr, "[phase] parse_ast=%.1fms materialize=%.1fms nodes=%zu spans=%zu\n", ms(t0, t1), ms(t1, t2),
               arena_.size(), tree_.span_count());
  return tree_;
}

Ast* Parser::parse_description() {
  Ast* root = node(Kind::description, 0);
  // A `;` means a newline (02-basics "Semicolons"): a run of them may also lead
  // the file or be all of it (grammar.js `description`).
  while (at(Token_kind::semicolon)) advance();
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

  // Arm the reserved-word-as-identifier hint for this statement (restored on the
  // way out, so a nested statement's keyword never leaks to the enclosing one).
  Kw_as_ident_guard _kg(
      *this, (t.kind == Token_kind::ident && t.kw != Keyword::none && starts_ident_use(peek(1)))
                 ? pos_
                 : kNoKw);
  Stmt_start_guard _stg(*this);

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
      case Keyword::kw_tick:  return parse_tick_statement();
      case Keyword::kw_step:  return parse_step_statement();
      case Keyword::kw_test:  return parse_test();
      case Keyword::kw_formal: return parse_formal();
      case Keyword::kw_type:  return parse_type_statement();
      case Keyword::kw_pub:
        // `pub type X = …` — an exportable type alias (same pub_modifier field
        // shape as lambdas/data declares). Any other `pub …` keeps the existing
        // lambda / declaration paths below.
        if (peek(1).is_kw(Keyword::kw_type)) return parse_type_statement();
        break;
      case Keyword::kw_impl:  return parse_impl();
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
  Scope_guard _sg(*this);
  Ast* sc = node(Kind::scope_statement, start);
  if (at(Token_kind::coloncolon)) {  // scope attributes: { ::[abc="...", color=N] stmts }
    advance();
    sc->add(parse_attribute_sq(), Field::f_attributes);
  }
  while (at(Token_kind::semicolon)) advance();  // `{ ; x }`: a `;` means a newline
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
  items.push_back(parse_stmt_item());
  while (at(Token_kind::semicolon)) {
    advance();
    while (at(Token_kind::semicolon)) advance();
    if (at(Token_kind::lbrace)) break;
    items.push_back(parse_stmt_item());
  }
  Ast* cond = items.back();
  require_condition(cond);
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
  // `for const k = 2; i in 0..<k {}`: owner ruling 122 adds an init clause
  // like `if`/`while` have (grammar.js takes it), but lhd does not lower one
  // yet, so it gets its own message instead of reading `const` as the
  // induction variable.
  if (at_kw(Keyword::kw_const) || at_kw(Keyword::kw_mut) || at_kw(Keyword::kw_reg) || at_kw(Keyword::kw_wire) ||
      at_kw(Keyword::kw_comptime))
    error("for-init-unsupported", "a `for` init clause is not supported yet: declare the value before the loop");
  // forBinding: '(' typed_identifier_list ')' | typed_identifier
  if (at(Token_kind::lparen)) {
    advance();
    Ast* til = parse_typed_identifier_list(/*allow_default=*/false, "an induction variable");
    expect(Token_kind::rparen, "unclosed-paren", "expected ')' in for binding");
    til->field = Field::f_index;
    f->add(til);
  } else {
    f->add(parse_typed_identifier(/*allow_default=*/false, "an induction variable"), Field::f_index);
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

// `tick N { stmts }` — a non-unrolling, cycle-driven loop usable only inside a
// `test`. The count expression (mandatory: an unbounded `tick { }` is not
// supported yet, and a tick takes no `clocks=`/`resets=` clauses) bounds the
// number of simulation cycles. Count -> f_value, body -> f_code (mirrors
// loop_statement; grammar.js `tick_statement`).
Ast* Parser::parse_tick_statement() {
  uint32_t start = cur().start_byte;
  advance();  // tick
  Ast* t = node(Kind::tick_statement, start);
  // The removed `clocks=(...)` / `resets=(...)` clauses get a pointed message.
  auto at_clause = [&]() {
    return at(Token_kind::ident) && (cur().text == "clocks" || cur().text == "resets") &&
           peek(1).kind == Token_kind::assign;
  };
  if (at(Token_kind::lbrace) || at_clause())
    error("expected-tick-count", "a `tick` needs a cycle count: `tick N { ... }` (an unbounded tick is not supported)");
  Ast* count   = parse_expression();
  count->field = Field::f_value;
  t->add(count);
  if (at_clause())
    error("tick-clause", "a `tick` takes only a cycle count: `tick N { ... }` (the `clocks=`/`resets=` clauses were "
                         "removed; the tick counter is `clock`)");
  t->add(parse_scope(), Field::f_code);
  finish(t, start);
  return t;
}

// `step [N]` — advance the simulation N cycles (default 1) inside a `test`. The
// optional count expression is stored under f_value.
Ast* Parser::parse_step_statement() {
  uint32_t start = cur().start_byte;
  advance();  // step
  Ast* s = node(Kind::step_statement, start);
  const bool terminator = at(Token_kind::semicolon) || at(Token_kind::rbrace) ||
                          eof() || cur().terminator_before;
  if (!terminator) {
    Ast* count   = parse_expression();
    count->field = Field::f_value;
    s->add(count);
  }
  expect_semicolon();
  finish(s, start);
  return s;
}

// `test name.path [(params)] { ... }`. Resembles a `comb name(...)` lambda but
// has no `-> (...)` return and names the test with a dotted selector path
// (`counter.foo`) so groups/leaves are addressable from the command line. The
// optional `(...)` is the same typed-with-defaults `arg_list` a lambda uses for
// its inputs (runtime test parameters). The selector goes under f_name, the
// parameters under f_input, and the body under f_code.
Ast* Parser::parse_test() {
  uint32_t start = cur().start_byte;
  advance();  // test
  Ast* tnode = node(Kind::test_statement, start);
  // Dotted selector name: identifier ('.' identifier)*
  uint32_t name_start = cur().start_byte;
  Ast*     name       = node(Kind::test_name, name_start);
  name->add(ident_leaf("expected-test-name", "expected a test name after 'test'"));
  while (accept(Token_kind::dot)) {
    name->add(ident_leaf("expected-test-name", "expected an identifier after '.' in test name"));
  }
  finish(name, name_start);
  tnode->add(name, Field::f_name);
  // Optional runtime parameter list (typed, with defaults; no return).
  if (at(Token_kind::lparen)) {
    tnode->add(parse_arg_list(), Field::f_input);
  }
  tnode->add(parse_scope(), Field::f_code);
  finish(tnode, start);
  return tnode;
}

// `formal name.path { ... }` (2f-verify). A declarative verification block:
// the body is ordinary statement syntax (test-block style — import roots,
// instance aliases, assert/assume/assert_always calls), but every claim holds
// at EVERY cycle; it never lowers to hardware (the design compile skips it —
// only the formal driver consumes it). The dotted selector name is the
// enable/disable + filter handle (`lhd formal verify --formal 'name.*'`),
// exactly like a test's. No parameter list: a formal block takes no runtime
// arguments.
Ast* Parser::parse_formal() {
  uint32_t start = cur().start_byte;
  advance();  // formal
  Ast* fnode = node(Kind::formal_statement, start);
  uint32_t name_start = cur().start_byte;
  Ast*     name       = node(Kind::formal_name, name_start);
  name->add(ident_leaf("expected-formal-name", "expected a formal-block name after 'formal'"));
  while (accept(Token_kind::dot)) {
    name->add(ident_leaf("expected-formal-name", "expected an identifier after '.' in formal-block name"));
  }
  finish(name, name_start);
  fnode->add(name, Field::f_name);
  fnode->add(parse_scope(), Field::f_code);
  finish(fnode, start);
  return fnode;
}

Ast* Parser::parse_type_statement() {
  uint32_t start = cur().start_byte;
  Ast* ts = node(Kind::type_statement, start);
  if (at_kw(Keyword::kw_pub)) {
    ts->add(leaf(Kind::pub_modifier), Field::f_pub);  // `pub type X = …` (exportable)
  }
  advance();  // type
  ts->add(leaf(Kind::identifier), Field::f_name);
  if (at(Token_kind::lt)) {
    advance();
    ts->add(parse_typed_identifier_list(/*allow_default=*/true, "a generic parameter"), Field::f_generic);
    expect(Token_kind::gt, "expected-gt", "expected '>' to close generics");
  }
  // The `=` is mandatory, as for `enum` (owner ruling 106; grammar.js
  // `type_statement`): `type Pt (x:S8)` is an error.
  expect(Token_kind::assign, "expected-eq", "expected '=' after the type name (write `type T = (...)`)");
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

Ast* Parser::parse_enum_assignment() {
  uint32_t start = cur().start_byte;
  advance();  // enum
  Ast* en = node(Kind::enum_assignment, start);
  en->add(leaf(Kind::identifier), Field::f_name);
  if (at(Token_kind::colon) || at(Token_kind::coloncolon)) en->add(parse_type_cast(), Field::f_type);
  // The `=` is required (grammar.js `enum_assignment`): `enum E = (a, b)`,
  // `enum E:U8 = (a, b)`; `enum E:U8 (a, b)` is a syntax error.
  expect(Token_kind::assign, "expected-eq", "expected '=' after the enum name (write `enum E = (a, b)`)");
  if (!at(Token_kind::lparen)) error("expected-paren", "expected '(' to open the enum values");
  Ast* values = parse_paren("an enum member name");
  check_enum_members(values);
  en->add(values, Field::f_values);
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
    // A storage word followed by `=` or `:` attempts a name. Route it through
    // name validation to diagnose the missing backticks.
    const bool named = at_field_word();
    if (!named && at_kw(Keyword::kw_const)) v->add(leaf(Kind::const_decl), Field::f_storage);
    else if (!named && at_kw(Keyword::kw_mut)) v->add(leaf(Kind::mut_decl), Field::f_storage);
    else if (!named && at_kw(Keyword::kw_reg)) v->add(leaf(Kind::reg_decl), Field::f_storage);
    else if (!named && at_kw(Keyword::kw_wire)) v->add(leaf(Kind::wire_decl), Field::f_storage);
    else if (!named && at_kw(Keyword::kw_stage)) {
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
  } else if (at_kw(Keyword::kw_wire)) {
    v->add(leaf(Kind::wire_decl), Field::f_storage);
  } else if (at_kw(Keyword::kw_stage)) {
    Ast* st = node(Kind::stage_decl, cur().start_byte);
    advance();
    if (at(Token_kind::lbracket)) st->add(parse_timing_slot(), Field::f_timing);
    finish(st, st->start_byte);
    v->add(st, Field::f_storage);
  }
  // The modifier may also follow the storage word: `const comptime N = 4` is
  // `comptime const N = 4` (owner ruling 2026-10-01; prpfmt prints `comptime`
  // first). The node goes where the leading spelling puts it, so both orders
  // build the same tree. A `comptime` before `=`/`:` is an attempted NAME and
  // keeps the reserved-word diagnostic.
  const auto has_kid = [&](Kind k) {
    return std::any_of(v->kids.begin(), v->kids.end(), [k](const Ast* a) { return a->kind == k; });
  };
  if (at_kw(Keyword::kw_comptime) && !at_field_word()) {
    if (has_kid(Kind::comptime_modifier)) error("comptime-twice", "`comptime` is written twice in this declaration");
    Ast* cpt   = leaf(Kind::comptime_modifier);
    cpt->field = Field::f_comptime;
    const bool after_pub = !v->kids.empty() && v->kids.front()->kind == Kind::pub_modifier;
    v->kids.insert(v->kids.begin() + (after_pub ? 1 : 0), cpt);
  }
  if (at_kw(Keyword::kw_comptime) && !at_field_word() && has_kid(Kind::comptime_modifier))
    error("comptime-twice", "`comptime` is written twice in this declaration");
  // `comptime` makes a VALUE compile-time; a register, a net, a pipeline stage
  // or a fluid declaration has no compile-time form.
  if (has_kid(Kind::comptime_modifier)
      && (has_kid(Kind::fluid_decl) || has_kid(Kind::reg_decl) || has_kid(Kind::wire_decl) || has_kid(Kind::stage_decl)))
    error("comptime-storage",
          "`comptime` applies only to `const` or `mut` (a `reg`, `wire`, `stage` or `fluid` is never compile-time)");
  // `comptime` alone is `comptime const` (grammar.js var_or_let_or_reg); `pub`
  // alone declares nothing: `pub x = 1` is an error.
  if (v->kids.size() == 1 && v->kids.front()->kind == Kind::pub_modifier)
    error("expected-declaration",
          "expected 'const', 'mut', 'reg', 'wire', 'stage', 'fluid' or 'comptime' after 'pub'");
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
  // assignment operator: tree-sitter wraps the aliased assign kind in an
  // `assignment_operator` node (prp2lnast reads the wrapper's single child).
  Kind opk      = assign_kind(cur().kind);
  Ast* op_inner = arena_.make(opk, cur().start_byte, cur().end_byte);
  Ast* op       = arena_.make(Kind::assignment_operator, cur().start_byte, cur().end_byte);
  op->add(op_inner);
  advance();
  // Past the operator the statement head is settled: a failure in the RVALUE is
  // not "you meant an identifier", so drop the backtick hint.
  kw_as_ident_ = kNoKw;
  a->add(op, Field::f_operator);
  // rvalue: expression | ref_identifier (the expression form `enum(a, b)` is
  // gone, spec 2026-09-29 §7: parse_atom rejects it)
  if (at_kw(Keyword::kw_ref)) {
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
  // grammar; consumed but not stored as a node). The keyword is left OUTSIDE the
  // assignment span — prp2lnast recovers it by scanning the raw source gap before
  // the statement (scan_overflow_in_gap), so `start` is re-anchored past it.
  Ast* overflow     = nullptr;
  bool has_overflow = false;
  if (at_kw(Keyword::kw_wrap) || at_kw(Keyword::kw_sat)) {
    advance();
    has_overflow = true;
    start        = cur().start_byte;
  }

  if (is_decl_keyword(cur())) {
    const size_t decl_kw_tok = pos_;
    Ast*         decl        = parse_var_or_let_or_reg();
    if (at(Token_kind::lparen)) {
      // '(' list ')' then '=' (assignment lvalue_list) or ';' (declaration list)
      advance();
      Ast*              list = node(Kind::lvalue_list, cur().start_byte);
      std::vector<Ast*> items;
      {
        Bracket_guard _bg(*this);
        // A destructuring/declaration list names at least one slot:
        // `const () = f()` and `const (,) = f()` are errors (grammar.js lvalue_list).
        skip_leading_commas(Token_kind::rparen);
        if (at(Token_kind::rparen)) error("empty-list", "a destructuring list names at least one variable");
        while (!at(Token_kind::rparen) && !eof()) {
          items.push_back(parse_lvalue_item(/*binding=*/true));  // `const (a, b)` binds a, b
          if (!accept(Token_kind::comma)) break;
          while (at(Token_kind::comma)) advance();
        }
        expect(Token_kind::rparen, "unclosed-paren", "expected ')'");
      }
      for (Ast* it : items) list->add(it, Field::f_item);
      finish(list, list->start_byte);
      if (at_assignment_operator()) {
        check_destructuring(list);
        Ast* a = finish_assignment(start, overflow, decl, list, nullptr);
        expect_semicolon();
        return a;
      }
      if (has_overflow)  // `wrap const (a, b)`: `wrap`/`sat` modify an assignment
        error("expected-assignment", "expected '=' after the declaration ('wrap'/'sat' need an assignment)");
      // Without `=` the list declares plain (typed, timed, attributed) names,
      // as in grammar.js `declaration_statement`: `const (a.b)`, `const (c[1])`,
      // the rename `const (x = t.a)` and a type after the attributes
      // (`mut (a::[x]:U8)`, written `a:U8:[x]`) are errors.
      auto plain = [](auto& self, const Ast* k) -> bool {
        if (k->kind == Kind::typed_identifier || k->kind == Kind::identifier) return true;
        return (k->kind == Kind::timed_identifier || k->kind == Kind::attribute_set) && !k->kids.empty() &&
               self(self, k->kids.front());
      };
      for (const Ast* li : list->kids) {
        const bool typed = std::ranges::any_of(li->kids, [](const Ast* k) { return k->field == Field::f_type; });
        for (const Ast* k : li->kids) {
          if (k->field == Field::f_type || (plain(plain, k) && !(typed && k->kind == Kind::attribute_set))) continue;
          Diag d;
          d.code     = "bad-declaration-target";
          d.category = std::string(kCategorySyntax);
          d.message  = k->kind == Kind::attribute_set
                           ? "a slot's type comes before its attributes (`a:U8:[attr]`, not `a::[attr]:U8`)"
                           : "a declaration without '=' lists plain names (`const (a, b)`); a field, a selector or a "
                             "rename `name = path` needs an assignment";
          d.span     = span_bytes(li->start_byte, li->end_byte);
          throw Parse_error(std::move(d));
        }
      }
      Ast* d = node(Kind::declaration_statement, start);
      d->add(decl, Field::f_decl);
      list->kind = Kind::typed_identifier_list;
      d->add(list, Field::f_lvalue);
      expect_semicolon();
      finish(d, start);
      return d;
    }
    // A declaration names something (`mut x`, `reg q:u8`, `stage[2] y = f()`), so
    // if what follows the storage kind cannot start a name, the word was meant as
    // an identifier. Blaming that directly beats where the old parse gave up:
    // `stage[0] = a` swallowed `[0]` as the pipelining slot and died on the `=`
    // with "expected an expression", and `reg[0] = a` parsed clean through as a
    // declaration of a variable literally named `[0]`. error() adds the escape.
    if (starts_ident_use(cur())) {
      // Blame the LAST storage word consumed (`pub reg[0] = a` -> `reg`, not
      // `pub`); a `stage[N]` slot leaves a `]` there, so fall back to the first.
      const bool   last_is_kw = pos_ > 0 && toks_[pos_ - 1].is_ident() && toks_[pos_ - 1].kw != Keyword::none;
      const Token& kw         = toks_[last_is_kw ? pos_ - 1 : decl_kw_tok];
      error_reserved_name(kw, "reserved-word-as-name",
                          "'" + std::string(kw.text)
                              + "' is a declaration keyword, so a name must follow it");
    }
    // The name being declared. NOTE this is the STATEMENT-level declaration only:
    // a tuple-literal field keeps its own path (parse_tuple_item), so the memory
    // config `mut mem = (const type = 1, const size = 16, ...)` -- a documented
    // API whose field IS spelled `type` -- is untouched.
    // A literal or a lambda may still head a (strange) target -- `mut true.x =
    // 1` -- as in the grammar, whose `_complex_identifier` target admits them.
    const bool head_kw = at_kw(Keyword::kw_true) || at_kw(Keyword::kw_false) || is_lambda_kind(cur()) ||
                         at_kw(Keyword::kw_pub);
    if (!head_kw) require_plain_name("a variable name");
    // The lvalue may be a complex location (`x#[i]`, `a.b`, `arr[i]`) when this
    // is an assignment, or a (typed) identifier when it is a declaration.
    Ast* lv = parse_postfix();
    Ast* tc = nullptr;
    // `const a::[attr] = …` — in declaration lvalue position `::[attr]` is the
    // attribute form of a type_cast (typed_identifier), NOT an attribute_set
    // write. parse_postfix greedily took it as an attribute_set; demote it.
    if (lv->kind == Kind::attribute_set && !lv->kids.empty() &&
        lv->kids.front()->kind == Kind::identifier) {
      Ast* id   = lv->kids.front();
      Ast* attr = nullptr;
      for (Ast* k : lv->kids) {
        if (k->field == Field::f_attribute) attr = k;
      }
      Ast* tcn = node(Kind::type_cast, id->end_byte);
      if (attr) tcn->add(attr, Field::f_attribute);
      finish(tcn, id->end_byte);
      id->field = Field::none;
      lv        = id;
      tc        = tcn;
    }
    if (!tc && (at(Token_kind::colon) || at(Token_kind::coloncolon)) &&
        (lv->kind == Kind::identifier || lv->kind == Kind::timed_identifier))
      tc = parse_type_cast();
    // A timed name (`stage[1] out@[4]`, `mut x@[1]:U8 = 3`) declares the name
    // with its timing: typed_identifier(identifier, timing, type) as in
    // grammar.js bindingTypedIdentifier / `_binding_typed_name`.
    if (lv->kind == Kind::timed_identifier && lv->kids.size() == 2 &&
        (tc || !at_assignment_operator())) {
      Ast* id     = lv->kids[0];
      Ast* timing = lv->kids[1];
      Ast* w      = node(Kind::typed_identifier, lv->start_byte);
      id->field   = Field::f_identifier;
      w->add(id);
      timing->field = Field::f_timing;
      w->add(timing);
      if (tc) w->add(tc, Field::f_type);
      tc = nullptr;
      finish(w, lv->start_byte);
      lv = w;
    }
    // An identifier lvalue carrying a type wraps as a typed_identifier (mirrors
    // the grammar); an untyped identifier lvalue stays bare (tree-sitter parity).
    Ast* ti = lv;
    if (lv->kind == Kind::identifier && tc) {
      ti        = node(Kind::typed_identifier, lv->start_byte);
      lv->field = Field::f_identifier;
      ti->add(lv);
      ti->add(tc, Field::f_type);
      tc = nullptr;
      finish(ti, lv->start_byte);
    }
    if (at_assignment_operator()) {
      require_lvalue(ti);  // `mut f(x) = 3`
      Ast* a = finish_assignment(start, overflow, decl, ti, tc);
      expect_semicolon();
      return a;
    }
    // `wrap`/`sat` modify an ASSIGNMENT: `wrap const y:U8` is an error.
    if (has_overflow)
      error("expected-assignment", "expected '=' after the declaration ('wrap'/'sat' need an assignment)");
    // A bare declaration names a variable: `mut a.b` / `mut f(x)` are errors
    // (grammar.js declaration_statement takes a typed_identifier only).
    if (ti->kind != Kind::identifier && ti->kind != Kind::typed_identifier)
      error("expected-assignment", "a declaration without '=' must name a plain variable");
    Ast* d = node(Kind::declaration_statement, start);
    d->add(decl, Field::f_decl);
    // A bare declaration ALWAYS wraps its lvalue in a typed_identifier — even an
    // untyped `mut c` (tree-sitter parity). Only the assignment form above leaves
    // an untyped identifier lvalue bare. (A typed lvalue is already wrapped.)
    if (ti->kind == Kind::identifier) {
      Ast* w    = node(Kind::typed_identifier, ti->start_byte);
      ti->field = Field::f_identifier;
      w->add(ti);
      finish(w, ti->start_byte);
      ti = w;
    }
    d->add(ti, Field::f_lvalue);
    expect_semicolon();
    finish(d, start);
    return d;
  }

  // No declaration keyword: assignment with a complex lvalue, or expr statement.
  //
  // A statement opening with `(` may be a destructuring assignment. Try the
  // lvalue-list reading first -- the one the grammar's `lvalue_list` gives it,
  // which (unlike a tuple expression) admits a typed named target `(a = x:U8)
  // = f()` -- and fall back to the expression when it does not parse as one
  // or no assignment operator follows (`(a, b) ++ c`, `(x)#[0] = 1`).
  if (at(Token_kind::lparen)) {
    const size_t save = pos_;
    Ast*         list = nullptr;
    try {
      advance();  // '('
      list = node(Kind::lvalue_list, cur().start_byte);
      Bracket_guard _bg(*this);
      skip_leading_commas(Token_kind::rparen);
      while (!at(Token_kind::rparen) && !eof()) {
        list->add(parse_lvalue_item(), Field::f_item);
        if (!accept(Token_kind::comma)) break;
        while (at(Token_kind::comma)) advance();
      }
      if (!at(Token_kind::rparen)) list = nullptr;
    } catch (const Parse_error&) {
      list = nullptr;
    }
    if (list && at(Token_kind::rparen)) {
      finish(list, list->start_byte);
      advance();  // ')'
      if (at_assignment_operator()) {
        check_destructuring(list);
        Ast* a = finish_assignment(start, overflow, nullptr, list, nullptr);
        expect_semicolon();
        return a;
      }
    }
    pos_ = save;  // orphaned arena nodes are harmless (see try_generic_call)
  }
  // After `wrap`/`sat` only an assignment target can follow: a NAME position,
  // where any keyword is a plain identifier (`wrap if.total = r + a`), as in
  // the grammar.
  Ast* e = has_overflow ? parse_postfix(/*name_ctx=*/true) : parse_expression();
  if (at(Token_kind::colon) || at(Token_kind::coloncolon) || at_assignment_operator())
    reject_type_word_name(e, "an assignment target");  // `U8 = 3`: `U8` names no variable
  Ast* type_cast = nullptr;
  bool typed     = false;
  if (at(Token_kind::colon) || at(Token_kind::coloncolon)) {
    typed = true;
    // `lvalue : Type = ...` (only legal with wrap/sat — a typed identifier
    // lvalue folds into a typed_identifier the way the grammar models it).
    type_cast = parse_type_cast();
    if (e->kind == Kind::identifier && type_cast) {
      Ast* ti   = node(Kind::typed_identifier, e->start_byte);
      e->field  = Field::f_identifier;
      ti->add(e);
      ti->add(type_cast, Field::f_type);
      finish(ti, e->start_byte);
      e         = ti;
      type_cast = nullptr;
    }
  }
  if (at_assignment_operator()) {
    // `(a, x=b.c) = rhs` — the LHS parsed optimistically as a tuple; an '=' now
    // confirms it is a destructuring lvalue_list.
    if (e->kind == Kind::tuple) {
      if (typed) error("typed-destructuring", "a destructuring assignment carries no type");
      e = tuple_to_lvalue_list(e);
      check_destructuring(e);
    } else {
      require_lvalue(e);  // `f(x) = 3`, `a + b = 3`
    }
    Ast* a = finish_assignment(start, overflow, nullptr, e, type_cast);
    expect_semicolon();
    return a;
  }
  // `value:U8` alone declares nothing (a declaration needs `const`/`mut`/...):
  // a typed target needs an assignment, as `wrap`/`sat` do.
  if (has_overflow || typed)
    error("expected-assignment", "expected an assignment operator (a declaration without '=' needs a keyword:"
                                 " `mut value:U8`)");
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
    // `pub` must head a lambda here (`mut x = pub`, `pub.x` are errors).
    if (!at_kw(Keyword::kw_fluid)) error("expected-lambda", "expected 'comb', 'mod', 'pipe' or 'fluid'");
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
    fdd->add(parse_typed_identifier_list(/*allow_default=*/true, "a generic parameter"), Field::f_generic);
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
      fdd->add(parse_typed_identifier(/*allow_default=*/false, "an output name"), Field::f_output);
    }
  }
  finish(fdd, start);
  return fdd;
}

Ast* Parser::parse_arg_list(const char* bind_role) {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lparen, "expected-paren", "expected '(' to open argument list");
  Bracket_guard _bg(*this);
  Ast* al = node(Kind::arg_list, start);
  skip_leading_commas(Token_kind::rparen);
  while (!at(Token_kind::rparen) && !eof()) {
    // [mod] typed_identifier [= default]. The `mod` (... / ref / reg) is an
    // anonymous token in the grammar carrying a `mod` field; emit it as an
    // anonymous marker node so the consumer can read which params are ref/reg/
    // vararg (get_text gives the keyword).
    if (at(Token_kind::ellipsis) || at_kw(Keyword::kw_ref) || at_kw(Keyword::kw_reg)) {
      Ast* m  = arena_.make(Kind::identifier, cur().start_byte, cur().end_byte);
      m->named = false;
      al->add(m, Field::f_mod);
      advance();
    }
    // ports bind names too, in and out: `mod f(`in`:U8) -> (`reg`:U8)`
    Ast* ti = parse_typed_identifier(/*allow_default=*/false, bind_role);
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
      Ast* op = make_binop(arena_, opk, cur().start_byte, cur().end_byte);  \
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
    Ast* op = make_binop(arena_, Kind::op_step, cur().start_byte, cur().end_byte);
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
  // Negative integer literal: fold `-` + integer into a single signed constant
  // atom, then allow a suffix chain (tree-sitter binds the sign tighter than
  // postfix here, so `-5.foo` is `(-5).foo` and `-5` in a comptime index/range/
  // timing slot reads as one `constant`).
  if (t.kind == Token_kind::minus && peek(1).kind == Token_kind::integer) {
    uint32_t start = t.start_byte;
    advance();  // '-'
    Ast* lit        = leaf(Kind::integer_literal);
    lit->start_byte = start;  // the literal's span includes the sign
    Ast* c          = node(Kind::constant, start);
    c->add(lit);
    finish(c, start);
    return parse_postfix_from(c);
  }
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
  if (t.is_kw(Keyword::kw_if) || t.is_kw(Keyword::kw_unique)) return parse_if_expression();
  if (t.is_kw(Keyword::kw_match)) return parse_match_expression();
  if (t.kind == Token_kind::lbrace) return parse_scope();
  if (t.kind == Token_kind::type_word) return parse_type_word_operand();
  return parse_postfix();
}

// A type word USED AS A VALUE (grammar.js `_type_word_name` /
// `_type_word_call`): the operand `U8` (`x does U8`, `(t=U8)`) or the
// conversion call `U8(x)` / `Bool(y)` / `Unsigned(bits=8)`, as an `identifier`
// / `function_call_expression` node. A conversion call's result is an ordinary
// value and heads any suffix chain (`U8(x)#[0]`, `U8(x).f`). The bare word takes
// only an attribute read (`U8.[max]`, grammar.js `attribute_read`), whose result
// is again an ordinary suffix head; `U8.x`, `U8[0]`, `U8#[0]` and `U8@[1]` stop
// here and the caller rejects the suffix, exactly like the grammar.
Ast* Parser::parse_type_word_operand() {
  const Token& t     = cur();
  uint32_t     start = t.start_byte;
  Ast*         id    = arena_.make(Kind::identifier, t.start_byte, t.end_byte);
  advance();
  if (term_stop()) return id;
  if (at(Token_kind::lparen)) {
    Ast* call = node(Kind::function_call_expression, start);
    id->field = Field::f_function;
    call->add(id);
    call->add(parse_arg_tuple(), Field::f_argument);
    finish(call, start);
    return parse_postfix_from(call);
  }
  if (at(Token_kind::dot) && peek(1).kind == Token_kind::lbracket) {
    Ast* ar   = node(Kind::attribute_read, start);
    id->field = Field::f_argument;
    ar->add(id);
    while (at(Token_kind::dot) && peek(1).kind == Token_kind::lbracket) {
      advance();  // '.'
      ar->add(parse_attribute_list(), Field::f_attrs);
    }
    finish(ar, start);
    return parse_postfix_from(ar);
  }
  if (at(Token_kind::dot) && peek(1).kind != Token_kind::lbracket)
    error("type-word-field", "a type has no fields: only an attribute read follows a type word (`U8.[max]`)");
  return id;
}

Ast* Parser::parse_postfix(bool name_ctx) { return parse_postfix_from(parse_atom(name_ctx)); }

Ast* Parser::consume_binary_tail(Ast* lhs) {
  if (term_stop() || binary_op_any(cur()) == Kind::invalid) return lhs;
  Ast* nd    = node(Kind::expression_item, lhs->start_byte);
  lhs->field = Field::f_operand;
  nd->add(lhs);
  Kind opk;
  while (!term_stop() && (opk = binary_op_any(cur())) != Kind::invalid) {
    Ast* op = make_binop(arena_, opk, cur().start_byte, cur().end_byte);
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
  // A single-expression `(...)` is only a distinct `paren_group` CST node when it
  // heads a suffix chain (`(expr).foo`, `(expr)#[..]`); standing alone it is a
  // single-item `tuple` (tree-sitter parity — the grouping-vs-tuple distinction
  // there is recovered downstream from a trailing comma in the source).
  const bool tentative_group = (e->kind == Kind::paren_group);
  for (;;) {
    if (term_stop()) break;
    Kind ek = e->kind;
    // `attribute_set` is callable so a call-site attribute binds to the callee
    // before the arg list: `alu::[name=pipeB_ex_mem](args)` parses as
    // function_call_expression(f_function=attribute_set(alu,[..]), f_argument=args).
    // (`alu(args)::[attr]` still parses the other way — the call matches first
    // when the args precede the `::[`.)
    bool callable = ek == Kind::identifier || ek == Kind::dot_expression ||
                    ek == Kind::member_selection || ek == Kind::bit_selection ||
                    ek == Kind::attribute_read || ek == Kind::timed_identifier ||
                    ek == Kind::attribute_set;
    if (at(Token_kind::lparen) && callable) {
      Ast* call = node(Kind::function_call_expression, start);
      e->field  = Field::f_function;
      call->add(e);
      call->add(parse_arg_tuple(), Field::f_argument);
      finish(call, start);
      e = call;
      continue;
    }
    // An explicit generic call `f<T=U8>(x)`. The callee is any name path the
    // grammar's `_complex_identifier` covers, so dotted callees work too:
    // `prp.queue.make<T=Signed>(depth=16)` (grammar.js genericTupleCall).
    // An instance attribute (`a::[x]`) is only followed by the call it
    // configures (`a::[x](1)`); a selector, bit-select, field or attribute
    // read of it (`a::[x][1]`, `a::[x]#[1]`, `a::[x].b`, `a::[x].[bits]`,
    // `a::[x]::[y]`) is an error (grammar.js: attribute_set is no suffix head).
    if (ek == Kind::attribute_set &&
        (at(Token_kind::lbracket) || at(Token_kind::hash) || at(Token_kind::dot) ||
         (at(Token_kind::coloncolon) && peek(1).kind == Token_kind::lbracket)))
      error("attribute-set-suffix",
            "an instance attribute `::[...]` can only be followed by a call; parenthesize it to select from it "
            "(`(a::[x])[1]`)");
    // A call-site generic list opens only with a `<` GLUED to the callee
    // (owner ruling 107): a blank right before the `<` makes it a comparison,
    // so `a < b > (c)` is a comparison chain and `f <N=3>(x)` is an error.
    // tree-sitter agrees (scanner.c scan_spaced_lt). A comment glued to the
    // `<` (`f /*c*/<T>(x)`) leaves it glued in both parsers: only the
    // character right before the `<` counts.
    // `f <N=3>(x)` and `f <U8>(x)`: neither `x < N = ...` nor `x < U8 > ...`
    // is a comparison, so name the fix.
    if (at(Token_kind::lt) && callable && ek != Kind::attribute_set && blank_before(cur()) &&
        ((peek(1).kind == Token_kind::ident && peek(2).kind == Token_kind::assign) ||
         (peek(1).kind == Token_kind::type_word && peek(2).kind == Token_kind::gt)))
      error("spaced-generic",
            "a generic list's `<` must touch the callee (`f<N=3>(x)`); a `<` after a blank is a comparison");
    if (at(Token_kind::lt) && callable && ek != Kind::attribute_set && !blank_before(cur())) {
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
        // reserved after `.` too (`t.U8` is an error; write t.`U8`)
        if (at(Token_kind::type_word)) error_type_word_name(cur(), "a field name");
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
  // No suffix consumed the tentative paren_group -> it was just a parenthesized
  // single expression, i.e. a single-item tuple.
  if (tentative_group && e->kind == Kind::paren_group) {
    e->kind = Kind::tuple;
    if (!e->kids.empty()) e->kids.front()->field = Field::f_item;
  }
  return e;
}

Ast* Parser::try_generic_call(Ast* fn) {
  size_t save = pos_;
  advance();  // '<'
  Bracket_guard _bg(*this);
  // generic_type_list: value (',' value)*, then '>' immediately followed by '('.
  // Each value is a generic ARGUMENT (parse_generic_value: a type, or a bare
  // postfix attribute read `x.[bits]`), never a full expression.
  Ast* list = node(Kind::generic_type_list, cur().start_byte);
  bool ok    = true;
  while (at(Token_kind::comma)) advance();
  // The list is a GUESS. `x < ~b`, `x < [1, 2]` are comparisons whose right
  // operand is no generic argument, and parse_generic_value THROWS on those; a
  // throw here therefore means "not a generic call" -> back off to the
  // comparison, as tree-sitter (which forks both parses) does (`x < -1` backs
  // off at the missing `>`). Only a list that OPENS with a named bind is
  // committed -- `x < N = …` is never a comparison -- so its errors keep their
  // precise location (`f<N=~3>(a)`: "expected a type" at the `~`).
  const bool committed = (at(Token_kind::ident) || at(Token_kind::type_word)) && peek(1).kind == Token_kind::assign;
  try {
    while (!at(Token_kind::gt) && !eof()) {
      // A NAMED generic bind (`f<T=U8>`, todo 3g C): `identifier '=' value`,
      // following the same naming rules as call arguments. Reuse arg_assignment
      // (lvalue=name, rvalue=value) so prp2lnast's named-arg machinery applies;
      // a bare positional value stays an item as before.
      if ((at(Token_kind::ident) || at(Token_kind::type_word)) && peek(1).kind == Token_kind::assign) {
        uint32_t nstart = cur().start_byte;
        Ast*     aa     = node(Kind::arg_assignment, nstart);
        Ast*     nm     = leaf(Kind::identifier);
        nm->field       = Field::f_lvalue;
        aa->add(nm);
        accept(Token_kind::assign);  // '='
        Ast* ty = parse_generic_value();
        if (!ty) {
          ok = false;
          break;
        }
        aa->add(ty, Field::f_rvalue);
        finish(aa, nstart);
        list->add(aa, Field::f_item);
      } else {
        Ast* ty = parse_generic_value();
        if (!ty) {
          ok = false;
          break;
        }
        list->add(ty, Field::f_item);
      }
      if (!accept(Token_kind::comma)) break;
      while (at(Token_kind::comma)) advance();
    }
  } catch (const Parse_error&) {
    if (committed) throw;
    pos_ = save;  // orphaned arena nodes are harmless (as on the path below)
    return nullptr;
  }
  // An empty list (`mk<>(x)`, `mk<,>(x)`) is no generic call (grammar.js
  // generic_type_list is listseq1): `<` stays a comparison, which then fails.
  if (ok && !list->kids.empty() && at(Token_kind::gt) && peek(1).kind == Token_kind::lparen) {
    advance();  // '>'
    // The argument tuple is still part of the guess: `a < b > (c:d)` is no call
    // (a typed item is no argument) but a comparison whose right operand is a
    // tuple, as in tree-sitter.
    Ast* args = nullptr;
    try {
      args = parse_arg_tuple();
    } catch (const Parse_error&) {
      if (committed) throw;
      pos_ = save;
      return nullptr;
    }
    Ast* call = node(Kind::function_call_expression, fn->start_byte);
    fn->field = Field::f_function;
    call->add(fn);
    finish(list, list->start_byte);
    call->add(list, Field::f_generic);
    call->add(args, Field::f_argument);
    finish(call, fn->start_byte);
    return call;
  }
  pos_ = save;  // not a generic call -> treat '<' as comparison
  return nullptr;
}

Ast* Parser::parse_atom(bool name_ctx) {
  const Token& t = cur();
  if (t.kind == Token_kind::lparen) return parse_paren();
  if (t.kind == Token_kind::lbracket) return parse_tuple_sq();
  // Where a value may start, these keywords always start their construct (the
  // grammar's keyword token is valid there, so tree-sitter never reads them as
  // a name): `x = if` / `1 + unique` / `pub.x` / `fluid(1)` / `true = 3` are
  // errors. Only in a NAME position (`ref match.x`, `wrap if.total = x`) are
  // `if`/`unique`/`match` plain identifiers.
  // A NAME position (a `_complex_identifier`) still admits a literal or a
  // lambda as a suffix head (`ref comb f() {}.x`), so those keywords keep
  // their meaning there too.
  if (at_constant()) return parse_constant();  // incl. `true`/`false`
  if ((t.kind == Token_kind::ident && t.text == "nil")
      || (t.is_kw(Keyword::kw_import) && peek(1).kind == Token_kind::lparen)) {
    // `nil` and the `import(...)` construct are language syntax, not names.
    Ast* literal = arena_.make(Kind::identifier, t.start_byte, t.end_byte);
    advance();
    return literal;
  }
  if (is_lambda_kind(t) || t.is_kw(Keyword::kw_pub)) {
    // `mut dut = pub`, `fluid.x`: the word can only start a lambda here.
    if (!looks_like_lambda())
      error_reserved_name(t, "reserved-word-as-name",
                          "'" + std::string(t.text) + "' starts a lambda, so it cannot be used as a value");
    return parse_lambda();
  }
  if (!name_ctx) {
    if (t.is_kw(Keyword::kw_if) || t.is_kw(Keyword::kw_unique)) return parse_if_expression();
    if (t.is_kw(Keyword::kw_match)) return parse_match_expression();
  }
  // parse_unary takes a type word used as a value (parse_type_word_operand);
  // here (an lvalue, a `ref` target, a suffix head) it would be a name.
  if (t.kind == Token_kind::type_word) error_type_word_name(t, "a name here");
  if (t.is_kw(Keyword::kw_enum) && peek(1).kind == Token_kind::lparen && !peek(1).terminator_before) error_enum_expression();
  if (t.kind == Token_kind::ident) {
    Ast* id = leaf(Kind::identifier);
    // timed_identifier: ident @[...]. `@` never continues a statement onto a new
    // line (src/scanner.c): `const a = b` newline `@[1]` is an error.
    if (at(Token_kind::at) && !term_stop()) {
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
  // tree-sitter wraps every literal in a `constant` node (the grammar's
  // `constant: choice(integer_literal, bool_literal, …)` rule). Reproduce it so
  // prp2lnast's `constant`-typed reads line up.
  uint32_t start = cur().start_byte;
  Ast*     lit;
  if (at(Token_kind::integer)) {
    lit = leaf(Kind::integer_literal);
  } else if (at(Token_kind::string)) {
    lit = leaf(Kind::string_literal);
  } else if (at(Token_kind::istring)) {
    lit = parse_istring();
  } else {
    lit = leaf(Kind::bool_literal);  // true / false
  }
  Ast* c = node(Kind::constant, start);
  c->add(lit);
  finish(c, start);
  return c;
}

Ast* Parser::parse_istring() {
  const Token& t         = cur();
  uint32_t     tok_start = t.start_byte;
  uint32_t     tok_end   = t.end_byte;
  Ast*         lit       = node(Kind::interpolated_string_literal, tok_start);
  const char*  b         = buf_.data();
  // Body is between the surrounding quotes. Sub-parse every `{expr}` hole into a
  // child expression (tree-sitter embeds the holes as named children). Literal
  // braces `{{` / `}}` and backslash escapes are skipped.
  uint32_t body_start = tok_start + 1;
  uint32_t body_end   = (tok_end > body_start) ? tok_end - 1 : body_start;
  uint32_t i          = body_start;
  while (i < body_end) {
    char cc = b[i];
    if (cc == '\\') {
      // escape (validated by the lexer): skip all of it, so the braces of
      // `\u{41}` never open a hole
      uint32_t e = Lexer::escape_end(b, body_end, i);
      i          = (e > i) ? e : i + 2;
      continue;
    }
    if (cc == '{') {
      if (i + 1 < body_end && b[i + 1] == '{') {
        i += 2;  // `{{` literal brace
        continue;
      }
      // The hole ends where the LEXER says (Lexer::istring_hole_end): a '}'
      // inside a comment, a nested string or a backtick name does not close
      // it. A naive brace count here used to end `"{a /* } */ + 1}"` at the
      // commented '}' and silently evaluate the hole as `a`.
      uint32_t es    = i + 1;
      uint32_t end   = Lexer(buf_).istring_hole_end(i);  // past the matching '}'
      uint32_t close = (end > es && end <= body_end) ? end - 1 : body_end;
      Ast*     e     = parse_subexpr(es, close);
      if (e) lit->add(e);
      i = (close < body_end) ? close + 1 : body_end;
      continue;
    }
    ++i;
  }
  advance();  // consume the interpolated-string token
  finish(lit, tok_start);
  return lit;
}

Ast* Parser::parse_subexpr(uint32_t lo, uint32_t hi) {
  // Swap in the hole's token stream for the duration of the sub-parse. The RAII
  // guard restores toks_/pos_/ebd_ on every exit path, including if
  // parse_expression() throws a Parse_error (the manual restore below used to be
  // skipped on throw -- harmless under fail-fast today, but exception-unsafe).
  struct State_guard {
    Parser&            p;
    std::vector<Token> toks;
    size_t             pos;
    int                ebd;
    explicit State_guard(Parser& pp) : p(pp), toks(std::move(pp.toks_)), pos(pp.pos_), ebd(pp.ebd_) {}
    ~State_guard() {
      p.toks_ = std::move(toks);
      p.pos_  = pos;
      p.ebd_  = ebd;
    }
    State_guard(const State_guard&)            = delete;
    State_guard& operator=(const State_guard&) = delete;
  } guard(*this);

  Lexer          sub(buf_);
  const uint32_t spec = sub.hole_spec_colon(lo, hi);  // the spec is text, not code
  toks_               = sub.tokenize_range(lo, spec);
  pos_                = 0;
  ebd_                = 1;  // inside a hole: no virtual-semicolon termination
  // An empty hole (`{}`, `{ /* c */ }`) is a lexical error; `{:b}` has no expression.
  if (eof()) error("expected-expression", "expected an expression before the format spec in a string hole");
  Ast* e = parse_expression();
  // The hole is ONE expression, optionally followed by a format spec: nothing
  // else may follow (`"{x y}"`, `"{x)}"`, `"{1{x}}"` are errors, as in the
  // grammar).
  if (!eof())
    error("bad-interpolation", "expected '}' or ':' (a format spec) after the expression in a string hole");
  if (spec < hi) check_format_spec(spec, hi);
  return e;
}

// grammar.js `_format_spec`: `:`, blanks, then a NON-EMPTY run of characters
// other than `}` `"` `{` newline, where a `/` must not open a comment. After it
// only blanks and comments may reach the hole's `}` (`hi`): `"{x:b /* c */}"`
// is spec `:b`, while `"{x:}"`, `"{x: }"`, `"{x:/* c */b}"` and `"{x:{}}"` are
// errors.
void Parser::check_format_spec(uint32_t colon, uint32_t hi) const {
  const char* b         = buf_.data();
  auto        spec_char = [&](uint32_t i) {
    const char c = b[i];
    // A format spec holds no backslash (`"{x:\x4}"` is an error).
    if (c == '}' || c == '"' || c == '{' || c == '\n' || c == '\\') return false;
    if (c != '/') return true;
    if (i + 1 >= hi) return false;
    const char d = b[i + 1];
    return !(d == '}' || d == '"' || d == '{' || d == '\n' || d == '/' || d == '*' || d == '\\');
  };
  auto bad = [&](uint32_t at_byte) {
    Diag d;
    d.code     = "bad-format-spec";
    d.category = std::string(kCategorySyntax);
    d.message  = "expected a format spec after ':' (such as `{x:b}`); it can not be empty or hold '{' or a backslash";
    d.span     = span_bytes(at_byte, at_byte + 1);
    throw Parse_error(std::move(d));
  };
  uint32_t i = colon + 1;
  while (i < hi && (b[i] == ' ' || b[i] == '\t')) ++i;
  if (i >= hi || b[i] == ' ' || b[i] == '\t' || !spec_char(i)) bad(colon);
  while (i < hi && spec_char(i)) i += (b[i] == '/') ? 2 : 1;
  // Trailing blanks and comments only.
  while (i < hi) {
    const char c = b[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
      ++i;
    } else if (c == '/' && i + 1 < hi && b[i + 1] == '/') {
      while (i < hi && b[i] != '\n') ++i;
    } else if (c == '/' && i + 1 < hi && b[i + 1] == '*') {
      int depth = 1;
      i += 2;
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
    } else {
      bad(i);
    }
  }
}

// The DIRECT entries of an enum body are its members, i.e. FIELDS (`E.a`): a
// bare type word can not be one (`enum E = (a, U8)`, like `(const U8 = 1)`);
// grammar.js `_enum_tuple`. A nested value (`l1 = (a, b)`) is an ordinary tuple.
void Parser::check_enum_members(const Ast* values) const {
  if (!values) return;
  for (const Ast* k : values->kids) {
    if (k->field != Field::f_item && values->kind != Kind::paren_group) continue;
    reject_type_word_name(k, "an enum member name");
  }
}

Ast* Parser::parse_complex_identifier() { return parse_postfix(); }

Ast* Parser::parse_ref_identifier() {
  uint32_t     start = cur().start_byte;
  const Token  kw    = cur();
  advance();  // ref
  // `ref` names the value being referenced, and every _complex_identifier the
  // grammar allows there starts with a word. When none does, `ref` was meant as
  // an identifier: `o = ref[1]` (a Verilog signal named `ref`) used to parse as
  // a reference to something called `[1]` and die far away in name resolution
  // with "read of undefined variable '[1]'".
  if (!at(Token_kind::ident)) {
    error_reserved_name(kw, "reserved-word-as-name",
                        "'ref' names the value being referenced, so a name must follow it");
  }
  Ast* r = node(Kind::ref_identifier, start);
  Ast* target = parse_postfix(/*name_ctx=*/true);
  // grammar.js `ref_identifier`: a `_complex_identifier` (a name, a field, a
  // selector, a bit-select, an attribute read or a timed name) -- never a
  // literal, a call or an attribute write (`ref true`, `ref f(x)`).
  switch (target->kind) {
    case Kind::identifier:
    case Kind::dot_expression:
    case Kind::member_selection:
    case Kind::bit_selection:
    case Kind::attribute_read:
    case Kind::timed_identifier:
      break;
    default: {
      Diag d;
      d.code     = "bad-ref-target";
      d.category = std::string(kCategorySyntax);
      d.message  = "'ref' takes a name, a field, a selector or a bit-select";
      d.span     = span_bytes(target->start_byte, target->end_byte);
      throw Parse_error(std::move(d));
    }
  }
  r->add(target);
  finish(r, start);
  return r;
}

// ===========================================================================
// Paren / tuple / arg-tuple classification
// ===========================================================================
Ast* Parser::parse_paren(const char* name_role) {
  uint32_t start = cur().start_byte;
  advance();  // '('
  Bracket_guard _bg(*this);
  skip_leading_commas(Token_kind::rparen);
  if (at(Token_kind::rparen)) {
    advance();
    Ast* tup = node(Kind::tuple, start);
    finish(tup, start);
    return tup;
  }
  if (name_role) require_plain_name(name_role);
  bool plain = false;
  Ast* first = parse_tuple_item(plain);
  if (plain && at(Token_kind::rparen)) {
    advance();
    Ast* pg = node(Kind::paren_group, start);
    pg->add(first);
    finish(pg, start);
    return pg;
  }
  Ast* tup = node(Kind::tuple, start);
  add_tuple_child(tup, first);
  while (accept(Token_kind::comma)) {
    while (at(Token_kind::comma)) advance();
    if (at(Token_kind::rparen)) break;
    if (name_role) require_plain_name(name_role);
    add_tuple_child(tup, parse_tuple_item());
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
  Bracket_guard _bg(*this);
  Ast* sq = node(Kind::tuple_sq, start);
  skip_leading_commas(Token_kind::rbracket);
  while (!at(Token_kind::rbracket) && !eof()) {
    add_tuple_child(sq, parse_tuple_item());
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

Ast* Parser::parse_tuple_item() {
  bool plain = false;  // common callers don't need the classification
  return parse_tuple_item(plain);
}

Ast* Parser::parse_stmt_item() {
  bool plain = false;
  return parse_tuple_item(plain, /*stmt=*/true);
}

Ast* Parser::parse_tuple_item(bool& plain, bool stmt) {
  // The target of an assignment item: a destructuring `(a, b) = f()` only as an
  // init-clause statement (grammar.js `_stmt_item`), else a plain target.
  auto item_target = [&](Ast* lv) -> Ast* {
    if (lv->kind == Kind::tuple) {
      Diag d;
      d.code     = "bad-assignment-target";
      d.category = std::string(kCategorySyntax);
      d.message  = stmt ? "a destructuring assignment `(a, b) = ...` is a statement of its own, not an init clause"
                        : "a destructuring assignment `(a, b) = ...` is a statement, not a tuple entry";
      d.span     = span_bytes(lv->start_byte, lv->end_byte);
      throw Parse_error(std::move(d));
    }
    require_lvalue(lv);  // `(f(x) = 1)`, `(a + b = 1)`
    return lv;
  };
  plain = false;
  uint32_t start = cur().start_byte;

  // A reserved word before `=`/`:` names a FIELD (grammar.js `_field_word`):
  // `(if = 1)`, `(pub:U8 = 0)`, `(in:U8)`. In an init clause (`stmt`) only the
  // typed-field form exists there (`_stmt_item` typed_field).
  auto field_word_item = [&](Ast* decl) -> Ast* {
    Ast* name = leaf(Kind::identifier);
    Ast* tc   = nullptr;
    if (at(Token_kind::colon) || at(Token_kind::coloncolon)) tc = parse_type_cast();
    Ast* lv = name;
    if (tc) {
      Ast* ti     = node(Kind::typed_identifier, name->start_byte);
      name->field = Field::f_identifier;
      ti->add(name);
      ti->add(tc, Field::f_type);
      finish(ti, name->start_byte);
      lv = ti;
    }
    if (at_assignment_operator() && !stmt) return finish_assignment(start, nullptr, decl, lv, nullptr);
    if (!tc) error("expected-expression", "expected an expression");  // `if =` in an init clause
    if (decl) {  // `(const in:U8)`: decl + typed_identifier (no value)
      decl->add(lv, Field::f_lvalue);
      finish(decl, start);
      return decl;
    }
    Ast* tf = node(Kind::typed_field, start);  // `(in:U8)`
    name->field = Field::f_identifier;
    tf->add(name);
    tc->field = Field::f_type;
    tf->add(tc);
    finish(tf, start);
    return tf;
  };
  if (at_field_word()) return field_word_item(nullptr);

  if (at_kw(Keyword::kw_ref)) return parse_ref_identifier();
  if ((is_lambda_kind(cur()) || at_kw(Keyword::kw_pub)) && looks_like_lambda()) return parse_lambda();

  if (is_decl_keyword(cur())) {
    Ast* decl = parse_var_or_let_or_reg();
    // In an init clause the declaration BINDS a variable (`while mut i = 0;`):
    // a keyword can not name it (only a value may follow, `const 3`). In a
    // tuple it declares a FIELD, which a keyword may name before `=`/`:`.
    if (stmt) {
      const Token_kind n = peek(1).kind;
      if (!is_value_start_kw(cur()) || assign_kind(n) != Kind::invalid || n == Token_kind::colon)
        require_plain_name("a variable name");  // `while mut unique = 0;`
    } else if (at_field_word()) {
      return field_word_item(decl);
    }
    Ast* lv   = parse_expression();  // a, a.b, a[i], 3, ...
    Ast* tc   = nullptr;
    // `(const U8)` is a positional value (the type), like `(const 3)`; `U8` can
    // not name the field: `(const U8 = 1)`, `(const U8:U4)` are errors.
    bool type_value = false;
    if (at(Token_kind::colon) || at(Token_kind::coloncolon) || at_assignment_operator())
      reject_type_word_name(lv, "a tuple field name");
    else if (lv->kind == Kind::identifier)
      type_value = is_type_word(std::string_view(buf_.data() + lv->start_byte, lv->end_byte - lv->start_byte));
    if ((at(Token_kind::colon) || at(Token_kind::coloncolon)) && lv->kind == Kind::identifier)
      tc = parse_type_cast();
    if (at_assignment_operator()) {
      // A typed field assignment folds name+type into a typed_identifier lvalue
      // (tree-sitter parity — the consumer reads the per-field type there).
      if (lv->kind == Kind::identifier && tc) {
        Ast* ti   = node(Kind::typed_identifier, lv->start_byte);
        lv->field = Field::f_identifier;
        ti->add(lv);
        ti->add(tc, Field::f_type);
        finish(ti, lv->start_byte);
        lv = ti;
        tc = nullptr;
      }
      lv = item_target(lv);
      return finish_assignment(start, nullptr, decl, lv, tc);
    }
    // no '=' : decl + typed_identifier (named field) | decl + expression (positional)
    if (tc || (lv->kind == Kind::identifier && !type_value)) {
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
  if (at(Token_kind::colon) || at(Token_kind::coloncolon) || at_assignment_operator())
    reject_type_word_name(e, "a tuple field name");  // `(U8=1)`, `(U8:U4)`
  if ((at(Token_kind::colon) || at(Token_kind::coloncolon)) && e->kind == Kind::identifier)
    tc = parse_type_cast();
  if (at_assignment_operator()) {
    // typed field assignment (`a:Bool = nil`) folds name+type into a
    // typed_identifier lvalue (tree-sitter parity).
    if (e->kind == Kind::identifier && tc) {
      Ast* ti   = node(Kind::typed_identifier, e->start_byte);
      e->field  = Field::f_identifier;
      ti->add(e);
      ti->add(tc, Field::f_type);
      finish(ti, e->start_byte);
      e  = ti;
      tc = nullptr;
    }
    e = item_target(e);
    return finish_assignment(start, nullptr, nullptr, e, tc);
  }
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
  plain = true;
  return e;
}

void Parser::add_tuple_child(Ast* parent, Ast* it) {
  // A decl-keyword tuple field with no '=' (`mut c:u24`, `mut 5`) is parsed as a
  // var_or_let_or_reg with its typed_identifier / value nested; tree-sitter puts
  // `decl:` and `lvalue:`/`value:` as TWO separate sibling fields on the tuple.
  // Splice it apart. Everything else is a single positional `item:` child.
  if (it->kind == Kind::var_or_let_or_reg) {
    Ast*              payload = nullptr;
    Field             pf      = Field::none;
    std::vector<Ast*> kept;
    kept.reserve(it->kids.size());
    for (Ast* k : it->kids) {
      if (k->field == Field::f_lvalue || k->field == Field::f_value) {
        payload = k;
        pf      = k->field;
      } else {
        kept.push_back(k);
      }
    }
    it->kids = std::move(kept);
    parent->add(it, Field::f_decl);
    if (payload) parent->add(payload, pf);
    return;
  }
  parent->add(it, Field::f_item);
}

Ast* Parser::parse_lvalue_item(bool binding) {
  uint32_t start = cur().start_byte;
  // Every lvalue_list entry is an `lvalue_item` wrapper (tree-sitter shape).
  Ast* li = node(Kind::lvalue_item, start);
  // named_lvalue: a rename slot `local = source.path` (spec 2026-09-29 §7/§8:
  // `(x = dox.b) = dox(a=3)` assigns the RHS field `dox.b` to the local `x`).
  // Both the local and every component of the source path use ordinary name
  // validation: keyword/type collisions require backticks, including fields.
  if (cur().kind == Token_kind::ident && peek(1).kind == Token_kind::assign) {
    require_plain_name("a destructuring target");
    Ast* name = leaf(Kind::identifier);
    advance();  // '='
    Ast* lv = parse_slot_path();
    Ast* nl   = node(Kind::named_lvalue, start);
    name->field = Field::f_name;
    nl->add(name);
    lv->field = Field::f_lvalue;
    nl->add(lv);
    finish(nl, start);
    li->add(nl);
    finish(li, start);
    return li;
  }
  if (binding) {
    require_plain_name("a variable name");  // `const (if, b) = (1, 2)`
  } else if (at_field_word() || is_decl_keyword(cur()) || at_kw(Keyword::kw_ref) || is_value_start_kw(cur())) {
    // At a statement's `(` the entry may also start a tuple entry, so these
    // keywords are that entry (`(in:U8, b)`, `(const, b)`, `(ref, b)`), never a
    // destructuring target (grammar.js lexes them as the keyword there).
    error("bad-assignment-target", "'" + std::string(cur().text) + "' is a reserved word, so it cannot be an assignment target");
  }
  Ast* e  = parse_postfix();
  require_lvalue(e);  // `mut (a, f(x)) = g()`
  Ast* tc = nullptr;
  if (at(Token_kind::colon) || at(Token_kind::coloncolon)) tc = parse_type_cast();
  if (e->kind == Kind::identifier) {
    Ast* ti   = node(Kind::typed_identifier, e->start_byte);
    e->field = Field::f_identifier;
    ti->add(e);
    if (tc) ti->add(tc, Field::f_type);
    finish(ti, e->start_byte);
    li->add(ti);
    finish(li, start);
    return li;
  }
  // Complex lvalue (`a.b`, `arr[i]`): lvalue_item carries the location and type
  // as direct fields (the grammar's inline `identifier`/`type` seq).
  e->field = Field::f_identifier;
  li->add(e);
  if (tc) li->add(tc, Field::f_type);
  finish(li, start);
  return li;
}

// A destructuring assignment (spec 2026-09-29 §7/§8) is a STATEMENT whose slots
// are bare names (`(a, b) = f()`, `const (p1, p2) = two(..)`) or renames
// `name = path` with a dotted name path (`(x = dox.b) = dox(a=3)`); the
// operator is `=`. Errors: a complex slot (`(a.b, c[1]) = f()`), a typed slot
// (`const (a:U32, b) = ...`), a path that is no dotted name (`(x = f(a).b) = ..`)
// and a compound operator (`(a, b) += f()`). `list` is the lvalue_list; the
// current token is the assignment operator.
void Parser::check_destructuring(const Ast* list) const {
  auto fail = [&](const char* code, const std::string& msg, const Ast* at) {
    Diag d;
    d.code     = code;
    d.category = std::string(kCategorySyntax);
    d.message  = msg;
    d.span     = at ? span_bytes(at->start_byte, at->end_byte) : span_bytes(cur().start_byte, cur().end_byte);
    throw Parse_error(std::move(d));
  };
  auto typed = [&](const Ast* ti) {
    for (const Ast* k : ti->kids)
      if (k->field == Field::f_type)
        fail("typed-destructuring",
             "a destructuring slot carries no type (`const (a, b) = f()`; the types come from the right-hand side)", k);
  };
  if (list->kids.empty())  // `() = f()` (grammar.js lvalue_list is listseq1)
    fail("empty-list", "a destructuring list names at least one variable", list);
  for (const Ast* li : list->kids) {
    for (const Ast* k : li->kids) {
      if (k->field == Field::f_type) typed(li);
      if (k->kind == Kind::typed_identifier) {
        typed(k);
      } else if (k->kind == Kind::named_lvalue) {
        for (const Ast* c : k->kids) {
          if (c->field != Field::f_lvalue) continue;
          if (c->kind != Kind::identifier) {
            bool dotted = c->kind == Kind::dot_expression;
            for (const Ast* p : c->kids) dotted = dotted && p->kind == Kind::identifier;
            if (!dotted)
              fail("bad-destructuring-target", "a rename slot `name = path` takes a name path (`x = dox.b`)", c);
          }
        }
      } else {
        fail("bad-destructuring-target",
             "a destructuring slot is a name or a rename `name = path`: assign a field or a selector in its own "
             "statement",
             k);
      }
    }
  }
  if (!at(Token_kind::assign))
    fail("bad-destructuring-operator", "a destructuring assignment takes `=`, not a compound operator", nullptr);
}

// The RHS field path of a rename slot (grammar.js `_slot_path`): a word or a
// dotted run of words -- `b`, `dox.b`, `deep.payload.inner.value` -- as an
// identifier / dot_expression. It ends the slot: `(x = a[0])`, `(x = f(a).b)`,
// `(x = y:U8)` and `(x = y@[1])` are errors.
Ast* Parser::parse_slot_path() {
  uint32_t start = cur().start_byte;
  if (!at(Token_kind::ident)) {
    if (at(Token_kind::type_word)) error_type_word_name(cur(), "a field name");
    error("bad-destructuring-target", "a rename slot `name = path` takes a name path (`x = dox.b`)");
  }
  Ast* head = leaf(Kind::identifier);
  Ast* path = head;
  if (at(Token_kind::dot)) {
    path = node(Kind::dot_expression, start);
    head->field = Field::f_item;
    path->add(head);
    while (at(Token_kind::dot)) {
      advance();  // '.'
      if (at(Token_kind::type_word)) error_type_word_name(cur(), "a field name");
      path->add(ident_leaf("expected-field", "expected a field name after '.'"));
    }
    finish(path, start);
  }
  if (!at(Token_kind::comma) && !at(Token_kind::rparen)) {
    if (at(Token_kind::colon) || at(Token_kind::coloncolon))
      error("typed-destructuring",
            "a destructuring slot carries no type (`const (a, b) = f()`; the types come from the right-hand side)");
    error("bad-destructuring-target", "a rename slot `name = path` takes a name path (`x = dox.b`)");
  }
  return path;
}

// Reinterpret a `tuple` parsed at statement start as an `lvalue_list` once a
// trailing '=' confirms it is a destructuring-assignment LHS (`(a, x=b.c) =
// rhs`). Each tuple item becomes an `lvalue_item`; a `local = path` item (parsed
// optimistically as a nested assignment) becomes a `named_lvalue`.
Ast* Parser::tuple_to_lvalue_list(Ast* tup) {
  std::vector<Ast*> items;
  items.reserve(tup->kids.size());
  for (Ast* it : tup->kids) {
    uint32_t s  = it->start_byte;
    Ast*     li = node(Kind::lvalue_item, s);
    if (it->kind == Kind::assignment) {
      Ast* name = nullptr;
      Ast* lv   = nullptr;
      for (Ast* c : it->kids) {
        if (c->field == Field::f_lvalue)
          name = c;
        else if (c->field == Field::f_rvalue)
          lv = c;
      }
      require_binding_name(name, /*dotted=*/false, "a destructuring target");  // `(a.b = x) = f()`
      require_binding_name(lv, /*dotted=*/true, "a destructuring field path");  // `(a = f(x)) = g()`
      if (is_keyword_name(name)) {  // `(if = x) = f()`: the local is a plain name
        Diag d;
        d.code     = "reserved-word-as-name";
        d.category = std::string(kCategorySyntax);
        d.message  = "'" + std::string(buf_.data() + name->start_byte, name->end_byte - name->start_byte) +
                     "' is a reserved word, so it cannot be a destructuring target";
        d.span     = span_bytes(name->start_byte, name->end_byte);
        throw Parse_error(std::move(d));
      }
      Ast* nl = node(Kind::named_lvalue, s);
      if (name) {
        name->field = Field::f_name;
        nl->add(name);
      }
      if (lv) {
        lv->field = Field::f_lvalue;
        nl->add(lv);
      }
      finish(nl, s);
      li->add(nl);
    } else if (it->kind == Kind::identifier) {
      reject_type_word_name(it, "an assignment target");  // `(U8, b) = f()`
      Ast* ti   = node(Kind::typed_identifier, s);
      it->field = Field::f_identifier;
      ti->add(it);
      finish(ti, s);
      li->add(ti);
    } else if (it->kind == Kind::typed_field) {
      // `(in:U8, b) = f()`: a keyword before `:` is a tuple FIELD name, which a
      // destructuring target can not be (grammar.js `_field_word`).
      if (!it->kids.empty() && is_keyword_name(it->kids.front())) {
        Diag d;
        d.code     = "bad-assignment-target";
        d.category = std::string(kCategorySyntax);
        d.message  = "a reserved word cannot be an assignment target";
        d.span     = span_bytes(it->start_byte, it->end_byte);
        throw Parse_error(std::move(d));
      }
      it->kind = Kind::typed_identifier;
      li->add(it);
    } else if (it->kind == Kind::typed_identifier) {
      li->add(it);
    } else {
      // complex lvalue (dot_expression / member_selection / bit_selection);
      // anything else -- a call, a literal, a nested tuple -- is an error:
      // `(a, f(x)) = g()`, `(a, 1) = g()`, `((a, b), c) = g()`.
      require_lvalue(it);
      it->field = Field::f_identifier;
      li->add(it);
    }
    finish(li, s);
    li->field = Field::f_item;
    items.push_back(li);
  }
  tup->kind = Kind::lvalue_list;
  tup->kids = std::move(items);
  return tup;
}

Ast* Parser::parse_arg_tuple() {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lparen, "expected-paren", "expected '(' to open arguments");
  Bracket_guard _bg(*this);
  Ast* at_ = node(Kind::arg_tuple, start);
  skip_leading_commas(Token_kind::rparen);
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
  uint32_t start = cur().start_byte;
  // `f(pub = 1)`, `__sum(as = (a, b))`: a keyword before `=` names the argument
  // (grammar.js `_field_word`); before `:` it is an error (no typed argument).
  Ast* e = nullptr;
  if (at_field_word()) {
    e = leaf(Kind::identifier);
    if (!at(Token_kind::assign)) error("expected-eq", "expected '=' after the argument name");
  } else {
    if (at_kw(Keyword::kw_ref)) return parse_ref_identifier();
    e = parse_expression();
  }
  if (at(Token_kind::assign)) {
    require_binding_name(e, /*dotted=*/true, "an argument name");  // `f(U8=1)`, `f(a[0]=1)`
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
  // A header ends at a newline, also inside brackets: `f(y=if a` newline `(b)
  // { .. })` is an error, as at statement level (grammar.js `_line_end`).
  Scope_guard _sg(*this);
  // `unique if` is an anonymous `unique` token in the grammar; emit it as an
  // anonymous marker (the consumer reads its text to lower a `unique_if` →
  // Hotmux instead of a priority mux chain).
  if (at_kw(Keyword::kw_unique)) {
    Ast* u  = arena_.make(Kind::identifier, cur().start_byte, cur().end_byte);
    u->named = false;
    ife->add(u);
    advance();
  }
  if (!accept_kw(Keyword::kw_if)) error("expected-if", "expected 'if'");
  // first branch (inline, no wrapper node): init?, condition, code
  {
    std::vector<Ast*> items;
    items.push_back(parse_stmt_item());
    while (at(Token_kind::semicolon)) {
      advance();
      while (at(Token_kind::semicolon)) advance();
      if (at(Token_kind::lbrace)) break;
      items.push_back(parse_stmt_item());
    }
    Ast* cond = items.back();
    require_condition(cond);
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
    // tree-sitter inlines each elif branch under the SAME field names as the
    // leading if-arm (init / condition / code), not a distinct `elif` field —
    // the consumer flattens all branches by those names.
    std::vector<Ast*> items;
    items.push_back(parse_stmt_item());
    while (at(Token_kind::semicolon)) {
      advance();
      while (at(Token_kind::semicolon)) advance();
      if (at(Token_kind::lbrace)) break;
      items.push_back(parse_stmt_item());
    }
    Ast* cond = items.back();
    require_condition(cond);
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
  if (accept_kw(Keyword::kw_else)) {
    ife->add(parse_scope(), Field::f_else);
  }
  finish(ife, start);
  return ife;
}

Ast* Parser::parse_match_expression() {
  uint32_t start = cur().start_byte;
  // The subject and each arm end at a newline, also inside brackets (see
  // parse_if_expression).
  Scope_guard _sg(*this);
  advance();  // match
  Ast* m = node(Kind::match_expression, start);
  // optional init then condition
  std::vector<Ast*> items;
  items.push_back(parse_stmt_item());
  while (at(Token_kind::semicolon)) {
    advance();
    while (at(Token_kind::semicolon)) advance();
    if (at(Token_kind::lbrace)) break;
    items.push_back(parse_stmt_item());
  }
  Ast* cond = items.back();
  require_condition(cond);
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
  uint32_t arm_count = 0;
  while (!at(Token_kind::rbrace) && !eof() && !at_kw(Keyword::kw_else)) {
    // Optional leading arm operator (`== 1`, `< 5`, `case (..)`). In the grammar
    // it is an anonymous token sharing the arm's `condition` field; emit it as an
    // anonymous marker (get_text gives the operator) so the consumer can apply
    // the implied comparison.
    bool is_arm_op = false;
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
        is_arm_op = true;
        break;
      default:
        is_arm_op = cur().is_kw(Keyword::kw_and) || cur().is_kw(Keyword::kw_or) ||
                    cur().is_kw(Keyword::kw_has) || cur().is_kw(Keyword::kw_case) ||
                    cur().is_kw(Keyword::kw_in) || cur().is_kw(Keyword::kw_equals) ||
                    cur().is_kw(Keyword::kw_does);
        break;
    }
    if (is_arm_op) {
      Ast* op  = arena_.make(Kind::identifier, cur().start_byte, cur().end_byte);
      op->named = false;
      m->add(op, Field::f_condition);
      advance();
    }
    m->add(parse_expression(), Field::f_condition);
    m->add(parse_scope(), Field::f_code);
    ++arm_count;
  }
  // A `match` must branch on something: at least one arm is required. A bare
  // `match x { }` (and the degenerate else-only `match x { else {…} }`) is
  // meaningless — flag it with a clean syntax error instead of letting an
  // armless `unique_if` slip downstream into an opaque "undriven result" /
  // internal error.
  if (arm_count == 0) {
    error("empty-match", "match has no arms — add at least one `== value { … }` arm");
  }
  // `else` is OPTIONAL. A match whose arms cover the full key space (e.g. a
  // `u2` selector with `==0 ==1 ==2 ==3`) needs no else. When omitted the
  // unmatched case is by definition unreachable: in hardware it is a
  // don't-care (the Hotmux none-of slot keeps the pre-match value), and at
  // comptime a constant selector that somehow misses every arm leaves the
  // result undriven — equivalent to an implicit `else { assert(false) }`.
  if (accept_kw(Keyword::kw_else)) {
    m->add(parse_scope(), Field::f_else_code);
  }
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
    // A storage keyword (or `comptime`/`ref`) is never an array length: `x:[mut]U8`,
    // `x:[reg]U8`, `x:[stage]` are errors in both parsers (grammar.js: at the
    // `[` of a type the keyword token wins over the identifier reading).
    if (is_array_length_keyword(peek(1)))
      error_reserved_name(peek(1), "reserved-word-as-name",
                          "'" + std::string(peek(1).text) + "' is a reserved word, so it cannot be an array length");
    at_->add(parse_select(/*allow_empty=*/true), Field::f_length);  // `[]` defers the size to the initializer
    // base (optional) — must not cross a statement terminator (a new line after
    // `[]` begins the next statement, it is not the array's base type).
    if (!term_stop()) {
      if (at(Token_kind::lbracket)) at_->add(parse_type(), Field::f_base);
      else if (is_primitive_type_word(cur())) at_->add(parse_primitive_type(), Field::f_base);
      else if ((is_lambda_kind(cur()) || at_kw(Keyword::kw_pub)) && looks_like_lambda())
        at_->add(parse_lambda(), Field::f_base);
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
  if (is_lambda_kind(cur())) {
    // lambda_type: a body-less lambda SIGNATURE in type position —
    // `call_method1: comb(a:U8, b:U3) -> (foo:U8, bar:U33)` — the typed
    // interface of a cpp() binding or any lambda-valued field
    // (07-typesystem.md "The typed interface is the source of truth").
    // The `type X = comb(...)` statement form keeps its own func_type
    // branch in parse_type_statement (matches tree-sitter's shape).
    uint32_t start = cur().start_byte;
    Ast*     lt = node(Kind::lambda_type, start);
    if (at_kw(Keyword::kw_comb)) {
      lt->add(leaf(Kind::comb_lambda), Field::f_func_type);
    } else if (at_kw(Keyword::kw_mod)) {
      lt->add(leaf(Kind::mod_lambda), Field::f_func_type);
    } else if (at_kw(Keyword::kw_pipe)) {
      Ast* pl = node(Kind::pipe_lambda, cur().start_byte);
      advance();
      if (at(Token_kind::lbracket)) pl->add(parse_select(), Field::f_depth);
      finish(pl, pl->start_byte);
      lt->add(pl, Field::f_func_type);
    } else {  // fluid
      Ast* fl = node(Kind::fluid_lambda, cur().start_byte);
      advance();
      if (at(Token_kind::lbracket)) fl->add(parse_attribute_sq(), Field::f_config);
      finish(fl, fl->start_byte);
      lt->add(fl, Field::f_func_type);
    }
    lt->add(parse_function_definition_decl());
    finish(lt, start);
    return lt;
  }
  if (is_primitive_type_word(cur())) return parse_primitive_type();
  // `pub` only heads a lambda (grammar.js reads it as the keyword in a type
  // position too): `x:pub` is an error.
  if (at_kw(Keyword::kw_pub)) error("expected-type", "expected a type");
  // expression_type: identifier (dotted), constant, tuple, if/match, call.
  // tree-sitter wraps EVERY non-primitive type expression in an `expression_type`
  // node (grammar rule `expression_type`); the consumer gates tuple-shape / inline
  // type lowering on that wrapper, so reproduce it for the non-identifier forms
  // too (the identifier/dotted form is wrapped in its own branch below).
  uint32_t start = cur().start_byte;
  if (at(Token_kind::lparen) || at_constant() || at_kw(Keyword::kw_if) || at_kw(Keyword::kw_unique) ||
      at_kw(Keyword::kw_match)) {
    Ast* et    = node(Kind::expression_type, start);
    Ast* inner = at(Token_kind::lparen)                                   ? parse_paren()
                 : at_constant()                                          ? parse_constant()
                 : (at_kw(Keyword::kw_if) || at_kw(Keyword::kw_unique))   ? parse_if_expression()
                                                                          : parse_match_expression();
    et->add(inner);
    finish(et, start);
    return et;
  }
  if (at_kw(Keyword::kw_enum) && peek(1).kind == Token_kind::lparen && !peek(1).terminator_before) error_enum_expression();  // `type V = enum(a)`
  if (cur().kind == Token_kind::ident) {
    Ast* et = node(Kind::expression_type, start);
    Ast* id;
    if (cur().text == "nil") {
      id = arena_.make(Kind::identifier, cur().start_byte, cur().end_byte);
      advance();
    } else {
      id = leaf(Kind::identifier);
    }
    et->add(id);
    while (at(Token_kind::dot) && peek(1).kind == Token_kind::ident) {
      advance();
      et->add(leaf(Kind::identifier), Field::f_item);
    }
    // A `(` that opens a new line starts a new statement (02-basics: a line
    // starting with `(` never continues the previous one): `mut r:Foo` newline
    // `(a)` is a declaration plus a tuple statement, not the type call `Foo(a)`.
    if (at(Token_kind::lparen) && !term_stop()) {
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

// A generic ARGUMENT -- a call-site bind (`f<N=…>`, or positional `f<…>`) or a
// generic-parameter default (`<N=…>`); 06-functions.md: a type (which also
// covers a literal, a name and a dotted field `cfg.w`), or a POSTFIX attribute
// read of a (dotted) name written bare: `x.[bits]`, `cfg.w.[max]`. The read is
// built exactly like its expression spelling -- attribute_read(argument=
// identifier | dot_expression, attrs=attribute_list+) -- so consumers lower it
// like any attribute read. Everything else needs parentheses: an operator
// expression (`<N=(W*2)>`; bare, the closing `>`/`>>` would be ambiguous) or a
// call (`<N=(g(x))>`; a bare `g(x)` is a type-position call). Mirrors
// grammar.js `_generic_value`.
Ast* Parser::parse_generic_value() {
  // Only a word parse_type would read as a plain (dotted) NAME can head the
  // read; primitive types, literals (`true`/`false` too), lambda kinds and
  // if/match keep their type-grammar meaning.
  const bool plain_name = at(Token_kind::ident) && !is_lambda_kind(cur()) && !at_constant() &&
                          !at_kw(Keyword::kw_if) && !at_kw(Keyword::kw_unique) && !at_kw(Keyword::kw_match);
  // `<N=U8.[max]>`: a type word heads the read too (never a dotted path:
  // `U8.x` is an error), exactly as in an expression (parse_type_word_operand).
  if (at(Token_kind::type_word) && peek(1).kind == Token_kind::dot && peek(2).kind == Token_kind::lbracket) {
    uint32_t start = cur().start_byte;
    Ast*     ar    = node(Kind::attribute_read, start);
    Ast*     head  = arena_.make(Kind::identifier, cur().start_byte, cur().end_byte);
    advance();
    ar->add(head, Field::f_argument);
    while (at(Token_kind::dot) && peek(1).kind == Token_kind::lbracket) {
      advance();  // '.'
      ar->add(parse_attribute_list(), Field::f_attrs);
    }
    finish(ar, start);
    return ar;
  }
  if (plain_name) {
    // Lookahead only: `name ('.' name)*` must be followed by `.[` (else it is
    // the dotted type/name path, `cfg.w`).
    size_t k = 1;
    while (peek(k).kind == Token_kind::dot && peek(k + 1).kind == Token_kind::ident) k += 2;
    if (peek(k).kind == Token_kind::dot && peek(k + 1).kind == Token_kind::lbracket) {
      uint32_t start = cur().start_byte;
      Ast*     head  = leaf(Kind::identifier);
      if (at(Token_kind::dot) && peek(1).kind == Token_kind::ident) {
        Ast* de = node(Kind::dot_expression, start);
        de->add(head, Field::f_item);
        while (at(Token_kind::dot) && peek(1).kind == Token_kind::ident) {
          advance();  // '.'
          de->add(leaf(Kind::identifier));
        }
        finish(de, start);
        head = de;
      }
      Ast* ar = node(Kind::attribute_read, start);
      ar->add(head, Field::f_argument);
      while (at(Token_kind::dot) && peek(1).kind == Token_kind::lbracket) {
        advance();  // '.'
        ar->add(parse_attribute_list(), Field::f_attrs);
      }
      finish(ar, start);
      return ar;
    }
  }
  // A negative literal is a generic value (`f<N=-3>`, owner ruling 102) though
  // no type (`x:-3` is an error): it reads like `<N=3>`, and the sign is part
  // of the literal (grammar.js `_negative_generic_value`), so `<N=- 3>` stays
  // an error.
  if (at(Token_kind::minus) && peek(1).kind == Token_kind::integer && peek(1).start_byte == cur().end_byte) {
    uint32_t start = cur().start_byte;
    advance();  // '-'
    Ast* lit        = leaf(Kind::integer_literal);
    lit->start_byte = start;  // the literal's span includes the sign
    Ast* c          = node(Kind::constant, start);
    c->add(lit);
    finish(c, start);
    Ast* et = node(Kind::expression_type, start);
    et->add(c);
    finish(et, start);
    return et;
  }
  return parse_type();
}

// A type word in a TYPE position (grammar.js `_primitive_type`): `U<N>` and
// `Unsigned` are a `uint_type`, `S<N>` and `Signed` a `sint_type` (both with an
// optional constraint tuple: `Unsigned(bits=8, max=300)`), and `Bool`,
// `String`, `Clock`, `Reset` the `bool_type`/`string_type`/`clock_type`/
// `reset_type` leaves.
Ast* Parser::parse_primitive_type() {
  uint32_t         start = cur().start_byte;
  std::string_view w     = cur().text;
  Kind             k;
  bool             sized = false;  // takes a constraint tuple
  if (w == "Bool") {
    k = Kind::bool_type;
  } else if (w == "String") {
    k = Kind::string_type;
  } else if (w == "Clock") {
    k = Kind::clock_type;
  } else if (w == "Reset") {
    k = Kind::reset_type;
  } else {
    k     = (w == "Unsigned" || w[0] == 'U') ? Kind::uint_type : Kind::sint_type;
    sized = true;
  }
  Ast* pt = node(k, start);
  advance();
  // `Unsigned(bits=3)`; a `(` on the next line is a new statement (see parse_type).
  if (sized && at(Token_kind::lparen) && !term_stop()) pt->add(parse_paren(), Field::f_constraint);
  finish(pt, start);
  return pt;
}

Ast* Parser::parse_typed_identifier(bool allow_default, const char* bind_role) {
  uint32_t start = cur().start_byte;
  // Every caller BINDS a name: a loop induction variable, a generic parameter, a
  // lambda's single named output, an arg_list parameter. The sites where the
  // grammar deliberately tolerates keyword spellings -- tuple FIELD names,
  // attribute names, named call arguments, `.field` selectors, enum VARIANTS --
  // never route through here, so one check covers them all without over-reaching.
  if (bind_role) require_plain_name(bind_role);
  Ast*     ti = node(Kind::typed_identifier, start);
  ti->add(leaf(Kind::identifier), Field::f_identifier);
  if (at(Token_kind::at)) {
    advance();
    ti->add(parse_timing_slot(), Field::f_timing);
  }
  if (at(Token_kind::colon) || at(Token_kind::coloncolon)) ti->add(parse_type_cast(), Field::f_type);
  // A generic-parameter DECLARATION default (`<T, N=1>`, todo 3g B): the
  // default follows `=`. Only inside a generic list (allow_default); an
  // `arg_list` parameter default is handled by parse_arg_list itself. Parse it
  // as a generic ARGUMENT (parse_generic_value — the same grammar as a `<…>`
  // bind: a type, a constant like `1`, a (dotted) name, or a bare postfix
  // attribute read `Z.[bits]`) NOT a full expression: a full expression would
  // swallow the closing `>` as a greater-than operator (the C++ `>>` template
  // ambiguity).
  if (allow_default && at(Token_kind::assign)) {
    advance();  // '='
    ti->add(parse_generic_value(), Field::f_definition);
  }
  finish(ti, start);
  return ti;
}

Ast* Parser::parse_typed_identifier_list(bool allow_default, const char* bind_role) {
  uint32_t start = cur().start_byte;
  Ast*     list = node(Kind::typed_identifier_list, start);
  while (at(Token_kind::comma)) advance();
  list->add(parse_typed_identifier(allow_default, bind_role), Field::f_item);
  while (accept(Token_kind::comma)) {
    while (at(Token_kind::comma)) advance();
    if (at(Token_kind::rparen) || at(Token_kind::gt) || at(Token_kind::rbracket)) break;
    list->add(parse_typed_identifier(allow_default, bind_role), Field::f_item);
  }
  finish(list, start);
  return list;
}

Ast* Parser::parse_attribute_sq() {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lbracket, "expected-bracket", "expected '[' to open attributes");
  Bracket_guard _bg(*this);
  Ast* sq = node(Kind::attribute_sq, start);
  skip_leading_commas(Token_kind::rbracket);
  while (!at(Token_kind::rbracket) && !eof()) {
    // _attribute_item: _expression | ref_identifier | attribute_assignment. A
    // keyword before `=` names the attribute (`::[comptime = 1]`).
    if (at_kw(Keyword::kw_ref) && !at_field_word()) {
      sq->add(parse_ref_identifier(), Field::f_item);
    } else {
      Ast* e = nullptr;
      if (at_field_word()) {
        e = leaf(Kind::identifier);
        if (!at(Token_kind::assign)) error("expected-eq", "expected '=' after the attribute name");
      } else {
        e = parse_expression();
      }
      if (at(Token_kind::assign)) {
        require_binding_name(e, /*dotted=*/true, "an attribute name");  // `x::[U4=1]`
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

Ast* Parser::parse_select(bool allow_empty) {
  uint32_t start = cur().start_byte;
  expect(Token_kind::lbracket, "expected-bracket", "expected '['");
  Bracket_guard _bg(*this);
  Ast* sel = node(Kind::select, start);
  // A selector (`a[i]`, `a#[i]`, `pipe[N]`) holds one index or range
  // (grammar.js `select`); only an array LENGTH may be empty (`x:[]U8`,
  // grammar.js `array_length`). `a[]`, `a#[]`, `pipe[]` are errors.
  if (at(Token_kind::rbracket) && !allow_empty)
    error("empty-select", "a selector needs an index or a range (`a[i]`, `a[lo..=hi]`); `[]` is empty");
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
  Bracket_guard _bg(*this);
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
  sl->add(parse_stmt_item(), Field::f_item);
  while (at(Token_kind::semicolon)) {
    advance();
    while (at(Token_kind::semicolon)) advance();
    if (at(Token_kind::lbrace) || eof()) break;
    sl->add(parse_stmt_item(), Field::f_item);
  }
  finish(sl, start);
  return sl;
}

Ast* Parser::parse_init_clause() { return parse_stmt_list(); }

}  // namespace prpparse
