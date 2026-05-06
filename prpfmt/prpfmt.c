#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

#include "prpfmt.h"

/******************************************************************************
 * 1. Entry & High-Level Dispatch
 ******************************************************************************/

void print_tree(TSTree *tree, PrpfmtState *st) {
  TSNode root_node = ts_tree_root_node(tree);
  uint32_t root_child_count = ts_node_child_count(root_node);
  TSNode prev_child;
  bool has_prev = false;
  for (uint32_t i = 0; i < root_child_count; i++) {
    TSNode child = ts_node_child(root_node, i);
    if (has_prev) {
      preserve_whitespace(prev_child, child, st);
    }
    print_statement(child, st, false);
    prev_child = child;
    has_prev = true;
  }
}

void print_statement(TSNode node, PrpfmtState *st, bool is_inline) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if ((symbol == sym__statement || symbol == sym__expression || symbol == sym__expression_with_comprehension) && ts_node_child_count(node) > 0) {
    print_statement(ts_node_child(node, 0), st, is_inline);
    return;
  }
  if (symbol == sym_comment) {
    print_comment(node, st);
    return;
  }
  if (ts_node_is_named(node)) {
    char *node_text = get_node_text(node, st->source_code);
    if (node_text) {
      check_format_directives(node_text, st);
      free(node_text);
    }
  }
  if (!st->fmt_on) {
    char *node_text = get_node_text(node, st->source_code);
    if (node_text) {
      print_indent(st);
      fprintf(st->outfile, "%s\n", node_text);
      free(node_text);
    }
    return;
  }
  if (symbol != sym_scope_statement && symbol != sym_stmt_list) {
    TSNode prev_sib = ts_node_prev_sibling(node);
    if (ts_node_is_null(prev_sib)) {
      if (!is_inline) print_indent(st);
    } else {
      TSSymbol prev_sym = ts_node_grammar_symbol(prev_sib);
      TSPoint prev_end = ts_node_end_point(prev_sib);
      TSPoint curr_start = ts_node_start_point(node);
      if (curr_start.row > prev_end.row || (prev_sym == anon_sym_LBRACE && !st->inline_exp)) {
        if (!is_inline) print_indent(st);
      }
    }
  }
  switch (symbol) {
    case sym_assignment: print_assignment(node, st, true); break;
    case sym_control_statement: print_control_statement(node, st); break;
    case sym_return_statement: print_return_statement(node, st); break;
    case sym_break_statement: print_break_statement(node, st); break;
    case sym_continue_statement: print_continue_statement(node, st); break;
    case sym_await_decl: print_await_decl(node, st); break;
    case sym_declaration_statement: print_declaration_statement(node, st); break;
    case sym_enum_assignment: print_enum_assignment(node, st); break;
    case sym_spawn_statement: print_spawn_statement(node, st); break;
    case sym_assert_statement: print_assert_statement(node, st); break;
    case sym_if_expression: print_if_expression(node, st, is_inline); break;
    case sym_for_statement: print_for_statement(node, st); break;
    case sym_function_call_statement: print_function_call_statement(node, st); break;
    case sym_impl_statement: print_impl_statement(node, st); break;
    case sym_import_statement: print_import_statement(node, st); break;
    case sym_lambda: print_lambda(node, st); break;
    case sym_loop_statement: print_loop_statement(node, st); break;
    case sym_scope_statement: print_scope_statement(node, st, is_inline); break;
    case sym_test_statement: print_test_statement(node, st); break;
    case sym_type_statement: print_type_statement(node, st); break;
    case sym_while_statement: print_while_statement(node, st); break;
    default: print__expression(node, st, is_inline); break;
  }
  if (!is_inline) {
    TSNode next = ts_node_next_sibling(node);
    if (ts_node_is_null(next)) fprintf(st->outfile, "\n");
    else if (ts_node_start_point(next).row > ts_node_end_point(node).row) fprintf(st->outfile, "\n");
  }
}

void print_indent(PrpfmtState *st) {
  for (int i = 0; i < st->indent_level * st->indent_size; i++) fprintf(st->outfile, " ");
}

void check_format_directives(const char *node_text, PrpfmtState *st) {
  if (strstr(node_text, "prpfmt off")) st->fmt_on = false;
  else if (strstr(node_text, "prpfmt on")) st->fmt_on = true;
}

/******************************************************************************
 * 2. Structural (Scopes, Lists, Tuples)
 ******************************************************************************/

void print_scope_statement(TSNode node, PrpfmtState *st, bool is_inline) {
  uint32_t child_count = ts_node_child_count(node);
  TSNode first = ts_node_child(node, 0);
  TSNode last = ts_node_child(node, child_count - 1);
  bool single_line = false;
  if (!ts_node_is_null(first) && !ts_node_is_null(last)) {
    if (ts_node_start_point(first).row == ts_node_end_point(last).row) single_line = true;
  }
  TSNode prev_child;
  bool has_prev = false;
  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (has_prev && !single_line && !is_inline) {
      preserve_whitespace(prev_child, child, st);
    }
    switch (symbol) {
      case anon_sym_LBRACE:
        if (is_inline || single_line) fprintf(st->outfile, "{ ");
        else { fprintf(st->outfile, "{\n"); st->indent_level++; }
        break;
      case sym_stmt_list:
      case aux_sym_stmt_list_repeat1:
        print_stmt_list(child, st);
        break;
      case anon_sym_RBRACE:
        if (is_inline || single_line) fprintf(st->outfile, "}");
        else { st->indent_level--; print_indent(st); fprintf(st->outfile, "}"); }
        break;
      case sym_comment: print_comment(child, st); break;
      default:
        if (ts_node_is_named(child)) print_statement(child, st, is_inline || single_line);
        break;
    }
    prev_child = child;
    has_prev = true;
  }
}

void print_stmt_list(TSNode node, PrpfmtState *st) {
  TSNode prev_child;
  bool has_prev = false;
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (has_prev) {
      preserve_whitespace(prev_child, child, st);
    }
    if (ts_node_is_named(child)) print_statement(child, st, false);
    else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        if (strcmp(text, ";") == 0) fprintf(st->outfile, " ; ");
        else fprintf(st->outfile, "%s", text);
        free(text);
      }
    }
    prev_child = child;
    has_prev = true;
  }
}

void print_tuple(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (ts_node_is_named(child)) {
      if (symbol == sym__tuple_list) print_tuple_list(child, st);
      else if (symbol == sym_comment) print_comment(child, st);
      else print__tuple_item(child, st);
    } else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        if (strcmp(text, ",") == 0) fprintf(st->outfile, ", ");
        else fprintf(st->outfile, "%s", text);
        free(text);
      }
    }
  }
}

void print_tuple_sq(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (ts_node_is_named(child)) {
      if (symbol == sym__tuple_list) print_tuple_list(child, st);
      else if (symbol == sym_comment) print_comment(child, st);
      else print__tuple_item(child, st);
    } else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        if (strcmp(text, ",") == 0) fprintf(st->outfile, ", ");
        else fprintf(st->outfile, "%s", text);
        free(text);
      }
    }
  }
}

void print_tuple_list(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (ts_node_is_named(child)) {
      if (symbol == sym__tuple_item) print__tuple_item(child, st);
      else if (symbol == sym_comment) print_comment(child, st);
      else print__tuple_item(child, st);
    } else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        if (strcmp(text, ",") == 0) fprintf(st->outfile, ", ");
        else fprintf(st->outfile, "%s", text);
        free(text);
      }
    }
  }
}

void print__tuple_item(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym__tuple_item && ts_node_child_count(node) > 0) {
    print__tuple_item(ts_node_child(node, 0), st);
    return;
  }
  switch (symbol) {
    case sym_ref_identifier: print_ref_identifier(node, st); break;
    case sym_assignment: print_assignment(node, st, false); break;
    case sym_type_specification: print_type_specification(node, st); break;
    case sym_lambda: print_lambda(node, st); break;
    default: print__expression_with_comprehension(node, st, true); break;
  }
}

/******************************************************************************
 * 3. Control Flow
 ******************************************************************************/

void print_if_expression(TSNode node, PrpfmtState *st, bool is_inline) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "condition") == 0) { fprintf(st->outfile, " "); print__expression(child, st, is_inline); }
      else if (strcmp(fn, "code") == 0 || strcmp(fn, "else") == 0 || strcmp(fn, "elif") == 0) {
        if (symbol == anon_sym_elif) fprintf(st->outfile, " elif");
        else if (symbol == anon_sym_else) fprintf(st->outfile, " else");
        else if (symbol == sym_scope_statement) { fprintf(st->outfile, " "); print_scope_statement(child, st, is_inline); }
        else {
          for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
            TSNode c2 = ts_node_child(child, j);
            TSSymbol s2 = ts_node_grammar_symbol(c2);
            const char *fn2 = ts_node_field_name_for_child(child, j);
            if (s2 == anon_sym_elif) fprintf(st->outfile, " elif");
            else if (s2 == anon_sym_else) fprintf(st->outfile, " else");
            else if (s2 == anon_sym_if) fprintf(st->outfile, " if");
            else if (fn2 && (strcmp(fn2, "condition") == 0)) { fprintf(st->outfile, " "); print__expression(c2, st, is_inline); }
            else if ((fn2 && strcmp(fn2, "code") == 0) || s2 == sym_scope_statement) { fprintf(st->outfile, " "); print_scope_statement(c2, st, is_inline); }
          }
        }
      }
      continue;
    }
    switch (symbol) {
      case anon_sym_unique: fprintf(st->outfile, "unique "); break;
      case anon_sym_if: fprintf(st->outfile, "if"); break;
      case sym_comment: print_comment(child, st); break;
    }
  }
}

void print_match_expression(TSNode node, PrpfmtState *st) {
  bool seen_lbrace = false;
  bool arm_started = false;
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_LBRACE) seen_lbrace = true;
    if (fn) {
      if (strcmp(fn, "init") == 0) {
        fprintf(st->outfile, " ");
        uint32_t cc = ts_node_child_count(child);
        if (cc > 0) {
          for (uint32_t j = 0; j < cc; j++) {
            TSNode c2 = ts_node_child(child, j);
            if (ts_node_is_named(c2)) print_statement(c2, st, true);
            else {
              char *text = get_node_text(c2, st->source_code);
              if (text) {
                if (strcmp(text, ";") == 0) fprintf(st->outfile, "; ");
                else fprintf(st->outfile, "%s", text);
                free(text);
              }
            }
          }
        } else print_statement(child, st, true);
      } else if (strcmp(fn, "condition") == 0) {
        if (seen_lbrace) {
          if (!arm_started) { print_indent(st); arm_started = true; }
          if (symbol == anon_sym_else) fprintf(st->outfile, "else");
          else if (symbol == sym_expression_list) print_expression_list(child, st);
          else {
             print__expression(child, st, true);
             if (i + 1 < ts_node_child_count(node)) {
                const char *nfn = ts_node_field_name_for_child(node, i + 1);
                if (nfn && strcmp(nfn, "condition") == 0) fprintf(st->outfile, " ");
             }
          }
        } else { fprintf(st->outfile, " "); print__expression(child, st, true); }
      } else if (strcmp(fn, "match_list") == 0) print_match_list(child, st);
      else if (strcmp(fn, "code") == 0) { fprintf(st->outfile, " "); print_scope_statement(child, st, false); fprintf(st->outfile, "\n"); arm_started = false; }
      continue;
    }
    switch (symbol) {
      case anon_sym_match: fprintf(st->outfile, "match"); break;
      case anon_sym_LBRACE: fprintf(st->outfile, " {\n"); st->indent_level++; break;
      case anon_sym_RBRACE: st->indent_level--; print_indent(st); fprintf(st->outfile, "}"); break;
      case sym_comment: print_comment(child, st); break;
      default: {
        if (seen_lbrace && !arm_started && ts_node_is_named(child)) { print_indent(st); arm_started = true; }
        char *text = get_node_text(child, st->source_code);
        if (text) {
           if (seen_lbrace && !arm_started && !ts_node_is_named(child) && strcmp(text, "}") != 0 && strcmp(text, ",") != 0) { print_indent(st); arm_started = true; }
           fprintf(st->outfile, "%s", text);
           if (strcmp(text, "in") == 0 || strcmp(text, "is") == 0 || strcmp(text, "has") == 0 || strcmp(text, "==") == 0 || strcmp(text, "!=") == 0 || strcmp(text, "match") == 0) fprintf(st->outfile, " ");
           free(text);
        }
      } break;
    }
  }
}

void print_match_list(TSNode node, PrpfmtState *st) {
  bool arm_started = false;
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "condition") == 0) {
        if (!arm_started) { print_indent(st); arm_started = true; }
        if (ts_node_grammar_symbol(child) == anon_sym_else) fprintf(st->outfile, "else");
        else if (ts_node_grammar_symbol(child) == sym_binary_compare_op) { print_match_compare_op(child, st); fprintf(st->outfile, " "); }
        else if (ts_node_grammar_symbol(child) == sym_expression_list) print_expression_list(child, st);
        else {
           for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
              TSNode c2 = ts_node_child(child, j);
              if (ts_node_grammar_symbol(c2) == sym_binary_compare_op) { print_match_compare_op(c2, st); fprintf(st->outfile, " "); }
              else if (ts_node_grammar_symbol(c2) == sym_expression_list) print_expression_list(c2, st);
           }
        }
      }
      if (strcmp(fn, "code") == 0) { fprintf(st->outfile, " "); print_scope_statement(child, st, false); fprintf(st->outfile, "\n"); arm_started = false; }
    } else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_match_compare_op(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) { fprintf(st->outfile, "%s", text); free(text); }
}

void print_for_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (fn) {
      if (strcmp(fn, "attributes") == 0) { fprintf(st->outfile, "::"); print_attribute_list(child, st); }
      else if (strcmp(fn, "init") == 0) print_stmt_list(child, st);
      else if (strcmp(fn, "data") == 0) print_expression_list(child, st);
      else if (strcmp(fn, "code") == 0) { fprintf(st->outfile, " "); print_scope_statement(child, st, false); }
      continue;
    }
    if (symbol == anon_sym_for) fprintf(st->outfile, "for ");
    else if (symbol == anon_sym_in) fprintf(st->outfile, " in ");
    else if (symbol == sym_comment) print_comment(child, st);
    else if (ts_node_is_named(child)) print_typed_identifier(child, st);
  }
}

void print_while_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "attributes") == 0) { fprintf(st->outfile, "::"); print_attribute_list(child, st); }
      else if (strcmp(fn, "init") == 0) print_stmt_list(child, st);
      else if (strcmp(fn, "condition") == 0) { fprintf(st->outfile, " "); print__expression(child, st, true); }
      else if (strcmp(fn, "code") == 0) { fprintf(st->outfile, " "); print_scope_statement(child, st, false); }
      continue;
    }
    if (ts_node_grammar_symbol(child) == anon_sym_while) fprintf(st->outfile, "while ");
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_loop_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "attributes") == 0) { fprintf(st->outfile, "::"); print_attribute_list(child, st); }
      else if (strcmp(fn, "init") == 0) print_stmt_list(child, st);
      else if (strcmp(fn, "code") == 0) { fprintf(st->outfile, " "); print_scope_statement(child, st, false); }
      continue;
    }
    if (ts_node_grammar_symbol(child) == anon_sym_loop) fprintf(st->outfile, "loop ");
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_control_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    switch (symbol) {
      case sym_return_statement: print_return_statement(child, st); break;
      case sym_break_statement: print_break_statement(child, st); break;
      case sym_continue_statement: print_continue_statement(child, st); break;
      case sym_comment: print_comment(child, st); break;
      default: { char *text = get_node_text(child, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } } break;
    }
  }
}

void print_return_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn && strcmp(fn, "argument") == 0) { fprintf(st->outfile, " "); print__expression_with_comprehension(child, st, true); }
    else if (ts_node_grammar_symbol(child) == anon_sym_return) fprintf(st->outfile, "return");
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_break_statement(TSNode node, PrpfmtState *st) { (void)node; (void)st; fprintf(st->outfile, "break"); }
void print_continue_statement(TSNode node, PrpfmtState *st) { (void)node; (void)st; fprintf(st->outfile, "continue"); }

/******************************************************************************
 * 4. Assignments & Declarations
 ******************************************************************************/

void print_assignment(TSNode node, PrpfmtState *st, bool spaces) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (fn) {
      if (strcmp(fn, "decl") == 0) { print_var_or_let_or_reg(child, st); }
      else if (strcmp(fn, "lvalue") == 0) {
        if (symbol == sym_lvalue_list) print_lvalue_list(child, st);
        else print_lvalue_item(child, st);
      } else if (strcmp(fn, "type") == 0) print_type_cast(child, st);
      else if (strcmp(fn, "operator") == 0) print_assignment_operator(child, st, spaces);
      else if (strcmp(fn, "delay") == 0) print_assignment_delay(child, st);
      else if (strcmp(fn, "rvalue") == 0) {
        if (symbol == sym_enum_definition) print_enum_definition(child, st);
        else if (symbol == sym_ref_identifier) print_ref_identifier(child, st);
        else print__expression_with_comprehension(child, st, true);
      }
      continue;
    }
    switch (symbol) {
      case anon_sym_wrap: fprintf(st->outfile, "wrap "); break;
      case anon_sym_sat: fprintf(st->outfile, "sat "); break;
      case sym_comment: print_comment(child, st); break;
      default: {
        char *text = get_node_text(child, st->source_code);
        if (text) { fprintf(st->outfile, "%s", text); free(text); }
      } break;
    }
  }
}

void print_lvalue_item(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  switch (symbol) {
    case sym_typed_identifier: print_typed_identifier(node, st); break;
    case sym_identifier: print_identifier(node, st); break;
    case sym_dot_expression: print_dot_expression(node, st); break;
    case sym_member_selection: print_member_selection(node, st); break;
    case sym_bit_selection: print_bit_selection(node, st); break;
    case sym_attribute_read: print_attribute_read(node, st); break;
    case sym__complex_identifier: print_complex_identifier(node, st); break;
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          fprintf(st->outfile, "%s", text);
          free(text);
        }
      } else {
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
          TSNode child = ts_node_child(node, i);
          const char *fn = ts_node_field_name_for_child(node, i);
          TSSymbol s2 = ts_node_grammar_symbol(child);
          if (fn) {
            if (strcmp(fn, "identifier") == 0) print_complex_identifier(child, st);
            else if (strcmp(fn, "type") == 0) print_type_cast(child, st);
          } else if (s2 == sym_comment) print_comment(child, st);
          else if (ts_node_is_named(child)) print_lvalue_item(child, st);
        }
      }
    } break;
  }
}

void print_lvalue_list(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_is_named(child)) {
      if (ts_node_grammar_symbol(child) == sym_lvalue_item) print_lvalue_item(child, st);
      else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
      else print_lvalue_item(child, st);
    } else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        if (strcmp(text, ",") == 0) fprintf(st->outfile, ", ");
        else fprintf(st->outfile, "%s", text);
        free(text);
      }
    }
  }
}

void print_declaration_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "decl") == 0) { print_var_or_let_or_reg(child, st); }
      else if (strcmp(fn, "lvalue") == 0) {
        if (ts_node_grammar_symbol(child) == sym_typed_identifier_list) print_typed_identifier_list(child, st);
        else print_typed_identifier(child, st);
      }
      continue;
    }
    if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_await_decl(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_grammar_symbol(child) == anon_sym_await) fprintf(st->outfile, "await ");
    else if (ts_node_grammar_symbol(child) == sym_tuple_sq) print_tuple_sq(child, st);
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_enum_assignment(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "name") == 0) print_identifier(child, st);
      else if (strcmp(fn, "values") == 0) print_tuple(child, st);
      else if (strcmp(fn, "body") == 0) print_arg_list(child, st);
      continue;
    }
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_enum) fprintf(st->outfile, "enum ");
    else if (symbol == anon_sym_variant) fprintf(st->outfile, "variant ");
    else if (symbol == anon_sym_EQ) fprintf(st->outfile, " = ");
    else if (symbol == sym_comment) print_comment(child, st);
  }
}

void print_enum_definition(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn && strcmp(fn, "input") == 0) { print_arg_list(child, st); continue; }
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_enum) fprintf(st->outfile, "enum ");
    else if (symbol == anon_sym_variant) fprintf(st->outfile, "variant ");
    else if (symbol == sym_comment) print_comment(child, st);
  }
}

void print_assignment_operator(TSNode node, PrpfmtState *st, bool spaces) {
  char *text = get_node_text(node, st->source_code);
  if (text) { if (spaces) fprintf(st->outfile, " %s ", text); else fprintf(st->outfile, "%s", text); free(text); }
}

void print_assignment_delay(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_AT) fprintf(st->outfile, "@");
    else if (symbol == anon_sym_LBRACK) fprintf(st->outfile, "[");
    else if (symbol == anon_sym_RBRACK) fprintf(st->outfile, "]");
    else if (symbol == sym_attribute_list) print_attribute_list(child, st);
    else if (symbol == sym_comment) print_comment(child, st);
  }
}

void print_spawn_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_spawn) fprintf(st->outfile, "spawn ");
    else if (symbol == sym_function_call_expression) print_function_call_expression(child, st);
    else if (symbol == sym_comment) print_comment(child, st);
  }
}

/******************************************************************************
 * 5. Functions & Parameters
 ******************************************************************************/

void print_lambda(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (fn) {
      if (strcmp(fn, "func_type") == 0) { char *text = get_node_text(child, st->source_code); if (text) { fprintf(st->outfile, "%s ", text); free(text); } }
      else if (strcmp(fn, "name") == 0) print_identifier(child, st);
      else if (strcmp(fn, "code") == 0) { fprintf(st->outfile, " "); print_scope_statement(child, st, false); }
      continue;
    }
    if (symbol == sym_function_definition_decl) print_function_definition_decl(child, st);
    else if (symbol == sym_comment) print_comment(child, st);
  }
}

void print_function_definition(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "lvalue") == 0) print_complex_identifier(child, st);
      else if (strcmp(fn, "condition") == 0) { fprintf(st->outfile, " where "); print_expression_list(child, st); }
      else if (strcmp(fn, "verification") == 0) print_func_def_verification(child, st);
      else if (strcmp(fn, "code") == 0) { fprintf(st->outfile, " "); print_scope_statement(child, st, false); }
      continue;
    }
    if (ts_node_grammar_symbol(child) == sym_function_definition_decl) print_function_definition_decl(child, st);
  }
}

void print_function_definition_decl(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "generic") == 0) { fprintf(st->outfile, "<"); print_typed_identifier_list(child, st); fprintf(st->outfile, ">"); }
      else if (strcmp(fn, "capture") == 0) print_tuple_sq(child, st);
      else if (strcmp(fn, "pipe_config") == 0) { fprintf(st->outfile, "::"); print_attribute_list(child, st); }
      else if (strcmp(fn, "input") == 0) print_arg_list(child, st);
      else if (strcmp(fn, "output") == 0) {
        TSSymbol s = ts_node_grammar_symbol(child);
        if (s == anon_sym_DASH_GT) fprintf(st->outfile, " -> ");
        else if (s == sym_arg_list) print_arg_list(child, st);
        else if (ts_node_is_named(child)) print_typed_identifier(child, st);
      }
      continue;
    }
  }
}

void print_func_def_verification(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_child_count(child) == 0) { char *text = get_node_text(child, st->source_code); if (text) { fprintf(st->outfile, " %s ", text); free(text); } }
    else print__expression(child, st, true);
  }
}

void print_arg_list(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (ts_node_is_named(child)) {
      if (symbol == sym_arg_list) print_arg_item_list(child, st);
      else if (symbol == sym_comment) print_comment(child, st);
      else print_arg_item(child, st);
    } else {
      char *text = get_node_text(child, st->source_code);
      if (text) {
        if (strcmp(text, "(") == 0) fprintf(st->outfile, "(");
        else if (strcmp(text, ")") == 0) fprintf(st->outfile, ")");
        else if (strcmp(text, ",") == 0) fprintf(st->outfile, ", ");
        else {
           fprintf(st->outfile, "%s", text);
           if (strcmp(text, "reg") == 0 || strcmp(text, "ref") == 0 || strcmp(text, "const") == 0 || strcmp(text, "mut") == 0) fprintf(st->outfile, " ");
        }
        free(text);
      }
    }
  }
}

void print_arg_item_list(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_is_named(child)) print_arg_item(child, st);
    else { char *text = get_node_text(child, st->source_code); if (text) { if (strcmp(text, ",") == 0) fprintf(st->outfile, ", "); else fprintf(st->outfile, "%s", text); free(text); } }
  }
}

void print_arg_item(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym_arg_list && ts_node_child_count(node) > 0) { print_arg_item(ts_node_child(node, 0), st); return; }
  switch (symbol) {
    case sym_typed_identifier: print_typed_identifier(node, st); break;
    case sym_identifier: print_identifier(node, st); break;
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          fprintf(st->outfile, "%s", text);
          free(text);
        }
      } else {
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
          TSNode child = ts_node_child(node, i);
          const char *fn = ts_node_field_name_for_child(node, i);
          if (fn) {
            if (strcmp(fn, "mod") == 0) { char *text = get_node_text(child, st->source_code); if (text) { fprintf(st->outfile, "%s ", text); free(text); } }
            else if (strcmp(fn, "definition") == 0) {
                for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
                  TSNode c2 = ts_node_child(child, j);
                  if (ts_node_grammar_symbol(c2) == sym_typed_identifier) print_typed_identifier(c2, st);
                  else if (ts_node_grammar_symbol(c2) == anon_sym_EQ) fprintf(st->outfile, " = ");
                  else if (ts_node_is_named(c2)) print__expression_with_comprehension(c2, st, true);
                }
            }
          } else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
          else if (ts_node_is_named(child)) print_arg_item(child, st);
        }
      }
    } break;
  }
}

void print_function_call_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "function") == 0) print_complex_identifier(child, st);
      else if (strcmp(fn, "argument") == 0) { fprintf(st->outfile, " "); print_expression_list(child, st); }
      continue;
    }
    TSSymbol s = ts_node_grammar_symbol(child);
    if (s == anon_sym_SEMI || s == sym__automatic_semicolon) print__semicolon(child, st);
    else if (s == sym_comment) print_comment(child, st);
  }
}

void print_function_call_expression(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "function") == 0) print_complex_identifier(child, st); else if (strcmp(fn, "argument") == 0) print_tuple(child, st); continue; }
    if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

/******************************************************************************
 * 6. Expressions & Selection
 ******************************************************************************/

static void print_binary_op(TSNode node, PrpfmtState *st, bool spaces) {
  char *text = get_node_text(node, st->source_code);
  if (text) { if (spaces) fprintf(st->outfile, " %s ", text); else fprintf(st->outfile, "%s", text); free(text); }
}

void print__expression(TSNode node, PrpfmtState *st, bool is_inline) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if ((symbol == sym__expression || symbol == sym__expression_with_comprehension) && ts_node_child_count(node) > 0) {
    print__expression(ts_node_child(node, 0), st, is_inline);
    return;
  }
  switch (symbol) {
    case sym__binary_times: print_binary_times(node, st); break;
    case sym__binary_other: print_binary_other(node, st); break;
    case sym__binary_compare: print_binary_compare(node, st); break;
    case sym__binary_logical: print_binary_logical(node, st); break;
    case sym_attribute_read: print_attribute_read(node, st); break;
    case sym_if_expression: print_if_expression(node, st, is_inline); break;
    case sym_match_expression: print_match_expression(node, st); break;
    case sym_type_specification: print_type_specification(node, st); break;
    case sym_unary_expression: print_unary_expression(node, st); break;
    case sym__complex_identifier: print_complex_identifier(node, st); break;
    case sym_constant: print_constant(node, st); break;
    case sym_function_call_expression: print_function_call_expression(node, st); break;
    case sym_lambda: print_lambda(node, st); break;
    case sym_tuple: print_tuple(node, st); break;
    case sym_optional_expression: print_optional_expression(node, st); break;
    case sym__restricted_expression: print__restricted_expression(node, st); break;
    default: print__restricted_expression(node, st); break;
  }
}

void print__expression_with_comprehension(TSNode node, PrpfmtState *st, bool is_inline) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym__expression_with_comprehension && ts_node_child_count(node) > 0) {
    print__expression_with_comprehension(ts_node_child(node, 0), st, is_inline);
    return;
  }
  if (symbol == sym_for_comprehension) print_for_comprehension(node, st);
  else print__expression(node, st, is_inline);
}

void print__restricted_expression(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym__restricted_expression && ts_node_child_count(node) > 0) {
    print__restricted_expression(ts_node_child(node, 0), st);
    return;
  }
  switch (symbol) {
    case sym__complex_identifier: print_complex_identifier(node, st); break;
    case sym_constant: print_constant(node, st); break;
    case sym_function_call_expression: print_function_call_expression(node, st); break;
    case sym_lambda: print_lambda(node, st); break;
    case sym_tuple: print_tuple(node, st); break;
    case sym_tuple_sq: print_tuple_sq(node, st); break;
    case sym_optional_expression: print_optional_expression(node, st); break;
    case sym_if_expression: print_if_expression(node, st, true); break;
    case sym_match_expression: print_match_expression(node, st); break;
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) {
          fprintf(st->outfile, "%s", text);
          free(text);
        }
      } else {
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) print__restricted_expression(ts_node_child(node, i), st);
      }
    } break;
  }
}

void print_binary_times(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "operand") == 0) print__expression(child, st, true); else if (strcmp(fn, "operator") == 0) print_binary_op(child, st, false); }
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_binary_other(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "operand") == 0) print__expression(child, st, true); else if (strcmp(fn, "operator") == 0) print_binary_op(child, st, true); }
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_binary_compare(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "operand") == 0) print__expression(child, st, true); else if (strcmp(fn, "operator") == 0) print_binary_op(child, st, true); }
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_binary_logical(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "operand") == 0) print__expression(child, st, true); else if (strcmp(fn, "operator") == 0) print_binary_op(child, st, true); }
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_unary_expression(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "operator") == 0) { char *text = get_node_text(child, st->source_code); if (text) { fprintf(st->outfile, "%s", text); if (strcmp(text, "not") == 0) fprintf(st->outfile, " "); free(text); } }
      else if (strcmp(fn, "argument") == 0) print__expression(child, st, true);
    } else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_dot_expression(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol s = ts_node_grammar_symbol(child);
    if (fn && strcmp(fn, "item") == 0) { print__expression(child, st, true); }
    else if (s == anon_sym_DOT) { fprintf(st->outfile, "."); }
    else if (ts_node_is_named(child)) {
      if (s == sym_comment) print_comment(child, st);
      else print_complex_identifier(child, st);
    } else {
       char *text = get_node_text(child, st->source_code);
       if (text) { fprintf(st->outfile, "%s", text); free(text); }
    }
  }
}

void print_optional_expression(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "argument") == 0) print__expression(child, st, true); else if (strcmp(fn, "operator") == 0) fprintf(st->outfile, "?"); }
  }
}

void print_type_specification(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol s = ts_node_grammar_symbol(child);
    if (fn) {
      if (strcmp(fn, "argument") == 0) print__restricted_expression(child, st);
      else if (strcmp(fn, "type") == 0 && ts_node_is_named(child)) print__type(child, st);
      continue;
    }
    if (s == anon_sym_COLON) fprintf(st->outfile, ":");
    else if (ts_node_is_named(child)) { if (s == sym_comment) print_comment(child, st); else print__type(child, st); }
  }
}

void print_type_cast(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_COLON) fprintf(st->outfile, ":");
    else if (ts_node_is_named(child)) { if (symbol == sym_comment) print_comment(child, st); else print__type(child, st); }
  }
}

void print_expression_list(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_is_named(child)) { if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st); else print__expression(child, st, true); }
    else { char *text = get_node_text(child, st->source_code); if (text) { if (strcmp(text, ",") == 0) fprintf(st->outfile, ", "); else fprintf(st->outfile, "%s", text); free(text); } }
  }
}

void print_for_comprehension(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "iterable") == 0) print__expression(child, st, true); else if (strcmp(fn, "body") == 0) print__expression(child, st, true); continue; }
    if (ts_node_grammar_symbol(child) == anon_sym_for) fprintf(st->outfile, "for ");
    else if (ts_node_grammar_symbol(child) == anon_sym_in) fprintf(st->outfile, " in ");
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_selection(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym_select && ts_node_child_count(node) > 0) { print_selection(ts_node_child(node, 0), st); return; }
  switch (symbol) {
    case sym_member_selection: print_member_selection(node, st); break;
    case sym_bit_selection: print_bit_selection(node, st); break;
    default: {
      for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        TSNode child = ts_node_child(node, i);
        if (ts_node_grammar_symbol(child) == sym_member_selection) print_member_selection(child, st);
        else if (ts_node_grammar_symbol(child) == sym_bit_selection) print_bit_selection(child, st);
      }
    } break;
  }
}

void print_member_selection(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "argument") == 0) print__expression(child, st, true);
      else if (strcmp(fn, "select") == 0 || strcmp(fn, "selection") == 0) print_select(child, st);
      continue;
    }
    if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_bit_selection(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "argument") == 0) print__expression(child, st, true);
      else if (strcmp(fn, "select") == 0 || strcmp(fn, "selection") == 0) { fprintf(st->outfile, "@"); print_select(child, st); }
      continue;
    }
    if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_attribute_read(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    TSSymbol s = ts_node_grammar_symbol(child);
    if (fn && strcmp(fn, "argument") == 0) { print__expression(child, st, true); }
    else if (s == anon_sym_DOT) { fprintf(st->outfile, "."); }
    else if (s == sym_attribute_list) { print_attribute_list(child, st); }
    else if (s == sym_comment) { print_comment(child, st); }
    else if (ts_node_is_named(child)) { print_attribute_read(child, st); }
    else {
       char *text = get_node_text(child, st->source_code);
       if (text) { fprintf(st->outfile, "%s", text); free(text); }
    }
  }
}

void print_select(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (ts_node_is_named(child)) {
      if (symbol == sym_selection_range) {
        for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
          TSNode c2 = ts_node_child(child, j);
          if (ts_node_is_named(c2)) print__expression(c2, st, true);
          else { char *text = get_node_text(c2, st->source_code); if (text) { if (strcmp(text, ",") == 0) fprintf(st->outfile, ", "); else fprintf(st->outfile, "%s", text); free(text); } }
        }
      } else if (symbol == sym_comment) print_comment(child, st);
      else print__expression(child, st, true);
    } else {
       char *text = get_node_text(child, st->source_code);
       if (text) { fprintf(st->outfile, "%s", text); free(text); }
    }
  }
}

/******************************************************************************
 * 7. Types & Identifiers
 ******************************************************************************/

void print__type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym__type && ts_node_child_count(node) > 0) { print__type(ts_node_child(node, 0), st); return; }
  switch (symbol) {
    case sym__primitive_type: print_primitive_type(node, st); break;
    case sym_array_type: print_array_type(node, st); break;
    case sym_lambda: print_lambda(node, st); break;
    case sym_expression_type: print_expression_type(node, st); break;
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) { fprintf(st->outfile, "%s", text); free(text); }
      } else {
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) print__type(ts_node_child(node, i), st);
      }
    } break;
  }
}

void print_primitive_type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  switch (symbol) {
    case sym_uint_type: print_uint_type(node, st); break;
    case sym_sint_type: print_sint_type(node, st); break;
    case sym_string_type: print_string_type(node, st); break;
    case sym_bool_type: print_bool_type(node, st); break;
    case anon_sym_type: fprintf(st->outfile, "type"); break;
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) { fprintf(st->outfile, "%s", text); free(text); }
      } else {
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) print_primitive_type(ts_node_child(node, i), st);
      }
    } break;
  }
}

void print_uint_type(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }
void print_sint_type(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }
void print_string_type(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }
void print_bool_type(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }
void print_type_type(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }

void print_array_type(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "length") == 0) print_tuple_sq(child, st); else if (strcmp(fn, "base") == 0) print__type(child, st); }
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_expression_type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  switch (symbol) {
    case sym_identifier: print_identifier(node, st); break;
    case sym_dot_expression_type: print_dot_expression_type(node, st); break;
    case sym_function_call_type: print_function_call_type(node, st); break;
    case sym_tuple: print_tuple(node, st); break;
    default: {
      if (ts_node_child_count(node) > 0) { for (uint32_t i = 0; i < ts_node_child_count(node); i++) print_expression_type(ts_node_child(node, i), st); }
      else {
        char *text = get_node_text(node, st->source_code);
        if (text) { fprintf(st->outfile, "%s", text); free(text); }
      }
    } break;
  }
}

void print_dot_expression_type(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_grammar_symbol(child) == anon_sym_DOT) fprintf(st->outfile, ".");
    else if (ts_node_grammar_symbol(child) == sym_identifier) print_identifier(child, st);
    else if (ts_node_grammar_symbol(child) == sym_expression_type) print_expression_type(child, st);
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_function_call_type(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "function") == 0) print_complex_identifier(child, st); else if (strcmp(fn, "argument") == 0) print_tuple(child, st); }
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_type_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "name") == 0) print_identifier(child, st);
      else if (strcmp(fn, "generic") == 0) { fprintf(st->outfile, "<"); print_typed_identifier_list(child, st); fprintf(st->outfile, ">"); }
      else if (strcmp(fn, "definition") == 0) { fprintf(st->outfile, " "); print_tuple(child, st); }
      else if (strcmp(fn, "alias") == 0) print__type(child, st);
      continue;
    }
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_type) fprintf(st->outfile, "type ");
    else if (symbol == anon_sym_EQ) fprintf(st->outfile, " = ");
    else if (symbol == anon_sym_SEMI) print__semicolon(child, st);
    else if (symbol == sym_comment) print_comment(child, st);
  }
}

void print_typed_identifier(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym_identifier) { print_identifier(node, st); return; }
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "identifier") == 0) print_identifier(child, st); else if (strcmp(fn, "type") == 0) print_type_cast(child, st); else if (strcmp(fn, "timing") == 0) print_constant(child, st); continue; }
    TSSymbol s2 = ts_node_grammar_symbol(child);
    if (s2 == anon_sym_AT) fprintf(st->outfile, "@");
    else if (s2 == anon_sym_LBRACK) fprintf(st->outfile, "[");
    else if (s2 == anon_sym_RBRACK) fprintf(st->outfile, "]");
    else if (s2 == sym__timing_sequence) print__timing_sequence(child, st);
  }
}

void print_typed_identifier_list(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_grammar_symbol(child) == anon_sym_COMMA) fprintf(st->outfile, ", ");
    else if (ts_node_grammar_symbol(child) == sym_typed_identifier) print_typed_identifier(child, st);
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_identifier(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }

void print_ref_identifier(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_grammar_symbol(child) == anon_sym_ref) fprintf(st->outfile, "ref ");
    else if (ts_node_grammar_symbol(child) == sym__complex_identifier) print_complex_identifier(child, st);
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_complex_identifier(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym__complex_identifier && ts_node_child_count(node) > 0) { print_complex_identifier(ts_node_child(node, 0), st); return; }
  switch (symbol) {
    case sym_identifier: print_identifier(node, st); break;
    case sym_dot_expression: print_dot_expression(node, st); break;
    case sym_select: print_selection(node, st); break;
    case sym_member_selection: print_member_selection(node, st); break;
    case sym_bit_selection: print_bit_selection(node, st); break;
    case sym_attribute_read: print_attribute_read(node, st); break;
    case sym_timed_identifier: print_timed_identifier(node, st); break;
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) { fprintf(st->outfile, "%s", text); free(text); }
      } else {
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) print_complex_identifier(ts_node_child(node, i), st);
      }
    } break;
  }
}

void print_complex_identifier_list(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_grammar_symbol(child) == anon_sym_COMMA) fprintf(st->outfile, ", ");
    else if (ts_node_grammar_symbol(child) == sym__complex_identifier) print_complex_identifier(child, st);
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_timed_identifier(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "identifier") == 0) print_identifier(child, st); else if (strcmp(fn, "timing") == 0) print__timing_sequence(child, st); continue; }
    if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_var_or_let_or_reg(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "comptime") == 0) fprintf(st->outfile, "comptime ");
      else if (strcmp(fn, "storage") == 0) {
        if (ts_node_grammar_symbol(child) == sym_await_decl) print_await_decl(child, st);
        else { char *text = get_node_text(child, st->source_code); if (text) { fprintf(st->outfile, "%s ", text); free(text); } }
      }
      continue;
    }
    if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

/******************************************************************************
 * 8. Literals & Numbers
 ******************************************************************************/

void print_constant(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  switch (symbol) {
    case sym_integer_literal: print_integer_literal(node, st); break;
    case sym_bool_literal: print_bool_literal(node, st); break;
    case sym_string_literal: case sym__string_literal: print_string_literal(node, st); break;
    case sym_interpolated_string_literal: print_interpolated_string_literal(node, st); break;
    case sym_unknown_literal: print_unknown_literal(node, st); break;
    default: {
      if (ts_node_child_count(node) == 0) {
        char *text = get_node_text(node, st->source_code);
        if (text) { fprintf(st->outfile, "%s", text); free(text); }
      } else {
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) print_constant(ts_node_child(node, i), st);
      }
    } break;
  }
}

void print_integer_literal(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }
void print_bool_literal(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }
void print_string_literal(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }
void print_interpolated_string_literal(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }
void print_unknown_literal(TSNode node, PrpfmtState *st) { char *text = get_node_text(node, st->source_code); if (text) { fprintf(st->outfile, "%s", text); free(text); } }

/******************************************************************************
 * 9. Comments
 ******************************************************************************/

void print_comment(TSNode node, PrpfmtState *st) {
  TSNode prev = ts_node_prev_sibling(node);
  if (ts_node_is_null(prev)) print_comment_newline(node, st);
  else if (ts_node_end_point(prev).row == ts_node_start_point(node).row) print_comment_inline(node, st);
  else print_comment_newline(node, st);
}

void print_comment_inline(TSNode node, PrpfmtState *st) {
  char *node_text = get_node_text(node, st->source_code);
  if (node_text) { check_format_directives(node_text, st); fprintf(st->outfile, " %s", node_text); if (!ts_node_is_null(ts_node_next_sibling(node))) fprintf(st->outfile, "\n"); free(node_text); }
}

void print_comment_newline(TSNode node, PrpfmtState *st) {
  char *node_text = get_node_text(node, st->source_code);
  if (node_text) { check_format_directives(node_text, st); print_indent(st); fprintf(st->outfile, "%s\n", node_text); free(node_text); }
}

/******************************************************************************
 * 10. Special Statements & Attributes
 ******************************************************************************/

void print_import_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "module") == 0) { if (ts_node_grammar_symbol(child) == sym__complex_identifier) print_complex_identifier(child, st); else print_string_literal(child, st); }
      else if (strcmp(fn, "alias") == 0) print_identifier(child, st);
      continue;
    }
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_import) fprintf(st->outfile, "import ");
    else if (symbol == anon_sym_as) fprintf(st->outfile, " as ");
    else if (symbol == anon_sym_SEMI) print__semicolon(child, st);
    else if (symbol == sym_comment) print_comment(child, st);
  }
}

void print_impl_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) { if (strcmp(fn, "name") == 0) print_complex_identifier(child, st); else if (strcmp(fn, "base") == 0) print__type(child, st); }
    else {
       TSSymbol symbol = ts_node_grammar_symbol(child);
       if (symbol == anon_sym_impl) fprintf(st->outfile, "impl ");
       else if (symbol == anon_sym_for) fprintf(st->outfile, " for ");
       else if (symbol == sym_scope_statement) { fprintf(st->outfile, " "); print_scope_statement(child, st, false); }
       else if (symbol == sym_comment) print_comment(child, st);
    }
  }
}

void print_test_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "args") == 0) { fprintf(st->outfile, " "); print_expression_list(child, st); }
      else if (strcmp(fn, "code") == 0) { fprintf(st->outfile, " "); print_scope_statement(child, st, false); }
      continue;
    }
    if (ts_node_grammar_symbol(child) == anon_sym_test) fprintf(st->outfile, "test");
    else if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st);
  }
}

void print_assert_statement(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    const char *fn = ts_node_field_name_for_child(node, i);
    if (fn) {
      if (strcmp(fn, "condition") == 0) print__expression(child, st, true);
      else if (strcmp(fn, "msg") == 0) {
        for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
          TSNode c2 = ts_node_child(child, j);
          if (ts_node_grammar_symbol(c2) == anon_sym_COMMA) fprintf(st->outfile, ", ");
          else print_constant(c2, st);
        }
      }
      continue;
    }
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_always) fprintf(st->outfile, "always ");
    else if (symbol == anon_sym_assert) fprintf(st->outfile, "assert ");
    else if (symbol == anon_sym_cassert) fprintf(st->outfile, "cassert ");
    else if (symbol == anon_sym_SEMI) print__semicolon(child, st);
    else if (symbol == sym_comment) print_comment(child, st);
  }
}

void print_attributes(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    if (symbol == anon_sym_COLON_COLON) fprintf(st->outfile, "::");
    else if (symbol == sym_attribute_list) print_attribute_list(child, st);
    else if (symbol == sym_comment) print_comment(child, st);
  }
}

void print_attribute_list(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    if (ts_node_is_named(child)) { if (ts_node_grammar_symbol(child) == sym_comment) print_comment(child, st); else print__expression(child, st, true); }
    else { char *text = get_node_text(child, st->source_code); if (text) { if (strcmp(text, ",") == 0) fprintf(st->outfile, ", "); else fprintf(st->outfile, "%s", text); free(text); } }
  }
}

void print__semicolon(TSNode node, PrpfmtState *st) { if (ts_node_grammar_symbol(node) == anon_sym_SEMI) fprintf(st->outfile, ";"); }
void print__space(TSNode node, PrpfmtState *st) { (void)node; (void)st; }

void print__timing_sequence(TSNode node, PrpfmtState *st) {
  for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol s = ts_node_grammar_symbol(child);
    if (s == anon_sym_POUND) fprintf(st->outfile, "#");
    else if (s == sym_constant) print_constant(child, st);
    else if (s == sym_comment) print_comment(child, st);
  }
}

/******************************************************************************
 * 11. Utilities
 ******************************************************************************/

void preserve_whitespace(TSNode prev, TSNode curr, PrpfmtState *st) {
  if (ts_node_is_null(prev) || ts_node_is_null(curr)) return;
  TSPoint prev_end = ts_node_end_point(prev);
  TSPoint curr_start = ts_node_start_point(curr);
  if (curr_start.row > prev_end.row + 1) {
    uint32_t blank_lines = curr_start.row - prev_end.row - 1;
    for (uint32_t i = 0; i < blank_lines; i++) fprintf(st->outfile, "\n");
  }
}

char *get_node_text(TSNode node, const char *source_code) {
  uint32_t start_byte = ts_node_start_byte(node);
  uint32_t end_byte = ts_node_end_byte(node);
  uint32_t length = end_byte - start_byte;
  char *text = (char *) malloc(length + 1);
  if (text == NULL) { perror("Failed to allocate memory for node text"); return NULL; }
  strncpy(text, source_code + start_byte, length);
  text[length] = '\0';
  return text;
}
