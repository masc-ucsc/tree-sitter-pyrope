// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ast.hpp"
#include "hhds/attrs/srcid.hpp"
#include "hhds/source_locator.hpp"
#include "hhds/tree.hpp"
#include "source_buffer.hpp"

namespace prpparse {

// Owns the hhds tree built from a parsed Ast, plus the Source_locator that the
// minted attrs::srcid resolve against.
//
// Provenance is built lazily and off the critical path. materialize() only
// records each spanned node's raw byte span in a flat side-table (spans_); it
// does NOT mint srcid or touch the Source_locator. srcid is a pure function of
// (path, start, end), so it can be recomputed any time it is actually needed:
//
//   - Diagnostics need a *span*, not a srcid, and a downstream pass holds the
//     spans here directly — so error reporting never forces minting.
//   - The persisted srcmap is the only real srcid consumer. build_srcmap_async()
//     mints every recorded span into loc_ on one background thread, meant to
//     overlap the next (slow) consumer pass. Parse-only runs never call it and
//     pay nothing.
class Prp_tree {
public:
  explicit Prp_tree(const Source_buffer& buf);
  ~Prp_tree();

  Prp_tree(const Prp_tree&)            = delete;
  Prp_tree& operator=(const Prp_tree&) = delete;

  // Build the hhds tree from `root`, recording spanned nodes into spans_.
  // `expected_spans` pre-sizes that table (an upper bound is fine). No-op if
  // `root` is null.
  void materialize(const Ast* root, size_t expected_spans = 0);

  // tree-sitter-style named-node s-expression (field labels, no positions).
  [[nodiscard]] std::string to_sexp() const;

  [[nodiscard]] hhds::Tree& tree() { return *tree_; }

  // ---- source provenance (lazy; off the critical path) ---------------------

  // Kick off background construction of the Source_locator (a srcid for every
  // recorded span) on a single worker thread, so it overlaps the next pass.
  // No-op if already started or built; safe to never call.
  void build_srcmap_async();

  // The Source_locator for this tree. Joins the background worker if one is
  // running, or builds it synchronously if it was never started.
  [[nodiscard]] hhds::Source_locator& locator();

  // Recorded spans (= spanned nodes). Available immediately after materialize().
  [[nodiscard]] size_t span_count() const { return spans_.size(); }

private:
  using Node = hhds::Tree::Node_class;

  // A spanned node: its tree position plus the raw byte span its srcid is later
  // hashed from. POD and 16 bytes — a flat vector of these is one cheap
  // allocation and frees in one shot (vs tearing down a 9M-entry hash map).
  struct Span_rec {
    hhds::Tree_pos pos;
    uint32_t       start;
    uint32_t       end;
  };

  void materialize_node(const Ast* a, const Node& n);
  void sexp_rec(const Node& n, std::string& out, int depth) const;
  void build_srcmap();    // worker body: mint every recorded span into loc_
  void ensure_srcmap();   // join the worker or build synchronously, once

  const Source_buffer&        buf_;
  std::string                 path_;  // sanitized (relative) for Source_locator
  std::shared_ptr<hhds::Tree> tree_;
  hhds::Source_locator        loc_;
  std::vector<Span_rec>       spans_;
  std::thread                 worker_;
  bool                        worker_started_ = false;
  bool                        srcmap_built_   = false;
  bool                        has_root_       = false;
};

}  // namespace prpparse
