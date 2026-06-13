// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string_view>

#include "hhds/tree.hpp"  // hhds::Type (uint16_t)

namespace prpparse {

// Node kinds mirror the tree-sitter-pyrope grammar node names (generated from
// src/node-types.json). `invalid` = 0 so an unset hhds node (default Type 0)
// decodes to (invalid, none).
enum class Kind : uint16_t {
  invalid = 0,
#define PRP_NODE(name) name,
#include "prp_nodes.def"
};

// Field roles = the edge label of a node under its parent (tree-sitter field
// names). Enumerators are prefixed `f_` because some names (`else`, `operator`)
// are C++ keywords. `none` = 0 (no role / a positional child).
enum class Field : uint16_t {
  none = 0,
#define PRP_FIELD(name) f_##name,
#include "prp_fields.def"
};

// hhds Type packing: kind in the high 10 bits, field in the low 6 bits.
inline constexpr int    kFieldBits = 6;
inline constexpr uint16_t kFieldMask = (1u << kFieldBits) - 1;

inline hhds::Type pack_type(Kind k, Field f = Field::none) {
  return static_cast<hhds::Type>((static_cast<uint16_t>(k) << kFieldBits) |
                                 static_cast<uint16_t>(f));
}
inline Kind  type_kind(hhds::Type t) { return static_cast<Kind>(t >> kFieldBits); }
inline Field type_field(hhds::Type t) { return static_cast<Field>(t & kFieldMask); }

inline std::string_view kind_name(Kind k) {
  switch (k) {
    case Kind::invalid:
      return "ERROR";
#define PRP_NODE(name)  \
  case Kind::name:      \
    return #name;
#include "prp_nodes.def"
  }
  return "?";
}

inline std::string_view field_name(Field f) {
  switch (f) {
    case Field::none:
      return "";
#define PRP_FIELD(name)   \
  case Field::f_##name:   \
    return #name;
#include "prp_fields.def"
  }
  return "?";
}

}  // namespace prpparse
