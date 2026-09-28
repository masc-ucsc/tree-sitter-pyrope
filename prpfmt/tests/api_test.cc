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
  assert(human == "const result = f(\n  first=some_long_value,\n  second=another_long_value\n)\n");
  assert(prpfmt_format_string_mode(human.data(), human.size(), 2, 40, PRPFMT_HUMAN, 1, &out, &len) == 0);
  assert(std::string(out, len) == human);
  free(out);

  // The byte-count API must not read beyond the provided source span.
  const std::string partial = "const x = 1\nNOT VALID PYROPE";
  assert(prpfmt_format_string(partial.data(), 12, 0, 0, 1, &out, &len) == 0);
  assert(std::string(out, len) == "const x = 1\n");
  free(out);
  assert(prpfmt_format_string("comb ???", 8, 0, 0, 1, &out, &len) == 2);
  assert(out == nullptr && len == 0);

  // Both layouts: comparisons under a looser logical operator print tight, a
  // comparison on its own keeps its spaces, and the comma that makes `(0,)` a
  // one-element tuple (not the scalar `(0)`) survives.
  const std::string cmp = "if foo != bar or bar == foo {\n  acc = (0,)\n}\nconst c = a == b\nconst t = ('c',)\n"
                          "const q = x == -1 and y in z\n";
  const std::string cmp_expected = "if foo!=bar or bar==foo {\n  acc = (0,)\n}\nconst c = a == b\nconst t = ('c',)\n"
                                   "const q = x==-1 and y in z\n";
  assert(prpfmt_format_string(cmp.data(), cmp.size(), 0, 0, 1, &out, &len) == 0);
  assert(std::string(out, len) == cmp_expected);
  free(out);
  assert(prpfmt_format_string_mode(cmp.data(), cmp.size(), 0, 0, PRPFMT_HUMAN, 1, &out, &len) == 0);
  assert(std::string(out, len) == cmp_expected);
  free(out);
}
