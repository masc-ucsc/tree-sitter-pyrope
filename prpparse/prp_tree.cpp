// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp_tree.hpp"

namespace prpparse {

Prp_tree::Prp_tree(const Source_buffer& buf) : buf_(buf) {
  // Source_locator requires a workspace-relative path (asserts on leading '/').
  path_ = buf.path();
  size_t i = 0;
  while (i < path_.size() && path_[i] == '/') ++i;
  path_ = path_.substr(i);

  tree_ = hhds::Tree::create();
}

void Prp_tree::materialize(const Ast* root) {
  if (!root) return;
  Node n = tree_->add_root_node();
  has_root_ = true;
  materialize_node(root, n);
}

void Prp_tree::materialize_node(const Ast* a, Node n) {
  n.set_type(pack_type(a->kind, a->field));
  if (a->end_byte > a->start_byte) {
    hhds::SourceId id =
        loc_.mint(path_, a->start_byte, a->end_byte, buf_.line_of(a->start_byte));
    n.attr(hhds::attrs::srcid).set(id);
  }
  for (const Ast* k : a->kids) {
    Node c = n.add_child();
    materialize_node(k, c);
  }
}

std::string Prp_tree::to_sexp() const {
  if (!has_root_) return "()";
  std::string out;
  sexp_rec(tree_->get_root_node(), out, 0);
  return out;
}

void Prp_tree::sexp_rec(Node n, std::string& out, int depth) const {
  out.append(static_cast<size_t>(depth) * 2, ' ');
  Field f = type_field(n.get_type());
  if (f != Field::none) {
    out += field_name(f);
    out += ": ";
  }
  out += '(';
  out += kind_name(type_kind(n.get_type()));
  for (Node c = n.first_child(); c.is_valid(); c = c.next_sibling()) {
    out += '\n';
    sexp_rec(c, out, depth + 1);
  }
  out += ')';
}

}  // namespace prpparse
