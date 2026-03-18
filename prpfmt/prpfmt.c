#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

#include "prpfmt.h"

/******************************************************************************
 * 1. Entry & High-Level Dispatch
 * Responsibilities: Initiates tree traversal, manages high-level statement dispatch, 
 * and handles global indentation/formatting state directives.
 ******************************************************************************/

void print_tree(TSTree *tree, PrpfmtState *st) {
  // Get root and child info
  TSNode root_node = ts_tree_root_node(tree);
  uint32_t root_child_count = ts_node_child_count(root_node);

  TSNode prev_child;
  bool has_prev = false;

  // Iterate over children
  for (uint32_t i = 0; i < root_child_count; i++) {
    TSNode child = ts_node_child(root_node, i);

    // Preserve newlines from original source code
    if (has_prev) {
      TSPoint prev_end = ts_node_end_point(prev_child);
      TSPoint curr_start = ts_node_start_point(child);

      if (curr_start.row > prev_end.row + 1) {
        uint32_t blank_lines = curr_start.row - prev_end.row - 1;
        for (uint32_t j = 0; j < blank_lines; j++) {
          fprintf(st->outfile, "\n");
        }
      }
    }

    print_statement(child, st, false);
    prev_child = child;
    has_prev = true;
  }
}

void print_statement(TSNode node, PrpfmtState *st, bool is_inline) {
  // Print comment if node is comment
  if (ts_node_grammar_symbol(node) == sym_comment) {
    print_comment(node, st);
    return;
  }

  // Print original text if prpfmt off
  if (!st->fmt_on) {
    char *node_text = get_node_text(node, st->source_code);
    if (node_text) {
      print_indent(st);
      fprintf(st->outfile, "%s\n", node_text);
      free(node_text);
    }
    return;
  }

  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Determine if first child (except if scope) on same line as previous sibling
    if (i == 0 && symbol != sym_scope_statement) {
      TSNode prev_sib = ts_node_prev_sibling(node);
      // Indent if no previous siblings
      if (ts_node_is_null(prev_sib)) {
        print_indent(st);
      } else {
        TSSymbol prev_sym = ts_node_grammar_symbol(prev_sib);
        TSPoint prev_end = ts_node_end_point(prev_sib);
        TSPoint curr_start = ts_node_start_point(node);
        // Indent if not on the same row as previous sibling
        if (curr_start.row > prev_end.row || (prev_sym == anon_sym_LBRACE && !st->inline_exp)) {
          print_indent(st);
        }
      }
    }

    // Dispatch to proper function
    switch (symbol) {
      case sym_assignment_or_declaration_statement:
        print_assignment_or_declaration_statement(child, st);
        break;
      case sym_control_statement:
        print_control_statement(child, st);
        break;
      case sym_declaration_statement:
        print_declaration_statement(child, st);
        break;
      case sym_enum_assignment_statement:
        print_enum_assignment_statement(child, st);
        break;
      case sym_expression_statement:
        print_expression_statement(child, st, is_inline);
        break;
      case sym_for_statement:
        print_for_statement(child, st);
        break;
      case sym_function_call_statement:
        print_function_call_statement(child, st);
        break;
      case sym_impl_statement:
        print_impl_statement(child, st);
        break;
      case sym_import_statement:
        print_import_statement(child, st);
        break;
      case sym_lambda:
        print_lambda(child, st);
        break;
      case sym_loop_statement:
        print_loop_statement(child, st);
        break;
      case sym_scope_statement:
        print_scope_statement(child, st, is_inline);
        break;
      case sym_test_statement:
        print_test_statement(child, st);
        break;
      case sym_type_statement:
        print_type_statement(child, st);
        break;
      case sym_while_statement:
        print_while_statement(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }

  // Check if next sibling is on same line
  TSNode next = ts_node_next_sibling(node);
  if (!ts_node_is_null(next)) {
    TSPoint end = ts_node_end_point(node);
    TSPoint nstart = ts_node_start_point(next);
    TSSymbol next_sym = ts_node_grammar_symbol(next);
    if (end.row == nstart.row) {
      if (next_sym == sym_comment) {
        return;
      }
      // Print space if next sibling on same line
      if (next_sym != anon_sym_RBRACE || st->inline_exp) {
        fprintf(st->outfile, " ");
        return;
      }
    }
  }
  // Add newline if no next sibling
  if (!(st->inline_exp)) {
    fprintf(st->outfile, "\n");
  }
}

void print_indent(PrpfmtState *st) {
  // Print spaces for indentation formatting
  for (int i = 0; i < st->indent_level * st->indent_size; i++) {
    fprintf(st->outfile, " ");
  }
}

void check_format_directives(const char *node_text, PrpfmtState *st) {
  // Update state if format directives applied
  if (strstr(node_text, "prpfmt off")) {
    st->fmt_on = false;
  } else if (strstr(node_text, "prpfmt on")) {
    st->fmt_on = true;
  }
}

/******************************************************************************
 * 2. Structural (Scopes, Lists, Tuples)
 * Responsibilities: Manages code blocks and collection delimiters. Ensures 
 * indentation level changes for nested scopes and proper spacing for lists.
 ******************************************************************************/

void print_scope_statement(TSNode node, PrpfmtState *st, bool is_inline) {
  // Get first/last child info
  uint32_t child_count = ts_node_child_count(node);
  TSNode first = ts_node_child(node, 0);
  TSNode last = ts_node_child(node, child_count - 1);

  bool single_line = false;

  // Determine if body fits on a single line (based on input)
  if (!ts_node_is_null(first) && !ts_node_is_null(last)) {
    if (ts_node_start_point(first).row == ts_node_end_point(last).row) {
      single_line = true;
    }
  }

  // Iterate over children
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSNode next = ts_node_next_sibling(child);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_LBRACE:
        // Add space after '{' if single_line or comment (handled by print_comment), '\n' otherwise
        if (is_inline || single_line) { 
          fprintf(st->outfile, "{ ");
        } else if (!ts_node_is_null(next) && ts_node_grammar_symbol(next) == sym_comment) {
          fprintf(st->outfile, "{");
          st->indent_level++;
        } else {
          fprintf(st->outfile, "{\n");
          st->indent_level++;
        }
        break;
      case sym_statement:
        if (is_inline || single_line) {
          // Temporarily disable indentation for inline scope
          int old_level = st->indent_level;
          st->indent_level = 0;
          bool old_inline = st->inline_exp;
          st->inline_exp = true;

          print_statement(child, st, true);

          // Restore indentation settings
          st->indent_level = old_level;
          st->inline_exp = old_inline;
        } else {
          print_statement(child, st, false);
        }
        break;
      case anon_sym_RBRACE:
        // Decrement indent level if closing a non-inline scope
        if (is_inline || single_line) {
          fprintf(st->outfile, "}");
        } else {
          st->indent_level--;
          print_indent(st);
          fprintf(st->outfile, "}");
        }
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_stmt_list(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_SEMI:
        // Print space after semicolon if more children exist
        fprintf(st->outfile, " ;");
        if (i < child_count - 1) {
          fprintf(st->outfile, " ");
        }
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        print__tuple_item(child, st);
        break;
    }
  }
}

void print_tuple(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_LPAREN:
        fprintf(st->outfile, "(");
        break;
      case anon_sym_RPAREN:
        fprintf(st->outfile, ")");
        break;
      case sym_tuple_list:
        print_tuple_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_tuple_sq(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_LBRACK:
        fprintf(st->outfile, "[");
        break;
      case anon_sym_RBRACK:
        fprintf(st->outfile, "]");
        break;
      case sym_tuple_list:
        print_tuple_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_tuple_list(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_COMMA:
        fprintf(st->outfile, ", ");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        print__tuple_item(child, st);
        break;
    }
  }
}

void print__tuple_item(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Dispatch to proper function
  switch (symbol) {
    case sym_ref_identifier:
      print_ref_identifier(node, st);
      break;
    case sym_assignment:
      print_assignment(node, st, false);
      break;
    case sym_typed_declaration:
      print_typed_declaration(node, st);
      break;
    case sym_lambda:
      print_lambda(node, st);
      break;
    default:
      print__expression_with_comprehension(node, st, true);
      break;
  }
}

/******************************************************************************
 * 3. Control Flow
 * Responsibilities: Formats branching and looping constructs (if, match, for, while). 
 * Manages keyword spacing and conditional clause alignment.
 ******************************************************************************/

void print_if_expression(TSNode node, PrpfmtState *st, bool is_inline) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *field_name = ts_node_field_name_for_child(node, i);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "condition") == 0) {
        fprintf(st->outfile, " ");
        print__expression(child, st, is_inline);
      } else if (strcmp(field_name, "code") == 0 || strcmp(field_name, "else") == 0 || strcmp(field_name, "elif") == 0) {
        if (symbol == anon_sym_elif) {
          fprintf(st->outfile, " elif");
        } else if (symbol == anon_sym_else) {
          fprintf(st->outfile, " else");
        } else if (symbol == sym_scope_statement) {
          fprintf(st->outfile, " ");
          print_scope_statement(child, st, is_inline);
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);
            const char *fn2 = ts_node_field_name_for_child(child, j);

            // Handle nested nodes
            if (s2 == anon_sym_elif) {
              fprintf(st->outfile, " elif");
            } else if (s2 == anon_sym_else) {
              fprintf(st->outfile, " else");
            } else if (s2 == anon_sym_if) {
              fprintf(st->outfile, " if");
            } else if (fn2 && strcmp(fn2, "condition") == 0) {
              fprintf(st->outfile, " ");
              print__expression(c2, st, is_inline);
            } else if (fn2 && strcmp(fn2, "code") == 0) {
              fprintf(st->outfile, " ");
              print_scope_statement(c2, st, is_inline);
            } else if (s2 == sym_scope_statement) {
              fprintf(st->outfile, " ");
              print_scope_statement(c2, st, is_inline);
            }
          }
        }
      }
      continue;
    }
     
    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_unique:
        fprintf(st->outfile, "unique ");
        break;
      case anon_sym_if:
        fprintf(st->outfile, "if");
        break;
      case sym_stmt_list:
        fprintf(st->outfile, " ");
        print_stmt_list(child, st);
        break;
      case anon_sym_SEMI:
        fprintf(st->outfile, " ;");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_match_expression(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "init") == 0) {
        if (symbol == sym_stmt_list) {
          print_stmt_list(child, st);
        } else if (symbol == anon_sym_SEMI) {
          fprintf(st->outfile, "; ");
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == sym_stmt_list) {
              print_stmt_list(c2, st);
            } else if (s2 == anon_sym_SEMI) {
              fprintf(st->outfile, "; ");
            }
          }
        }
      } else if (strcmp(field_name, "condition") == 0) {
        fprintf(st->outfile, " ");
        print__expression(child, st, true);
      } else if (strcmp(field_name, "match_list") == 0) {
        print_match_list(child, st);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_match:
        fprintf(st->outfile, "match");
        break;
      case anon_sym_LBRACE:
        fprintf(st->outfile, " {\n");
        st->indent_level++;
        break;
      case anon_sym_RBRACE:
        st->indent_level--;
        print_indent(st);
        fprintf(st->outfile, "}");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_match_list(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);
  bool arm_started = false;

  // Iterate over children
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "condition") == 0) {
        if (!arm_started) {
          print_indent(st);
          arm_started = true;
        }
        if (symbol == anon_sym_else) {
          fprintf(st->outfile, "else");
        } else if (symbol == sym_match_operator) {
          print_match_operator(child, st);
          fprintf(st->outfile, " ");
        } else if (symbol == sym_expression_list) {
          print_expression_list(child, st);
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == sym_match_operator) {
              print_match_operator(c2, st);
              fprintf(st->outfile, " ");
            } else if (s2 == sym_expression_list) {
              print_expression_list(c2, st);
            }
          }
        }
      }
      if (strcmp(field_name, "code") == 0) {
        fprintf(st->outfile, " ");
        print_scope_statement(child, st, false);

        fprintf(st->outfile, "\n");
        arm_started = false;
      }
    } else if (symbol == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_match_operator(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_for_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "attributes") == 0) {
        if (symbol == sym_attribute_list) {
          fprintf(st->outfile, "::");
          print_attribute_list(child, st);
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == anon_sym_COLON_COLON) {
              fprintf(st->outfile, "::");
            } else if (s2 == sym_attribute_list) {
              print_attribute_list(c2, st);
            }
          }
        }
      } else if (strcmp(field_name, "init") == 0) {
        if (symbol == sym_stmt_list) {
          print_stmt_list(child, st);
        } else if (symbol == anon_sym_SEMI) {
          fprintf(st->outfile, "; ");
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == sym_stmt_list) {
              print_stmt_list(c2, st);
            } else if (s2 == anon_sym_SEMI) {
              fprintf(st->outfile, "; ");
            }
          }
        }
      } else if (strcmp(field_name, "index") == 0) {
        if (symbol == sym_typed_identifier_list) {
          print_typed_identifier_list(child, st);
        } else {
          print_typed_identifier(child, st);
        }
      } else if (strcmp(field_name, "data") == 0) {
        print_expression_list(child, st);
      } else if (strcmp(field_name, "code") == 0) {
        fprintf(st->outfile, " ");
        print_scope_statement(child, st, false);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_for:
        fprintf(st->outfile, "for ");
        break;
      case anon_sym_LPAREN:
        fprintf(st->outfile, "(");
        break;
      case anon_sym_RPAREN:
        fprintf(st->outfile, ")");
        break;
      case anon_sym_in:
        fprintf(st->outfile, " in ");
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_ref_identifier:
        print_ref_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_while_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "attributes") == 0) {
        if (symbol == sym_attribute_list) {
          fprintf(st->outfile, "::");
          print_attribute_list(child, st);
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == anon_sym_COLON_COLON) {
              fprintf(st->outfile, "::");
            } else if (s2 == sym_attribute_list) {
              print_attribute_list(c2, st);
            }
          }
        }
      } else if (strcmp(field_name, "init") == 0) {
        if (symbol == sym_stmt_list) {
          print_stmt_list(child, st);
        } else if (symbol == anon_sym_SEMI) {
          fprintf(st->outfile, "; ");
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == sym_stmt_list) {
              print_stmt_list(c2, st);
            } else if (s2 == anon_sym_SEMI) {
              fprintf(st->outfile, "; ");
            }
          }
        }
      } else if (strcmp(field_name, "condition") == 0) {
        fprintf(st->outfile, " ");
        print__expression(child, st, true);
      } else if (strcmp(field_name, "code") == 0) {
        fprintf(st->outfile, " ");
        print_scope_statement(child, st, false);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_while:
        fprintf(st->outfile, "while ");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_loop_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "attributes") == 0) {
        if (symbol == sym_attribute_list) {
          fprintf(st->outfile, "::");
          print_attribute_list(child, st);
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == anon_sym_COLON_COLON) {
              fprintf(st->outfile, "::");
            } else if (s2 == sym_attribute_list) {
              print_attribute_list(c2, st);
            }
          }
        }
      } else if (strcmp(field_name, "init") == 0) {
        print_stmt_list(child, st);
      } else if (strcmp(field_name, "code") == 0) {
        fprintf(st->outfile, " ");
        print_scope_statement(child, st, false);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_loop:
        fprintf(st->outfile, "loop ");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_control_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *field_name = ts_node_field_name_for_child(node, i);

    // Format named nodes
    if (field_name && strcmp(field_name, "argument") == 0) {
      fprintf(st->outfile, " ");
      print__expression_with_comprehension(child, st, true);
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_continue:
        fprintf(st->outfile, "continue");
        break;
      case anon_sym_break:
        fprintf(st->outfile, "break");
        break;
      case anon_sym_return:
        fprintf(st->outfile, "return");
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_when_unless_cond(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *field_name = ts_node_field_name_for_child(node, i);

    // Format named nodes
    if (field_name && strcmp(field_name, "condition") == 0) {
      fprintf(st->outfile, " ");
      print__expression(child, st, true);
      continue;
    }

    // Format anonymous nodes
    if (symbol == anon_sym_when) {
      fprintf(st->outfile, "when");
    } else if (symbol == anon_sym_unless) {
      fprintf(st->outfile, "unless");
    }
  }
}

/******************************************************************************
 * 4. Assignments & Declarations
 * Responsibilities: Formats variable declarations and value assignments. Handles 
 * spacing around operators (=, :=) and alignment of L-values/R-values.
 ******************************************************************************/

void print_assignment(TSNode node, PrpfmtState *st, bool spaces) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *field_name = ts_node_field_name_for_child(node, i);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "decl") == 0) {
        print_var_or_let_or_reg(child, st);
        fprintf(st->outfile, " ");
      } else if (strcmp(field_name, "lvalue") == 0) {
        if (symbol == sym_lvalue_list) {
          print_lvalue_list(child, st);
        } else if (symbol == sym_typed_identifier) {
          print_typed_identifier(child, st);
        } else if (symbol == sym_complex_identifier) {
          print_complex_identifier(child, st);
        }
      } else if (strcmp(field_name, "type") == 0) {
        print_type_cast(child, st);
      } else if (strcmp(field_name, "operator") == 0) {
        print_assignment_operator(child, st, spaces);
      } else if (strcmp(field_name, "delay") == 0) {
        print_assignment_delay(child, st);
      } else if (strcmp(field_name, "rvalue") == 0) {
        if (symbol == sym_enum_definition) {
          print_enum_definition(child, st);
        } else if (symbol == sym_ref_identifier) {
          print_ref_identifier(child, st);
        } else {
          print__expression_with_comprehension(child, st, true);
        }
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_LPAREN:
        fprintf(st->outfile, "(");
        break;
      case anon_sym_RPAREN:
        fprintf(st->outfile, ")");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_lvalue_item(TSNode node, PrpfmtState *st) {
  // Print directly if no children
  uint32_t child_count = ts_node_child_count(node);
  if (child_count == 0) {
    char *text = get_node_text(node, st->source_code);
    if (text) {
      fprintf(st->outfile, "%s", text);
      free(text);
    }
    return;
  }

  // Iterate over children
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "identifier") == 0) {
        print_complex_identifier(child, st);
      } else if (strcmp(field_name, "type") == 0) {
        print_type_cast(child, st);
      }
      continue;
    }

    // Dispatch to proper function
    switch (symbol) {
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_lvalue_list(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_COMMA:
        fprintf(st->outfile, ", ");
        break;
      case sym_lvalue_item:
        print_lvalue_item(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_assignment_or_declaration_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_assignment:
        print_assignment(child, st, true);
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_declaration_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "decl") == 0) {
        print_var_or_let_or_reg(child, st);
        fprintf(st->outfile, " ");
      } else if (strcmp(field_name, "lvalue") == 0) {
        if (symbol == sym_typed_identifier_list) {
          print_typed_identifier_list(child, st);
        } else {
          print_typed_identifier(child, st);
        }
      }
      continue;
    }
 
    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_LPAREN:
        fprintf(st->outfile, "(");
        break;
      case anon_sym_RPAREN:
        fprintf(st->outfile, ")");
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_enum_assignment_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_enum_assignment:
        print_enum_assignment(child, st);
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_enum_assignment(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "name") == 0) {
        print_identifier(child, st);
      } else if (strcmp(field_name, "values") == 0) {
        print_tuple(child, st);
      } else if (strcmp(field_name, "body") == 0) {
        print_arg_list(child, st);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_enum:
        fprintf(st->outfile, "enum ");
        break;
      case anon_sym_variant:
        fprintf(st->outfile, "variant ");
        break;
      case anon_sym_EQ:
        fprintf(st->outfile, " = ");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_enum_definition(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name && strcmp(field_name, "input") == 0) {
      print_arg_list(child, st);
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_enum:
        fprintf(st->outfile, "enum ");
        break;
      case anon_sym_variant:
        fprintf(st->outfile, "variant ");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_typed_declaration(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "decl") == 0) {
        print_var_or_let_or_reg(child, st);
        fprintf(st->outfile, " ");
      } else if (strcmp(field_name, "lvalue") == 0) {
        print_typed_identifier(child, st);
      }
    } else if (symbol == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_assignment_operator(TSNode node, PrpfmtState *st, bool spaces) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    if (spaces) {
      fprintf(st->outfile, " %s ", text);
    } else {
      fprintf(st->outfile, "%s", text);
    }
    free(text);
  }
}

void print_assignment_delay(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_delay_tok:
        print_delay_tok(child, st);
        break;
      case anon_sym_AT:
        fprintf(st->outfile, "@");
        break;
      case anon_sym_LBRACK:
        fprintf(st->outfile, "[");
        break;
      case anon_sym_RBRACK:
        fprintf(st->outfile, "]");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        print__expression(child, st, true);
        break;
    }
  }
}

/******************************************************************************
 * 5. Functions & Parameters
 * Responsibilities: Formats function definitions, lambdas, and call sites. 
 * Manages generic parameters, capture lists, and argument list spacing.
 ******************************************************************************/

void print_lambda(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);
  bool has_name = false;

  // Iterate over children
  for (uint32_t i = 0; i < child_count; i++) {
    // Check if lambda has name
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn && strcmp(fn, "name") == 0) {
      has_name = true;
      break;
    }

    TSNode child = ts_node_child(node, i);
    if (ts_node_grammar_symbol(child) == sym_function_definition_decl) {
      // Iterate over children
      uint32_t cc2 = ts_node_child_count(child);
      for (uint32_t j = 0; j < cc2; j++) {

        // Handle nested nodes
        const char *fn2 = ts_node_field_name_for_child(child, j);
        if (fn2 && strcmp(fn2, "name") == 0) {
          has_name = true;
          break;
        }
      }
    }
    if (has_name) {
      break;
    }
  }

  // Iterate over children
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "func_type") == 0) {
        switch (symbol) {
          case sym_comb_tok:
            print_comb_tok(child, st);
            break;
          case sym_pipe_tok:
            print_pipe_tok(child, st);
            break;
          case sym_flow_tok:
            print_flow_tok(child, st);
            break;
        }

        if (has_name) {
          fprintf(st->outfile, " ");
        }
      } else if (strcmp(field_name, "name") == 0) {
        print_identifier(child, st);
      } else if (strcmp(field_name, "code") == 0) {
        fprintf(st->outfile, " ");
        print_scope_statement(child, st, false);
      }
    } else if (symbol == sym_function_definition_decl) {
      print_function_definition_decl(child, st);
    }
  }
}

void print_function_definition(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "lvalue") == 0) {
        print_complex_identifier(child, st);
      } else if (strcmp(field_name, "condition") == 0) {
        fprintf(st->outfile, " where ");
        print_expression_list(child, st);
      } else if (strcmp(field_name, "verification") == 0) {
        print_func_def_verification(child, st);
      } else if (strcmp(field_name, "code") == 0) {
        fprintf(st->outfile, " ");
        print_scope_statement(child, st, false);
      }
    } else if (symbol == sym_function_definition_decl) {
      print_function_definition_decl(child, st);
    }
  }
}

void print_function_definition_decl(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "generic") == 0) {
        if (symbol == sym_typed_identifier_list) {
          fprintf(st->outfile, "<");
          print_typed_identifier_list(child, st);
          fprintf(st->outfile, ">");
        } else if (symbol == sym_arg_item_list) {
          fprintf(st->outfile, "<");
          print_arg_item_list(child, st);
          fprintf(st->outfile, ">");
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == anon_sym_LT) {
              fprintf(st->outfile, "<");
            } else if (s2 == anon_sym_GT) {
              fprintf(st->outfile, ">");
            } else if (s2 == sym_typed_identifier_list) {
              print_typed_identifier_list(c2, st);
            } else if (s2 == sym_arg_item_list) {
              print_arg_item_list(c2, st);
            }
          }
        }
      } else if (strcmp(field_name, "capture") == 0) {
        if (symbol == sym_typed_identifier_list) {
          fprintf(st->outfile, "[");
          print_typed_identifier_list(child, st);
          fprintf(st->outfile, "]");
        } else if (symbol == sym_arg_item_list) {
          fprintf(st->outfile, "[");
          print_arg_item_list(child, st);
          fprintf(st->outfile, "]");
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == anon_sym_LBRACK) {
              fprintf(st->outfile, "[");
            } else if (s2 == anon_sym_RBRACK) {
              fprintf(st->outfile, "]");
            } else if (s2 == sym_typed_identifier_list) {
              print_typed_identifier_list(c2, st);
            } else if (s2 == sym_arg_item_list) {
              print_arg_item_list(c2, st);
            }
          }
        }
      } else if (strcmp(field_name, "pipe_config") == 0) {
        if (symbol == sym_attribute_list) {
          fprintf(st->outfile, "::");
          print_attribute_list(child, st);
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == anon_sym_COLON_COLON) {
              fprintf(st->outfile, "::");
            } else if (s2 == sym_attribute_list) {
              print_attribute_list(c2, st);
            }
          }
        }
      } else if (strcmp(field_name, "input") == 0) {
        if (symbol == sym_arg_list) {
          print_arg_list(child, st);
        } else if (symbol == sym_arg_item_list) {
          fprintf(st->outfile, "(");
          print_arg_item_list(child, st);
          fprintf(st->outfile, ")");
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == anon_sym_LPAREN) {
              fprintf(st->outfile, "(");
            } else if (s2 == anon_sym_RPAREN) {
              fprintf(st->outfile, ")");
            } else if (s2 == sym_arg_list) {
              print_arg_list(c2, st);
            } else if (s2 == sym_arg_item_list) {
              print_arg_item_list(c2, st);
            }
          }
        }
      } else if (strcmp(field_name, "output") == 0) {
        if (symbol == sym_arg_list) {
          print_arg_list(child, st);
        } else if (symbol == sym_type_or_identifier) {
          print_type_or_identifier(child, st);
        } else {
          // Handle cases like bool
          char *text = get_node_text(child, st->source_code);
          if (text) {
            fprintf(st->outfile, "%s", text);
            free(text);
          }
        }
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_LT:
        fprintf(st->outfile, "<");
        break;
      case anon_sym_GT:
        fprintf(st->outfile, ">");
        break;
      case anon_sym_LBRACK:
        fprintf(st->outfile, "[");
        break;
      case anon_sym_RBRACK:
        fprintf(st->outfile, "]");
        break;
      case anon_sym_DASH_GT:
        fprintf(st->outfile, " -> ");
        break;
      case sym_typed_identifier_list:
        print_typed_identifier_list(child, st);
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case sym_type_or_identifier:
        print_type_or_identifier(child, st);
        break;
    }
  }
}

void print_func_def_verification(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Print if child has no children
    if (ts_node_child_count(child) == 0) {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        fprintf(st->outfile, " %s ", text);
        free(text);
      }
    } else {
      print__expression(child, st, true);
    }
  }
}

void print_arg_list(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_LPAREN:
        fprintf(st->outfile, "(");
        break;
      case anon_sym_RPAREN:
        fprintf(st->outfile, ")");
        break;
      case sym_arg_item_list:
        print_arg_item_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_arg_item_list(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_COMMA:
        fprintf(st->outfile, ", ");
        break;
      case sym_arg_item:
        print_arg_item(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_arg_item(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "mod") == 0) {
        char *text = get_node_text(child, st->source_code);
        if (text) {
          fprintf(st->outfile, "%s ", text);
          free(text);
        }
      } else if (strcmp(field_name, "definition") == 0) {
        if (symbol == anon_sym_EQ) {
          fprintf(st->outfile, " = ");
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          if (cc2 > 0) {
            for (uint32_t j = 0; j < cc2; j++) {
              TSNode c2 = ts_node_child(child, j);
              TSSymbol s2 = ts_node_grammar_symbol(c2);

              // Handle nested nodes
              if (s2 == anon_sym_EQ) {
                fprintf(st->outfile, " = ");
              } else {
                print__expression_with_comprehension(c2, st, true);
              }
            }
          } else {
            // Already handled by anon_sym_EQ or we just print the expression if it's direct
            if (symbol != anon_sym_EQ) {
              print__expression_with_comprehension(child, st, true);
            }
          }
        }
      }
    } else if (symbol == sym_typed_identifier) {
      print_typed_identifier(child, st);
    } else if (symbol == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_function_call_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *field_name = ts_node_field_name_for_child(node, i);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "function") == 0) {
        print_complex_identifier(child, st);
      } else if (strcmp(field_name, "argument") == 0) {
        fprintf(st->outfile, " ");
        print_expression_list(child, st);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_function_call_expression(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "function") == 0) {
        print_complex_identifier(child, st);
      } else if (strcmp(field_name, "argument") == 0) {
        print_tuple(child, st);
      }
    } else if (symbol == sym_comment) {
      print_comment(child, st);
    }
  }
}

/******************************************************************************
 * 6. Expressions & Selection
 * Responsibilities: Formats complex expressions, including binary/unary operations, 
 * dot access, type casts, and member/bit selection.
 ******************************************************************************/

void print_expression_statement(TSNode node, PrpfmtState *st, bool is_inline) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

  // Dispatch to proper function
    switch (symbol) {
      case sym_for_comprehension:
        print_for_comprehension(child, st);
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        print__expression_with_comprehension(child, st, is_inline);
        break;
    }
  }
}

void print__expression(TSNode node, PrpfmtState *st, bool is_inline) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Dispatch to proper function
  switch (symbol) {
    case sym_binary_expression:
      print_binary_expression(node, st);
      break;
    case sym_if_expression:
      print_if_expression(node, st, is_inline);
      break;
    case sym_match_expression:
      print_match_expression(node, st);
      break;
    case sym_type_specification:
      print_type_specification(node, st);
      break;
    case sym_unary_expression:
      print_unary_expression(node, st);
      break;
    case sym_complex_identifier:
    case sym_constant:
    case sym_function_call_expression:
    case sym_lambda:
    case sym_tuple:
    case sym_optional_expression:
    case sym__restricted_expression:
    default:
      print__restricted_expression(node, st);
      break;
  }
}

void print__expression_with_comprehension(TSNode node, PrpfmtState *st, bool is_inline) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Dispatch to proper function
  switch (symbol) {
    case sym_for_comprehension:
      print_for_comprehension(node, st);
      break;
    default:
      print__expression(node, st, is_inline);
      break;
  }
}

void print__restricted_expression(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Dispatch to proper function
  switch (symbol) {
    case sym_complex_identifier:
      print_complex_identifier(node, st);
      break;
    case sym_constant:
      print_constant(node, st);
      break;
    case sym_function_call_expression:
      print_function_call_expression(node, st);
      break;
    case sym_lambda:
      print_lambda(node, st);
      break;
    case sym_tuple:
      print_tuple(node, st);
      break;
    case sym_optional_expression:
      print_optional_expression(node, st);
      break;
    case sym_if_expression:
      print_if_expression(node, st, true);
      break;
    case sym_match_expression:
      print_match_expression(node, st);
      break;
    default:
      // Fallback: print node text if it's a leaf or we don't have a specific handler
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          fprintf(st->outfile, "%s", text);
          free(text);
        }
      }
      break;
  }
}

void print_binary_expression(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "left") == 0 || strcmp(field_name, "right") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(field_name, "operator") == 0) {
        TSSymbol symbol = ts_node_grammar_symbol(child);
        char *text = get_node_text(child, st->source_code);

        if (text) {
          switch (symbol) {
            // High precedence binary operators 
            case anon_sym_TILDE:
            case anon_sym_STAR:
            case anon_sym_SLASH:
              fprintf(st->outfile, "%s", text);
              break;
            default:
              fprintf(st->outfile, " %s ", text);
              break;
          }
          free(text);
        }
      }
      continue;
    }
    
    if (ts_node_child_count(child) == 0) {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        fprintf(st->outfile, "%s", text);
        free(text);
      }
    }
  }
}

void print_unary_expression(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "operator") == 0) {
        char *text = get_node_text(child, st->source_code);
        if (text) {
          fprintf(st->outfile, "%s", text);
          if (strcmp(text, "not") == 0) {
            fprintf(st->outfile, " ");
          }
          free(text);
        }
      } else if (strcmp(field_name, "argument") == 0) {
        print__expression(child, st, true);
      }
    }
  }
}

void print_dot_expression(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    if (symbol == anon_sym_DOT) {
      fprintf(st->outfile, ".");
    } else if (symbol == sym_identifier) {
      print_identifier(child, st);
    } else if (symbol == sym_constant) {
      print_constant(child, st);
    } else {
      print__restricted_expression(child, st);
    }
  }
}

void print_optional_expression(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "argument") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(field_name, "operator") == 0) {
        fprintf(st->outfile, "?");
      }
    }
  }
}

void print_type_specification(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "argument") == 0) {
        print__restricted_expression(child, st);
      } else if (strcmp(field_name, "type") == 0) {
        print__type(child, st);
      } else if (strcmp(field_name, "attribute") == 0) {
        print_attributes(child, st);
      }
    } else if (symbol == anon_sym_COLON) {
      fprintf(st->outfile, ":");
    }
  }
}

void print_type_cast(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "type") == 0) {
        print__type(child, st);
      } else if (strcmp(field_name, "attribute") == 0) {
        print_attributes(child, st);
      }
    } else if (symbol == anon_sym_COLON) {
      fprintf(st->outfile, ":");
    }
  }
}

void print_expression_list(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    
    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_COMMA:
        fprintf(st->outfile, ", ");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        print__expression(child, st, true);
        break;
    }
  }
}

void print_for_comprehension(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "index") == 0) {
        if (symbol == sym_typed_identifier_list) {
          print_typed_identifier_list(child, st);
        } else {
          print_typed_identifier(child, st);
        }
      } else if (strcmp(field_name, "data") == 0) {
        print_expression_list(child, st);
      } else if (strcmp(field_name, "condition") == 0) {
        print_stmt_list(child, st);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_for:
        fprintf(st->outfile, "for ");
        break;
      case anon_sym_LPAREN:
        fprintf(st->outfile, "(");
        break;
      case anon_sym_RPAREN:
        fprintf(st->outfile, ")");
        break;
      case anon_sym_in:
        fprintf(st->outfile, " in ");
        break;
      case anon_sym_if:
        fprintf(st->outfile, " if ");
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_selection(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_member_selection:
        print_member_selection(child, st);
        break;
      case sym_bit_selection:
        print_bit_selection(child, st);
        break;
    }
  }
}

void print_member_selection(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_member_select:
        print_member_select(child, st);
        break;
      default:
        print__restricted_expression(child, st);
        break;
    }
  }
}

void print_bit_selection(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_bit_select:
        print_bit_select(child, st);
        break;
      default:
        print__restricted_expression(child, st);
        break;
    }
  }
}

void print_member_select(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);

    print_select(child, st);
  }
}

void print_bit_select(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *field_name = ts_node_field_name_for_child(node, i);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "type") == 0) {
        print_bit_select_type(child, st);
      } else if (strcmp(field_name, "select") == 0) {
        print_select(child, st);
      }
      continue;
    } else if (symbol == anon_sym_POUND) {
      fprintf(st->outfile, "#");
    }
  }
}

void print_select(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_select_options:
        print_select_options(child, st);
        break;
      case anon_sym_LBRACK:
        fprintf(st->outfile, "[");
        break;
      case anon_sym_RBRACK:
        fprintf(st->outfile, "]");
        break;
    }
  }
}

void print_select_options(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_expression_list:
        print_expression_list(child, st);
        break;
      case anon_sym_DOT_DOT:
        fprintf(st->outfile, "..");
        break;
      case anon_sym_DOT_DOT_EQ:
        fprintf(st->outfile, "..=");
        break;
      case anon_sym_DOT_DOT_LT:
        fprintf(st->outfile, "..<");
        break;
      default:
        print__expression(child, st, true);
        break;
    }
  }
}

void print_bit_select_type(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

/******************************************************************************
 * 7. Types & Identifiers
 * Responsibilities: Formats type names and identifiers. Handles primitive, array, 
 * and complex type signatures, ensuring consistent identifier naming format.
 ******************************************************************************/

void print__type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Dispatch to proper function
  switch (symbol) {
    case sym_primitive_type:
      print_primitive_type(node, st);
      break;
    case sym_array_type:
      print_array_type(node, st);
      break;
    case sym_lambda:
      print_lambda(node, st);
      break;
    case sym_expression_type:
      print_expression_type(node, st);
      break;
    default:
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          fprintf(st->outfile, "%s", text);
          free(text);
        }
      }
      break;
  }
}

void print_primitive_type(TSNode node, PrpfmtState *st) {
  // Print if no children
  uint32_t child_count = ts_node_child_count(node);
  if (child_count == 0) {
    char *text = get_node_text(node, st->source_code);
    if (text) {
      fprintf(st->outfile, "%s", text);
      free(text);
    }
    return;
  }

  // Iterate over children
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_unsized_integer_type:
        print_unsized_integer_type(child, st);
        break;
      case sym_sized_integer_type:
        print_sized_integer_type(child, st);
        break;
      case sym_bounded_integer_type:
        print_bounded_integer_type(child, st);
        break;
      case sym_range_type:
        print_range_type(child, st);
        break;
      case sym_string_type:
        print_string_type(child, st);
        break;
      case sym_boolean_type:
        print_boolean_type(child, st);
        break;
      case sym_type_type:
        print_type_type(child, st);
        break;
      default:
        if (ts_node_child_count(child) == 0) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            fprintf(st->outfile, "%s", text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_unsized_integer_type(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_sized_integer_type(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_bounded_integer_type(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name && strcmp(field_name, "constrained") == 0) {
      // Iterate over nodes
      uint32_t cc2 = ts_node_child_count(child);
      for (uint32_t j = 0; j < cc2; j++) {
        TSNode c2 = ts_node_child(child, j);
        TSSymbol s2 = ts_node_grammar_symbol(c2);

        // Handle nested nodes
        if (s2 == anon_sym_LPAREN) {
          fprintf(st->outfile, "(");
        } else if (s2 == anon_sym_RPAREN) {
          fprintf(st->outfile, ")");
        } else if (s2 == sym_select_options) {
          print_select_options(c2, st);
        }
      }
    } else if (symbol == sym_unsized_integer_type) {
      print_unsized_integer_type(child, st);
    }
  }
}

void print_range_type(TSNode node, PrpfmtState *st) {
  // Iterate over nodes
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_range:
        fprintf(st->outfile, "range");
        break;
      case anon_sym_LPAREN:
        fprintf(st->outfile, "(");
        break;
      case anon_sym_RPAREN:
        fprintf(st->outfile, ")");
        break;
      case sym_select_options:
        print_select_options(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_array_type(TSNode node, PrpfmtState *st) {
  // Iterate over named nodes
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "length") == 0) {
        print_tuple_sq(child, st);
      } else if (strcmp(field_name, "base") == 0) {
        switch (symbol) {
          case sym_primitive_type:
            print_primitive_type(child, st);
            break;
          case sym_array_type:
            print_array_type(child, st);
            break;
          case sym_lambda:
            print_lambda(child, st);
            break;
          case sym_expression_type:
            print_expression_type(child, st);
            break;
        }
      }
    } else if (symbol == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_string_type(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_boolean_type(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_type_type(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_expression_type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Dispatch to proper function
  switch (symbol) {
    case sym_identifier:
      print_identifier(node, st);
      break;
    case sym_dot_expression_type:
      print_dot_expression_type(node, st);
      break;
    case sym_function_call_type:
      print_function_call_type(node, st);
      break;
    case sym_tuple:
      print_tuple(node, st);
      break;
    default:
      // Handle the case where expression_type is a named node with children
      {
        // Iterate over children
        uint32_t cc = ts_node_child_count(node);
        if (cc > 0) {
          for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_child(node, i);
            print_expression_type(child, st);
          }
        } else {
          char *text = get_node_text(node, st->source_code);
          if (text) {
            fprintf(st->outfile, "%s", text);
            free(text);
          }
        }
      }
      break;
  }
}

void print_dot_expression_type(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_DOT:
        fprintf(st->outfile, ".");
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_expression_type:
        print_expression_type(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_function_call_type(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "function") == 0) {
        print_complex_identifier(child, st);
      } else if (strcmp(field_name, "argument") == 0) {
        print_tuple(child, st);
      }
    } else if (symbol == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_type_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "name") == 0) {
        print_identifier(child, st);
      } else if (strcmp(field_name, "generic") == 0) {
        if (symbol == sym_typed_identifier_list) {
          fprintf(st->outfile, "<");
          print_typed_identifier_list(child, st);
          fprintf(st->outfile, ">");
        } else {
          // Iterate over children
          uint32_t cc2 = ts_node_child_count(child);
          for (uint32_t j = 0; j < cc2; j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);

            // Handle nested nodes
            if (s2 == anon_sym_LT) {
              fprintf(st->outfile, "<");
            } else if (s2 == anon_sym_GT) {
              fprintf(st->outfile, ">");
            } else if (s2 == sym_typed_identifier_list) {
              print_typed_identifier_list(c2, st);
            }
          }
        }
      } else if (strcmp(field_name, "definition") == 0) {
        fprintf(st->outfile, " ");
        print_tuple(child, st);
      } else if (strcmp(field_name, "alias") == 0) {
        print__type(child, st);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_type:
        fprintf(st->outfile, "type ");
        break;
      case anon_sym_EQ:
        fprintf(st->outfile, " = ");
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_type_or_identifier(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Dispatch to proper function
  switch (symbol) {
    case sym_typed_identifier:
      print_typed_identifier(node, st);
      break;
    case sym_primitive_type:
      print_primitive_type(node, st);
      break;
    case sym_identifier:
      print_identifier(node, st);
      break;
    default:
      {
        // Iterate over children
        uint32_t cc = ts_node_child_count(node);
        if (cc > 0) {
          for (uint32_t i = 0; i < cc; i++) {
            TSNode child = ts_node_child(node, i);
            print_type_or_identifier(child, st);
          }
        } else {
          char *text = get_node_text(node, st->source_code);
          if (text) {
            fprintf(st->outfile, "%s", text);
            free(text);
          }
        }
      }
      break;
  }
}

void print_typed_identifier(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "identifier") == 0) {
        print_identifier(child, st);
      } else if (strcmp(field_name, "type") == 0) {
        print_type_cast(child, st);
      } else if (strcmp(field_name, "timing") == 0) {
        if (symbol == sym_constant) {
          print_constant(child, st);
        } else {
          // Handle seq('[', $._expression, ']') if it's not a named node
          // or just call print__expression if it matches
          print__expression(child, st, true);
        }
      }
      continue;
    }

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_AT:
        fprintf(st->outfile, "@");
        break;
      case anon_sym_LBRACK:
        fprintf(st->outfile, "[");
        break;
      case anon_sym_RBRACK:
        fprintf(st->outfile, "]");
        break;
    }
  }
}

void print_typed_identifier_list(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_COMMA:
        fprintf(st->outfile, ", ");
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_identifier(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_ref_identifier(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_ref:
        fprintf(st->outfile, "ref ");
        break;
      case sym_complex_identifier:
        print_complex_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_complex_identifier(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_dot_expression:
        print_dot_expression(child, st);
        break;
      case sym_selection:
        print_selection(child, st);
        break;
      case sym_timed_identifier:
        print_timed_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        // If child is a token, print it
        if (ts_node_child_count(child) == 0) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            fprintf(st->outfile, "%s", text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_complex_identifier_list(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_COMMA:
        fprintf(st->outfile, ", ");
        break;
      case sym_complex_identifier:
        print_complex_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_timed_identifier(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "identifier") == 0) {
        print_identifier(child, st);
      } else if (strcmp(field_name, "timing") == 0) {
        if (symbol == sym_constant) {
          print_constant(child, st);
        } else {
          print__expression(child, st, true);
        }
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_AT:
        fprintf(st->outfile, "@");
        break;
      case anon_sym_LBRACK:
        fprintf(st->outfile, "[");
        break;
      case anon_sym_RBRACK:
        fprintf(st->outfile, "]");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_var_or_let_or_reg(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

/******************************************************************************
 * 8. Literals & Numbers
 * Responsibilities: Formats constant values including numbers (binary, hex, decimal), 
 * booleans, and strings. Ensures literal values are emitted as-is from source.
 ******************************************************************************/

void print_constant(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__number(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Dispatch to proper function
  switch (symbol) {
    case sym__neg_simple_number:
      print__neg_simple_number(node, st);
      break;
    case sym__neg_scaled_number:
      print__neg_scaled_number(node, st);
      break;
    case sym__neg_hex_number:
      print__neg_hex_number(node, st);
      break;
    case sym__neg_decimal_number:
      print__neg_decimal_number(node, st);
      break;
    case sym__neg_octal_number:
      print__neg_octal_number(node, st);
      break;
    case sym__neg_binary_number:
      print__neg_binary_number(node, st);
      break;
    case sym__neg_typed_number:
      print__neg_typed_number(node, st);
      break;
    case sym__simple_number:
      print__simple_number(node, st);
      break;
    case sym__scaled_number:
      print__scaled_number(node, st);
      break;
    case sym__hex_number:
      print__hex_number(node, st);
      break;
    case sym__decimal_number:
      print__decimal_number(node, st);
      break;
    case sym__octal_number:
      print__octal_number(node, st);
      break;
    case sym__binary_number:
      print__binary_number(node, st);
      break;
    case sym__typed_number:
      print__typed_number(node, st);
      break;
    default:
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          fprintf(st->outfile, "%s", text);
          free(text);
        }
      }
      break;
  }
}

void print__simple_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__scaled_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__hex_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__decimal_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__octal_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__binary_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__typed_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__neg_simple_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__neg_scaled_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__neg_hex_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__neg_decimal_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__neg_octal_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__neg_binary_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__neg_typed_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__bool_literal(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__string_literal(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Dispatch to proper function
  switch (symbol) {
    case sym__simple_string_literal:
      print__simple_string_literal(node, st);
      break;
    default:
      print_complex_string_literal(node, st);
      break;
  }
}

void print__simple_string_literal(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_complex_string_literal(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__unknown_literal(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

/******************************************************************************
 * 9. Comments
 * Responsibilities: Processes and preserves code comments. Distinguishes between 
 * inline comments (same line as code) and newline comments.
 ******************************************************************************/

void print_comment(TSNode node, PrpfmtState *st) {
  // Print newline comment if no previous siblings
  TSNode prev = ts_node_prev_sibling(node);
  if (ts_node_is_null(prev)) {
    print_comment_newline(node, st);
    return;
  }

  TSPoint start = ts_node_start_point(node);
  TSPoint prev_end = ts_node_end_point(prev);

  // Check source code to see if inline/newline comment
  if (start.row == prev_end.row) {
    print_comment_inline(node, st);
  } else {
    print_comment_newline(node, st);
  }
}

void print_comment_inline(TSNode node, PrpfmtState *st) {
  char *node_text = get_node_text(node, st->source_code);
  if (node_text) {
    check_format_directives(node_text, st);
    fprintf(st->outfile, " %s", node_text);
    if (!ts_node_is_null(ts_node_next_sibling(node))) {
      fprintf(st->outfile, "\n");
    }
    free(node_text);
  }
}

void print_comment_newline(TSNode node, PrpfmtState *st) {
  char *node_text = get_node_text(node, st->source_code);
  if (node_text) {
    check_format_directives(node_text, st);
    print_indent(st);
    fprintf(st->outfile, "%s\n", node_text);
    free(node_text);
  }
}

/******************************************************************************
 * 10. Special Statements & Attributes
 * Responsibilities: Formats non-standard constructs like imports, assertions, 
 * and hardware-specific attributes (comb, pipe, flow, delay).
 ******************************************************************************/

void print_import_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "module") == 0) {
        if (symbol == sym_module_path) {
          print_module_path(child, st);
        } else {
          print__string_literal(child, st);
        }
      } else if (strcmp(field_name, "alias") == 0) {
        print_identifier(child, st);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_import:
        fprintf(st->outfile, "import ");
        break;
      case anon_sym_as:
        fprintf(st->outfile, " as ");
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_impl_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "trait_name") == 0 || strcmp(field_name, "type_name") == 0) {
        print_identifier(child, st);
      } else if (strcmp(field_name, "implementation") == 0) {
        print_tuple(child, st);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_impl:
        fprintf(st->outfile, "impl ");
        break;
      case anon_sym_for:
        fprintf(st->outfile, " for ");
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_test_statement(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *field_name = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Format named nodes
    if (field_name) {
      if (strcmp(field_name, "args") == 0) {
        fprintf(st->outfile, " ");
        print_expression_list(child, st);
      } else if (strcmp(field_name, "condition") == 0) {
        // Handle flattened optseq('where', $.expression_list)
        // Iterate over children
        uint32_t cc2 = ts_node_child_count(child);
        for (uint32_t j = 0; j < cc2; j++) {
          TSNode c2 = ts_node_child(child, j);
          TSSymbol s2 = ts_node_grammar_symbol(c2);

          // Handle nested nodes
          if (s2 == anon_sym_where) {
            fprintf(st->outfile, " where ");
          } else if (s2 == sym_expression_list) {
            print_expression_list(c2, st);
          }
        }
      } else if (strcmp(field_name, "code") == 0) {
        fprintf(st->outfile, " ");
        print_scope_statement(child, st, false);
      }
      continue;
    }

    // Dispatch anonymous nodes to proper function
    switch (symbol) {
      case anon_sym_test:
        fprintf(st->outfile, "test");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_cassert_statement(TSNode node, PrpfmtState *st) {
  // FIXME: parser incorrect
  fprintf(st->outfile, "cassert_statement\n");
}

void print_module_path(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_DOT:
        fprintf(st->outfile, ".");
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_attributes(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_COLON:
        fprintf(st->outfile, ":");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
    }
  }
}

void print_attribute_list(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_LBRACK:
        fprintf(st->outfile, "[");
        break;
      case anon_sym_RBRACK:
        fprintf(st->outfile, "]");
        break;
      case sym_attribute_item_list:
        print_attribute_item_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_attribute_item_list(TSNode node, PrpfmtState *st) {
  // Iterate over children
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case anon_sym_COMMA:
        fprintf(st->outfile, ", ");
        break;
      case sym_attribute_item:
        print_attribute_item(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
    }
  }
}

void print_attribute_item(TSNode node, PrpfmtState *st) {
  // Print if no children
  uint32_t child_count = ts_node_child_count(node);
  if (child_count == 0) {
    char *text = get_node_text(node, st->source_code);
    if (text) {
      fprintf(st->outfile, "%s", text);
      free(text);
    }
    return;
  }

  // Iterate over children
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Dispatch to proper function
    switch (symbol) {
      case sym_assignment:
        print_assignment(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        print__expression(child, st, true);
        break;
    }
  }
}

void print_comb_tok(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_pipe_tok(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_flow_tok(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print_delay_tok(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    fprintf(st->outfile, "%s", text);
    free(text);
  }
}

void print__semicolon(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Dispatch to proper function
  switch (symbol) {
    case anon_sym_SEMI:
      fprintf(st->outfile, " ;");
      break;
    case sym_when_unless_cond:
      fprintf(st->outfile, " ");
      print_when_unless_cond(node, st);
      break;
  }
}

/******************************************************************************
 * 11. Utilities
 * Responsibilities: Provides low-level helper functions for string extraction 
 * and node analysis from the tree-sitter tree.
 ******************************************************************************/

char *get_node_text(TSNode node, const char *source_code) {
  // Get byte position of text in original code
  uint32_t start_byte = ts_node_start_byte(node);
  uint32_t end_byte = ts_node_end_byte(node);
  uint32_t length = end_byte - start_byte;

  // Allocate memory for node text
  char *text = (char *) malloc(length + 1);
  if (text == NULL) {
    perror("Failed to allocate memory for node text");
    return NULL;
  }

  // Retrieve original text
  strncpy(text, source_code + start_byte, length);
  text[length] = '\0';

  return text;
}
