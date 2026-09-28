#include <algorithm>
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
    if (type == TOKEN_TEXT && st.buffer[i].text.empty()) continue;
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

// A required newline upgrades any pending optional break. This matters for
// comments and scope boundaries, which must survive even in unlimited AI mode.
void emit_force_break(PrpfmtState &st) {
  for (auto it = st.buffer.rbegin(); it != st.buffer.rend(); ++it) {
    if (it->type == TOKEN_NEWLINE || it->type == TOKEN_FORCE_BREAK) return;
    if (it->type == TOKEN_BREAK_POINT || it->type == TOKEN_SOFT_BREAK) {
      it->type = TOKEN_FORCE_BREAK;
      return;
    }
    if (!it->text.empty()) break;
  }
  push_token(st, TOKEN_FORCE_BREAK);
}

void emit_line_break(PrpfmtState &st) {
  emit_force_break(st);
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
        col = std::max(col, t.target_col);
      }

      // Record anchor position for subsequent items
      if ((t.type == TOKEN_ALIGN_OPERATOR ||
           t.type == TOKEN_ALIGN_RELATIONAL) &&
          !t.text.empty() && ptr_ok) {
        anc_stack[stack_ptr] = col - indent * indent_size;
      }

      col += (int)t.text.size();
      break;
    case TOKEN_ANCHOR:
      // Set a manual hanging indent anchor
      if (ptr_ok) {
        anc_stack[stack_ptr] = col - indent * indent_size;
      }
      break;
    case TOKEN_ANCHOR_OFF:
      // Clear the current manual anchor
      if (ptr_ok) {
        anc_stack[stack_ptr] = -1;
      }
      break;
    case TOKEN_SPACE:
      if (!at_start) col++;
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
      } else if (!at_start) {
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
static bool is_comment_token(const Token &token) {
  return token.type == TOKEN_ALIGN_COMMENT ||
         (token.type == TOKEN_TEXT && token.text.starts_with("//"));
}

// Width is a readability target. A small overflow can cost less than splitting
// an expression; comma boundaries remain cheap places to break long lists.
static int overflow_slack(const PrpfmtState &st) {
  return std::min(12, st.max_width / 10);
}

static void calculate_group_metrics(PrpfmtState &st) {
  std::array<int, 1024> w_stack;
  int w_top = -1;
  std::array<int, 1024> anchor_stack;
  int a_top = -1;
  int cur_off = 0, cur_cost = 0, f_count = 0, comments = 0;

  for (int i = 0; i < (int)st.buffer.size(); i++) {
    Token &t = st.buffer[i];
    if (t.type == TOKEN_GROUP_START) {
      // Record starting offsets to compute the total span later
      t.pre_flat_length = cur_off;
      t.pre_explode_cost = cur_cost;
      t.pre_force_counter = f_count;
      t.pre_comment_counter = comments;

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
        st_t.pre_comment_counter = comments != st_t.pre_comment_counter;
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
          if (is_comment_token(t)) ++comments;
          else cur_off += (int)t.text.size();
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
  int col = 0, indent = 0, at_start = 1, s_ptr = 0;
  std::array<bool, 256> explode_stack{};
  std::array<bool, 256> propagate_stack{};
  std::array<int, 256> anchor_stack{};
  anchor_stack.fill(-1);

  // Phase 1: Determine which groups must explode (wrap) based on width and penalties
  // Simulate the token layout sequentially to determine the actual column positions
  for (int i = 0; i < (int)st.buffer.size(); i++) {
    Token &t = st.buffer[i];

    if (t.type == TOKEN_GROUP_START) {
      if (!t.exploded) {
        bool forced = st.mode == PRPFMT_HUMAN && t.pre_force_counter;
        bool parent_in_bounds = s_ptr >= 0 && s_ptr < (int)explode_stack.size();
        bool parent_exp = parent_in_bounds ? explode_stack[s_ptr] : false;
        bool parent_prop = parent_in_bounds ? propagate_stack[s_ptr] : false;
        bool should_exp = forced;

        if (!should_exp && st.mode == PRPFMT_HUMAN) {
          // Include indentation and any flat suffix on the same line: closing
          // delimiters and the output clause of a header. A trailing comment
          // stays attached without forcing otherwise fitting code to wrap.
          int column = at_start ? indent * st.indent_size : col;
          if (at_start && parent_in_bounds && anchor_stack[s_ptr] > 0) column += anchor_stack[s_ptr];
          int suffix = 0;
          for (int j = t.pre_group_end + 1; j > 0 && j < (int)st.buffer.size(); ++j) {
            auto &next = st.buffer[j];
            if (is_comment_token(next)) {
              if (j > 0 && st.buffer[j - 1].type == TOKEN_SPACE) --suffix;
              break;
            }
            if (next.type == TOKEN_BREAK_POINT || next.type == TOKEN_SOFT_BREAK ||
                next.type == TOKEN_NEWLINE || next.type == TOKEN_FORCE_BREAK) break;
            if (next.type == TOKEN_GROUP_START) {
              if (!next.pre_force_counter && !next.pre_comment_counter && !next.exploded) {
                suffix += next.pre_flat_length;
                j = next.pre_group_end;
              }
            } else if (next.type == TOKEN_SPACE) ++suffix;
            else suffix += (int)next.text.size();
          }
          int slack = std::min(overflow_slack(st), t.pre_explode_cost / 10);
          should_exp = static_cast<long long>(column) + t.pre_flat_length + suffix >
                       static_cast<long long>(st.max_width) + slack;
        }

        if (st.mode == PRPFMT_HUMAN && !should_exp && parent_exp && parent_prop && t.propagates) {
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

  if (st.mode == PRPFMT_AI) return;

  int main_col = 0, main_indent = 0, main_start = 1, main_sp = 0;
  std::array<bool, 256> main_exp{};
  std::array<int, 256> main_anc{};
  main_anc.fill(-1);
  for (int i = 0; i < (int)st.buffer.size(); ++i) {
    auto &token = st.buffer[i];
    if (token.type == TOKEN_ALIGN_GROUP_START && token.pre_group_end > i) {
      int c = main_col, ind = main_indent, start = main_start, sp = main_sp;
      auto exp = main_exp;
      auto anc = main_anc;
      std::vector<std::pair<int, int>> operators;
      int target = 0;
      bool seen_operator = false;
      for (int j = i + 1; j < token.pre_group_end; ++j) {
        auto &t = st.buffer[j];
        if (t.type == TOKEN_ALIGN_OPERATOR && ind == main_indent && !seen_operator) {
          int column = start ? ind * st.indent_size : c;
          operators.emplace_back(j, column);
          target = std::max(target, column);
          seen_operator = true;
        }
        simulate_step(t, c, ind, start, st.indent_size, exp, anc, sp, TOKEN_TEXT);
        if (start) seen_operator = false;
      }
      if (operators.size() > 1) {
        for (auto [index, column] : operators) st.buffer[index].target_col = target;
        // Allow a modest overflow to preserve the alignment of the whole group,
        // and do not drop it merely because an attached comment is long.
        c = main_col; ind = main_indent; start = main_start; sp = main_sp;
        exp = main_exp; anc = main_anc;
        bool overflow = false;
        bool in_comment = false;
        for (int j = i + 1; j < token.pre_group_end; ++j) {
          if (is_comment_token(st.buffer[j])) in_comment = true;
          simulate_step(st.buffer[j], c, ind, start, st.indent_size, exp, anc, sp, TOKEN_TEXT);
          if (!in_comment && static_cast<long long>(c) > static_cast<long long>(st.max_width) + overflow_slack(st))
            overflow = true;
          if (start) in_comment = false;
        }
        if (overflow) for (auto [index, column] : operators) st.buffer[index].target_col = 0;
      }
    }
    simulate_step(token, main_col, main_indent, main_start, st.indent_size, main_exp, main_anc, main_sp, TOKEN_TEXT);
  }
}

// Translate the resolved IR tokens into final text output
void prpfmt_render(PrpfmtState &st) {
  std::string output;
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
            output += ' ';
            col++;
          }
          at_start = 0;
        }
        if (t.target_col > col) {
          // Pad with spaces to reach the target alignment column
          int padding_spaces = t.target_col - col;
          for (int j = 0; j < padding_spaces; j++) {
            output += ' ';
            col++;
          }
        }
        if ((t.type == TOKEN_ALIGN_OPERATOR ||
             t.type == TOKEN_ALIGN_RELATIONAL) &&
            !t.text.empty()) {
          // Record the column position after alignment to serve as a hanging indent anchor
          if (ptr_ok) {
            anchor_stack[stack_ptr] = col - indent * st.indent_size;
          }
        }
        if (!t.text.empty()) {
          // Print the actual token text
          output += t.text;
          col += (int)t.text.size();
        }
        break;
      case TOKEN_ANCHOR:
        // Set a manual hanging indent anchor
        if (ptr_ok) {
          anchor_stack[stack_ptr] = col - indent * st.indent_size;
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
          output += ' ';
          col++;
        }
        break;
      case TOKEN_NEWLINE:
      case TOKEN_FORCE_BREAK:
        // Print a mandatory newline
        while (!output.empty() && output.back() == ' ') output.pop_back();
          output += '\n';
        at_start = 1;
        col = 0;
        break;
      case TOKEN_BREAK_POINT:
        // Print a newline if exploded, otherwise a space
        if (is_exploded) {
          while (!output.empty() && output.back() == ' ') output.pop_back();
          output += '\n';
          at_start = 1;
          col = 0;
        } else if (!at_start) {
          output += ' ';
          col++;
        }
        break;
      case TOKEN_SOFT_BREAK:
        // Print a newline if exploded, otherwise nothing
        if (is_exploded) {
          while (!output.empty() && output.back() == ' ') output.pop_back();
          output += '\n';
          at_start = 1;
          col = 0;
        }
        break;
      case TOKEN_SOFT_SPACE:
        // Print a space if exploded, otherwise nothing
        if (is_exploded && !at_start) {
          output += ' ';
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
  if (!output.empty()) fwrite(output.data(), 1, output.size(), st.outfile);
}
