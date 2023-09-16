#include <string.h>
#include <stdio.h>
#include <tree_sitter/api.h>

int file_to_string(char *fp, char *string);

int main(int argc, char **argv) {
   // Open prp file and assign to string
   if (argc < 2) 
      printf("Argument of type file path expected\n"), exit(1);

   char *buffer;

   FILE *fp = fopen(argv[1], "r");
   if (!fp) perror(argv[1]), exit(EXIT_FAILURE);

   fseek(fp, 0L, SEEK_END);
   long lSize = ftell(fp);
   rewind(fp);

   buffer = malloc(lSize + 1);
   if (!buffer) fclose(fp), fputs("memory allocation fails", stderr), exit(1);

   if (1 != fread(buffer, lSize, 1, fp))
      fclose(fp), free(buffer), fputs("read fails", stderr), exit(1);
   
   fclose(fp);

   printf("%s", buffer);

   // Parse string 
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

   char *string = ts_node_string(root_node);

   printf("%s\n", string);

   ts_tree_delete(tree);
   ts_parser_delete(parser);
   free(buffer);
   return 0;
}

