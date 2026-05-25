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
  t->propagates = true;
  t->target_col = 0;
  t->penalty = 0;
  t->pre_flat_length = 0;
  t->pre_explode_cost = 0;
  t->pre_force_counter = 0;
  t->pre_group_end = -1;
}

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

void emit_token(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_TEXT, strdup(text));
  st->buffer.size++;
}

void emit_space(struct PrpfmtState *st) {
  for (int i = st->buffer.size - 1; i >= 0; i--) {
    TokenType type = st->buffer.data[i].type;
    if (type == TOKEN_SPACE || type == TOKEN_BREAK_POINT) {
      return;
    }
    if (type == TOKEN_NEWLINE || type == TOKEN_FORCE_BREAK) {
      return;
    }
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

void emit_blank_line(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_NEWLINE, NULL);
  st->buffer.size++;
}

void emit_break_point(struct PrpfmtState *st, int penalty) {
  if (has_recent_break(st)) {
    return;
  }
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_BREAK_POINT, NULL);
  st->buffer.data[st->buffer.size].penalty = penalty;
  st->buffer.size++;
}

void emit_soft_break(struct PrpfmtState *st, int penalty) {
  if (has_recent_break(st)) {
    return;
  }
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_SOFT_BREAK, NULL);
  st->buffer.data[st->buffer.size].penalty = penalty;
  st->buffer.size++;
}

void emit_soft_space(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_SOFT_SPACE, NULL);
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

void emit_group_start(struct PrpfmtState *st, bool force_explode, bool propagates) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_GROUP_START, NULL);
  st->buffer.data[st->buffer.size].exploded = force_explode;
  st->buffer.data[st->buffer.size].propagates = propagates;
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

void emit_align_math(struct PrpfmtState *st, const char *text) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ALIGN_MATH, strdup(text));
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

void emit_anchor_off(struct PrpfmtState *st) {
  ensure_capacity(st);
  init_token(&st->buffer.data[st->buffer.size], TOKEN_ANCHOR_OFF, NULL);
  st->buffer.size++;
}

void emit_line_break(struct PrpfmtState *st) {
  if (!has_recent_break(st))
    emit_blank_line(st);
}

void emit_force_break(struct PrpfmtState *st) {
  if (!has_recent_break(st)) {
    ensure_capacity(st);
    init_token(&st->buffer.data[st->buffer.size], TOKEN_FORCE_BREAK, NULL);
    st->buffer.size++;
  }
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

static void simulate_step(Token *t, int *col, int *indent, int *at_start,
                          int indent_size, bool *exp_stack, int *anc_stack,
                          int *stack_ptr, TokenType channel_filter) {
  bool is_exploded = *stack_ptr >= 0 ? exp_stack[*stack_ptr] : false;
  int current_anchor = *stack_ptr >= 0 ? anc_stack[*stack_ptr] : -1;

  if (*at_start &&
      (t->type == TOKEN_TEXT || t->type == TOKEN_ALIGN_OPERATOR ||
       t->type == TOKEN_ALIGN_RELATIONAL || t->type == TOKEN_ALIGN_MATH ||
       t->type == TOKEN_ALIGN_COMMENT)) {
    int baseline = (current_anchor >= 0) ? current_anchor : 0;
    *col += baseline + (*indent * indent_size);
    *at_start = 0;
  }

  switch (t->type) {
  case TOKEN_TEXT:
    if (t->text) {
      *col += (int)strlen(t->text);
    }
    break;
  case TOKEN_ALIGN_OPERATOR:
  case TOKEN_ALIGN_RELATIONAL:
  case TOKEN_ALIGN_MATH:
  case TOKEN_ALIGN_COMMENT:
    if (t->target_col > 0 &&
        (channel_filter == TOKEN_TEXT || t->type == channel_filter)) {
      *col = t->target_col;
    }
    if ((t->type == TOKEN_ALIGN_OPERATOR ||
         t->type == TOKEN_ALIGN_RELATIONAL) &&
        t->text && strcmp(t->text, ":") != 0 && *stack_ptr >= 0) {
      anc_stack[*stack_ptr] = *col;
    }
    if (t->text) {
      *col += (int)strlen(t->text);
    }
    break;
  case TOKEN_ANCHOR:
    if (*stack_ptr >= 0) {
      anc_stack[*stack_ptr] = *col;
    }
    break;
  case TOKEN_ANCHOR_OFF:
    if (*stack_ptr >= 0) {
      anc_stack[*stack_ptr] = -1;
    }
    break;
  case TOKEN_SPACE:
    (*col)++;
    break;
  case TOKEN_NEWLINE:
  case TOKEN_FORCE_BREAK:
    *col = 0;
    *at_start = 1;
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
  case TOKEN_SOFT_SPACE:
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

static void calculate_group_metrics(struct PrpfmtState *st) {
  int w_stack[1024], w_top = -1;
  int a_stack[1024], a_top = -1;
  int cur_off = 0, cur_cost = 0, f_count = 0;

  for (int i = 0; i < st->buffer.size; i++) {
    Token *t = &st->buffer.data[i];
    if (t->type == TOKEN_GROUP_START) {
      t->pre_flat_length = cur_off;
      t->pre_explode_cost = cur_cost;
      t->pre_force_counter = f_count;
      if (w_top < 1023) {
        w_stack[++w_top] = i;
      }
    } else if (t->type == TOKEN_GROUP_END) {
      if (w_top >= 0) {
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
        a_stack[++a_top] = i;
      }
    } else if (t->type == TOKEN_ALIGN_GROUP_END) {
      if (a_top >= 0) {
        st->buffer.data[a_stack[a_top--]].pre_group_end = i;
      }
    } else {
      switch (t->type) {
      case TOKEN_TEXT:
      case TOKEN_ALIGN_OPERATOR:
      case TOKEN_ALIGN_RELATIONAL:
      case TOKEN_ALIGN_MATH:
      case TOKEN_ALIGN_COMMENT:
        if (t->text) {
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
        f_count++;
        break;
      default:
        break;
      }
    }
  }
}

void prpfmt_solve(struct PrpfmtState *st) {
  calculate_group_metrics(st);
  int col = 0, indent = 0, at_start = 1, s_ptr = -1;
  bool e_stack[256];
  bool p_stack[256];
  int a_stack[256];

  for (int i = 0; i < st->buffer.size; i++) {
    Token *t = &st->buffer.data[i];
    if (t->type == TOKEN_GROUP_START) {
      if (!t->exploded) {
        bool forced = (bool)t->pre_force_counter;
        bool parent_exp = (s_ptr >= 0) ? e_stack[s_ptr] : false;
        bool parent_prop = (s_ptr >= 0) ? p_stack[s_ptr] : false;
        bool should_exp = forced;
        if (!should_exp) {
          int overflow = (col + t->pre_flat_length) - st->max_width;
          should_exp =
              ((overflow > 0 ? overflow * 1000 : 0) > t->pre_explode_cost);
        }
        if (!should_exp && parent_exp && parent_prop && t->propagates) {
          should_exp = true;
        }
        t->exploded = should_exp;
      }
    }
    int tmp_s_ptr = s_ptr;
    simulate_step(t, &col, &indent, &at_start, st->indent_size, e_stack,
                  a_stack, &tmp_s_ptr, TOKEN_TEXT);
    if (t->type == TOKEN_GROUP_START) {
      s_ptr++;
      if (s_ptr >= 0 && s_ptr < 256) {
        p_stack[s_ptr] = t->propagates;
      }
    } else if (t->type == TOKEN_GROUP_END) {
      if (s_ptr >= 0) {
        s_ptr--;
      }
    }
  }

  int m_col = 0, m_indent = 0, m_at_start = 1, m_s_ptr = 0;
  bool m_e_stack[256];
  for (int k = 0; k < 256; k++) {
    m_e_stack[k] = false;
  }
  int m_a_stack[256];
  for (int k = 0; k < 256; k++) {
    m_a_stack[k] = -1;
  }

  for (int i = 0; i < st->buffer.size; i++) {
    Token *main_t = &st->buffer.data[i];
    if (main_t->type == TOKEN_ALIGN_GROUP_START) {
      int base_ind = m_indent, base_sp = m_s_ptr, g_end = main_t->pre_group_end;
      if (g_end > i) {
        int l_count = 0;
        {
          int tc = 0, ti = base_ind, ts = 1, tsp = base_sp;
          bool te[256];
          for (int k = 0; k <= base_sp && k < 256; k++) {
            te[k] = m_e_stack[k];
          }
          int ta[256];
          for (int k = 0; k <= base_sp && k < 256; k++) {
            ta[k] = m_a_stack[k];
          }
          for (int j = i + 1; j < g_end; j++) {
            if (ts) {
              l_count++;
            }
            simulate_step(&st->buffer.data[j], &tc, &ti, &ts, st->indent_size,
                          te, ta, &tsp, TOKEN_TEXT);
          }
        }
        l_count += 5;
        TokenType chans[] = {TOKEN_ALIGN_OPERATOR, TOKEN_ALIGN_RELATIONAL,
                             TOKEN_ALIGN_MATH, TOKEN_ALIGN_COMMENT};
        for (int c = 0; c < 4; c++) {
          TokenType chan = chans[c];
          int *h_base = calloc(l_count, sizeof(int)),
              *h_op = calloc(l_count, sizeof(int)),
              *m_val = calloc(l_count, sizeof(int));
          int col = 0, ind = base_ind, ats = 1, sp = base_sp, cur_l = -1,
              last_l = -1;
          bool e[256];
          for (int k = 0; k <= base_sp && k < 256; k++) {
            e[k] = m_e_stack[k];
          }
          int a[256];
          for (int k = 0; k <= base_sp && k < 256; k++) {
            a[k] = m_a_stack[k];
          }
          for (int j = i + 1; j < g_end; j++) {
            Token *t = &st->buffer.data[j];
            if (ats) {
              cur_l++;
            }
            if (cur_l >= 0 && cur_l < l_count) {
              if (ind == base_ind && sp <= base_sp + 3 &&
                  (t->type == TOKEN_TEXT || t->type == TOKEN_ALIGN_OPERATOR ||
                   t->type == TOKEN_ALIGN_RELATIONAL ||
                   t->type == TOKEN_ALIGN_MATH ||
                   t->type == TOKEN_ALIGN_COMMENT)) {
                h_base[cur_l] = 1;
              }
              if (t->type == chan) {
                if (cur_l != last_l && ind == base_ind && sp <= base_sp + 3 &&
                    (chan != TOKEN_ALIGN_MATH || ats)) {
                  if (chan != TOKEN_ALIGN_COMMENT || ind == base_ind) {
                    int ac = col;
                    if (ats) {
                      ac = ((sp >= 0 && a[sp] >= 0) ? a[sp] : 0) +
                           (ind * st->indent_size);
                    }
                    int mv = ac;
                    if (chan != TOKEN_ALIGN_COMMENT && t->text) {
                      mv += (int)strlen(t->text);
                    }
                    h_op[cur_l] = 1;
                    m_val[cur_l] = mv;
                    last_l = cur_l;
                  }
                }
              }
            }
            simulate_step(t, &col, &ind, &ats, st->indent_size, e, a, &sp,
                          TOKEN_TEXT);
          }
          int max_l = cur_l;
          int *cl_max = calloc(max_l + 1, sizeof(int));
          if (chan == TOKEN_ALIGN_COMMENT) {
            int g_max = 0, g_cnt = 0;
            for (int l = 0; l <= max_l; l++) {
              if (h_op[l]) {
                if (m_val[l] > g_max) {
                  g_max = m_val[l];
                }
                g_cnt++;
              }
            }
            if (g_cnt > 1) {
              for (int l = 0; l <= max_l; l++) {
                cl_max[l] = g_max;
              }
            }
          } else {
            int c_max = 0, c_cnt = 0, s_l = 0;
            for (int l = 0; l <= max_l; l++) {
              if (h_base[l]) {
                if (h_op[l]) {
                  if (m_val[l] > c_max) {
                    c_max = m_val[l];
                  }
                  c_cnt++;
                } else {
                  for (int cl = s_l; cl < l; cl++) {
                    cl_max[cl] = (c_cnt > 1) ? c_max : 0;
                  }
                  c_max = 0;
                  c_cnt = 0;
                  s_l = l + 1;
                }
              }
            }
            for (int cl = s_l; cl <= max_l; cl++) {
              cl_max[cl] = (c_cnt > 1) ? c_max : 0;
            }
          }
          col = 0;
          ind = base_ind;
          ats = 1;
          sp = base_sp;
          for (int k = 0; k <= base_sp && k < 256; k++) {
            a[k] = m_a_stack[k];
          }
          cur_l = -1;
          last_l = -1;
          for (int j = i + 1; j < g_end; j++) {
            Token *t = &st->buffer.data[j];
            if (ats) {
              cur_l++;
            }
            if (cur_l >= 0 && cur_l <= max_l && t->type == chan) {
              if (cur_l != last_l && ind == base_ind && sp <= base_sp + 3 &&
                  (chan != TOKEN_ALIGN_MATH || ats)) {
                if (chan != TOKEN_ALIGN_COMMENT || ind == base_ind) {
                  int ac = col;
                  if (ats) {
                    ac = ((sp >= 0 && a[sp] >= 0) ? a[sp] : 0) +
                         (ind * st->indent_size);
                  }
                  int target = cl_max[cur_l];
                  if (target > 0) {
                    if (chan != TOKEN_ALIGN_COMMENT && t->text) {
                      target -= (int)strlen(t->text);
                    }
                    if (target > ac) {
                      bool allow =
                          (chan == TOKEN_ALIGN_COMMENT)
                              ? (target - ac <= 20)
                              : (target - ac <= 15 || target - ac <= ac / 4);
                      if (allow) {
                        t->target_col = target;
                      }
                    }
                  }
                  last_l = cur_l;
                }
              }
            }
            simulate_step(t, &col, &ind, &ats, st->indent_size, e, a, &sp,
                          chan);
          }
          free(h_base);
          free(h_op);
          free(m_val);
          free(cl_max);
        }
      }
    }
    simulate_step(main_t, &m_col, &m_indent, &m_at_start, st->indent_size,
                  m_e_stack, m_a_stack, &m_s_ptr, TOKEN_TEXT);
  }
}

void prpfmt_render(struct PrpfmtState *st) {
  int ind = 0, col = 0, ats = 1, sp = 0;
  bool e_stack[256];
  int a_stack[256];
  e_stack[0] = false;
  a_stack[0] = -1;

  for (int i = 0; i < st->buffer.size; i++) {
    Token *t = &st->buffer.data[i];
    bool cur_exp = sp >= 0 ? e_stack[sp] : false;
    int cur_anc = sp >= 0 ? a_stack[sp] : -1;

    switch (t->type) {
    case TOKEN_TEXT:
    case TOKEN_ALIGN_OPERATOR:
    case TOKEN_ALIGN_RELATIONAL:
    case TOKEN_ALIGN_MATH:
    case TOKEN_ALIGN_COMMENT:
      if (ats) {
        int base = (cur_anc >= 0) ? cur_anc : 0;
        int sps = base + (ind * st->indent_size);
        for (int j = 0; j < sps; j++) {
          fprintf(st->outfile, " ");
          col++;
        }
        ats = 0;
      }
      if (t->target_col > col) {
        int pad = t->target_col - col;
        for (int j = 0; j < pad; j++) {
          fprintf(st->outfile, " ");
          col++;
        }
      }
      if ((t->type == TOKEN_ALIGN_OPERATOR ||
           t->type == TOKEN_ALIGN_RELATIONAL) &&
          t->text && strcmp(t->text, ":") != 0) {
        if (sp >= 0) {
          a_stack[sp] = col;
        }
      }
      if (t->text) {
        fprintf(st->outfile, "%s", t->text);
        col += (int)strlen(t->text);
      }
      break;
    case TOKEN_ANCHOR:
      if (sp >= 0) {
        a_stack[sp] = col;
      }
      break;
    case TOKEN_ANCHOR_OFF:
      if (sp >= 0) {
        a_stack[sp] = -1;
      }
      break;
    case TOKEN_SPACE:
      if (!ats) {
        fprintf(st->outfile, " ");
        col++;
      }
      break;
    case TOKEN_NEWLINE:
    case TOKEN_FORCE_BREAK:
      fprintf(st->outfile, "\n");
      ats = 1;
      col = 0;
      break;
    case TOKEN_BREAK_POINT:
      if (cur_exp) {
        fprintf(st->outfile, "\n");
        ats = 1;
        col = 0;
      } else if (!ats) {
        fprintf(st->outfile, " ");
        col++;
      }
      break;
    case TOKEN_SOFT_BREAK:
      if (cur_exp) {
        fprintf(st->outfile, "\n");
        ats = 1;
        col = 0;
      }
      break;
    case TOKEN_SOFT_SPACE:
      if (cur_exp && !ats) {
        fprintf(st->outfile, " ");
        col++;
      }
      break;
    case TOKEN_INDENT_INC:
      ind++;
      break;
    case TOKEN_INDENT_DEC:
      ind--;
      break;
    case TOKEN_GROUP_START:
      sp++;
      if (sp < 256) {
        e_stack[sp] = t->exploded;
        a_stack[sp] = (sp > 0) ? a_stack[sp - 1] : -1;
      }
      break;
    case TOKEN_GROUP_END:
      if (sp > 0) {
        sp--;
      }
      break;
    default:
      break;
    }
  }
}
