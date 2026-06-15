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
  // tree-sitter distinguishes named nodes (grammar rules) from anonymous tokens
  // (punctuation / keywords). prpparse omits anonymous tokens by default, but a
  // few carry a FIELD the LiveHD consumer reads (arg-list `mod` = ref/reg/...,
  // a match-arm condition operator). Those are emitted as marker nodes flagged
  // `named = false`, so the node facade can replay tree-sitter's named/all-child
  // distinction (named_child skips them; child includes them; is_named == false).
  bool              named = true;
  // Parent back-pointer (the facade's ts_node_parent / next_named_sibling). Set
  // by link_parents() after the construct is fully built (handles re-parenting).
  Ast*              parent = nullptr;
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

// Set every node's `parent` from the tree rooted at `root` (depth-first). Called
// once after a top-level construct is parsed, so re-parenting done during the
// parse (e.g. tuple -> lvalue_list) is reflected.
inline void link_parents(Ast* root) {
  if (!root) return;
  for (Ast* k : root->kids) {
    k->parent = root;
    link_parents(k);
  }
}

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

  // Total nodes allocated so far — an upper bound on spanned nodes, used to
  // pre-size the materialized tree's span side-table.
  [[nodiscard]] size_t size() const { return pool_.size(); }

private:
  std::deque<Ast> pool_;
};

}  // namespace prpparse
