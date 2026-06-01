#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ir.h"
#include "prpfmt.h"

/******************************************************************************
 * Internal Bookkeeping                                                       *
 ******************************************************************************/

// Double the IR buffer capacity if needed
static void ensure_capacity(struct PrpfmtState *st) {
  if (st->buffer.size >= st->buffer.capacity) {
    st->buffer.capacity =
        st->buffer.capacity == 0 ? 1024 : st->buffer.capacity * 2;
    st->buffer.data =
        realloc(st->buffer.data, st->buffer.capacity * sizeof(Token));
  }
}

// Initialize token with default values and provided text
static void init_token(Token *t, TokenType type, char *text) {
  t->type = type;
  t->text = text;
  t->exploded = false;
  t->propagates = true;
  t->target_col = 0;
  t->penalty = 0;
  t->pre_flat_length = 0;
  t->pre_explode_cost = 0;
  t->pre_force_counter = 0;
  t->pre_group_end = -1;
}

// Check if last meaningful IR token was a break (prevent redundant breaks or spaces)
static bool has_recent_break(struct PrpfmtState *st) {
  for (int i = st->buffer.size - 1; i >= 0; i--) {
    TokenType type = st->buffer.data[i].type;
    if (type == TOKEN_NEWLINE || type == TOKEN_FORCE_BREAK ||
        type == TOKEN_BREAK_POINT || type == TOKEN_SOFT_BREAK) {
      return true;
    }
    if (type == TOKEN_TEXT || type == TOKEN_SPACE ||
        type == TOKEN_ALIGN_OPERATOR || type == TOKEN_ALIGN_RELATIONAL ||
        type == TOKEN_ALIGN_COMMENT) {
      return false;
    }
  }
  return true;
}


/******************************************************************************
 * 1. Token IR Emitters                                                       *
 ******************************************************************************/

// Append a text token to the IR buffer
void emit_token(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_TEXT, strdup(text));
  st->buffer.size++;
}

// Append a space token to the IR buffer if not redundant
void emit_space(struct PrpfmtState *st) {
  for (int i = st->buffer.size - 1; i >= 0; i--) {
    TokenType type = st->buffer.data[i].type;

    // Space/newline already emitted
    if (type == TOKEN_SPACE || type == TOKEN_BREAK_POINT ||
        type == TOKEN_NEWLINE || type == TOKEN_FORCE_BREAK) {
      return;
    }

    // Previous text token ends with a space
    if (st->buffer.data[i].text) {
      size_t len = strlen(st->buffer.data[i].text);
      if (len > 0 && isspace((unsigned char)st->buffer.data[i].text[len - 1])) {
        return;
      }
    }

    // Non-text tokens
    if (type == TOKEN_GROUP_START || type == TOKEN_GROUP_END ||
        type == TOKEN_ALIGN_GROUP_START || type == TOKEN_ALIGN_GROUP_END ||
        type == TOKEN_INDENT_INC || type == TOKEN_INDENT_DEC ||
        type == TOKEN_ANCHOR || type == TOKEN_ANCHOR_OFF ||
        type == TOKEN_SOFT_BREAK || type == TOKEN_SOFT_SPACE) {
      continue;
    }

    break;
  }

  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_SPACE, NULL);
  st->buffer.size++;
}

// Append a mandatory blank line token to the IR buffer
void emit_blank_line(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_NEWLINE, NULL);
  st->buffer.size++;
}

// Append a conditional break token: space when flat, newline when exploded
void emit_break_point(struct PrpfmtState *st, int penalty) {
  if (has_recent_break(st)) {
    return;
  }

  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_BREAK_POINT, NULL);
  st->buffer.data[st->buffer.size].penalty = penalty;
  st->buffer.size++;
}

// Append a conditional break token: empty when flat, newline when exploded
void emit_soft_break(struct PrpfmtState *st, int penalty) {
  if (has_recent_break(st)) {
    return;
  }

  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_SOFT_BREAK, NULL);
  st->buffer.data[st->buffer.size].penalty = penalty;
  st->buffer.size++;
}

// Append a conditional space token: empty when flat, space when exploded
void emit_soft_space(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_SOFT_SPACE, NULL);
  st->buffer.size++;
}

// Append a token to increment indentation level
void emit_indent_inc(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_INDENT_INC, NULL);
  st->buffer.size++;
}

// Append a token to decrement indentation level
void emit_indent_dec(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_INDENT_DEC, NULL);
  st->buffer.size++;
}

// Start a smart-wrapping group with optional forced explosion and propagation
void emit_group_start(struct PrpfmtState *st, bool force_explode,
                      bool propagates) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_GROUP_START, NULL);
  st->buffer.data[st->buffer.size].exploded = force_explode;
  st->buffer.data[st->buffer.size].propagates = propagates;
  st->buffer.size++;
}

// End the current smart-wrapping group
void emit_group_end(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_GROUP_END, NULL);
  st->buffer.size++;
}

// Start an alignment block for columnar layout
void emit_align_group_start(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_GROUP_START, NULL);
  st->buffer.size++;
}

// End the current alignment block
void emit_align_group_end(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_GROUP_END, NULL);
  st->buffer.size++;
}

// Append an operator for vertical alignment (e.g., "=")
void emit_align_operator(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_OPERATOR,
             strdup(text));
  st->buffer.size++;
}

// Append a relational operator for vertical alignment (e.g., "==")
void emit_align_relational(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_RELATIONAL,
             strdup(text));
  st->buffer.size++;
}

// Append a math operator for alignment (only when wrapped)
void emit_align_math(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_MATH, strdup(text));
  st->buffer.size++;
}

// Append a comment for vertical alignment
void emit_align_comment(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_COMMENT,
             strdup(text));
  st->buffer.size++;
}

// Set a vertical anchor for hanging indentation
void emit_anchor(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ANCHOR, NULL);
  st->buffer.size++;
}

// Disable current vertical anchor
void emit_anchor_off(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ANCHOR_OFF, NULL);
  st->buffer.size++;
}

// Append a newline if not redundant
void emit_line_break(struct PrpfmtState *st) {
  if (!has_recent_break(st))
    emit_blank_line(st);
}

// Force a newline regardless of group state
void emit_force_break(struct PrpfmtState *st) {
  if (!has_recent_break(st)) {
    ensure_capacity(st);
    init_token(&st->buffer.data[st->buffer.size], TOKEN_FORCE_BREAK, NULL);
    st->buffer.size++;
  }
}

/******************************************************************************
 * 2. Layout Rendering                                                        *
 ******************************************************************************/

// Simulate the rendering of a single token to update column and indentation state
static void simulate_step(Token *t, int *col, int *indent, int *at_start,
                          int indent_size, bool *exp_stack, int *anc_stack,
                          int *stack_ptr, TokenType channel_filter) {
  // Retrieve the current explosion status and anchor column from the stacks
  bool is_exploded = *stack_ptr >= 0 ? exp_stack[*stack_ptr] : false;
  int current_anchor = *stack_ptr >= 0 ? anc_stack[*stack_ptr] : -1;

  // Handle auto-indentation and anchors at the start of a new line
  if (*at_start &&
      (t->type == TOKEN_TEXT || t->type == TOKEN_ALIGN_OPERATOR ||
       t->type == TOKEN_ALIGN_RELATIONAL || t->type == TOKEN_ALIGN_MATH ||
       t->type == TOKEN_ALIGN_COMMENT)) {
    // Determine the horizontal starting point (anchor or left margin)
    int baseline = (current_anchor >= 0) ? current_anchor : 0;
    // Apply indentation and anchor offsets to the column position
    *col += baseline + (*indent * indent_size);
    // Mark the line as started to prevent redundant indentation
    *at_start = 0;
  }

  switch (t->type) {
    case TOKEN_TEXT:
      // Update column based on literal text length
      if (t->text) {
        *col += (int)strlen(t->text);
      }
      break;
    case TOKEN_ALIGN_OPERATOR:
    case TOKEN_ALIGN_RELATIONAL:
    case TOKEN_ALIGN_MATH:
    case TOKEN_ALIGN_COMMENT:
      // Jump to target alignment column if filtering for this channel
      if (t->target_col > 0 &&
          (channel_filter == TOKEN_TEXT || t->type == channel_filter)) {
        *col = t->target_col;
      }

      // Record anchor position for subsequent items
      if ((t->type == TOKEN_ALIGN_OPERATOR ||
           t->type == TOKEN_ALIGN_RELATIONAL) &&
          t->text && *stack_ptr >= 0) {
        anc_stack[*stack_ptr] = *col;
      }

      if (t->text) {
        *col += (int)strlen(t->text);
      }
      break;
    case TOKEN_ANCHOR:
      // Set a manual hanging indent anchor
      if (*stack_ptr >= 0) {
        anc_stack[*stack_ptr] = *col;
      }
      break;
    case TOKEN_ANCHOR_OFF:
      // Clear the current manual anchor
      if (*stack_ptr >= 0) {
        anc_stack[*stack_ptr] = -1;
      }
      break;
    case TOKEN_SPACE:
      (*col)++;
      break;
    case TOKEN_NEWLINE:
    case TOKEN_FORCE_BREAK:
      // Reset column state on mandatory breaks
      *col = 0;
      *at_start = 1;
      break;
    case TOKEN_BREAK_POINT:
      // Break or space depending on group state
      if (is_exploded) {
        *col = 0;
        *at_start = 1;
      } else {
        (*col)++;
      }
      break;
    case TOKEN_SOFT_BREAK:
      // Break or ignore depending on group state
      if (is_exploded) {
        *col = 0;
        *at_start = 1;
      }
      break;
    case TOKEN_SOFT_SPACE:
      // Space or ignore depending on group state
      if (is_exploded) {
        (*col)++;
      }
      break;
    case TOKEN_INDENT_INC:
      (*indent)++;
      break;
    case TOKEN_INDENT_DEC:
      (*indent)--;
      break;
    case TOKEN_GROUP_START:
      // Push group state to the context stacks
      (*stack_ptr)++;
      if (*stack_ptr < 256) {
        // Record token explosion status
        exp_stack[*stack_ptr] = t->exploded;
        // Inherit parent group's anchor (if nested)
        anc_stack[*stack_ptr] = (*stack_ptr > 0) ? anc_stack[*stack_ptr - 1] : -1;
      }
      break;
    case TOKEN_GROUP_END:
      // Pop group state from the context stacks
      if (*stack_ptr > 0) {
        (*stack_ptr)--;
      }
      break;
    default:
      break;
  }
}

// Calculate flat lengths and explosion penalties for all formatting groups in a single pass
static void calculate_group_metrics(struct PrpfmtState *st) {
  int w_stack[1024], w_top = -1;
  int anchor_stack[1024], a_top = -1;
  int cur_off = 0, cur_cost = 0, f_count = 0;

  for (int i = 0; i < st->buffer.size; i++) {
    Token *t = &st->buffer.data[i];
    if (t->type == TOKEN_GROUP_START) {
      // Record starting offsets to compute the total span later
      t->pre_flat_length = cur_off;
      t->pre_explode_cost = cur_cost;
      t->pre_force_counter = f_count;

      if (w_top < 1023) {
        w_stack[++w_top] = i;
      }
    } else if (t->type == TOKEN_GROUP_END) {
      if (w_top >= 0) {
        // Calculate total length and penalty by comparing current offsets to starting offsets
        int idx = w_stack[w_top--];
        Token *st_t = &st->buffer.data[idx];
        int st_f = st_t->pre_force_counter;

        st_t->pre_flat_length = cur_off - st_t->pre_flat_length;
        st_t->pre_explode_cost = cur_cost - st_t->pre_explode_cost;
        st_t->pre_force_counter = (f_count != st_f);
        st_t->pre_group_end = i;
      }
    } else if (t->type == TOKEN_ALIGN_GROUP_START) {
      if (a_top < 1023) {
        anchor_stack[++a_top] = i;
      }
    } else if (t->type == TOKEN_ALIGN_GROUP_END) {
      if (a_top >= 0) {
        st->buffer.data[anchor_stack[a_top--]].pre_group_end = i;
      }
    } else {
      switch (t->type) {
        case TOKEN_TEXT:
        case TOKEN_ALIGN_OPERATOR:
        case TOKEN_ALIGN_RELATIONAL:
        case TOKEN_ALIGN_MATH:
        case TOKEN_ALIGN_COMMENT:
          if (t->text) {
            // Accumulate width for text and operators
            cur_off += (int)strlen(t->text);
          }
          break;
        case TOKEN_SPACE:
          cur_off++;
          break;
        case TOKEN_BREAK_POINT:
          cur_off++;
          cur_cost += t->penalty;
          break;
        case TOKEN_SOFT_BREAK:
          cur_cost += t->penalty;
          break;
        case TOKEN_FORCE_BREAK:
        case TOKEN_NEWLINE:
          // Track mandatory line breaks
          f_count++;
          break;
        default:
          break;
      }
    }
  }
}

// Determine line breaks and column alignments for all tokens in the IR buffer
void prpfmt_solve(struct PrpfmtState *st) {

  // Pre-pass: Measure flat widths and explosion penalties before simulating layout
  calculate_group_metrics(st);
  int col = 0, indent = 0, at_start = 1, s_ptr = -1;
  bool explode_stack[256];
  bool propagate_stack[256];
  int anchor_stack[256];

  // Phase 1: Determine which groups must explode (wrap) based on width and penalties
  // Simulate the token layout sequentially to determine the actual column positions
  for (int i = 0; i < st->buffer.size; i++) {
    Token *t = &st->buffer.data[i];

    if (t->type == TOKEN_GROUP_START) {
      if (!t->exploded) {
        bool forced = (bool)t->pre_force_counter;
        bool parent_exp = (s_ptr >= 0) ? explode_stack[s_ptr] : false;
        bool parent_prop = (s_ptr >= 0) ? propagate_stack[s_ptr] : false;
        bool should_exp = forced;

        if (!should_exp) {
          // Explode if the flat width exceeds max_width and the penalty is acceptable
          int overflow = (col + t->pre_flat_length) - st->max_width;
          should_exp =
              ((overflow > 0 ? overflow * 1000 : 0) > t->pre_explode_cost);
        }

        if (!should_exp && parent_exp && parent_prop && t->propagates) {
          // Propagate explosion downwards if required by the parent group
          should_exp = true;
        }
        t->exploded = should_exp;
      }
    }

    int tmp_s_ptr = s_ptr;
    simulate_step(t, &col, &indent, &at_start, st->indent_size, explode_stack,
                  anchor_stack, &tmp_s_ptr, TOKEN_TEXT);

    if (t->type == TOKEN_GROUP_START) {
      s_ptr++;

      if (s_ptr >= 0 && s_ptr < 256) {
        propagate_stack[s_ptr] = t->propagates;
      }
    } else if (t->type == TOKEN_GROUP_END) {
      if (s_ptr >= 0) {
        s_ptr--;
      }
    }
  }

  // Initialize the main simulation state machine for Phase 2
  int main_col = 0, main_indent = 0, main_at_start = 1, main_stack_ptr = 0;
  bool main_explode_stack[256];
  for (int k = 0; k < 256; k++) {
    main_explode_stack[k] = false;
  }

  int main_anchor_stack[256];
  for (int k = 0; k < 256; k++) {
    main_anchor_stack[k] = -1;
  }

  // Phase 2: Calculate target columns for aligned operators and comments
  for (int i = 0; i < st->buffer.size; i++) {
    Token *main_t = &st->buffer.data[i];

    if (main_t->type == TOKEN_ALIGN_GROUP_START) {
      int base_ind = main_indent, base_sp = main_stack_ptr, group_end_idx = main_t->pre_group_end;

      if (group_end_idx > i) {
        int l_count = 0;

        // Run a temporary sub-simulation to count the total number of lines in the block
        {
          int temp_col = 0, temp_indent = base_ind, temp_at_start = 1, temp_stack_ptr = base_sp;
          bool temp_explode_stack[256];

          for (int k = 0; k <= base_sp && k < 256; k++) {
            temp_explode_stack[k] = main_explode_stack[k];
          }

          int temp_anchor_stack[256];
          for (int k = 0; k <= base_sp && k < 256; k++) {
            temp_anchor_stack[k] = main_anchor_stack[k];
          }

          for (int j = i + 1; j < group_end_idx; j++) {
            if (temp_at_start) {
              l_count++;
            }
            simulate_step(&st->buffer.data[j], &temp_col, &temp_indent, &temp_at_start, st->indent_size,
                          temp_explode_stack, temp_anchor_stack, &temp_stack_ptr, TOKEN_TEXT);
          }
        }

        // Add a safety buffer to the line count to prevent out-of-bounds memory allocation
        l_count += 5;
        TokenType chans[] = {TOKEN_ALIGN_OPERATOR, TOKEN_ALIGN_RELATIONAL,
                             TOKEN_ALIGN_MATH, TOKEN_ALIGN_COMMENT};

        // Solve alignment independently for each "channel" to prevent interference
        for (int c = 0; c < 4; c++) {
          TokenType current_channel = chans[c];

          // has_baseline: true if line has a valid anchor/baseline token
          // has_operator:   true if line contains the target alignment operator
          // measured_width:  calculated column width for operator on this line
          int *has_baseline = calloc(l_count, sizeof(int)),
              *has_operator = calloc(l_count, sizeof(int)),
              *measured_width = calloc(l_count, sizeof(int));
          int col = 0, indent = base_ind, at_start = 1, stack_ptr = base_sp, current_line = -1,
              last_aligned_line = -1;
          bool temp_explode_stack[256];

          for (int k = 0; k <= base_sp && k < 256; k++) {
            temp_explode_stack[k] = main_explode_stack[k];
          }

          int temp_anchor_stack[256];
          for (int k = 0; k <= base_sp && k < 256; k++) {
            temp_anchor_stack[k] = main_anchor_stack[k];
          }

          // Pass A: Scan the block to find the maximum column width for this channel
          for (int j = i + 1; j < group_end_idx; j++) {
            Token *t = &st->buffer.data[j];

            if (at_start) {
              current_line++;
            }

            if (current_line >= 0 && current_line < l_count) {
              // Only consider tokens valid for alignment if they are near the base nesting level (depth + 3 limit)
              if (indent == base_ind && stack_ptr <= base_sp + 3 &&
                  (t->type == TOKEN_TEXT || t->type == TOKEN_ALIGN_OPERATOR ||
                   t->type == TOKEN_ALIGN_RELATIONAL ||
                   t->type == TOKEN_ALIGN_MATH ||
                   t->type == TOKEN_ALIGN_COMMENT)) {
                has_baseline[current_line] = 1;
              }

              if (t->type == current_channel) {
                // Determine if this token should be aligned based on channel and indentation rules
                if (current_line != last_aligned_line && indent == base_ind && stack_ptr <= base_sp + 3 &&
                    (current_channel != TOKEN_ALIGN_MATH || at_start)) {
                  if (current_channel != TOKEN_ALIGN_COMMENT || indent == base_ind) {
                    int anchor_col = col;

                    if (at_start) {
                      // Adjust column based on anchor position if available
                      anchor_col = ((stack_ptr >= 0 && temp_anchor_stack[stack_ptr] >= 0) ? temp_anchor_stack[stack_ptr] : 0) +
                           (indent * st->indent_size);
                    }

                    int measured_val = anchor_col;
                    if (current_channel != TOKEN_ALIGN_COMMENT && t->text) {
                      measured_val += (int)strlen(t->text);
                    }

                    has_operator[current_line] = 1;
                    measured_width[current_line] = measured_val;
                    last_aligned_line = current_line;
                  }
                }
              }
            }

            simulate_step(t, &col, &indent, &at_start, st->indent_size, temp_explode_stack, temp_anchor_stack, &stack_ptr,
                          TOKEN_TEXT);
          }

          int max_line_idx = current_line;
          int *target_col_max = calloc(max_line_idx + 1, sizeof(int));

          // Calculate the target column (target_col_max) for each line
          if (current_channel == TOKEN_ALIGN_COMMENT) {
            int global_max = 0, global_count = 0;

            // Comments align globally across the entire block
            for (int line_idx = 0; line_idx <= max_line_idx; line_idx++) {
              if (has_operator[line_idx]) {
                if (measured_width[line_idx] > global_max) {
                  global_max = measured_width[line_idx];
                }
                global_count++;
              }
            }

            if (global_count > 1) {
              for (int line_idx = 0; line_idx <= max_line_idx; line_idx++) {
                target_col_max[line_idx] = global_max;
              }
            }
          } else {
            int run_max = 0, run_count = 0, start_line = 0;

            // Operators align in contiguous sub-groups (break alignment on empty lines)
            for (int line_idx = 0; line_idx <= max_line_idx; line_idx++) {
              if (has_baseline[line_idx]) {
                if (has_operator[line_idx]) {
                  if (measured_width[line_idx] > run_max) {
                    run_max = measured_width[line_idx];
                  }
                  run_count++;
                } else {
                  for (int run_line_idx = start_line; run_line_idx < line_idx; run_line_idx++) {
                    target_col_max[run_line_idx] = (run_count > 1) ? run_max : 0;
                  }

                  run_max = 0;
                  run_count = 0;
                  start_line = line_idx + 1;
                }
              }
            }

            for (int run_line_idx = start_line; run_line_idx <= max_line_idx; run_line_idx++) {
              target_col_max[run_line_idx] = (run_count > 1) ? run_max : 0;
            }
          }

          col = 0;
          indent = base_ind;
          at_start = 1;
          stack_ptr = base_sp;

          for (int k = 0; k <= base_sp && k < 256; k++) {
            temp_anchor_stack[k] = main_anchor_stack[k];
          }

          current_line = -1;
          last_aligned_line = -1;

          // Pass B: Assign the calculated target column to the matching tokens
          for (int j = i + 1; j < group_end_idx; j++) {
            Token *t = &st->buffer.data[j];

            if (at_start) {
              current_line++;
            }

            if (current_line >= 0 && current_line <= max_line_idx && t->type == current_channel) {
              if (current_line != last_aligned_line && indent == base_ind && stack_ptr <= base_sp + 3 &&
                  (current_channel != TOKEN_ALIGN_MATH || at_start)) {
                if (current_channel != TOKEN_ALIGN_COMMENT || indent == base_ind) {
                  int anchor_col = col;

                  if (at_start) {
                    anchor_col = ((stack_ptr >= 0 && temp_anchor_stack[stack_ptr] >= 0) ? temp_anchor_stack[stack_ptr] : 0) +
                         (indent * st->indent_size);
                  }

                  int target = target_col_max[current_line];
                  if (target > 0) {
                    if (current_channel != TOKEN_ALIGN_COMMENT && t->text) {
                      target -= (int)strlen(t->text);
                    }

                    if (target > anchor_col) {
                      // Heuristic: Only jump to the target column if it is reasonably close
                      // Prevent huge gaps (e.g., 50 spaces) just to align one outlier
                      bool allow =
                          (current_channel == TOKEN_ALIGN_COMMENT)
                              ? (target - anchor_col <= 20)
                              : (target - anchor_col <= 15 || target - anchor_col <= anchor_col / 4);
                      if (allow) {
                        t->target_col = target;
                      }
                    }
                  }
                  last_aligned_line = current_line;
                }
              }
            }
            simulate_step(t, &col, &indent, &at_start, st->indent_size, temp_explode_stack, temp_anchor_stack, &stack_ptr,
                          current_channel);
          }
          free(has_baseline);
          free(has_operator);
          free(measured_width);
          free(target_col_max);
        }
      }
    }
    // Update the main state machine with the current token
    simulate_step(main_t, &main_col, &main_indent, &main_at_start, st->indent_size,
                  main_explode_stack, main_anchor_stack, &main_stack_ptr, TOKEN_TEXT);
  }
}

// Translate the resolved IR tokens into final text output
void prpfmt_render(struct PrpfmtState *st) {
  int indent = 0, col = 0, at_start = 1, stack_ptr = 0;
  bool explode_stack[256];
  int anchor_stack[256];
  explode_stack[0] = false;
  anchor_stack[0] = -1;

  for (int i = 0; i < st->buffer.size; i++) {
    Token *t = &st->buffer.data[i];
    // Retrieve context state for the current nesting level
    bool is_exploded = stack_ptr >= 0 ? explode_stack[stack_ptr] : false;
    int current_anchor = stack_ptr >= 0 ? anchor_stack[stack_ptr] : -1;

    switch (t->type) {
      case TOKEN_TEXT:
      case TOKEN_ALIGN_OPERATOR:
      case TOKEN_ALIGN_RELATIONAL:
      case TOKEN_ALIGN_MATH:
      case TOKEN_ALIGN_COMMENT:
        if (at_start) {
          // Print leading indentation and anchor offsets at the start of a line
          int baseline = (current_anchor >= 0) ? current_anchor : 0;
          int spaces_needed = baseline + (indent * st->indent_size);

          for (int j = 0; j < spaces_needed; j++) {
            fprintf(st->outfile, " ");
            col++;
          }
          at_start = 0;
        }
        if (t->target_col > col) {
          // Pad with spaces to reach the target alignment column
          int padding_spaces = t->target_col - col;
          for (int j = 0; j < padding_spaces; j++) {
            fprintf(st->outfile, " ");
            col++;
          }
        }
        if ((t->type == TOKEN_ALIGN_OPERATOR ||
             t->type == TOKEN_ALIGN_RELATIONAL) &&
            t->text) {
          // Record the column position after alignment to serve as a hanging indent anchor
          if (stack_ptr >= 0) {
            anchor_stack[stack_ptr] = col;
          }
        }
        if (t->text) {
          // Print the actual token text
          fprintf(st->outfile, "%s", t->text);
          col += (int)strlen(t->text);
        }
        break;
      case TOKEN_ANCHOR:
        // Set a manual hanging indent anchor
        if (stack_ptr >= 0) {
          anchor_stack[stack_ptr] = col;
        }
        break;
      case TOKEN_ANCHOR_OFF:
        // Clear the current manual anchor
        if (stack_ptr >= 0) {
          anchor_stack[stack_ptr] = -1;
        }
        break;
      case TOKEN_SPACE:
        // Print a space if not at the start of a line
        if (!at_start) {
          fprintf(st->outfile, " ");
          col++;
        }
        break;
      case TOKEN_NEWLINE:
      case TOKEN_FORCE_BREAK:
        // Print a mandatory newline
        fprintf(st->outfile, "\n");
        at_start = 1;
        col = 0;
        break;
      case TOKEN_BREAK_POINT:
        // Print a newline if exploded, otherwise a space
        if (is_exploded) {
          fprintf(st->outfile, "\n");
          at_start = 1;
          col = 0;
        } else if (!at_start) {
          fprintf(st->outfile, " ");
          col++;
        }
        break;
      case TOKEN_SOFT_BREAK:
        // Print a newline if exploded, otherwise nothing
        if (is_exploded) {
          fprintf(st->outfile, "\n");
          at_start = 1;
          col = 0;
        }
        break;
      case TOKEN_SOFT_SPACE:
        // Print a space if exploded, otherwise nothing
        if (is_exploded && !at_start) {
          fprintf(st->outfile, " ");
          col++;
        }
        break;
      case TOKEN_INDENT_INC:
        indent++;
        break;
      case TOKEN_INDENT_DEC:
        indent--;
        break;
      case TOKEN_GROUP_START:
        // Push group state to the context stacks
        stack_ptr++;
        if (stack_ptr < 256) {
          explode_stack[stack_ptr] = t->exploded;
          anchor_stack[stack_ptr] = (stack_ptr > 0) ? anchor_stack[stack_ptr - 1] : -1;
        }
        break;
      case TOKEN_GROUP_END:
        // Pop group state from the context stacks
        if (stack_ptr > 0) {
          stack_ptr--;
        }
        break;
      default:
        break;
    }
  }
}

// Free all allocated memory in the IR buffer
void prpfmt_free_buffer(struct PrpfmtState *st) {
  for (int i = 0; i < st->buffer.size; i++) {
    if (st->buffer.data[i].text) {
      free(st->buffer.data[i].text);
    }
  }
  free(st->buffer.data);
  st->buffer.data = NULL;
  st->buffer.size = 0;
  st->buffer.capacity = 0;
}
