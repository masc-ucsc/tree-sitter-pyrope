// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Phase-0 smoke test: build a typed hhds tree, mint a Source_locator span,
// attach it as attrs::srcid, resolve it back, and render with Tree::print.
// Validates the exact hhds API surface prpparse depends on.

#include <sstream>
#include <string>

#include "gtest/gtest.h"
#include "hhds/attrs/srcid.hpp"
#include "hhds/source_locator.hpp"
#include "hhds/tree.hpp"
#include "prpparse.hpp"

TEST(Smoke, Version) { EXPECT_STREQ(prpparse::version, "0.1.0"); }

TEST(Smoke, HelloTree) {
  auto tree = hhds::Tree::create();

  auto root = tree->add_root_node();
  root.set_type(1);
  auto lhs = root.add_child();
  lhs.set_type(2);
  auto rhs = root.add_child();
  rhs.set_type(3);

  EXPECT_EQ(root.get_type(), 1);
  EXPECT_EQ(lhs.get_type(), 2);
  EXPECT_EQ(rhs.get_type(), 3);
}

TEST(Smoke, SrcidRoundTrip) {
  auto tree = hhds::Tree::create();
  auto root = tree->add_root_node();
  root.set_type(1);
  auto lhs = root.add_child();
  lhs.set_type(2);

  hhds::Source_locator loc;
  hhds::SourceId sid = loc.mint("smoke.prp", /*start*/ 0, /*end*/ 5, /*line*/ 1);
  lhs.attr(hhds::attrs::srcid).set(sid);

  ASSERT_TRUE(lhs.attr(hhds::attrs::srcid).has());
  EXPECT_EQ(lhs.attr(hhds::attrs::srcid).get(), sid);

  auto anchor = loc.resolve(sid);
  ASSERT_TRUE(anchor.has_value());
  EXPECT_EQ(anchor->start_byte, 0u);
  EXPECT_EQ(anchor->end_byte, 5u);
  EXPECT_EQ(anchor->line, 1u);
}

TEST(Smoke, TreePrint) {
  auto tree = hhds::Tree::create();
  auto root = tree->add_root_node();
  root.set_type(1);
  auto lhs = root.add_child();
  lhs.set_type(2);
  auto rhs = root.add_child();
  rhs.set_type(3);

  const hhds::Type_entry type_table[] = {
      {"unknown"},
      {"add"},
      {"lhs"},
      {"rhs"},
  };
  hhds::Tree::PrintOptions opts;
  opts.type_table = type_table;

  std::ostringstream os;
  tree->print(os, opts);
  EXPECT_FALSE(os.str().empty());
}
