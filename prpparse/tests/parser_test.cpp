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
  Parser        p(buf);
  try {
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
  EXPECT_NE(sexp("x = (a)\n").find("paren_group"), std::string::npos);
  EXPECT_NE(sexp("x = (a, b)\n").find("(tuple"), std::string::npos);
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
