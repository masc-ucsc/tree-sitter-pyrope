// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "node_kind.hpp"

namespace prpparse {

// Lightweight intermediate node built during recursive descent. Stores byte
// offsets only (no string copies — string_view discipline holds). The parser
// can freely restructure these (e.g. reinterpret a parsed expression as an
// lvalue) before materializing into the hhds tree. Arena-owned, so raw Ast*
// pointers stay valid for the parse's lifetime.
struct Ast {
  Kind              kind  = Kind::invalid;
  Field             field = Field::none;  // role under its parent
  uint32_t          start_byte = 0;
  uint32_t          end_byte   = 0;  // exclusive
  std::vector<Ast*> kids;

  void add(Ast* child) {
    if (child) kids.push_back(child);
  }
  void add(Ast* child, Field f) {
    if (child) {
      child->field = f;
      kids.push_back(child);
    }
  }
  [[nodiscard]] bool empty_span() const { return end_byte <= start_byte; }
};

// Stable-address arena for Ast nodes.
class Ast_arena {
public:
  Ast* make(Kind k, uint32_t start = 0, uint32_t end = 0) {
    Ast& n = pool_.emplace_back();
    n.kind = k;
    n.start_byte = start;
    n.end_byte = end;
    return &n;
  }

private:
  std::deque<Ast> pool_;
};

}  // namespace prpparse
