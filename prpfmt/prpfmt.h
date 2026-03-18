#ifndef PRP_FMT_H
#define PRP_FMT_H

#include <stdio.h>
#include <tree_sitter/api.h>

typedef struct {
  const char *source_code; // Input source for text extraction via get_node_text
  FILE *outfile;           // Output target (stdout or file)
  int indent_level;        // Nesting depth for indentation
  int indent_size;         // Spaces per level (default: 4)
  bool fmt_on;             // Toggle for 'prpfmt on/off' directives
  bool inline_exp;         // If true, suppresses newlines for nested expressions
} PrpfmtState;

/* 
 * SYMBOL ENUM
 * These constants must match the symbol IDs generated in 
 * tree-sitter-pyrope/src/parser.c. If the grammar is re-generated, 
 * these values may need synchronization.
 */
enum {
  sym_identifier = 1,
  anon_sym_SEMI = 2,
  anon_sym_LBRACE = 3,
  anon_sym_RBRACE = 4,
  anon_sym_LPAREN = 5,
  anon_sym_RPAREN = 6,
  anon_sym_import = 7,
  anon_sym_as = 8,
  anon_sym_DOT = 9,
  anon_sym_continue = 10,
  anon_sym_break = 11,
  anon_sym_return = 12,
  anon_sym_unique = 13,
  anon_sym_if = 14,
  anon_sym_elif = 15,
  anon_sym_else = 16,
  anon_sym_for = 17,
  anon_sym_COLON_COLON = 18,
  anon_sym_in = 19,
  anon_sym_while = 20,
  anon_sym_loop = 21,
  anon_sym_match = 22,
  anon_sym_and = 23,
  anon_sym_BANGand = 24,
  anon_sym_or = 25,
  anon_sym_BANGor = 26,
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
  anon_sym_BANGhas = 40,
  anon_sym_case = 41,
  anon_sym_BANGcase = 42,
  anon_sym_BANGin = 43,
  anon_sym_equals = 44,
  anon_sym_BANGequals = 45,
  anon_sym_does = 46,
  anon_sym_BANGdoes = 47,
  anon_sym_is = 48,
  anon_sym_BANGis = 49,
  anon_sym_test = 50,
  anon_sym_where = 51,
  anon_sym_type = 52,
  anon_sym_EQ = 53,
  anon_sym_impl = 54,
  anon_sym_COMMA = 55,
  anon_sym_LBRACK = 56,
  anon_sym_RBRACK = 57,
  anon_sym_COLON = 58,
  anon_sym_AT = 59,
  anon_sym_const = 60,
  anon_sym_mut = 61,
  anon_sym_reg = 62,
  anon_sym_DASH_GT = 63,
  anon_sym_enum = 64,
  anon_sym_variant = 65,
  anon_sym_ref = 66,
  anon_sym_DOT_DOT_DOT = 67,
  anon_sym_BANG = 68,
  anon_sym_not = 69,
  anon_sym_TILDE = 70,
  anon_sym_DASH = 71,
  anon_sym_QMARK = 72,
  anon_sym_DOT_DOT_EQ = 73,
  anon_sym_DOT_DOT_LT = 74,
  anon_sym_DOT_DOT_PLUS = 75,
  anon_sym_step = 76,
  anon_sym_implies = 77,
  anon_sym_BANGimplies = 78,
  anon_sym_and_then = 79,
  anon_sym_or_else = 80,
  anon_sym_GT_GT = 81,
  anon_sym_LT_LT = 82,
  anon_sym_BANG_AMP = 83,
  anon_sym_BANG_CARET = 84,
  anon_sym_BANG_PIPE = 85,
  anon_sym_STAR = 86,
  anon_sym_SLASH = 87,
  anon_sym_PERCENT = 88,
  anon_sym_PLUS = 89,
  anon_sym_PIPE_GT = 90,
  anon_sym_PLUS_PLUS = 91,
  sym_assignment_operator = 92,
  anon_sym_DOT_DOT = 93,
  anon_sym_POUND = 94,
  anon_sym_sext = 95,
  anon_sym_zext = 96,
  anon_sym_int = 97,
  anon_sym_integer = 98,
  anon_sym_signed = 99,
  anon_sym_uint = 100,
  anon_sym_unsigned = 101,
  sym_sized_integer_type = 102,
  anon_sym_range = 103,
  sym_string_type = 104,
  sym_boolean_type = 105,
  sym_comb_tok = 106,
  sym_pipe_tok = 107,
  sym_flow_tok = 108,
  sym_delay_tok = 109,
  sym__neg_simple_number = 110,
  sym__neg_scaled_number = 111,
  sym__neg_hex_number = 112,
  sym__neg_decimal_number = 113,
  sym__neg_octal_number = 114,
  sym__neg_binary_number = 115,
  sym__neg_typed_number = 116,
  sym__simple_number = 117,
  sym__scaled_number = 118,
  sym__hex_number = 119,
  sym__decimal_number = 120,
  sym__octal_number = 121,
  sym__binary_number = 122,
  sym__typed_number = 123,
  sym__bool_literal = 124,
  sym__simple_string_literal = 125,
  anon_sym_DQUOTE = 126,
  aux_sym_complex_string_literal_token1 = 127,
  aux_sym_complex_string_literal_token2 = 128,
  sym__space = 129,
  sym_comment = 130,
  anon_sym_when = 131,
  anon_sym_unless = 132,
  sym__automatic_semicolon = 133,
  sym_description = 134,
  sym_statement = 135,
  sym_scope_statement = 136,
  sym_assignment_or_declaration_statement = 137,
  sym_declaration_statement = 138,
  sym_import_statement = 139,
  sym_module_path = 140,
  sym_control_statement = 141,
  sym_stmt_list = 142,
  sym_if_expression = 143,
  sym_for_statement = 144,
  sym_while_statement = 145,
  sym_loop_statement = 146,
  sym_match_expression = 147,
  sym_match_list = 148,
  sym_match_operator = 149,
  sym_expression_statement = 150,
  sym_test_statement = 151,
  sym_type_statement = 152,
  sym_impl_statement = 153,
  sym_expression_list = 154,
  sym_function_call_expression = 155,
  sym_function_call_statement = 156,
  sym_tuple = 157,
  sym_tuple_sq = 158,
  sym_tuple_list = 159,
  sym__tuple_item = 160,
  sym_attributes = 161,
  sym_assignment = 162,
  sym_typed_declaration = 163,
  sym_lvalue_item = 164,
  sym_lvalue_list = 165,
  sym_enum_assignment_statement = 166,
  sym_assignment_delay = 167,
  sym_var_or_let_or_reg = 168,
  sym_attribute_item = 169,
  sym_attribute_item_list = 170,
  sym_attribute_list = 171,
  sym_function_definition_decl = 172,
  sym_enum_definition = 173,
  sym_enum_assignment = 174,
  sym_ref_identifier = 175,
  sym_arg_list = 176,
  sym_arg_item_list = 177,
  sym_arg_item = 178,
  sym_type_or_identifier = 179,
  sym_complex_identifier = 180,
  sym_timed_identifier = 181,
  sym__timing_sequence = 182,
  sym_typed_identifier = 183,
  sym_typed_identifier_list = 184,
  sym__expression = 185,
  sym__expression_with_comprehension = 186,
  sym_for_comprehension = 187,
  sym_selection = 188,
  sym_member_selection = 189,
  sym_bit_selection = 190,
  sym_type_specification = 191,
  sym_unary_expression = 192,
  sym_optional_expression = 193,
  sym_binary_expression = 194,
  sym_dot_expression = 195,
  sym__restricted_expression = 196,
  sym_lambda = 197,
  sym_select = 198,
  sym_select_options = 199,
  sym_member_select = 200,
  sym_bit_select = 201,
  sym_bit_select_type = 202,
  sym_type_cast = 203,
  sym__type = 204,
  sym_expression_type = 205,
  sym_dot_expression_type = 206,
  sym_function_call_type = 207,
  sym_array_type = 208,
  sym_primitive_type = 209,
  sym_unsized_integer_type = 210,
  sym_bounded_integer_type = 211,
  sym_range_type = 212,
  sym_type_type = 213,
  sym_constant = 214,
  sym__number = 215,
  sym__unknown_literal = 216,
  sym__string_literal = 217,
  sym_complex_string_literal = 218,
  sym__semicolon = 219,
  sym_when_unless_cond = 220,
  aux_sym_description_repeat1 = 221,
  aux_sym_description_repeat2 = 222,
  aux_sym_scope_statement_repeat1 = 223,
  aux_sym_module_path_repeat1 = 224,
  aux_sym_stmt_list_repeat1 = 225,
  aux_sym_if_expression_repeat1 = 226,
  aux_sym_match_list_repeat1 = 227,
  aux_sym_expression_list_repeat1 = 228,
  aux_sym_tuple_list_repeat1 = 229,
  aux_sym_tuple_list_repeat2 = 230,
  aux_sym_lvalue_list_repeat1 = 231,
  aux_sym_attribute_item_list_repeat1 = 232,
  aux_sym_arg_item_list_repeat1 = 233,
  aux_sym_typed_identifier_list_repeat1 = 234,
  aux_sym_dot_expression_repeat1 = 235,
  aux_sym_member_select_repeat1 = 236,
  aux_sym_dot_expression_type_repeat1 = 237,
  aux_sym_complex_string_literal_repeat1 = 238,
};

/******************************************************************************
 * 1. Entry & High-Level Dispatch
 * Responsibilities: Initiates tree traversal, manages high-level statement dispatch, 
 * and handles global indentation/formatting state directives.
 ******************************************************************************/
void print_tree(TSTree *tree, PrpfmtState *st);
void print_statement(TSNode node, PrpfmtState *st, bool is_inline);
void print_indent(PrpfmtState *st);
void check_format_directives(const char *node_text, PrpfmtState *st);

/******************************************************************************
 * 2. Structural (Scopes, Lists, Tuples)
 * Responsibilities: Manages code blocks and collection delimiters. Ensures 
 * indentation level changes for nested scopes and proper spacing for lists.
 ******************************************************************************/
void print_scope_statement(TSNode node, PrpfmtState *st, bool is_inline);
void print_stmt_list(TSNode node, PrpfmtState *st);
void print_tuple(TSNode node, PrpfmtState *st);
void print_tuple_sq(TSNode node, PrpfmtState *st);
void print_tuple_list(TSNode node, PrpfmtState *st);
void print__tuple_item(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 3. Control Flow
 * Responsibilities: Formats branching and looping constructs (if, match, for, while). 
 * Manages keyword spacing and conditional clause alignment.
 ******************************************************************************/
void print_if_expression(TSNode node, PrpfmtState *st, bool is_inline);
void print_match_expression(TSNode node, PrpfmtState *st);
void print_match_list(TSNode node, PrpfmtState *st);
void print_match_operator(TSNode node, PrpfmtState *st);
void print_for_statement(TSNode node, PrpfmtState *st);
void print_while_statement(TSNode node, PrpfmtState *st);
void print_loop_statement(TSNode node, PrpfmtState *st);
void print_control_statement(TSNode node, PrpfmtState *st);
void print_when_unless_cond(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 4. Assignments & Declarations
 * Responsibilities: Formats variable declarations and value assignments. Handles 
 * spacing around operators (=, :=) and alignment of L-values/R-values.
 ******************************************************************************/
void print_assignment(TSNode node, PrpfmtState *st, bool spaces);
void print_lvalue_item(TSNode node, PrpfmtState *st);
void print_lvalue_list(TSNode node, PrpfmtState *st);
void print_assignment_or_declaration_statement(TSNode node, PrpfmtState *st);
void print_declaration_statement(TSNode node, PrpfmtState *st);
void print_enum_assignment_statement(TSNode node, PrpfmtState *st);
void print_enum_assignment(TSNode node, PrpfmtState *st);
void print_enum_definition(TSNode node, PrpfmtState *st);
void print_typed_declaration(TSNode node, PrpfmtState *st);
void print_assignment_operator(TSNode node, PrpfmtState *st, bool spaces);
void print_assignment_delay(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 5. Functions & Parameters
 * Responsibilities: Formats function definitions, lambdas, and call sites. 
 * Manages generic parameters, capture lists, and argument list spacing.
 ******************************************************************************/
void print_lambda(TSNode node, PrpfmtState *st);
void print_function_definition(TSNode node, PrpfmtState *st);
void print_function_definition_decl(TSNode node, PrpfmtState *st);
void print_func_def_verification(TSNode node, PrpfmtState *st);
void print_arg_list(TSNode node, PrpfmtState *st);
void print_arg_item_list(TSNode node, PrpfmtState *st);
void print_arg_item(TSNode node, PrpfmtState *st);
void print_function_call_statement(TSNode node, PrpfmtState *st);
void print_function_call_expression(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 6. Expressions & Selection
 * Responsibilities: Formats complex expressions, including binary/unary operations, 
 * dot access, type casts, and member/bit selection.
 ******************************************************************************/
void print_expression_statement(TSNode node, PrpfmtState *st, bool is_inline);
void print__expression(TSNode node, PrpfmtState *st, bool is_inline);
void print__expression_with_comprehension(TSNode node, PrpfmtState *st, bool is_inline);
void print__restricted_expression(TSNode node, PrpfmtState *st);
void print_binary_expression(TSNode node, PrpfmtState *st);
void print_unary_expression(TSNode node, PrpfmtState *st);
void print_dot_expression(TSNode node, PrpfmtState *st);
void print_optional_expression(TSNode node, PrpfmtState *st);
void print_type_specification(TSNode node, PrpfmtState *st);
void print_type_cast(TSNode node, PrpfmtState *st);
void print_expression_list(TSNode node, PrpfmtState *st);
void print_for_comprehension(TSNode node, PrpfmtState *st);
void print_selection(TSNode node, PrpfmtState *st);
void print_member_selection(TSNode node, PrpfmtState *st);
void print_bit_selection(TSNode node, PrpfmtState *st);
void print_member_select(TSNode node, PrpfmtState *st);
void print_bit_select(TSNode node, PrpfmtState *st);
void print_select(TSNode node, PrpfmtState *st);
void print_select_options(TSNode node, PrpfmtState *st);
void print_bit_select_type(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 7. Types & Identifiers
 * Responsibilities: Formats type names and identifiers. Handles primitive, array, 
 * and complex type signatures, ensuring consistent identifier naming format.
 ******************************************************************************/
void print__type(TSNode node, PrpfmtState *st);
void print_primitive_type(TSNode node, PrpfmtState *st);
void print_unsized_integer_type(TSNode node, PrpfmtState *st);
void print_sized_integer_type(TSNode node, PrpfmtState *st);
void print_bounded_integer_type(TSNode node, PrpfmtState *st);
void print_range_type(TSNode node, PrpfmtState *st);
void print_array_type(TSNode node, PrpfmtState *st);
void print_string_type(TSNode node, PrpfmtState *st);
void print_boolean_type(TSNode node, PrpfmtState *st);
void print_type_type(TSNode node, PrpfmtState *st);
void print_expression_type(TSNode node, PrpfmtState *st);
void print_dot_expression_type(TSNode node, PrpfmtState *st);
void print_function_call_type(TSNode node, PrpfmtState *st);
void print_type_statement(TSNode node, PrpfmtState *st);
void print_type_or_identifier(TSNode node, PrpfmtState *st);
void print_typed_identifier(TSNode node, PrpfmtState *st);
void print_typed_identifier_list(TSNode node, PrpfmtState *st);
void print_identifier(TSNode node, PrpfmtState *st);
void print_ref_identifier(TSNode node, PrpfmtState *st);
void print_complex_identifier(TSNode node, PrpfmtState *st);
void print_complex_identifier_list(TSNode node, PrpfmtState *st);
void print_timed_identifier(TSNode node, PrpfmtState *st);
void print_var_or_let_or_reg(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 8. Literals & Numbers
 * Responsibilities: Formats constant values including numbers (binary, hex, decimal), 
 * booleans, and strings. Ensures literal values are emitted as-is from source.
 ******************************************************************************/
void print_constant(TSNode node, PrpfmtState *st);
void print__number(TSNode node, PrpfmtState *st);
void print__simple_number(TSNode node, PrpfmtState *st);
void print__scaled_number(TSNode node, PrpfmtState *st);
void print__hex_number(TSNode node, PrpfmtState *st);
void print__decimal_number(TSNode node, PrpfmtState *st);
void print__octal_number(TSNode node, PrpfmtState *st);
void print__binary_number(TSNode node, PrpfmtState *st);
void print__typed_number(TSNode node, PrpfmtState *st);
void print__neg_simple_number(TSNode node, PrpfmtState *st);
void print__neg_scaled_number(TSNode node, PrpfmtState *st);
void print__neg_hex_number(TSNode node, PrpfmtState *st);
void print__neg_decimal_number(TSNode node, PrpfmtState *st);
void print__neg_octal_number(TSNode node, PrpfmtState *st);
void print__neg_binary_number(TSNode node, PrpfmtState *st);
void print__neg_typed_number(TSNode node, PrpfmtState *st);
void print__bool_literal(TSNode node, PrpfmtState *st);
void print__string_literal(TSNode node, PrpfmtState *st);
void print__simple_string_literal(TSNode node, PrpfmtState *st);
void print_complex_string_literal(TSNode node, PrpfmtState *st);
void print__unknown_literal(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 9. Comments
 * Responsibilities: Processes and preserves code comments. Distinguishes between 
 * inline comments (same line as code) and newline comments.
 ******************************************************************************/
void print_comment(TSNode node, PrpfmtState *st);
void print_comment_inline(TSNode node, PrpfmtState *st);
void print_comment_newline(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 10. Special Statements & Attributes
 * Responsibilities: Formats non-standard constructs like imports, assertions, 
 * and hardware-specific attributes (comb, pipe, flow, delay).
 ******************************************************************************/
void print_import_statement(TSNode node, PrpfmtState *st);
void print_impl_statement(TSNode node, PrpfmtState *st);
void print_test_statement(TSNode node, PrpfmtState *st);
void print_cassert_statement(TSNode node, PrpfmtState *st);
void print_module_path(TSNode node, PrpfmtState *st);
void print_attributes(TSNode node, PrpfmtState *st);
void print_attribute_list(TSNode node, PrpfmtState *st);
void print_attribute_item_list(TSNode node, PrpfmtState *st);
void print_attribute_item(TSNode node, PrpfmtState *st);
void print_comb_tok(TSNode node, PrpfmtState *st);
void print_pipe_tok(TSNode node, PrpfmtState *st);
void print_flow_tok(TSNode node, PrpfmtState *st);
void print_delay_tok(TSNode node, PrpfmtState *st);
void print__semicolon(TSNode node, PrpfmtState *st);
void print__space(TSNode node, PrpfmtState *st);
void print__timing_sequence(TSNode node, PrpfmtState *st);

/******************************************************************************
 * 11. Utilities
 * Responsibilities: Provides low-level helper functions for string extraction 
 * and node analysis from the tree-sitter tree.
 ******************************************************************************/
char *get_node_text(TSNode node, const char *source_code);

#endif // PRP_FMT_H
