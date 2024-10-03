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
void print_if_expression(char *input_string, TSNode *node);
void print_while_statement(char *input_string, TSNode *node);
void print_for_statement(char *input_string, TSNode *node);
void print_function_definition_statement(char *input_string, TSNode *node);
void print_loop_statement(char *input_string, TSNode *node);
void print_pipestage_scope_statement(char *input_string, TSNode *node);
void print_match_expression(char *input_string, TSNode *node);
void print_match_list(char *input_string, TSNode *node);
void print_control_statement(char *input_string, TSNode *node);
void print_expression_statement(char *input_string, TSNode *node);
void print_stmt_list(char *input_string, TSNode *node);
void print_complex_identifier(char *input_string, TSNode *node);
void print_complex_identifier_list(char *input_string, TSNode *node);
void print_identifier(char *input_string, TSNode *node);
void print_binary_expression(char *input_string, TSNode *node);
void print_unary_expression(char *input_string, TSNode *node);
void print_expression(char *input_string, TSNode *node);
void print_lambda(char *input_string, TSNode *node);
void print_function_definition(char *input_string, TSNode *node);
void print_tuple(char *input_string, TSNode *node);
void print_tuple_sq(char *input_string, TSNode *node);
void print_optional_expression(char *input_string, TSNode *node);
void print_enum_definition(char *input_string, TSNode *node);
void print_ref_identifier(char *input_string, TSNode *node);
void print_tuple_list(char *input_string, TSNode *node);
void print__tuple_item(char *input_string, TSNode *node);
void print_simple_assignment(char *input_string, TSNode *node);
void print_function_call(char *input_string, TSNode *node);
void print_restricted_expression(char *input_string, TSNode *node);
void print_expression_list(char *input_string, TSNode *node);
void print_simple_function_call(char *input_string, TSNode *node);
void print_capture_list(char *input_string, TSNode *node);
void print_arg_list(char *input_string, TSNode *node);
void print_arg_item_list(char *input_string, TSNode *node);
void print_arg_item(char *input_string, TSNode *node);
void print_typed_identifier(char *input_string, TSNode *node);
void print_type_cast(char *input_string, TSNode *node);
void print_type(char *input_string, TSNode *node);
void print_attributes(char *input_string, TSNode *node);
void print_typed_identifier_list(char *input_string, TSNode *node);
void print_type_or_identifier(char *input_string, TSNode *node);
void print_test_statement(char *input_string, TSNode *node);
void print_dot_expression(char *input_string, TSNode *node);

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
      //printf("printing statement %d\n", i);
      TSNode child = ts_node_child(root_node, i);
      print_statement(input_string, &child);
      printf("\n");
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
  //printf("Statement symbol : %d\n", stsym);
  if (stsym != sym_statement) {
    if (stsym == sym_comment) {
      print_node_text_with_whitespace(input_string, node);
      printf("\n");
      return ;
    }
    printf("Node symbol is %d (expected %d, statement)\n", stsym, sym_statement);
  }

  //printf("node children: %d", ts_node_child_count(*node));
  for (int i = 0; i < ts_node_child_count(*node); i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    //printf("sym %d\n", symbol);

    switch(symbol) {
      case sym_scope_statement: case sym_scope_expression:
        print_scope_statement(input_string, &child);
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
        print_expression_statement(input_string, &child);
        break;
      case sym_test_statement:
        print_test_statement(input_string, &child);
        break;
      case sym_comment:
        print_node_text_with_whitespace(input_string, &child);
        break;
      default:
        printf("reached default, %d", symbol);
  }
  }
}

void print_scope_statement(char *input_string, TSNode *node) {
  uint32_t named_child_count = ts_node_named_child_count(*node);
  printf ("{\n");
  for (uint32_t i = 0; i < named_child_count; i++) {
    printf("  ");
    TSNode current_node = ts_node_named_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(current_node);
    if (symbol==sym_statement) {
      print_statement(input_string, &current_node);
    } else {
      printf("Expected statement (print scope statement)");
    }
    printf("\n");
  }
  printf("}");
}

void print_assignment_or_declaration_statement(char *input_string, TSNode *node) {
  TSNode decl = ts_node_child_by_field_name(*node, "decl", 4);
  if (!ts_node_is_null(decl)) {
    print_node_text(input_string, &decl);
    putchar(' ');
  }

  TSNode lvalue = ts_node_child_by_field_name(*node, "lvalue", 6);
  if (ts_node_grammar_symbol(lvalue) == sym_complex_identifier_list) {
    print_complex_identifier_list(input_string, &lvalue);
  } else if (ts_node_grammar_symbol(lvalue) == sym_complex_identifier) {
    print_complex_identifier(input_string, &lvalue);
    TSNode type = ts_node_child_by_field_name(*node, "type", 4);
    if (!ts_node_is_null(type)) {
      print_type_cast(input_string, &type);
    }
  } 
  putchar(' ');

  TSNode operator = ts_node_child_by_field_name(*node, "operator", 8);
  print_node_text(input_string, &operator);
  putchar(' ');

  TSNode delay = ts_node_child_by_field_name(*node, "delay", 5);
  if (!ts_node_is_null(delay)) {
    print_node_text(input_string, &delay);
    // TODO: print cycle select or pound
  }

  TSNode rvalue = ts_node_child_by_field_name(*node, "rvalue", 6);
  switch(ts_node_grammar_symbol(rvalue)) {
    case sym_ref_identifier:
      print_ref_identifier(input_string, &rvalue);
      break;
    case sym_enum_definition:
      print_enum_definition(input_string, &rvalue);
      break;
    default: 
      print_expression(input_string, &rvalue);
  }
}

void print_function_definition_statement(char *input_string, TSNode *node) {
  TSNode func_type = ts_node_child(*node, 0);
  print_node_text(input_string, &func_type);
  
  TSNode lvalue = ts_node_child(*node, 1);
  print_node_text(input_string, &lvalue);

  TSNode function_definition = ts_node_child(*node, 2);
  print_function_definition(input_string, &function_definition);
}

void print_function_call_statement(char *input_string, TSNode *node) {
  TSNode simple_function_call = ts_node_child(*node, 0);
  TSSymbol symbol = ts_node_grammar_symbol(simple_function_call);
  if (symbol != sym_simple_function_call) {
    printf("expected simple function call (fcall statement)");
  }

  TSNode always = ts_node_child_by_field_name(simple_function_call, "always", 6);
  if (!ts_node_is_null(always)) {
    printf("always ");
  }

  TSNode function = ts_node_child_by_field_name(simple_function_call, "function", 8);
  print_complex_identifier(input_string, &function);
  putchar(' ');

  TSNode argument = ts_node_child_by_field_name(simple_function_call, "argument", 8);
  print_expression_list(input_string, &argument);
}

void print_control_statement(char *input_string, TSNode *node) {
  TSNode first = ts_node_child(*node, 0);
  TSNode argument = ts_node_child_by_field_name(*node, "argument", 8);

  print_node_text(input_string, &first);
  printf(" return");

  if (!ts_node_is_null(argument)) {
    print_expression(input_string, &argument);
  }
}

void print_stmt_list(char *input_string, TSNode *node) {
  uint32_t ncc = ts_node_named_child_count(*node);
  for (int i = 0; i < ncc; i++) {
    if (i > 0) { printf(" ; "); }
    TSNode child = ts_node_named_child(*node, i);
    print__tuple_item(input_string, &child);
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
        printf("elif ");
        break;
      case anon_sym_else:
        printf("else ");
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
        printf("unexpected node type, print_if_expression");
    }
  }
}

void print_for_statement(char *input_string, TSNode *node) { uint32_t cc = ts_node_child_count(*node);
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
  printf("#>");
  TSNode c = ts_node_child(*node, 0);
  printf("pipestage %d", ts_node_grammar_symbol(c));
  TSNode fsm = ts_node_child_by_field_name(*node, "fsm", 3);
  if (!ts_node_is_null(fsm)) {
    TSNode sibling = ts_node_next_sibling(fsm);
    print_identifier(input_string, &fsm);
    print_tuple_sq(input_string, &sibling);
  }
  TSNode scope = ts_node_child_by_field_name(*node, "scope", 5);
  print_scope_statement(input_string, &scope);
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

void print_expression_statement(char *input_string, TSNode *node) {
  TSNode child = ts_node_child(*node, 0);
  //printf("print_expression_statement symbol: %d\n", ts_node_grammar_symbol(child));
  print_expression(input_string, &child);
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
        print_expression(input_string, &child);
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
  putchar('('); 
  for(uint32_t i = 0; i < ts_node_named_child_count(*node); i++) {
    TSNode child = ts_node_named_child(*node, i);
    switch(ts_node_grammar_symbol(child)) {
      case sym_tuple_list: 
        print_tuple_list(input_string, &child);
        break;
      case sym_comment:
        print_node_text_with_whitespace(input_string, &child);
        putchar('\n');
        break;
      default:
        printf("reached default, capture list");
    }
  }
  putchar(')'); 
}

void print_tuple_sq(char *input_string, TSNode *node) {
  putchar('['); 
  if(ts_node_child_count(*node) > 2) {
    TSNode list = ts_node_child(*node, 1);
    print_tuple_list(input_string, &list);
  }
  putchar(']'); 
}

void print_tuple_list(char *input_string, TSNode *node) {
  TSNode child1 = ts_node_named_child(*node, 0);
  print__tuple_item(input_string, &child1);
  
  for (int i = 1; i < ts_node_named_child_count(*node); i++) {
    printf(", ");
    TSNode child = ts_node_named_child(*node, i);
    print__tuple_item(input_string, &child);
  }
}

void print__tuple_item(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);

  switch (symbol) {
    case sym_ref_identifier:
      print_ref_identifier(input_string, node);
      break;
    case sym_simple_assignment:
      print_simple_assignment(input_string, node);
      break;
    default:
      print_expression(input_string, node);
  }
}

void print_complex_identifier(char *input_string, TSNode *node) {
  TSNode child = ts_node_child(*node, 0);
  TSSymbol symbol = ts_node_grammar_symbol(child);

  switch (symbol) {
    case sym_identifier:
      print_identifier(input_string, &child);
      break;
    case sym_dot_expression:
      print_dot_expression(input_string, &child);
      break;
    case sym_selection:
      // TODO: print selection
      break;
    default:
      printf("reached default, %d", symbol);
  }
}

void print_complex_identifier_list(char *input_string, TSNode *node) {
  uint32_t ncc = ts_node_named_child_count(*node);

  for (int i = 0; i < ncc; i++) {
    if (i > 0) { printf(", "); }
    TSNode child = ts_node_named_child(*node, i);
    TSSymbol csym = ts_node_grammar_symbol(child);
    if (csym == sym_complex_identifier) {
      print_complex_identifier(input_string, &child);
    } else {
      printf("expected complex identifier");
    }
  }
}

void print_dot_expression(char *input_string, TSNode *node) {
  TSNode item = ts_node_named_child(*node, 0);
  TSNode choice = ts_node_named_child(*node, 1);

  print_restricted_expression(input_string, &item);
  putchar('.');
  TSSymbol sym = ts_node_grammar_symbol(choice);
  switch(sym) {
    case sym_identifier:
      print_identifier(input_string, &choice);
      break;
    case sym_constant:
      print_node_text(input_string, &choice);
      break;
    case sym_tuple_sq:
      print_tuple_sq(input_string, &choice);
      break;
    default:
      printf("reached default dot expr");
  }
}

void print_identifier(char *input_string, TSNode *node) {
  uint32_t start_byte = ts_node_start_byte(*node);
  uint32_t end_byte = ts_node_end_byte(*node);
  
  if (input_string[start_byte] == '`') {
    print_node_text_with_whitespace(input_string, node);
  } else {
    print_node_text(input_string, node);
  }
}

void print_binary_expression(char *input_string, TSNode *node) {
  assert(!ts_node_is_null(*node));

  TSNode left = ts_node_child_by_field_name(*node, "left", 4);
  print_expression(input_string, &left);
  putchar(' '); 

  TSNode operator = ts_node_child_by_field_name(*node, "operator", 8);
  print_node_text(input_string, &operator);

  putchar(' '); 
  TSNode right = ts_node_child_by_field_name(*node, "right", 5);
  print_expression(input_string, &right);
}

void print_unary_expression(char *input_string, TSNode *node) {
  assert(!ts_node_is_null(*node));

  TSNode operator = ts_node_child_by_field_name(*node, "operator", 8);
  // TODO: add space when operator is 'not'
  print_node_text(input_string, &operator);

  TSNode argument = ts_node_child_by_field_name(*node, "argument", 8);
  print_expression(input_string, &argument);
}

void print_restricted_expression(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);

  switch(symbol) {
    case sym_complex_identifier:
      print_complex_identifier(input_string, node);
      break;
    case sym_constant:
      print_node_text(input_string, node);
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
    default: 
      printf("reached default, print_restricted_expression\nsymbol: %d\n", symbol);
  }
}

void print_function_call(char *input_string, TSNode *node) {
  TSNode function = ts_node_child(*node, 0);
  TSNode argument = ts_node_child(*node, 1);

  print_complex_identifier(input_string, &function);
  print_tuple(input_string, &argument);
}

void print_expression(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);
 
  switch(symbol) {
    case sym_unary_expression: 
      print_unary_expression(input_string, node);
      break;
    case sym_binary_expression:
      print_binary_expression(input_string, node);
      break;
    case sym_if_expression:
      print_if_expression(input_string, node);
      break;
    case sym_scope_expression:
    default:
      print_restricted_expression(input_string, node);
  }
}

void print_lambda(char *input_string, TSNode *node) {
  TSNode func_type = ts_node_child(*node, 0);
  TSNode func_def = ts_node_child(*node, 1);

  print_node_text(input_string, &func_type);
  putchar(' ');
  print_function_definition(input_string, &func_def);
}

void print_function_definition(char *input_string, TSNode *node) {
  TSNode capture = ts_node_child_by_field_name(*node, "capture", 7);
  if (!ts_node_is_null(capture)) {
    TSNode capture_node = ts_node_next_sibling(capture); 
    putchar('[');
    if(ts_node_symbol(capture_node) == sym_capture_list) {
      print_capture_list(input_string, &capture_node);
    }
    putchar(']');
  }

  TSNode generic = ts_node_child_by_field_name(*node, "generic", 7);
  if (!ts_node_is_null(generic)) {
    TSNode generic_node = ts_node_next_sibling(generic);
    putchar('<');
    if (!ts_node_is_null(generic_node)) {
      print_typed_identifier_list(input_string, &generic_node);
    }
    putchar('>');
  }

  TSNode input = ts_node_child_by_field_name(*node, "input", 5);
  if (!ts_node_is_null(input)) {
    print_arg_list(input_string, &input);
  }

  TSNode output = ts_node_child_by_field_name(*node, "output", 6);
  if (!ts_node_is_null(output)) {
    TSNode output_node = ts_node_next_sibling(output);
    TSSymbol sym = ts_node_grammar_symbol(output_node);
    printf("->");
    switch(sym) {
      case sym_arg_list:
        print_arg_list(input_string, &output_node);
        break;
      case sym_type_or_identifier:
        print_type_or_identifier(input_string, &output_node);
        break;
      default:
        printf("default in print function definition (output) %d", sym);
    }
  } 

  TSNode condition = ts_node_child_by_field_name(*node, "condition", 9);
  if (!ts_node_is_null(condition)) {
    printf("where ");
    TSNode cond_list = ts_node_next_sibling(condition);
    if (!ts_node_is_null(cond_list)) {
      print_expression_list(input_string, &cond_list);
    }
  }

  TSNode verification = ts_node_child_by_field_name(*node, "verification", 12);
  // Add verification handling

  TSNode code = ts_node_child_by_field_name(*node, "code", 4);
  print_scope_statement(input_string, &code);
}

void print_arg_list(char *input_string, TSNode *node) {
  putchar('(');
  if (ts_node_named_child_count(*node) > 0) {
    TSNode arg_list = ts_node_named_child(*node, 0);
    print_arg_item_list(input_string, &arg_list);
  }
  putchar(')');
}

void print_arg_item_list(char *input_string, TSNode *node) {
  TSNode child1 = ts_node_named_child(*node, 0);
  print_arg_item(input_string, &child1);
  
  for (int i = 1; i < ts_node_named_child_count(*node); i++) {
    printf(", ");
    TSNode child = ts_node_named_child(*node, i);
    print_arg_item(input_string, &child);
  }
}

void print_arg_item(char *input_string, TSNode *node) {
  TSNode mod = ts_node_child_by_field_name(*node, "mod", 3);
  TSNode def = ts_node_child_by_field_name(*node, "definition", 10);
  int i = 0;

  if (!ts_node_is_null(mod)) {
    print_node_text(input_string, &mod);
    putchar(' ');
    i++;
  }

  TSNode type = ts_node_child(*node, i);
  print_typed_identifier(input_string, &type);

  if (!ts_node_is_null(def)) {
    TSNode def_node = ts_node_next_sibling(def);
    print_expression(input_string, &def_node);
  }
}

void print_typed_identifier(char *input_string, TSNode *node) {
  TSNode ident = ts_node_child(*node, 0);
  TSNode type = ts_node_child_by_field_name(*node, "type", 4);

  print_identifier(input_string, &ident);
  if (!ts_node_is_null(type)) {
    print_type_cast(input_string, &type);
  }
}

void print_typed_identifier_list(char *input_string, TSNode *node) {
  TSNode child1 = ts_node_named_child(*node, 0);
  print_typed_identifier(input_string, &child1);
  
  for (int i = 1; i < ts_node_named_child_count(*node); i++) {
    printf(", ");
    TSNode child = ts_node_named_child(*node, i);
    print_typed_identifier(input_string, &child);
  }
}

void print_type_or_identifier(char *input_string, TSNode *node) {
  TSNode type = ts_node_child_by_field_name(*node, "type", 4);
  if (!ts_node_is_null(type)) {
    print_type_cast(input_string, &type);
  } else {
    TSNode ident = ts_node_named_child(*node, 0);
    print_typed_identifier(input_string, &ident);
  }
}

void print_type_cast(char *input_string, TSNode *node) {
  TSNode type = ts_node_child_by_field_name(*node, "type", 4);
  TSNode attribute = ts_node_child_by_field_name(*node, "attribute", 9);

  putchar(':');
  if (!ts_node_is_null(type)) {
    print_type(input_string, &type);
  }
  if (!ts_node_is_null(attribute)) {
    print_attributes(input_string, &attribute);
  }
}

void print_type(char *input_string, TSNode *node) {
  print_node_text(input_string, node); // TODO: add type functionality
}

void print_attributes(char *input_string, TSNode *node) {
  putchar(':');
  TSNode child = ts_node_named_child(*node, 0);
  TSSymbol sym = ts_node_grammar_symbol(child);

  if(sym == sym_tuple_sq) {
    print_tuple_sq(input_string, &child);
  } else if (sym == sym_tuple) {
    print_tuple(input_string, &child);
  } else {
    printf("error in print attributes");
  }
}

void print_capture_list(char *input_string, TSNode *node) {
  printf("capture list"); // TODO: add print capture list
}
void print_simple_assignment(char *input_string, TSNode *node) {
  TSNode decl = ts_node_child_by_field_name(*node, "decl", 4);
  if (!ts_node_is_null(decl)) {
    print_node_text(input_string, &decl);
    putchar(' ');
  }

  TSNode lvalue = ts_node_child_by_field_name(*node, "lvalue", 6);
  if (ts_node_grammar_symbol(lvalue) == sym_identifier) {
    print_identifier(input_string, &lvalue);
  } else {
    // TODO: Print type_cast or type_specification
  } 
  putchar(' ');

  TSNode operator = ts_node_child_by_field_name(*node, "operator", 8);
  print_node_text(input_string, &operator);
  putchar(' ');

  TSNode delay = ts_node_child_by_field_name(*node, "delay", 5);
  if (!ts_node_is_null(delay)) {
    print_node_text(input_string, &delay);
  }

  TSNode rvalue = ts_node_child_by_field_name(*node, "rvalue", 6);
  TSSymbol sym = ts_node_grammar_symbol(rvalue);
  switch(sym) {
    case sym_ref_identifier:
      print_ref_identifier(input_string, &rvalue);
      break;
    default:
      print_expression(input_string, &rvalue);
  }
}

void print_enum_definition(char *input_string, TSNode *node) {
  TSNode first = ts_node_child(*node, 0);
  TSNode input = ts_node_child_by_field_name(*node, "input", 5);
  
  print_node_text(input_string, &first);
  putchar(' ');
  print_arg_list(input_string, &input);
}

void print_ref_identifier(char *input_string, TSNode *node) {
  TSNode complex = ts_node_named_child(*node, 0);

  printf("ref ");
  print_complex_identifier(input_string, &complex);
}

void print_optional_expression(char *input_string, TSNode *node) {
  TSNode arg = ts_node_child(*node, 0);

  print_expression(input_string, &arg);
  putchar('?');
}

// clang -I tree-sitter/lib/include tree-sitter-pyrope/prpfmt/prpfmt.c tree-sitter-pyrope/src/parser.c tree-sitter-pyrope/src/scanner.c tree-sitter/libtree-sitter.a
