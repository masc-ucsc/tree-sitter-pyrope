#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ir.h"
#include "prpfmt.h"

static void ensure_capacity(struct PrpfmtState *st) {
  if (st->buffer.size >= st->buffer.capacity) {
    st->buffer.capacity = st->buffer.capacity == 0 ? 1024 : st->buffer.capacity * 2;
    st->buffer.data = realloc(st->buffer.data, st->buffer.capacity * sizeof(Token));
  }
}

static void init_token(Token *t, TokenType type, char *text) {
  t->type = type;
  t->text = text;
  t->exploded = false;
  t->target_col = 0;
  t->penalty = 0;
}

void emit_token(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_TEXT, strdup(text));
  st->buffer.size++;
}

void emit_space(struct PrpfmtState *st) {
  // Look back to prevent duplicate spaces or spaces after newlines
  for (int i = st->buffer.size - 1; i >= 0; i--) {
    TokenType type = st->buffer.data[i].type;

    if (type == TOKEN_SPACE || type == TOKEN_BREAK_POINT) {
      return; // Already has a space or potential space
    }

    if (type == TOKEN_NEWLINE || type == TOKEN_FORCE_BREAK) {
      return; // No space needed at start of line
    }

    // Skip structural/zero-width tokens that don't affect horizontal spacing
    if (type == TOKEN_GROUP_START || type == TOKEN_GROUP_END ||
        type == TOKEN_ALIGN_GROUP_START || type == TOKEN_ALIGN_GROUP_END ||
        type == TOKEN_INDENT_INC || type == TOKEN_INDENT_DEC ||
        type == TOKEN_ANCHOR || type == TOKEN_SOFT_BREAK) {
      continue;
    }

    // If we hit TEXT or an OPERATOR, we need the space
    break;
  }

  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_SPACE, NULL);
  st->buffer.size++;
}

void emit_newline(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_NEWLINE, NULL);
  st->buffer.size++;
}

void emit_break_point(struct PrpfmtState *st, int penalty) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_BREAK_POINT, NULL);
  st->buffer.data[st->buffer.size].penalty = penalty;
  st->buffer.size++;
}

void emit_soft_break(struct PrpfmtState *st, int penalty) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_SOFT_BREAK, NULL);
  st->buffer.data[st->buffer.size].penalty = penalty;
  st->buffer.size++;
}

void emit_indent_inc(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_INDENT_INC, NULL);
  st->buffer.size++;
}

void emit_indent_dec(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_INDENT_DEC, NULL);
  st->buffer.size++;
}

void emit_group_start(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_GROUP_START, NULL);
  st->buffer.size++;
}

void emit_group_end(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_GROUP_END, NULL);
  st->buffer.size++;
}

void emit_align_group_start(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_GROUP_START, NULL);
  st->buffer.size++;
}

void emit_align_group_end(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_GROUP_END, NULL);
  st->buffer.size++;
}

void emit_align_operator(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_OPERATOR, strdup(text));
  st->buffer.size++;
}

void emit_align_relational(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_RELATIONAL, strdup(text));
  st->buffer.size++;
}

void emit_align_comment(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_COMMENT, strdup(text));
  st->buffer.size++;
}

void emit_anchor(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ANCHOR, NULL);
  st->buffer.size++;
}

void emit_force_break(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_FORCE_BREAK, NULL);
  st->buffer.size++;
}

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

// Internal helper for column simulation during solver passes
static void simulate_step(Token *t, int *col, int *indent, int *at_start, int indent_size, bool *exp_stack, int *anc_stack, int *stack_ptr, TokenType channel_filter) {
  bool is_exploded = *stack_ptr >= 0 ? exp_stack[*stack_ptr] : false;
  int current_anchor = *stack_ptr >= 0 ? anc_stack[*stack_ptr] : -1;

  if (*at_start && (t->type == TOKEN_TEXT || t->type == TOKEN_ALIGN_OPERATOR || t->type == TOKEN_ALIGN_RELATIONAL || t->type == TOKEN_ALIGN_COMMENT)) {
    int baseline = (current_anchor >= 0) ? current_anchor : 0;
    *col += baseline + (*indent * indent_size);
    *at_start = 0;
  }

  switch (t->type) {
    case TOKEN_TEXT:
      if (t->text) {
        *col += strlen(t->text);
      }
      break;
    case TOKEN_ALIGN_OPERATOR:
    case TOKEN_ALIGN_RELATIONAL:
    case TOKEN_ALIGN_COMMENT:
      // Only apply padding if we are simulating this specific channel,
      // or if we are doing a full simulation (filter == TOKEN_TEXT)
      if (t->target_col > 0 && (channel_filter == TOKEN_TEXT || t->type == channel_filter)) {
        *col = t->target_col;
      }
      if ((t->type == TOKEN_ALIGN_OPERATOR || t->type == TOKEN_ALIGN_RELATIONAL) && *stack_ptr >= 0) {
        anc_stack[*stack_ptr] = *col;
      }
      if (t->text) {
        *col += strlen(t->text);
      }
      break;
    case TOKEN_ANCHOR:
      if (*stack_ptr >= 0) {
        anc_stack[*stack_ptr] = *col;
      }
      break;
    case TOKEN_SPACE:
      (*col)++;
      break;
    case TOKEN_NEWLINE:
    case TOKEN_FORCE_BREAK:
      *col = 0;
      *at_start = 1;
      if (*stack_ptr >= 0) {
        anc_stack[*stack_ptr] = -1;
      }
      break;
    case TOKEN_BREAK_POINT:
      if (is_exploded) {
        *col = 0;
        *at_start = 1;
      } else {
        (*col)++;
      }
      break;
    case TOKEN_SOFT_BREAK:
      if (is_exploded) {
        *col = 0;
        *at_start = 1;
      }
      break;
    case TOKEN_INDENT_INC:
      (*indent)++;
      break;
    case TOKEN_INDENT_DEC:
      (*indent)--;
      break;
    case TOKEN_GROUP_START:
      (*stack_ptr)++;
      if (*stack_ptr < 256) {
        exp_stack[*stack_ptr] = t->exploded;
        anc_stack[*stack_ptr] = (*stack_ptr > 0) ? anc_stack[*stack_ptr - 1] : -1;
      }
      break;
    case TOKEN_GROUP_END:
      if (*stack_ptr > 0) {
        (*stack_ptr)--;
      }
      break;
    default:
      break;
  }
}

void prpfmt_solve(struct PrpfmtState *st) {
  // Pass 1: Decide explosion status for groups
  for (int i = 0; i < st->buffer.size; i++) {
    if (st->buffer.data[i].type == TOKEN_GROUP_START) {
      int flat_length = 0;
      int explode_cost = 0;
      bool force_explode = false;
      int depth = 1;
      for (int j = i + 1; j < st->buffer.size; j++) {
        Token *t = &st->buffer.data[j];
        if (t->type == TOKEN_GROUP_START) {
          depth++;
        }
        if (t->type == TOKEN_GROUP_END) {
          depth--;
        }
        if (depth == 0) {
          break;
        }
        switch (t->type) {
          case TOKEN_TEXT:
          case TOKEN_ALIGN_OPERATOR:
          case TOKEN_ALIGN_RELATIONAL:
            if (t->text) {
              flat_length += strlen(t->text);
            }
            break;
          case TOKEN_ALIGN_COMMENT: {
            bool has_more = false;
            int inner_depth = 0;
            for (int k = j + 1; k < st->buffer.size; k++) {
              TokenType nt = st->buffer.data[k].type;
              if (nt == TOKEN_GROUP_START) {
                inner_depth++;
              }
              if (nt == TOKEN_GROUP_END) {
                if (inner_depth == 0) {
                  break;
                }
                inner_depth--;
              }
              if (nt == TOKEN_TEXT || nt == TOKEN_ALIGN_OPERATOR || nt == TOKEN_ALIGN_COMMENT) {
                has_more = true;
                break;
              }
            }
            if (has_more) {
              force_explode = true;
            }
            break;
          }
          case TOKEN_SPACE:
            flat_length += 1;
            break;
          case TOKEN_BREAK_POINT:
            flat_length += 1;
            explode_cost += t->penalty;
            break;
          case TOKEN_SOFT_BREAK:
            explode_cost += t->penalty;
            break;
          case TOKEN_FORCE_BREAK:
          case TOKEN_NEWLINE:
            force_explode = true;
            break;
          default:
            break;
        }
        if (force_explode) {
          break;
        }
      }

      if (force_explode) {
        st->buffer.data[i].exploded = true;
      } else {
        int overflow = flat_length - st->max_width;
        long flat_cost = (overflow > 0) ? (overflow * 1000) : 0;
        st->buffer.data[i].exploded = (flat_cost > explode_cost);
      }
    }
  }

  // Pass 2: Vertical Alignment (Sequential Channels)
  for (int i = 0; i < st->buffer.size; i++) {
    if (st->buffer.data[i].type == TOKEN_ALIGN_GROUP_START) {
      int group_end = -1;
      int depth = 1;
      for (int j = i + 1; j < st->buffer.size; j++) {
        if (st->buffer.data[j].type == TOKEN_ALIGN_GROUP_START) {
          depth++;
        }
        if (st->buffer.data[j].type == TOKEN_ALIGN_GROUP_END) {
          depth--;
        }
        if (depth == 0) {
          group_end = j;
          break;
        }
      }
      if (group_end == -1) {
        continue;
      }

      // Sequential Channels: 1. Assignment, 2. Relational, then 3. Comments
      TokenType channels[] = {TOKEN_ALIGN_OPERATOR, TOKEN_ALIGN_RELATIONAL, TOKEN_ALIGN_COMMENT};

      for (int c = 0; c < 3; c++) {
        TokenType current_channel = channels[c];
        int max_col = 0;
        int channel_count = 0;

        // Sub-pass 1: Measure current channel (clean simulation)
        int col = 0, indent = 0, at_start = 1, s_ptr = 0;
        int current_line = 0;
        int last_aligned_line = -1;
        bool e_stack[256];
        e_stack[0] = false;
        int a_stack[256];
        a_stack[0] = -1;

        for (int j = i + 1; j < group_end; j++) {
          Token *t = &st->buffer.data[j];

          if (t->type == TOKEN_NEWLINE || t->type == TOKEN_FORCE_BREAK || (t->type == TOKEN_BREAK_POINT && (s_ptr >= 0 && e_stack[s_ptr]))) {
            current_line++;
          }

          if (t->type == current_channel) {
            // Logic:
            // 1. Only align the FIRST one on a line (last_aligned_line check)
            // 2. For comments, only align if we are at the top-level indent (indent == 0)
            if (current_line != last_aligned_line) {
              if (current_channel != TOKEN_ALIGN_COMMENT || indent == 0) {
                int actual_col = col;
                if (at_start) {
                  int baseline = (s_ptr >= 0 && a_stack[s_ptr] >= 0) ? a_stack[s_ptr] : 0;
                  actual_col = baseline + (indent * st->indent_size);
                }
                if (actual_col > max_col) {
                  max_col = actual_col;
                }
                last_aligned_line = current_line;
                channel_count++;
              }
            }
          }
          simulate_step(t, &col, &indent, &at_start, st->indent_size, e_stack, a_stack, &s_ptr, TOKEN_TEXT);
        }

        // Sub-pass 2: Set target columns for current channel
        // Only align if there are at least two instances to align with each other
        if (channel_count > 1) {
          col = 0;
          indent = 0;
          at_start = 1;
          s_ptr = 0;
          a_stack[0] = -1;
          current_line = 0;
          last_aligned_line = -1;
          for (int j = i + 1; j < group_end; j++) {
            Token *t = &st->buffer.data[j];

            if (t->type == TOKEN_NEWLINE || t->type == TOKEN_FORCE_BREAK || (t->type == TOKEN_BREAK_POINT && (s_ptr >= 0 && e_stack[s_ptr]))) {
              current_line++;
            }

            if (t->type == current_channel) {
              if (current_line != last_aligned_line) {
                if (current_channel != TOKEN_ALIGN_COMMENT || indent == 0) {
                  int actual_col = col;
                  if (at_start) {
                    int baseline = (s_ptr >= 0 && a_stack[s_ptr] >= 0) ? a_stack[s_ptr] : 0;
                    actual_col = baseline + (indent * st->indent_size);
                  }
                  if (max_col > actual_col && max_col - actual_col < 25) {
                    t->target_col = max_col;
                  }
                  last_aligned_line = current_line;
                }
              }
            }
            simulate_step(t, &col, &indent, &at_start, st->indent_size, e_stack, a_stack, &s_ptr, current_channel);
          }
        }
      }
      i = group_end;
    }
  }
}

void prpfmt_render(struct PrpfmtState *st) {
  int current_indent = 0;
  int current_col = 0;
  int at_start_of_line = 1;

  // Small stack to track exploded status of nested groups
  bool exploded_stack[256];
  // Small stack to track anchor columns for relative indentation
  int anchor_stack[256];

  int stack_ptr = 0;
  exploded_stack[0] = false;
  anchor_stack[0] = -1; // -1 means no anchor, use normal indent

  for (int i = 0; i < st->buffer.size; i++) {
    Token *t = &st->buffer.data[i];
    bool current_exploded = stack_ptr >= 0 ? exploded_stack[stack_ptr] : false;
    int current_anchor = stack_ptr >= 0 ? anchor_stack[stack_ptr] : -1;

    switch (t->type) {
      case TOKEN_TEXT:
      case TOKEN_ALIGN_OPERATOR:
      case TOKEN_ALIGN_RELATIONAL:
      case TOKEN_ALIGN_COMMENT:
        if (at_start_of_line) {
          int baseline = (current_anchor >= 0) ? current_anchor : 0;
          int indent_spaces = baseline + (current_indent * st->indent_size);
          for (int j = 0; j < indent_spaces; j++) {
            fprintf(st->outfile, " ");
            current_col++;
          }
          at_start_of_line = 0;
        }

        if (t->target_col > current_col) {
          int padding = t->target_col - current_col;
          for (int j = 0; j < padding; j++) {
            fprintf(st->outfile, " ");
            current_col++;
          }
        }

        // If this is an alignment operator, it updates the anchor for its group
        if (t->type == TOKEN_ALIGN_OPERATOR || t->type == TOKEN_ALIGN_RELATIONAL) {
          if (stack_ptr >= 0) {
            anchor_stack[stack_ptr] = current_col;
          }
        }

        if (t->text) {
          fprintf(st->outfile, "%s", t->text);
          current_col += strlen(t->text);
        }
        break;
      case TOKEN_ANCHOR:
        if (stack_ptr >= 0) {
          anchor_stack[stack_ptr] = current_col;
        }
        break;
      case TOKEN_SPACE:
        if (!at_start_of_line) {
          fprintf(st->outfile, " ");
          current_col++;
        }
        break;
      case TOKEN_NEWLINE:
      case TOKEN_FORCE_BREAK:
        fprintf(st->outfile, "\n");
        at_start_of_line = 1;
        current_col = 0;
        if (stack_ptr >= 0) {
          anchor_stack[stack_ptr] = -1;
        }
        break;
      case TOKEN_BREAK_POINT:
        if (current_exploded) {
          fprintf(st->outfile, "\n");
          at_start_of_line = 1;
          current_col = 0;
        } else if (!at_start_of_line) {
          fprintf(st->outfile, " ");
          current_col++;
        }
        break;
      case TOKEN_SOFT_BREAK:
        if (current_exploded) {
          fprintf(st->outfile, "\n");
          at_start_of_line = 1;
          current_col = 0;
        }
        break;
      case TOKEN_INDENT_INC:
        current_indent++;
        break;
      case TOKEN_INDENT_DEC:
        current_indent--;
        break;
      case TOKEN_GROUP_START:
        stack_ptr++;
        if (stack_ptr < 256) {
          exploded_stack[stack_ptr] = t->exploded;
          // Inherit anchor from parent by default
          anchor_stack[stack_ptr] = (stack_ptr > 0) ? anchor_stack[stack_ptr - 1] : -1;
        }
        break;
      case TOKEN_GROUP_END:
        if (stack_ptr > 0) {
          stack_ptr--;
        }
        break;
      default:
        break;
    }
  }
}
