#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <tree_sitter/api.h>

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
  TSNode child = ts_node_child(*node, 0);
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
    default:
      printf("reached default");
      //print_node_text(input_string, &child);
      break;
  }
}

void print_scope_statement(char *input_string, TSNode *node) {
  uint32_t named_child_count = ts_node_named_child_count(*node);
  printf ("{\n");
  for (uint32_t i = 0; i < named_child_count; i++) {
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
    // TODO: Print complex identifier
    TSNode type = ts_node_child_by_field_name(*node, "type", 4);
    if (!ts_node_is_null(type)) {
      // TODO: Print type cast
    }
  } 
  print_node_text(input_string, &lvalue);
  putchar(' ');

  TSNode operator = ts_node_child_by_field_name(*node, "operator", 8);
  print_node_text(input_string, &operator);
  putchar(' ');

  TSNode delay = ts_node_child_by_field_name(*node, "delay", 5);
  if (!ts_node_is_null(delay)) {
    print_node_text(input_string, &delay);
  }

  TSNode rvalue = ts_node_child_by_field_name(*node, "rvalue", 6);
  print_node_text(input_string, &rvalue);
  switch(ts_node_grammar_symbol(rvalue)) {
    
  }
  printf("\n");
}

void print_function_definition_statement(char *input_string, TSNode *node) {
  TSNode func_type = ts_node_child(*node, 0);
  print_node_text(input_string, &func_type);
  
  TSNode lvalue = ts_node_child(*node, 1);
  print_node_text(input_string, &lvalue);

  TSNode function_definition = ts_node_child(*node, 2);
  print_node_text(input_string, &function_definition); // TODO: add function
}

void print_function_call_statement(char *input_string, TSNode *node) {
  TSNode simple_function_call = ts_node_child(*node, 0);

  TSNode always = ts_node_child_by_field_name(simple_function_call, "always", 6);
  if (!ts_node_is_null(always)) {
    printf("always");
  }

  TSNode function = ts_node_child_by_field_name(simple_function_call, "function", 8);
  print_node_text(input_string, &function);

  TSNode argument = ts_node_child_by_field_name(simple_function_call, "argument", 8);
  print_node_text(input_string, &argument);
}

void print_if_expression(char *input_string, TSNode *node) {
  assert(!ts_node_is_null(*node));
  printf("print_if_expression()\n");

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
  print_scope_statement(input_string, &code);

  for(uint32_t i = 3; i < child_count; i++) {
    TSNode current_node = ts_node_child(*node, i);
    switch(ts_node_grammar_symbol(current_node)) {
      case ELIF:
        printf("elif ");
        break;
      case STMT_LIST:
        print_stmt_list(input_string, &current_node);
        break;
      case SCOPE_STMT:
        print_scope_statement(input_string, &current_node);
        break;
      case ELSE:
        printf("else ");
        break;
    }
  }
}

void print_while_statement(char *input_string, TSNode *node) {
  printf("while ");
  TSNode condition = ts_node_child(*node, 1);
  print_stmt_list(input_string, &condition);
  TSNode scope_stmt = ts_node_child(*node, 2);
  print_scope_statement(input_string, &scope_stmt);
}

void print_for_statement(char *input_string, TSNode *node) {
  //printf("child count: %d\n", ts_node_named_child_count(*node));
  printf("for ");

  TSNode index = ts_node_named_child(*node, 0);
  print_node_text(input_string, &index);
  
  printf(" in ");

  TSNode data = ts_node_named_child(*node, 1);
  print_node_text(input_string, &data);

  TSNode scope = ts_node_named_child(*node, 2);
  print_scope_statement(input_string, &scope);
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
  TSSymbol symbol = ts_node_grammar_symbol(child);
  //printf("symbol %d\n", symbol);

  switch(symbol) {
    case SCOPE_EXPRESSION:
      print_scope_statement(input_string, &child); 
      break;
    case COMPLEX_IDENTIFIER:
      print_complex_identifier(input_string, &child);
  }
}

void print_stmt_list(char *input_string, TSNode *node) {
  assert(!ts_node_is_null(*node));
  //printf("printingstmtlist");
  print_node_text(input_string, node); // TODO: Print formatted stmt_list 
}

void print_complex_identifier(char *input_string, TSNode *node) {
  uint32_t start_byte = ts_node_start_byte(*node);
  uint32_t end_byte = ts_node_end_byte(*node);
  
  if (input_string[start_byte] == '`') {
    print_node_text_with_whitespace(input_string, node);
  } else {
    print_node_text(input_string, node);
  }

  // Add dot_expression and selection
}
// clang -I tree-sitter/lib/include tree-sitter-pyrope/prpfmt/prpfmt.c tree-sitter-pyrope/src/parser.c tree-sitter-pyrope/src/scanner.c tree-sitter/libtree-sitter.a
