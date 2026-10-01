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
  Lexed lx("0xF_F 1_000K 0ub?1 0o17 42");
  auto& t = lx.t;
  EXPECT_TRUE(t[0].text == "0xF_F");
  EXPECT_TRUE(t[1].text == "1_000K");
  EXPECT_TRUE(t[2].text == "0ub?1");
  EXPECT_TRUE(t[3].text == "0o17");
  EXPECT_TRUE(t[4].text == "42");
}

TEST(Lexer, OrEqIsOneToken) {
  Lexed lx("a or= b");
  auto& t = lx.t;
  EXPECT_EQ(t[1].kind, Token_kind::assign_log_or);
}

// `and=` / `or=` is the compound assignment only after an operand; after `(`,
// `,` or a declaration keyword the word is a NAME followed by `=` (a field or
// argument), with or without a space: `(and=1)` == `(and = 1)`.
TEST(Lexer, AndEqIsANameWhereNoOperandPrecedes) {
  Lexed lx("(and=1) f(or=1) (const and=3) x.and and=1");
  auto& t = lx.t;
  std::vector<Token_kind> got;
  for (auto& x : t)
    if (x.kind != Token_kind::eof) got.push_back(x.kind);
  using K = Token_kind;
  std::vector<Token_kind> exp = {K::lparen, K::ident, K::assign, K::integer, K::rparen,                 // (and=1)
                                 K::ident,  K::lparen, K::ident, K::assign, K::integer, K::rparen,      // f(or=1)
                                 K::lparen, K::ident, K::ident, K::assign, K::integer, K::rparen,       // (const and=3)
                                 K::ident,  K::dot,    K::ident, K::assign_log_and, K::integer};        // x.and and=1
  EXPECT_EQ(got, exp);
  EXPECT_TRUE(t[1].is_kw(Keyword::kw_and));
}

// `%` starts a continuation line like every other binary operator; `@` does not.
TEST(Lexer, PercentContinuesAtDoesNot) {
  Lexed lx("a\n% b\nc\n");
  auto& t = lx.t;
  for (auto& x : t)
    if (x.kind == Token_kind::percent) EXPECT_FALSE(x.terminator_before);
  // A line never starts with `@` (spec 2026-09-29 §8): a lexical error, also
  // inside brackets; a newline inside a block comment is no line break.
  for (const char* src : {"c\n@[1]\n", "(c\n  @[1])\n", "c // x\n@[1]\n"}) {
    EXPECT_THROW(Lexed{src}, Parse_error) << src;
  }
  EXPECT_NO_THROW(Lexed{"c /* x\n */ @[1]\n"});
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

TEST(Lexer, BacktickIdentifierMustNotBeEmpty) {
  EXPECT_THROW(Lexed{"``"}, Parse_error);
  EXPECT_NO_THROW(Lexed{"` `"});
  EXPECT_NO_THROW(Lexed{R"(`\`` `\n` `\0`)"});
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

TEST(Lexer, BlockCommentsNest) {
  // Block comments NEST: `/* a /* b */ c */` is ONE comment. The inner `*/`
  // closes the inner `/*`, so `x` (after the inner close, still inside the outer
  // comment) is NOT a token; only `y` after the OUTER close is.
  Lexed lx("/* a /* b */ x = 1 */ y\n");
  auto& t = lx.t;
  ASSERT_GE(t.size(), 2u);
  EXPECT_EQ(t[0].kind, Token_kind::ident);
  EXPECT_TRUE(t[0].text == "y");
  EXPECT_EQ(t[1].kind, Token_kind::eof);
}

TEST(Lexer, DeeplyNestedBlockComment) {
  Lexed lx("/* 1 /* 2 /* 3 */ 2 */ 1 */ z\n");  // 3-deep; only `z` survives
  auto& t = lx.t;
  ASSERT_GE(t.size(), 2u);
  EXPECT_TRUE(t[0].text == "z");
  EXPECT_EQ(t[1].kind, Token_kind::eof);
}

// The type words -- `U<N>`/`S<N>` (a `U` or `S` and ASCII digits only),
// `Unsigned`, `Signed`, `Bool`, `String`, `Clock`, `Reset` -- lex as the
// reserved type_word token, never as an identifier; the backticked spelling,
// words that only start like one, and every lowercase spelling (`u4`, `bool`,
// `unsigned`, `i32`) are identifiers.
TEST(Lexer, TypeWords) {
  Lexed lx("U4 S20 U0 U99999999 Unsigned Signed Bool String Clock Reset "
           "`U4` U Sx U8x S_0 `u4` `s20` `i32` I8 `bool` `unsigned` `string` Clocks Booleans u i s u_8 u8x");
  auto& t = lx.t;
  for (int i = 0; i < 10; ++i) EXPECT_EQ(t[i].kind, Token_kind::type_word) << t[i].text;
  for (int i = 10; i < 29; ++i) EXPECT_EQ(t[i].kind, Token_kind::ident) << t[i].text;
  for (int i = 10; i < 29; ++i) EXPECT_EQ(t[i].kw, Keyword::none) << t[i].text;
}

// The old lowercase spellings are ORDINARY identifiers (owner ruling
// 2026-09-30, reversing spec 2026-09-29 §7): no diagnostic, no keyword, no type
// word. The exact new type words stay reserved.
TEST(Lexer, OldTypeSpellingsAreIdentifiers) {
  for (const char* w : {"u8", "s20", "s2", "i32", "u0", "s1", "i0", "bool", "boolean", "unsigned", "signed", "string"}) {
    Lexed lx(std::string("x = ") + w + "\n");
    bool  seen = false;
    for (const auto& tk : lx.t)
      if (tk.text == w) {
        EXPECT_EQ(tk.kind, Token_kind::ident) << w;
        EXPECT_EQ(tk.kw, Keyword::none) << w;
        seen = true;
      }
    EXPECT_TRUE(seen) << w;
  }
  // a backticked spelling is the same ordinary name (backticks are just unneeded)
  Lexed bt("`u8` `s2` `bool`");
  for (int i = 0; i < 3; ++i) EXPECT_EQ(bt.t[i].kind, Token_kind::ident) << i;
  // the exact new spellings stay type words
  Lexed tw("U8 S2 Bool Unsigned");
  for (int i = 0; i < 4; ++i) EXPECT_EQ(tw.t[i].kind, Token_kind::type_word) << i;
  EXPECT_NO_THROW(Lexed{"x = \"a u8 bool string\" // u8 bool\ny = 'signed'\n"});
}

// Identifier characters are letters (non-ASCII too), digits and `_` only
// (spec 2026-09-29 §8). Only ASCII blanks separate tokens (owner ruling 108): a
// non-ASCII blank (NBSP, U+3000, the BOM, ZWSP) is an error.
TEST(Lexer, IdentifierCharacters) {
  Lexed ok("caf\u00e9 \u03b1\u03b2 \u53d8\u91cf x\u0663 `_0` __ `_` `a\u00d7b`");
  EXPECT_EQ(ok.t.size(), 9u);
  for (size_t i = 0; i + 1 < ok.t.size(); ++i) EXPECT_EQ(ok.t[i].kind, Token_kind::ident) << i;
  for (const char* src : {"a\u00a0b", "a \u3000b", "\ufeffa", "a\u200b b"}) {
    EXPECT_THROW(Lexed{src}, Parse_error) << src;
  }
  EXPECT_NO_THROW(Lexed{"a \"\u00a0\" // \u3000\n"});  // strings and comments hold any text
  for (const char* src : {"a\U0001F600b", "a\u00d7b", "a\u2014b", "a\u00b7b", "a\u20acb", "a\u2192b",
                          "a\uff0bb", "a\u00b2b", "a\u2160b", "e\u0301", "_", "x = _\n", "a._\n", "\u0663x"}) {
    EXPECT_THROW(Lexed{src}, Parse_error) << src;
  }
}

// String escapes (spec 2026-09-29 §8): exactly \n \t \r \\ \" \' \0 \xNN
// \u{1..6 hex} and \`, in "..." strings and backticked names; `\u{..}` never
// opens a hole; `{}` is an empty hole and a lone `}` is an error.
TEST(Lexer, StringEscapes) {
  Lexed ok(R"("\n\t\r\\\"\'\0\x4F\u{41}\u{1F600}\u{10FFFF}\`" `a\`b\n` "{{x}} }} {y}")");
  EXPECT_EQ(ok.t.size(), 4u);
  for (const char* src : {R"("\{")", R"("\}")", R"("\q")", R"("\u0041")", R"("\u{}")", R"("\u{1234567}")",
                          R"("\x4")", R"("\xZZ")", R"(`a\qb`)", R"(`\{`)", R"("{}")", R"("{ }")",
                          R"("{ /* c */ }")", R"("a } b")", R"("}}}")", R"("{x}}")"}) {
    EXPECT_THROW(Lexed{src}, Parse_error) << src;
  }
}

// `{{` in a double-quoted string is a literal brace, not a hole opener, so the
// string ends at its own closing quote: `"{{"` is one token.
TEST(Lexer, StringDoubledBraces) {
  Lexed lx("\"{{\" \"{{x}}\" \"{{{x}}}\"");
  auto& t = lx.t;
  ASSERT_EQ(t.size(), 4u);
  for (int i = 0; i < 3; ++i) EXPECT_EQ(t[i].kind, Token_kind::istring);
  EXPECT_TRUE(t[0].text == "\"{{\"");
}
