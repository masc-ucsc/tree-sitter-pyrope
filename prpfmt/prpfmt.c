#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <tree_sitter/api.h>

#include "prpfmt.h"

// Forward declarations for static helper functions
static void unwrap_hidden(TSNode *node, TSSymbol *symbol);
static void emit_operator(TSNode node, PrpfmtState *st, SpacingConfig spacing);
static void ensure_match_arm_started(bool seen_lbrace, bool *arm_started);
static void emit_vertical_transition(PrpfmtState *st, TSNode curr, TSNode next, bool force_break);

static bool has_trailing_comment(TSNode node) {
  uint32_t row = ts_node_end_point(node).row;
  TSNode curr = ts_node_next_sibling(node);
  
  while (!ts_node_is_null(curr) && ts_node_start_point(curr).row == row) {
    if (ts_node_grammar_symbol(curr) == sym_comment) {
      return true;
    }
    curr = ts_node_next_sibling(curr);
  }
  return false;
}

static bool is_block_style_tuple(TSNode node) {
  uint32_t child_count = ts_node_child_count(node);
  if (child_count < 2) return false;

  TSNode open_paren = ts_node_child(node, 0);

  for (uint32_t i = 1; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // 1. Check for Leading Comma (comma at start of its own line)
    if (symbol == anon_sym_COMMA) {
      TSNode prev_sib = ts_node_prev_sibling(child);
      if (!ts_node_is_null(prev_sib) &&
          ts_node_start_point(child).row > ts_node_end_point(prev_sib).row) {
        return true;
      }
    }

    // 2. Check if the first item starts on a different line than '('
    // This indicates the user intended a vertical block.
    if (i == 1) {
      if (ts_node_start_point(child).row > ts_node_start_point(open_paren).row) {
        return true;
      }
    }
  }
  return false;
}

static bool is_assertion(TSNode node, PrpfmtState *st) {
  if (ts_node_grammar_symbol(node) != sym_function_call_expression) {
    return false;
  }

  TSNode func_name = ts_node_child(node, 0);
  if (ts_node_is_null(func_name) || ts_node_grammar_symbol(func_name) != sym_identifier) {
    return false;
  }

  char *name = get_node_text(func_name, st->source_code);
  if (!name) {
    return false;
  }

  bool match = (strcmp(name, "cassert") == 0 ||
                strcmp(name, "assert") == 0 ||
                strcmp(name, "always") == 0);
  free(name);
  return match;
}

static bool is_alignable(TSNode node, PrpfmtState *st) {
  // Rule 1: Any node with a trailing comment is alignable (Clang-style)
  if (has_trailing_comment(node)) {
    return true;
  }

  TSSymbol symbol = ts_node_grammar_symbol(node);

  // Rule 2: A comment itself is only alignable if it is a TRAILING comment.
  if (symbol == sym_comment) {
    TSNode prev = ts_node_prev_sibling(node);
    bool at_start = ts_node_is_null(prev) || (ts_node_end_point(prev).row < ts_node_start_point(node).row);
    return !at_start;
  }

  // Rule 3: Function calls named 'assert', 'cassert', or 'always' are alignable
  if (is_assertion(node, st)) return true;

  // Rule 4: Structural nodes with internal alignment operators
  switch (symbol) {
    case sym_assignment:
    case sym_declaration_statement:
    case sym_enum_assignment:
    case sym_type_statement:
    case sym_typed_identifier:
    case sym__tuple_list:
    case sym__tuple_item:
      return true;
    default:
      return false;
  }
}

bool has_recursive_line_comment(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  if (symbol == sym_comment) {
    char *text = get_node_text(node, st->source_code);
    if (text) {
      const char *ptr = text;
      while (*ptr && isspace((unsigned char)*ptr)) ptr++;
      bool is_line = (strncmp(ptr, "//", 2) == 0);
      free(text);
      if (is_line) return true;
    }
  }
  uint32_t child_count = ts_node_child_count(node);
  for (uint32_t i = 0; i < child_count; i++) {
    if (has_recursive_line_comment(ts_node_child(node, i), st)) {
      return true;
    }
  }
  return false;
}

static bool is_inline_eligible(TSNode node, PrpfmtState *st) {
  if (has_recursive_line_comment(node, st)) {
    return false;
  }

  uint32_t named_child_count = 0;
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    if (symbol == sym_stmt_list) {
      named_child_count += ts_node_named_child_count(child);
    } else if (ts_node_is_named(child)) {
      named_child_count++;
    }
  }

  return named_child_count <= 1;
}

static bool has_blank_line_between(TSNode prev, TSNode curr) {
  if (ts_node_is_null(prev) || ts_node_is_null(curr)) {
    return false;
  }

  return ts_node_start_point(curr).row > ts_node_end_point(prev).row + 1;
}

static void emit_vertical_transition(PrpfmtState *st, TSNode curr, TSNode next, bool force_break) {
  if (ts_node_start_point(next).row >
      ts_node_end_point(curr).row) {
    if (force_break) {
      emit_force_break(st);
    } else {
      emit_line_break(st);
    }

    if (has_blank_line_between(curr, next)) {
      emit_blank_line(st);
    }
  } else {
    emit_space(st);
  }
}

/******************************************************************************
 * 1. Entry & High-Level Dispatch
 ******************************************************************************/

/*
 * print_description is the top-level entry point for the formatter.
 * It iterates through the root nodes of the AST and manages "macro" layout:
 * 1. Strips leading file newlines to ensure a consistent file start.
 * 2. Detects and wraps sequential statements into vertical alignment groups.
 * 3. Handles transitions (newlines/spaces) between top-level statements.
 */
void print_description(TSTree *tree, PrpfmtState *st) {
  TSNode root_node = ts_tree_root_node(tree);
  uint32_t root_child_count = ts_node_child_count(root_node);

  TSNode prev_child = {0};
  bool in_align_group = false;

  for (uint32_t i = 0; i < root_child_count; i++) {
    TSNode child = ts_node_child(root_node, i);

    // Lookahead logic to determine if we should start/end a vertical alignment group
    bool current_alignable = is_alignable(child, st);
    bool next_alignable = false;
    bool blank_line_after = false;

    if (i + 1 < root_child_count) {
      TSNode next = ts_node_child(root_node, i + 1);
      next_alignable = is_alignable(next, st);
      blank_line_after = has_blank_line_between(child, next);
    }

    // Start new alignment group if multiple alignable items follow sequentially
    if (current_alignable && next_alignable && !blank_line_after && !in_align_group) {
      emit_align_group_start(st);
      in_align_group = true;
    }

    print__statement(child, st, prev_child, false);

    // Close alignment group if sequence of alignable items is broken
    if (in_align_group && (!next_alignable || blank_line_after)) {
      emit_align_group_end(st);
      in_align_group = false;
    }

    if (i + 1 < root_child_count) {
      TSNode next = ts_node_child(root_node, i + 1);
      if (st->fmt_on) {
        emit_vertical_transition(st, child, next, true);
      }
    }
    prev_child = child;
  }
  // EOF: trailing newline
  emit_blank_line(st);
}


/*
 * print__statement is the central dispatcher for all Pyrope statements.
 * It serves two primary roles:
 * 1. Raw Mode: If 'prpfmt off' is active, it extracts and prints exact source text.
 * 2. Formatted Mode: It routes the node to the specialized printer function 
 *    (e.g., print_assignment) based on its grammar symbol.
 * Returns: true if raw text was printed, false otherwise.
 */
bool print__statement(TSNode node, PrpfmtState *st, TSNode prev_node, bool is_inline) {
  emit_group_start(st, false, false);
  TSSymbol symbol = ts_node_grammar_symbol(node);
  bool was_fmt_on = st->fmt_on;

  // 1. Peek at directives if it's a comment
  if (symbol == sym_comment) {
    char *text = get_node_text(node, st->source_code);
    if (text) {
      check_format_directives(text, st);
      free(text);
    }
  }

  // 2. Use raw passthrough if we were ALREADY off, 
  // OR if this was the comment that toggled us off.
  // This ensures directives themselves are printed as raw text.
  if (!was_fmt_on || !st->fmt_on) {
    uint32_t start_byte = 0;

    if (ts_node_is_null(prev_node)) {
      start_byte = ts_node_start_byte(node);
    } else {
      start_byte = ts_node_end_byte(prev_node);
    }

    uint32_t end_byte = ts_node_end_byte(node);
    uint32_t length = end_byte - start_byte;
    
    if (length > 0) {
      char *raw_text = (char *)malloc(length + 1);
      if (raw_text) {
        strncpy(raw_text, st->source_code + start_byte, length);
        raw_text[length] = '\0';
        emit_token(st, raw_text);
        free(raw_text);
      }
    }
    emit_group_end(st);

    // If it was a comment, don't trigger a statement separator transition
    if (symbol == sym_comment) {
      return false;
    }
    return true;
  }

  // 3. Special Case: Comments in ENABLED mode handle their own transition logic
  if (symbol == sym_comment) {
    print_comment(node, st, true); // true = already peeked
    emit_group_end(st);
    return false;
  }

  switch (symbol) {
    case anon_sym_wrap:
      emit_token(st, "wrap");
      emit_space(st);
      break;
    case anon_sym_sat:
      emit_token(st, "sat");
      emit_space(st);
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
    case sym_for_statement:
      print_for_statement(node, st);
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
    case anon_sym_SEMI:
    case sym__automatic_semicolon:
      print__semicolon(node, st, SPACE_NONE);
      break;
    default:
      print__expression(node, st, is_inline);
      break;
  }

  emit_group_end(st);
  return false;
}

void check_format_directives(const char *node_text, PrpfmtState *st) {
  const char *ptr = node_text;
  while (*ptr && isspace((unsigned char)*ptr)) ptr++;
  
  if (strncmp(ptr, "//", 2) == 0) {
    ptr += 2;
    while (*ptr && isspace((unsigned char)*ptr)) ptr++;
    
    if (strncmp(ptr, "prpfmt off", 10) == 0) {
      st->fmt_on = false;
    } else if (strncmp(ptr, "prpfmt on", 9) == 0) {
      st->fmt_on = true;
    }
  }
}

/******************************************************************************
 * 2. Structural (Scopes, Lists, Tuples)
 ******************************************************************************/

/*
 * print_scope_statement handles blocks wrapped in braces { ... }.
 * It operates in two modes based on 'is_inline':
 * 1. Standard: Uses FORCE_BREAK to ensure a traditional vertical block layout.
 * 2. Inline:   Uses BREAK_POINT to allow the block to remain on a single line 
 *              (e.g., in lambdas) unless the Solver determines it must explode.
 */
void print_scope_statement(TSNode node, PrpfmtState *st, bool is_inline) {
  uint32_t child_count = ts_node_child_count(node);
  bool originally_one_line = (ts_node_start_point(node).row == ts_node_end_point(node).row);
  
  // Force block format (vertical) at top level (nesting_level == 0)
  // unless explicitly forced inline by parent (e.g. lambda) or st->allow_inline
  bool can_inline = is_inline || st->allow_inline || (st->nesting_level > 0 && originally_one_line);
  
  emit_group_start(st, false, !can_inline);
  st->nesting_level++;
  
  TSNode prev_child = {0};
  bool in_align_group = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    // Alignment logic for statements within scope
    bool current_alignable = is_alignable(child, st);
    bool next_alignable = false;
    bool blank_line_after = false;

    if (i + 1 < child_count) {
      TSNode next = ts_node_child(node, i + 1);
      next_alignable = is_alignable(next, st);
      blank_line_after = has_blank_line_between(child, next);
    }

    if (!is_inline &&
        !originally_one_line &&
        !st->allow_inline &&
        current_alignable &&
        next_alignable &&
        !blank_line_after &&
        !in_align_group) {
      emit_align_group_start(st);
      in_align_group = true;
    }

    // Vertical transitions from previous node
    if (i > 0) {
      TSSymbol prev_sym = ts_node_grammar_symbol(prev_child);

      // Special case: skip mandatory break if we are about to print a trailing comment
      bool has_trailing_comment = false;
      if (symbol == sym_comment &&
          ts_node_start_point(child).row == ts_node_end_point(prev_child).row) {
        has_trailing_comment = true;
      }

      if (has_trailing_comment) {
        emit_space(st);
      } else if (can_inline) {
        emit_break_point(st, 10);
      } else if (prev_sym == anon_sym_LBRACE ||
                 symbol == anon_sym_RBRACE) {
        emit_line_break(st);
      } else {
        emit_vertical_transition(st, prev_child, child, true);
      }
    }

    switch (symbol) {
      case anon_sym_LBRACE:
        emit_token(st, "{");
        emit_indent_inc(st);
        break;
      case anon_sym_RBRACE:
        emit_indent_dec(st);
        emit_token(st, "}");
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym_stmt_list:
      case sym__automatic_semicolon:
        if (symbol == sym_stmt_list) {
          print_stmt_list(child, st);
        } else {
          print__semicolon(child, st, SPACE_NONE);
        }
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        print__statement(child, st, prev_child, can_inline);
        break;
    }

    if (in_align_group &&
        (!next_alignable || blank_line_after)) {
      emit_align_group_end(st);
      in_align_group = false;
    }

    prev_child = child;
  }
  st->nesting_level--;
  emit_group_end(st);
}

/*
 * print_stmt_list handles sequential lists of statements (e.g., inside scopes).
 * Like print_description, it manages the "macro" layout for its children:
 * 1. Detects and wraps sequential statements into vertical alignment groups.
 * 2. Handles transitions (newlines/spaces) between statements.
 * 3. Collapses multiple blank lines into a single consistent one.
 */
void print_stmt_list(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);
  TSNode prev_child = {0};
  bool in_align_group = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    bool was_raw = false;

    bool current_alignable = is_alignable(child, st);
    bool next_alignable = false;
    bool blank_line_after = false;

    if (i + 1 < child_count) {
      TSNode next = ts_node_child(node, i + 1);
      next_alignable = is_alignable(next, st);
      blank_line_after = has_blank_line_between(child, next);
    }

    if (current_alignable && next_alignable && !blank_line_after && !in_align_group) {
      emit_align_group_start(st);
      in_align_group = true;
    }

    emit_group_start(st, false, false); // Don't propagate explosion to individual statements

    switch (symbol) {
      case sym_comment:
        print_comment(child, st, false);
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      default:
        was_raw = print__statement(child, st, prev_child, false);
        break;
    }
    emit_group_end(st);

    if (in_align_group && (!next_alignable || blank_line_after)) {
      emit_align_group_end(st);
      in_align_group = false;
    }

    if (i + 1 < child_count) {
      TSNode next = ts_node_child(node, i + 1);

      if (!was_raw) {
        emit_vertical_transition(st, child, next, true);
      }
    }
    prev_child = child;
  }
}

void print_tuple(TSNode node, PrpfmtState *st) {
  bool is_block = is_block_style_tuple(node);
  emit_group_start(st, is_block, true);

  uint32_t child_count = ts_node_child_count(node);
  TSNode prev_child = {0};
  bool in_align_group = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    if (symbol == anon_sym_LPAREN) {
      emit_token(st, "(");
      if (is_block) {
        emit_anchor_off(st); // Disable inherited assignment anchor
        emit_indent_inc(st);
        emit_soft_break(st, 10);
      } else {
        emit_anchor(st); // Set anchor for hanging indent
      }
      prev_child = child;
      continue;
    }

    if (symbol == anon_sym_RPAREN) {
      if (is_block) {
        emit_soft_break(st, 10);
        emit_indent_dec(st);
      }
      emit_token(st, ")");
    } else {
      bool current_alignable = is_alignable(child, st);
      bool next_alignable = false;
      bool blank_line_after = false;

      if (current_alignable) {
        for (uint32_t j = i + 1; j < child_count; j++) {
          TSNode next = ts_node_child(node, j);
          TSSymbol next_sym = ts_node_grammar_symbol(next);
          if (next_sym != anon_sym_COMMA &&
              next_sym != sym_comment) {
            if (next_sym != anon_sym_RPAREN) {
              next_alignable = is_alignable(next, st);
              blank_line_after = has_blank_line_between(child, next);
            }
            break;
          }
        }
      }

      if (current_alignable &&
          next_alignable &&
          !blank_line_after &&
          !in_align_group) {
        emit_align_group_start(st);
        in_align_group = true;
      }

      // Transition logic
      if (ts_node_start_point(child).row > ts_node_end_point(prev_child).row) {
        emit_line_break(st);
      } else {
        TSSymbol prev_sym = ts_node_grammar_symbol(prev_child);
        if (symbol != anon_sym_COMMA &&
            prev_sym != anon_sym_LPAREN &&
            prev_sym != anon_sym_COMMA) {
          emit_space(st);
        }
      }

      emit_group_start(st, false, false); // FIREWALL
      switch (symbol) {
        case sym_comment:
          print_comment(child, st, false);
          break;
        default:
          print__tuple_list(child, st, is_block ? SPACE_BOTH : SPACE_NONE);
          break;
      }
      emit_group_end(st);

      // Keep group open for trailing comments
      bool next_is_trailing_comment = false;
      if (i + 1 < child_count) {
        TSNode next = ts_node_child(node, i + 1);
        if (ts_node_grammar_symbol(next) == sym_comment &&
            ts_node_start_point(next).row == ts_node_end_point(child).row) {
          next_is_trailing_comment = true;
        }
      }

      if (in_align_group &&
          symbol != anon_sym_COMMA &&
          symbol != sym_comment &&
          !next_is_trailing_comment &&
          (!next_alignable || blank_line_after)) {
        emit_align_group_end(st);
        in_align_group = false;
      }
    }

    prev_child = child;
  }
  emit_group_end(st);
}

/*
 * print_assertion_args is a specialized, "flattened" version of print_tuple
 * used exclusively for cassert/assert calls to ensure relational operators
 * are not isolated in a sub-group, allowing them to align vertically.
 */
void print_assertion_args(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);
  TSNode prev_child = {0};

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    if (symbol == anon_sym_LPAREN) {
      emit_token(st, "(");
      prev_child = child;
      continue;
    }

    if (symbol == anon_sym_RPAREN) {
      emit_token(st, ")");
    } else {
      // Transitions
      if (!ts_node_is_null(prev_child)) {
        if (ts_node_start_point(child).row > ts_node_end_point(prev_child).row) {
          emit_line_break(st);
        } else {
           TSSymbol prev_sym = ts_node_grammar_symbol(prev_child);
           if (symbol != anon_sym_COMMA && prev_sym != anon_sym_LPAREN && prev_sym != anon_sym_COMMA) {
             emit_space(st);
           }
        }
      }

      switch (symbol) {
        case sym_comment:
          print_comment(child, st, false);
          break;
        default:
          print__tuple_list(child, st, SPACE_NONE);
          break;
      }
    }
    prev_child = child;
  }
}

void print_tuple_sq(TSNode node, PrpfmtState *st) {
  bool is_block = is_block_style_tuple(node);
  emit_group_start(st, is_block, true);

  uint32_t child_count = ts_node_child_count(node);
  TSNode prev_child = {0};
  bool in_align_group = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    if (symbol == anon_sym_LBRACK) {
      emit_token(st, "[");
      if (is_block) {
        emit_anchor_off(st); // Disable inherited anchor
        emit_indent_inc(st);
        emit_soft_break(st, 10);
      } else {
        emit_anchor(st); // Set anchor for hanging indent
      }
      prev_child = child;
      continue;
    }

    // Transitions for all nodes (including closing bracket and comments)
    if (symbol != sym_comment && symbol != anon_sym_RBRACK) {
      if (ts_node_start_point(child).row > ts_node_end_point(prev_child).row) {
        emit_line_break(st);
      } else {
        TSSymbol prev_sym = ts_node_grammar_symbol(prev_child);
        if (symbol != anon_sym_RBRACK &&
            symbol != anon_sym_COMMA &&
            prev_sym != anon_sym_LBRACK &&
            prev_sym != anon_sym_COMMA) {
          emit_space(st);
        }
      }
    }

    if (symbol == anon_sym_RBRACK) {
      if (is_block) {
        emit_soft_break(st, 10);
        emit_indent_dec(st);
      }
      emit_token(st, "]");
    } else {
      bool current_alignable = is_alignable(child, st);
      bool next_alignable = false;
      bool blank_line_after = false;

      if (current_alignable) {
        for (uint32_t j = i + 1; j < child_count; j++) {
          TSNode next = ts_node_child(node, j);
          TSSymbol next_sym = ts_node_grammar_symbol(next);
          if (next_sym != anon_sym_COMMA &&
              next_sym != sym_comment) {
            if (next_sym != anon_sym_RBRACK) {
              next_alignable = is_alignable(next, st);
              blank_line_after = has_blank_line_between(child, next);
            }
            break;
          }
        }
      }

      if (current_alignable &&
          next_alignable &&
          !blank_line_after &&
          !in_align_group) {
        emit_align_group_start(st);
        in_align_group = true;
      }

      emit_group_start(st, false, false); // FIREWALL
      switch (symbol) {
        case sym_comment:
          print_comment(child, st, false);
          break;
        default:
          print__tuple_list(child, st, is_block ? SPACE_BOTH : SPACE_NONE);
          break;
      }
      emit_group_end(st);

      // Keep group open for trailing comments
      bool next_is_trailing_comment = false;
      if (i + 1 < child_count) {
        TSNode next = ts_node_child(node, i + 1);
        if (ts_node_grammar_symbol(next) == sym_comment &&
            ts_node_start_point(next).row == ts_node_end_point(child).row) {
          next_is_trailing_comment = true;
        }
      }

      if (in_align_group &&
          symbol != anon_sym_COMMA &&
          symbol != sym_comment &&
          !next_is_trailing_comment &&
          (!next_alignable || blank_line_after)) {
        emit_align_group_end(st);
        in_align_group = false;
      }
    }

    prev_child = child;
  }
  emit_group_end(st);
}

void print__tuple_list(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  switch (symbol) {
    case anon_sym_COMMA:
      {
        TSNode prev = ts_node_prev_sibling(node);
        if (!ts_node_is_null(prev) &&
            ts_node_end_point(prev).row < ts_node_start_point(node).row) {
          // Leading comma: keep it with the next item
          emit_token(st, ",");
          // Leading comma: no space after leading comma
        } else {
          // Normal comma: add a break point after
          emit_token(st, ",");
          emit_break_point(st, 10);
        }
      }
      break;
    default:
      print__tuple_item(node, st, spacing);
      break;
  }
}

void print__tuple_item(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  switch (symbol) {
    case sym_var_or_let_or_reg:
      print_var_or_let_or_reg(node, st);
      emit_space(st);
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
  bool old_allow = st->allow_inline;
  // Allow inlining only if nested and originally on one line
  if (st->nesting_level > 0 && ts_node_start_point(node).row == ts_node_end_point(node).row) {
    st->allow_inline = true;
  } else {
    st->allow_inline = false;
  }

  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);
  bool header_open = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_unique:
        emit_token(st, "unique");
        emit_space(st);
        break;
      case anon_sym_if:
        emit_token(st, "if");
        emit_space(st);
        emit_anchor(st);
        emit_group_start(st, false, false); // Isolated Header
        header_open = true;
        break;
      case anon_sym_elif:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_anchor_off(st);
        emit_space(st);
        emit_token(st, "elif");
        emit_space(st);
        emit_anchor(st);
        emit_group_start(st, false, false); // Isolated Header
        header_open = true;
        break;
      case anon_sym_else:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_anchor_off(st);
        emit_space(st);
        emit_token(st, "else");
        break;
      case sym_stmt_list:
        // This is the promoted 'init' stmt_list
        print_stmt_list(child, st);
        break;
      case sym_scope_statement:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        print_scope_statement(child, st, is_inline && is_inline_eligible(child, st));
        break;
      case anon_sym_SEMI:
      case sym__semicolon:
      case sym__automatic_semicolon:
        // This is the semicolon from the init clause or a statement terminator
        emit_token(st, ";");
        emit_space(st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn) {
          if (strcmp(fn, "condition") == 0) {
            print__expression(child, st, is_inline);
          } else if (strcmp(fn, "code") == 0 ||
                     strcmp(fn, "else_code") == 0) {
            if (header_open) {
              emit_group_end(st);
              header_open = false;
            }
            emit_anchor_off(st);
            emit_space(st);
            print_scope_statement(child, st, is_inline && is_inline_eligible(child, st));
          }
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, is_inline);
        }
        break;
    }
  }
  if (header_open) {
    emit_group_end(st);
  }
  emit_anchor_off(st);
  emit_group_end(st);
  st->allow_inline = old_allow;
}


void print_match_expression(TSNode node, PrpfmtState *st) {
  bool old_allow = st->allow_inline;
  // Allow inlining only if nested and originally on one line
  if (st->nesting_level > 0 && ts_node_start_point(node).row == ts_node_end_point(node).row) {
    st->allow_inline = true;
  } else {
    st->allow_inline = false;
  }

  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);
  bool seen_lbrace = false;
  bool arm_started = false;
  bool header_open = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_match:
        emit_token(st, "match");
        emit_space(st);
        emit_group_start(st, false, false); // Start Isolated Header
        header_open = true;
        break;
      case anon_sym_LBRACE:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_space(st);
        emit_token(st, "{");
        emit_indent_inc(st);
        st->nesting_level++;
        if (!has_trailing_comment(child)) {
          emit_line_break(st);
        }
        seen_lbrace = true;
        break;
      case anon_sym_RBRACE:
        st->nesting_level--;
        emit_indent_dec(st);
        emit_line_break(st);
        emit_token(st, "}");
        break;
      case sym_stmt_list:
        print_stmt_list(child, st);
        break;
      case sym_expression_list:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        ensure_match_arm_started(seen_lbrace, &arm_started);
        print_expression_list(child, st);
        break;
      case sym_scope_statement:
        emit_space(st);
        print_scope_statement(child, st, false);
        if (fn && (strcmp(fn, "code") == 0 || strcmp(fn, "else_code") == 0)) {
          if (i + 1 < child_count) {
            TSNode next = ts_node_child(node, i + 1);
            if (ts_node_grammar_symbol(next) != anon_sym_RBRACE) {
              emit_vertical_transition(st, child, next, true);
            }
          }
          arm_started = false;
        }
        break;
      case anon_sym_else:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        ensure_match_arm_started(seen_lbrace, &arm_started);
        emit_token(st, "else");
        emit_space(st);
        break;
      case anon_sym_in:
      case anon_sym_BANGin:
      case anon_sym_is:
      case anon_sym_BANGis:
      case anon_sym_has:
      case anon_sym_BANGhas:
      case anon_sym_case:
      case anon_sym_BANGcase:
      case anon_sym_equals:
      case anon_sym_BANGequals:
      case anon_sym_does:
      case anon_sym_BANGdoes:
      case anon_sym_EQ_EQ:
      case anon_sym_BANG_EQ:
      case anon_sym_LT:
      case anon_sym_LT_EQ:
      case anon_sym_GT:
      case anon_sym_GT_EQ:
      case anon_sym_and:
      case anon_sym_BANGand:
      case anon_sym_or:
      case anon_sym_BANGor:
      case anon_sym_AMP:
      case anon_sym_CARET:
      case anon_sym_PIPE:
      case anon_sym_TILDE_AMP:
      case anon_sym_TILDE_CARET:
      case anon_sym_TILDE_PIPE:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        ensure_match_arm_started(seen_lbrace, &arm_started);
        emit_node_text(child, st);
        emit_space(st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "condition") == 0) {
          ensure_match_arm_started(seen_lbrace, &arm_started);
          print__expression(child, st, false);
        } else if (ts_node_is_named(child)) {
          print__expression(child, st, false);
        } else {
          char *text = get_node_text(child, st->source_code);
          if (text) {
            bool is_comma = (strcmp(text, ",") == 0);
            bool is_semi = (strcmp(text, ";") == 0);
            if (!is_comma && !is_semi) {
              ensure_match_arm_started(seen_lbrace, &arm_started);
            }
            emit_token(st, text);
            // Add space after keywords/operators that aren't punctuation
            if (is_semi || (!is_comma && 
                strcmp(text, "{") != 0 && strcmp(text, "}") != 0 && 
                strcmp(text, "(") != 0 && strcmp(text, ")") != 0)) {
              emit_space(st);
            }
            free(text);
          }
        }
        break;
    }
  }
  if (header_open) {
    emit_group_end(st);
  }
  emit_group_end(st);
  st->allow_inline = old_allow;
}

void print_for_statement(TSNode node, PrpfmtState *st) {
  bool old_allow = st->allow_inline;
  // Allow inlining only if nested and originally on one line
  if (st->nesting_level > 0 && ts_node_start_point(node).row == ts_node_end_point(node).row) {
    st->allow_inline = true;
  } else {
    st->allow_inline = false;
  }

  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);
  bool has_init = false;
  bool header_open = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_for:
        emit_token(st, "for");
        emit_space(st);
        emit_anchor(st); // Anchor after keyword
        emit_group_start(st, false, false); // Isolated Header
        header_open = true;
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
      case sym_stmt_list:
        emit_space(st);
        print_stmt_list(child, st);
        has_init = true;
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__semicolon:
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
          emit_space(st);
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
          emit_space(st);
        }
        print_typed_identifier(child, st);
        break;
      case anon_sym_in:
        emit_space(st);
        emit_token(st, "in");
        emit_space(st);
        break;
      case sym_ref_identifier:
        print_ref_identifier(child, st);
        break;
      case sym_expression_list:
        print_expression_list(child, st);
        break;
      case sym_scope_statement:
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  if (header_open) {
    emit_group_end(st);
  }
  emit_group_end(st);
  st->allow_inline = old_allow;
}

void print_while_statement(TSNode node, PrpfmtState *st) {
  bool old_allow = st->allow_inline;
  // Allow inlining only if nested and originally on one line
  if (st->nesting_level > 0 && ts_node_start_point(node).row == ts_node_end_point(node).row) {
    st->allow_inline = true;
  } else {
    st->allow_inline = false;
  }

  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);
  bool has_init = false;
  bool header_open = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_while:
        emit_token(st, "while");
        emit_space(st);
        emit_anchor(st); // Anchor after keyword
        emit_group_start(st, false, false); // Isolated Header
        header_open = true;
        break;
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
      case sym_stmt_list:
        if (fn && strcmp(fn, "init") == 0) {
          emit_space(st);
          print_stmt_list(child, st);
          has_init = true;
        } else {
          print_stmt_list(child, st);
        }
        break;
      case sym_when_unless_cond:
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
        if (header_open) {
          emit_group_end(st);
          header_open = false;
        }
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "condition") == 0) {
          if (!has_init) {
            emit_space(st);
          }
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        break;
    }
  }
  if (header_open) {
    emit_group_end(st);
  }
  emit_group_end(st);
  st->allow_inline = old_allow;
}

/* 
 * Note: print_when_unless_cond and print__semicolon are consolidated
 * into a single case group. While grammatically distinct, they are 
 * AST siblings due to hidden node promotion.
 */
void print_when_unless_cond(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_when:
        emit_space(st);
        emit_token(st, "when");
        emit_space(st);
        break;
      case anon_sym_unless:
        emit_space(st);
        emit_token(st, "unless");
        emit_space(st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "condition") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        break;
    }
  }
}

void print_loop_statement(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
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
        emit_space(st);
        print_stmt_list(child, st);
        break;
      case sym_scope_statement:
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_control_statement(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
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
        print_comment(child, st, false);
        break;
      default:
        emit_node_text(child, st);
        break;
    }
  }
  emit_group_end(st);
}

void print_return_statement(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
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
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "argument") == 0) {
          emit_space(st);
          print__expression_with_comprehension(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          emit_space(st);
          print__expression_with_comprehension(child, st, true);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_break_statement(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_break:
        emit_token(st, "break");
        break;
      case sym_when_unless_cond:
      case sym__semicolon:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_continue_statement(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_continue:
        emit_token(st, "continue");
        break;
      case sym_when_unless_cond:
      case sym__semicolon:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

/******************************************************************************
 * 4. Assignments & Declarations
 ******************************************************************************/

void print_assignment(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  emit_group_start(st, false, true);
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
        {
          char *op_text = get_node_text(child, st->source_code);
          if (op_text) {
            if (spacing & SPACE_BEFORE) {
              emit_space(st);
            }
            if (spacing == SPACE_NONE) {
              emit_token(st, op_text);
            } else {
              emit_align_operator(st, op_text);
            }
            if (spacing & SPACE_AFTER) {
              emit_space(st);
            }
            free(op_text);
          }
        }
        break;
      case sym_enum_definition:
        print_enum_definition(child, st);
        break;
      case sym_ref_identifier:
        print_ref_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        // Handle expression-like rvalues and anonymous tokens (braces)
        if (fn && strcmp(fn, "rvalue") == 0) {
          print__expression_with_comprehension(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression_with_comprehension(child, st, true);
        }
        break;
    }
  }
  emit_group_end(st);
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
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t child_count = ts_node_child_count(node);
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
              print_comment(child, st, false);
              break;
            default:
              if (fn) {
                if (strcmp(fn, "identifier") == 0) {
                  print__complex_identifier(child, st);
                } else if (strcmp(fn, "type") == 0) {
                  print_type_cast(child, st);
                }
              } else if (!ts_node_is_named(child)) {
                emit_node_text(child, st);
              }
              break;
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
        print_comment(child, st, false);
        break;
      case anon_sym_COMMA:
        emit_token(st, ",");
        emit_space(st);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
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
      case anon_sym_LPAREN:
        emit_token(st, "(");
        break;
      case anon_sym_RPAREN:
        emit_token(st, ")");
        break;
      case sym_typed_identifier_list:
        print_typed_identifier_list(child, st);
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_stage_decl(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_stage:
        emit_token(st, "stage");
        break;
      case sym_tuple_sq:
        print_tuple_sq(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
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
        emit_token(st, "enum");
        emit_space(st);
        break;
      case anon_sym_variant:
        emit_token(st, "variant");
        emit_space(st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_EQ:
        emit_space(st);
        emit_token(st, "=");
        emit_space(st);
        break;
      case sym_tuple:
        print_tuple(child, st);
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_enum_definition(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_enum:
        {
          bool next_is_paren = false;
          if (i + 1 < child_count) {
            TSNode next = ts_node_child(node, i + 1);
            TSSymbol s = ts_node_grammar_symbol(next);
            if (s == sym_arg_list || s == sym_tuple) {
              next_is_paren = true;
            }
          }
          if (next_is_paren) {
            emit_token(st, "enum");
          } else {
            emit_token(st, "enum");
            emit_space(st);
          }
        }
        break;
      case anon_sym_variant:
        {
          bool next_is_paren = false;
          if (i + 1 < child_count) {
            TSNode next = ts_node_child(node, i + 1);
            TSSymbol s = ts_node_grammar_symbol(next);
            if (s == sym_arg_list || s == sym_tuple) {
              next_is_paren = true;
            }
          }
          if (next_is_paren) {
            emit_token(st, "variant");
          } else {
            emit_token(st, "variant");
            emit_space(st);
          }
        }
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_assignment_operator(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  emit_operator(node, st, spacing);
}

void print_spawn_statement(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_spawn:
        emit_token(st, "spawn");
        emit_space(st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_EQ:
        emit_space(st);
        emit_token(st, "=");
        emit_space(st);
        break;
      case sym_scope_statement:
        print_scope_statement(child, st, is_inline_eligible(child, st));
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

/******************************************************************************
 * 5. Functions & Parameters
 ******************************************************************************/

void print_lambda(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
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
      case sym_pipe_lambda:
        emit_node_text(child, st);
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
          emit_space(st);
          header_done = true;
        }
        print_identifier(child, st);
        break;
      case sym_function_definition_decl:
        print_function_definition_decl(child, st);
        break;
      case sym_scope_statement:
        emit_space(st);
        print_scope_statement(child, st, is_inline_eligible(child, st));
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_function_definition_decl(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
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
        emit_space(st);
        emit_token(st, "->");
        emit_space(st);
        break;
      case sym_type_cast:
        print_type_cast(child, st);
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_arg_list(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, true);
  if (has_recursive_line_comment(node, st)) {
    emit_force_break(st);
  }

  uint32_t child_count = ts_node_child_count(node);
  bool in_align_group = false;

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_LPAREN:
        emit_token(st, "(");
        emit_indent_inc(st);
        emit_soft_break(st, 10);
        break;
      case anon_sym_RPAREN:
        emit_soft_break(st, 10);
        emit_indent_dec(st);
        emit_token(st, ")");
        break;
      case anon_sym_COMMA:
        emit_token(st, ",");
        emit_break_point(st, 10);
        break;
      case anon_sym_EQ:
        emit_token(st, "=");
        break;
      case anon_sym_DOT_DOT_DOT:
        emit_token(st, "...");
        break;
      case anon_sym_ref:
        emit_token(st, "ref");
        emit_space(st);
        break;
      case anon_sym_const:
        emit_token(st, "const");
        emit_space(st);
        break;
      case anon_sym_mut:
        emit_token(st, "mut");
        emit_space(st);
        break;
      case alias_sym_reg_decl:
      case anon_sym_reg:
        emit_token(st, "reg");
        emit_space(st);
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_arg_list:
        print_arg_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        {
          bool current_alignable = is_alignable(child, st);
          bool next_alignable = false;
          bool blank_line_after = false;

          if (current_alignable) {
            for (uint32_t j = i + 1; j < child_count; j++) {
              TSNode next = ts_node_child(node, j);
              TSSymbol next_sym = ts_node_grammar_symbol(next);
              if (next_sym != anon_sym_COMMA && next_sym != sym_comment) {
                if (next_sym != anon_sym_RPAREN) {
                  next_alignable = is_alignable(next, st);
                  blank_line_after = has_blank_line_between(child, next);
                }
                break;
              }
            }
          }

          if (current_alignable && next_alignable && !blank_line_after && !in_align_group) {
            emit_align_group_start(st);
            in_align_group = true;
          }

          if (ts_node_is_named(child)) {
            print__expression(child, st, true);
          } else {
            emit_node_text(child, st);
          }

          if (in_align_group && symbol != anon_sym_COMMA && symbol != sym_comment && (!next_alignable || blank_line_after)) {
            emit_align_group_end(st);
            in_align_group = false;
          }
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_function_call_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn) {
          if (strcmp(fn, "function") == 0) {
            print__complex_identifier(child, st);
          } else if (strcmp(fn, "argument") == 0) {
            emit_space(st);
            print_expression_list(child, st);
          }
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_function_call_expression(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);
  bool old_in_assert = st->in_assert;

  // Check if this is an assertion call to enable vertical alignment
  TSNode func_name = ts_node_child(node, 0);
  if (!ts_node_is_null(func_name)) {
    char *name = get_node_text(func_name, st->source_code);
    if (name) {
      if (strcmp(name, "cassert") == 0 || strcmp(name, "assert") == 0 || strcmp(name, "always") == 0) {
        st->in_assert = true;
      }
      free(name);
    }
  }

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_tuple:
        if (st->in_assert) {
          print_assertion_args(child, st);
        } else {
          print_tuple(child, st);
        }
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "function") == 0) {
          print__complex_identifier(child, st);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__complex_identifier(child, st);
        }
        break;
    }
  }
  st->in_assert = old_in_assert;
}

/******************************************************************************
 * 6. Expressions & Selection
 ******************************************************************************/

/* 
 * Note: The print__pri1_operand through print__pri4_operand functions from 
 * previous versions have been omitted. These were empty proxies that simply 
 * called print__expression. In this unified architecture, tiered dispatchers 
 * call print__expression directly for all operand types to reduce redundant 
 * function calls and code clutter.
 */

void print__expression(TSNode node, PrpfmtState *st, bool is_inline) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  unwrap_hidden(&node, &symbol);

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
    case sym_dot_expression:
      print_dot_expression(node, st);
      break;
    case sym_if_expression:
      print_if_expression(node, st, is_inline);
      break;
    case sym_match_expression:
      print_match_expression(node, st);
      break;
    case sym_bit_selection:
      print_bit_selection(node, st);
      break;
    case sym_type_specification:
      print_type_specification(node, st);
      break;
    case sym_unary_expression:
      print_unary_expression(node, st);
      break;
    case sym_var_or_let_or_reg:
      print_var_or_let_or_reg(node, st);
      emit_space(st);
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
  unwrap_hidden(&node, &symbol);

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
  unwrap_hidden(&node, &symbol);

  switch (symbol) {
    case sym__complex_identifier:
      print__complex_identifier(node, st);
      break;
    case sym_constant:
      print_constant(node, st);
      break;
    case sym_function_call_expression:
      print_function_call_expression(node, st);
      break;
    case sym_attribute_read:
      print_attribute_read(node, st);
      break;
    case sym_dot_expression:
      print_dot_expression(node, st);
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
    default:
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print__restricted_expression(ts_node_child(node, i), st);
        }
      }
      break;
  }
}

void print_expression_item(TSNode node, PrpfmtState *st) {
  print__expression(node, st, true);
}

void print__binary_times(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, true); // Symmetrical unit
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_binary_times_op:
        {
          char *op_text = get_node_text(child, st->source_code);
          emit_soft_break(st, 100);
          emit_align_math(st, op_text); // Use MATH channel
          emit_soft_space(st);
          if (op_text) {
            free(op_text);
          }
        }
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        emit_group_start(st, false, false); // FIREWALL
        if (fn && strcmp(fn, "operand") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        emit_group_end(st);
        break;
    }
  }
  emit_group_end(st);
}

void print__binary_other(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, true); // Symmetrical unit
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_binary_other_op:
        {
          char *op_text = get_node_text(child, st->source_code);
          emit_break_point(st, 50);
          emit_align_math(st, op_text); // Use MATH channel
          emit_space(st);
          if (op_text) {
            free(op_text);
          }
        }
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        emit_group_start(st, false, false); // FIREWALL
        if (fn && strcmp(fn, "operand") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        emit_group_end(st);
        break;
    }
  }
  emit_group_end(st);
}

void print__binary_compare(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_binary_compare_op:
        {
          char *op_text = get_node_text(child, st->source_code);
          if (op_text) {
            emit_break_point(st, 30);
            if (st->in_assert && (strcmp(op_text, "==") == 0 || strcmp(op_text, "!=") == 0)) {
              emit_align_relational(st, op_text);
            } else {
              emit_token(st, op_text);
            }
            emit_space(st);
            free(op_text);
          }
        }
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        emit_group_start(st, false, false); // FIREWALL
        if (fn && strcmp(fn, "operand") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        emit_group_end(st);
        break;
    }
  }
  emit_group_end(st);
}

void print__binary_logical(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, true); // Symmetrical unit
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_binary_logical_op:
        {
          char *op_text = get_node_text(child, st->source_code);
          emit_break_point(st, 20);
          emit_align_math(st, op_text);
          emit_space(st);
          if (op_text) {
            free(op_text);
          }
        }
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        emit_group_start(st, false, false); // FIREWALL
        if (fn && strcmp(fn, "operand") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        emit_group_end(st);
        break;
    }
  }
  emit_group_end(st);
}

void print_unary_expression(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_not:
        emit_token(st, "not");
        emit_space(st);
        break;
      case anon_sym_BANG:
        emit_token(st, "!");
        break;
      case anon_sym_TILDE:
        emit_token(st, "~");
        break;
      case anon_sym_DASH:
        emit_token(st, "-");
        break;
      case anon_sym_DOT_DOT_DOT:
        emit_token(st, "...");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "argument") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        break;
    }
  }
}

void print_dot_expression(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_DOT:
        emit_soft_break(st, 200); // High penalty: only break if really needed
        emit_token(st, ".");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn &&
            strcmp(fn, "item") == 0) {
          print__expression(child, st, true);
        } else if (ts_node_is_named(child)) {
          print__complex_identifier(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_optional_expression(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_QMARK:
        emit_token(st, "?");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "argument") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        break;
    }
  }
}

void print_type_specification(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_COLON:
        emit_token(st, ":");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn) {
          if (strcmp(fn, "argument") == 0) {
            print__restricted_expression(child, st);
          } else if (strcmp(fn, "type") == 0) {
            print__type(child, st);
          } else if (strcmp(fn, "attribute") == 0) {
            if (symbol == sym_attribute_list) {
              print_attribute_list(child, st);
            } else {
              print__type(child, st);
            }
          }
        } else if (ts_node_is_named(child)) {
          print__type(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_type_cast(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_COLON:
        emit_token(st, ":");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn) {
          if (strcmp(fn, "type") == 0) {
            print__type(child, st);
          } else if (strcmp(fn, "attribute") == 0) {
            emit_node_text(child, st);
          }
        } else if (ts_node_is_named(child)) {
          print__type(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_expression_list(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  emit_indent_inc(st);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case sym_ref_identifier:
        print_ref_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      case anon_sym_COMMA:
        emit_token(st, ",");
        emit_break_point(st, 10);
        break;
      default:
        if (ts_node_is_named(child)) {
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_indent_dec(st);
  emit_group_end(st);
}

void print_for_comprehension(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);
  emit_space(st);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_for:
        emit_token(st, "for");
        emit_space(st);
        break;
      case anon_sym_in:
        emit_space(st);
        emit_token(st, "in");
        emit_space(st);
        break;
      case anon_sym_if:
        emit_space(st);
        emit_token(st, "if");
        emit_space(st);
        break;
      case sym_expression_list:
        print_expression_list(child, st);
        break;
      case sym_stmt_list:
        print_stmt_list(child, st);
        break;
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case sym_typed_identifier_list:
        print_typed_identifier_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

static void handle_select_contents(TSNode node, PrpfmtState *st) {
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    if (symbol == sym_select) {
      print_select(child, st);
    } else if (symbol == sym_comment) {
      print_comment(child, st, false);
    } else if (fn && (strcmp(fn, "reduction") == 0 || strcmp(fn, "extension") == 0)) {
      handle_select_contents(child, st);
    } else if (ts_node_is_named(child)) {
      print__expression(child, st, true);
    } else {
      emit_node_text(child, st);
    }
  }
}

void print_member_selection(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_select:
        print_select(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "argument") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        break;
    }
  }
}

void print_bit_selection(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_select:
        if (ts_node_is_named(child)) {
          print_select(child, st);
        } else {
          handle_select_contents(child, st);
        }
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && (strcmp(fn, "argument") == 0 || 
                   strcmp(fn, "reduction") == 0 || 
                   strcmp(fn, "extension") == 0)) {
          if (ts_node_is_named(child)) {
             print__expression(child, st, true);
          } else {
             emit_node_text(child, st);
          }
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        break;
    }
  }
}

void print_attribute_read(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_DOT:
        emit_soft_break(st, 200);
        emit_token(st, ".");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn &&
            strcmp(fn, "argument") == 0) {
          print__expression(child, st, true);
        } else if (ts_node_is_named(child)) {
          print_attribute_read(child, st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_select(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_LBRACK:
        emit_node_text(child, st);
        emit_indent_inc(st);
        if (!has_trailing_comment(child)) {
          emit_soft_break(st, 0);
        }
        break;
      case anon_sym_RBRACK:
        emit_soft_break(st, 0);
        emit_indent_dec(st);
        emit_node_text(child, st);
        break;
      case sym_selection_range:
        print_selection_range(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          emit_group_start(st, false, false);
          print__expression(child, st, true);
          emit_group_end(st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_selection_range(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t j = 0; j < child_count; j++) {
    TSNode child = ts_node_child(node, j);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (ts_node_is_named(child)) {
          emit_group_start(st, false, false);
          print__expression(child, st, true);
          emit_group_end(st);
        } else {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

/******************************************************************************
 * 7. Types & Identifiers
 ******************************************************************************/

void print__type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  unwrap_hidden(&node, &symbol);

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
    default:
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print__type(ts_node_child(node, i), st);
        }
      }
      break;
  }
}

void print_expression_type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  switch (symbol) {
    case sym_identifier:
      print_identifier(node, st);
      break;
    case sym_constant:
      print_constant(node, st);
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
      if (ts_node_child_count(node) > 0) {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print_expression_type(ts_node_child(node, i), st);
        }
      } else {
        emit_node_text(node, st);
      }
      break;
  }
}

void print_dot_expression_type(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_DOT:
        emit_soft_break(st, 200);
        emit_token(st, ".");
        break;
      case sym_expression_type:
        print_expression_type(child, st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_function_call_type(TSNode node, PrpfmtState *st) {
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
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "function") == 0) {
          print__complex_identifier(child, st);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__complex_identifier(child, st);
        }
        break;
    }
  }
}

void print_array_type(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_tuple_sq:
        print_tuple_sq(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "base") == 0) {
          print__type(child, st);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__type(child, st);
        }
        break;
    }
  }
}

void print__primitive_type(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  unwrap_hidden(&node, &symbol);

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
    default:
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print__primitive_type(ts_node_child(node, i), st);
        }
      }
      break;
  }
}

void print_type_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_type:
        emit_token(st, "type");
        emit_space(st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_LT:
        emit_token(st, "<");
        break;
      case anon_sym_GT:
        emit_token(st, ">");
        break;
      case sym_typed_identifier_list:
        print_typed_identifier_list(child, st);
        break;
      case anon_sym_EQ:
        emit_space(st);
        emit_token(st, "=");
        emit_space(st);
        break;
      case sym_tuple:
        emit_space(st);
        print_tuple(child, st);
        break;
      case anon_sym_comb:
      case anon_sym_mod:
      case anon_sym_pipe:
        emit_node_text(child, st);
        emit_space(st);
        break;
      case sym_function_definition_decl:
        print_function_definition_decl(child, st);
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && (strcmp(fn, "alias") == 0 || strcmp(fn, "type") == 0)) {
          print__type(child, st);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__type(child, st);
        }
        break;
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
    TSSymbol s2 = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (s2) {
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_AT:
        emit_token(st, "@");
        break;
      case anon_sym_LBRACK:
        emit_token(st, "[");
        break;
      case anon_sym_RBRACK:
        emit_token(st, "]");
        break;
      case sym_type_cast:
        print_type_cast(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "identifier") == 0) {
          print_identifier(child, st);
        } else if (fn && strcmp(fn, "type") == 0) {
          print_type_cast(child, st);
        } else if (fn && strcmp(fn, "timing") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        break;
    }
  }
}

void print_typed_identifier_list(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case sym_typed_identifier:
        print_typed_identifier(child, st);
        break;
      case anon_sym_COMMA:
        emit_token(st, ",");
        emit_space(st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_ref_identifier(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_ref:
        emit_token(st, "ref");
        emit_space(st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__complex_identifier(child, st);
        }
        break;
    }
  }
}

void print__complex_identifier(TSNode node, PrpfmtState *st) {
  TSSymbol symbol = ts_node_grammar_symbol(node);
  unwrap_hidden(&node, &symbol);

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
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print__complex_identifier(ts_node_child(node, i), st);
        }
      }
      break;
  }
}

void print_timed_identifier(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case sym_identifier:
        print_identifier(child, st);
        break;
      case anon_sym_AT:
        emit_token(st, "@");
        break;
      case anon_sym_LBRACK:
        emit_token(st, "[");
        break;
      case anon_sym_RBRACK:
        emit_token(st, "]");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "timing") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        break;
    }
  }
}

void print_var_or_let_or_reg(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_comptime:
        emit_token(st, "comptime");
        emit_space(st);
        break;
      case anon_sym_const:
        emit_token(st, "const");
        emit_space(st);
        break;
      case anon_sym_mut:
        emit_token(st, "mut");
        emit_space(st);
        break;
      case anon_sym_reg:
      case alias_sym_reg_decl:
        emit_token(st, "reg");
        emit_space(st);
        break;
      case sym_stage_decl:
        print_stage_decl(child, st);
        emit_space(st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_identifier(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print_uint_type(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print_sint_type(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print_bool_type(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print_string_type(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
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
    default:
      if (ts_node_child_count(node) == 0) {
        emit_node_text(node, st);
      } else {
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
          print_constant(ts_node_child(node, i), st);
        }
      }
      break;
  }
}

void print_integer_literal(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print__simple_number(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print__scaled_number(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print__hex_number(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print__decimal_number(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print__octal_number(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print__binary_number(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print__typed_number(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print_bool_literal(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print_unknown_literal(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print__string_literal(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print_string_literal(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print_interpolated_string_literal(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

void print__format_spec(TSNode node, PrpfmtState *st) {
  emit_node_text(node, st);
}

/******************************************************************************
 * 9. Comments
 ******************************************************************************/

void print_comment(TSNode node, PrpfmtState *st, bool is_prechecked) {
  TSNode prev = ts_node_prev_sibling(node);
  TSNode next = ts_node_next_sibling(node);

  bool at_start = ts_node_is_null(prev) || (ts_node_end_point(prev).row < ts_node_start_point(node).row);
  bool at_end = ts_node_is_null(next) || (ts_node_start_point(next).row > ts_node_end_point(node).row);

  if (at_start && at_end) {
    print_comment_newline(node, st, is_prechecked);
  } else if (at_end) {
    print_comment_trailing(node, st, is_prechecked);
  } else if (at_start) {
    print_comment_inline(node, st, is_prechecked);
  } else {
    print_comment_inline(node, st, is_prechecked);
  }
}

void print_comment_inline(TSNode node, PrpfmtState *st, bool is_prechecked) {
  char *node_text = get_node_text(node, st->source_code);
  if (node_text) {
    if (!is_prechecked) {
      check_format_directives(node_text, st);
    }
    emit_space(st);
    emit_token(st, node_text);
    emit_space(st);
    free(node_text);
  }
}

void print_comment_trailing(TSNode node, PrpfmtState *st, bool is_prechecked) {
  char *node_text = get_node_text(node, st->source_code);
  if (node_text) {
    if (!is_prechecked) {
      check_format_directives(node_text, st);
    }
    emit_space(st);
    emit_align_comment(st, node_text);

    // If it's a line comment and more code follows on a new line in source,
    // we MUST ensure a newline exists so the next token isn't swallowed.
    TSNode next = ts_node_next_sibling(node);
    const char *ptr = node_text;
    while (*ptr && isspace((unsigned char)*ptr)) ptr++;
    
    if (!ts_node_is_null(next) &&
        strncmp(ptr, "//", 2) == 0 &&
        ts_node_start_point(next).row > ts_node_end_point(node).row) {
      emit_line_break(st);
    }

    free(node_text);
  }
}

void print_comment_newline(TSNode node, PrpfmtState *st, bool is_prechecked) {
  char *node_text = get_node_text(node, st->source_code);
  if (node_text) {
    if (!is_prechecked) {
      check_format_directives(node_text, st);
    }
    emit_token(st, node_text);
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
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_import:
        emit_token(st, "import");
        emit_space(st);
        break;
      case anon_sym_as:
        emit_space(st);
        emit_token(st, "as");
        emit_space(st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_string_literal:
      case sym_interpolated_string_literal:
      case sym__string_literal:
        print__string_literal(child, st);
        break;
      case anon_sym_DOT:
        emit_token(st, ".");
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_impl_statement(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_impl:
        emit_token(st, "impl");
        emit_space(st);
        break;
      case anon_sym_for:
        emit_space(st);
        emit_token(st, "for");
        emit_space(st);
        break;
      case sym_identifier:
        print_identifier(child, st);
        break;
      case sym_tuple:
        emit_space(st);
        print_tuple(child, st);
        break;
      case sym_when_unless_cond:
      case anon_sym_SEMI:
      case sym__automatic_semicolon:
        print__semicolon(child, st, SPACE_NONE);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && (strcmp(fn, "trait_name") == 0 || strcmp(fn, "type_name") == 0)) {
          print_identifier(child, st);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
}

void print_test_statement(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_test:
        emit_token(st, "test");
        break;
      case sym_expression_list:
        emit_space(st);
        print_expression_list(child, st);
        break;
      case sym_scope_statement:
        emit_anchor_off(st); // Kill condition anchor before block
        emit_space(st);
        print_scope_statement(child, st, false);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}



void print_attributes(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, false);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_COLON_COLON:
        emit_token(st, "::");
        break;
      case sym_attribute_list:
        print_attribute_list(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        }
        break;
    }
  }
  emit_group_end(st);
}

void print_attribute_list(TSNode node, PrpfmtState *st) {
  emit_group_start(st, false, true);
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);

    switch (symbol) {
      case anon_sym_LBRACK:
        emit_token(st, "[");
        emit_indent_inc(st);
        emit_soft_break(st, 10);
        break;
      case anon_sym_RBRACK:
        emit_soft_break(st, 10);
        emit_indent_dec(st);
        emit_token(st, "]");
        break;
      case anon_sym_COMMA:
        emit_token(st, ",");
        emit_break_point(st, 10);
        break;
      case anon_sym_EQ:
        emit_token(st, "=");
        break;
      case sym_ref_identifier:
        print_ref_identifier(child, st);
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        emit_group_start(st, false, false); // FIREWALL
        if (ts_node_is_named(child)) {
          print__expression(child, st, true);
        } else {
          emit_node_text(child, st);
        }
        emit_group_end(st);
        break;
    }
  }
  emit_group_end(st);
}

/* 
 * Statement finisher: handles optional conditional gate and semicolon.
 * Corresponds to the grammar rule: _semicolon -> when_unless_cond? ;
 */
void print__semicolon(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  TSSymbol symbol = ts_node_grammar_symbol(node);

  if (symbol == sym_when_unless_cond) {
    print_when_unless_cond(node, st);
  } else if (symbol == anon_sym_SEMI || symbol == sym__automatic_semicolon) {
    if (spacing & SPACE_BEFORE) {
      emit_space(st);
    }
    emit_token(st, ";");
    if (spacing & SPACE_AFTER) {
      emit_space(st);
    }
  }
}

void print__timing_sequence(TSNode node, PrpfmtState *st) {
  uint32_t child_count = ts_node_child_count(node);

  for (uint32_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    TSSymbol symbol = ts_node_grammar_symbol(child);
    const char *fn = ts_node_field_name_for_child(node, i);

    switch (symbol) {
      case anon_sym_AT:
        emit_token(st, "@");
        break;
      case anon_sym_POUND:
        emit_token(st, "#");
        break;
      case anon_sym_LBRACK:
        emit_token(st, "[");
        break;
      case anon_sym_RBRACK:
        emit_token(st, "]");
        break;
      case sym_comment:
        print_comment(child, st, false);
        break;
      default:
        if (fn && strcmp(fn, "timing") == 0) {
          print__expression(child, st, true);
        } else if (!ts_node_is_named(child)) {
          emit_node_text(child, st);
        } else {
          print__expression(child, st, true);
        }
        break;
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
      emit_blank_line(st);
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

void emit_node_text(TSNode node, PrpfmtState *st) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    emit_token(st, text);
    free(text);
  }
}

/******************************************************************************
 * Private Static Helpers
 ******************************************************************************/

/*
 * Utility to "unwrap" hidden choice nodes in the AST.
 *
 * USE CRITERIA:
 * - DO use for "Choice-Only" hidden rules (e.g., _expression, _type). These rules
 *   act only as labels for a list of options. By unwrapping, we jump straight
 *   to the actual content for processing in a Unified Switch.
 *
 * - DO NOT use for "Structural" hidden rules (e.g., _binary_times, _semicolon).
 *   These rules have internal components (sequences/operators) that must be
 *   preserved as a group for correct formatting.
 */
static void unwrap_hidden(TSNode *node, TSSymbol *symbol) {
  while (ts_node_child_count(*node) > 0) {
    TSSymbol s = ts_node_grammar_symbol(*node);
    switch (s) {
      case sym__expression:
      case sym__expression_with_comprehension:
      case sym__type:
      case sym__restricted_expression:
      case sym__complex_identifier:
      case sym__primitive_type:
        {
          *node = ts_node_child(*node, 0);
          *symbol = ts_node_grammar_symbol(*node);
          break;
        }
      default:
        {
          return;
        }
    }
  }
}

/*
 * Internal helper for formatting operator tokens.
 * NOT a direct grammar node printer.
 *
 * Helps nodes:
 * - assignment_operator (=, +=, etc.)
 * - binary_times_op (*, /, %)
 * - binary_other_op (+, -, bitwise, etc.)
 * - binary_compare_op (==, !=, in, etc.)
 * - binary_logical_op (and, or, implies)
 */
static void emit_operator(TSNode node, PrpfmtState *st, SpacingConfig spacing) {
  char *text = get_node_text(node, st->source_code);
  if (text) {
    if (spacing & SPACE_BEFORE) {
      emit_space(st);
    }
    emit_token(st, text);
    if (spacing & SPACE_AFTER) {
      emit_space(st);
    }
    free(text);
  }
}

/* Helper for print_match_expression to handle arm indentation and state tracking */
static void ensure_match_arm_started(bool seen_lbrace, bool *arm_started) {
  if (seen_lbrace && !*arm_started) {
    *arm_started = true;
  }
}
