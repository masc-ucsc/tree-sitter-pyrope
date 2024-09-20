#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <tree_sitter/api.h>

#define STATEMENT 125
#define COMMENT 120
#define FOR_STATEMENT 132 
#define IF_EXPRESSION 131
#define ELIF 10
#define STMT_LIST 130
#define SCOPE_STMT 126
#define ELSE 11
#define EXPRESSION_STATEMENT 139
#define SCOPE_EXPRESSION 180
#define ASSIGNMENT_OR_DECLARATION_STATEMENT 127
#define COMPLEX_IDENTIFIER 163
#define COMPLEX_IDENTIFIER_LIST 164 
#define FUNCTION_CALL_STATEMENT 128 
#define CONTROL_STATEMENT 129
#define WHILE_STATEMENT 133
#define FUNCTION_DEFINITION_STATEMENT 150
#define LOOP_STATEMENT 134 
#define BINARY_EXPRESSION 177
#define UNARY_EXPRESSION 175 
#define CONSTANT 200
#define IDENTIFIER 1
#define FUNCTION_CALL 179
#define LAMBDA 182
#define TEST_STATEMENT 140
#define REF_IDENTIFIER 157
#define TUPLE 143
#define TUPLE_SQ 144
#define SIMPLE_ASSIGNMENT 149
#define ENUM_DEFINITION 156
#define DOT_EXPRESSION 178 
#define OPTIONAL_EXPRESSION 176
#define MATCH_EXPRESSION 136
#define ARG_LIST 52
#define SELECTION 170 
#define TYPE_OR_IDENTIFIER 162 

bool depth_first_traversal(char *input_string, TSNode *node, int depth);
void print_node_text(char *input_string, TSNode *node);
void print_node_text_with_whitespace(char *input_string, TSNode *node);
void print_children(char *input_string, TSNode *node);
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
void print_control_statement(char *input_string, TSNode *node);
void print_expression_statement(char *input_string, TSNode *node);
void print_stmt_list(char *input_string, TSNode *node);
void print_complex_identifier(char *input_string, TSNode *node);
void print_identifier(char *input_string, TSNode *node);
void print_binary_expression(char *input_string, TSNode *node);
void print_unary_expression(char *input_string, TSNode *node);
void print_expression(char *input_string, TSNode *node);
void print_lambda(char *input_string, TSNode *node);
void print_function_definition(char *input_string, TSNode *node);
void print_tuple(char *input_string, TSNode *node);
void print_tuple_sq(char *input_string, TSNode *node);
void print_optional_expression(char *input_string, TSNode *node);
void print_match_expression(char *input_string, TSNode *node);
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

bool depth_first_traversal(char *input_string, TSNode *node, int depth) {
  if (ts_node_grammar_symbol(*node)==SCOPE_STMT) {
    //print_scope_statement(input_string, node);
  }

  uint32_t child_count = ts_node_child_count(*node);
  if (child_count > 0) {
    for (uint32_t child = 0; child < child_count; child++) {
      TSNode child_node = ts_node_child(*node, child);
      for (int i = 0; i < depth; i++) {
        printf("  ");
      }
      const char *type = ts_node_type(*node);
      printf("%s ", type);
      printf("%d ", ts_node_symbol(*node));
      print_node_text(input_string, node);
      printf("\n");
      depth_first_traversal(input_string, &child_node, depth+1);
    }
  } else {
    // Else we have reached a terminal node
  }
  return true;
}

void print_children(char *input_string, TSNode *node) {
  for (uint32_t i = 0; i < ts_node_child_count(*node); i++) {
    printf("*");
    TSNode child = ts_node_child(*node, i);
    print_node_text(input_string, &child);
  }
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
  if (stsym != STATEMENT) {
    if (stsym == COMMENT) {
      print_node_text_with_whitespace(input_string, node);
      printf("\n");
      return ;
    }
    printf("Node symbol is %d (expected %d, statement)\n", stsym, STATEMENT);
  }

  //printf("node children: %d", ts_node_child_count(*node));
  for (int i = 0; i < ts_node_child_count(*node); i++) {
    TSNode child = ts_node_child(*node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    //printf("sym %d\n", symbol);

    switch(symbol) {
      case SCOPE_STMT: case SCOPE_EXPRESSION:
        print_scope_statement(input_string, &child);
        break;
      case ASSIGNMENT_OR_DECLARATION_STATEMENT:
        print_assignment_or_declaration_statement(input_string, &child);
        break;
      case FUNCTION_CALL_STATEMENT:
        print_function_call_statement(input_string, &child);
        break;
      case CONTROL_STATEMENT:
        print_control_statement(input_string, &child);
        break;
      case WHILE_STATEMENT:
        print_while_statement(input_string, &child);
        break;
      case FOR_STATEMENT:
        print_for_statement(input_string, &child);
        break;
      case FUNCTION_DEFINITION_STATEMENT:
        print_function_definition_statement(input_string, &child);
        break;
      case LOOP_STATEMENT:
        print_loop_statement(input_string, &child);
        break;
      case EXPRESSION_STATEMENT: 
        print_expression_statement(input_string, &child);
        break;
      case TEST_STATEMENT:
        print_test_statement(input_string, &child);
        break;
      case COMMENT:
        print_node_text_with_whitespace(input_string, &child);
        break;
      default:
        printf("reached default, %d", symbol);
        //print_node_text(input_string, &child);
  }
  }
}

void print_scope_statement(char *input_string, TSNode *node) {
  uint32_t named_child_count = ts_node_named_child_count(*node);
  printf ("{\n");
  for (uint32_t i = 0; i < named_child_count; i++) {
    printf("  ");
    TSNode current_node = ts_node_named_child(*node, i);
    print_statement(input_string, &current_node);
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
  if (ts_node_grammar_symbol(lvalue) == COMPLEX_IDENTIFIER_LIST) {
    // TODO: Print complex identifier list
  } else if (ts_node_grammar_symbol(lvalue) == COMPLEX_IDENTIFIER) {
    print_complex_identifier(input_string, &lvalue);
    TSNode type = ts_node_child_by_field_name(*node, "type", 4);
    if (!ts_node_is_null(type)) {
      // TODO: Print type cast
    }
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
  //printf("rv sym %d\n", ts_node_grammar_symbol(rvalue));
  switch(ts_node_grammar_symbol(rvalue)) {
    case REF_IDENTIFIER:
      print_ref_identifier(input_string, &rvalue);
      break;
    case ENUM_DEFINITION:
      // TODO: print enum definition
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

  TSNode always = ts_node_child_by_field_name(simple_function_call, "always", 6);
  if (!ts_node_is_null(always)) {
    printf("always");
  }

  TSNode function = ts_node_child_by_field_name(simple_function_call, "function", 8);
  print_node_text(input_string, &function);
  putchar(' ');

  TSNode argument = ts_node_child_by_field_name(simple_function_call, "argument", 8);
  print_node_text(input_string, &argument);
}

void print_if_expression(char *input_string, TSNode *node) {
  assert(!ts_node_is_null(*node));
  //printf("print_if_expression()\n");

  assert(ts_node_grammar_symbol(*node)==131);

  // Print useful information
  uint32_t child_count = ts_node_child_count(*node);
  //printf("Child count: %d\n", child_count);
  //printf("Named child count: %d\n", ts_node_named_child_count(*node));
  //print_children(input_string, node);
  //printf("\n");

  // Print if_expression
  printf("if ");
  TSNode condition = ts_node_child(*node, 1); 
  print_stmt_list(input_string, &condition); 
  TSNode code = ts_node_child(*node, 2);
  printf(" ");
  print_scope_statement(input_string, &code);

  for(uint32_t i = 3; i < child_count; i++) {
    TSNode current_node = ts_node_child(*node, i);
    switch(ts_node_grammar_symbol(current_node)) {
      case ELIF:
        printf(" elif ");
        break;
      case STMT_LIST:
        print_stmt_list(input_string, &current_node);
        printf("\n");
        break;
      case SCOPE_STMT:
        print_scope_statement(input_string, &current_node);
        printf("\n");
        break;
      case ELSE:
        printf(" else ");
        break;
    }
  }
  //printf("\n");
}

void print_while_statement(char *input_string, TSNode *node) {
  printf("while ");
  TSNode condition = ts_node_child(*node, 1);
  print_stmt_list(input_string, &condition);
  TSNode scope_stmt = ts_node_child(*node, 2);
  print_scope_statement(input_string, &scope_stmt);
}

void print_for_statement(char *input_string, TSNode *node) {
  printf("for ");

  TSNode index = ts_node_child_by_field_name(*node, "index", 5);
  if (!ts_node_is_null(index)) {
    putchar('(');
    print_typed_identifier_list(input_string, &index);
    putchar(')');
  } else {
    TSNode ident = ts_node_named_child(*node, 0);
    print_typed_identifier(input_string, &ident);
  }
  
  printf(" in ");

  TSNode data = ts_node_child_by_field_name(*node, "data", 4);
  if (!ts_node_is_null(data)) {
    print_expression_list(input_string, &data);
  } else {
    TSNode ref = ts_node_named_child(*node, 1);
    print_ref_identifier(input_string, &ref);
  }
  putchar (' ');
  TSNode code = ts_node_child_by_field_name(*node, "code", 4);
  print_scope_statement(input_string, &code);
}

void print_loop_statement(char *input_string, TSNode *node) {
  printf("loop ");

  TSNode code = ts_node_named_child(*node, 0);
  print_scope_statement(input_string, &code);
}

void print_control_statement(char *input_string, TSNode *node) {
  print_node_text(input_string, node);
}

void print_expression_statement(char *input_string, TSNode *node) {
  TSNode child = ts_node_child(*node, 0);
  //printf("print_expression_statement symbol: %d\n", ts_node_grammar_symbol(child));
  print_expression(input_string, &child);
}

void print_stmt_list(char *input_string, TSNode *node) {
  assert(!ts_node_is_null(*node));
  //printf("printingstmtlist");
  print_node_text(input_string, node); // TODO: Print formatted stmt_list 
}

void print_complex_identifier(char *input_string, TSNode *node) {
  TSNode child = ts_node_child(*node, 0);
  TSSymbol symbol = ts_node_grammar_symbol(child);

  switch (symbol) {
    case IDENTIFIER:
      print_identifier(input_string, &child);
      break;
    case DOT_EXPRESSION:
      print_dot_expression(input_string, &child);
      break;
    case SELECTION:
      // TODO: print selection
      break;
    default:
      printf("reached default, %d", symbol);
  }
}

void print_dot_expression(char *input_string, TSNode *node) {
  TSNode item = ts_node_named_child(*node, 0);
  TSNode choice = ts_node_named_child(*node, 1);

  print_restricted_expression(input_string, &item);
  putchar('.');
  TSSymbol sym = ts_node_grammar_symbol(choice);
  switch(sym) {
    case IDENTIFIER:
      print_identifier(input_string, &choice);
      break;
    case CONSTANT:
      print_node_text(input_string, &choice);
      break;
    case TUPLE_SQ:
      print_tuple_sq(input_string, &choice);
      break;
    default:
      printf("reached default dot expr");
  }
}

void print_test_statement(char *input_string, TSNode *node) {
  TSNode args = ts_node_child_by_field_name(*node, "args", 4);
  TSNode condition = ts_node_child_by_field_name(*node, "condition", 9);
  TSNode code = ts_node_child_by_field_name(*node, "code", 4);

  printf("test ");
  print_expression_list(input_string, &args);
  if (!ts_node_is_null(condition)) {
    print_expression_list(input_string, &condition); // TODO: does this always contain "where"?
  }
  print_scope_statement(input_string, &code);
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
    case COMPLEX_IDENTIFIER:
      print_complex_identifier(input_string, node);
      break;
    case CONSTANT:
      print_node_text(input_string, node);
      break;
    case FUNCTION_CALL:
      print_function_call(input_string, node);
      break;
    case LAMBDA:
      print_lambda(input_string, node);
      break;
    case TUPLE:
      print_tuple(input_string, node);
      break;
    case TUPLE_SQ:
      print_tuple_sq(input_string, node);
      break;
    case OPTIONAL_EXPRESSION:
      print_optional_expression(input_string, node);
      break;
    case IF_EXPRESSION:
      print_if_expression(input_string, node);
      break;
    case MATCH_EXPRESSION:
      print_match_expression(input_string, node);
      break;
    case SCOPE_EXPRESSION:
      print_scope_statement(input_string, node);
      break;
    default: 
      printf("reached default, print_restricted_expression\nsymbol: %d\n", symbol);
  }
}

void print_simple_function_call(char *input_string, TSNode *node) {
  TSNode always = ts_node_child_by_field_name(*node, "always", 6);
  TSNode function = ts_node_child_by_field_name(*node, "function", 8);
  TSNode argument = ts_node_child_by_field_name(*node, "argument", 8);

  if(!ts_node_is_null(always)) {
    printf("always ");
  }
  print_complex_identifier(input_string, &function);
  print_expression_list(input_string, &argument);
}

void print_function_call(char *input_string, TSNode *node) {
  TSNode function = ts_node_child(*node, 0);
  TSNode argument = ts_node_child(*node, 1);

  print_complex_identifier(input_string, &function);
  print_tuple(input_string, &argument);
}

void print_expression_list(char *input_string, TSNode *node) {
  TSNode child1 = ts_node_named_child(*node, 0);
  print_expression(input_string, &child1);
  
  for (int i = 1; i < ts_node_named_child_count(*node); i++) {
    printf(", ");
    TSNode child = ts_node_named_child(*node, i);
    print_expression(input_string, &child);
  }
}

void print_expression(char *input_string, TSNode *node) {
  TSSymbol symbol = ts_node_grammar_symbol(*node);
 
  switch(symbol) {
    case UNARY_EXPRESSION: 
      print_unary_expression(input_string, node);
      break;
    case BINARY_EXPRESSION:
      print_binary_expression(input_string, node);
      break;
    case IF_EXPRESSION:
      print_if_expression(input_string, node);
      break;
    case SCOPE_EXPRESSION:
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
  printf("cc%d", ts_node_child_count(*node));
  TSNode capture = ts_node_child_by_field_name(*node, "capture", 7);
  if (!ts_node_is_null(capture)) {
    putchar('[');
    TSNode capture_list = ts_node_named_child(capture, 0);
    if(!ts_node_is_null(capture_list)) {
      print_capture_list(input_string, &capture_list);
    }
    putchar(']');
  }

  TSNode generic = ts_node_child_by_field_name(*node, "generic", 7);
  if (!ts_node_is_null(generic)) {
    putchar('<');
    TSNode generic_list = ts_node_named_child(generic, 0);
    if (!ts_node_is_null(generic_list)) {
      print_typed_identifier_list(input_string, &generic_list);
    }
    putchar('>');
  }

  TSNode input = ts_node_child_by_field_name(*node, "input", 5);
  printf("bing%s", ts_node_grammar_type(input));
  if (!ts_node_is_null(input)) {
    print_arg_list(input_string, &input);
  }

  TSNode output = ts_node_child_by_field_name(*node, "output", 6);
  printf("bing%d", ts_node_grammar_symbol(output));
  printf("bg%d", ts_node_child_count(output));
  if (!ts_node_is_null(output)) {
    TSSymbol sym = ts_node_grammar_symbol(output);
    printf("->");
    //TSNode output_node = ts_node_named_child(output, 0);
    switch(sym) {
      case ARG_LIST:
        //print_arg_list(input_string, &output);
        break;
      case TYPE_OR_IDENTIFIER:
        //print_type_or_identifier(input_string, &output);
        break;
      default:
        printf("default in print function definition (output) %d", sym);
    }
  } else { printf("null"); }

  TSNode condition = ts_node_child_by_field_name(*node, "condition", 9);
  if (!ts_node_is_null(condition)) {
    printf("where ");
    TSNode cond_list = ts_node_named_child(condition, 0);
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
  printf("yak%d", ts_node_grammar_symbol(*node));
  putchar('(');
  TSNode arg_list = ts_node_named_child(*node, 0);
  print_arg_item_list(input_string, &arg_list);
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
    i++;
  }

  TSNode type = ts_node_named_child(*node, i);
  print_typed_identifier(input_string, &type);

  if (!ts_node_is_null(def)) {
    print_expression(input_string, &def);
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

  if(sym == TUPLE_SQ) {
    print_tuple_sq(input_string, &child);
  } else if (sym == TUPLE) {
    print_tuple(input_string, &child);
  } else {
    printf("error in print attributes");
  }
}

void print_capture_list(char *input_string, TSNode *node) {
  printf("capture list"); // TODO: add print capture list
}
void print_tuple(char *input_string, TSNode *node) {
  putchar('('); 
  if(ts_node_child_count(*node) > 2) {
    TSNode list = ts_node_child(*node, 1);
    print_tuple_list(input_string, &list);
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
    case REF_IDENTIFIER:
      print_ref_identifier(input_string, node);
      break;
    case SIMPLE_ASSIGNMENT:
      print_simple_assignment(input_string, node);
      break;
    default:
      print_expression(input_string, node);
  }
}
void print_simple_assignment(char *input_string, TSNode *node) {
  TSNode decl = ts_node_child_by_field_name(*node, "decl", 4);
  if (!ts_node_is_null(decl)) {
    print_node_text(input_string, &decl);
    putchar(' ');
  }

  TSNode lvalue = ts_node_child_by_field_name(*node, "lvalue", 6);
  if (ts_node_grammar_symbol(lvalue) == IDENTIFIER) {
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
    case REF_IDENTIFIER:
      print_ref_identifier(input_string, &rvalue);
      break;
    default:
      print_expression(input_string, &rvalue);
  }
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

void print_match_expression(char *input_string, TSNode *node) {
  TSNode stmt_list = ts_node_child_by_field_name(*node, "stmt_list", 9);
  TSNode match_list = ts_node_child_by_field_name(*node, "match_list", 10);

  printf("match");
  print_stmt_list(input_string, &stmt_list);
  putchar('{');
  putchar(' '); // TODO: print match list
  putchar('}');
}
// clang -I tree-sitter/lib/include tree-sitter-pyrope/prpfmt/prpfmt.c tree-sitter-pyrope/src/parser.c tree-sitter-pyrope/src/scanner.c tree-sitter/libtree-sitter.a
