// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace prpparse {

// ---------------------------------------------------------------------------
// Token kinds (generated from prp_tokens.def)
// ---------------------------------------------------------------------------
enum class Token_kind : uint16_t {
#define PRP_TOKEN(name, spelling) name,
#include "prp_tokens.def"
};

inline std::string_view to_string(Token_kind k) {
  switch (k) {
#define PRP_TOKEN(name, spelling) \
  case Token_kind::name:          \
    return spelling;
#include "prp_tokens.def"
  }
  return "<?>";
}

// ---------------------------------------------------------------------------
// Keywords (generated from prp_keywords.def). `none` = the token is a plain
// identifier with no keyword spelling.
// ---------------------------------------------------------------------------
enum class Keyword : uint16_t {
  none = 0,
#define PRP_KEYWORD(name) kw_##name,
#include "prp_keywords.def"
};

inline std::string_view keyword_spelling(Keyword k) {
  switch (k) {
    case Keyword::none:
      return "";
#define PRP_KEYWORD(name)    \
  case Keyword::kw_##name:   \
    return #name;
#include "prp_keywords.def"
  }
  return "";
}

// Returns the Keyword id for a word spelling, or Keyword::none.
inline Keyword classify_keyword(std::string_view s) {
  static const std::unordered_map<std::string_view, Keyword> table = {
#define PRP_KEYWORD(name) {#name, Keyword::kw_##name},
#include "prp_keywords.def"
  };
  auto it = table.find(s);
  return it == table.end() ? Keyword::none : it->second;
}

// ---------------------------------------------------------------------------
// Token
// ---------------------------------------------------------------------------
struct Token {
  Token_kind       kind = Token_kind::invalid;
  Keyword          kw   = Keyword::none;  // set only for `ident` tokens
  uint32_t         start_byte = 0;
  uint32_t         end_byte   = 0;  // exclusive
  uint32_t         line       = 1;  // 1-based line of `start_byte`
  bool             terminator_before = false;  // virtual `;` precedes this token
  std::string_view text;            // view into the Source_buffer

  [[nodiscard]] bool is(Token_kind k) const { return kind == k; }
  [[nodiscard]] bool is_kw(Keyword k) const { return kind == Token_kind::ident && kw == k; }
  [[nodiscard]] bool is_ident() const { return kind == Token_kind::ident; }
  // A bare identifier usable as a name (any word, keyword or not).
  [[nodiscard]] bool is_word() const { return kind == Token_kind::ident; }
};

}  // namespace prpparse
