#ifndef PRP_FMT_H
#define PRP_FMT_H

#include <cstdio>
#include <string_view>
#include <tree_sitter/api.h>
#include "ir.h"

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

struct PrpfmtState {
  std::string_view source_code; // Input source for text extraction via get_node_text
  FILE *outfile;           // Output target (stdout or file)
  int indent_size;         // Spaces per level (default: 4)
  int max_width;           // Maximum line width (default: 80)
  bool in_assert;          // True if currently printing an assertion (for alignment)
  bool allow_inline;       // Contextual permission for blocks to stay on one line
  int nesting_level;       // Current block depth (0 = top level)
  bool fmt_on;             // Toggle for 'prpfmt on/off' directives
  bool inline_exp;         // If true, suppresses newlines for nested expressions
  TokenBuffer buffer;      // Buffer for IR
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
bool print__statement(TSNode node, PrpfmtState &st, TSNode prev_node, bool is_inline);

/******************************************************************************
 * 2. Structural (Scopes, Lists, Tuples)                                      *
 ******************************************************************************/
void print_scope_statement(TSNode node, PrpfmtState &st, bool is_inline);
void print_stmt_list(TSNode node, PrpfmtState &st);
void print_tuple(TSNode node, PrpfmtState &st);
void print_assertion_args(TSNode node, PrpfmtState &st);
void print_tuple_sq(TSNode node, PrpfmtState &st);
void print_attribute_sq(TSNode node, PrpfmtState &st);
void print_attribute_assignment(TSNode node, PrpfmtState &st);
void print__tuple_list(TSNode node, PrpfmtState &st, SpacingConfig spacing);
void print__tuple_item(TSNode node, PrpfmtState &st, SpacingConfig spacing);

/******************************************************************************
 * 3. Control Flow                                                            *
 ******************************************************************************/
void print_if_expression(TSNode node, PrpfmtState &st, bool is_inline);
void print_match_expression(TSNode node, PrpfmtState &st);
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
void print_spawn_statement(TSNode node, PrpfmtState &st);

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
void print_expression_list(TSNode node, PrpfmtState &st);
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
