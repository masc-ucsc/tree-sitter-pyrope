// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "parser.hpp"

#include <string>

#include "gtest/gtest.h"
#include "source_buffer.hpp"

using namespace prpparse;

static std::string sexp(const std::string& s) {
  Source_buffer buf("t.prp", s);
  Parser        p(buf);
  return p.parse().to_sexp();
}
static bool parses(const std::string& s) {
  Source_buffer buf("t.prp", s);
  try {
    // The lexer runs in the Parser constructor, so lexical errors (e.g. an
    // unterminated comment) throw here — keep it inside the try.
    Parser p(buf);
    p.parse();
    return true;
  } catch (const Parse_error&) {
    return false;
  }
}
// The Diag of the first syntax error, or a Diag with an empty code when the
// input parses. Lets a test assert on the MESSAGE, not just accept/reject.
static Diag diag_of(const std::string& s) {
  Source_buffer buf("t.prp", s);
  try {
    Parser p(buf);
    p.parse();
  } catch (const Parse_error& e) {
    return e.diag;
  }
  Diag none;
  none.code.clear();
  return none;
}
static size_t count(const std::string& hay, const std::string& needle) {
  size_t n = 0, p = 0;
  while ((p = hay.find(needle, p)) != std::string::npos) {
    ++n;
    p += needle.size();
  }
  return n;
}

TEST(Parser, Assignment) {
  auto s = sexp("a = 1\n");
  EXPECT_NE(s.find("assignment"), std::string::npos);
  EXPECT_NE(s.find("integer_literal"), std::string::npos);
}

TEST(Parser, FlatBinaryChain) {
  // `a + b + c` is a single flat expression_item with two op_add children.
  EXPECT_EQ(count(sexp("x = a + b + c\n"), "op_add"), 2u);
  EXPECT_EQ(count(sexp("x = a + b + c\n"), "expression_item"), 1u);
}

TEST(Parser, PrecedenceTiers) {
  // `a + b * c` => '*' nested under a '+' chain (separate tiers).
  auto s = sexp("x = a + b * c\n");
  EXPECT_NE(s.find("op_add"), std::string::npos);
  EXPECT_NE(s.find("op_mul"), std::string::npos);
}

TEST(Parser, TupleConcatOp) {
  // `a ++ b` is the tuple-concat operator, an `op_tuple_concat` in the
  // `binary_other` tier (so `a ++ b ++ c` is one flat chain, like `&`/`|`).
  auto s = sexp("x = a ++ b\n");
  EXPECT_NE(s.find("op_tuple_concat"), std::string::npos);
  EXPECT_NE(s.find("binary_other_op"), std::string::npos);
  EXPECT_EQ(count(sexp("x = a ++ b ++ c\n"), "op_tuple_concat"), 2u);
  EXPECT_EQ(count(sexp("x = a ++ b ++ c\n"), "expression_item"), 1u);
  // `++` must not be mis-lexed as two `+` tokens.
  EXPECT_EQ(count(sexp("x = a ++ b\n"), "op_add"), 0u);
}

TEST(Parser, TupleConcatAssign) {
  // `++=` compound assign lowers to an `assign_tuple_concat` operator.
  auto s = sexp("acc ++= b\n");
  EXPECT_NE(s.find("assign_tuple_concat"), std::string::npos);
}

TEST(Parser, IfElse) { EXPECT_TRUE(parses("if a == b {\n  c = 1\n} else {\n  c = 2\n}\n")); }

TEST(Parser, Match) {
  EXPECT_TRUE(parses("y = match x {\n  == 0 { 1 }\n  == 1 { 2 }\n  else { 3 }\n}\n"));
}

// `else` is optional: an exhaustive match (e.g. every value of a bounded key)
// needs no else — the unmatched case is an unreachable don't-care.
TEST(Parser, MatchNoElse) {
  EXPECT_TRUE(parses("y = match x {\n  == 0 { 1 }\n  == 1 { 2 }\n  == 2 { 3 }\n  == 3 { 4 }\n}\n"));
}

// A match must have at least one arm — a bare `match x { }` (or else-only) is
// a syntax error, not an armless unique_if slipping downstream.
TEST(Parser, MatchEmptyRejected) {
  EXPECT_FALSE(parses("y = match x {\n}\n"));
  EXPECT_FALSE(parses("y = match x {\n  else { 1 }\n}\n"));
}

TEST(Parser, Lambda) {
  EXPECT_TRUE(parses("comb f(a:u8, b:u8) -> (c:u8) {\n  c = a + b\n}\n"));
}

TEST(Parser, ForLoop) { EXPECT_TRUE(parses("for i in 0..=3 {\n  s = s + i\n}\n")); }

TEST(Parser, VirtualSemicolonStopsBinaryChain) {
  // `g(x)` then a bare `step` statement: `step` must not bind as a binary op.
  EXPECT_TRUE(parses("comb f() {\n  g(x)\n  step\n}\n"));
}

TEST(Parser, SuffixDoesNotCrossNewline) {
  // `foo()` then `[1,2,a]` on the next line are two statements, not a select.
  EXPECT_TRUE(parses("comb f() {\n  foo()\n  [1, 2, a].bar()\n}\n"));
}

TEST(Parser, ContinuationInsideParens) {
  // Inside (), a newline before `and` keeps continuing the expression.
  EXPECT_TRUE(parses("x = (a\n     and b)\n"));
}

TEST(Parser, ParenGroupVsTuple) {
  // A bare `(a)` is a single-item tuple (tree-sitter parity); `paren_group` is
  // emitted only when the parens head a suffix chain (`(a).foo`, `(a)#[..]`).
  EXPECT_NE(sexp("x = (a)\n").find("(tuple"), std::string::npos);
  EXPECT_EQ(sexp("x = (a)\n").find("paren_group"), std::string::npos);
  EXPECT_NE(sexp("x = (a, b)\n").find("(tuple"), std::string::npos);
  EXPECT_NE(sexp("x = (a).foo\n").find("paren_group"), std::string::npos);
}

TEST(Parser, Destructuring) { EXPECT_TRUE(parses("const (x, b) = (true, c)\n")); }

TEST(Parser, TypedLvalueAssignment) { EXPECT_TRUE(parses("x = (a = 0, b:bool = nil)\n")); }

TEST(Parser, BitSelectAndAttributes) {
  EXPECT_TRUE(parses("y = e#[3]\n"));
  EXPECT_TRUE(parses("z = a.[comptime]\n"));
}

TEST(Parser, RejectsTrailingOperator) { EXPECT_FALSE(parses("a = b +\n")); }
TEST(Parser, RejectsTwoStatementsOnOneLine) { EXPECT_FALSE(parses("a b c\n")); }
TEST(Parser, RejectsUnclosedParen) { EXPECT_FALSE(parses("a = (1, 2\n")); }
TEST(Parser, RejectsAssignmentNoTerminator) { EXPECT_FALSE(parses("a = 1 b = 2\n")); }

// Declaration-assignment with a complex lvalue (bit-select / dot / index).
TEST(Parser, ComplexDeclLvalue) {
  EXPECT_TRUE(parses("mut trans:u2 = nil\nmut trans#[0] = 1\n"));
  EXPECT_TRUE(parses("mut a.b = 1\n"));
}

// A (pub-prefixed) lambda used as an expression operand.
TEST(Parser, LambdaAsOperand) {
  EXPECT_TRUE(parses("reg internal:u8 = pub comb m(a) -> (r) { r = a }\n"));
}

// A reserved word is a NAME only in its backticked form. Bare, it is rejected at
// every site that BINDS a name -- a declaration or a parameter -- because a bare
// binding used to mint a name nothing else could refer to. Everywhere the grammar
// deliberately tolerates keyword spellings (tuple FIELD names, attribute names,
// named call arguments) is untouched: `mut mem = (const type = 1, ...)` is the
// documented __memory config API and must keep parsing.
TEST(Parser, KeywordAsIdentifier) {
  // bound names: bare rejected, backticked accepted
  EXPECT_FALSE(parses("mut if = 3\n"));
  EXPECT_TRUE(parses("mut `if` = 3\n"));
  EXPECT_FALSE(parses("comb f() { reg reg:u8 = 0 }\n"));
  EXPECT_TRUE(parses("comb f() { reg `reg`:u8 = 0 }\n"));
  EXPECT_FALSE(parses("const as = 0sb1010\n"));
  EXPECT_TRUE(parses("const `as` = 0sb1010\n"));
  // parameters bind names too (a Verilog port named `in`)
  EXPECT_FALSE(parses("pub mod f(in:u8) -> (o:u8@[0]) { o = 1 }\n"));
  EXPECT_TRUE(parses("pub mod f(`in`:u8) -> (o:u8@[0]) { o = `in` }\n"));
  // ...and the message points at the escape
  Diag d = diag_of("comb f() { reg reg:u8 = 0 }\n");
  EXPECT_EQ(d.code, "reserved-word-as-name");
  EXPECT_NE(d.message.find("cannot be a variable name"), std::string::npos) << d.message;
  EXPECT_NE(d.hint.find("`reg`"), std::string::npos) << d.hint;

  // more bindings: induction variable, generic parameter, single named output
  EXPECT_FALSE(parses("mut y = (1,2)\nfor if in y { mut z = 1 }\n"));
  EXPECT_TRUE(parses("mut y = (1,2)\nfor `if` in y { mut z = 1 }\n"));
  EXPECT_FALSE(parses("comb f<if>(a:u8) -> (o:u8) { o = a }\n"));
  EXPECT_FALSE(parses("comb f(a:u8) -> in:u8 { `in` = a }\n"));

  // NOT a binding: keyword spellings the grammar tolerates stay legal.
  // enum VARIANTS are field names -- and `enum E ( .. )` shares parse_arg_list
  // with parameter lists, so it must opt out explicitly or `enum E (in, out)`
  // would reject while the identical `enum E = (in, out)` (parse_paren) accepts.
  EXPECT_TRUE(parses("const Op = enum(and, or, not)\n"));
  EXPECT_TRUE(parses("enum E ( in, out )\n"));
  EXPECT_TRUE(parses("enum E = (in, out)\n"));
  // a lambda NAME is not a variable: `mod pipe(..)`, `comb tick(..)` are idiomatic
  EXPECT_TRUE(parses("pub mod pipe(a:u8) -> (o:u8@[0]) { o = a }\n"));
  EXPECT_TRUE(parses("comb tick(a:u8) -> (o:u8) { o = a }\n"));
  EXPECT_TRUE(parses("mut a = (const reg=1)\nmut x = a.reg\n"));  // field selector
  EXPECT_TRUE(parses("mut m = (\n  const type = 1,\n  const size = 16,\n)\n"));  // __memory config
  EXPECT_TRUE(parses("mut x = 1\nmut y = x.[comptime]\n"));                    // attribute read
  EXPECT_TRUE(parses("mut y = f(type=1)\n"));                                   // named call arg
  EXPECT_TRUE(parses("wrap if.total = r + a\n"));                               // assignment, not a binding
  EXPECT_TRUE(parses("comb f() { stage[2] y = 1 }\n"));                         // real pipelining decl
}

// A reserved word standing where a NAME is required is a syntax error -- Pyrope
// spells such an identifier with backticks, and any sequence may sit between
// them. The diagnostic has to SAY that, because the caret never points there:
// `stage[0] = a` reports on the `=`, which reads as a broken expression. Verilog
// imports hit this constantly (bedrock's br_delay_valid names its shift register
// `stage`, so `stage[0] = in` is the natural translation).
TEST(Parser, ReservedWordAsNameExplainsTheBacktickEscape) {
  struct Case {
    const char* src;
    const char* word;
    const char* code;
  };
  const Case cases[] = {
      {"stage[0] = a\n", "stage", "reserved-word-as-name"},   // the pipelining slot ate `[0]`
      {"reg[0] = a\n", "reg", "reserved-word-as-name"},       // used to declare a name literally called `[0]`
      {"pub reg[0] = a\n", "reg", "reserved-word-as-name"},   // blame the storage word, not `pub`
      {"mut:u8 = 3\n", "mut", "reserved-word-as-name"},
      {"o = ref[1]\n", "ref", "reserved-word-as-name"},       // rvalue position
      {"tick = 3\n", "tick", "expected-expression"},          // construct keeps its own message
      {"test[0] = a\n", "test", "expected-test-name"},
  };
  for (const auto& c : cases) {
    Diag d = diag_of(c.src);
    EXPECT_EQ(d.code, c.code) << c.src;
    // The help line must show the escaped spelling to copy.
    EXPECT_NE(d.hint.find(std::string("`") + c.word + "`"), std::string::npos)
        << c.src << " hint=" << d.hint;
  }
  // Escaping it is the documented fix, so the escaped forms must parse.
  EXPECT_TRUE(parses("`stage`[0] = a\n"));
  EXPECT_TRUE(parses("`reg`[0] = a\n"));
  EXPECT_TRUE(parses("o = `ref`[1]\n"));
}

// The escape hint belongs to the statement that OPENS with the word. A keyword
// construct that really is one must not collect it, and neither may a failure in
// an assignment's rvalue -- `stage[2] y = <broken>` is a real pipelining
// declaration whose problem is the right-hand side, not the word `stage`.
TEST(Parser, ReservedWordHintDoesNotLeak) {
  EXPECT_TRUE(parses("comb f() { stage[2] y = 1 }\n"));  // the real declaration still parses
  EXPECT_TRUE(diag_of("comb f() {\n stage[2] y = (\n}\n").hint.empty());
  EXPECT_TRUE(diag_of("comb f() {\n mut x = (\n}\n").hint.empty());
  EXPECT_TRUE(diag_of("comb f() {\n if a {\n b = \n}\n}\n").hint.empty());
}

// Multi-line block comments: newlines INSIDE a /* */ do not terminate a
// statement; only real line breaks do.
TEST(Parser, MultiLineBlockComment) {
  EXPECT_TRUE(parses("a = 1 /* multi\n line */\nb = 2\n"));   // real newline after */ -> 2 stmts
  EXPECT_FALSE(parses("a = 1 /* multi\n line */ b = 2\n"));   // b on same line as */ -> error
  EXPECT_TRUE(parses("mut x = /* inline */ 5\n"));
}

// Unterminated /* is a lexical error, localized at the opening /*.
TEST(Parser, UnterminatedBlockComment) {
  EXPECT_FALSE(parses("a = 1\n/* never closed\nb = 2\n"));
}

// A '[' that begins a new line is a new statement, not another select on the
// previous line's expression.
TEST(Parser, SelectDoesNotCrossNewline) {
  EXPECT_TRUE(parses("x = a[1].foo[xx]\n[4].foo[yy] = y\n"));
}

// `true(...)` / `false(...)` use the word as an identifier (call target).
TEST(Parser, BoolLiteralAsCallTarget) {
  EXPECT_TRUE(parses("y = true(a does bool)\n"));
  // but a plain bool literal is still a bool literal.
  EXPECT_NE(sexp("x = true\n").find("bool_literal"), std::string::npos);
}

// --sexp with every whitespace run collapsed to one space, so a test can pin a
// whole subtree on one line.
static std::string flat_sexp(const std::string& s) {
  std::string out;
  bool        ws = false;
  for (char c : sexp(s)) {
    if (c == ' ' || c == '\n' || c == '\t') {
      ws = true;
      continue;
    }
    if (ws && !out.empty()) out += ' ';
    ws = false;
    out += c;
  }
  return out;
}

// Ruling 2026-09-27: a generic ARGUMENT may be a POSTFIX value written bare --
// an attribute read of a (dotted) name -- at the call site (named or
// positional). It is the same attribute_read node the expression `a.[bits]`
// builds (argument = identifier / dot_expression), matching grammar.js.
TEST(Parser, GenericBindPostfixAttributeRead) {
  const std::string read_a = "(attribute_read argument: (identifier) attrs: (attribute_list name: (identifier)))";
  EXPECT_NE(flat_sexp("r = low<N=a.[bits]>(x=b)\n")
                .find("generic: (generic_type_list item: (arg_assignment lvalue: (identifier) rvalue: " + read_a + "))"),
            std::string::npos);
  // dotted field then attribute: the head is a dot_expression, as in an expression
  EXPECT_NE(flat_sexp("m = addn<N=cfg.w.[max]>(x=b)\n")
                .find("rvalue: (attribute_read argument: (dot_expression item: (identifier) (identifier)) attrs: "
                      "(attribute_list name: (identifier)))"),
            std::string::npos);
  // positional bind (same fork as `f<T>(x)`: the generic call wins)
  EXPECT_NE(flat_sexp("r = f<a.[bits]>(x=b)\n").find("generic: (generic_type_list item: " + read_a + ")"),
            std::string::npos);
  // chained reads, mixed with a type and a dotted field (which stays a type)
  auto mixed = flat_sexp("r = f<T=u8, N=a.[bits].[max], M=cfg.w>(x=b)\n");
  EXPECT_EQ(count(mixed, "attribute_list"), 2u);
  EXPECT_NE(mixed.find("rvalue: (uint_type)"), std::string::npos);
  EXPECT_NE(mixed.find("rvalue: (expression_type (identifier) item: (identifier))"), std::string::npos);
  // declaration, nested call argument, comparison operand, spaced / split `.[`
  EXPECT_TRUE(parses("const v = addn<N=b.[bits]>(x=b)\n"));
  EXPECT_TRUE(parses("cassert(addn<N=b.[bits]>(x=1) == 9)\n"));
  EXPECT_TRUE(parses("r = f<N=a.[bits]>(x) + g<M=b.[max]>(y)\n"));
  EXPECT_TRUE(parses("r = f<N=a . [bits]>(x)\n"));
  EXPECT_TRUE(parses("r = f<N=a.\n[bits]\n>(x)\n"));
  // the parenthesized spellings keep working
  EXPECT_TRUE(parses("r = f<N=(a.[bits]), M=(W*2)>(x=b)\n"));
  // `a < b.[bits]` with no `>(` is still a comparison
  EXPECT_EQ(flat_sexp("c = a < b.[bits]\n").find("generic_type_list"), std::string::npos);
}

// Same ruling, DECLARATION half: a generic-parameter default may be a bare
// postfix attribute read (`comb f<N=Z.[bits]>`), on comb/mod and type aliases.
TEST(Parser, GenericDefaultPostfixAttributeRead) {
  const std::string def = "definition: (attribute_read argument: (identifier) attrs: (attribute_list name: (identifier)))";
  EXPECT_NE(flat_sexp("comb addn<N=Z.[bits]>(x:u8) -> (y:u8) { y = x }\n").find(def), std::string::npos);
  EXPECT_NE(flat_sexp("mod low<N=Z.[bits]>(x:u8) -> (y:u8) { y = x }\n").find(def), std::string::npos);
  EXPECT_NE(flat_sexp("type Row<N=Z.[bits]> = unsigned(bits=N)\n").find(def), std::string::npos);
  EXPECT_NE(flat_sexp("mod low<N=cfg.w.[max], T=u8>(x:u8) -> (y:u8) { y = x }\n")
                .find("definition: (attribute_read argument: (dot_expression item: (identifier) (identifier))"),
            std::string::npos);
  EXPECT_TRUE(parses("mod lowp<N=(Z.[bits])>(x:u8) -> (y:u8) { y = x }\n"));
}

// Only the postfix form is new: an operator expression still needs parentheses
// (a bare `>`/`>>` would be ambiguous), and member/bit selections or an
// attribute read of a call are not generic arguments. Mirrors the `:error`
// cases in test/corpus/generic_postfix.txt.
TEST(Parser, GenericArgumentOperatorNeedsParens) {
  EXPECT_FALSE(parses("r = f<N=W*2>(x=b)\n"));
  EXPECT_FALSE(parses("r = f<N=a.[bits]+1>(x=b)\n"));
  EXPECT_FALSE(parses("r = f<N=g(a).[bits]>(x=b)\n"));
  EXPECT_FALSE(parses("r = f<N=a[0]>(x=b)\n"));
  EXPECT_FALSE(parses("r = f<N=a#[0..<2]>(x=b)\n"));
  EXPECT_FALSE(parses("r = f<N=a.[bits]>>(x=b)\n"));
  EXPECT_FALSE(parses("r = f<N=u8.[bits]>(x=b)\n"));
  EXPECT_EQ(diag_of("comb f<N=W*2>(x:u8) -> (y:u8) { y = x }\n").code, "expected-gt");
  EXPECT_EQ(diag_of("comb f<N=Z.[bits]+1>(x:u8) -> (y:u8) { y = x }\n").code, "expected-gt");
  EXPECT_EQ(diag_of("comb f<N=Z.[bits]>>(x:u8) -> (y:u8) { y = x }\n").code, "expected-gt");
}

// `x < …` is only a GUESSED generic call: when the right operand is no generic
// argument (`-1`, `~b`, `...b`, `[1, 2]`, `{a}`, `!b`) the guess must back off
// to the comparison instead of failing with "expected a type" (tree-sitter
// accepts all of these). A list that opens with a named bind (`f<N=…`) cannot
// be a comparison, so it stays committed and keeps its precise error.
TEST(Parser, GenericGuessBacksOffToComparison) {
  for (const char* src : {"y = x < -1\n", "if x < -1 {\n  y = 1\n}\n", "y = x < ~b\n", "y = x < ...b\n",
                          "y = x < [1, 2]\n", "y = x < {a}\n", "y = x < !b\n", "while i < -3 {\n  i += 1\n}\n",
                          "y = x < -1 and z > 3\n", "y = g(a < b, N = -3)\n"}) {
    SCOPED_TRACE(src);
    EXPECT_EQ(diag_of(src).code, "");
    auto t = flat_sexp(src);
    EXPECT_EQ(t.find("generic_type_list"), std::string::npos);
    EXPECT_NE(t.find("(op_lt)"), std::string::npos);
  }
  // committed: the named bind's value error is reported where it is
  auto d = diag_of("r = f<N=-3>(a)\n");
  EXPECT_EQ(d.code, "expected-type");
  EXPECT_EQ(d.span.start_col, 9u);
}
