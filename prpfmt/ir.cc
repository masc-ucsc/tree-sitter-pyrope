#include <algorithm>
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
  // Raw `prpfmt off` text keeps its spacing; only formatted comments are marked.
  t.block_comment = st.fmt_on && (type == TOKEN_TEXT || type == TOKEN_ALIGN_COMMENT) && text.starts_with("/*");
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
    if (type == TOKEN_TEXT || type == TOKEN_SPACE || type == TOKEN_SOFT_TEXT ||
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

int text_width(std::string_view text) {
  int width = 0;
  for (unsigned char c : text)
    if ((c & 0xC0) != 0x80) ++width;  // skip UTF-8 continuation bytes
  return width;
}

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
  // `a /* c */ + b`: the break prints the space itself, flat or broken.
  if (!st.buffer.empty() && st.buffer.back().type == TOKEN_SPACE) st.buffer.pop_back();
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

// Append text that prints only when the enclosing group is exploded (the
// trailing `,` of a one-item-per-line list); it has no flat width.
void emit_soft_text(PrpfmtState &st, std::string_view text) {
  push_text(st, TOKEN_SOFT_TEXT, text);
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
  st.open_groups.push_back((int)st.buffer.size() - 1);
}

// End the current smart-wrapping group, pairing it with its start (Token::match
// lets a pass over the buffer jump over a closed group in O(1)).
void emit_group_end(PrpfmtState &st) {
  push_token(st, TOKEN_GROUP_END);
  if (st.open_groups.empty()) return;
  int start = st.open_groups.back(), end = (int)st.buffer.size() - 1;
  st.open_groups.pop_back();
  st.buffer[start].match = end;
  st.buffer[end].match = start;
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
                          int indent_size, std::span<char> exp_stack,
                          std::span<int> anc_stack, int &stack_ptr,
                          TokenType channel_filter) {
  // The stacks are sized to the deepest group nesting (group_depth), so
  // stack_ptr stays in range; the clamp is a guard ("flat, no anchor") that
  // never indexes out of bounds.
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
      col += text_width(t.text);
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

      col += text_width(t.text);
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
    case TOKEN_SOFT_TEXT:
      if (is_exploded) col += text_width(t.text);
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

// Deepest group nesting in the buffer, which sizes the solver's and the
// renderer's per-level stacks (fixed-size stacks silently flattened layouts
// nested deeper than they could hold, so a second pass printed them differently).
static int group_depth(const PrpfmtState &st) {
  int depth = 0, deepest = 0;
  for (const auto &t : st.buffer) {
    if (t.type == TOKEN_GROUP_START) deepest = std::max(deepest, ++depth);
    else if (t.type == TOKEN_GROUP_END && depth > 0) --depth;
  }
  return deepest;
}

static void calculate_group_metrics(PrpfmtState &st) {
  std::vector<int> w_stack(st.buffer.size() + 1);
  int w_top = -1;
  std::vector<int> anchor_stack(st.buffer.size() + 1);
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

      w_stack[++w_top] = i;
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
        // Groups nested in a yielding group see its end in their suffix.
        t.yield_next = st_t.yield_next;
      }
    } else if (t.type == TOKEN_ALIGN_GROUP_START) {
      anchor_stack[++a_top] = i;
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
          else cur_off += text_width(t.text);
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

// An inline `if` chain that does not fit puts each `elif`/`else` branch on a
// continuation line with its block inline. When one of those lines still
// overflows, breaking only that block would print a `}` newline `elif`
// staircase, so the whole chain takes the statement layout instead: the
// continuation breaks become spaces and every branch block breaks
// (`if c {` newline body newline `} elif d {` ... `}`).
static bool chain_overflows(const PrpfmtState &st, int start, int column, int indent, int suffix) {
  const int end = st.buffer[start].pre_group_end;
  int depth = 0, cont = 0, width = column;
  bool overflow = false;
  for (int k = start + 1; k < end && !overflow; ++k) {
    const Token &t = st.buffer[k];
    switch (t.type) {
      case TOKEN_GROUP_START: ++depth; break;
      case TOKEN_GROUP_END: --depth; break;
      case TOKEN_INDENT_INC: if (depth == 0) ++cont; break;
      case TOKEN_INDENT_DEC: if (depth == 0) --cont; break;
      case TOKEN_BREAK_POINT:
        if (depth == 0) {
          overflow = width > st.max_width;
          width = (indent + cont) * st.indent_size;
        } else ++width;
        break;
      case TOKEN_SPACE: ++width; break;
      case TOKEN_TEXT: case TOKEN_ALIGN_OPERATOR: case TOKEN_ALIGN_RELATIONAL:
      case TOKEN_ALIGN_MATH: case TOKEN_ALIGN_COMMENT:
        width += text_width(t.text);
        break;
      default: break;
    }
  }
  return overflow || width + suffix > st.max_width;
}

// The whole chain takes the statement layout. Also used when a branch holds
// a required line break (a block comment that starts its source line): one
// branch that must break breaks them all, never a `}` newline `else` tail.
static void statement_chain(PrpfmtState &st, int start) {
  const int end = st.buffer[start].pre_group_end;
  int depth = 0;
  for (int k = start + 1; k < end; ++k) {
    Token &t = st.buffer[k];
    if (t.type == TOKEN_GROUP_START) {
      if (depth == 0 && t.chain_role == 2) t.exploded = true;
      ++depth;
    } else if (t.type == TOKEN_GROUP_END) --depth;
    else if (depth == 0 && t.type == TOKEN_BREAK_POINT) t.type = TOKEN_SPACE;
    else if (depth == 0 && (t.type == TOKEN_INDENT_INC || t.type == TOKEN_INDENT_DEC)) {
      t.type = TOKEN_SOFT_TEXT;  // no-op: no text, no indentation change
      t.text.clear();
    }
  }
}

static void statement_chain_if_needed(PrpfmtState &st, int start, int column, int indent, int suffix) {
  if (st.buffer[start].pre_group_end > start && chain_overflows(st, start, column, indent, suffix))
    statement_chain(st, start);
}

// Tokens that print nothing and never separate two texts.
static bool is_marker(const Token &t) {
  switch (t.type) {
    case TOKEN_GROUP_START: case TOKEN_GROUP_END: case TOKEN_ALIGN_GROUP_START: case TOKEN_ALIGN_GROUP_END:
    case TOKEN_INDENT_INC: case TOKEN_INDENT_DEC: case TOKEN_ANCHOR: case TOKEN_ANCHOR_OFF:
      return true;
    case TOKEN_TEXT: case TOKEN_ALIGN_OPERATOR: case TOKEN_ALIGN_RELATIONAL: case TOKEN_ALIGN_MATH:
    case TOKEN_ALIGN_COMMENT: case TOKEN_SOFT_TEXT:
      return t.text.empty();
    default:
      return false;
  }
}

// `t` always prints whitespace (a space, or a line break) on the side that
// touches its neighbor (`leading`: the side that follows the neighbor).
static bool separates(const Token &t, bool leading) {
  switch (t.type) {
    case TOKEN_SPACE: case TOKEN_BREAK_POINT: case TOKEN_NEWLINE: case TOKEN_FORCE_BREAK:
      return true;
    case TOKEN_TEXT: case TOKEN_ALIGN_OPERATOR: case TOKEN_ALIGN_RELATIONAL: case TOKEN_ALIGN_MATH:
    case TOKEN_ALIGN_COMMENT:
      return std::isspace((unsigned char)(leading ? t.text.front() : t.text.back()));
    default:  // a soft break, soft space or soft text prints nothing when flat
      return false;
  }
}

// A block comment always has whitespace on both sides (`f( /* c */ a )`,
// `a:u4 /* g */ =3`, `x /* c */ , y`): wherever a printer glued one to a
// neighbor (an opening or closing bracket, a comma, an operator), or left
// only a break that prints nothing when flat, a space goes in between. A
// space at a line start or end prints nothing, so a broken layout is
// unchanged. Runs before the layout is solved, so widths count the spaces.
static void space_block_comments(PrpfmtState &st) {
  auto &b = st.buffer;
  std::vector<char> space_before(b.size(), 0), space_after(b.size(), 0);
  bool any = false;
  for (size_t i = 0; i < b.size(); ++i) {
    if (!b[i].block_comment) continue;
    size_t k = i;
    while (k > 0 && is_marker(b[k - 1])) --k;
    if (k > 0 && !separates(b[k - 1], false)) space_before[i] = any = true;
    k = i + 1;
    while (k < b.size() && is_marker(b[k])) ++k;
    if (k < b.size() && !separates(b[k], true)) space_after[i] = any = true;
  }
  if (!any) return;
  TokenBuffer out;
  out.reserve(b.size() + b.size() / 8);
  std::vector<int> where(b.size());
  for (size_t i = 0; i < b.size(); ++i) {
    if (space_before[i]) out.emplace_back().type = TOKEN_SPACE;
    where[i] = (int)out.size();
    out.push_back(std::move(b[i]));
    if (space_after[i]) out.emplace_back().type = TOKEN_SPACE;
  }
  for (auto &t : out)
    if (t.match >= 0) t.match = where[t.match];
  b = std::move(out);
}

// Determine line breaks and column alignments for all tokens in the IR buffer
// AI mode has no width limit, except for an inline `if`/`elif`/`else`
// EXPRESSION chain: one longer than the default width puts each branch on its
// own continuation line, the Human-mode chain layout (suggestions6 1.9: a
// 7-line chain used to come back as one 150+ column line). The width is fixed,
// so --width still has no effect in AI mode and the layout stays canonical.
constexpr int kAiChainWidth = 132;

void prpfmt_solve(PrpfmtState &st) {
  space_block_comments(st);
  struct Width_guard {
    PrpfmtState &st;
    int          saved;
    ~Width_guard() { st.max_width = saved; }
  } width_guard{st, st.max_width};
  if (st.mode == PRPFMT_AI) st.max_width = kAiChainWidth;

  // Pre-pass: Measure flat widths and explosion penalties before simulating layout
  calculate_group_metrics(st);
  const size_t levels = size_t(group_depth(st)) + 2;
  int col = 0, indent = 0, at_start = 1, s_ptr = 0;
  std::vector<char> explode_stack(levels, 0);
  std::vector<char> propagate_stack(levels, 0);
  std::vector<int> anchor_stack(levels, -1);
  // An inline `if` chain that fits (within its slack) keeps what it holds
  // flat as well: a branch's operator chain does not break on its own
  // (`else { x` newline `+ y }`) while the chain stays on one line.
  std::vector<char> fits_stack(levels, 0);
  // AI mode measures only the OUTERMOST inline `if` chain (kAiChainWidth): a
  // chain nested inside another stays flat, which keeps AI mode linear on
  // deeply nested chains (Human mode measures them all).
  std::vector<char> in_chain_stack(levels, 0);

  // Phase 1: Determine which groups must explode (wrap) based on width and penalties
  // Simulate the token layout sequentially to determine the actual column positions
  for (int i = 0; i < (int)st.buffer.size(); i++) {
    Token &t = st.buffer[i];

    bool fits_here = false;
    if (t.type == TOKEN_GROUP_START) {
      if (!t.exploded) {
        bool forced = st.mode == PRPFMT_HUMAN && t.pre_force_counter;
        bool parent_in_bounds = s_ptr >= 0 && s_ptr < (int)explode_stack.size();
        bool parent_exp = parent_in_bounds ? explode_stack[s_ptr] : false;
        bool parent_prop = parent_in_bounds ? propagate_stack[s_ptr] : false;
        bool parent_fits = parent_in_bounds && fits_stack[s_ptr];
        bool should_exp = forced;
        if (forced && t.chain_role == 1 && t.pre_group_end > i) statement_chain(st, i);

        // Human mode measures every group; AI mode only an outermost inline `if` chain.
        const bool in_chain = parent_in_bounds && in_chain_stack[s_ptr];
        const bool measured = st.mode == PRPFMT_HUMAN || (t.chain_role == 1 && !in_chain);
        if (!should_exp && measured && parent_fits) {
          fits_here = true;  // see fits_stack
        } else if (!should_exp && measured) {
          // Include indentation and any flat suffix on the same line: closing
          // delimiters and the output clause of a header. A trailing comment
          // stays attached without forcing otherwise fitting code to wrap.
          int column = at_start ? indent * st.indent_size : col;
          if (at_start && parent_in_bounds && anchor_stack[s_ptr] > 0) column += anchor_stack[s_ptr];
          int suffix = 0;
          bool yield_armed = t.yield_next;
          const bool yield_deep = t.yield_next && t.yield_deep;
          for (int j = t.pre_group_end + 1; j > 0 && j < (int)st.buffer.size(); ++j) {
            auto &next = st.buffer[j];
            if (is_comment_token(next)) {
              if (j > 0 && st.buffer[j - 1].type == TOKEN_SPACE) --suffix;
              break;
            }
            if (next.type == TOKEN_BREAK_POINT || next.type == TOKEN_SOFT_BREAK ||
                next.type == TOKEN_NEWLINE || next.type == TOKEN_FORCE_BREAK) break;
            if (next.type == TOKEN_GROUP_START) {
              // A yielding group (a generic list, an `if` branch header) lets
              // the next group split first: only that group's opening text up
              // to its first break (`(`, `{`) counts, so `f<T=u1>(` and
              // `elif c {` stay whole and the arguments or the block break.
              // An inline `if` chain can break itself (continuation lines or
              // the statement layout), so nothing of it counts against what
              // precedes it: `x:T(bits=N) = if c {` keeps the type
              // annotation whole (its head may overflow modestly) and the
              // chain breaks instead.
              if (next.chain_role == 1 || next.chain_role == 3) break;
              bool yields = yield_armed;
              yield_armed = yields && yield_deep;  // a deep yield lasts until the first break
              if (!yields && !next.pre_force_counter && !next.pre_comment_counter && !next.exploded) {
                suffix += next.pre_flat_length;
                j = next.pre_group_end;
                if (next.yield_next) yield_armed = true;
              }
            } else if (next.type == TOKEN_GROUP_END) {
              if (next.yield_next) yield_armed = true;
            } else if (next.type == TOKEN_SPACE) ++suffix;
            else suffix += text_width(next.text);
          }
          int slack = std::min(overflow_slack(st), t.pre_explode_cost / 10);
          should_exp = static_cast<long long>(column) + t.pre_flat_length + suffix >
                       static_cast<long long>(st.max_width) + slack;
          if (should_exp && t.chain_role == 1) statement_chain_if_needed(st, i, column, indent, suffix);
          fits_here = !should_exp && t.chain_role == 1;
        }

        // A branch block of an inline `if` chain breaks only with its chain
        // (continuation lines, or the statement layout set by
        // statement_chain_if_needed); on its own it would print a
        // `{ a } else {` newline ... staircase.
        if (should_exp && !forced && t.chain_role == 2 && !parent_exp) should_exp = false;

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

      if (s_ptr >= 0 && s_ptr < (int)propagate_stack.size()) {
        propagate_stack[s_ptr] = t.propagates;
        fits_stack[s_ptr] = fits_here;
        in_chain_stack[s_ptr] = (s_ptr > 0 && in_chain_stack[s_ptr - 1]) || t.chain_role == 1 || t.chain_role == 3;
      }
    } else if (t.type == TOKEN_GROUP_END) {
      if (s_ptr >= 0) {
        s_ptr--;
      }
    }
  }

  if (st.mode == PRPFMT_AI) return;

  int main_col = 0, main_indent = 0, main_start = 1, main_sp = 0;
  std::vector<char> main_exp(levels, 0);
  std::vector<int> main_anc(levels, -1);
  for (int i = 0; i < (int)st.buffer.size(); ++i) {
    auto &token = st.buffer[i];
    if (token.type == TOKEN_ALIGN_GROUP_START && token.pre_group_end > i) {
      int c = main_col, ind = main_indent, start = main_start, sp = main_sp;
      auto exp = main_exp;
      auto anc = main_anc;
      // Runs of statements whose operator sits on the statement's first
      // line. A statement broken before its operator (`mut f:Unsigned(`
      // newline ... `) = 0`) joins no run and ends the current one, so no
      // padding lands after a closing `)`.
      std::vector<std::vector<std::pair<int, int>>> runs(1);
      std::vector<std::pair<int, int>> operators;
      bool seen_operator = false;
      bool first_line = true;
      const int base_sp = sp;
      for (int j = i + 1; j < token.pre_group_end; ++j) {
        auto &t = st.buffer[j];
        if (t.type == TOKEN_ALIGN_OPERATOR && ind == main_indent && !seen_operator) {
          int column = start ? ind * st.indent_size : c;
          if (first_line) runs.back().emplace_back(j, column);
          else if (!runs.back().empty()) runs.emplace_back();
          seen_operator = true;
        }
        bool was_start = start;
        simulate_step(t, c, ind, start, st.indent_size, exp, anc, sp, TOKEN_TEXT);
        if (start && !was_start) first_line = sp == base_sp;  // a new statement, or a continuation
        // A trailing comment's line break sits inside the comment's own
        // group; once that group closes at the line start, the line is the
        // next statement's (`mut p = 1 // c` newline `mut arr = 2` aligns).
        else if (start && t.type == TOKEN_GROUP_END && sp == base_sp) first_line = true;
        if (start) seen_operator = false;
      }
      for (auto &run : runs) {
        if (run.size() < 2) continue;
        int target = 0;
        for (auto [index, column] : run) target = std::max(target, column);
        for (auto [index, column] : run) {
          st.buffer[index].target_col = target;
          operators.emplace_back(index, column);
        }
      }
      if (!operators.empty()) {
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
  const size_t levels = size_t(group_depth(st)) + 2;
  std::vector<char> explode_stack(levels, 0);
  std::vector<int> anchor_stack(levels, -1);

  for (int i = 0; i < (int)st.buffer.size(); i++) {
    const Token &t = st.buffer[i];
    // Retrieve context state for the current nesting level (the stacks hold
    // the deepest nesting; the clamp only guards against indexing out of
    // bounds).
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
          col += text_width(t.text);
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
      case TOKEN_SOFT_TEXT:
        // Print the text only if exploded (never at a line start)
        if (is_exploded && !at_start) {
          output += t.text;
          col += text_width(t.text);
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
        if (stack_ptr < (int)explode_stack.size()) {
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
