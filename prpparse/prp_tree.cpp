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

Prp_tree::~Prp_tree() {
  if (worker_.joinable()) worker_.join();
}

void Prp_tree::materialize(const Ast* root, size_t expected_spans) {
  if (!root) return;
  spans_.reserve(expected_spans);
  Node n = tree_->add_root_node();
  has_root_ = true;
  materialize_node(root, n);
}

void Prp_tree::materialize_node(const Ast* a, const Node& n) {
  n.set_type(pack_type(a->kind, a->field));
  // Record the raw span only; srcid is minted later (build_srcmap). No hashing,
  // no Source_locator, no attr-store insert on this hot path.
  if (a->end_byte > a->start_byte) {
    spans_.push_back(Span_rec{n.get_debug_nid(), a->start_byte, a->end_byte});
  }
  for (const Ast* k : a->kids) {
    Node c = n.add_child();
    materialize_node(k, c);
  }
}

void Prp_tree::build_srcmap() {
  // path_ must be workspace-relative and non-empty for the locator; an empty
  // path (pathological input) simply yields no provenance rather than asserting.
  if (spans_.empty() || path_.empty()) return;
  loc_.reserve(spans_.size());
  auto minter = loc_.span_minter(path_);
  for (const Span_rec& s : spans_) {
    minter.mint(s.start, s.end, buf_.line_of(s.start));
  }
}

void Prp_tree::build_srcmap_async() {
  if (worker_started_ || srcmap_built_) return;
  worker_started_ = true;
  worker_         = std::thread([this] { build_srcmap(); });
}

void Prp_tree::ensure_srcmap() {
  if (srcmap_built_) return;
  if (worker_started_) {
    if (worker_.joinable()) worker_.join();
  } else {
    build_srcmap();
  }
  srcmap_built_ = true;
}

hhds::Source_locator& Prp_tree::locator() {
  ensure_srcmap();
  return loc_;
}

std::string Prp_tree::to_sexp() const {
  if (!has_root_) return "()";
  std::string out;
  sexp_rec(tree_->get_root_node(), out, 0);
  return out;
}

void Prp_tree::sexp_rec(const Node& n, std::string& out, int depth) const {
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
