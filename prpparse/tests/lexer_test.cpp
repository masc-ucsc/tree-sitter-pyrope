// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lexer.hpp"

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "source_buffer.hpp"

using namespace prpparse;

// Owns the Source_buffer so token text (string_views into it) stays valid for
// the lifetime of the test.
struct Lexed {
  Source_buffer      buf;
  std::vector<Token> t;
  explicit Lexed(const std::string& s) : buf("t.prp", s) {
    Lexer l(buf);
    t = l.tokenize();
  }
};

TEST(Lexer, Basic) {
  Lexed lx("a = 1\n");
  auto& t = lx.t;
  ASSERT_GE(t.size(), 4u);
  EXPECT_EQ(t[0].kind, Token_kind::ident);
  EXPECT_TRUE(t[0].text == "a");
  EXPECT_EQ(t[1].kind, Token_kind::assign);
  EXPECT_EQ(t[2].kind, Token_kind::integer);
  EXPECT_EQ(t.back().kind, Token_kind::eof);
}

TEST(Lexer, Keywords) {
  Lexed lx("if mut comptime");
  auto& t = lx.t;
  EXPECT_TRUE(t[0].is_kw(Keyword::kw_if));
  EXPECT_TRUE(t[1].is_kw(Keyword::kw_mut));
  EXPECT_TRUE(t[2].is_kw(Keyword::kw_comptime));
}

TEST(Lexer, LongestMatchOperators) {
  Lexed                   lx("..= ..< ..+ .. :: -> <<= >>= << >> == != <= >=");
  auto&                   t = lx.t;
  std::vector<Token_kind> got;
  for (auto& x : t)
    if (x.kind != Token_kind::eof) got.push_back(x.kind);
  std::vector<Token_kind> exp = {
      Token_kind::range_incl, Token_kind::range_excl, Token_kind::range_count, Token_kind::dotdot,
      Token_kind::coloncolon, Token_kind::arrow,      Token_kind::assign_shl,  Token_kind::assign_sra,
      Token_kind::shl,        Token_kind::shr,        Token_kind::eq,          Token_kind::ne,
      Token_kind::le,         Token_kind::ge};
  EXPECT_EQ(got, exp);
}

TEST(Lexer, Numbers) {
  Lexed lx("0xF_F 1_000K 0b?1 0o17 42");
  auto& t = lx.t;
  EXPECT_TRUE(t[0].text == "0xF_F");
  EXPECT_TRUE(t[1].text == "1_000K");
  EXPECT_TRUE(t[2].text == "0b?1");
  EXPECT_TRUE(t[3].text == "0o17");
  EXPECT_TRUE(t[4].text == "42");
}

TEST(Lexer, OrEqIsOneToken) {
  Lexed lx("a or= b");
  auto& t = lx.t;
  EXPECT_EQ(t[1].kind, Token_kind::assign_log_or);
}

TEST(Lexer, NegativeIsOperatorPlusNumber) {
  // plan decision 7: '-' is always an operator token; parser folds the sign.
  Lexed lx("-5");
  auto& t = lx.t;
  EXPECT_EQ(t[0].kind, Token_kind::minus);
  EXPECT_EQ(t[1].kind, Token_kind::integer);
}

TEST(Lexer, TerminatorBeforeOnNewline) {
  Lexed lx("a = 1\nb = 2\n");
  auto& t = lx.t;
  for (auto& x : t)
    if (x.text == "b") EXPECT_TRUE(x.terminator_before);
}

TEST(Lexer, ContinuationOperatorNoTerminator) {
  Lexed lx("a\n+ b\n");
  auto& t = lx.t;  // '+' continues the previous line
  for (auto& x : t)
    if (x.kind == Token_kind::plus) EXPECT_FALSE(x.terminator_before);
}

TEST(Lexer, TrailingCommentDoesNotSuppressTerminator) {
  Lexed lx("a = 1 // comment\nb = 2\n");
  auto& t = lx.t;
  for (auto& x : t)
    if (x.text == "b") EXPECT_TRUE(x.terminator_before);
}

TEST(Lexer, InterpolatedStringIsOneToken) {
  Lexed lx("\"hi {a + b} end\"\n");
  auto& t = lx.t;
  EXPECT_EQ(t[0].kind, Token_kind::istring);
  EXPECT_TRUE(t[0].text == "\"hi {a + b} end\"");
}

TEST(Lexer, BacktickIdentifier) {
  Lexed lx("`weird name` = 1\n");
  auto& t = lx.t;
  EXPECT_EQ(t[0].kind, Token_kind::ident);
  EXPECT_TRUE(t[0].text == "`weird name`");
}

TEST(Lexer, BlockCommentInternalNewlineIsNotTerminator) {
  // `a = 1 /* x\n y */ b` : the newline is inside the comment, b on the same
  // physical line as */, so there is NO statement terminator before b.
  Lexed lx("a = 1 /* x\n y */ b = 2\n");
  auto& t = lx.t;
  for (auto& x : t)
    if (x.text == "b") EXPECT_FALSE(x.terminator_before);
}

TEST(Lexer, RealNewlineAfterBlockCommentIsTerminator) {
  Lexed lx("a = 1 /* x\n y */\nb = 2\n");  // real newline after */ -> terminator
  auto& t = lx.t;
  for (auto& x : t)
    if (x.text == "b") EXPECT_TRUE(x.terminator_before);
}
