#include <string.h>
#include <stdio.h>
#include <tree_sitter/api.h>

bool depth_first_traversal(TSNode *node);

int main(int argc, char **argv) {
  if (argc < 2)
    printf("Argument of type file path expected\n"), exit(1);

  char *buffer;

  FILE *fp = fopen(argv[1], "r"); 
  if (!fp) perror(argv[1]), exit(EXIT_FAILURE);

  fseek(fp, 0L, SEEK_END);
  long l_size = ftell(fp);
  rewind(fp);

  buffer = malloc(l_size + 1);
  if (!buffer) fclose(fp), fputs("Memory allocation fails", stderr), exit(1);
  
  if (1 != fread(buffer, l_size, 1, fp))
    fclose(fp), free(buffer), fputs("Read fails", stderr), exit(1);

  fclose(fp);

  printf("%s", buffer);
  
  // Parsing
  TSLanguage *tree_sitter_pyrope();
  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_pyrope());

  TSTree *tree = ts_parser_parse_string(
    parser, 
    NULL, 
    buffer, 
    strlen(buffer)
  );

  TSNode root_node = ts_tree_root_node(tree);
  
  depth_first_traversal(&root_node);

  ts_tree_delete(tree);
  ts_parser_delete(parser);
  free(buffer);
  return 0;
}

bool depth_first_traversal(TSNode *node) {
  printf("%s\n", ts_node_type(*node));
  for (int child = 0; child < ts_node_child_count(*node); child++) {
    TSNode child_node = ts_node_child(*node, child);
    depth_first_traversal(&child_node);
  }
  return true;
}
