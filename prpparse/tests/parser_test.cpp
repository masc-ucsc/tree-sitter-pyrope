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

// Reserved words used as identifiers where the keyword construct is not valid
// (matches tree-sitter's contextual keyword handling).
TEST(Parser, KeywordAsIdentifier) {
  EXPECT_TRUE(parses("mut if = 3\n"));
  EXPECT_TRUE(parses("wrap if.total = r + a\n"));
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
