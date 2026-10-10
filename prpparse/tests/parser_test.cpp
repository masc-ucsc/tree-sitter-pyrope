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

TEST(Parser, BinaryLiteralsRequireSignedness) {
  for (const char* literal : {"0ub1100", "0sb1110", "0ub?1", "0sb1_?0", "-0ub1100", "-0sb1110", "0UB101", "0SB101", "0uB101", "0Sb101"}) {
    EXPECT_TRUE(parses(std::string("const x = ") + literal + "\n")) << literal;
  }
  for (const char* literal : {"0b1100", "0B1100", "-0b1100", "-0B1100", "0b?1", "0B?1"}) {
    EXPECT_FALSE(parses(std::string("const x = ") + literal + "\n")) << literal;
  }
}

// `?` is reserved for a future validity check, not an expression today.
TEST(Parser, ReservedQuestionMarkRejected) {
  EXPECT_FALSE(parses("const x = ?\n"));
  EXPECT_FALSE(parses("const x = ? + 1\n"));
  EXPECT_FALSE(parses("const x = foo?\n"));
  EXPECT_FALSE(parses("foo?\n"));
  EXPECT_FALSE(parses("const x = (?, 1)\n"));
  EXPECT_FALSE(parses("f(?)\n"));
  EXPECT_FALSE(parses("if ? { x = 1 }\n"));
  EXPECT_FALSE(parses("const x = \"{?}\"\n"));
  EXPECT_TRUE(parses("const x = 0ub?\nconst y = 0sb?\n"));
  EXPECT_TRUE(parses("const `foo?` = '?'\n"));
}

TEST(Parser, ReservedPlaceholderNames) {
  EXPECT_FALSE(parses("const _0 = 1\n"));
  EXPECT_FALSE(parses("const _1a = 1\n"));
  EXPECT_FALSE(parses("const _23abc = 1\n"));
  EXPECT_FALSE(parses("const _1A9 = 1\n"));
  EXPECT_FALSE(parses("const _1é = 1\n"));
  EXPECT_FALSE(parses("const _١x = 1\n"));
  EXPECT_FALSE(parses("const (_0, b) = pair\n"));
  EXPECT_FALSE(parses("comb f(_1a:U8) -> (r:U8) { r = 0 }\n"));
  EXPECT_FALSE(parses("comb f<_2T>() -> () {}\n"));
  EXPECT_FALSE(parses("for _3i in xs {}\n"));
  EXPECT_FALSE(parses("const x = obj._0\n"));
  EXPECT_FALSE(parses("const x = (_1a=1)\n"));
  EXPECT_FALSE(parses("const x = _0\n"));
  EXPECT_FALSE(parses("_0 = 1\n"));
  EXPECT_TRUE(parses("const `_0` = 1\n"));
  EXPECT_TRUE(parses("const `_1a` = 1\n"));
  EXPECT_TRUE(parses("const `_١x` = 1\n"));
  EXPECT_TRUE(parses("const _a1 = 1\n"));
  EXPECT_TRUE(parses("const _1_a = 1\n"));
  EXPECT_TRUE(parses("const _1_ = 1\n"));
  EXPECT_TRUE(parses("const __0 = 1\n"));
  EXPECT_TRUE(parses("const x = obj.`_0`\n"));
}

TEST(Parser, EmptyBacktickIdentifierRejected) {
  EXPECT_FALSE(parses("const `` = 1\n"));
  EXPECT_FALSE(parses("const x = ``\n"));
  EXPECT_FALSE(parses("const x = obj.``\n"));
  EXPECT_FALSE(parses("comb ``() -> () {}\n"));
  EXPECT_FALSE(parses("comb f(``) -> () {}\n"));
  EXPECT_FALSE(parses("const x = (``=1)\n"));
  EXPECT_FALSE(parses("const x = \"{``}\"\n"));
  EXPECT_EQ(diag_of("const `` = 1\n").code, "empty-identifier");
  EXPECT_TRUE(parses("const ` ` = 1\n"));
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
  EXPECT_TRUE(parses("comb f(a:U8, b:U8) -> (c:U8) {\n  c = a + b\n}\n"));
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

TEST(Parser, TypedLvalueAssignment) { EXPECT_TRUE(parses("x = (a = 0, b:Bool = nil)\n")); }

TEST(Parser, BitSelectAndAttributes) {
  EXPECT_TRUE(parses("y = e#[3]\n"));
  EXPECT_TRUE(parses("z = a.[`comptime`]\n"));
}

TEST(Parser, RejectsTrailingOperator) { EXPECT_FALSE(parses("a = b +\n")); }
TEST(Parser, RejectsTwoStatementsOnOneLine) { EXPECT_FALSE(parses("a b c\n")); }
TEST(Parser, RejectsUnclosedParen) { EXPECT_FALSE(parses("a = (1, 2\n")); }
TEST(Parser, RejectsAssignmentNoTerminator) { EXPECT_FALSE(parses("a = 1 b = 2\n")); }

// Declaration-assignment with a complex lvalue (bit-select / dot / index).
TEST(Parser, ComplexDeclLvalue) {
  EXPECT_TRUE(parses("mut trans:U2 = nil\nmut trans#[0] = 1\n"));
  EXPECT_TRUE(parses("mut a.b = 1\n"));
}

// A (pub-prefixed) lambda used as an expression operand.
TEST(Parser, LambdaAsOperand) {
  EXPECT_TRUE(parses("reg internal:U8 = pub comb m(a) -> (r) { r = a }\n"));
}

// A reserved word is a NAME only in its backticked form. Bare, it is rejected at
// every site that BINDS a name -- a declaration or a parameter -- because a bare
// binding used to mint a name nothing else could refer to. Fields, attributes,
// methods and arguments use the same rule; their escaped forms stay legal.
TEST(Parser, KeywordAsIdentifier) {
  // bound names: bare rejected, backticked accepted
  EXPECT_FALSE(parses("mut if = 3\n"));
  EXPECT_TRUE(parses("mut `if` = 3\n"));
  EXPECT_FALSE(parses("comb f() { reg reg:U8 = 0 }\n"));
  EXPECT_TRUE(parses("comb f() { reg `reg`:U8 = 0 }\n"));
  EXPECT_FALSE(parses("const as = 0sb1010\n"));
  EXPECT_TRUE(parses("const `as` = 0sb1010\n"));
  // parameters bind names too (a Verilog port named `in`)
  EXPECT_FALSE(parses("pub mod f(in:U8) -> (o:U8@[0]) { o = 1 }\n"));
  EXPECT_TRUE(parses("pub mod f(`in`:U8) -> (o:U8@[0]) { o = `in` }\n"));
  // ...and the message points at the escape
  Diag d = diag_of("comb f() { reg reg:U8 = 0 }\n");
  EXPECT_EQ(d.code, "reserved-word-as-name");
  EXPECT_NE(d.message.find("cannot be a variable name"), std::string::npos) << d.message;
  EXPECT_NE(d.hint.find("`reg`"), std::string::npos) << d.hint;

  // more bindings: induction variable, generic parameter, single named output
  EXPECT_FALSE(parses("mut y = (1,2)\nfor if in y { mut z = 1 }\n"));
  EXPECT_TRUE(parses("mut y = (1,2)\nfor `if` in y { mut z = 1 }\n"));
  EXPECT_FALSE(parses("comb f<if>(a:U8) -> (o:U8) { o = a }\n"));
  EXPECT_FALSE(parses("comb f(a:U8) -> in:U8 { `in` = a }\n"));

  // Field and method names require the same escaping as bindings.
  // enum VARIANTS are field names.
  EXPECT_TRUE(parses("enum Op = (`and`, `or`)\n"));
  EXPECT_TRUE(parses("enum E = (`in`, out)\n"));
  // Lambda names follow the same rule.
  EXPECT_TRUE(parses("pub mod `pipe`(a:U8) -> (o:U8@[0]) { o = a }\n"));
  // ...but `tick`/`step` are no name anywhere (owner ruling 99): a lambda, a
  // field, a named argument spelled so needs the backticks.
  EXPECT_FALSE(parses("comb tick(a:U8) -> (o:U8) { o = a }\n"));
  EXPECT_TRUE(parses("comb `tick`(a:U8) -> (o:U8) { o = a }\n"));
  EXPECT_FALSE(parses("mut x = (mut step = 0)\n"));
  EXPECT_FALSE(parses("x.step = 1\n"));
  EXPECT_FALSE(parses("y = f(tick=1)\n"));
  EXPECT_TRUE(parses("mut x = (mut `step` = 0)\nx.`step` = 1\n"));
  EXPECT_TRUE(parses("mut a = (const `reg`=1)\nmut x = a.`reg`\n"));  // field selector
  EXPECT_TRUE(parses("mut m = (\n  const `type` = 1,\n  const size = 16,\n)\n"));  // __memory config
  EXPECT_TRUE(parses("mut x = 1\nmut y = x.[`comptime`]\n"));                    // attribute read
  EXPECT_TRUE(parses("mut y = f(`type`=1)\n"));                                   // named call arg
  EXPECT_TRUE(parses("wrap `if`.total = r + a\n"));                               // assignment, not a binding
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
      {"mut:U8 = 3\n", "mut", "reserved-word-as-name"},
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
  EXPECT_TRUE(parses("x = a[1].foo[xx]\n[4].foo(yy)\n"));
}

// `true` / `false` are always the literals where a value may start (the
// grammar's `bool_literal` keywords): never a call target or an assignment
// target. Mirrors test/corpus/reserved_words.txt "Bool literal".
TEST(Parser, BoolLiteralIsNeverAName) {
  EXPECT_FALSE(parses("y = true(a does Bool)\n"));
  EXPECT_FALSE(parses("true = 3\n"));
  EXPECT_FALSE(parses("(true, b) = (1, 2)\n"));
  EXPECT_NE(sexp("x = true\n").find("bool_literal"), std::string::npos);
  EXPECT_EQ(count(sexp("x = f(true)\ny = (false, true)\nz = a or true\n"), "(bool_literal)"), 4u);
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
  auto mixed = flat_sexp("r = f<T=U8, N=a.[bits].[max], M=cfg.w>(x=b)\n");
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
  EXPECT_NE(flat_sexp("comb addn<N=Z.[bits]>(x:U8) -> (y:U8) { y = x }\n").find(def), std::string::npos);
  EXPECT_NE(flat_sexp("mod low<N=Z.[bits]>(x:U8) -> (y:U8) { y = x }\n").find(def), std::string::npos);
  EXPECT_NE(flat_sexp("type Row<N=Z.[bits]> = Unsigned(bits=N)\n").find(def), std::string::npos);
  EXPECT_NE(flat_sexp("mod low<N=cfg.w.[max], T=U8>(x:U8) -> (y:U8) { y = x }\n")
                .find("definition: (attribute_read argument: (dot_expression item: (identifier) (identifier))"),
            std::string::npos);
  EXPECT_TRUE(parses("mod lowp<N=(Z.[bits])>(x:U8) -> (y:U8) { y = x }\n"));
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

  EXPECT_EQ(diag_of("comb f<N=W*2>(x:U8) -> (y:U8) { y = x }\n").code, "expected-gt");
  EXPECT_EQ(diag_of("comb f<N=Z.[bits]+1>(x:U8) -> (y:U8) { y = x }\n").code, "expected-gt");
  EXPECT_EQ(diag_of("comb f<N=Z.[bits]>>(x:U8) -> (y:U8) { y = x }\n").code, "expected-gt");
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
  auto d = diag_of("r = f<N=~3>(a)\n");
  EXPECT_EQ(d.code, "expected-type");
  EXPECT_EQ(d.span.start_col, 9u);
  // A negative literal is a generic value (owner ruling 102), the sign glued
  // to the digits as in grammar.js.
  EXPECT_EQ(diag_of("r = f<N=-3>(a)\n").code, "");
  EXPECT_EQ(diag_of("comb g<N=-1>(a) -> (b) { b = a }\n").code, "");
  EXPECT_EQ(diag_of("r = f<N=- 3>(a)\n").code, "expected-type");
  EXPECT_EQ(diag_of("mut x:-3 = 1\n").code, "expected-type");
}

// A generic list opens only with a `<` glued to the callee (owner ruling 107;
// test/corpus/parser_consistency.txt "Generic call: ..."): a `<` after a blank
// is always a comparison, also on the next line; a comment glued to the `<`
// leaves it glued.
TEST(Parser, GenericNeedsGluedLt) {
  for (const char* src : {"r = a < b > (c)\n", "r = f  <T>(x)\n", "r = a\n  < b\n", "r = (f\n<T>(x))\n"}) {
    SCOPED_TRACE(src);
    EXPECT_EQ(diag_of(src).code, "");
    auto t = flat_sexp(src);
    EXPECT_EQ(t.find("generic_type_list"), std::string::npos);
    EXPECT_NE(t.find("(op_lt)"), std::string::npos);
  }
  for (const char* src : {"r = f<T>(x)\n", "r = h(x=a<b, y=c>(d + 1))\n", "r = f /* c */<T>(x)\n"}) {
    SCOPED_TRACE(src);
    EXPECT_EQ(diag_of(src).code, "");
    EXPECT_NE(flat_sexp(src).find("generic_type_list"), std::string::npos);
  }
  auto d = diag_of("r = f <N=3>(x)\n");
  EXPECT_EQ(d.code, "spaced-generic");
  EXPECT_EQ(d.span.start_col, 7u);
  EXPECT_EQ(diag_of("r = f\n  <N=3>(x)\n").code, "spaced-generic");
}

// Mirrors test/corpus/string_comment_markers.txt: comment openers inside a
// double-quoted string are string text, never the start of a comment.
TEST(Parser, StringHoldsCommentMarkers) {
  auto s = sexp("s = \"/* a\"\nr = a /* c */\n");
  EXPECT_EQ(count(s, "(assignment\n"), 2u);
  EXPECT_TRUE(parses("s = \"http://x\"\n"));
  EXPECT_TRUE(parses("s = \"  a /* b */ c  \"\n"));
}

// Mirrors the rest of test/corpus/string_comment_markers.txt: `//` and `/*`
// are text anywhere in a string outside a `{...}` hole (also after a hole or an
// escaped quote), a real comment may follow the closing quote, and single-
// quoted strings never hold comments.
TEST(Parser, StringCommentMarkersAreText) {
  for (const char* src : {"s = \"//x\"\n", "s = \"/*x\"\n", "s = \"/* a */\"\n", "s = \"a/* b */c\"\n",
                          "s = \"x{a}//c\"\n", "s = \"a{b}// c\"\n", "s = \"\\\"//\"\n", "s = '//x'\n",
                          "t = 'http://x.y/z'\n", "s = \"http://x {a}\"\n", "puts(\"a // b\")\n"}) {
    EXPECT_TRUE(parses(src)) << src;
  }
  EXPECT_EQ(count(sexp("s = \"ab\" // real\nt = \"/*\" /* real */\n"), "(assignment\n"), 2u);
  EXPECT_EQ(count(sexp("s = \"x{a}//c\"\n"), "(identifier)"), 2u);
}

// A '}' inside a comment, a nested string or a backtick name within an
// interpolation hole does not close the hole: the parser must sub-parse the
// SAME window the lexer found (Lexer::istring_hole_end). A naive brace count
// ended `"x{a /* } */ + 1}y"` at the commented '}' and silently evaluated the
// hole as just `a` (no error), so lhd printed the wrong value.
TEST(Parser, InterpolationHoleSkipsCommentsAndStrings) {
  EXPECT_EQ(count(sexp("s = \"x{a /* } */ + 1}y\"\n"), "op_add"), 1u);
  EXPECT_EQ(count(sexp("s = \"x{a /* /* } */ } */ + 1}y\"\n"), "op_add"), 1u);
  EXPECT_EQ(count(sexp("s = \"x{a // }\n + 1}y\"\n"), "op_add"), 1u);
  EXPECT_EQ(count(sexp("s = \"x{f(\"}}\") + 1}y\"\n"), "op_add"), 1u);
  EXPECT_EQ(count(sexp("s = \"x{f('}') + 1}y\"\n"), "op_add"), 1u);
  EXPECT_EQ(count(sexp("s = \"x{`a}b` + 1}y\"\n"), "op_add"), 1u);
  EXPECT_EQ(count(sexp("s = \"x{a /* c */}y\"\n"), "(identifier)"), 2u);
  EXPECT_FALSE(parses("s = \"x{a /* } y\"\n"));  // unterminated comment in a hole
}

// Mirrors test/corpus/nested_comments.txt: block comments NEST, so
// `/* a /* b */ c */` is ONE comment (grammar: src/scanner.c scan_comment).
TEST(Parser, NestedBlockComments) {
  EXPECT_TRUE(parses("/* a /* b */ c */\n"));
  EXPECT_EQ(count(sexp("a = 1\n/* x /* y */\n   z */\nb = 2\n"), "(assignment\n"), 2u);
  EXPECT_EQ(count(sexp("a = 1 /* x /* y */ z */\nb = 2\n"), "(assignment\n"), 2u);
  EXPECT_EQ(count(sexp("a = 1 /* x /* y */ z */ + 2\n"), "op_add"), 1u);
  // A comment before an operator on the next line does not terminate: the
  // automatic-semicolon rule skips nested comments too.
  auto s = sexp("a = 1\n/* x /* y */ z */ + 2\nb = 3\n  /* p /* q */ */\n  or true\n");
  EXPECT_EQ(count(s, "(assignment\n"), 2u);
  EXPECT_EQ(count(s, "op_add"), 1u);
  EXPECT_EQ(count(s, "op_log_or"), 1u);
  // The inner `*/` closes only the inner `/*`: `+ 5` stays commented out.
  EXPECT_EQ(count(sexp("a = 1 /* x /* y */\n  + 5 */ + 2\n"), "op_add"), 1u);
  EXPECT_TRUE(parses("s = \"v={ /* a /* b */ */ x }\"\n"));
  EXPECT_FALSE(parses("a = 1 /* x /* y */\nb = 2\n"));  // unterminated (outer open)
  EXPECT_FALSE(parses("a = 1\n/* x\n"));
  EXPECT_FALSE(parses("a = 1 /* x */ */\n"));
}

// Mirrors test/corpus/continuation_lines.txt: a line starting with `/`
// continues (also after a comment line or a nested block comment), and only
// the WHOLE words `else`/`elif` continue an `if` chain -- `els`, `eli`,
// `elsev`, `elifx`, `elsz=3`, `elsewhere` are names that start a statement.
TEST(Parser, ContinuationLines) {
  EXPECT_EQ(count(sexp("r = a\n  / b\nx = 1\n"), "op_div"), 1u);
  EXPECT_EQ(count(sexp("r = a\n  // why\n  / b\nx = 1\n"), "op_div"), 1u);
  EXPECT_EQ(count(sexp("r = a\n  /* q /* n */ */ / b\nx = 1\n"), "op_div"), 1u);
  auto s = sexp("mut els = a\nels = 3\nelsev = 3\neli = 3\nelifx = 3\nelsz=3\nelsewhere = 4\n");
  EXPECT_EQ(count(s, "(assignment\n"), 7u);
  EXPECT_TRUE(parses("if a {\n  b = 1\n}\nelif c {\n  b = 2\n}\nelse {\n  b = 3\n}\n"));
  EXPECT_EQ(count(sexp("r = a\n  equals b\nequalsx = 1\n"), "(assignment\n"), 2u);
}

// Mirrors the `:` / `#` cases of test/corpus/continuation_lines.txt: a line
// starting with a type annotation (`:u8`), an attribute (`::[..]`, `:[..]`) or
// a bit selector (`#[..]`, `#|[..]`, `#sext[..]`, ...) continues the previous
// statement; no statement starts with `:` or `#`.
TEST(Parser, ContinuationColonAndHash) {
  auto s = sexp("mut acc\n:U8 = a\nb = acc\n");
  EXPECT_EQ(count(s, "(assignment\n"), 2u);
  EXPECT_EQ(count(s, "(type_cast"), 1u);
  EXPECT_EQ(count(sexp("mut acc\n  :U8 = a\n"), "(uint_type)"), 1u);
  s = sexp("res = (b)\n#[0..=3]\nx = 1\n");
  EXPECT_EQ(count(s, "(assignment\n"), 2u);
  EXPECT_EQ(count(s, "(bit_selection"), 1u);
  s = sexp("r1 = b\n  #|[..]\nr2 = b\n  #^[..]\nr3 = b\n  #&[..]\nr4 = b\n  #+[..]\nr5 = b\n  #sext[0..=3]\n"
           "r6 = b\n  #zext[0..=3]\n");
  EXPECT_EQ(count(s, "(assignment\n"), 6u);
  EXPECT_EQ(count(s, "(bit_selection"), 6u);
  EXPECT_EQ(count(sexp("x = f\n  ::[name=u](a)\n"), "(attribute_set"), 1u);
  s = sexp("const x\n  ::[`comptime`] = 3\nconst y:U8\n  :[`comptime`] = 3\n");
  EXPECT_EQ(count(s, "(assignment\n"), 2u);
  EXPECT_EQ(count(s, "(attribute_sq"), 2u);
  EXPECT_TRUE(parses("r = match x {\n  == 1 { 3 }\n  else { 4 }\n}\nfor ::[unroll] i in 0..<3 {\n  a = i\n}\n"));
  EXPECT_FALSE(parses("a = 1\n:b = 2\n"));
  EXPECT_FALSE(parses("a = 1\n#[0] = 2\n"));
}

// Mirrors test/corpus/identifier_dollar.txt: `$` is not an identifier
// character, so a name holding one must be written in backticks.
TEST(Parser, DollarNeedsBackticks) {
  EXPECT_TRUE(parses("`foo$bar` = 1\ny = `a$` + `$x`\n"));
  for (const char* src : {"foo$bar = 1\n", "x = a$\n", "$x = 1\n", "x = _a$b\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
}

// Mirrors test/corpus/format_spec_comments.txt: a comment after a hole's
// format spec is a comment, not spec text, and never hides the closing `}`.
// prpparse builds no spec node: the hole's expression is just `n`.
TEST(Parser, FormatSpecComments) {
  EXPECT_EQ(count(sexp("s = \"{n:b /* c */}\"\n"), "(identifier)"), 2u);
  auto s = sexp("s = \"{n:b // c\n}\"\nx = 1\n");
  EXPECT_EQ(count(s, "(assignment\n"), 2u);
  EXPECT_EQ(count(s, "(identifier)"), 3u);
  EXPECT_EQ(count(sexp("s = \"{n /* c */ :b}\"\n"), "(identifier)"), 2u);
  EXPECT_EQ(count(sexp("s = \"{n:b}\"\nt = \"{n:08x} and {m}\"\n"), "(identifier)"), 5u);
}

// Mirrors the non-ASCII case of test/corpus/continuation_lines.txt: a word
// operator followed by a non-ASCII letter is a name (`andé`), so the line
// starts a new statement (src/scanner.c is_ident_char).
TEST(Parser, WordOperatorWithNonAsciiTailIsAName) {
  EXPECT_EQ(count(sexp("a = b\nandé = 3\noré = 4\niné = 5\nelseé = 6\n"), "(assignment\n"), 5u);
}

// Mirrors the `case` cases of test/corpus/continuation_lines.txt: a line
// starting with the WHOLE word `case` continues the previous expression (as
// every binary word operator does); `cases`/`case_x`/`caseé` are names, and
// `case` match arms on their own lines still parse.
TEST(Parser, ContinuationCase) {
  auto s = sexp("const m = x\n  case y\nconst z = 1\n");
  EXPECT_EQ(count(s, "(assignment\n"), 2u);
  EXPECT_EQ(count(s, "op_case"), 1u);
  s = sexp("const m = x\n  // why\n  case y\nconst z = 1\n");
  EXPECT_EQ(count(s, "(assignment\n"), 2u);
  EXPECT_EQ(count(s, "op_case"), 1u);
  EXPECT_EQ(count(sexp("a = b\ncases = 3\ncase_x = 4\ncaseé = 5\n"), "(assignment\n"), 4u);
  EXPECT_TRUE(parses("match (const a=1, const b=3) {\n  case (a=1) { cassert(true) }\n"
                     "  case (b=3) { cassert(true) }\n  else { cassert(false) }\n}\n"));
}

// Mirrors test/corpus/type_words.txt: the capitalized type words `U<N>`,
// `S<N>`, `Unsigned`, `Signed`, `Bool`, `String`, `Clock`, `Reset` are RESERVED.
// The bare word is always the type: a type in type positions, and an operand /
// conversion callee (an `identifier` node) where a value is expected -- never a
// name being bound or a field name. It heads a suffix chain only through an
// attribute read (`U8.[max]`); a conversion call's result heads any chain
// (`U8(x)#[0]`). The backticked `` `U4` `` is an ordinary name everywhere, and the
// old lowercase spellings (`u8`, `bool`, `unsigned`, ...) are ordinary names.
TEST(Parser, TypeWordsInTypePositions) {
  auto s = sexp("mut a:U8 = 0\nmut b:S4 = 0\nmut c:Unsigned(bits=8, max=200) = 0\nmut d:Signed(bits=8) = 0\n"
                "mut e:Bool = false\nmut f:String = \"x\"\nmut w:[4]U2 = 0\n"
                "mod m(clk:Clock, rst:Reset, x:U1333) -> (o:U8@[1]) { o = x }\n"
                "const q = f<U8, T=Signed>(1)\ntype Word = U32\n");
  EXPECT_EQ(count(s, "(uint_type"), 7u);
  EXPECT_EQ(count(s, "(sint_type"), 3u);
  EXPECT_EQ(count(s, "(bool_type)"), 1u);
  EXPECT_EQ(count(s, "(string_type)"), 1u);
  EXPECT_EQ(count(s, "(clock_type)"), 1u);
  EXPECT_EQ(count(s, "(reset_type)"), 1u);
  EXPECT_EQ(count(s, "constraint: (tuple"), 2u);
}

TEST(Parser, TypeWordsAsValues) {
  auto s = sexp("cassert(x does U8)\nconst q = U8(3) + S4(y)\nconst b = Bool(x) and U1(flag) == 1\n"
                "const s = String(n) ++ \" bits\"\nconst u = Unsigned(x) + Signed(y)\n"
                "const t = (a=U8, b=S20)\nconst r = [Bool, Clock, Reset]\n");
  EXPECT_EQ(count(s, "(function_call_expression"), 8u);
  for (const char* k : {"(uint_type", "(sint_type", "(bool_type", "(string_type", "(clock_type", "(reset_type"})
    EXPECT_EQ(count(s, k), 0u) << k;
  // suffix chains
  s = sexp("cassert(U8.[max] == 255 and S4.[min] == -8)\nconst b = U8(x)#[0]\nconst f = U8(x).foo\n"
           "const m = Unsigned(bits=4).[max]\nconst k = U8.[max]#[0..<4]\nconst n = g<N=U8.[bits]>(x)\n");
  EXPECT_EQ(count(s, "(attribute_read"), 5u);
  EXPECT_EQ(count(s, "(bit_selection"), 2u);
  EXPECT_EQ(count(s, "(dot_expression"), 1u);
  EXPECT_NE(flat_sexp("const n = g<N=U8.[bits]>(x)\n")
                .find("rvalue: (attribute_read argument: (identifier) attrs: (attribute_list name: (identifier)))"),
            std::string::npos);
}

TEST(Parser, TypeWordsBacktickedAndLowercaseAreNames) {
  EXPECT_TRUE(parses("const `U4` = 3\nmut y:U4 = `U4`\nconst s = x.`Bool`\nconst t = (`S8`=1)\n"
                     "const c = f(`Clock`=2)\ncomb `Reset`(`U1`) -> (`String`) { `String` = `U1` }\n"));
  EXPECT_EQ(count(sexp("mut z:`U4` = 0\n"), "(uint_type"), 0u);  // a backticked word is a name, not the type
  // a backticked old spelling is the same ordinary name
  auto s = sexp("const `u8` = 1\nmut `s1` = 2\nconst `i0` = 3\nmut `bool` = true\nconst `unsigned` = 4\n"
                "const `string` = \"s\"\nconst int = 5\nconst `I8` = 6\nmut x:`u8` = 3\n");
  EXPECT_EQ(count(s, "(expression_type"), 1u);  // `x:`u8``: a user type named `u8`
  EXPECT_EQ(count(s, "(uint_type"), 0u);
  EXPECT_TRUE(parses("const U = 1\nconst Sx = 2\nconst U8x = 3\nconst S_0 = 4\nconst Clocks = 5\n"
                     "const Booleans = 6\nconst U4é = 7\nconst Stringy = 8\n"));
  EXPECT_TRUE(parses("const q = (const U8)\n"));  // a positional value, like `(const 3)`
}

TEST(Parser, TypeWordsAreReserved) {
  for (const char* src :
       {"const U4 = 3\n", "reg S1:U32 = 0\n", "const Bool = 1\n", "mut Clock = 0\n", "U8 = 3\n",
        "String = \"x\"\n", "const q = x.U4\n", "const q = x.Reset\n", "const q = (U8=1)\n",
        "const q = (mut Bool:U2 = 0)\n", "const q = (const U8 = 1)\n", "const q = f(Clock=1)\n",
        "comb f(U4) -> (o) { o = 1 }\n", "comb f(a) -> (Signed) { Signed = a }\n",
        "comb Unsigned(a) -> (o) { o = a }\n", "for S1 in 0..<3 { }\n", "import a.b as U4\n",
        "mut (U4, b) = f()\n", "(a=U4) = f()\n", "const q = U8.x\n", "const q = U8[0]\n", "const q = U8#[0]\n",
        "const q = U8@[1]\n", "x = ref U8\n", "const q = x::[U4=1]\n", "const q = x.[Bool]\n", "test U8.x { }\n",
        "enum Clock = (a, b)\n", "type Bool = U1\n", "(U4, b) = f()\n", "wrap U8:U4 = 3\n", "U8.x = 1\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("const Bool = 1\n").code, "reserved-type-name");
}

// Mirrors test/corpus/enum_syntax.txt: an enum declaration requires `=`.
TEST(Parser, EnumRequiresEquals) {
  EXPECT_TRUE(parses("enum State = (Idle, Run, Done)\nenum Op:U8 = (add = 1, sub = 2)\n"));
  EXPECT_FALSE(parses("enum E:U8 (a, b)\n"));
  EXPECT_FALSE(parses("enum E (a, b)\n"));
  // The expression form `enum(...)` was removed (spec 2026-09-29 §7).
  for (const char* src : {"const Color = enum(red, green)\n", "type V = enum(a, b)\n", "const x = f(enum(a, b))\n",
                          "mut c:enum(a, b) = 0\n", "const Op = enum(and, or, not)\n", "x = enum(a:U8)\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("const Color = enum(red, green)\n").code, "enum-expression");
}

// Mirrors test/corpus/generic_dotted_callee.txt: explicit generics on a dotted
// callee; a plain comparison of a dotted name is unaffected.
TEST(Parser, GenericCallOnDottedCallee) {
  auto s = flat_sexp("const q = prp.queue.make<T=Signed>(depth=16)\n");
  EXPECT_NE(s.find("(function_call_expression function: (dot_expression"), std::string::npos);
  EXPECT_NE(s.find("generic: (generic_type_list item: (arg_assignment lvalue: (identifier) rvalue: (sint_type)))"),
            std::string::npos);
  EXPECT_EQ(count(sexp("const r = a.b<U8>(x)\n"), "(generic_type_list"), 1u);
  EXPECT_EQ(count(sexp("const c = a.b < d\n"), "(op_lt)"), 1u);
}

// Mirrors test/corpus/assignment_targets.txt (spec 2026-09-29 §4/§7/§8): an
// assignment target is a name, or a field, a selector or a bit-select OF A NAME;
// a destructuring `(a, b) = f()` of NAMES or `name = path` renames is legal as a
// STATEMENT only (never in an expression or an init clause), with `=` only and
// untyped slots.
TEST(Parser, AssignmentTargets) {
  EXPECT_TRUE(parses("a = 1\na.b = 1\na[0] = 1\na#[0] = 1\na.b[1]#[2] = 3\na:U8 = 3\nself.x = 1\na.[x] = 1\n"
                     "a@[1] = 2\na.b::[c] = 1\nwrap a.b#[0] = 1\nmut a.b = 1\n"));
  auto s = sexp("(a, b) = f()\nmut (c, d) = f()\n(e, x = y.z) = f()\n(g = h, k) = f()\n"
                "(v=deep.payload.inner.value) = deep(a=3)\nconst (a2=f.xx, b2=f.yy) = f()\n"
                "const (p1, p2) = two(a=1)\n");
  EXPECT_EQ(count(s, "(lvalue_list"), 7u);
  EXPECT_EQ(count(s, "(named_lvalue"), 5u);
  EXPECT_TRUE(parses("const t = (a = 1, b.c = 2)\nconst u = f(a = 1, b.c = 2)\n"));
  for (const char* src :
       {"f(x) = 3\n", "a + b = 3\n", "(1) = 2\n", "(a, f(x)) = g()\n", "((a, b), c) = g()\n", "[a, b] = f()\n",
        "const q = (f(x)=1)\n", "const q = ((a, b) = g())\n", "f(a[0] = 3)\n", "if a = 1 { }\n", "mut a.b\n",
        "-a = 3\n", "mut f(x) = 3\n", "x = (mut (a,b) = g())\n", "mut (a, f(x)) = g()\n", "f(x) += 1\n",
        // call/expression-rooted targets
        "f(x).a = 3\n", "(a+b).c = 3\n", "f(x)[0] = 3\n", "f(x)#[0] = 3\n", "(x)#[0] = 1\n", "(a).b = 1\n",
        "[a, b][0] = 1\n", "mut true.x = 1\n", "U8.[max] = 1\n", "\"s\".x = 1\n", "f(x).a += 1\n",
        // complex / typed destructuring slots
        "(a.b, c[1]) = f()\n", "(a.b, c) = f()\n", "(a, c[1]) = f()\n", "(a#[0], b) = f()\n",
        "(self.c, self.d) = f(y=a)\n", "const (a.b, c) = f()\n", "const (a:U32, b) = f()\n",
        "const (lo:U4, hi:U4) = f()\n", "(a:U8, b) = f()\n", "(g = h:U8, k) = f()\n", "const (x = y:U8) = f()\n",
        "(x = f(a).b) = g()\n", "(x = a[0]) = g()\n", "(a, b):U8 = f()\n",
        // destructuring in an expression or an init clause, or with a compound op
        "if (a, b) = f(); a { }\n", "while (a, b) = f(); a { }\n", "if const (a, b) = f(); a { }\n",
        "(a, b) += f()\n", "const (a, b) += f()\n", "(a, b) ++= f()\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("f(x) = 3\n").code, "bad-assignment-target");
  EXPECT_EQ(diag_of("f(x).a = 3\n").code, "bad-assignment-target");
  EXPECT_EQ(diag_of("(a.b, c[1]) = f()\n").code, "bad-destructuring-target");
  EXPECT_EQ(diag_of("const (a:U32, b) = f()\n").code, "typed-destructuring");
  EXPECT_EQ(diag_of("(a, b) += f()\n").code, "bad-destructuring-operator");
  // a declaration list without `=` declares names (unchanged)
  EXPECT_TRUE(parses("mut (a, b)\nmut (c:U8, d)\n"));
}

// Resolved parser disagreements (spec 2026-09-29 §8).
TEST(Parser, ResolvedDisagreements) {
  for (const char* src :
       {// `::` is one token: split by a blank, a comment or a newline it is two `:`
        "const tmp: :[debug] = 1\n", "const tmp:/**/:[debug] = 1\n", "const tmp:\n  :[debug] = 1\n",
        "reg r: :[retime=true] = 0\n",
        // a bare `_` is no name anywhere
        "const _ = 8\n", "mut _ = 8\n", "_ = f(a=1)\n", "for _ in 0..<3 { }\n", "(_, b) = f()\n",
        "const (_, b) = f()\n", "comb f(_) -> (r) { r = 1 }\n", "const x = _ + 1\n", "const t = (_ = 1)\n",
        "const y = a._\n",
        // `wrap`/`sat` need an initializer
        "wrap const y:U8\n", "wrap mut y:U8\n", "wrap wire y:U8\n", "sat mut y\n", "wrap const (a, b)\n",
        // a named lambda is no binary / bit-select operand inside a tuple
        "const T = (comb f(self) { } + 1)\n", "const T = (comb f(self) { }#[0])\n",
        "const T = (comb f(self) { }\n  + 1)\n", "const T = (a, comb f(self) { } * 2)\n",
        // a line never starts with `@`
        "if x1\n@[1] == 1 { }\n", "while x1\n@[1] == 1 { }\n", "const a = x1\n@[1] == 1\n", "const a = (x1\n@[1])\n",
        // a field of a tuple TYPE in type position
        "reg flags:(a:Bool).flags = false\n", "const x:(a:U8).b = 1\n", "reg f:(a:Bool)\n  .flags.active = 0\n",
        // a bare typed statement declares nothing
        "value:U8\n", "{ value:U8 }\n", "{\n value\n:U8 }\n", "a.b:U8\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("const _ = 8\n").code, "bare-underscore");
  // `_:T` is the anonymous entry of a tuple TYPE: legal only as `_` directly after
  // `(` or `,` and before a single `:`; every other lone `_` stays an error.
  for (const char* src : {"comb f(v:(_:U4, _:U8)) -> (o:U12) { o = 1 }\n", "type P = (_:U4,\n  _:U8)\n",
                          "mut x:(_ :U4, _:U8) = (3, 54)\n"}) {
    EXPECT_TRUE(parses(src)) << src;
  }
  for (const char* src : {"const x = f(_)\n", "(_, b) = f()\n", "const y:(a:U4, _) = 1\n", "const z = (a, _::U4)\n", "x._:U4\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  for (const char* src : {"const tmp::[debug] = 1\n", "const `_` = 8\n`_` = 3\nconst y = a.`_` + `_`\n",
                          "wrap const y:U8 = 3\nsat mut z = 1\n", "const T = (comb f(self) { }, 1)\n",
                          "stage[1] out@[4]\n", "comb f() { stage[1] out@[4]\n  mut z@[1]:U8\n  mut w@[2]:U8 = 3 }\n",
                          "mut y:U8\nconst x:(a:U8) = (a=1)\nconst b = x1@[1] == 1\n"}) {
    EXPECT_TRUE(parses(src)) << src;
  }
  EXPECT_NE(flat_sexp("stage[1] out@[4]\n")
                .find("(declaration_statement decl: (var_or_let_or_reg storage: (stage_decl timing: (timing_slot"),
            std::string::npos);
  EXPECT_NE(flat_sexp("stage[1] out@[4]\n").find("lvalue: (typed_identifier identifier: (identifier) timing: (timing_slot"),
            std::string::npos);
}

// Mirrors test/corpus/string_braces.txt (spec 2026-09-29 §3/§7/§8): in a
// double-quoted string `{{` and `}}` are ESCAPED LITERAL BRACES (`"{{x}}"` is
// the text `{x}`, no hole); `\{`/`\}` are no escapes; an empty hole `{}` and a
// lone `}` are errors; a format spec needs an expression (`"{:b}"`).
TEST(Parser, StringDoubledBraces) {
  EXPECT_EQ(count(sexp("const a = \"{{x}}\"\n"), "(identifier)"), 1u);  // just `a`
  EXPECT_TRUE(parses("const b = \"a{{\"\nconst c = \"}}\"\nconst d = \"I have {{num}}\"\n"));
  EXPECT_EQ(count(sexp("const a = \"{{{x}}}\"\n"), "(identifier)"), 2u);
  EXPECT_EQ(count(sexp("const b = \"{{}} {y:b} {{\"\n"), "(identifier)"), 2u);
  for (const char* src : {"const a = \"{:b}\"\n", "const a = \"{ :b}\"\n", "const d = \"\\{x}\"\n",
                          "const d = \"I have \\{num\\}\"\n", "const d = \"a \\} b\"\n", "const e = \"{}\"\n",
                          "const e = \"{ }\"\n", "const e = \"{ /* x */ }\"\n", "const e = \"a={} b={}\"\n",
                          "const f = \"a } b\"\n", "const f = \"}}}\"\n", "const f = \"{x}}\"\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("const e = \"{}\"\n").code, "empty-interpolation");
  EXPECT_EQ(diag_of("const f = \"a } b\"\n").code, "lone-brace");
}

// String escapes (spec 2026-09-29 §8): exactly \n \t \r \\ \" \' \0 \xNN
// \u{1..6 hex} and \`, also inside backticked names. `\u{...}` is one escape,
// never an interpolation hole. Comment openers inside strings are text;
// comments inside holes are comments.
TEST(Parser, StringEscapes) {
  auto s = sexp(R"(const a = "\n\t\r\\\"\'\0\x4F\`")"
                "\n"
                R"(const b = "x\u{41}y\u{1F600}\u{10FFFF}{c}")"
                "\n"
                R"(const `a\`b\n` = 1)"
                "\n");
  EXPECT_EQ(count(s, "(identifier)"), 4u);  // a, b, c and the backticked name: no hole from `\u{41}`
  for (const char* src : {R"(const a = "\q")", R"(const a = "\u0041")", R"(const a = "\u{}")",
                          R"(const a = "\u{1234567}")", R"(const a = "\x4")", R"(const a = "\{")",
                          R"(const `a\qb` = 1)", R"(const `a\{` = 1)", R"(const a = "{x:\x4}")",
                          R"(const a = "{x:\n}")", R"(const a = "{x:b\t}")"}) {
    EXPECT_FALSE(parses(std::string(src) + "\n")) << src;
  }
  EXPECT_EQ(diag_of(R"(const a = "\q")"
                    "\n")
                .code,
            "bad-escape");
  EXPECT_TRUE(parses("const a = \"/* x */ {b /* } */} // y\"\n"));
}

// The old lowercase type spellings are ORDINARY identifiers (owner ruling
// 2026-09-30): every position that takes a name takes them, with or without
// backticks, and the type / cast-callee positions parse too (lhd, not the
// parser, reports "`u8` was renamed `U8`" for an undeclared one).
TEST(Parser, OldTypeSpellingsAreNames) {
  for (const char* src :
       {"const u8 = 3\n", "const s1 = 3\n", "const s2 = 3\n", "mut i0 = 3\n", "const x = u4 + 1\n",
        "const bool = 1\n", "const boolean = 1\n", "const unsigned = 1\n", "const signed = 1\n",
        "const string = \"a\"\n", "const i32 = 1\n",
        // type position and cast callee
        "const x:u8 = 3\n", "const x:i32 = 3\n", "const x:boolean = 3\n", "const x = u8(y)\n", "const x = string(3)\n",
        "const t = (a:bool)\n", "const x = y does bool\n",
        // field, method, lambda name, parameter, port, parameter default
        "y.bool = 1\n", "const x = a.i32\n", "const x = rnd.boolean()\n", "comb u8(a) -> (r) { r = a }\n",
        "comb f(s20:U8) -> (r) { r = 1 }\n", "comb f(s2:U8, u8:U8) -> (i32:U8) { i32 = s2 + u8 }\n",
        "mod m(clk:Clock, s2:U1) -> (o:U1) { o = s2 }\n", "comb f<u8>(a) -> (r) { r = a }\n",
        // tuple fields, arguments, enums, registers, loops, attributes, types, imports, tests
        "const t = (u8 = 1, b = 2)\n", "const t = (mut u8 = 1)\n", "f(i32 = 1)\n", "const x = f(a=bool)\n",
        "enum bool = (a, b)\n", "enum E = (string, b)\n", "reg s1:U32 = 0\n", "for u1 in 0..<2 { }\n",
        "const y = x.[unsigned]\n", "const y = x::[signed]\n", "reg r::[u8=1] = 0\n", "type string = U8\n",
        "import a.u8 as b\n", "import foo as s1\n", "const (u8, b) = f()\n", "test u8 { }\n",
        "const x = \"{u8}\"\n"}) {
    EXPECT_TRUE(parses(src)) << src;
  }
  // backticks are unnecessary but legal: the same ordinary name
  EXPECT_TRUE(parses("const `u8` = 3\nconst y = x.`bool` + `s1`\nconst z = \"u8 bool string\" // u8\n"
                     "const u = 1\nconst u_8 = 2\nconst s1x = 3\nconst st1 = 4\nconst in0 = 5\nconst bools = 6\n"));
  EXPECT_TRUE(parses("const u8 = 1\nconst y = `u8` + u8\n"));
  // no `renamed-type-word` diagnostic exists in the parser any more; the new
  // spellings stay reserved (U8 / S2 as names are errors, backticked they pass)
  for (const char* src : {"const U8 = 3\n", "const S2 = 3\n", "mut Bool = 1\n", "comb f(S2:U8) -> (r) { r = 1 }\n",
                          "const t = (U8 = 1)\n", "y.S2 = 1\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("const S2 = 3\n").code, "reserved-type-name");
  EXPECT_TRUE(parses("const `S2` = 3\nconst `U8` = 1\ny.`S2` = 1\n"));
}

// Reserved type words (spec 2026-09-29 §7): `U`/`S` followed by ANY digits and
// Unsigned/Signed/Bool/String/Clock/Reset are reserved everywhere, also after
// `.`; backticks make them ordinary names.
TEST(Parser, ReservedTypeWordsEverywhere) {
  for (const char* src : {"const U0 = 1\n", "const U1333 = 1\n", "mut U99999999 = 1\n", "foo.U33 = 333\n",
                          "const y = foo.S1\n", "const y = foo.Clock\n", "const t = (Reset = 1)\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_TRUE(parses("`U3` = 1111\nfoo.`U33` = 333\nconst y = foo.`Clock` + `S0`\n"));
}

// Identifier characters (spec 2026-09-29 §2/§8): letters (non-ASCII letters
// too), digits and `_`; any other character in a name is an error. Only ASCII
// blanks separate tokens (owner ruling 108): NBSP or a BOM is an error.
TEST(Parser, IdentifierCharacters) {
  EXPECT_TRUE(parses("const caf\u00e9 = 1\nconst \u03b1\u03b2 = 2\nconst \u53d8\u91cf = 3\n"
                     "const x = caf\u00e9 + \u03b1\u03b2\n"));
  EXPECT_FALSE(parses("const a\u00a0= 1\n"));
  EXPECT_FALSE(parses("\ufeffconst b = 2\n"));
  EXPECT_EQ(diag_of("const a\u3000= 1\n").code, "non-ascii-space");
  for (const char* src : {"const a\U0001F600b = 1\n", "const a\u00d7b = 1\n", "const a\u2014b = 1\n",
                          "const a\u00a0b = 1\n", "const a\u200bb = 1\n", "const a\ufeffb = 1\n",
                          "const a\u00b7b = 1\n", "const a\u20acb = 1\n", "const a\u2192b = 1\n",
                          "const a\uff0bb = 1\n", "const a\u00b2b = 1\n", "const a\u2160b = 1\n",
                          "const e\u0301 = 1\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_TRUE(parses("const `a\U0001F600b` = 1\n"));
}

// Tree-sitter may accept a superset; the compiler parser enforces one name rule.
TEST(Parser, ReservedWordsMatchTheGrammar) {
  for (const char* src :
       {"mut if = 1\n", "const in:U8 = 1\n", "reg else\n", "mut (a, for)\n", "const (if, b) = (1, 2)\n",
        "const (mut, b) = (1, 2)\n", "(if = x, b) = f()\n", "const (if = y) = f()\n",
        "(for = x) = f()\n", "const (for = x) = f()\n", "(a = x.U8) = f()\n", "mod f(in:U8) -> (o:U8) { o = 1 }\n",
        "comb f(a:U8) -> (else:U8) { }\n", "comb f(a:U8) -> in:U8 { }\n", "comb f<if>(a:U8) -> (o:U8) { o = a }\n",
        "for import in 0..<3 { }\n", "for (i, match) in x { }\n", "while mut unique = 0; x { }\n",
        "if const else = 1; x { }\n", "test t(in:U8 = 1) { }\n",
        // values and targets
        "if = 3\n", "const y = 1 + unique\n", "mut dut = pub\n", "const y = fluid.x\n", "true = 3\n",
        "(true, b) = (1, 2)\n", "(const, b) = (1, 2)\n", "(not, b) = (1, 2)\n", "(in:U8, b) = f()\n",
        "x = ref true\n", "x = ref f(x)\n", "mut x:pub = 1\n", "pub x = 1\n",
        "while mut unique=0 ; unique!=3 { unique += 1 }\n", "const y = f(if:U8)\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("mut if = 1\n").code, "reserved-word-as-name");
  EXPECT_EQ(diag_of("const (if, b) = (1, 2)\n").code, "reserved-word-as-name");
  EXPECT_EQ(diag_of("while mut unique = 0; x { }\n").code, "reserved-word-as-name");
  for (const char* src :
       {"mut `if` = 1\nmod f(`in`:U8) -> (`else`:U8) { `else` = `in` }\nfor `import` in 0..<3 { }\n"
        "const (`mut`, b) = (1, 2)\nwhile mut `unique` = 0; `unique` != 3 { `unique` += 1 }\n",
        // fields
        "const t = (`if` = 1, `unique` = 2, `match` = 3, `true` = 4, `pub` = 5)\n",
        "const y = f(`pub` = 1, `fluid`=2, `in`=3)\nconst lhs2 = __sum(`as`=(v1, v2))\n",
        "mut m = (const `if` = 1, const `type` = 1, mut `for`:U8 = 2, const `in`:U8)\n",
        "const t = (a:U8, `in`:U8, `if`:U8 = 0)\n", "reg r::[`comptime`=1] = 0\nconst y = f<`if`=U8>(1)\n",
        // a rename slot's SOURCE path is field names (`local = source.path`)
        "const (a = `if`) = f()\n(a = `for`) = f()\n(a = x.`if`) = f()\nconst (a = `true`.`if`) = f()\n",
        "(`if` = x) = f()\n", "const y = x.`if`.`in`.[`comptime`]\n",
        "x and= y\nx or= y\nconst t = (`and`=1)\nconst u = (const `and`=3 + 1)\nconst v = f(`or`=1)\n",
        "const t = (true == x)\n",
        // a plain name where no construct can start
        "`else` = 3\nwrap `if`.total = r + a\nx = ref `match`.y\nmut y = `for`\n",
        "comptime c = 1\npub comptime d:U8 = 2\ncomptime mut e = 0\n"}) {
    EXPECT_TRUE(parses(src)) << src;
  }
  // `(`and` = 1)` and `(`and`=1)` are the same program (spaces have no meaning).
  EXPECT_EQ(sexp("const t = (`and` = 1)\n"), sexp("const t = (`and`=1)\n"));
  EXPECT_EQ(count(sexp("x and= y\n"), "assign_log_and"), 1u);
  EXPECT_NE(flat_sexp("comptime c = 1\n").find("(var_or_let_or_reg comptime: (comptime_modifier))"),
            std::string::npos);
}

// `comptime` modifies `const`/`mut` in either order (owner ruling 2026-10-01):
// `const comptime x` is the same tree as `comptime const x` (prpfmt prints the
// `comptime`-first spelling). It never applies to `reg`/`wire`/`stage`/`fluid`.
TEST(Parser, ComptimeModifierOrder) {
  EXPECT_EQ(sexp("const comptime N = 4\n"), sexp("comptime const N = 4\n"));
  EXPECT_EQ(sexp("mut comptime n = 0\n"), sexp("comptime mut n = 0\n"));
  EXPECT_EQ(sexp("pub const comptime N:U8 = 4\n"), sexp("pub comptime const N:U8 = 4\n"));
  EXPECT_EQ(sexp("const comptime t = (a = 1, b = 2)\n"), sexp("comptime const t = (a = 1, b = 2)\n"));
  // inside a lambda body too
  EXPECT_EQ(sexp("comb f() -> (o) {\n  const comptime k = 2\n  o = k\n}\n"),
            sexp("comb f() -> (o) {\n  comptime const k = 2\n  o = k\n}\n"));
  // the keyword is still reserved as a NAME
  EXPECT_EQ(diag_of("const comptime = 1\n").code, "reserved-word-as-name");
  EXPECT_EQ(diag_of("mut comptime:U8 = 1\n").code, "reserved-word-as-name");
  EXPECT_EQ(diag_of("const comptime comptime x = 1\n").code, "comptime-twice");
  EXPECT_EQ(diag_of("comptime const comptime x = 1\n").code, "comptime-twice");
  for (const char* src : {"comptime reg r = 0\n", "reg comptime r = 0\n", "comptime wire w = 0\n",
                          "wire comptime w = 0\n", "comptime stage s = 0\n", "comptime fluid const f = 0\n"}) {
    const Diag d = diag_of(src);
    EXPECT_EQ(d.code, "comptime-storage") << src;
    EXPECT_NE(d.message.find("`const` or `mut`"), std::string::npos) << src << d.message;
  }
}

// Mirrors the `%` / `@[` cases of test/corpus/continuation_lines.txt.
TEST(Parser, ContinuationPercentNotTiming) {
  auto s = sexp("const a = 7\n  % 2\nconst b = c\n  // why\n  % d\n");
  EXPECT_EQ(count(s, "(assignment\n"), 2u);
  EXPECT_EQ(count(s, "op_mod"), 2u);
  EXPECT_FALSE(parses("const a = b\n  @[1]\n"));
  EXPECT_TRUE(parses("mut a = b@[1]\n"));
}

// Mirrors the string cases of test/corpus/format_spec_comments.txt: a hole is
// ONE expression optionally followed by a NON-EMPTY format spec; a raw newline
// in string text is an error (inside a hole it is whitespace).
TEST(Parser, StringHolesAndFormatSpecs) {
  for (const char* src :
       {"const s = \"a\nb\"\n", "const x = 1\nconst s = \"{x y z}\"\n", "const x = 1\nconst s = \"{x)}\"\n",
        "const s = \"{1{x}}\"\n", "const s = \"{x;y}\"\n", "const s = \"{x]}\"\n",
        "const x = 1\nconst s = \"{x:}\"\n", "const x = 1\nconst s = \"{x: }\"\n",
        "const x = 1\nconst s = \"{x:/* c */b}\"\n", "const x = 1\nconst s = \"{x:{}}\"\n",
        "const s = \"{x:a/}\"\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("const s = \"{x y}\"\n").code, "bad-interpolation");
  EXPECT_EQ(diag_of("const s = \"{x:}\"\n").code, "bad-format-spec");
  for (const char* src : {"const s = \"a{x\n}b\"\n", "const s = \"{x: 08b} {y :x} {z:a/b}\"\n",
                          "const s = \"{x::[a=1]}\"\n"}) {
    EXPECT_TRUE(parses(src)) << src;
  }
}

// Mirrors the enum cases of test/corpus/enum_syntax.txt: an enum member is a
// field (`E.a`), so a bare type word can not be one.
TEST(Parser, EnumMemberIsNotATypeWord) {
  EXPECT_FALSE(parses("enum E = (a, U8)\n"));
  EXPECT_FALSE(parses("enum E:U8 = (a, Bool)\n"));
  EXPECT_FALSE(parses("enum E = (U8)\n"));
  EXPECT_EQ(diag_of("enum E = (a, U8)\n").code, "reserved-type-name");
  EXPECT_TRUE(parses("enum E = (a, `U8`, b:U8, c = U8(3))\n"));
}

// A line starting with `(` is a new statement (02-basics), also after a type
// annotation: `mut r:Foo` newline `(a)` is a declaration plus a tuple
// statement, never the type call `Foo(a)` / the constraint `Unsigned(bits=3)`.
TEST(Parser, ParenOnNewLineEndsTypeAnnotation) {
  auto s = sexp("mut r:Foo\n(a)\n");
  EXPECT_EQ(count(s, "function_call_type"), 0u);
  EXPECT_EQ(count(s, "(declaration_statement"), 1u);
  EXPECT_EQ(count(s, "(tuple"), 1u);
  EXPECT_TRUE(parses("const r:U8\n(a, b)\n"));
  EXPECT_TRUE(parses("const r:U8\n(a, b)#[0]\n"));
  EXPECT_TRUE(parses("mut r:Foo\n(a) = 1\n"));
  EXPECT_EQ(count(sexp("const r:U8\n(a, b)\n"), "(tuple"), 1u);
  // The second line alone is no statement: `(bits=3) = 0` is an error.
  EXPECT_FALSE(parses("reg r:Unsigned\n(bits=3) = 0\n"));
  EXPECT_FALSE(parses("(b=3) = 0\n"));
  // On one line (or inside brackets) the call / constraint is still read.
  EXPECT_NE(sexp("reg r:Unsigned(bits=3) = 0\n").find("constraint"), std::string::npos);
  EXPECT_NE(sexp("mut r:Foo(a) = 1\n").find("function_call_type"), std::string::npos);
  EXPECT_TRUE(parses("comb f(a:Foo\n(b)) { }\n"));
  // The same in a condition: the `(` line is no call argument of the condition.
  EXPECT_FALSE(parses("if rose\n(x=dut.ack) { }\n"));
  EXPECT_FALSE(parses("if Bool\n(e#[3]) { }\n"));
  EXPECT_TRUE(parses("if rose(x=dut.ack) { }\n"));
}

// A selector holds an index or a range (grammar.js `select`); only an array
// LENGTH (`x:[]U8`), `stage[]` and a timing slot `x@[]` may be empty.
TEST(Parser, EmptySelectorIsAnError) {
  for (const char* src : {"const x = a[]\n", "a[] = 1\n", "const x = a#[]\n", "t#[] = 1\n", "const x = f[](c)\n",
                          "mut x[] = 20\n", "cassert(d2[][0] == 1)\n", "pipe[] f(a) -> (b) { b = a }\n",
                          "mut x = pipe[] f() { }\n", "const x = a[1][]\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("const x = a[]\n").code, "empty-select");
  for (const char* src : {"mut x:[]U8 = 0\n", "mut x:[][] = 1\n", "mut x:[3][] = 1\n", "stage[] x = 1\n",
                          "mut x@[] = 1\n", "const x = a@[]\n", "mut x = pipe[2] f() { }\n",
                          "const x = a[..]\n"}) {
    EXPECT_TRUE(parses(src)) << src;
  }
}

// A present list holds at least one item (grammar.js `listseq1`): commas alone
// (`(,)`, `[,]`, `f(,)`, `::[,]`) and an empty generic list `f<>(x)` are
// errors; `()`, `[]`, `f()`, `(,a)`, `(a,,b)` stay legal. A destructuring list
// names at least one variable (`() = f()`, `const () = f()`).
TEST(Parser, EmptyCommaListsAreErrors) {
  for (const char* src :
       {"const t = (,)\n", "const t = (,,)\n", "for (i, j) in (,) { }\n", "comb rotate(a) -> (,) { }\n",
        "comb rotate(,) { }\n", "const t = [,]\n", "const t = f(,)\n", "const n = mk<>(value=130)\n",
        "const n = mk<,>(value=130)\n", "const x = a::[,]\n", "const x:(,) = 1\n", "enum E = (,)\n",
        "const x = f(a=(,))\n", "(,) = f()\n", "const (,) = f()\n", "() = f()\n", "const () = f()\n",
        "mut x = fluid[,] f() { }\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("const t = (,)\n").code, "empty-list");
  EXPECT_EQ(diag_of("const () = f()\n").code, "empty-list");
  for (const char* src : {"const t = ()\n", "const t = []\n", "const t = f()\n", "const t = (,a)\n",
                          "const t = (a,,b)\n", "const t = (a,)\n", "comb rotate() -> () { }\n",
                          "const n = mk<T,>(value=130)\n", "const n = mk<,T>(value=130)\n", "const x = a::[]\n",
                          "mut x = fluid[] f() { }\n", "(a, b) = f()\n"}) {
    EXPECT_TRUE(parses(src)) << src;
  }
}

// An instance attribute `a::[x]` is only followed by the call it configures
// (`a::[x](1)`); a selector, bit-select, field, attribute read or another
// `::[..]` of it is an error (parenthesized it is an ordinary suffix head).
TEST(Parser, AttributeSetTakesOnlyACall) {
  for (const char* src : {"const b = a::[x][1]\n", "const b = a::[x]#[1]\n", "const b = a::[x].[bits]\n",
                          "const y = m::[x].data\n", "a ::[x][1] = 3\n", "a::[x].b = 3\n",
                          "const x = Signed(E::[x] .aa)\n", "const b = a::[x]::[y]\n", "const b = a::[x]\n.c\n",
                          "const b = a::[x]\n#[0]\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("const b = a::[x][1]\n").code, "attribute-set-suffix");
  for (const char* src : {"const b = a::[x](1)\n", "const b = a::[x](1).b\n", "const b = a::[x](1)[0]\n",
                          "const b = a::[x]\n", "a::[x] = 3\n", "const b = (a::[x]).c\n", "const b = a.b::[x]\n",
                          "const b = a[1]::[x]\n", "const b = a::[x] + 1\n", "const b = a::[x]\n[1]\n"}) {
    EXPECT_TRUE(parses(src)) << src;
  }
}

// A storage keyword, `comptime` or `ref` is never an array length:
// `x:[mut]U8`, `x:[reg]U8`, `x:[const]U8`, `x:[stage]` are errors (only as the
// first word after `[`); a backticked word is a name, and in a value selector
// (`a[mut]`) the word stays an identifier in both parsers.
TEST(Parser, KeywordIsNoArrayLength) {
  for (const char* src : {"reg buf1:[mut]U8 = nil\n", "mut x:[reg]U8 = 0\n", "reg b:[const]U8 = nil\n",
                          "reg array:[stage ] = 0\n", "mut x:[wire]U8 = 0\n", "mut x:[ref]U8 = 0\n",
                          "mut x:[comptime]U8 = 0\n", "mut x:[mut.a]U8 = 0\n", "mut x:[mut+1]U8 = 0\n",
                          "mut x:[mut..]U8 = 0\n", "mut x:[3][reg]U8 = 0\n", "comb f(a:[reg]U8) { }\n",
                          "type T = [mut]U8\n", "x:[mut]U8 = 0\n", "mut x = [mut]\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("mut x:[reg]U8 = 0\n").code, "reserved-word-as-name");
  for (const char* src : {"mut x:[`mut`]U8 = 0\n", "mut x:[1+`mut`]U8 = 0\n", "mut x:[..=`mut`]U8 = 0\n",
                          "mut x:[f(`mut`)]U8 = 0\n", "const x = a[`mut`]\n", "const x = a[`reg`]\n",
                          "mut x:[`wrap`]U8 = 0\n"}) {
    EXPECT_TRUE(parses(src)) << src;
  }
}

// Reservation is EXACT-SPELLING (case-sensitive) in every name position (owner
// ruling 2026-09-30): `if` is reserved, `IF` is a plain name; `Clock` is a type
// word, `clock` is a plain name. Exercise the shared name check, including
// diagnostics.
TEST(Parser, UniformReservedNames) {
  const std::vector<std::pair<std::string, std::string>> sites = {
      {"const ", " = 1\n"}, {"comb ", "(self) -> () { }\n"},
      {"comb f(", ":U8) -> () { }\n"}, {"comb f<", ">(x) -> () { }\n"},
      {"for ", " in 0..<2 { }\n"}, {"const x = (const ", " = 1)\n"},
      {"const x = t.", "\n"}, {"const x = f(", "=1)\n"},
      {"const x = f<", "=1>(a=1)\n"}, {"const x = y.[", "]\n"},
      {"enum E = (", ")\n"}, {"const x::[", "=1] = 1\n"},
  };
  // Exact spellings of keywords and type words are reserved.
  for (const std::string word : {"if", "tick", "comptime", "type", "test", "Bool", "Clock", "Reset", "U8", "S4", "Unsigned",
                                 "Signed", "String"}) {
    for (const auto& [prefix, suffix] : sites) {
      const std::string bad = prefix + word + suffix;
      EXPECT_FALSE(parses(bad)) << bad;
      const auto d = diag_of(bad);
      // `for comptime x in ..` reads `comptime` as a (unsupported) init clause,
      // which has its own message and no backtick hint.
      if (d.code != "for-init-unsupported")
        EXPECT_NE(d.hint.find("`" + word + "`"), std::string::npos) << bad << d.hint;
      EXPECT_TRUE(parses(prefix + "`" + word + "`" + suffix)) << bad;
    }
  }
  // Every other case variant is an ordinary name with no backticks, and the
  // backticked form is the same name.
  for (const std::string word : {"IF", "If", "TiCk", "CoMpTiMe", "TyPe", "Test", "bOoL", "BOOL", "CLOCK", "clock", "reset",
                                 "RESET", "uNsIgNeD", "I32", "NIL"}) {
    for (const auto& [prefix, suffix] : sites) {
      EXPECT_TRUE(parses(prefix + word + suffix)) << prefix + word + suffix;
      EXPECT_TRUE(parses(prefix + "`" + word + "`" + suffix)) << prefix + word + suffix;
    }
  }
  EXPECT_TRUE(parses("const iffy = 1\nconst format = 2\nconst type_data = nil\n"));
  EXPECT_TRUE(parses("const `foo$bar` = 1\nconst x = `foo$bar`\n"));
}

// `clock` / `reset` (lowercase) are ordinary names; only `Clock` / `Reset` are
// type words. This is what lets the minted `clock:Clock` / `reset:Reset` and a
// tick block's `clock` read need no backticks.
TEST(Parser, LowercaseClockResetArePlainNames) {
  // binding names
  EXPECT_TRUE(parses("const clock = 1\nmut reset = 0\nreset = clock\n"));
  EXPECT_TRUE(parses("reg clock:U8 = 0\n"));
  // port names typed with the real type words
  EXPECT_TRUE(parses("mod m(clock:Clock, reset:Reset, x:U1) -> (o:U1) { o = x }\n"));
  EXPECT_TRUE(parses("comb f(clock:U8, reset:U8) -> (r:U8) { r = clock + reset }\n"));
  // field names, both reads and writes
  EXPECT_TRUE(parses("const x = t.clock\nconst y = t.reset\n"));
  EXPECT_TRUE(parses("x.clock = 1\nx.reset = 2\n"));
  EXPECT_TRUE(parses("const t = (clock = 1, reset = 2)\n"));
  // named arguments
  EXPECT_TRUE(parses("const x = f(clock=1, reset=2)\n"));
  // bare reference inside a tick block of a test
  EXPECT_TRUE(parses("test t {\n  tick 3 {\n    dut.a = 100 + clock\n    dut.reset = clock < 2\n  }\n}\n"));
  // the type words stay reserved as names
  for (const char* src : {"const Clock = 1\n", "const Reset = 1\n", "mod m(Clock:U1) -> () { }\n",
                          "mod m(Reset:U1) -> () { }\n", "const x = t.Clock\n", "x.Reset = 1\n",
                          "const x = f(Clock=1)\n", "const t = (Reset = 1)\n", "reg Clock:U8 = 0\n"}) {
    EXPECT_FALSE(parses(src)) << src;
  }
  EXPECT_EQ(diag_of("const Clock = 1\n").code, "reserved-type-name");
  EXPECT_EQ(diag_of("const Reset = 1\n").code, "reserved-type-name");
  // backticked type words are ordinary names
  EXPECT_TRUE(parses("const `Clock` = 1\nconst `Reset` = 2\nconst x = t.`Clock`\n"));
  // the old lowercase type spellings are ordinary names (owner ruling 2026-09-30)
  EXPECT_TRUE(parses("const bool = 1\nconst unsigned = 1\n"));
  // reserved keyword spelling is exact
  EXPECT_FALSE(parses("const if = 1\n"));
  EXPECT_TRUE(parses("const IF = 1\nconst If = 2\nconst TiCk = 3\n"));
}

TEST(Parser, SynthesisAttributeKeysAndTupleDefaults) {
  const std::string source = R"(pub comb f::[synth=(color="crit",adder="cla")](a:U8)->(y:U8) {
    mut v::[synth.color="lane",synth.adder="brent"] = a+1
    {::[synth.color=7,synth.grow=false] y=f::[name=u0,synth.color="execute"](a=v).y }
  })";
  EXPECT_TRUE(parses(source));
  EXPECT_NE(sexp(source).find("dot_expression"), std::string::npos);
  EXPECT_FALSE(parses("mut v::[synth..color=1] = 0"));
}
