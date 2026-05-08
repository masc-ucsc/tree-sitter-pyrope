#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

#include "prpfmt.h"

/******************************************************************************
 * 1. Entry & High-Level Dispatch
 ******************************************************************************/

void print_description(TSTree *tree, PrpfmtState *st) {
  TSNode root_node = ts_tree_root_node(tree);
  uint32_t root_child_count = ts_node_child_count(root_node);

  TSNode prev_child;
  bool has_prev = false;

  // Preserve any newlines at the top of the file
  if (root_child_count > 0) {
    TSNode first_child = ts_node_child(root_node, 0);
    TSPoint start = ts_node_start_point(first_child);
    for (uint32_t j = 0; j < start.row; j++) {
      emit_newline(st);
    }
  }

  for (uint32_t i = 0; i < root_child_count; i++) {
    TSNode child = ts_node_child(root_node, i);

    // Preserve newlines from original source code
    if (has_prev) {
      preserve_whitespace(prev_child, child, st);
    }

    print__statement(child, st, false);

    prev_child = child;
    has_prev = true;
  }

}


void print__statement(TSNode node, PrpfmtState *st, bool is_inline) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  if (ts_node_is_named(node)) {
    char *node_text = get_node_text(node, st->source_code);
    if (node_text) {
      check_format_directives(node_text, st);
      free(node_text);
    }
  }

  // Print original text if prpfmt off
  if (!st->fmt_on) {
    char *node_text = get_node_text(node, st->source_code);
    if (node_text) {
      print_indent(st);
      emit_token(st, node_text);
      emit_newline(st);
      free(node_text);
    }
    return;
  }

  // Handle indentation for new lines and scope entry
  if (symbol != sym_scope_statement && symbol != sym_stmt_list && symbol != sym_comment) {
    TSNode prev_sib = ts_node_prev_sibling(node);
    if (ts_node_is_null(prev_sib)) {
      if (!is_inline) {
        print_indent(st);
      }
    } else {
      TSSymbol prev_sym = ts_node_grammar_symbol(prev_sib);
      TSPoint prev_end = ts_node_end_point(prev_sib);
      TSPoint curr_start = ts_node_start_point(node);

      if (curr_start.row > prev_end.row ||
          (prev_sym == anon_sym_LBRACE && !st->inline_exp)) {
        if (!is_inline) {
          print_indent(st);
        }
      }
    }
  }

  switch (symbol) {
    case sym_comment:
      print_comment(node, st);
      return;
    case anon_sym_wrap:
      emit_token(st, "wrap ");
      break;
    case anon_sym_sat:
      emit_token(st, "sat ");
      break;
    case sym_assignment:
      print_assignment(node, st, SPACE_BOTH);
      break;
    case sym_control_statement:
      print_control_statement(node, st);
      break;
    case sym_declaration_statement:
      print_declaration_statement(node, st);
      break;
    case sym_enum_assignment:
      print_enum_assignment(node, st);
      break;
    case sym_spawn_statement:
      print_spawn_statement(node, st);
      break;
    case sym_assert_statement:
      print_assert_statement(node, st);
      break;
    case sym_for_statement:
      print_for_statement(node, st);
      break;
    case sym_function_call_statement:
      print_function_call_statement(node, st);
      break;
    case sym_impl_statement:
      print_impl_statement(node, st);
      break;
    case sym_import_statement:
      print_import_statement(node, st);
      break;
    case sym_lambda:
      print_lambda(node, st);
      break;
    case sym_loop_statement:
      print_loop_statement(node, st);
      break;
    case sym_scope_statement:
      print_scope_statement(node, st, is_inline);
      break;
    case sym_test_statement:
      print_test_statement(node, st);
      break;
    case sym_type_statement:
      print_type_statement(node, st);
      break;
    case sym_while_statement:
      print_while_statement(node, st);
      break;
    case sym_when_unless_cond:
      print_when_unless_cond(node, st);
      break;
    case anon_sym_SEMI:
    case sym__automatic_semicolon:
      print__semicolon(node, st, SPACE_BOTH);
      break;
    default:
      print__expression(node, st, is_inline);
      break;
  }

  // Add trailing newline if top-level statement
  if (!is_inline) {
    TSNode next = ts_node_next_sibling(node);
    if (ts_node_is_null(next)) {
      emit_newline(st);
    } else if (ts_node_start_point(next).row > ts_node_end_point(node).row) {
      emit_newline(st);
    }
  }
}

void print_indent(PrpfmtState *st) {
  // Print spaces based on current indent level and size
  for (int i = 0; i < st->indent_level * st->indent_size; i++) {
    emit_token(st, " ");
  }
}

void check_format_directives(const char *node_text, PrpfmtState *st) {
  // Search for formatting toggle directives
  if (strstr(node_text, "prpfmt off")) {
    st->fmt_on = false;
  } else if (strstr(node_text, "prpfmt on")) {
    st->fmt_on = true;
  }
}

/******************************************************************************
 * 2. Structural (Scopes, Lists, Tuples)
 ******************************************************************************/

void print_scope_statement(TSNode node, PrpfmtState *st, bool is_inline) {
  uint32_t child_count = ts_node_child_count(node);
  TSNode first = ts_node_child(node, 0);
  TSNode last = ts_node_child(node, child_count - 1);
  bool single_line = false;

  // Determine if the scope is written on a single line in the source
  // TODO: this should be changed to use line wrap
  if (!ts_node_is_null(first) && !ts_node_is_null(last)) {
    if (ts_node_start_point(first).row == ts_node_end_point(last).row) {
      single_line = true;
    }
  }

  TSNode prev_child;
  bool has_prev = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Preserve original vertical spacing between statements
    if (has_prev && !single_line && !is_inline) {
      preserve_whitespace(prev_child, child, st);
    }

    switch (symbol) {
      case anon_sym_LBRACE:
        if (is_inline || single_line) {
          emit_token(st, "{ ");
        } else {
          emit_token(st, "{");
          emit_newline(st);
          st->indent_level++;
        }
        break;
      case anon_sym_RBRACE:
        if (is_inline || single_line) {
          emit_token(st, " }");
        } else {
          st->indent_level--;
          print_indent(st);
          emit_token(st, "}");
        }
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      case sym_when_unless_cond:
        print_when_unless_cond(child, st);
        break;
      default:
        print__statement(child, st, is_inline || single_line);
        break;
    }

    prev_child = child;
    has_prev = true;
  }
}

void print_stmt_list(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case sym_comment:
        print_comment(child, st);
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_BOTH);
        break;
      default:
        print__tuple_item(child, st, SPACE_BOTH);
        break;
    }
  }
}

void print_tuple(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_LPAREN:
        emit_token(st, "(");
        break;
      case anon_sym_RPAREN:
        emit_token(st, ")");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        print__tuple_list(child, st);
        break;
    }
  }
}

void print_tuple_sq(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_LBRACK:
        emit_token(st, "[");
        break;
      case anon_sym_RBRACK:
        emit_token(st, "]");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        print__tuple_list(child, st);
        break;
    }
  }
}

void print__tuple_list(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  switch (symbol) {
    case anon_sym_COMMA:
      emit_token(st, ", ");
      break;
    default:
      print__tuple_item(node, st, SPACE_NONE);
      break;
  }
}

void print__tuple_item(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  switch (symbol) {
    case sym_var_or_let_or_reg:
      print_var_or_let_or_reg(node, st);
      emit_token(st, " ");
      break;
    case sym_ref_identifier:
      print_ref_identifier(node, st);
      break;
    case sym_typed_identifier:
      print_typed_identifier(node, st);
      break;
    case sym_assignment:
      print_assignment(node, st, spacing);
      break;
    case sym_type_specification:
      print_type_specification(node, st);
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
 ******************************************************************************/

void print_if_expression(TSNode node, PrpfmtState *st, bool is_inline) {
  uint32_t child_count = ts_node_child_count(node);
  bool has_init = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_unique:
        emit_token(st, "unique ");
        break;
      case anon_sym_if:
        emit_token(st, "if");
        break;
      case anon_sym_elif:
        emit_token(st, " elif");
        break;
      case anon_sym_else:
        emit_token(st, " else");
        break;
      case sym_stmt_list:
        if (fn && strcmp(fn, "init") == 0) {
          emit_token(st, " ");
          print_stmt_list(child, st);
          has_init = true;
        } else {
          print_stmt_list(child, st);
        }
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        if (fn && strcmp(fn, "init") == 0) {
          print__semicolon(child, st, SPACE_AFTER);
          has_init = true;
        } else {
          print__semicolon(child, st, SPACE_NONE);
        }
        break;
      case sym_scope_statement:
        emit_token(st, " ");
        print_scope_statement(child, st, is_inline);
        has_init = false;
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (fn) {
          if (strcmp(fn, "condition") == 0) {
            if (!has_init) {
              emit_token(st, " ");
            }
            print__expression(child, st, is_inline);
          } else if (strcmp(fn, "code") == 0 || strcmp(fn, "else") == 0 || strcmp(fn, "elif") == 0) {
            if (ts_node_is_named(child)) {
              print__statement(child, st, is_inline);
            }
            has_init = false;
          }
        } else if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        } else {
          print__expression(child, st, is_inline);
        }
        break;
    }
  }
}

void print_match_expression(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);
  bool seen_lbrace = false;
  bool arm_started = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    if (symbol == anon_sym_LBRACE) {
      seen_lbrace = true;
    }

    if (fn) {
      if (strcmp(fn, "init") == 0) {
        if (symbol == sym_stmt_list) {
          emit_token(st, " ");
          print_stmt_list(child, st);
        } else if (symbol == anon_sym_SEMI || symbol == sym__automatic_semicolon) {
          print__semicolon(child, st, SPACE_AFTER);
        }
      } else if (strcmp(fn, "condition") == 0) {
        // If we are inside the braces, this 'condition' is a match arm pattern
        if (seen_lbrace) {
          if (!arm_started) {
            print_indent(st);
            arm_started = true;
          }
          if (symbol == anon_sym_else) {
            emit_token(st, "else");
          } else if (symbol == sym_expression_list) {
            print_expression_list(child, st);
          } else {
            print__expression(child, st, true);
            if (i + 1 < ts_node_child_count(node)) {
              const char *nfn = ts_node_field_name_for_child(node, i + 1);
              if (nfn && strcmp(nfn, "condition") == 0) {
                emit_token(st, " ");
              }
            }
          }
        } else {
          // If outside braces, this is the main expression being matched
          emit_token(st, " ");
          print__expression(child, st, true);
        }
      } else if (strcmp(fn, "code") == 0) {
        // Block of code to execute for a matched arm
        emit_token(st, " ");
        print_scope_statement(child, st, false);
        emit_newline(st);
        arm_started = false;
      }
      continue;
    }

    switch (symbol) {
      case anon_sym_match:
        emit_token(st, "match");
        break;
      case anon_sym_LBRACE:
        emit_token(st, " {");
        emit_newline(st);
        st->indent_level++;
        break;
      case anon_sym_RBRACE:
        st->indent_level--;
        print_indent(st);
        emit_token(st, "}");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        {
          // Handles anonymous tokens within arms like '==' or 'in'
          char *text = get_node_text(child, st->source_code);
          if (text) {
            if (seen_lbrace && !arm_started) {
              if (ts_node_is_named(child)) {
                print_indent(st);
                arm_started = true;
              } else if (strcmp(text, "}") != 0 && strcmp(text, ",") != 0) {
                print_indent(st);
                arm_started = true;
              }
            }
            emit_token(st, text);

            switch (symbol) {
              case anon_sym_in:
              case anon_sym_is:
              case anon_sym_has:
              case anon_sym_EQ_EQ:
              case anon_sym_BANG_EQ:
              case anon_sym_match:
                emit_token(st, " ");
                break;
              default:
                break;
            }
            free(text);
          }
        }
        break;
    }
  }
}

void print_for_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);
  bool has_init = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_for:
        emit_token(st, "for");
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
      case sym_stmt_list:
        emit_token(st, " ");
        print_stmt_list(child, st);
        has_init = true;
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        if (fn && strcmp(fn, "init") == 0) {
          print__semicolon(child, st, SPACE_AFTER);
          has_init = true;
        } else {
          print__semicolon(child, st, SPACE_NONE);
        }
        break;
      case anon_sym_LPAREN:
        if (!has_init) {
          emit_token(st, " ");
        }
        emit_token(st, "(");
        break;
      case sym_typed_identifier_list:
        print_typed_identifier_list(child, st);
        break;
      case anon_sym_RPAREN:
        emit_token(st, ")");
        break;
      case sym_typed_identifier:
        if (!has_init) {
          emit_token(st, " ");
        }
        print_typed_identifier(child, st);
        break;
      case anon_sym_in:
        emit_token(st, " in ");
        break;
      case sym_ref_identifier:
        print_ref_identifier(child, st);
        break;
      case sym_expression_list:
        print_expression_list(child, st);
        break;
      case sym_scope_statement:
        emit_token(st, " ");
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_while_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);
  bool has_init = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_while:
        emit_token(st, "while");
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
      case sym_stmt_list:
        if (fn && strcmp(fn, "init") == 0) {
          emit_token(st, " ");
          print_stmt_list(child, st);
          has_init = true;
        } else {
          print_stmt_list(child, st);
        }
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        if (fn && strcmp(fn, "init") == 0) {
          print__semicolon(child, st, SPACE_AFTER);
          has_init = true;
        } else {
          print__semicolon(child, st, SPACE_NONE);
        }
        break;
      case sym_scope_statement:
        emit_token(st, " ");
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (fn && strcmp(fn, "condition") == 0) {
          if (!has_init) {
            emit_token(st, " ");
          }
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        } else {
          print__expression(child, st, true);
        }
        break;
    }
  }
}

/* 
 * Note: print_when_unless_cond is decoupled from print__semicolon.
 * While grammatically a child, it is an AST sibling due to hidden node 
 * promotion. Separation allows for cleaner spacing management.
 */
void print_when_unless_cond(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    if (fn && strcmp(fn, "condition") == 0) {
      print__expression(child, st, true);
      continue;
    }

    switch (symbol) {
      case anon_sym_when:
        emit_token(st, " when ");
        break;
      case anon_sym_unless:
        emit_token(st, " unless ");
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        break;
    }
  }
}

void print_loop_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_loop:
        emit_token(st, "loop");
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
      case sym_stmt_list:
        emit_token(st, " ");
        print_stmt_list(child, st);
        break;
      case sym_scope_statement:
        emit_token(st, " ");
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_control_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    switch (symbol) {
      case sym_return_statement:
        print_return_statement(child, st);
        break;
      case sym_break_statement:
        print_break_statement(child, st);
        break;
      case sym_continue_statement:
        print_continue_statement(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_return_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_return:
        emit_token(st, "return");
        break;
      case sym_when_unless_cond:
        print_when_unless_cond(child, st);
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (fn && strcmp(fn, "argument") == 0) {
          emit_token(st, " ");
          print__expression_with_comprehension(child, st, true);
        } else if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_break_statement(TSNode node, PrpfmtState *st) {
  (void)node;
  (void)st;
  emit_token(st, "break");
}

void print_continue_statement(TSNode node, PrpfmtState *st) {
  (void)node;
  (void)st;
  emit_token(st, "continue");
}

/******************************************************************************
 * 4. Assignments & Declarations
 ******************************************************************************/

void print_assignment(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_var_or_let_or_reg:
        print_var_or_let_or_reg(child, st);
        break;
      case sym_lvalue_list:
        print_lvalue_list(child, st);
        break;
      case sym_lvalue_item:
      case sym_typed_identifier:
      case sym__complex_identifier:
        print_lvalue_item(child, st);
        break;
      case sym_type_cast:
        print_type_cast(child, st);
        break;
      case sym_assignment_operator:
        print_assignment_operator(child, st, spacing);
        break;
      case sym_enum_definition:
        print_enum_definition(child, st);
        break;
      case sym_ref_identifier:
        print_ref_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        // Handle expression-like rvalues and anonymous tokens (braces)
        if (fn && strcmp(fn, "rvalue") == 0) {
          print__expression_with_comprehension(child, st, true);
        } else if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        } else {
          print__expression_with_comprehension(child, st, true);
        }
        break;
    }
  }
}

void print_lvalue_item(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  switch (symbol) {
    case sym_typed_identifier:
      print_typed_identifier(node, st);
      break;
    case sym_identifier:
      print_identifier(node, st);
      break;
    case sym_dot_expression:
      print_dot_expression(node, st);
      break;
    case sym_member_selection:
      print_member_selection(node, st);
      break;
    case sym_bit_selection:
      print_bit_selection(node, st);
      break;
    case sym_attribute_read:
      print_attribute_read(node, st);
      break;
    default:
      {
        uint32_t child_count = ts_node_child_count(node);
        if (child_count == 0) {
          char *text = get_node_text(node, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        } else {
          for (uint32_t i = 0; i < child_count; i++) {
            TSNode child = ts_node_child(node, i);
            TSSymbol s2 = ts_node_grammar_symbol(child);
            const char *fn = ts_node_field_name_for_child(node, i);

            switch (s2) {
              case sym_typed_identifier:
                print_typed_identifier(child, st);
                break;
              case sym_identifier:
                print_identifier(child, st);
                break;
              case sym_dot_expression:
                print_dot_expression(child, st);
                break;
              case sym_member_selection:
                print_member_selection(child, st);
                break;
              case sym_bit_selection:
                print_bit_selection(child, st);
                break;
              case sym_attribute_read:
                print_attribute_read(child, st);
                break;
              case sym_type_cast:
                print_type_cast(child, st);
                break;
              case sym_comment:
                print_comment(child, st);
                break;
              default:
                if (fn) {
                  if (strcmp(fn, "identifier") == 0) {
                    print__complex_identifier(child, st);
                  } else if (strcmp(fn, "type") == 0) {
                    print_type_cast(child, st);
                  }
                } else if (!ts_node_is_named(child)) {
                  char *text = get_node_text(child, st->source_code);
                  if (text) {
                    emit_token(st, text);
                    free(text);
                  }
                }
                break;
            }
          }
        }
      }
      break;
  }
}

void print_lvalue_list(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case sym_lvalue_item:
        print_lvalue_item(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      case anon_sym_COMMA:
        emit_token(st, ", ");
        break;
      default:
        if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_declaration_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case sym_var_or_let_or_reg:
        print_var_or_let_or_reg(child, st);
        break;
      case sym_typed_identifier_list:
        print_typed_identifier_list(child, st);
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_when_unless_cond:
        print_when_unless_cond(child, st);
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_await_decl(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_await:
        emit_token(st, "await");
        break;
      case sym_tuple_sq:
        print_tuple_sq(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        break;
    }
  }
}

void print_enum_assignment(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_enum:
        emit_token(st, "enum ");
        break;
      case anon_sym_variant:
        emit_token(st, "variant ");
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_EQ:
        emit_token(st, " = ");
        break;
      case sym_tuple:
        print_tuple(child, st);
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_enum_definition(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_enum:
        emit_token(st, "enum ");
        break;
      case anon_sym_variant:
        emit_token(st, "variant ");
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_assignment_operator(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    if (spacing & SPACE_BEFORE) {
      emit_token(st, " ");
    }
    emit_token(st, text);
    if (spacing & SPACE_AFTER) {
      emit_token(st, " ");
    }
    free(text);
  }
}

void print_spawn_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_spawn:
        emit_token(st, "spawn ");
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_EQ:
        emit_token(st, " = ");
        break;
      case sym_scope_statement:
        print_scope_statement(child, st, false);
        break;
      case sym_when_unless_cond:
        print_when_unless_cond(child, st);
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

/******************************************************************************
 * 5. Functions & Parameters
 ******************************************************************************/

void print_lambda(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);
  bool header_done = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_comb:
      case anon_sym_mod:
      case anon_sym_proc:
      case anon_sym_pipe:
        {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
      case sym_tuple_sq:
        if (fn && strcmp(fn, "func_type") == 0) {
          print_tuple_sq(child, st);
        } else {
          print_tuple_sq(child, st);
        }
        break;
      case sym_identifier:
        if (!header_done) {
          emit_token(st, " ");
          header_done = true;
        }
        print_identifier(child, st);
        break;
      case sym_function_definition_decl:
        print_function_definition_decl(child, st);
        break;
      case sym_scope_statement:
        emit_token(st, " ");
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_function_definition_decl(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_LT:
        emit_token(st, "<");
        break;
      case anon_sym_GT:
        emit_token(st, ">");
        break;
      case sym_typed_identifier_list:
        print_typed_identifier_list(child, st);
        break;
      case sym_tuple_sq:
        print_tuple_sq(child, st);
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case anon_sym_DASH_GT:
        emit_token(st, " -> ");
        break;
      case sym_type_cast:
        print_type_cast(child, st);
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_arg_list(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_LPAREN:
        emit_token(st, "(");
        break;
      case anon_sym_RPAREN:
        emit_token(st, ")");
        break;
      case anon_sym_COMMA:
        emit_token(st, ", ");
        break;
      case anon_sym_EQ:
        emit_token(st, "=");
        break;
      case anon_sym_DOT_DOT_DOT:
        emit_token(st, "...");
        break;
      case anon_sym_ref:
        emit_token(st, "ref ");
        break;
      case alias_sym_reg_decl:
      case anon_sym_reg:
        emit_token(st, "reg ");
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (fn && strcmp(fn, "definition") == 0) {
          print__expression_with_comprehension(child, st, true);
        } else if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            if (strcmp(text, "reg") == 0 || strcmp(text, "ref") == 0 || strcmp(text, "const") == 0 || strcmp(text, "mut") == 0) {
              emit_token(st, " ");
            }
            free(text);
          }
        } else {
          print__expression_with_comprehension(child, st, true);
        }
        break;
    }
  }
}

void print_function_call_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_when_unless_cond:
        print_when_unless_cond(child, st);
        break;
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (fn) {
          if (strcmp(fn, "function") == 0) {
            print__complex_identifier(child, st);
          } else if (strcmp(fn, "argument") == 0) {
            emit_token(st, " ");
            print_expression_list(child, st);
          }
        } else if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
        break;
    }
  }
}

void print_function_call_expression(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_tuple:
        print_tuple(child, st);
        break;
      case sym_comment:
        print_comment(child, st);
        break;
      default:
        if (fn && strcmp(fn, "function") == 0) {
          print__complex_identifier(child, st);
        } else if (!ts_node_is_named(child)) {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        } else {
          print__complex_identifier(child, st);
        }
        break;
    }
  }
}

/******************************************************************************
 * 6. Expressions & Selection
 ******************************************************************************/

static void print_binary_op(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    if (spacing & SPACE_BEFORE) {
      emit_token(st, " ");
    }
    emit_token(st, text);
    if (spacing & SPACE_AFTER) {
      emit_token(st, " ");
    }
    free(text);
  }
}

void print__expression(TSNode node, PrpfmtState *st, bool is_inline) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if ((symbol == sym__expression || symbol == sym__expression_with_comprehension) && ts_node_child_count(node) > 0) {
    print__expression(ts_node_child(node, 0), st, is_inline);
    return;
  }
  switch (symbol) {
    case sym__binary_times:
      print__binary_times(node, st);
      break;
    case sym__binary_other:
      print__binary_other(node, st);
      break;
    case sym__binary_compare:
      print__binary_compare(node, st);
      break;
    case sym__binary_logical:
      print__binary_logical(node, st);
      break;
    case sym_attribute_read:
      print_attribute_read(node, st);
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
    case sym_var_or_let_or_reg:
      print_var_or_let_or_reg(node, st);
      emit_token(st, " ");
      break;
    case sym_ref_identifier:
      print_ref_identifier(node, st);
      break;
    case sym__complex_identifier:
      print__complex_identifier(node, st);
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
    case sym_scope_statement:
      print_scope_statement(node, st, is_inline);
      break;
    case sym_optional_expression:
      print_optional_expression(node, st);
      break;
    case sym__restricted_expression:
      print__restricted_expression(node, st);
      break;
    default:
      print__restricted_expression(node, st);
      break;
  }
}

void print__expression_with_comprehension(TSNode node, PrpfmtState *st, bool is_inline) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  if (symbol == sym__expression_with_comprehension && ts_node_child_count(node) > 0) {
    print__expression_with_comprehension(ts_node_child(node, 0), st, is_inline);
    return;
  }

  if (symbol == sym_for_comprehension) {
    print_for_comprehension(node, st);
  } else {
    print__expression(node, st, is_inline);
  }
}

void print__restricted_expression(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  if (symbol == sym__restricted_expression && ts_node_child_count(node) > 0) {
    print__restricted_expression(ts_node_child(node, 0), st);
    return;
  }

  switch (symbol) {
    case sym__complex_identifier:
      print__complex_identifier(node, st);
      break;
    case sym_ref_identifier:
      print_ref_identifier(node, st);
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
    case sym_tuple_sq:
      print_tuple_sq(node, st);
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
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          emit_token(st, text);
          free(text);
        }
      } else {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
          print__restricted_expression(ts_node_child(node, i), st);
        }
      }
    } break;
  }
}

void print_expression_item(TSNode node, PrpfmtState *st) {
  print__expression(node, st, true);
}

void print__binary_times(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "operand") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(fn, "operator") == 0) {
        print_binary_times_op(child, st, SPACE_NONE);
      }
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_binary_times_op(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  print_binary_op(node, st, spacing);
}

void print__binary_other(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "operand") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(fn, "operator") == 0) {
        print_binary_other_op(child, st, SPACE_BOTH);
      }
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_binary_other_op(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  print_binary_op(node, st, spacing);
}

void print__binary_compare(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "operand") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(fn, "operator") == 0) {
        print_binary_compare_op(child, st, SPACE_BOTH);
      }
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_binary_compare_op(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  print_binary_op(node, st, spacing);
}

void print__binary_logical(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "operand") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(fn, "operator") == 0) {
        print_binary_logical_op(child, st, SPACE_BOTH);
      }
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_binary_logical_op(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  print_binary_op(node, st, spacing);
}

void print__pri1_operand(TSNode node, PrpfmtState *st) {
  print__expression(node, st, true);
}
void print__pri2_operand(TSNode node, PrpfmtState *st) {
  print__expression(node, st, true);
}
void print__pri3_operand(TSNode node, PrpfmtState *st) {
  print__expression(node, st, true);
}
void print__pri4_operand(TSNode node, PrpfmtState *st) {
  print__expression(node, st, true);
}

void print_unary_expression(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "operator") == 0) {
        char *text = get_node_text(child, st->source_code);
        if (text) {
          emit_token(st, text);
          if (strcmp(text, "not") == 0) {
            emit_token(st, " ");
          }
          free(text);
        }
      } else if (strcmp(fn, "argument") == 0) {
        print__expression(child, st, true);
      }
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_dot_expression(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol s = ts_node_grammar_symbol(child);

    if (fn && strcmp(fn, "item") == 0) {
      print__expression(child, st, true);
    } else if (s == anon_sym_DOT) {
      emit_token(st, ".");
    } else if (ts_node_is_named(child)) {
      if (s == sym_comment) {
        print_comment(child, st);
      } else {
        print__complex_identifier(child, st);
      }
    } else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        emit_token(st, text);
        free(text);
      }
    }
  }
}

void print_optional_expression(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "argument") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(fn, "operator") == 0) {
        emit_token(st, "?");
      }
    }
  }
}

void print_type_specification(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol s = ts_node_grammar_symbol(child);

    if (fn) {
      if (strcmp(fn, "argument") == 0) {
        print__restricted_expression(child, st);
      } else if (strcmp(fn, "type") == 0 && ts_node_is_named(child)) {
        print__type(child, st);
      }
      continue;
    }

    if (s == anon_sym_COLON) {
      emit_token(st, ":");
    } else if (ts_node_is_named(child)) {
      if (s == sym_comment) {
        print_comment(child, st);
      } else {
        print__type(child, st);
      }
    }
  }
}

void print_type_cast(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "attribute") == 0) {
        if (symbol == sym_attribute_list) {
          print_attribute_list(child, st);
        } else {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        }
      } else if (strcmp(fn, "type") == 0) {
        print__type(child, st);
      }
      continue;
    }

    if (symbol == anon_sym_COLON) {
      emit_token(st, ":");
    } else if (ts_node_is_named(child)) {
      if (symbol == sym_comment) {
        print_comment(child, st);
      } else {
        print__type(child, st);
      }
    }
  }
}

void print_expression_list(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);

    if (ts_node_is_named(child)) {
      TSSymbol sym = ts_node_grammar_symbol(child);
      if (sym == sym_comment) {
        print_comment(child, st);
      } else if (sym == sym_ref_identifier) {
        print_ref_identifier(child, st);
      } else {
        print__expression(child, st, true);
      }
    } else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        if (strcmp(text, ",") == 0) {
          emit_token(st, ", ");
        } else {
          emit_token(st, text);
        }
        free(text);
      }
    }
  }
}

void print_for_comprehension(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "iterable") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(fn, "body") == 0) {
        print__expression(child, st, true);
      }
      continue;
    }

    if (ts_node_grammar_symbol(child) == anon_sym_for) {
      emit_token(st, "for ");
    } else if (ts_node_grammar_symbol(child) == anon_sym_in) {
      emit_token(st, " in ");
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_member_selection(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "argument") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(fn, "select") == 0 || strcmp(fn, "selection") == 0) {
        print_select(child, st);
      }
      continue;
    }

    if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_bit_selection(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "argument") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(fn, "select") == 0 || strcmp(fn, "selection") == 0) {
        emit_token(st, "@");
        print_select(child, st);
      }
      continue;
    }

    if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_attribute_read(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol s = ts_node_grammar_symbol(child);

    if (fn && strcmp(fn, "argument") == 0) {
      print__expression(child, st, true);
    } else if (s == anon_sym_DOT) {
      emit_token(st, ".");
    } else if (s == sym_attribute_list) {
      print_attribute_list(child, st);
    } else if (s == sym_comment) {
      print_comment(child, st);
    } else if (ts_node_is_named(child)) {
      print_attribute_read(child, st);
    } else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        emit_token(st, text);
        free(text);
      }
    }
  }
}

void print_select(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    if (ts_node_is_named(child)) {
      if (symbol == sym_selection_range) {
        print_selection_range(child, st);
      } else if (symbol == sym_comment) {
        print_comment(child, st);
      } else {
        print__expression(child, st, true);
      }
    } else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        emit_token(st, text);
        free(text);
      }
    }
  }
}

void print_selection_range(TSNode node, PrpfmtState *st) {
  for (uint32_t j = 0; j < ts_node_child_count(node); j++) {
    TSNode c2 = ts_node_child(node, j);

    if (ts_node_is_named(c2)) {
      print__expression(c2, st, true);
    } else {
      char *text = get_node_text(c2, st->source_code);
      if (text) {
        if (strcmp(text, ",") == 0) {
          emit_token(st, ", ");
        } else {
          emit_token(st, text);
        }
        free(text);
      }
    }
  }
}

/******************************************************************************
 * 7. Types & Identifiers
 ******************************************************************************/

void print__type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  if (symbol == sym__type && ts_node_child_count(node) > 0) {
    print__type(ts_node_child(node, 0), st);
    return;
  }

  switch (symbol) {
    case sym__primitive_type:
      print__primitive_type(node, st);
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
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          emit_token(st, text);
          free(text);
        }
      } else {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
          print__type(ts_node_child(node, i), st);
        }
      }
    } break;
  }
}

void print_expression_type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

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
    default: {
      if (ts_node_child_count(node) > 0) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
          print_expression_type(ts_node_child(node, i), st);
        }
      } else {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          emit_token(st, text);
          free(text);
        }
      }
    } break;
  }
}

void print_dot_expression_type(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);

    if (ts_node_grammar_symbol(child) == anon_sym_DOT) {
      emit_token(st, ".");
    } else if (ts_node_grammar_symbol(child) == sym_identifier) {
      print_identifier(child, st);
    } else if (ts_node_grammar_symbol(child) == sym_expression_type) {
      print_expression_type(child, st);
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_function_call_type(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "function") == 0) {
        print__complex_identifier(child, st);
      } else if (strcmp(fn, "argument") == 0) {
        print_tuple(child, st);
      }
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_array_type(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "length") == 0) {
        print_tuple_sq(child, st);
      } else if (strcmp(fn, "base") == 0) {
        print__type(child, st);
      }
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print__primitive_type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  switch (symbol) {
    case sym_uint_type:
      print_uint_type(node, st);
      break;
    case sym_sint_type:
      print_sint_type(node, st);
      break;
    case sym_string_type:
      print_string_type(node, st);
      break;
    case sym_bool_type:
      print_bool_type(node, st);
      break;
    case anon_sym_type:
      emit_token(st, "type");
      break;
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          emit_token(st, text);
          free(text);
        }
      } else {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
          print__primitive_type(ts_node_child(node, i), st);
        }
      }
    } break;
  }
}

void print_uint_type(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print_sint_type(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print_bool_type(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print_string_type(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}
void print_type_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "name") == 0) {
        print_identifier(child, st);
      } else if (strcmp(fn, "generic") == 0) {
        emit_token(st, "<");
        print_typed_identifier_list(child, st);
        emit_token(st, ">");
      } else if (strcmp(fn, "definition") == 0) {
        emit_token(st, " ");
        print_tuple(child, st);
      } else if (strcmp(fn, "alias") == 0) {
        print__type(child, st);
      }
      continue;
    }

    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_type) {
      emit_token(st, "type ");
    } else if (symbol == anon_sym_EQ) {
      emit_token(st, " = ");
    } else if (symbol == anon_sym_SEMI) {
      print__semicolon(child, st, SPACE_NONE);
    } else if (symbol == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_typed_identifier(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym_identifier) {
    print_identifier(node, st);
    return;
  }
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "identifier") == 0) {
        print_identifier(child, st);
      } else if (strcmp(fn, "type") == 0) {
        print_type_cast(child, st);
      } else if (strcmp(fn, "timing") == 0) {
        print_constant(child, st);
      }
      continue;
    }

    TSSymbol s2 = ts_node_grammar_symbol(child);
    if (s2 == anon_sym_AT) {
      emit_token(st, "@");
    } else if (s2 == anon_sym_LBRACK) {
      emit_token(st, "[");
    } else if (s2 == anon_sym_RBRACK) {
      emit_token(st, "]");
    } else if (s2 == sym__timing_sequence) {
      print__timing_sequence(child, st);
    }
  }
}

void print_typed_identifier_list(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_grammar_symbol(child) == anon_sym_COMMA) {
      emit_token(st, ", ");
    } else if (ts_node_grammar_symbol(child) == sym_typed_identifier) {
      print_typed_identifier(child, st);
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_identifier(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print_ref_identifier(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_grammar_symbol(child) == anon_sym_ref) {
      emit_token(st, "ref ");
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    } else {
      print__complex_identifier(child, st);
    }
  }
}

void print__complex_identifier(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym__complex_identifier && ts_node_child_count(node) > 0) {
    print__complex_identifier(ts_node_child(node, 0), st);
    return;
  }

  switch (symbol) {
    case sym_identifier:
      print_identifier(node, st);
      break;
    case sym_dot_expression:
      print_dot_expression(node, st);
      break;
    case sym_member_selection:
      print_member_selection(node, st);
      break;
    case sym_bit_selection:
      print_bit_selection(node, st);
      break;
    case sym_attribute_read:
      print_attribute_read(node, st);
      break;
    case sym_timed_identifier:
      print_timed_identifier(node, st);
      break;
    default:
      {
        if (ts_node_child_count(node) == 0) {
          char *text = get_node_text(node, st->source_code);
          if (text) {
            emit_token(st, text);
            free(text);
          }
        } else {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
            print__complex_identifier(ts_node_child(node, i), st);
          }
        }
      }
      break;
  }
}

void print_timed_identifier(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "identifier") == 0) {
        print_identifier(child, st);
      } else if (strcmp(fn, "timing") == 0) {
        print__timing_sequence(child, st);
      }
      continue;
    }

    if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_var_or_let_or_reg(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "comptime") == 0) {
        emit_token(st, "comptime ");
      } else if (strcmp(fn, "storage") == 0) {
        if (ts_node_grammar_symbol(child) == sym_await_decl) {
          print_await_decl(child, st);
        } else {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            emit_token(st, text);
      emit_token(st, " ");
            free(text);
          }
        }
      }
      continue;
    }

    if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

/******************************************************************************
 * 8. Literals & Numbers
 ******************************************************************************/

void print_constant(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  switch (symbol) {
    case sym_integer_literal:
      print_integer_literal(node, st);
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
    case sym_bool_literal:
      print_bool_literal(node, st);
      break;
    case sym_unknown_literal:
      print_unknown_literal(node, st);
      break;
    case sym_string_literal:
      print_string_literal(node, st);
      break;
    case sym__string_literal:
      print__string_literal(node, st);
      break;
    case sym_interpolated_string_literal:
      print_interpolated_string_literal(node, st);
      break;
    case sym__format_spec:
      print__format_spec(node, st);
      break;
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          emit_token(st, text);
          free(text);
        }
      } else {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
          print_constant(ts_node_child(node, i), st);
        }
      }
    } break;
  }
}

void print_integer_literal(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print__simple_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print__scaled_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print__hex_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print__decimal_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print__octal_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print__binary_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print__typed_number(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print_bool_literal(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print_unknown_literal(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print__string_literal(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print_string_literal(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print_interpolated_string_literal(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

void print__format_spec(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

/******************************************************************************
 * 9. Comments
 ******************************************************************************/

void print_comment(TSNode node, PrpfmtState *st) {
  TSNode prev = ts_node_prev_sibling(node);

  if (ts_node_is_null(prev)) {
    print_comment_newline(node, st);
  } else if (ts_node_end_point(prev).row == ts_node_start_point(node).row) {
    print_comment_inline(node, st);
  } else {
    print_comment_newline(node, st);
  }
}

void print_comment_inline(TSNode node, PrpfmtState *st) {
  char *node_text = get_node_text(node, st->source_code);
  if (node_text) {
    check_format_directives(node_text, st);
    emit_token(st, " ");
      emit_token(st, node_text);
    if (!ts_node_is_null(ts_node_next_sibling(node))) {
      emit_newline(st);
    }
    free(node_text);
  }
}

void print_comment_newline(TSNode node, PrpfmtState *st) {
  char *node_text = get_node_text(node, st->source_code);
  if (node_text) {
    check_format_directives(node_text, st);
    print_indent(st);
    emit_token(st, node_text);
      emit_newline(st);
    free(node_text);
  }
}

/******************************************************************************
 * 10. Special Statements & Attributes
 ******************************************************************************/

void print_import_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "module") == 0) {
        if (ts_node_grammar_symbol(child) == sym__complex_identifier) {
          print__complex_identifier(child, st);
        } else {
          print__string_literal(child, st);
        }
      } else if (strcmp(fn, "alias") == 0) {
        print_identifier(child, st);
      }
      continue;
    }

    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_import) {
      emit_token(st, "import ");
    } else if (symbol == anon_sym_as) {
      emit_token(st, " as ");
    } else if (symbol == anon_sym_SEMI) {
      print__semicolon(child, st, SPACE_NONE);
    } else if (symbol == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_impl_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "name") == 0) {
        print__complex_identifier(child, st);
      } else if (strcmp(fn, "base") == 0) {
        print__type(child, st);
      }
    } else {
      TSSymbol symbol = ts_node_grammar_symbol(child);
      if (symbol == anon_sym_impl) {
        emit_token(st, "impl ");
      } else if (symbol == anon_sym_for) {
        emit_token(st, " for ");
      } else if (symbol == sym_scope_statement) {
        emit_token(st, " ");
        print_scope_statement(child, st, false);
      } else if (symbol == sym_comment) {
        print_comment(child, st);
      }
    }
  }
}

void print_test_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "args") == 0) {
        emit_token(st, " ");
        print_expression_list(child, st);
      } else if (strcmp(fn, "code") == 0) {
        emit_token(st, " ");
        print_scope_statement(child, st, false);
      }
      continue;
    }

    if (ts_node_grammar_symbol(child) == anon_sym_test) {
      emit_token(st, "test");
    } else if (ts_node_grammar_symbol(child) == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_assert_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (fn) {
      if (strcmp(fn, "condition") == 0) {
        print__expression(child, st, true);
      } else if (strcmp(fn, "msg") == 0) {
        for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
          TSNode c2 = ts_node_child(child, j);
          if (ts_node_grammar_symbol(c2) == anon_sym_COMMA) {
            emit_token(st, ", ");
          } else {
            print_constant(c2, st);
          }
        }
      }
      continue;
    }

    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_always) {
      emit_token(st, "always ");
    } else if (symbol == anon_sym_assert) {
      emit_token(st, "assert ");
    } else if (symbol == anon_sym_cassert) {
      emit_token(st, "cassert ");
    } else if (symbol == anon_sym_SEMI) {
      print__semicolon(child, st, SPACE_NONE);
    } else if (symbol == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_attributes(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    if (symbol == anon_sym_COLON_COLON) {
      emit_token(st, "::");
    } else if (symbol == sym_attribute_list) {
      print_attribute_list(child, st);
    } else if (symbol == sym_comment) {
      print_comment(child, st);
    }
  }
}

void print_attribute_list(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);

    if (ts_node_is_named(child)) {
      TSSymbol sym = ts_node_grammar_symbol(child);
      if (sym == sym_comment) {
        print_comment(child, st);
      } else if (sym == sym_ref_identifier) {
        print_ref_identifier(child, st);
      } else {
        print__expression(child, st, true);
      }
    } else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        if (strcmp(text, ",") == 0) {
          emit_token(st, ", ");
        } else {
          emit_token(st, text);
        }
        free(text);
      }
    }
  }
}

void print__semicolon(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  if (ts_node_grammar_symbol(node) == anon_sym_SEMI) {
    if (spacing & SPACE_BEFORE) {
      emit_token(st, " ");
    }
    emit_token(st, ";");
    if (spacing & SPACE_AFTER) {
      emit_token(st, " ");
    }
  }
}

void print__timing_sequence(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol s = ts_node_grammar_symbol(child);

    if (s == anon_sym_POUND) {
      emit_token(st, "#");
    } else if (s == sym_constant) {
      print_constant(child, st);
    } else if (s == sym_comment) {
      print_comment(child, st);
    }
  }
}

/******************************************************************************
 * 11. Utilities
 ******************************************************************************/

void preserve_whitespace(TSNode prev, TSNode curr, PrpfmtState *st) {
  if (ts_node_is_null(prev) || ts_node_is_null(curr)) {
    return;
  }

  TSPoint prev_end = ts_node_end_point(prev);
  TSPoint curr_start = ts_node_start_point(curr);

  if (curr_start.row > prev_end.row + 1) {
    uint32_t blank_lines = curr_start.row - prev_end.row - 1;
    for (uint32_t i = 0; i < blank_lines; i++) {
      emit_newline(st);
    }
  }
}

char *get_node_text(TSNode node, const char *source_code) {
  uint32_t start_byte = ts_node_start_byte(node);
  uint32_t end_byte = ts_node_end_byte(node);
  uint32_t length = end_byte - start_byte;

  char *text = (char *) malloc(length + 1);
  if (text == NULL) {
    perror("Failed to allocate memory for node text");
    return NULL;
  }

  strncpy(text, source_code + start_byte, length);
  text[length] = '\0';

  return text;
}
