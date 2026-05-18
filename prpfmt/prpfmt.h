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
  sym_break_statement = 12,
  sym_continue_statement = 13,
  anon_sym_return = 14,
  anon_sym_unique = 15,
  anon_sym_if = 16,
  anon_sym_elif = 17,
  anon_sym_else = 18,
  anon_sym_for = 19,
  anon_sym_COLON_COLON = 20,
  anon_sym_in = 21,
  anon_sym_while = 22,
  anon_sym_loop = 23,
  anon_sym_match = 24,
  anon_sym_and = 25,
  anon_sym_BANGand = 26,
  anon_sym_or = 27,
  anon_sym_BANGor = 28,
  anon_sym_AMP = 29,
  anon_sym_CARET = 30,
  anon_sym_PIPE = 31,
  anon_sym_TILDE_AMP = 32,
  anon_sym_TILDE_CARET = 33,
  anon_sym_TILDE_PIPE = 34,
  anon_sym_LT = 35,
  anon_sym_LT_EQ = 36,
  anon_sym_GT = 37,
  anon_sym_GT_EQ = 38,
  anon_sym_EQ_EQ = 39,
  anon_sym_BANG_EQ = 40,
  anon_sym_has = 41,
  anon_sym_BANGhas = 42,
  anon_sym_case = 43,
  anon_sym_BANGcase = 44,
  anon_sym_BANGin = 45,
  anon_sym_equals = 46,
  anon_sym_BANGequals = 47,
  anon_sym_does = 48,
  anon_sym_BANGdoes = 49,
  anon_sym_is = 50,
  anon_sym_BANGis = 51,
  anon_sym_test = 52,
  anon_sym_type = 53,
  anon_sym_EQ = 54,
  anon_sym_comb = 55,
  anon_sym_mod = 56,
  anon_sym_pipe = 57,
  anon_sym_impl = 58,
  anon_sym_always = 59,
  anon_sym_assert = 60,
  anon_sym_cassert = 61,
  anon_sym_COMMA = 62,
  anon_sym_LBRACK = 63,
  anon_sym_RBRACK = 64,
  anon_sym_comptime = 65,
  anon_sym_const = 66,
  anon_sym_mut = 67,
  anon_sym_reg = 68,
  anon_sym_await = 69,
  anon_sym_DASH_GT = 70,
  anon_sym_enum = 71,
  anon_sym_variant = 72,
  anon_sym_spawn = 73,
  anon_sym_ref = 74,
  anon_sym_DOT_DOT_DOT = 75,
  anon_sym_AT = 76,
  anon_sym_POUND = 77,
  anon_sym_PLUS = 78,
  anon_sym_sext = 79,
  anon_sym_zext = 80,
  anon_sym_COLON = 81,
  anon_sym_BANG = 82,
  anon_sym_not = 83,
  anon_sym_TILDE = 84,
  anon_sym_DASH = 85,
  anon_sym_QMARK = 86,
  anon_sym_STAR = 87,
  anon_sym_SLASH = 88,
  anon_sym_PERCENT = 89,
  anon_sym_PLUS_PLUS = 90,
  anon_sym_LT_LT = 91,
  anon_sym_GT_GT = 92,
  anon_sym_BANG_AMP = 93,
  anon_sym_BANG_PIPE = 94,
  anon_sym_BANG_CARET = 95,
  anon_sym_DOT_DOT_EQ = 96,
  anon_sym_DOT_DOT_LT = 97,
  anon_sym_DOT_DOT_PLUS = 98,
  anon_sym_step = 99,
  anon_sym_implies = 100,
  anon_sym_BANGimplies = 101,
  anon_sym_proc = 102,
  anon_sym_PLUS_EQ = 103,
  anon_sym_DASH_EQ = 104,
  anon_sym_STAR_EQ = 105,
  anon_sym_SLASH_EQ = 106,
  anon_sym_PIPE_EQ = 107,
  anon_sym_AMP_EQ = 108,
  anon_sym_CARET_EQ = 109,
  anon_sym_LT_LT_EQ = 110,
  anon_sym_GT_GT_EQ = 111,
  anon_sym_PLUS_PLUS_EQ = 112,
  anon_sym_or_EQ = 113,
  anon_sym_and_EQ = 114,
  anon_sym_DOT_DOT = 115,
  anon_sym_uint = 116,
  anon_sym_unsigned = 117,
  aux_sym_uint_type_token1 = 118,
  anon_sym_int = 119,
  anon_sym_integer = 120,
  aux_sym_sint_type_token1 = 121,
  sym_bool_type = 122,
  sym_string_type = 123,
  sym__simple_number = 124,
  sym__scaled_number = 125,
  sym__hex_number = 126,
  sym__decimal_number = 127,
  sym__octal_number = 128,
  sym__binary_number = 129,
  sym__typed_number = 130,
  sym_bool_literal = 131,
  sym_string_literal = 132,
  anon_sym_DQUOTE = 133,
  aux_sym_interpolated_string_literal_token1 = 134,
  aux_sym_interpolated_string_literal_token2 = 135,
  sym__format_spec = 136,
  sym__space = 137,
  sym_comment = 138,
  anon_sym_when = 139,
  anon_sym_unless = 140,
  sym__automatic_semicolon = 141,
  sym_description = 142,
  sym__statement = 143,
  sym_scope_statement = 144,
  sym_declaration_statement = 145,
  sym_import_statement = 146,
  sym_control_statement = 147,
  sym_return_statement = 148,
  sym_stmt_list = 149,
  sym_if_expression = 150,
  sym_for_statement = 151,
  sym_while_statement = 152,
  sym_loop_statement = 153,
  sym_match_expression = 154,
  sym_test_statement = 155,
  sym_type_statement = 156,
  sym_impl_statement = 157,
  sym_assert_statement = 158,
  sym_expression_list = 159,
  sym_function_call_expression = 160,
  sym_function_call_statement = 161,
  sym_tuple = 162,
  sym_tuple_sq = 163,
  sym__tuple_list = 164,
  sym__tuple_item = 165,
  sym_assignment = 166,
  sym_lvalue_item = 167,
  sym_lvalue_list = 168,
  sym_var_or_let_or_reg = 169,
  sym_await_decl = 170,
  sym_attribute_list = 171,
  sym_function_definition_decl = 172,
  sym_enum_definition = 173,
  sym_enum_assignment = 174,
  sym_spawn_statement = 175,
  sym_ref_identifier = 176,
  sym_arg_list = 177,
  sym__complex_identifier = 178,
  sym_timed_identifier = 179,
  sym__timing_sequence = 180,
  sym_typed_identifier = 181,
  sym_typed_identifier_list = 182,
  sym__expression = 183,
  sym__expression_with_comprehension = 184,
  sym_for_comprehension = 185,
  sym_member_selection = 186,
  sym_bit_selection = 187,
  sym_attribute_read = 188,
  sym_type_specification = 189,
  sym_unary_expression = 190,
  sym_optional_expression = 191,
  sym__binary_times = 192,
  sym_binary_times_op = 193,
  sym__binary_other = 194,
  sym_binary_other_op = 195,
  sym__binary_compare = 196,
  sym_binary_compare_op = 197,
  sym__binary_logical = 198,
  sym_binary_logical_op = 199,
  sym__pri1_operand = 200,
  sym__pri2_operand = 201,
  sym__pri3_operand = 202,
  sym__pri4_operand = 203,
  sym_dot_expression = 204,
  sym__restricted_expression = 205,
  sym_lambda = 206,
  sym_assignment_operator = 207,
  sym_select = 208,
  sym_selection_range = 209,
  sym_type_cast = 210,
  sym__type = 211,
  sym_expression_type = 212,
  sym_dot_expression_type = 213,
  sym_function_call_type = 214,
  sym_array_type = 215,
  sym__primitive_type = 216,
  sym_uint_type = 217,
  sym_sint_type = 218,
  sym_constant = 219,
  sym_integer_literal = 220,
  sym_unknown_literal = 221,
  sym__string_literal = 222,
  sym_interpolated_string_literal = 223,
  sym__semicolon = 224,
  sym_when_unless_cond = 225,
  aux_sym_description_repeat1 = 226,
  aux_sym_description_repeat2 = 227,
  aux_sym_scope_statement_repeat1 = 228,
  aux_sym_import_statement_repeat1 = 229,
  aux_sym_stmt_list_repeat1 = 230,
  aux_sym_if_expression_repeat1 = 231,
  aux_sym_match_expression_repeat1 = 232,
  aux_sym_expression_list_repeat1 = 233,
  aux_sym__tuple_list_repeat1 = 234,
  aux_sym__tuple_list_repeat2 = 235,
  aux_sym_lvalue_list_repeat1 = 236,
  aux_sym_attribute_list_repeat1 = 237,
  aux_sym_arg_list_repeat1 = 238,
  aux_sym_typed_identifier_list_repeat1 = 239,
  aux_sym_member_selection_repeat1 = 240,
  aux_sym_attribute_read_repeat1 = 241,
  aux_sym__binary_times_repeat1 = 242,
  aux_sym__binary_other_repeat1 = 243,
  aux_sym__binary_compare_repeat1 = 244,
  aux_sym__binary_logical_repeat1 = 245,
  aux_sym_dot_expression_repeat1 = 246,
  aux_sym_dot_expression_type_repeat1 = 247,
  aux_sym_interpolated_string_literal_repeat1 = 248,
  alias_sym_assign = 249,
  alias_sym_op_add = 250,
  alias_sym_op_bit_and = 251,
  alias_sym_op_bit_or = 252,
  alias_sym_op_bit_xor = 253,
  alias_sym_op_case = 254,
  alias_sym_op_does = 255,
  alias_sym_op_eq = 256,
  alias_sym_op_equals = 257,
  alias_sym_op_ge = 258,
  alias_sym_op_gt = 259,
  alias_sym_op_has = 260,
  alias_sym_op_in = 261,
  alias_sym_op_is = 262,
  alias_sym_op_le = 263,
  alias_sym_op_log_and = 264,
  alias_sym_op_log_nand = 265,
  alias_sym_op_log_nor = 266,
  alias_sym_op_log_or = 267,
  alias_sym_op_lt = 268,
  alias_sym_op_ne = 269,
  alias_sym_op_not_case = 270,
  alias_sym_op_not_does = 271,
  alias_sym_op_not_equals = 272,
  alias_sym_op_not_has = 273,
  alias_sym_op_not_in = 274,
  alias_sym_op_not_is = 275,
  alias_sym_op_range_exclusive = 276,
  alias_sym_op_range_inclusive = 277,
  alias_sym_op_spread = 278,
  alias_sym_op_sub = 279,
  alias_sym_open_all = 280,
  alias_sym_reduction_and = 281,
  alias_sym_reduction_or = 282,
  alias_sym_reduction_xor = 283,
  alias_sym_reg_decl = 284,
} PyropeSymbol;

/******************************************************************************
 * 1. Entry & High-Level Dispatch                                             *
 ******************************************************************************/
void print_description(TSTree *tree, PrpfmtState *st);
bool print__statement(TSNode node, PrpfmtState *st, TSNode prev_node, bool is_inline);
void check_format_directives(const char *node_text, PrpfmtState *st);

/******************************************************************************
 * 2. Structural (Scopes, Lists, Tuples)                                      *
 ******************************************************************************/
void print_scope_statement(TSNode node, PrpfmtState *st, bool is_inline);
void print_stmt_list(TSNode node, PrpfmtState *st);
void print_tuple(TSNode node, PrpfmtState *st);
void print_tuple_sq(TSNode node, PrpfmtState *st);
void print__tuple_list(TSNode node, PrpfmtState *st);
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
void print_declaration_statement(TSNode node, PrpfmtState *st);
void print_await_decl(TSNode node, PrpfmtState *st);
void print_enum_assignment(TSNode node, PrpfmtState *st);
void print_enum_definition(TSNode node, PrpfmtState *st);
void print_assignment_operator(TSNode node, PrpfmtState *st, SpacingConfig spacing);
void print_spawn_statement(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 5. Functions & Parameters                                                  *
 ******************************************************************************/
void print_lambda(TSNode node, PrpfmtState *st);
void print_function_definition_decl(TSNode node, PrpfmtState *st);
void print_arg_list(TSNode node, PrpfmtState *st);
void print_function_call_statement(TSNode node, PrpfmtState *st);
void print_function_call_expression(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 6. Expressions & Selection                                                 *
 ******************************************************************************/
/* Note: print__pri1_operand to print__pri4_operand were omitted as redundant proxies. */
void print__expression(TSNode node, PrpfmtState *st, bool is_inline);
void print__expression_with_comprehension(TSNode node, PrpfmtState *st, bool is_inline);
void print__restricted_expression(TSNode node, PrpfmtState *st);
void print_expression_item(TSNode node, PrpfmtState *st);
void print__binary_times(TSNode node, PrpfmtState *st);
void print__binary_other(TSNode node, PrpfmtState *st);
void print__binary_compare(TSNode node, PrpfmtState *st);
void print__binary_logical(TSNode node, PrpfmtState *st);
void print_unary_expression(TSNode node, PrpfmtState *st);
void print_dot_expression(TSNode node, PrpfmtState *st);
void print_optional_expression(TSNode node, PrpfmtState *st);
void print_type_specification(TSNode node, PrpfmtState *st);
void print_type_cast(TSNode node, PrpfmtState *st);
void print_expression_list(TSNode node, PrpfmtState *st);
void print_for_comprehension(TSNode node, PrpfmtState *st);
void print_member_selection(TSNode node, PrpfmtState *st);
void print_bit_selection(TSNode node, PrpfmtState *st);
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
void print__primitive_type(TSNode node, PrpfmtState *st);
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
void print_comment(TSNode node, PrpfmtState *st);
void print_comment_inline(TSNode node, PrpfmtState *st);
void print_comment_newline(TSNode node, PrpfmtState *st);
void print_comment_trailing(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 10. Special Statements & Attributes                                        *
 ******************************************************************************/
void print_import_statement(TSNode node, PrpfmtState *st);
void print_type_statement(TSNode node, PrpfmtState *st);
void print_impl_statement(TSNode node, PrpfmtState *st);
void print_test_statement(TSNode node, PrpfmtState *st);
void print_assert_statement(TSNode node, PrpfmtState *st);
void print_attributes(TSNode node, PrpfmtState *st);
void print_attribute_list(TSNode node, PrpfmtState *st);
void print__semicolon(TSNode node, PrpfmtState *st, SpacingConfig spacing);
void print__timing_sequence(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 11. Utilities                                                              *
 ******************************************************************************/
void preserve_whitespace(TSNode prev, TSNode curr, PrpfmtState *st);
bool has_recursive_comment(TSNode node);
char *get_node_text(TSNode node, const char *source_code);
void emit_node_text(TSNode node, PrpfmtState *st);

#endif // PRP_FMT_H
