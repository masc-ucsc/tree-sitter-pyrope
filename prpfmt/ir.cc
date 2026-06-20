#include <array>
#include <cctype>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ir.h"
#include "prpfmt.h"

/******************************************************************************
 * Internal Bookkeeping                                                       *
 ******************************************************************************/

// Append a freshly-default-constructed token of the given type and hand back a
// reference so callers can fill in the fields that matter. std::vector owns the
// storage and grows itself -- no manual capacity tracking or realloc.
static Token &push_token(PrpfmtState &st, TokenType type) {
  Token &t = st.buffer.emplace_back();  // default-constructs (init_token defaults)
  t.type = type;
  return t;
}

// Same, but for tokens that carry literal text.
static Token &push_text(PrpfmtState &st, TokenType type, std::string_view text) {
  Token &t = push_token(st, type);
  t.text.assign(text.data(), text.size());
  return t;
}

// Check if last meaningful IR token was a break (prevent redundant breaks or spaces)
static bool has_recent_break(PrpfmtState &st) {
  for (int i = (int)st.buffer.size() - 1; i >= 0; i--) {
    TokenType type = st.buffer[i].type;
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
void emit_token(PrpfmtState &st, std::string_view text) {
  push_text(st, TOKEN_TEXT, text);
}

// Append a space token to the IR buffer if not redundant
void emit_space(PrpfmtState &st) {
  for (int i = (int)st.buffer.size() - 1; i >= 0; i--) {
    const Token &tok = st.buffer[i];
    TokenType type = tok.type;

    // Space/newline already emitted
    if (type == TOKEN_SPACE || type == TOKEN_BREAK_POINT ||
        type == TOKEN_NEWLINE || type == TOKEN_FORCE_BREAK) {
      return;
    }

    // Previous text token ends with a space
    if (!tok.text.empty() && isspace((unsigned char)tok.text.back())) {
      return;
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

  push_token(st, TOKEN_SPACE);
}

// Append a mandatory blank line token to the IR buffer
void emit_blank_line(PrpfmtState &st) {
  push_token(st, TOKEN_NEWLINE);
}

// Append a conditional break token: space when flat, newline when exploded
void emit_break_point(PrpfmtState &st, int penalty) {
  if (has_recent_break(st)) {
    return;
  }
  push_token(st, TOKEN_BREAK_POINT).penalty = penalty;
}

// Append a conditional break token: empty when flat, newline when exploded
void emit_soft_break(PrpfmtState &st, int penalty) {
  if (has_recent_break(st)) {
    return;
  }
  push_token(st, TOKEN_SOFT_BREAK).penalty = penalty;
}

// Append a conditional space token: empty when flat, space when exploded
void emit_soft_space(PrpfmtState &st) {
  push_token(st, TOKEN_SOFT_SPACE);
}

// Append a token to increment indentation level
void emit_indent_inc(PrpfmtState &st) {
  push_token(st, TOKEN_INDENT_INC);
}

// Append a token to decrement indentation level
void emit_indent_dec(PrpfmtState &st) {
  push_token(st, TOKEN_INDENT_DEC);
}

// Start a smart-wrapping group with optional forced explosion and propagation
void emit_group_start(PrpfmtState &st, bool force_explode, bool propagates) {
  Token &t = push_token(st, TOKEN_GROUP_START);
  t.exploded = force_explode;
  t.propagates = propagates;
}

// End the current smart-wrapping group
void emit_group_end(PrpfmtState &st) {
  push_token(st, TOKEN_GROUP_END);
}

// Start an alignment block for columnar layout
void emit_align_group_start(PrpfmtState &st) {
  push_token(st, TOKEN_ALIGN_GROUP_START);
}

// End the current alignment block
void emit_align_group_end(PrpfmtState &st) {
  push_token(st, TOKEN_ALIGN_GROUP_END);
}

// Append an operator for vertical alignment (e.g., "=")
void emit_align_operator(PrpfmtState &st, std::string_view text) {
  push_text(st, TOKEN_ALIGN_OPERATOR, text);
}

// Append a relational operator for vertical alignment (e.g., "==")
void emit_align_relational(PrpfmtState &st, std::string_view text) {
  push_text(st, TOKEN_ALIGN_RELATIONAL, text);
}

// Append a math operator for alignment (only when wrapped)
void emit_align_math(PrpfmtState &st, std::string_view text) {
  push_text(st, TOKEN_ALIGN_MATH, text);
}

// Append a comment for vertical alignment
void emit_align_comment(PrpfmtState &st, std::string_view text) {
  push_text(st, TOKEN_ALIGN_COMMENT, text);
}

// Set a vertical anchor for hanging indentation
void emit_anchor(PrpfmtState &st) {
  push_token(st, TOKEN_ANCHOR);
}

// Disable current vertical anchor
void emit_anchor_off(PrpfmtState &st) {
  push_token(st, TOKEN_ANCHOR_OFF);
}

// Append a newline if not redundant
void emit_line_break(PrpfmtState &st) {
  if (!has_recent_break(st))
    emit_blank_line(st);
}

// Force a newline regardless of group state
void emit_force_break(PrpfmtState &st) {
  if (!has_recent_break(st)) {
    push_token(st, TOKEN_FORCE_BREAK);
  }
}

/******************************************************************************
 * 2. Layout Rendering                                                        *
 ******************************************************************************/

// Simulate the rendering of a single token to update column and indentation
// state. The cursor fields are mutable references and the explosion/anchor
// stacks are bounded views (std::span), so the solver can no longer walk off
// the end of a raw stack array unnoticed.
static void simulate_step(const Token &t, int &col, int &indent, int &at_start,
                          int indent_size, std::span<bool> exp_stack,
                          std::span<int> anc_stack, int &stack_ptr,
                          TokenType channel_filter) {
  // stack_ptr tracks the true nesting depth, which on pathologically nested
  // input can exceed the fixed stack capacity. The TOKEN_GROUP_START push
  // already drops out-of-range writes, so clamp every access here as well:
  // beyond capacity we fall back to "flat, no anchor" instead of indexing OOB.
  const bool ptr_ok = stack_ptr >= 0 && stack_ptr < (int)exp_stack.size();
  bool is_exploded = ptr_ok ? exp_stack[stack_ptr] : false;
  int current_anchor = ptr_ok ? anc_stack[stack_ptr] : -1;

  // Handle auto-indentation and anchors at the start of a new line
  if (at_start &&
      (t.type == TOKEN_TEXT || t.type == TOKEN_ALIGN_OPERATOR ||
       t.type == TOKEN_ALIGN_RELATIONAL || t.type == TOKEN_ALIGN_MATH ||
       t.type == TOKEN_ALIGN_COMMENT)) {
    // Determine the horizontal starting point (anchor or left margin)
    int baseline = (current_anchor >= 0) ? current_anchor : 0;
    // Apply indentation and anchor offsets to the column position
    col += baseline + (indent * indent_size);
    // Mark the line as started to prevent redundant indentation
    at_start = 0;
  }

  switch (t.type) {
    case TOKEN_TEXT:
      // Update column based on literal text length
      col += (int)t.text.size();
      break;
    case TOKEN_ALIGN_OPERATOR:
    case TOKEN_ALIGN_RELATIONAL:
    case TOKEN_ALIGN_MATH:
    case TOKEN_ALIGN_COMMENT:
      // Jump to target alignment column if filtering for this channel
      if (t.target_col > 0 &&
          (channel_filter == TOKEN_TEXT || t.type == channel_filter)) {
        col = t.target_col;
      }

      // Record anchor position for subsequent items
      if ((t.type == TOKEN_ALIGN_OPERATOR ||
           t.type == TOKEN_ALIGN_RELATIONAL) &&
          !t.text.empty() && ptr_ok) {
        anc_stack[stack_ptr] = col;
      }

      col += (int)t.text.size();
      break;
    case TOKEN_ANCHOR:
      // Set a manual hanging indent anchor
      if (ptr_ok) {
        anc_stack[stack_ptr] = col;
      }
      break;
    case TOKEN_ANCHOR_OFF:
      // Clear the current manual anchor
      if (ptr_ok) {
        anc_stack[stack_ptr] = -1;
      }
      break;
    case TOKEN_SPACE:
      col++;
      break;
    case TOKEN_NEWLINE:
    case TOKEN_FORCE_BREAK:
      // Reset column state on mandatory breaks
      col = 0;
      at_start = 1;
      break;
    case TOKEN_BREAK_POINT:
      // Break or space depending on group state
      if (is_exploded) {
        col = 0;
        at_start = 1;
      } else {
        col++;
      }
      break;
    case TOKEN_SOFT_BREAK:
      // Break or ignore depending on group state
      if (is_exploded) {
        col = 0;
        at_start = 1;
      }
      break;
    case TOKEN_SOFT_SPACE:
      // Space or ignore depending on group state
      if (is_exploded) {
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
      if (stack_ptr < (int)exp_stack.size()) {
        // Record token explosion status
        exp_stack[stack_ptr] = t.exploded;
        // Inherit parent group's anchor (if nested)
        anc_stack[stack_ptr] = (stack_ptr > 0) ? anc_stack[stack_ptr - 1] : -1;
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

// Calculate flat lengths and explosion penalties for all formatting groups in a single pass
static void calculate_group_metrics(PrpfmtState &st) {
  std::array<int, 1024> w_stack;
  int w_top = -1;
  std::array<int, 1024> anchor_stack;
  int a_top = -1;
  int cur_off = 0, cur_cost = 0, f_count = 0;

  for (int i = 0; i < (int)st.buffer.size(); i++) {
    Token &t = st.buffer[i];
    if (t.type == TOKEN_GROUP_START) {
      // Record starting offsets to compute the total span later
      t.pre_flat_length = cur_off;
      t.pre_explode_cost = cur_cost;
      t.pre_force_counter = f_count;

      if (w_top < 1023) {
        w_stack[++w_top] = i;
      }
    } else if (t.type == TOKEN_GROUP_END) {
      if (w_top >= 0) {
        // Calculate total length and penalty by comparing current offsets to starting offsets
        int idx = w_stack[w_top--];
        Token &st_t = st.buffer[idx];
        int st_f = st_t.pre_force_counter;

        st_t.pre_flat_length = cur_off - st_t.pre_flat_length;
        st_t.pre_explode_cost = cur_cost - st_t.pre_explode_cost;
        st_t.pre_force_counter = (f_count != st_f);
        st_t.pre_group_end = i;
      }
    } else if (t.type == TOKEN_ALIGN_GROUP_START) {
      if (a_top < 1023) {
        anchor_stack[++a_top] = i;
      }
    } else if (t.type == TOKEN_ALIGN_GROUP_END) {
      if (a_top >= 0) {
        st.buffer[anchor_stack[a_top--]].pre_group_end = i;
      }
    } else {
      switch (t.type) {
        case TOKEN_TEXT:
        case TOKEN_ALIGN_OPERATOR:
        case TOKEN_ALIGN_RELATIONAL:
        case TOKEN_ALIGN_MATH:
        case TOKEN_ALIGN_COMMENT:
          // Accumulate width for text and operators
          cur_off += (int)t.text.size();
          break;
        case TOKEN_SPACE:
          cur_off++;
          break;
        case TOKEN_BREAK_POINT:
          cur_off++;
          cur_cost += t.penalty;
          break;
        case TOKEN_SOFT_BREAK:
          cur_cost += t.penalty;
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
void prpfmt_solve(PrpfmtState &st) {

  // Pre-pass: Measure flat widths and explosion penalties before simulating layout
  calculate_group_metrics(st);
  int col = 0, indent = 0, at_start = 1, s_ptr = -1;
  std::array<bool, 256> explode_stack{};
  std::array<bool, 256> propagate_stack{};
  std::array<int, 256> anchor_stack{};

  // Phase 1: Determine which groups must explode (wrap) based on width and penalties
  // Simulate the token layout sequentially to determine the actual column positions
  for (int i = 0; i < (int)st.buffer.size(); i++) {
    Token &t = st.buffer[i];

    if (t.type == TOKEN_GROUP_START) {
      if (!t.exploded) {
        bool forced = (bool)t.pre_force_counter;
        bool parent_in_bounds = s_ptr >= 0 && s_ptr < (int)explode_stack.size();
        bool parent_exp = parent_in_bounds ? explode_stack[s_ptr] : false;
        bool parent_prop = parent_in_bounds ? propagate_stack[s_ptr] : false;
        bool should_exp = forced;

        if (!should_exp) {
          // Explode if the flat width exceeds max_width and the penalty is acceptable
          int overflow = (col + t.pre_flat_length) - st.max_width;
          should_exp =
              ((overflow > 0 ? overflow * 1000 : 0) > t.pre_explode_cost);
        }

        if (!should_exp && parent_exp && parent_prop && t.propagates) {
          // Propagate explosion downwards if required by the parent group
          should_exp = true;
        }
        t.exploded = should_exp;
      }
    }

    int tmp_s_ptr = s_ptr;
    simulate_step(t, col, indent, at_start, st.indent_size, explode_stack,
                  anchor_stack, tmp_s_ptr, TOKEN_TEXT);

    if (t.type == TOKEN_GROUP_START) {
      s_ptr++;

      if (s_ptr >= 0 && s_ptr < 256) {
        propagate_stack[s_ptr] = t.propagates;
      }
    } else if (t.type == TOKEN_GROUP_END) {
      if (s_ptr >= 0) {
        s_ptr--;
      }
    }
  }

  // Initialize the main simulation state machine for Phase 2
  int main_col = 0, main_indent = 0, main_at_start = 1, main_stack_ptr = 0;
  std::array<bool, 256> main_explode_stack{};
  std::array<int, 256> main_anchor_stack;
  main_anchor_stack.fill(-1);

  // Phase 2: Calculate target columns for aligned operators and comments
  for (int i = 0; i < (int)st.buffer.size(); i++) {
    Token &main_t = st.buffer[i];

    if (main_t.type == TOKEN_ALIGN_GROUP_START) {
      int base_ind = main_indent, base_sp = main_stack_ptr, group_end_idx = main_t.pre_group_end;

      if (group_end_idx > i) {
        int l_count = 0;

        // Run a temporary sub-simulation to count the total number of lines in the block
        {
          int temp_col = 0, temp_indent = base_ind, temp_at_start = 1, temp_stack_ptr = base_sp;
          std::array<bool, 256> temp_explode_stack{};

          for (int k = 0; k <= base_sp && k < 256; k++) {
            temp_explode_stack[k] = main_explode_stack[k];
          }

          std::array<int, 256> temp_anchor_stack{};
          for (int k = 0; k <= base_sp && k < 256; k++) {
            temp_anchor_stack[k] = main_anchor_stack[k];
          }

          for (int j = i + 1; j < group_end_idx; j++) {
            if (temp_at_start) {
              l_count++;
            }
            simulate_step(st.buffer[j], temp_col, temp_indent, temp_at_start, st.indent_size,
                          temp_explode_stack, temp_anchor_stack, temp_stack_ptr, TOKEN_TEXT);
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
          std::vector<int> has_baseline(l_count, 0);
          std::vector<int> has_operator(l_count, 0);
          std::vector<int> measured_width(l_count, 0);
          int col = 0, indent = base_ind, at_start = 1, stack_ptr = base_sp, current_line = -1,
              last_aligned_line = -1;
          std::array<bool, 256> temp_explode_stack{};

          for (int k = 0; k <= base_sp && k < 256; k++) {
            temp_explode_stack[k] = main_explode_stack[k];
          }

          std::array<int, 256> temp_anchor_stack{};
          for (int k = 0; k <= base_sp && k < 256; k++) {
            temp_anchor_stack[k] = main_anchor_stack[k];
          }

          // Pass A: Scan the block to find the maximum column width for this channel
          for (int j = i + 1; j < group_end_idx; j++) {
            Token &t = st.buffer[j];

            if (at_start) {
              current_line++;
            }

            if (current_line >= 0 && current_line < l_count) {
              // Only consider tokens valid for alignment if they are near the base nesting level (depth + 3 limit)
              if (indent == base_ind && stack_ptr <= base_sp + 3 &&
                  (t.type == TOKEN_TEXT || t.type == TOKEN_ALIGN_OPERATOR ||
                   t.type == TOKEN_ALIGN_RELATIONAL ||
                   t.type == TOKEN_ALIGN_MATH ||
                   t.type == TOKEN_ALIGN_COMMENT)) {
                has_baseline[current_line] = 1;
              }

              if (t.type == current_channel) {
                // Determine if this token should be aligned based on channel and indentation rules
                if (current_line != last_aligned_line && indent == base_ind && stack_ptr <= base_sp + 3 &&
                    (current_channel != TOKEN_ALIGN_MATH || at_start)) {
                  if (current_channel != TOKEN_ALIGN_COMMENT || indent == base_ind) {
                    int anchor_col = col;

                    if (at_start) {
                      // Adjust column based on anchor position if available
                      anchor_col = ((stack_ptr >= 0 && temp_anchor_stack[stack_ptr] >= 0) ? temp_anchor_stack[stack_ptr] : 0) +
                           (indent * st.indent_size);
                    }

                    int measured_val = anchor_col;
                    if (current_channel != TOKEN_ALIGN_COMMENT && !t.text.empty()) {
                      measured_val += (int)t.text.size();
                    }

                    has_operator[current_line] = 1;
                    measured_width[current_line] = measured_val;
                    last_aligned_line = current_line;
                  }
                }
              }
            }

            simulate_step(t, col, indent, at_start, st.indent_size, temp_explode_stack, temp_anchor_stack, stack_ptr,
                          TOKEN_TEXT);
          }

          int max_line_idx = current_line;
          std::vector<int> target_col_max(max_line_idx + 1, 0);

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
            Token &t = st.buffer[j];

            if (at_start) {
              current_line++;
            }

            if (current_line >= 0 && current_line <= max_line_idx && t.type == current_channel) {
              if (current_line != last_aligned_line && indent == base_ind && stack_ptr <= base_sp + 3 &&
                  (current_channel != TOKEN_ALIGN_MATH || at_start)) {
                if (current_channel != TOKEN_ALIGN_COMMENT || indent == base_ind) {
                  int anchor_col = col;

                  if (at_start) {
                    anchor_col = ((stack_ptr >= 0 && temp_anchor_stack[stack_ptr] >= 0) ? temp_anchor_stack[stack_ptr] : 0) +
                         (indent * st.indent_size);
                  }

                  int target = target_col_max[current_line];
                  if (target > 0) {
                    if (current_channel != TOKEN_ALIGN_COMMENT && !t.text.empty()) {
                      target -= (int)t.text.size();
                    }

                    if (target > anchor_col) {
                      // Heuristic: Only jump to the target column if it is reasonably close
                      // Prevent huge gaps (e.g., 50 spaces) just to align one outlier
                      bool allow =
                          (current_channel == TOKEN_ALIGN_COMMENT)
                              ? (target - anchor_col <= 20)
                              : (target - anchor_col <= 15 || target - anchor_col <= anchor_col / 4);
                      if (allow) {
                        t.target_col = target;
                      }
                    }
                  }
                  last_aligned_line = current_line;
                }
              }
            }
            simulate_step(t, col, indent, at_start, st.indent_size, temp_explode_stack, temp_anchor_stack, stack_ptr,
                          current_channel);
          }
        }
      }
    }
    // Update the main state machine with the current token
    simulate_step(main_t, main_col, main_indent, main_at_start, st.indent_size,
                  main_explode_stack, main_anchor_stack, main_stack_ptr, TOKEN_TEXT);
  }
}

// Translate the resolved IR tokens into final text output
void prpfmt_render(PrpfmtState &st) {
  int indent = 0, col = 0, at_start = 1, stack_ptr = 0;
  std::array<bool, 256> explode_stack{};
  std::array<int, 256> anchor_stack{};
  explode_stack[0] = false;
  anchor_stack[0] = -1;

  for (int i = 0; i < (int)st.buffer.size(); i++) {
    const Token &t = st.buffer[i];
    // Retrieve context state for the current nesting level. stack_ptr may run
    // past the fixed stack on pathologically nested input; clamp like the
    // GROUP_START push does (flat / no anchor beyond capacity) rather than
    // indexing out of bounds.
    const bool ptr_ok = stack_ptr >= 0 && stack_ptr < (int)explode_stack.size();
    bool is_exploded = ptr_ok ? explode_stack[stack_ptr] : false;
    int current_anchor = ptr_ok ? anchor_stack[stack_ptr] : -1;

    switch (t.type) {
      case TOKEN_TEXT:
      case TOKEN_ALIGN_OPERATOR:
      case TOKEN_ALIGN_RELATIONAL:
      case TOKEN_ALIGN_MATH:
      case TOKEN_ALIGN_COMMENT:
        if (at_start) {
          // Print leading indentation and anchor offsets at the start of a line
          int baseline = (current_anchor >= 0) ? current_anchor : 0;
          int spaces_needed = baseline + (indent * st.indent_size);

          for (int j = 0; j < spaces_needed; j++) {
            fprintf(st.outfile, " ");
            col++;
          }
          at_start = 0;
        }
        if (t.target_col > col) {
          // Pad with spaces to reach the target alignment column
          int padding_spaces = t.target_col - col;
          for (int j = 0; j < padding_spaces; j++) {
            fprintf(st.outfile, " ");
            col++;
          }
        }
        if ((t.type == TOKEN_ALIGN_OPERATOR ||
             t.type == TOKEN_ALIGN_RELATIONAL) &&
            !t.text.empty()) {
          // Record the column position after alignment to serve as a hanging indent anchor
          if (ptr_ok) {
            anchor_stack[stack_ptr] = col;
          }
        }
        if (!t.text.empty()) {
          // Print the actual token text
          fprintf(st.outfile, "%s", t.text.c_str());
          col += (int)t.text.size();
        }
        break;
      case TOKEN_ANCHOR:
        // Set a manual hanging indent anchor
        if (ptr_ok) {
          anchor_stack[stack_ptr] = col;
        }
        break;
      case TOKEN_ANCHOR_OFF:
        // Clear the current manual anchor
        if (ptr_ok) {
          anchor_stack[stack_ptr] = -1;
        }
        break;
      case TOKEN_SPACE:
        // Print a space if not at the start of a line
        if (!at_start) {
          fprintf(st.outfile, " ");
          col++;
        }
        break;
      case TOKEN_NEWLINE:
      case TOKEN_FORCE_BREAK:
        // Print a mandatory newline
        fprintf(st.outfile, "\n");
        at_start = 1;
        col = 0;
        break;
      case TOKEN_BREAK_POINT:
        // Print a newline if exploded, otherwise a space
        if (is_exploded) {
          fprintf(st.outfile, "\n");
          at_start = 1;
          col = 0;
        } else if (!at_start) {
          fprintf(st.outfile, " ");
          col++;
        }
        break;
      case TOKEN_SOFT_BREAK:
        // Print a newline if exploded, otherwise nothing
        if (is_exploded) {
          fprintf(st.outfile, "\n");
          at_start = 1;
          col = 0;
        }
        break;
      case TOKEN_SOFT_SPACE:
        // Print a space if exploded, otherwise nothing
        if (is_exploded && !at_start) {
          fprintf(st.outfile, " ");
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
          explode_stack[stack_ptr] = t.exploded;
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
