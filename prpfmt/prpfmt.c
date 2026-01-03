#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>

// Symbol enum from tree-sitter-pyrope/src/parser.c
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

void print_node_text(char *input_string, TSNode *node);
void print_node_text_with_whitespace(char *input_string, TSNode *node);
char *file_to_string(char *path);
bool is_next_line_empty(char *input_string, TSNode *node);

void print_statement(char *input_string, TSNode *node, bool wrap);
void print_scope_statement(char *input_string, TSNode *node, bool wrap);
void print_assignment_or_declaration_statement(char *input_string,
                                               TSNode *node);
void print_function_call_statement(char *input_string, TSNode *node);
void print_control_statement(char *input_string, TSNode *node);
void print_stmt_list(char *input_string, TSNode *node);
void print_if_expression(char *input_string, TSNode *node);
void print_for_statement(char *input_string, TSNode *node);
void print_while_statement(char *input_string, TSNode *node);
void print_loop_statement(char *input_string, TSNode *node);
void print_match_expression(char *input_string, TSNode *node);
void print_match_list(char *input_string, TSNode *node);
void print_test_statement(char *input_string, TSNode *node);
void print_expression_list(char *input_string, TSNode *node);
void print_simple_function_call(char *input_string, TSNode *node);
void print_tuple(char *input_string, TSNode *node);
void print_tuple_sq(char *input_string, TSNode *node);
void print_tuple_list(char *input_string, TSNode *node);
void print__tuple_item(char *input_string, TSNode *node);
void print_function_inline(char *input_string, TSNode *node);
void print_attributes(char *input_string, TSNode *node);
void print_simple_assignment(char *input_string, TSNode *node);
void print_function_definition_statement(char *input_string, TSNode *node);
void print_function_definition_decl(char *input_string, TSNode *node);
void print_enum_definition(char *input_string, TSNode *node);
void print_ref_identifier(char *input_string, TSNode *node);
void print_arg_list(char *input_string, TSNode *node);
void print_arg_item_list(char *input_string, TSNode *node);
void print_arg_item(char *input_string, TSNode *node);
void print_type_or_identifier(char *input_string, TSNode *node);
void print_complex_identifier(char *input_string, TSNode *node);
void print_complex_identifier_list(char *input_string, TSNode *node);
void print_typed_identifier(char *input_string, TSNode *node);
void print_typed_identifier_list(char *input_string, TSNode *node);
void print__expression(char *input_string, TSNode *node);
void print__expression_with_comprehension(char *input_string, TSNode *node);
void print_for_comprehension(char *input_string, TSNode *node);
void print_type_specification(char *input_string, TSNode *node);
void print_selection(char *input_string, TSNode *node);
void print_member_selection(char *input_string, TSNode *node);
void print_bit_selection(char *input_string, TSNode *node);
void print_cycle_selection(char *input_string, TSNode *node);
void print_unary_expression(char *input_string, TSNode *node);
void print_binary_expression(char *input_string, TSNode *node);
void print_optional_expression(char *input_string, TSNode *node);
void print_dot_expression(char *input_string, TSNode *node);
void print_function_call(char *input_string, TSNode *node);
void print__restricted_expression(char *input_string, TSNode *node);
void print_lambda(char *input_string, TSNode *node);
void print_select(char *input_string, TSNode *node);
void print_select_options(char *input_string, TSNode *node);
void print_member_select(char *input_string, TSNode *node);
void print_bit_select(char *input_string, TSNode *node);
void print_cycle_select(char *input_string, TSNode *node);
void print_type_cast(char *input_string, TSNode *node);
void print_trivial_identifier_list(char *input_string, TSNode *node);
void print__type(char *input_string, TSNode *node);
void print_expression_type(char *input_string, TSNode *node);
void print_dot_expression_type(char *input_string, TSNode *node);
void print_function_call_type(char *input_string, TSNode *node);
void print_array_type(char *input_string, TSNode *node);
void print_function_type(char *input_string, TSNode *node);
void print_primitive_type(char *input_string, TSNode *node);
void print_constant(char *input_string, TSNode *node);
void print_identifier(char *input_string, TSNode *node);
void print_comment(char *input_string, TSNode *node);
bool print__semicolon(char *input_string, TSNode *node);
void print_when_unless_cond(char *input_string, TSNode *node);

const int if_wrap_length = 100;

int main(int argc, char **argv) {
  if (argc < 2)
    printf("Argument of type file path expected\n"), exit(1);

  char *input_string = file_to_string(argv[1]);

  // Parsing
  TSLanguage *tree_sitter_pyrope();
  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_pyrope());

  TSTree *tree =
      ts_parser_parse_string(parser, NULL, input_string, strlen(input_string));

  // Tree usage and print
  TSNode root_node = ts_tree_root_node(tree);
  uint32_t root_child_count = ts_node_child_count(root_node);

  if (root_child_count > 0) {
    for (uint32_t i = 0; i < root_child_count; i++) {
      TSNode child = ts_node_child(root_node, i);
      print_statement(input_string, &child, false);
      // printf("reached top level\n");
    }
  }

  // Cleanup
  ts_tree_delete(tree);
  ts_parser_delete(parser);
  free(input_string);
  return 0;
}

char *file_to_string(char *path) {
  char *buffer;

  FILE *fp = fopen(path, "r");
  if (!fp)
    perror(path), exit(EXIT_FAILURE);

  fseek(fp, 0L, SEEK_END);
  long l_size = ftell(fp);
  rewind(fp);

  buffer = malloc(l_size + 1);
  if (!buffer)
    fclose(fp), fputs("Memory allocation fails", stderr), exit(1);

  if (1 != fread(buffer, l_size, 1, fp))
    fclose(fp), free(buffer), fputs("Read fails", stderr), exit(1);

  fclose(fp);

  // printf("%s", buffer);
  return buffer;
}

void print_node_text(char *input_string, TSNode *node) {
  uint32_t start = ts_node_start_byte(*node);
  uint32_t end = ts_node_end_byte(*node);
  for (uint32_t i = start; i < end; i++) {
    char t = input_string[i];
    if (t != ' ' && t != '\t' && t != '\n') {
      putchar(t);
    }
  }
}

void print_node_text_with_whitespace(char *input_string, TSNode *node) {
  uint32_t start = ts_node_start_byte(*node);
  uint32_t end = ts_node_end_byte(*node);
  for (uint32_t i = start; i < end; i++) {
    char t = input_string[i];
    putchar(t);
  }
}

bool is_next_line_empty(char *input_string, TSNode *node) {
  uint32_t end_byte = ts_node_end_byte(*node);

  // Navigate to end of current line
  while (input_string[end_byte] != '\n') {
    if (input_string[end_byte] == EOF)
      return false;
    end_byte++;
  }
  end_byte++;

  // Check if next line is empty
  while (input_string[end_byte] != '\n') {
    if (input_string[end_byte] == EOF)
      return false;

    if (input_string[end_byte] != ' ' && input_string[end_byte] != '\t')
      return false;
    end_byte++;
  }

  return true;
}
// Print grammar rules

void print_statement(char *input_string, TSNode *node, bool wrap) {
  if (ts_node_is_null(*node)) {
    printf("TSNode is null, expected statement");
  }
  TSSymbol stsym = ts_node_grammar_symbol(*node);
  if (stsym != sym_statement) {
    if (stsym == sym_comment) {
      print_node_text_with_whitespace(input_string, node);
      printf("\n");
      return;
    }
    printf("Node symbol is %d (expected %d, statement)\n", stsym,
           sym_statement);
  }

  for (int i = 0; i < ts_node_child_count(*node); i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_scope_statement:
      print_scope_statement(input_string, &child, false);
      break;
    case sym_assignment_or_declaration_statement:
      print_assignment_or_declaration_statement(input_string, &child);
      break;
    case sym_function_call_statement:
      print_function_call_statement(input_string, &child);
      break;
    case sym_control_statement:
      print_control_statement(input_string, &child);
      break;
    case sym_while_statement:
      print_while_statement(input_string, &child);
      break;
    case sym_for_statement:
      print_for_statement(input_string, &child);
      break;
    case sym_loop_statement:
      print_loop_statement(input_string, &child);
      break;
    case sym_expression_statement: {
      TSNode c2 = ts_node_child(child, 0);
      print__expression_with_comprehension(input_string, &c2);
      break;
    }
    case sym_test_statement:
      print_test_statement(input_string, &child);
      break;
    case sym_comment:
      printf(" ");
      print_node_text_with_whitespace(input_string, &child);
      break;
    default:
      printf("reached default,statement %d", symbol);
    }
  }
  if (!wrap) {
    printf("\n");
  }
  if (is_next_line_empty(input_string, node))
    printf("\n");
}

void print_scope_statement(char *input_string, TSNode *node, bool wrap) {
  TSNode parent = ts_node_parent(*node);
  uint32_t depth = 2;
  while (!ts_node_is_null(parent)) {
    TSSymbol symbol = ts_node_grammar_symbol(parent);
    if (symbol == sym_scope_statement) {
      depth += 2;
    }
    parent = ts_node_parent(parent);
  }

  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_LBRACE:
      printf("{");
      if (!wrap) {
        printf("\n");
      }
      break;
    case anon_sym_RBRACE:
      if (!wrap) {
        printf("%*s", (depth - 2), "");
      }
      printf("}");
      break;
    case sym_statement:
      if (!wrap) {
        printf("%*s", (depth), "");
      }
      print_statement(input_string, &child, wrap);
      break;
    // case sym_scope_expression:
    case sym_scope_statement:
      print_scope_statement(input_string, &child, false);
      break;
    case sym_comment:
      print_node_text_with_whitespace(input_string, &child);
      break;
    default:
      printf("unexpected node type, scope_statement:%d", symbol);
    }
  }
}

void print_assignment_or_declaration_statement(char *input_string,
                                               TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_var_or_let_or_reg:
      print_node_text(input_string, &child);
      putchar(' ');
      break;
    case anon_sym_LPAREN:
      printf("(");
      break;
    case anon_sym_RPAREN:
      printf(")");
      break;
      // case sym_complex_identifier_list:
      print_complex_identifier_list(input_string, &child);
      break;
    case sym_complex_identifier:
      print_complex_identifier(input_string, &child);
      break;
    case sym_type_cast:
      print_type_cast(input_string, &child);
      break;
    case sym_assignment_operator:
      putchar(' ');
      print_node_text(input_string, &child);
      putchar(' ');
      break;
      // case sym_cycle_select_or_pound:
      {
        TSNode c2 = ts_node_child(child, 0);
        print_cycle_select(input_string, &c2);
        break;
      }
      break;
    case sym_enum_definition:
      print_enum_definition(input_string, &child);
      break;
    case sym_ref_identifier:
      print_ref_identifier(input_string, &child);
      break;
    case sym_when_unless_cond:
      print_when_unless_cond(input_string, &child);
      break;
    default:
      if (!print__semicolon(input_string, &child)) {
        print__expression_with_comprehension(input_string, &child);
      }
    }
  }
}

void print_function_call_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      // case sym_simple_function_call:
      print_simple_function_call(input_string, &child);
      break;
    case sym_comment:
      printf(" ");
      print_comment(input_string, &child);
      break;
    default:
      if (!print__semicolon(input_string, &child)) {
        printf("unexpected node type, fcall statement");
      }
    }
  }
}

void print_control_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_continue:
      printf("continue ");
      break;
    case anon_sym_break:
      printf("break ");
      break;
    case anon_sym_return:
      printf("return ");
      break;
    default: {
      const char *field_name = ts_node_field_name_for_child(*node, i);
      if (strcmp(field_name, "argument") == 0) {
        print__expression_with_comprehension(input_string, &child);
      } else {
        if (!print__semicolon(input_string, &child)) {
          printf("unexpected node type, control statement");
        }
      }
    }
    }
  }
}

void print_stmt_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_SEMI:
      printf(" ; ");
      break;
    default:
      print__tuple_item(input_string, &child);
    }
  }
}

void print_if_expression(char *input_string, TSNode *node) {
  bool wrap = false;
  uint32_t start = ts_node_start_byte(*node);
  uint32_t end = ts_node_end_byte(*node);
  int length = end - start;
  if (length < if_wrap_length) {
    printf("wlen:%d:", length);
    wrap = true;
  }
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_unique:
      printf("unique ");
      break;
    case anon_sym_if:
      printf("if ");
      break;
    case anon_sym_elif:
      printf(" elif ");
      break;
    case anon_sym_else:
      printf(" else");
      break;
    case sym_stmt_list:
      print_stmt_list(input_string, &child);
      break;
    case sym_scope_statement:
      putchar(' ');
      print_scope_statement(input_string, &child, wrap);
      break;
    default:
      printf("unexpected node type, print_if_expression");
    }
  }
}

void print_for_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_for:
      printf("for ");
      break;
    case anon_sym_LPAREN:
      printf("(");
      break;
    case anon_sym_RPAREN:
      printf(")");
      break;
    case sym_typed_identifier_list:
      print_typed_identifier_list(input_string, &child);
      break;
    case sym_typed_identifier:
      print_typed_identifier(input_string, &child);
      break;
    case anon_sym_in:
      printf(" in ");
      break;
    case sym_ref_identifier:
      print_ref_identifier(input_string, &child);
      break;
    case sym_expression_list:
      print_expression_list(input_string, &child);
      break;
    case sym_scope_statement:
      print_scope_statement(input_string, &child, false);
      break;
    default:
      printf("unexpected node type, print_for_statement");
    }
  }
}

void print_while_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_while:
      printf("while ");
      break;
    case sym_stmt_list:
      print_stmt_list(input_string, &child);
      break;
    case sym_scope_statement:
      print_scope_statement(input_string, &child, false);
      break;
    default:
      printf("unexpected node type, print_while_statement");
    }
  }
}

void print_loop_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_loop:
      printf("loop ");
      break;
    case sym_scope_statement:
      print_scope_statement(input_string, &child, false);
      break;
    default:
      printf("unexpected node type, print_while_statement");
    }
  }
}

void print_match_expression(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_match:
      printf("match ");
      break;
    case anon_sym_LBRACE:
      printf("{");
      break;
    case anon_sym_RBRACE:
      printf("}");
      break;
    case sym_stmt_list:
      print_stmt_list(input_string, &child);
      break;
    case sym_match_list:
      print_match_list(input_string, &child);
      break;
    case sym_comment:
      printf(" ");
      print_comment(input_string, &child);
      break;
    default:
      printf("unexpected node type, print_match_expression%d", symbol);
    }
  }
}

void print_match_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_match_operator:
      print_node_text(input_string, &child);
      break;
    case sym_expression_list:
      print_expression_list(input_string, &child);
      break;
    case anon_sym_else:
      printf(" else ");
      break;
    case sym_scope_statement:
      print_scope_statement(input_string, &child, false);
      break;
    case sym_comment:
      printf(" ");
      print_comment(input_string, &child);
      break;
    default:
      printf("unexpected node type, print_match_list%d", symbol);
    }
  }
}

void print_test_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_test:
      printf("test ");
      break;
    case anon_sym_where:
      printf(" where ");
      break;
    case sym_expression_list:
      print_expression_list(input_string, &child);
      break;
    case sym_scope_statement:
      printf(" ");
      print_scope_statement(input_string, &child, false);
      break;
    default:
      printf("unexpected node type, print_test_statement");
    }
  }
}

void print_expression_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_COMMA:
      printf(", ");
      break;
    default:
      print__expression(input_string, &child);
    }
  }
}

void print_simple_function_call(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      // case sym_always_tok:
      printf("always ");
      break;
    case sym_complex_identifier:
      print_complex_identifier(input_string, &child);
      printf(" ");
      break;
    case sym_expression_list:
      print_expression_list(input_string, &child);
      break;
    default:
      printf("unexpected node type, simple_function_call");
    }
  }
}

void print_tuple(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_LPAREN:
      printf("(");
      break;
    case anon_sym_RPAREN:
      printf(")");
      break;
    case sym_tuple_list:
      print_tuple_list(input_string, &child);
      break;
    case sym_comment:
      printf(" ");
      print_comment(input_string, &child);
      break;
    default:
      printf("unexpected node type, tuple%d", symbol);
    }
  }
}

void print_tuple_sq(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_LBRACK:
      printf("[");
      break;
    case anon_sym_RBRACK:
      printf("]");
      break;
    case sym_tuple_list:
      print_tuple_list(input_string, &child);
      break;
    case sym_comment:
      printf(" ");
      print_comment(input_string, &child);
      break;
    default:
      printf("unexpected node type, tuple_sq%d", symbol);
    }
  }
}

void print_tuple_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_COMMA:
      printf(",");
      break;
    default:
      print__tuple_item(input_string, &child);
    }
  }
}

void print__tuple_item(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);

  switch (symbol) {
  case sym_ref_identifier:
    print_ref_identifier(input_string, node);
    break;
    // case sym_simple_assignment:
    print_simple_assignment(input_string, node);
    break;
    // case sym_function_inline:
    print_function_inline(input_string, node);
    break;
  case sym_scope_statement:
    print_scope_statement(input_string, node, false);
    break;
  default:
    print__expression_with_comprehension(input_string, node);
  }
}

void print_function_inline(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_comb_tok:
      printf("comb ");
      break;
    case sym_pipe_tok:
      printf("pipe ");
      break;
    case sym_flow_tok:
      printf("flow ");
      break;
    case sym_identifier:
      print_identifier(input_string, &child);
      break;
    case anon_sym_LT:
      printf("<");
      break;
    case anon_sym_GT:
      printf(">");
      break;
    case sym_typed_identifier_list:
      print_typed_identifier_list(input_string, &child);
      break;
    case sym_arg_list:
      print_arg_list(input_string, &child);
      break;
    case anon_sym_DASH_GT:
      printf("->");
      break;
    default:
      printf("reached default, function_inline:%d", symbol);
    }
  }
}

void print_attributes(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_COLON:
      printf(":");
      break;
    case sym_tuple:
      print_tuple(input_string, &child);
      break;
    case sym_tuple_sq:
      print_tuple_sq(input_string, &child);
      break;
    default:
      printf("reached default, attributes:%d", symbol);
    }
  }
}

void print_simple_assignment(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_var_or_let_or_reg:
      print_node_text(input_string, &child);
      putchar(' ');
      break;
    case sym_identifier:
      print_identifier(input_string, &child);
      break;
    case sym_type_cast:
      print_type_cast(input_string, &child);
      break;
    case sym_type_specification:
      print_type_specification(input_string, &child);
      break;
    case sym_assignment_operator:
      print_node_text(input_string, &child);
      break;
      // case sym_cycle_select_or_pound:
      {
        TSNode c2 = ts_node_child(child, 0);
        print_cycle_select(input_string, &c2);
        break;
      }
    case sym_ref_identifier:
      print_ref_identifier(input_string, &child);
      break;
    default:
      print__expression_with_comprehension(input_string, &child);
    }
  }
}

void print_function_definition_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_comb_tok:
      printf("comb ");
      break;
    case sym_pipe_tok:
      printf("pipe ");
      break;
    case sym_flow_tok:
      printf("flow ");
      break;
    case sym_complex_identifier:
      print_complex_identifier(input_string, &child);
      break;
    case sym_function_definition_decl:
      print_function_definition_decl(input_string, &child);
      break;
    default:
      printf("reached default, function_def_stmt: %d", symbol);
    }
  }
}

void print_function_definition_decl(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_LBRACK:
      printf("[");
      break;
    case anon_sym_RBRACK:
      printf("]");
      break;
    case anon_sym_LT:
      printf("<");
      break;
    case anon_sym_GT:
      printf(">");
      break;
    case sym_typed_identifier_list:
      print_typed_identifier_list(input_string, &child);
      break;
    case sym_arg_list:
      print_arg_list(input_string, &child);
      break;
    case anon_sym_DASH_GT:
      printf("->");
      break;
    case sym_type_or_identifier:
      print_type_or_identifier(input_string, &child);
      break;
    case anon_sym_where:
      printf("where ");
      break;
    case sym_expression_list:
      print_expression_list(input_string, &child);
      break;
    case sym_scope_statement:
      print_scope_statement(input_string, &child, false);
      break;
    default:
      printf("reached default, func_def%d", symbol);
    }
  }
}

void print_enum_definition(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_enum:
      printf("enum ");
      break;
    case anon_sym_variant:
      printf("variant ");
      break;
    case sym_arg_list:
      print_arg_list(input_string, &child);
      break;
    default:
      printf("reached default, enum_def %d", symbol);
    }
  }
}

void print_ref_identifier(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_ref:
      printf("ref ");
      break;
    case sym_complex_identifier:
      print_complex_identifier(input_string, &child);
      break;
    default:
      printf("reached default, ref_ident %d", symbol);
    }
  }
}

void print_arg_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_LPAREN:
      printf("(");
      break;
    case anon_sym_RPAREN:
      printf(")");
      break;
    case sym_arg_item_list:
      print_arg_item_list(input_string, &child);
      break;
    default:
      printf("reached default,arg_list %d", symbol);
    }
  }
}

void print_arg_item_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_COMMA:
      printf(",");
      break;
    case sym_arg_item:
      print_arg_item(input_string, &child);
      break;
    case sym_comment:
      printf(" ");
      print_node_text_with_whitespace(input_string, &child);
      break;
    default:
      printf("reached default,arg_item_list %d", symbol);
    }
  }
}

void print_arg_item(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_DOT_DOT_DOT:
      printf("...");
      break;
    case anon_sym_ref:
      printf("ref ");
      break;
    case anon_sym_reg:
      printf("reg ");
      break;
    case sym_typed_identifier:
      print_typed_identifier(input_string, &child);
      break;
    case anon_sym_EQ:
      printf(" = ");
      break;
    default:
      print__expression_with_comprehension(input_string, &child);
    }
  }
}

void print_type_or_identifier(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_type_cast:
      print_type_cast(input_string, &child);
      break;
    case sym_typed_identifier:
      print_typed_identifier(input_string, &child);
      break;
    default:
      printf("default %d", symbol);
    }
  }
}

void print_complex_identifier(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_identifier:
      print_identifier(input_string, &child);
      break;
    case sym_dot_expression:
      print_dot_expression(input_string, &child);
      break;
    case sym_selection:
      print_selection(input_string, &child);
      break;
    default:
      printf("reached default,complex_identifier %d", symbol);
    }
  }
}

void print_complex_identifier_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_complex_identifier:
      print_complex_identifier(input_string, &child);
      break;
    case anon_sym_COMMA:
      printf(",");
      break;
    default:
      printf("reached default,complex_identifier_list %d", symbol);
    }
  }
}

void print_typed_identifier(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_identifier:
      print_identifier(input_string, &child);
      break;
    case sym_type_cast:
      print_type_cast(input_string, &child);
      break;
    default:
      printf("reached default,typed_ident %d", symbol);
    }
  }
}

void print_typed_identifier_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_typed_identifier:
      print_typed_identifier(input_string, &child);
      break;
    case anon_sym_COMMA:
      printf(",");
      break;
    default:
      printf("reached default,typed_ident_list %d", symbol);
    }
  }
}

void print__expression(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);

  switch (symbol) {
  case sym_type_specification:
    print_type_specification(input_string, node);
    break;
  case sym_unary_expression:
    print_unary_expression(input_string, node);
    break;
  case sym_binary_expression:
    print_binary_expression(input_string, node);
    break;
  default:
    print__restricted_expression(input_string, node);
  }
}

void print__expression_with_comprehension(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);

  switch (symbol) {
  case sym_for_comprehension:
    print_for_comprehension(input_string, node);
    break;
  default:
    print__expression(input_string, node);
  }
}

void print_for_comprehension(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_for:
      printf("for");
      break;
    case anon_sym_LPAREN:
      printf("(");
      break;
    case anon_sym_RPAREN:
      printf(")");
      break;
    case sym_typed_identifier_list:
      print_typed_identifier_list(input_string, &child);
      break;
    case sym_typed_identifier:
      print_typed_identifier(input_string, &child);
      break;
    case anon_sym_in:
      printf(" in ");
      break;
    case sym_expression_list:
      print_expression_list(input_string, &child);
      break;
    case anon_sym_if:
      printf(" if ");
      break;
    case sym_stmt_list:
      print_stmt_list(input_string, &child);
      break;
    default:
      printf("unexpected node type, for_comprehension");
    }
  }
}

void print_selection(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_member_selection:
      print_member_selection(input_string, &child);
      break;
    case sym_bit_selection:
      print_bit_selection(input_string, &child);
      break;
      // case sym_cycle_selection:
      print_cycle_selection(input_string, &child);
      break;
    default:
      printf("unexpected node type, print_selection");
    }
  }
}

void print_member_selection(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_member_select:
      print_member_select(input_string, &child);
      break;
    default:
      print__restricted_expression(input_string, &child);
    }
  }
}

void print_bit_selection(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_bit_select:
      print_bit_select(input_string, &child);
      break;
    default:
      print__restricted_expression(input_string, &child);
    }
  }
}

void print_cycle_selection(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    print_cycle_select(input_string, &child);
  }
}

void print_type_specification(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_COLON:
      printf(":");
      break;
    case sym_attributes:
      print_attributes(input_string, &child);
      break;
    default: {
      const char *field_name = ts_node_field_name_for_child(*node, i);
      if (strcmp(field_name, "argument") == 0) {
        print__restricted_expression(input_string, &child);
      } else if (strcmp(field_name, "type") == 0) {
        print__type(input_string, &child);
      } else {
        printf("reached default,type_spec %d", symbol);
      }
    }
    }
  }
}

void print_unary_expression(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_not:
      printf("not ");
      break;
    case anon_sym_BANG:
    case anon_sym_TILDE:
    case anon_sym_DASH:
    case anon_sym_DOT_DOT_DOT:
      print_node_text(input_string, &child);
      break;
    default:
      print__expression(input_string, &child);
    }
  }
}

void print_optional_expression(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_QMARK:
      printf("?");
      break;
    default:
      print__expression(input_string, &child);
    }
  }
}

void print_binary_expression(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    if (symbol >= anon_sym_SEMI && symbol <= anon_sym_unsigned) {
      putchar(' ');
      print_node_text(input_string, &child);
      putchar(' ');
    } else {
      print__expression(input_string, &child);
    }
  }
}

void print_dot_expression(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_DOT:
      printf(".");
      break;
    case sym_identifier:
      print_identifier(input_string, &child);
      break;
    case sym_constant:
      print_constant(input_string, &child);
      break;
    default:
      print__restricted_expression(input_string, &child);
    }
  }
}

void print_function_call(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_complex_identifier:
      print_complex_identifier(input_string, &child);
      break;
    case sym_tuple:
      print_tuple(input_string, &child);
      break;
    default:
      printf("reached default, function_call");
    }
  }
}

void print__restricted_expression(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);

  switch (symbol) {
  case sym_complex_identifier:
    print_complex_identifier(input_string, node);
    break;
  case sym_constant:
    print_constant(input_string, node);
    break;
    // case sym_function_call:
    print_function_call(input_string, node);
    break;
  case sym_lambda:
    print_lambda(input_string, node);
    break;
  case sym_tuple:
    print_tuple(input_string, node);
    break;
  case sym_tuple_sq:
    print_tuple_sq(input_string, node);
    break;
  case sym_optional_expression:
    print_optional_expression(input_string, node);
    break;
  case sym_if_expression:
    print_if_expression(input_string, node);
    break;
  case sym_match_expression:
    print_match_expression(input_string, node);
    break;
    // case sym_scope_expression:
    print_scope_statement(input_string, node, false);
    break;
  case sym_comment:
    printf(" ");
    print_node_text_with_whitespace(input_string, node);
    break;
  default:
    printf("reached default, print_restricted_expression %d\n", symbol);
  }
}

void print_lambda(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_comb_tok:
      printf("comb ");
      break;
    case sym_pipe_tok:
      printf("pipe ");
      break;
    case sym_flow_tok:
      printf("flow ");
      break;
    case sym_function_definition_decl:
      print_function_definition_decl(input_string, &child);
      break;
    default:
      printf("reached default,lambda %d", symbol);
    }
  }
}

void print_select(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_LBRACK:
      printf("[");
      break;
    case anon_sym_RBRACK:
      printf("]");
      break;
    case sym_select_options:
      print_select_options(input_string, &child);
      break;
    default:
      printf("reached default,select %d", symbol);
    }
  }
}

void print_select_options(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_expression_list:
      print_expression_list(input_string, &child);
      break;
    case anon_sym_DOT_DOT:
      printf("..");
      break;
    case anon_sym_DOT_DOT_EQ:
      printf("..=");
      break;
    case anon_sym_DOT_DOT_LT:
      printf("..<");
      break;
    default:
      print__expression(input_string, &child);
    }
  }
}

void print_member_select(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_select:
      print_select(input_string, &child);
      break;
    default:
      printf("reached default, member_select");
    }
  }
}

void print_bit_select(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_bit_select_type:
      print_node_text(input_string, &child);
      break;
    case sym_select:
      print_select(input_string, &child);
      break;
    case anon_sym_AT:
      printf("@");
      break;
    default:
      printf("reached default, bit_select");
    }
  }
}

void print_cycle_select(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_POUND:
      printf("#");
      break;
    case sym_select:
      print_select(input_string, &child);
      break;
    default:
      printf("reached default,cycle select %d", symbol);
    }
  }
}

void print_type_cast(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_COLON:
      printf(":");
      break;
    case sym_attributes:
      print_attributes(input_string, &child);
      break;
    default:
      print__type(input_string, &child);
    }
  }
}

void print_trivial_identifier_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_identifier:
      print_identifier(input_string, &child);
      break;
    case anon_sym_COMMA:
      printf(",");
      break;
    default:
      printf("reached default,trivial identifier list %d", symbol);
    }
  }
}

void print__type(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);

  switch (symbol) {
  case sym_primitive_type:
    print_primitive_type(input_string, node);
    break;
  case sym_array_type:
    print_array_type(input_string, node);
    break;
    // case sym_function_type:
    print_function_type(input_string, node);
    break;
  case sym_expression_type:
    print_expression_type(input_string, node);
    break;
  default:
    printf("reached default, print__type:%d\n", symbol);
  }
}

void print_expression_type(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_identifier:
      print_identifier(input_string, &child);
      break;
    case sym_constant:
      print_constant(input_string, &child);
      break;
    case sym_tuple:
      print_tuple(input_string, &child);
      break;
    case sym_if_expression:
      print_if_expression(input_string, &child);
      break;
    case sym_match_expression:
      print_match_expression(input_string, &child);
      break;
    case sym_scope_statement:
      print_scope_statement(input_string, &child, false);
      break;
    case sym_dot_expression_type:
      print_dot_expression_type(input_string, &child);
      break;
    case sym_function_call_type:
      print_function_call_type(input_string, &child);
      break;
    default:
      printf("unexpected node type, print_expression_type:%d", symbol);
    }
  }
}

void print_dot_expression_type(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_DOT:
      printf(".");
      break;
    case sym_expression_type:
      print_expression_type(input_string, &child);
      break;
    default:
      printf("unexpected node type, print_dot_expression_type");
    }
  }
}

void print_function_call_type(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_complex_identifier:
      print_complex_identifier(input_string, &child);
      break;
    case sym_tuple:
      print_tuple(input_string, &child);
      break;
    default:
      printf("unexpected node type, print_function_call_type");
    }
  }
}

void print_array_type(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_tuple_sq:
      print_tuple_sq(input_string, &child);
      break;
    case sym_primitive_type:
      print_primitive_type(input_string, &child);
      break;
    case sym_array_type:
      print_array_type(input_string, &child);
      break;
      // case sym_function_type:
      print_function_type(input_string, &child);
      break;
    case sym_expression_type:
      print_expression_type(input_string, &child);
      break;
    default:
      printf("unexpected node type, print_array_type");
    }
  }
}

void print_function_type(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_comb_tok:
      printf("comb ");
      break;
    case sym_pipe_tok:
      printf("pipe ");
      break;
    case sym_flow_tok:
      printf("flow ");
      break;
    case anon_sym_LT:
      printf("<");
      break;
    case anon_sym_GT:
      printf(">");
      break;
    case sym_typed_identifier_list:
      print_typed_identifier_list(input_string, &child);
      break;
    case sym_arg_list:
      print_arg_list(input_string, &child);
      break;
    case anon_sym_DASH_GT:
      printf("->");
      break;
    default:
      printf("unexpected node type, print_function_type");
    }
  }
}

void print_primitive_type(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case sym_unsized_integer_type:
      print_node_text(input_string, &child);
      printf(" ");
      break;
    case sym_sized_integer_type:
      print_node_text(input_string, &child);
      break;
    case sym_bounded_integer_type:
      print_node_text(input_string, &child);
      break;
    case sym_range_type:
      print_node_text(input_string, &child);
      break;
    case sym_string_type:
      printf("string");
      break;
    case sym_boolean_type:
      printf("boolean");
      break;
    case sym_type_type:
      printf("type");
      break;
    default:
      printf("unexpected node type, print_array_type");
    }
  }
}

void print_constant(char *input_string, TSNode *node) {
  print_node_text_with_whitespace(input_string, node);
}

void print_identifier(char *input_string, TSNode *node) {
  print_node_text_with_whitespace(input_string, node);
}

void print_comment(char *input_string, TSNode *node) {
  print_node_text_with_whitespace(input_string, node);
  // printf("\n");
}

bool print__semicolon(char *input_string, TSNode *node) {
  bool found_node = false;
  TSSymbol symbol = ts_node_grammar_symbol(*node);

  switch (symbol) {
  case anon_sym_SEMI:
    found_node = true;
    printf(" ; ");
    break;
  case sym_when_unless_cond:
    found_node = true;
    putchar(' ');
    print_when_unless_cond(input_string, node);
    putchar(' ');
    break;
  }

  return found_node;
}

void print_when_unless_cond(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
    case anon_sym_when:
      printf(" when ");
      break;
    case anon_sym_unless:
      printf(" unless ");
      break;
    default:
      print__expression(input_string, &child);
    }
  }
}
// clang -I tree-sitter/lib/include tree-sitter-pyrope/prpfmt/prpfmt.c
// tree-sitter-pyrope/src/parser.c tree-sitter-pyrope/src/scanner.c
// tree-sitter/libtree-sitter.a
