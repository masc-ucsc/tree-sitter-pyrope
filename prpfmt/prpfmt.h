#ifndef PRP_FMT_H
#define PRP_FMT_H

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <tree_sitter/api.h>
#include "ir.h"
#include "prpfmt_api.h"

/*
 * SpacingConfig controls the emission of spaces around tokens (primarily operators
 * and punctuation) during the initial IR hooking phase.
 * It is implemented as a bitmask so flags can be combined.
 */
enum SpacingConfig {
  SPACE_NONE   = 0,
  SPACE_BEFORE = 1 << 0,
  SPACE_AFTER  = 1 << 1,
  SPACE_BOTH   = 3
};

// Parent of every named node, built once per file: tree-sitter's
// ts_node_parent() walks down from the root (O(depth)), so the per-node
// ancestor checks were cubic on deeply nested input.
struct NodeKey {
  const void *id;
  uint32_t start;
  bool operator==(const NodeKey &) const = default;
};
struct NodeKeyHash {
  size_t operator()(const NodeKey &k) const { return std::hash<const void *>()(k.id) ^ (size_t(k.start) << 1); }
};
using ParentMap = std::unordered_map<NodeKey, TSNode, NodeKeyHash>;

struct PrpfmtState {
  std::string_view source_code; // Input source for text extraction via get_node_text
  FILE *outfile;           // Output target (stdout or file)
  int indent_size;         // Spaces per level (default: 2)
  int max_width;           // Human-mode soft width target (default: 132)
  bool in_assert;          // True if currently printing an assertion (for alignment)
  bool allow_inline;       // Contextual permission for blocks to stay on one line
  int nesting_level;       // Current block depth (0 = top level)
  bool fmt_on;             // Toggle for 'prpfmt on/off' directives
  bool inline_exp;         // If true, suppresses newlines for nested expressions
  TokenBuffer buffer;      // Buffer for IR
  PrpfmtMode mode = PRPFMT_AI;
  // Same-file lambda declarations by (backtick-free) name, used to decide
  // when `f(x=x)` may print as the shorthand `f(x)` and when a bare `x` in a
  // call counts as the named binding `x=x` for sorting (see resolve_callee).
  struct CalleeSig {
    int decls = 0;             // statement-level `comb|mod|pipe|fluid NAME` declarations
    bool plain = true;         // no `self` first parameter, no `...` gather
    std::vector<std::string> params;
    TSNode scope{};            // block that declares it; calls must sit inside
  };
  // What collect_callees learns about the file's names and calls. Collected
  // once per file; the states that format string holes share it.
  struct CallFacts {
    std::unordered_map<std::string, CalleeSig> callees;
    // Other bindings of the same names (variables, parameters, aliases, nested
    // or tuple-member lambdas, imports): any of these makes the name unresolved.
    std::unordered_map<std::string, int> other_bindings;
    // The unnamed arguments of every call in the file, by callee name (the last
    // name of a dotted callee): the name of a bare `x` / `ref x` argument (a
    // possible same-name pun), or "" for any other value. See
    // bound_by_position.
    std::unordered_map<std::string, std::vector<std::string>> unnamed_args;
  };
  std::shared_ptr<CallFacts> calls = std::make_shared<CallFacts>();
  // Comments between a branch's `}` and the next `elif`/`else`, handed from
  // print_if_expression to the branch block, which prints them after its `{`.
  std::vector<TSNode> header_comments{};
  // Comments trailing a one-line branch `if c { a } // c` before the next
  // `elif`/`else`, handed from print_if_expression to that branch's block,
  // which prints them after its last statement.
  std::vector<TSNode> tail_comments{};
  const ParentMap *parents = nullptr;  // see parent_of()
  // Memoized subtree scans (has_recursive_line_comment, holds_vertical_layout,
  // scope_must_break), keyed by node byte range and symbol, so nested
  // expressions are scanned once instead of once per enclosing check.
  struct NodeKey {
    uint32_t start, end, symbol, kind;
    bool operator==(const NodeKey &) const = default;
  };
  struct NodeKeyHash {
    size_t operator()(const NodeKey &k) const {
      uint64_t h = (uint64_t(k.start) << 32) ^ k.end;
      h ^= (uint64_t(k.symbol) << 8 | k.kind) * 0x9e3779b97f4a7c15ULL;
      return std::hash<uint64_t>{}(h);
    }
  };
  mutable std::unordered_map<NodeKey, bool, NodeKeyHash> scan_cache{};
  // Indices of the groups emitted but not closed yet (see Token::match).
  std::vector<int> open_groups{};
};

/*
 * SYMBOL IDS
 * tree-sitter renumbers every grammar symbol on each `tree-sitter generate`,
 * so prpfmt's dispatch ids are lifted directly from the generated parser at
 * build time (see gen_symbols.sh and the Makefile rule). Never hand-edit
 * ts_symbols.h; regenerate it.
 */
#include "ts_symbols.h"

/******************************************************************************
 * 1. Entry & High-Level Dispatch                                             *
 ******************************************************************************/
void print_description(TSTree *tree, PrpfmtState &st);
// A UTF-8 byte order mark at the start of the file is an error (owner ruling
// 108: only ASCII blanks separate tokens; prpparse "non-ASCII space"), but the
// tree-sitter runtime skips it, so the formatter refuses such an input itself.
inline bool has_leading_bom(std::string_view src) { return src.starts_with("\xEF\xBB\xBF"); }
// Format a parsed file. When the output would reparse into a different shape
// (see Parse_shape in prpfmt.cc), it formats again keeping every grouping
// parenthesis and source order. *parse_error: the output does not parse.
// *too_deep: the input nests too deeply to format safely; nothing is formatted
// (an empty result) and *parse_error is set as well.
std::string prpfmt_format(TSParser *parser, TSTree *tree, std::string_view src, int indent_size, int max_width,
                          PrpfmtMode mode, bool *parse_error, bool *too_deep = nullptr);
bool print__statement(TSNode node, PrpfmtState &st, TSNode prev_node, bool is_inline);

/******************************************************************************
 * 2. Structural (Scopes, Lists, Tuples)                                      *
 ******************************************************************************/
// value_block: the branch block of an `if`/`match` expression in value
// position, whose value keeps its expression layout when the block breaks.
void print_scope_statement(TSNode node, PrpfmtState &st, bool is_inline, bool value_block = false);
void print_stmt_list(TSNode node, PrpfmtState &st);
void print_tuple(TSNode node, PrpfmtState &st);
void print_type_call_args(TSNode node, PrpfmtState &st);
void print_assertion_args(TSNode node, PrpfmtState &st);
void print_tuple_sq(TSNode node, PrpfmtState &st);
void print_attribute_sq(TSNode node, PrpfmtState &st);
void print_attribute_assignment(TSNode node, PrpfmtState &st);
void print__tuple_list(TSNode node, PrpfmtState &st, SpacingConfig spacing);
void print__tuple_item(TSNode node, PrpfmtState &st, SpacingConfig spacing);
void print_arg_assignment(TSNode node, PrpfmtState &st, SpacingConfig spacing);
void print_step_statement(TSNode node, PrpfmtState &st);

/******************************************************************************
 * 3. Control Flow                                                            *
 ******************************************************************************/
void print_if_expression(TSNode node, PrpfmtState &st, bool is_inline);
void print_match_expression(TSNode node, PrpfmtState &st, bool is_inline);
void print_for_statement(TSNode node, PrpfmtState &st);
void print_while_statement(TSNode node, PrpfmtState &st);
void print_loop_statement(TSNode node, PrpfmtState &st);
void print_control_statement(TSNode node, PrpfmtState &st);
void print_return_statement(TSNode node, PrpfmtState &st);
void print_break_statement(TSNode node, PrpfmtState &st);
void print_continue_statement(TSNode node, PrpfmtState &st);

/******************************************************************************
 * 4. Assignments & Declarations                                              *
 ******************************************************************************/
void print_assignment(TSNode node, PrpfmtState &st, SpacingConfig spacing);
void print_lvalue_item(TSNode node, PrpfmtState &st);
void print_lvalue_list(TSNode node, PrpfmtState &st);
void print_named_lvalue(TSNode node, PrpfmtState &st);
void print_declaration_statement(TSNode node, PrpfmtState &st);
void print_stage_decl(TSNode node, PrpfmtState &st);
void print_enum_assignment(TSNode node, PrpfmtState &st);
void print_enum_definition(TSNode node, PrpfmtState &st);
void print_assignment_operator(TSNode node, PrpfmtState &st, SpacingConfig spacing);

/******************************************************************************
 * 5. Functions & Parameters                                                  *
 ******************************************************************************/
void print_lambda(TSNode node, PrpfmtState &st);
void print_pipe_lambda(TSNode node, PrpfmtState &st);
void print_fluid_lambda(TSNode node, PrpfmtState &st);
void print_function_definition_decl(TSNode node, PrpfmtState &st);
void print_arg_list(TSNode node, PrpfmtState &st);
void print_function_call_expression(TSNode node, PrpfmtState &st);

/******************************************************************************
 * 6. Expressions & Selection                                                 *
 ******************************************************************************/
void print__expression(TSNode node, PrpfmtState &st, bool is_inline);
void print__restricted_expression(TSNode node, PrpfmtState &st);
void print_expression_item(TSNode node, PrpfmtState &st);
void print__binary_times(TSNode node, PrpfmtState &st);
void print__binary_other(TSNode node, PrpfmtState &st);
void print__binary_step(TSNode node, PrpfmtState &st);
void print__binary_compare(TSNode node, PrpfmtState &st);
void print__binary_logical(TSNode node, PrpfmtState &st);
void print_unary_expression(TSNode node, PrpfmtState &st);
void print_dot_expression(TSNode node, PrpfmtState &st);
void print_type_cast(TSNode node, PrpfmtState &st);
void print_test_name(TSNode node, PrpfmtState &st);
void print_tick_statement(TSNode node, PrpfmtState &st);
void print_member_selection(TSNode node, PrpfmtState &st);
void print_paren_group(TSNode node, PrpfmtState &st);
void print_bit_selection(TSNode node, PrpfmtState &st);
void print__suffix_head(TSNode node, PrpfmtState &st);

void print_attribute_read(TSNode node, PrpfmtState &st);
void print_select(TSNode node, PrpfmtState &st);
void print_selection_range(TSNode node, PrpfmtState &st);

/******************************************************************************
 * 7. Types & Identifiers                                                     *
 ******************************************************************************/
void print__type(TSNode node, PrpfmtState &st);
void print_lambda_type(TSNode node, PrpfmtState &st);
void print_expression_type(TSNode node, PrpfmtState &st);
void print_dot_expression_type(TSNode node, PrpfmtState &st);
void print_function_call_type(TSNode node, PrpfmtState &st);
void print_array_type(TSNode node, PrpfmtState &st);
void print_array_length(TSNode node, PrpfmtState &st);
void print__primitive_type(TSNode node, PrpfmtState &st);
void print_type_statement(TSNode node, PrpfmtState &st);
void print_typed_identifier(TSNode node, PrpfmtState &st);
void print_typed_identifier_list(TSNode node, PrpfmtState &st);
void print_ref_identifier(TSNode node, PrpfmtState &st);
void print__complex_identifier(TSNode node, PrpfmtState &st);
void print_timed_identifier(TSNode node, PrpfmtState &st);
void print_var_or_let_or_reg(TSNode node, PrpfmtState &st);
void print_identifier(TSNode node, PrpfmtState &st);
void print_uint_type(TSNode node, PrpfmtState &st);
void print_sint_type(TSNode node, PrpfmtState &st);
void print_bool_type(TSNode node, PrpfmtState &st);
void print_string_type(TSNode node, PrpfmtState &st);

/******************************************************************************
 * 8. Literals & Numbers                                                      *
 ******************************************************************************/
void print_constant(TSNode node, PrpfmtState &st);
void print_integer_literal(TSNode node, PrpfmtState &st);
void print__simple_number(TSNode node, PrpfmtState &st);
void print__scaled_number(TSNode node, PrpfmtState &st);
void print__hex_number(TSNode node, PrpfmtState &st);
void print__decimal_number(TSNode node, PrpfmtState &st);
void print__octal_number(TSNode node, PrpfmtState &st);
void print__binary_number(TSNode node, PrpfmtState &st);
void print__typed_number(TSNode node, PrpfmtState &st);
void print_bool_literal(TSNode node, PrpfmtState &st);
void print_unknown_literal(TSNode node, PrpfmtState &st);
void print__string_literal(TSNode node, PrpfmtState &st);
void print_string_literal(TSNode node, PrpfmtState &st);
void print_interpolated_string_literal(TSNode node, PrpfmtState &st);
void print__format_spec(TSNode node, PrpfmtState &st);

/******************************************************************************
 * 9. Comments                                                                *
 ******************************************************************************/
void print_comment(TSNode node, PrpfmtState &st, bool is_prechecked);
void print_comment_inline(TSNode node, PrpfmtState &st, bool is_prechecked);
void print_comment_trailing(TSNode node, PrpfmtState &st, bool is_prechecked);
void print_comment_newline(TSNode node, PrpfmtState &st, bool is_prechecked);

/******************************************************************************
 * 10. Special Statements & Attributes                                        *
 ******************************************************************************/
void print_import_statement(TSNode node, PrpfmtState &st);
void print_impl_statement(TSNode node, PrpfmtState &st);
void print_test_statement(TSNode node, PrpfmtState &st);
void print_formal_statement(TSNode node, PrpfmtState &st);
void print_attribute_list(TSNode node, PrpfmtState &st);
void print__semicolon(TSNode node, PrpfmtState &st, SpacingConfig spacing);
void print_timing_slot(TSNode node, PrpfmtState &st);

/******************************************************************************
 * 11. Utilities                                                              *
 ******************************************************************************/
bool has_recursive_line_comment(TSNode node, const PrpfmtState &st);
std::string_view get_node_text(TSNode node, std::string_view source_code);
void emit_node_text(TSNode node, PrpfmtState &st);

#endif // PRP_FMT_H
