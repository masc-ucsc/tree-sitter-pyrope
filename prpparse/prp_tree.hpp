// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>

#include "ast.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/source_locator.hpp"
#include "hhds/tree.hpp"
#include "source_buffer.hpp"

namespace prpparse {

// Owns the hhds tree built from a parsed Ast, plus the Source_locator that the
// minted attrs::srcid resolve against.
class Prp_tree {
public:
  explicit Prp_tree(const Source_buffer& buf);

  // Build the hhds tree from `root`. Mints srcid for every node carrying a
  // non-empty source span. No-op if `root` is null.
  void materialize(const Ast* root);

  // tree-sitter-style named-node s-expression (field labels, no positions).
  [[nodiscard]] std::string to_sexp() const;

  [[nodiscard]] hhds::Tree&           tree() { return *tree_; }
  [[nodiscard]] hhds::Source_locator& locator() { return loc_; }

private:
  using Node = hhds::Tree::Node_class;

  void materialize_node(const Ast* a, Node n);
  void sexp_rec(Node n, std::string& out, int depth) const;

  const Source_buffer&        buf_;
  std::string                 path_;  // sanitized (relative) for Source_locator
  std::shared_ptr<hhds::Tree> tree_;
  hhds::Source_locator        loc_;
  bool                        has_root_ = false;
};

}  // namespace prpparse
