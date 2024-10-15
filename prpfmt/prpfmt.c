#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <tree_sitter/api.h>

// Symbol enum from tree-sitter-pyrope/src/parser.c
enum {
  sym_identifier = 1,
  anon_sym_SEMI = 2,
  anon_sym_LBRACE = 3,
  anon_sym_RBRACE = 4,
  anon_sym_continue = 5,
  anon_sym_break = 6,
  anon_sym_return = 7,
  anon_sym_unique = 8,
  anon_sym_if = 9,
  anon_sym_elif = 10,
  anon_sym_else = 11,
  anon_sym_for = 12,
  anon_sym_LPAREN = 13,
  anon_sym_RPAREN = 14,
  anon_sym_in = 15,
  anon_sym_while = 16,
  anon_sym_loop = 17,
  anon_sym_POUND_GT = 18,
  anon_sym_match = 19,
  anon_sym_and = 20,
  anon_sym_BANGand = 21,
  anon_sym_or = 22,
  anon_sym_BANGor = 23,
  anon_sym_AMP = 24,
  anon_sym_CARET = 25,
  anon_sym_PIPE = 26,
  anon_sym_TILDE_AMP = 27,
  anon_sym_TILDE_CARET = 28,
  anon_sym_TILDE_PIPE = 29,
  anon_sym_LT = 30,
  anon_sym_LT_EQ = 31,
  anon_sym_GT = 32,
  anon_sym_GT_EQ = 33,
  anon_sym_EQ_EQ = 34,
  anon_sym_BANG_EQ = 35,
  anon_sym_has = 36,
  anon_sym_BANGhas = 37,
  anon_sym_case = 38,
  anon_sym_BANGcase = 39,
  anon_sym_BANGin = 40,
  anon_sym_equals = 41,
  anon_sym_BANGequals = 42,
  anon_sym_does = 43,
  anon_sym_BANGdoes = 44,
  anon_sym_is = 45,
  anon_sym_BANGis = 46,
  anon_sym_test = 47,
  anon_sym_where = 48,
  anon_sym_COMMA = 49,
  anon_sym_LBRACK = 50,
  anon_sym_RBRACK = 51,
  anon_sym_DASH_GT = 52,
  anon_sym_COLON = 53,
  anon_sym_var = 54,
  anon_sym_let = 55,
  anon_sym_reg = 56,
  anon_sym_requires = 57,
  anon_sym_ensures = 58,
  anon_sym_enum = 59,
  anon_sym_variant = 60,
  anon_sym_ref = 61,
  anon_sym_EQ = 62,
  anon_sym_DOT_DOT_DOT = 63,
  anon_sym_BANG = 64,
  anon_sym_not = 65,
  anon_sym_TILDE = 66,
  anon_sym_DASH = 67,
  anon_sym_QMARK = 68,
  anon_sym_DOT_DOT_EQ = 69,
  anon_sym_DOT_DOT_LT = 70,
  anon_sym_DOT_DOT_PLUS = 71,
  anon_sym_step = 72,
  anon_sym_implies = 73,
  anon_sym_BANGimplies = 74,
  anon_sym_and_then = 75,
  anon_sym_or_else = 76,
  anon_sym_GT_GT = 77,
  anon_sym_LT_LT = 78,
  anon_sym_BANG_AMP = 79,
  anon_sym_BANG_CARET = 80,
  anon_sym_BANG_PIPE = 81,
  anon_sym_STAR = 82,
  anon_sym_SLASH = 83,
  anon_sym_PERCENT = 84,
  anon_sym_PLUS = 85,
  anon_sym_PIPE_GT = 86,
  anon_sym_PLUS_PLUS = 87,
  anon_sym_DOT = 88,
  sym_assignment_operator = 89,
  anon_sym_DOT_DOT = 90,
  anon_sym_AT = 91,
  anon_sym_sext = 92,
  anon_sym_zext = 93,
  anon_sym_POUND = 94,
  anon_sym_int = 95,
  anon_sym_integer = 96,
  anon_sym_signed = 97,
  anon_sym_uint = 98,
  anon_sym_unsigned = 99,
  sym_sized_integer_type = 100,
  anon_sym_range = 101,
  sym_string_type = 102,
  sym_boolean_type = 103,
  sym_type_type = 104,
  sym_fun_tok = 105,
  sym_proc_tok = 106,
  sym_always_tok = 107,
  sym__simple_number = 108,
  sym__scaled_number = 109,
  sym__hex_number = 110,
  sym__decimal_number = 111,
  sym__octal_number = 112,
  sym__binary_number = 113,
  sym__bool_literal = 114,
  sym__simple_string_literal = 115,
  anon_sym_DQUOTE = 116,
  aux_sym_complex_string_literal_token1 = 117,
  aux_sym_complex_string_literal_token2 = 118,
  sym__space = 119,
  sym_comment = 120,
  anon_sym_when = 121,
  anon_sym_unless = 122,
  sym__automatic_semicolon = 123,
  sym_description = 124,
  sym_statement = 125,
  sym_scope_statement = 126,
  sym_assignment_or_declaration_statement = 127,
  sym_function_call_statement = 128,
  sym_control_statement = 129,
  sym_stmt_list = 130,
  sym_if_expression = 131,
  sym_for_statement = 132,
  sym_while_statement = 133,
  sym_loop_statement = 134,
  sym_pipestage_scope_statement = 135,
  sym_match_expression = 136,
  sym_match_list = 137,
  sym_match_operator = 138,
  sym_expression_statement = 139,
  sym_test_statement = 140,
  sym_expression_list = 141,
  sym_simple_function_call = 142,
  sym_tuple = 143,
  sym_tuple_sq = 144,
  sym_tuple_list = 145,
  sym__tuple_item = 146,
  sym_function_inline = 147,
  sym_attributes = 148,
  sym_simple_assignment = 149,
  sym_function_definition_statement = 150,
  sym__assignment_or_declaration = 151,
  sym_cycle_select_or_pound = 152,
  sym_var_or_let_or_reg = 153,
  sym_function_definition = 154,
  sym_func_def_verification = 155,
  sym_enum_definition = 156,
  sym_ref_identifier = 157,
  sym_capture_list = 158,
  sym_arg_list = 159,
  sym_arg_item_list = 160,
  sym_arg_item = 161,
  sym_type_or_identifier = 162,
  sym_complex_identifier = 163,
  sym_complex_identifier_list = 164,
  sym_typed_identifier = 165,
  sym_typed_identifier_list = 166,
  sym__expression = 167,
  sym__expression_with_comprehension = 168,
  sym_for_comprehension = 169,
  sym_selection = 170,
  sym_member_selection = 171,
  sym_bit_selection = 172,
  sym_cycle_selection = 173,
  sym_type_specification = 174,
  sym_unary_expression = 175,
  sym_optional_expression = 176,
  sym_binary_expression = 177,
  sym_dot_expression = 178,
  sym_function_call = 179,
  sym_scope_expression = 180,
  sym__restricted_expression = 181,
  sym_lambda = 182,
  sym_select = 183,
  sym_select_options = 184,
  sym_member_select = 185,
  sym_bit_select = 186,
  sym_bit_select_type = 187,
  sym_cycle_select = 188,
  sym_type_cast = 189,
  sym__type = 190,
  sym_expression_type = 191,
  sym_dot_expression_type = 192,
  sym_function_call_type = 193,
  sym_array_type = 194,
  sym_function_type = 195,
  sym_primitive_type = 196,
  sym_unsized_integer_type = 197,
  sym_bounded_integer_type = 198,
  sym_range_type = 199,
  sym_constant = 200,
  sym__number = 201,
  sym__string_literal = 202,
  sym_complex_string_literal = 203,
  sym__semicolon = 204,
  sym_when_unless_cond = 205,
  aux_sym_description_repeat1 = 206,
  aux_sym_description_repeat2 = 207,
  aux_sym_scope_statement_repeat1 = 208,
  aux_sym_stmt_list_repeat1 = 209,
  aux_sym_if_expression_repeat1 = 210,
  aux_sym_match_list_repeat1 = 211,
  aux_sym_expression_list_repeat1 = 212,
  aux_sym_tuple_list_repeat1 = 213,
  aux_sym_tuple_list_repeat2 = 214,
  aux_sym_function_definition_repeat1 = 215,
  aux_sym_capture_list_repeat1 = 216,
  aux_sym_arg_item_list_repeat1 = 217,
  aux_sym_complex_identifier_list_repeat1 = 218,
  aux_sym_typed_identifier_list_repeat1 = 219,
  aux_sym_dot_expression_repeat1 = 220,
  aux_sym_member_select_repeat1 = 221,
  aux_sym_dot_expression_type_repeat1 = 222,
  aux_sym_complex_string_literal_repeat1 = 223,
};

void print_node_text(char *input_string, TSNode *node);
void print_node_text_with_whitespace(char *input_string, TSNode *node);
char *file_to_string(char *path);

void print_statement(char *input_string, TSNode *node);
void print_scope_statement(char *input_string, TSNode *node);
void print_assignment_or_declaration_statement(char *input_string, TSNode *node);
void print_function_call_statement(char *input_string, TSNode *node);
void print_control_statement(char *input_string, TSNode *node);
void print_stmt_list(char *input_string, TSNode *node);
void print_if_expression(char *input_string, TSNode *node);
void print_for_statement(char *input_string, TSNode *node);
void print_while_statement(char *input_string, TSNode *node);
void print_loop_statement(char *input_string, TSNode *node);
void print_pipestage_scope_statement(char *input_string, TSNode *node);
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
void print_function_definition(char *input_string, TSNode *node);
void print_func_def_verification(char *input_string, TSNode *node);
void print_enum_definition(char *input_string, TSNode *node);
void print_ref_identifier(char *input_string, TSNode *node);
void print_capture_list(char *input_string, TSNode *node);
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
void print_constant(char *input_string, TSNode *node);
void print_identifier(char *input_string, TSNode *node);
void print_comment(char *input_string, TSNode *node);
void print__semicolon(char *input_string, TSNode *node);

int main(int argc, char **argv) {
  if (argc < 2)
    printf("Argument of type file path expected\n"), exit(1);

  char *input_string = file_to_string(argv[1]);

  // Parsing
  TSLanguage *tree_sitter_pyrope();
  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_pyrope());

  TSTree *tree = ts_parser_parse_string(
    parser, 
    NULL, 
    input_string, 
    strlen(input_string)
  );

  // Tree usage and print
  TSNode root_node = ts_tree_root_node(tree);
  uint32_t root_child_count = ts_node_child_count(root_node);
  
  if (root_child_count > 0) {
    for (uint32_t i = 0; i < root_child_count; i++) {
      TSNode child = ts_node_child(root_node, i);
      print_statement(input_string, &child);
      //printf("reached top level\n");
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
  if (!fp) perror(path), exit(EXIT_FAILURE);

  fseek(fp, 0L, SEEK_END);
  long l_size = ftell(fp);
  rewind(fp);

  buffer = malloc(l_size + 1);
  if (!buffer) fclose(fp), fputs("Memory allocation fails", stderr), exit(1);
  
  if (1 != fread(buffer, l_size, 1, fp))
    fclose(fp), free(buffer), fputs("Read fails", stderr), exit(1);

  fclose(fp);

  //printf("%s", buffer);
  return buffer;
}

void print_node_text(char *input_string, TSNode *node) {
  uint32_t start = ts_node_start_byte(*node);
  uint32_t end = ts_node_end_byte(*node);
  for (uint32_t i = start; i < end; i++) {
    char t = input_string[i];
    if(t != ' ' && t != '\t' && t != '\n') {
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

// Print grammar rules

void print_statement(char *input_string, TSNode *node) {
  if (ts_node_is_null(*node)) {
    printf("TSNode is null, expected statement");
  } 
  TSSymbol stsym = ts_node_symbol(*node); 
  if (stsym != sym_statement) {
    if (stsym == sym_comment) {
      print_node_text_with_whitespace(input_string, node);
      printf("\n");
      return ;
    }
    printf("Node symbol is %d (expected %d, statement)\n", stsym, sym_statement);
  }

  for (int i = 0; i < ts_node_child_count(*node); i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
      case sym_scope_statement: 
        print_scope_statement(input_string, &child);
        break;
      case sym_pipestage_scope_statement: 
        print_pipestage_scope_statement(input_string, &child);
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
      case sym_function_definition_statement:
        print_function_definition_statement(input_string, &child);
        break;
      case sym_loop_statement:
        print_loop_statement(input_string, &child);
        break;
      case sym_expression_statement: 
        {
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
        printf("reached default, %d", symbol);
    }
  }
  printf("\n");
}

void print_scope_statement(char *input_string, TSNode *node) {
  TSNode parent = ts_node_parent(*node);
  uint32_t depth = 2;
  while (!ts_node_is_null(parent)) {
    TSSymbol symbol = ts_node_symbol(parent);
    if (symbol == sym_scope_statement) {
      depth += 2;
    }
    parent = ts_node_parent(parent);
  }

  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
      case anon_sym_LBRACE:
        printf("{\n");
        break;
      case anon_sym_RBRACE: 
        printf("%*s", (depth-2), "");
        printf("}");
        break;
      case sym_statement:
        printf("%*s", depth, "");
        print_statement(input_string, &child);
        break;
      default:
        printf("unexpected node type, scope_statement");
    }
  }
}

void print_assignment_or_declaration_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
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
      case sym_complex_identifier_list:
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
      case sym_cycle_select_or_pound:
        // TODO: print this
        break;
      case sym_enum_definition:
        print_enum_definition(input_string, &child);
        break;
      case sym_ref_identifier:
        print_ref_identifier(input_string, &child);
        break;
      default:
        print__expression(input_string, &child);
    }
  }
}

void print_function_call_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
      case sym_simple_function_call:
        print_simple_function_call(input_string, &child);
        break;
      case sym_comment:
        printf(" ");
        print_comment(input_string, &child);
        break;
      default:
        print__semicolon(input_string, &child);
    }
  }
}

void print_control_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
      case anon_sym_continue:
        printf("continue ");
        break;
      case anon_sym_break:
        printf("break ");
        break;
      case anon_sym_return:
        printf("return ");
        break;
      default:
      {
        const char *field_name = ts_node_field_name_for_child(*node, i); 
        if (strcmp(field_name, "argument") == 0) {
          print__expression_with_comprehension(input_string, &child); 
        } else {
          print__semicolon(input_string, &child);
        }
        print__expression_with_comprehension(input_string, &child);
      }
    }
  }
}

void print_stmt_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
      case anon_sym_SEMI:
        printf(" ; ");
        break;
      default:
        print__tuple_item(input_string, &child);
    }
  }
}

void print_if_expression(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
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
        print_scope_statement(input_string, &child);
        break;
      case sym_pipestage_scope_statement:
        print_pipestage_scope_statement(input_string, &child);
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

    switch(symbol) {
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
        print_scope_statement(input_string, &child);
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

    switch(symbol) {
      case anon_sym_while:
        printf("while ");
        break;
      case sym_stmt_list:
        print_stmt_list(input_string, &child);
        break;
      case sym_scope_statement:
        print_scope_statement(input_string, &child);
        break;
      case sym_pipestage_scope_statement:
        print_pipestage_scope_statement(input_string, &child);
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

    switch(symbol) {
      case anon_sym_loop:
        printf("loop ");
        break;
      case sym_scope_statement:
        print_scope_statement(input_string, &child);
        break;
      case sym_pipestage_scope_statement:
        print_pipestage_scope_statement(input_string, &child);
        break;
      default:
        printf("unexpected node type, print_while_statement");
    }
  }
}

void print_pipestage_scope_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
      case anon_sym_POUND_GT:
        printf("#> ");
        break;
      case sym_identifier:
        print_identifier(input_string, &child);
        break;
      case sym_tuple_sq:
        print_tuple_sq(input_string, &child);
        break;
      case sym_scope_statement:
        print_scope_statement(input_string, &child);
        break;
      default:
        printf("unexpected node type, pipestage_scope_statement");
    }
  }
}

void print_match_expression(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
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
      default:
        printf("unexpected node type, print_match_expression");
    }
  }
}

void print_match_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
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
        print_scope_statement(input_string, &child);
        break;
      case sym_pipestage_scope_statement:
        print_pipestage_scope_statement(input_string, &child);
        break;
      default:
        printf("unexpected node type, print_match_list");
    }
  }
}

void print_test_statement(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
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
        print_scope_statement(input_string, &child);
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

    switch(symbol) {
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

    switch(symbol) {
      case sym_always_tok:
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

    switch(symbol) {
      case anon_sym_LPAREN:
        printf("(");
        break;
      case anon_sym_RPAREN:
        printf(")");
        break;
      case sym_tuple_list:
        print_tuple_list(input_string, &child);
        break;
      default:
        printf("unexpected node type, tuple");
    }
  }
}

void print_tuple_sq(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
      case anon_sym_LBRACK:
        printf("[");
        break;
      case anon_sym_RBRACK:
        printf("]");
        break;
      case sym_tuple_list:
        print_tuple_list(input_string, &child);
        break;
      default:
        printf("unexpected node type, tuple_sq");
    }
  }
}

void print_tuple_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
      case anon_sym_COMMA:
        printf(", ");
        break;
      default:
        print__tuple_item(input_string, &child);
    }
  }
}

void print__tuple_item(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);

  switch(symbol) {
    case sym_ref_identifier:
      print_ref_identifier(input_string, node);
      break;
    case sym_simple_assignment:
      print_simple_assignment(input_string, node);
      break;
    case sym_function_inline:
      print_function_inline(input_string, node);
      break;
    case sym_scope_statement:
      print_scope_statement(input_string, node);
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
    
    switch(symbol) {
      case sym_fun_tok:
        printf("fun ");
        break;
      case sym_proc_tok:
        printf("proc ");
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
        printf("reached default, %d", symbol);
    }
  }
}

void print_attributes(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
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
        printf("reached default, %d", symbol);
    }
  }
}

void print_simple_assignment(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
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
      case sym_cycle_select_or_pound:
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
    
    switch(symbol) {
      case sym_fun_tok:
        printf("fun ");
        break;
      case sym_proc_tok:
        printf("proc ");
        break;
      case sym_complex_identifier:
        print_complex_identifier(input_string, &child);
        break;
      case sym_function_definition:
        print_function_definition(input_string, &child);
        break;
      default:
        printf("reached default %d", symbol);
    }
  }
}

void print_function_definition(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
      case anon_sym_LBRACK:
        printf("[");
        break;
      case anon_sym_RBRACK:
        printf("]");
        break;
      case sym_capture_list:
        print_capture_list(input_string, &child);
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
      case sym_func_def_verification:
        print_func_def_verification(input_string, &child);
        break;
      case sym_scope_statement:
        print_scope_statement(input_string, &child);
        break;
      default:
        printf("reached default, %d", symbol);
    }
  }
}

void print_func_def_verification(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
      case anon_sym_requires:
        printf("requires ");
        break;
      case anon_sym_ensures:
        printf("ensures ");
        break;
      default:
        print__expression(input_string, &child);
    }
  }
}

void print_enum_definition(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
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
        printf("reached default %d", symbol);
    }
  }
}

void print_ref_identifier(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
      case anon_sym_ref:
        printf("ref ");
        break;
      case sym_complex_identifier:
        print_complex_identifier(input_string, &child);
        break;
      default:
        printf("reached default %d", symbol);
    }
  }
}

void print_capture_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
      case sym_typed_identifier:
        print_typed_identifier(input_string, &child);
        break;
      case anon_sym_EQ:
        printf("=");
        break;
      default:
        print__expression_with_comprehension(input_string, &child);
    }
  }
}

void print_arg_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
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
        printf("reached default %d",symbol);
    }
  }
}

void print_arg_item_list(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
      case anon_sym_COMMA:
        printf(", ");
        break;
      case sym_arg_item:
        print_arg_item(input_string, &child);
        break;
      default:
        printf("reached default %d",symbol);
    }
  }
}

void print_arg_item(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
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
    
    switch(symbol) {
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
        printf("reached default, %d", symbol);
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
        printf(", ");
        break;
      default:
        printf("reached default, %d", symbol);
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
        printf("reached default, %d", symbol);
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
        printf(", ");
        break;
      default:
        printf("reached default, %d", symbol);
    }
  }
}

void print__expression(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);
  
  switch(symbol) {
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

  switch(symbol) {
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

    switch(symbol) {
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

    switch(symbol) {
      case sym_member_selection:
        print_member_selection(input_string, &child);
        break;
      case sym_bit_selection:
        print_bit_selection(input_string, &child);
        break;
      case sym_cycle_selection:
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

    switch(symbol) {
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

    switch(symbol) {
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

    switch(symbol) {
      case sym_cycle_select:
        print_cycle_select(input_string, &child);
        break;
      default:
        print__restricted_expression(input_string, &child);
    }
  }
}

void print_type_specification(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
      case anon_sym_COLON:
        printf(":");
        break;
      case sym_attributes:
        print_attributes(input_string, &child);
        break;
      default:
        {
        const char *field_name = ts_node_field_name_for_child(*node, i); 
        if (strcmp(field_name, "argument") == 0) {
          print__restricted_expression(input_string, &child); 
        } else if (strcmp(field_name, "type") == 0) {
          print__type(input_string, &child);
        } else {
          printf("reached default, %d", symbol);
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

    switch(symbol) {
      case anon_sym_not:
        printf("not ");
        break;
      case anon_sym_BANG: case anon_sym_TILDE:
      case anon_sym_DASH: case anon_sym_DOT_DOT_DOT:
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

    switch(symbol) {
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

    switch(symbol) {
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

    switch(symbol) {
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

  switch(symbol) {
    case sym__restricted_expression:
      printf("Cool1");
      break;
    case sym_complex_identifier:
      print_complex_identifier(input_string, node);
      break;
    case sym_constant:
      print_constant(input_string, node);
      break;
    case sym_function_call:
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
    case sym_scope_expression:
      print_scope_statement(input_string, node);
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
    
    switch(symbol) {
      case sym_fun_tok:
        printf("fun ");
        break;
      case sym_proc_tok:
        printf("proc ");
        break;
      case sym_function_definition:
        print_function_definition(input_string, &child);
        break;
      default:
        printf("reached default %d", symbol);
    }
  }
}

void print_select(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
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
        printf("reached default %d", symbol);
    }
  }
}

void print_select_options(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
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
    
    switch(symbol) {
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
    
    switch(symbol) {
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
        printf("reached default, member_select");
    }
  }
}

void print_cycle_select(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    switch(symbol) {
      case anon_sym_POUND:
        printf("#");
        break;
      case sym_select:
        print_select(input_string, &child);
        break;
      default:
        printf("reached default, %d", symbol);
    }
  }
}

void print_type_cast(char *input_string, TSNode *node) {
  uint32_t cc = ts_node_child_count(*node);
  for (int i = 0; i < cc; i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch(symbol) {
      case anon_sym_POUND:
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
        printf(", ");
        break;
      default:
        printf("reached default, %d", symbol);
    }
  }
}

void print_constant(char *input_string, TSNode *node) {
  print_node_text_with_whitespace(input_string, node);
}

void print_identifier(char *input_string, TSNode *node) {
  print_node_text_with_whitespace(input_string, node);
}

void print__type(char *input_string, TSNode *node) {
  print_node_text(input_string, node); // Add type functionality
}

void print_comment(char *input_string, TSNode *node) {
  print_node_text_with_whitespace(input_string, node);
}

void print__semicolon(char *input_string, TSNode *node) {
  // TODO: when unless cond
}
// clang -I tree-sitter/lib/include tree-sitter-pyrope/prpfmt/prpfmt.c tree-sitter-pyrope/src/parser.c tree-sitter-pyrope/src/scanner.c tree-sitter/libtree-sitter.a
