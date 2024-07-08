#include <string.h>
#include <stdio.h>
#include <tree_sitter/api.h>

#define ELIF 10
#define STMT_LIST 130
#define SCOPE_STMT 126
#define ELSE 11

bool depth_first_traversal(char *input_string, TSNode *node);
void print_node_info(char *input_string, TSNode *node); // Print node info
bool print_node_text(char *input_string, TSNode *node);
void print_children(char *input_string, TSNode *node);
char *file_to_string(char *path);
void print_if_expression(char *input_string, TSNode *node);
void print_stmt_list(char *input_string, TSNode *node);
void print_scope_stmt(char *input_string, TSNode *node);

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

  TSNode root_node = ts_tree_root_node(tree);
  
  depth_first_traversal(input_string, &root_node);

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

bool depth_first_traversal(char *input_string, TSNode *node) {
  //printf("%s\n", ts_node_type(*node));
  if (ts_node_grammar_symbol(*node)==131) {
    print_if_expression(input_string, node);
  }
  //print_node_info(input_string, node);
  uint32_t child_count = ts_node_child_count(*node);
  if (child_count > 0) {
    for (int child = 0; child < child_count; child++) {
      TSNode child_node = ts_node_child(*node, child);
      depth_first_traversal(input_string, &child_node);
    }
  } else {
    //print_node_info(input_string, ts_node_grammar_type(*node), ts_node_start_byte(*node), ts_node_end_byte(*node));
  }
  return true;
}

void print_children(char *input_string, TSNode *node) {
  for (int i = 0; i < ts_node_child_count(*node); i++) {
    printf("*");
    TSNode child = ts_node_child(*node, i);
    print_node_text(input_string, &child);
  }
}

void print_node_info(char *input_string, TSNode *node) {
  const char *type = ts_node_type(*node);
  char *str = ts_node_string(*node);
  printf("Type: %s\n", type);
  //printf("Grammar Type: %s\n", ts_node_grammar_type(*node));
  // printf("Grammar Symbol: %d\n", ts_node_grammar_symbol(*node));
  //printf("String: %s\n", str);
  printf("Named: %s\n", ts_node_is_named(*node)?"true":"false");
  printf("Symbol: %d\n", ts_node_symbol(*node));
  free(str);
  printf("\n----------------------------------------\n");
}

bool print_node_text(char *input_string, TSNode *node) {
  for (uint32_t i = ts_node_start_byte(*node); i <= ts_node_end_byte(*node); i++) {
    char t = input_string[i];
    //if(t != ' ' && t != '\t' && t != '\n') {
      putchar(t);
    //}
  }
  return true;
}

void print_if_expression(char *input_string, TSNode *node) {
  // Print useful information
  int child_count = ts_node_child_count(*node);
  printf("Child count: %d\n", child_count);
  printf("Named child count: %d\n", ts_node_named_child_count(*node));
  print_children(input_string, node);
  printf("\n");

  // Print if_expression
  printf("if ");
  TSNode condition = ts_node_child(*node, 1); 
  print_stmt_list(input_string, &condition); 
  TSNode code = ts_node_child(*node, 2);
  print_scope_stmt(input_string, &code);

  for(int i = 3; i < child_count; i++) {
    TSNode current_node = ts_node_child(*node, i);
    switch(ts_node_grammar_symbol(current_node)) {
      case ELIF:
        printf("elif ");
        break;
      case STMT_LIST:
        print_stmt_list(input_string, &current_node);
        break;
      case SCOPE_STMT:
        print_scope_stmt(input_string, &current_node);
        break;
      case ELSE:
        printf("else ");
        break;
    }
  }
}

void print_stmt_list(char *input_string, TSNode *node) {
  print_node_text(input_string, node); // TODO: Print formatted stmt_list 
}

void print_scope_stmt(char *input_string, TSNode *node) {
  print_node_text(input_string, node); // TODO: Print formatted scope_stmt
}
