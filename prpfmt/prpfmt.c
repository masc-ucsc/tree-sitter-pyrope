#include <string.h>
#include <stdio.h>
#include <tree_sitter/api.h>

bool depth_first_traversal(char *input_string, TSNode *node);
bool print_node(char *input_string, TSNode *node); // Print node info
char *file_to_string(char *path);
void print_if_expression(char *input_string, TSNode *node);
void print_stmt_list(char *input_string, TSNode *node);

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
  print_node(input_string, node);
  uint32_t child_count = ts_node_child_count(*node);
  if (child_count > 0) {
    for (int child = 0; child < child_count; child++) {
      TSNode child_node = ts_node_child(*node, child);
      depth_first_traversal(input_string, &child_node);
    }
  } else {
    //print_node(input_string, ts_node_grammar_type(*node), ts_node_start_byte(*node), ts_node_end_byte(*node));
  }
  return true;
}

bool print_node(char *input_string, TSNode *node) {
  const char *type = ts_node_type(*node);
  char *str = ts_node_string(*node);
  printf("Type: %s\n", type);
  //printf("Grammar Type: %s\n", ts_node_grammar_type(*node));
  // printf("Grammar Symbol: %d\n", ts_node_grammar_symbol(*node));
  //printf("String: %s\n", str);
  printf("Named: %d\n", ts_node_is_named(*node));
  printf("Symbol: %d\n", ts_node_symbol(*node));
  free(str);
  printf("Node text:\n");
  for (uint32_t i = ts_node_start_byte(*node); i <= ts_node_end_byte(*node); i++) {
    char t = input_string[i];
    //if(t != ' ' && t != '\t' && t != '\n') {
      putchar(t);
    //}
  }
  printf("\n----------------------------------------\n");
  return true;
}

void print_if_expression(char *input_string, TSNode *node) {
  printf("if ");
  TSNode condition = ts_node_child_by_field_name(*node, "condition", 9); 
  print_stmt_list(input_string, &condition); 
}

void print_stmt_list(char *input_string, TSNode *node) {
  printf("stmt_list"); // Placeholder
}
