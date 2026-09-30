#include "../prpfmt_api.h"

#include <cassert>
#include <cstdlib>
#include <string>

int main() {
  const std::string src = "const result = f(first=some_long_value, second=another_long_value)\n";
  char *out = nullptr;
  size_t len = 0;
  assert(prpfmt_format_string(src.data(), src.size(), 2, 20, 1, &out, &len) == 0);
  const std::string ai(out, len);
  free(out);
  assert(ai == src);

  assert(prpfmt_format_string_mode(src.data(), src.size(), 2, 40, PRPFMT_HUMAN, 1, &out, &len) == 0);
  const std::string human(out, len);
  free(out);
  assert(human == "const result = f(\n  first=some_long_value,\n  second=another_long_value,\n)\n");
  assert(prpfmt_format_string_mode(human.data(), human.size(), 2, 40, PRPFMT_HUMAN, 1, &out, &len) == 0);
  assert(std::string(out, len) == human);
  free(out);

  // Malformed input never returns a formatted buffer, even without verify.
  for (const std::string bad : {"const `` = 1\n", "const x = @\n", "const x = (1\n"}) {
    for (const auto mode : {PRPFMT_AI, PRPFMT_HUMAN}) {
      out = nullptr;
      len = 99;
      assert(prpfmt_format_string_mode(bad.data(), bad.size(), 0, 0, mode, 0, &out, &len) == 2);
      assert(out == nullptr && len == 0);
    }
  }

  // The byte-count API must not read beyond the provided source span.
  const std::string partial = "const x = 1\nNOT VALID PYROPE";
  assert(prpfmt_format_string(partial.data(), 12, 0, 0, 1, &out, &len) == 0);
  assert(std::string(out, len) == "const x = 1\n");
  free(out);
  assert(prpfmt_format_string("comb ???", 8, 0, 0, 1, &out, &len) == 2);
  assert(out == nullptr && len == 0);

  // Both layouts: comparisons always keep their spaces (also under a logical
  // operator), and the comma that makes `(0,)` a one-element tuple (not the
  // scalar `(0)`) survives.
  const std::string cmp = "if foo != bar or bar == foo {\n  acc = (0,)\n}\nconst c = a == b\nconst t = ('c',)\n"
                          "const q = x == -1 and y in z\n";
  const std::string cmp_expected = cmp;
  assert(prpfmt_format_string(cmp.data(), cmp.size(), 0, 0, 1, &out, &len) == 0);
  assert(std::string(out, len) == cmp_expected);
  free(out);
  assert(prpfmt_format_string_mode(cmp.data(), cmp.size(), 0, 0, PRPFMT_HUMAN, 1, &out, &len) == 0);
  assert(std::string(out, len) == cmp_expected);
  free(out);

  // Block comments nest (`/* a /* b */ c */` is one comment, in lhd and in
  // the grammar): the whole comment, inner `*/` included, prints unchanged.
  const std::string nested_comment = "r = b\n/* old /" "* note */ if a == 0 // */\nr = a /* x /" "* y */ z */ + 1\n";
  assert(prpfmt_format_string(nested_comment.data(), nested_comment.size(), 0, 0, 1, &out, &len) == 0);
  assert(std::string(out, len) == nested_comment);
  free(out);

  // The capitalized type words are reserved: a backticked `U4`/`Bool` is a
  // name (not the type) and keeps its backticks. Reserved words and banned
  // type spellings retain their escapes regardless of case.
  const std::string type_words = "mut `U4`:U8 = U8(x) + `u8` + `Bool`\nmod m(c:Clock, r:Reset) -> (o:S4) {\n  o = 0\n}\n";
  const std::string type_words_expected = "mut `U4`:U8 = U8(x) + `u8` + `Bool`\nmod m(c:Clock, r:Reset) -> (o:S4) {\n  o = 0\n}\n";
  assert(prpfmt_format_string(type_words.data(), type_words.size(), 0, 0, 1, &out, &len) == 0);
  assert(std::string(out, len) == type_words_expected);
  free(out);

  const std::string reserved_case = "const `ELSE` = 1\nconst `eLsE` = 2\nconst `u8` = 3\nconst `I32` = 4\nconst `cLoCk` = 5\n";
  assert(prpfmt_format_string(reserved_case.data(), reserved_case.size(), 0, 0, 1, &out, &len) == 0);
  assert(std::string(out, len) == reserved_case);
  free(out);

  // Deep nesting formats on a large-stack thread; a tree too deep for it is
  // refused with 4 instead of crashing.
  const std::string nested = "const x = " + std::string(20000, '-') + "a\n";
  assert(prpfmt_format_string(nested.data(), nested.size(), 0, 0, 1, &out, &len) == 0);
  free(out);
  const std::string too_deep = "const x = " + std::string(250000, '-') + "a\n";
  assert(prpfmt_format_string(too_deep.data(), too_deep.size(), 0, 0, 1, &out, &len) == 4);
  assert(out == nullptr && len == 0);
}
