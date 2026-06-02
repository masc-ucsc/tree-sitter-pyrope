#ifndef PRP_FMT_H
#define PRP_FMT_H

#include <stdio.h>
#include <tree_sitter/api.h>
#include "ir.h"

/* 
 * SpacingConfig controls the emission of spaces around tokens (primarily operators 
 * and punctuation) during the initial IR hooking phase. 
 * It is implemented as a bitmask so flags can be combined.
 */
typedef enum {
  SPACE_NONE   = 0,
  SPACE_BEFORE = 1 << 0,
  SPACE_AFTER  = 1 << 1,
  SPACE_BOTH   = 3
} SpacingConfig;

typedef struct PrpfmtState {
  const char *source_code; // Input source for text extraction via get_node_text
  FILE *outfile;           // Output target (stdout or file)
  int indent_size;         // Spaces per level (default: 4)
  int max_width;           // Maximum line width (default: 80)
  bool in_assert;          // True if currently printing an assertion (for alignment)
  bool allow_inline;       // Contextual permission for blocks to stay on one line
  int nesting_level;       // Current block depth (0 = top level)
  bool fmt_on;             // Toggle for 'prpfmt on/off' directives
  bool inline_exp;         // If true, suppresses newlines for nested expressions
  TokenBuffer buffer;      // Buffer for IR
} PrpfmtState;

/* 
 * SYMBOL ENUM
 * These constants must match the symbol IDs generated in 
 * tree-sitter-pyrope/src/parser.c.
 */
typedef enum {
  sym_identifier = 1,
  anon_sym_SEMI = 2,
  anon_sym_wrap = 3,
  anon_sym_sat = 4,
  anon_sym_LBRACE = 5,
  anon_sym_RBRACE = 6,
  anon_sym_LPAREN = 7,
  anon_sym_RPAREN = 8,
  anon_sym_import = 9,
  anon_sym_DOT = 10,
  anon_sym_as = 11,
  anon_sym_break = 12,
  anon_sym_continue = 13,
  anon_sym_return = 14,
  anon_sym_unique = 15,
  anon_sym_if = 16,
  anon_sym_elif = 17,
  anon_sym_else = 18,
  anon_sym_COLON_COLON = 19,
  anon_sym_for = 20,
  anon_sym_in = 21,
  anon_sym_while = 22,
  anon_sym_loop = 23,
  anon_sym_match = 24,
  anon_sym_and = 25,
  anon_sym_or = 26,
  anon_sym_AMP = 27,
  anon_sym_CARET = 28,
  anon_sym_PIPE = 29,
  anon_sym_TILDE_AMP = 30,
  anon_sym_TILDE_CARET = 31,
  anon_sym_TILDE_PIPE = 32,
  anon_sym_LT = 33,
  anon_sym_LT_EQ = 34,
  anon_sym_GT = 35,
  anon_sym_GT_EQ = 36,
  anon_sym_EQ_EQ = 37,
  anon_sym_BANG_EQ = 38,
  anon_sym_has = 39,
  anon_sym_case = 40,
  anon_sym_equals = 41,
  anon_sym_does = 42,
  anon_sym_is = 43,
  anon_sym_test = 44,
  anon_sym_type = 45,
  anon_sym_EQ = 46,
  anon_sym_comb = 47,
  anon_sym_mod = 48,
  anon_sym_impl = 49,
  anon_sym_COMMA = 50,
  anon_sym_LBRACK = 51,
  anon_sym_RBRACK = 52,
  anon_sym_comptime = 53,
  anon_sym_const = 54,
  anon_sym_mut = 55,
  anon_sym_reg = 56,
  anon_sym_stage = 57,
  anon_sym_DASH_GT = 58,
  anon_sym_enum = 59,
  anon_sym_spawn = 60,
  anon_sym_ref = 61,
  anon_sym_DOT_DOT_DOT = 62,
  anon_sym_AT = 63,
  anon_sym_POUND = 64,
  anon_sym_PLUS = 65,
  anon_sym_sext = 66,
  anon_sym_zext = 67,
  anon_sym_COLON = 68,
  anon_sym_BANG = 69,
  anon_sym_not = 70,
  anon_sym_TILDE = 71,
  anon_sym_DASH = 72,
  anon_sym_STAR = 73,
  anon_sym_SLASH = 74,
  anon_sym_PERCENT = 75,
  anon_sym_PLUS_PLUS = 76,
  anon_sym_LT_LT = 77,
  anon_sym_GT_GT = 78,
  anon_sym_BANG_AMP = 79,
  anon_sym_BANG_PIPE = 80,
  anon_sym_BANG_CARET = 81,
  anon_sym_DOT_DOT_EQ = 82,
  anon_sym_DOT_DOT_LT = 83,
  anon_sym_DOT_DOT_PLUS = 84,
  anon_sym_step = 85,
  anon_sym_implies = 86,
  anon_sym_pipe = 87,
  anon_sym_PLUS_EQ = 88,
  anon_sym_DASH_EQ = 89,
  anon_sym_STAR_EQ = 90,
  anon_sym_SLASH_EQ = 91,
  anon_sym_PIPE_EQ = 92,
  anon_sym_AMP_EQ = 93,
  anon_sym_CARET_EQ = 94,
  anon_sym_LT_LT_EQ = 95,
  anon_sym_GT_GT_EQ = 96,
  anon_sym_PLUS_PLUS_EQ = 97,
  anon_sym_or_EQ = 98,
  anon_sym_and_EQ = 99,
  anon_sym_DOT_DOT = 100,
  anon_sym_uint = 101,
  anon_sym_unsigned = 102,
  aux_sym_uint_type_token1 = 103,
  anon_sym_int = 104,
  anon_sym_integer = 105,
  aux_sym_sint_type_token1 = 106,
  sym_bool_type = 107,
  sym_string_type = 108,
  sym__simple_number = 109,
  sym__scaled_number = 110,
  sym__hex_number = 111,
  sym__decimal_number = 112,
  sym__octal_number = 113,
  sym__binary_number = 114,
  sym__typed_number = 115,
  sym_bool_literal = 116,
  sym_unknown_literal = 117,
  sym_string_literal = 118,
  anon_sym_DQUOTE = 119,
  aux_sym_interpolated_string_literal_token1 = 120,
  aux_sym_interpolated_string_literal_token2 = 121,
  sym__format_spec = 122,
  sym__space = 123,
  sym_comment = 124,
  anon_sym_when = 125,
  anon_sym_unless = 126,
  sym__automatic_semicolon = 127,
  sym_description = 128,
  sym__statement = 129,
  sym_scope_statement = 130,
  sym_declaration_statement = 131,
  sym_import_statement = 132,
  sym_control_statement = 133,
  sym_break_statement = 134,
  sym_continue_statement = 135,
  sym_return_statement = 136,
  sym_stmt_list = 137,
  sym__if_branch = 138,
  sym_if_expression = 139,
  sym__attr_prefix = 140,
  sym__init_clause = 141,
  sym_for_statement = 142,
  sym_while_statement = 143,
  sym_loop_statement = 144,
  sym_match_expression = 145,
  sym_test_statement = 146,
  sym_type_statement = 147,
  sym_impl_statement = 148,
  sym_expression_list = 149,
  sym_function_call_expression = 150,
  sym_tuple = 151,
  sym_tuple_sq = 152,
  sym_attribute_sq = 153,
  sym__attribute_item = 154,
  sym_attribute_assignment = 155,
  sym__tuple_list = 156,
  sym__tuple_item = 157,
  sym_assignment = 158,
  sym_lvalue_item = 159,
  sym_named_lvalue = 160,
  sym_lvalue_list = 161,
  sym_var_or_let_or_reg = 162,
  sym_stage_decl = 163,
  sym_attribute_list = 164,
  sym_function_definition_decl = 165,
  sym_enum_definition = 166,
  sym_enum_assignment = 167,
  sym_spawn_statement = 168,
  sym_ref_identifier = 169,
  sym_arg_list = 170,
  sym__complex_identifier = 171,
  sym_timed_identifier = 172,
  sym__timing_sequence = 173,
  sym_timing_slot = 174,
  sym_typed_identifier = 175,
  sym_typed_identifier_list = 176,
  sym__expression = 177,
  sym_member_selection = 178,
  sym_bit_selection = 179,
  sym_attribute_read = 180,
  sym_type_specification = 181,
  sym_unary_expression = 182,
  sym__binary_times = 183,
  sym_binary_times_op = 184,
  sym__binary_other = 185,
  sym_binary_other_op = 186,
  sym__binary_compare = 187,
  sym_binary_compare_op = 188,
  sym__binary_logical = 189,
  sym_binary_logical_op = 190,
  sym__pri1_operand = 191,
  sym__pri2_operand = 192,
  sym__pri3_operand = 193,
  sym__pri4_operand = 194,
  sym_dot_expression = 195,
  sym__restricted_expression = 196,
  sym_paren_group = 197,
  sym__suffix_head = 198,
  sym_lambda = 199,
  sym_pipe_lambda = 200,
  sym_assignment_operator = 201,
  sym_select = 202,
  sym_selection_range = 203,
  sym_type_cast = 204,
  sym__type = 205,
  sym_expression_type = 206,
  sym_dot_expression_type = 207,
  sym_function_call_type = 208,
  sym_array_type = 209,
  sym_array_length = 210,
  sym__primitive_type = 211,
  sym_uint_type = 212,
  sym_sint_type = 213,
  sym_constant = 214,
  sym_integer_literal = 215,
  sym__string_literal = 216,
  sym_interpolated_string_literal = 217,
  sym__semicolon = 218,
  sym_when_unless_cond = 219,
  aux_sym_description_repeat1 = 220,
  aux_sym_description_repeat2 = 221,
  aux_sym_scope_statement_repeat1 = 222,
  aux_sym_import_statement_repeat1 = 223,
  aux_sym_stmt_list_repeat1 = 224,
  aux_sym_if_expression_repeat1 = 225,
  aux_sym_match_expression_repeat1 = 226,
  aux_sym_expression_list_repeat1 = 227,
  aux_sym_attribute_sq_repeat1 = 228,
  aux_sym_attribute_sq_repeat2 = 229,
  aux_sym__tuple_list_repeat1 = 230,
  aux_sym_lvalue_list_repeat1 = 231,
  aux_sym_arg_list_repeat1 = 232,
  aux_sym_typed_identifier_list_repeat1 = 233,
  aux_sym_member_selection_repeat1 = 234,
  aux_sym_attribute_read_repeat1 = 235,
  aux_sym__binary_times_repeat1 = 236,
  aux_sym__binary_other_repeat1 = 237,
  aux_sym__binary_compare_repeat1 = 238,
  aux_sym__binary_logical_repeat1 = 239,
  aux_sym_dot_expression_repeat1 = 240,
  aux_sym_dot_expression_type_repeat1 = 241,
  aux_sym_interpolated_string_literal_repeat1 = 242,
  alias_sym_assign = 243,
  alias_sym_op_add = 244,
  alias_sym_op_bit_and = 245,
  alias_sym_op_bit_or = 246,
  alias_sym_op_bit_xor = 247,
  alias_sym_op_case = 248,
  alias_sym_op_does = 249,
  alias_sym_op_eq = 250,
  alias_sym_op_equals = 251,
  alias_sym_op_ge = 252,
  alias_sym_op_gt = 253,
  alias_sym_op_has = 254,
  alias_sym_op_in = 255,
  alias_sym_op_is = 256,
  alias_sym_op_le = 257,
  alias_sym_op_log_and = 258,
  alias_sym_op_log_or = 259,
  alias_sym_op_lt = 260,
  alias_sym_op_ne = 261,
  alias_sym_op_range_exclusive = 262,
  alias_sym_op_range_inclusive = 263,
  alias_sym_op_spread = 264,
  alias_sym_op_sub = 265,
  alias_sym_open_all = 266,
  alias_sym_reduction_and = 267,
  alias_sym_reduction_or = 268,
  alias_sym_reduction_xor = 269,
  alias_sym_reg_decl = 270,
} PyropeSymbol;

/******************************************************************************
 * 1. Entry & High-Level Dispatch                                             *
 ******************************************************************************/
void print_description(TSTree *tree, PrpfmtState *st);
bool print__statement(TSNode node, PrpfmtState *st, TSNode prev_node, bool is_inline);

/******************************************************************************
 * 2. Structural (Scopes, Lists, Tuples)                                      *
 ******************************************************************************/
void print_scope_statement(TSNode node, PrpfmtState *st, bool is_inline);
void print_stmt_list(TSNode node, PrpfmtState *st);
void print_tuple(TSNode node, PrpfmtState *st);
void print_assertion_args(TSNode node, PrpfmtState *st);
void print_tuple_sq(TSNode node, PrpfmtState *st);
void print_attribute_sq(TSNode node, PrpfmtState *st);
void print_attribute_assignment(TSNode node, PrpfmtState *st);
void print__tuple_list(TSNode node, PrpfmtState *st, SpacingConfig spacing);
void print__tuple_item(TSNode node, PrpfmtState *st, SpacingConfig spacing);

/******************************************************************************
 * 3. Control Flow                                                            *
 ******************************************************************************/
void print_if_expression(TSNode node, PrpfmtState *st, bool is_inline);
void print_match_expression(TSNode node, PrpfmtState *st);
void print_for_statement(TSNode node, PrpfmtState *st);
void print_while_statement(TSNode node, PrpfmtState *st);
void print_when_unless_cond(TSNode node, PrpfmtState *st);
void print_loop_statement(TSNode node, PrpfmtState *st);
void print_control_statement(TSNode node, PrpfmtState *st);
void print_return_statement(TSNode node, PrpfmtState *st);
void print_break_statement(TSNode node, PrpfmtState *st);
void print_continue_statement(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 4. Assignments & Declarations                                              *
 ******************************************************************************/
void print_assignment(TSNode node, PrpfmtState *st, SpacingConfig spacing);
void print_lvalue_item(TSNode node, PrpfmtState *st);
void print_lvalue_list(TSNode node, PrpfmtState *st);
void print_named_lvalue(TSNode node, PrpfmtState *st);
void print_declaration_statement(TSNode node, PrpfmtState *st);
void print_stage_decl(TSNode node, PrpfmtState *st);
void print_enum_assignment(TSNode node, PrpfmtState *st);
void print_enum_definition(TSNode node, PrpfmtState *st);
void print_assignment_operator(TSNode node, PrpfmtState *st, SpacingConfig spacing);
void print_spawn_statement(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 5. Functions & Parameters                                                  *
 ******************************************************************************/
void print_lambda(TSNode node, PrpfmtState *st);
void print_pipe_lambda(TSNode node, PrpfmtState *st);
void print_function_definition_decl(TSNode node, PrpfmtState *st);
void print_arg_list(TSNode node, PrpfmtState *st);
void print_function_call_expression(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 6. Expressions & Selection                                                 *
 ******************************************************************************/
void print__expression(TSNode node, PrpfmtState *st, bool is_inline);
void print__restricted_expression(TSNode node, PrpfmtState *st);
void print_expression_item(TSNode node, PrpfmtState *st);
void print__binary_times(TSNode node, PrpfmtState *st);
void print__binary_other(TSNode node, PrpfmtState *st);
void print__binary_compare(TSNode node, PrpfmtState *st);
void print__binary_logical(TSNode node, PrpfmtState *st);
void print_unary_expression(TSNode node, PrpfmtState *st);
void print_dot_expression(TSNode node, PrpfmtState *st);
void print_type_specification(TSNode node, PrpfmtState *st);
void print_type_cast(TSNode node, PrpfmtState *st);
void print_expression_list(TSNode node, PrpfmtState *st);
void print_member_selection(TSNode node, PrpfmtState *st);
void print_paren_group(TSNode node, PrpfmtState *st);
void print_bit_selection(TSNode node, PrpfmtState *st);
void print__suffix_head(TSNode node, PrpfmtState *st);

void print_attribute_read(TSNode node, PrpfmtState *st);
void print_select(TSNode node, PrpfmtState *st);
void print_selection_range(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 7. Types & Identifiers                                                     *
 ******************************************************************************/
void print__type(TSNode node, PrpfmtState *st);
void print_expression_type(TSNode node, PrpfmtState *st);
void print_dot_expression_type(TSNode node, PrpfmtState *st);
void print_function_call_type(TSNode node, PrpfmtState *st);
void print_array_type(TSNode node, PrpfmtState *st);
void print_array_length(TSNode node, PrpfmtState *st);
void print__primitive_type(TSNode node, PrpfmtState *st);
void print_type_statement(TSNode node, PrpfmtState *st);
void print_typed_identifier(TSNode node, PrpfmtState *st);
void print_typed_identifier_list(TSNode node, PrpfmtState *st);
void print_ref_identifier(TSNode node, PrpfmtState *st);
void print__complex_identifier(TSNode node, PrpfmtState *st);
void print_timed_identifier(TSNode node, PrpfmtState *st);
void print_var_or_let_or_reg(TSNode node, PrpfmtState *st);
void print_identifier(TSNode node, PrpfmtState *st);
void print_uint_type(TSNode node, PrpfmtState *st);
void print_sint_type(TSNode node, PrpfmtState *st);
void print_bool_type(TSNode node, PrpfmtState *st);
void print_string_type(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 8. Literals & Numbers                                                      *
 ******************************************************************************/
void print_constant(TSNode node, PrpfmtState *st);
void print_integer_literal(TSNode node, PrpfmtState *st);
void print__simple_number(TSNode node, PrpfmtState *st);
void print__scaled_number(TSNode node, PrpfmtState *st);
void print__hex_number(TSNode node, PrpfmtState *st);
void print__decimal_number(TSNode node, PrpfmtState *st);
void print__octal_number(TSNode node, PrpfmtState *st);
void print__binary_number(TSNode node, PrpfmtState *st);
void print__typed_number(TSNode node, PrpfmtState *st);
void print_bool_literal(TSNode node, PrpfmtState *st);
void print_unknown_literal(TSNode node, PrpfmtState *st);
void print__string_literal(TSNode node, PrpfmtState *st);
void print_string_literal(TSNode node, PrpfmtState *st);
void print_interpolated_string_literal(TSNode node, PrpfmtState *st);
void print__format_spec(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 9. Comments                                                                *
 ******************************************************************************/
void print_comment(TSNode node, PrpfmtState *st, bool is_prechecked);
void print_comment_inline(TSNode node, PrpfmtState *st, bool is_prechecked);
void print_comment_trailing(TSNode node, PrpfmtState *st, bool is_prechecked);
void print_comment_newline(TSNode node, PrpfmtState *st, bool is_prechecked);

/******************************************************************************
 * 10. Special Statements & Attributes                                        *
 ******************************************************************************/
void print_import_statement(TSNode node, PrpfmtState *st);
void print_impl_statement(TSNode node, PrpfmtState *st);
void print_test_statement(TSNode node, PrpfmtState *st);
void print_attribute_list(TSNode node, PrpfmtState *st);
void print__semicolon(TSNode node, PrpfmtState *st, SpacingConfig spacing);
void print_timing_slot(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 11. Utilities                                                              *
 ******************************************************************************/
bool has_recursive_line_comment(TSNode node, PrpfmtState *st);
char *get_node_text(TSNode node, const char *source_code);
void emit_node_text(TSNode node, PrpfmtState *st);

#endif // PRP_FMT_H
