#ifndef PRP_IR_H
#define PRP_IR_H

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

enum TokenType {
  TOKEN_TEXT,               // Literal source text (identifiers, keywords, etc.)
  TOKEN_SPACE,              // A mandatory space
  TOKEN_NEWLINE,            // A mandatory blank line
  TOKEN_BREAK_POINT,        // Optional break (space when flat, newline when exploded)
  TOKEN_SOFT_BREAK,         // Optional break (empty when flat, newline when exploded)
  TOKEN_SOFT_SPACE,         // Optional space (empty when flat, space when exploded)
  TOKEN_INDENT_INC,         // Increases indentation level
  TOKEN_INDENT_DEC,         // Decreases indentation level
  TOKEN_GROUP_START,        // Start of a smart-wrapping group
  TOKEN_GROUP_END,          // End of a smart-wrapping group
  TOKEN_ALIGN_GROUP_START,  // Start of an alignment block
  TOKEN_ALIGN_GROUP_END,    // End of an alignment block
  TOKEN_ALIGN_OPERATOR,     // Operator for alignment (e.g., =)
  TOKEN_ALIGN_RELATIONAL,   // Relational operator for alignment (e.g., ==, !=)
  TOKEN_ALIGN_MATH,         // Math operator for alignment (only when wrapped)
  TOKEN_ALIGN_COMMENT,      // Comment for alignment
  TOKEN_ANCHOR,             // Sets a vertical anchor for hanging indent without aligning
  TOKEN_ANCHOR_OFF,         // Explicitly disables the current anchor
  TOKEN_FORCE_BREAK         // Forces a newline
};

// A single IR token. `text` is empty for tokens that carry no literal text
// (spaces, breaks, group/indent markers); std::string owns its storage so the
// buffer is freed automatically with the vector. Defaults match the values the
// old hand-rolled init_token() stamped on every token.
struct Token {
  TokenType type{};
  std::string text;          // Literal text (empty == none)
  bool exploded = false;     // For Groups: should this group wrap?
  bool propagates = true;    // For Groups: should explosion propagate to children?
  int target_col = 0;        // For Alignment: which column should we jump to?
  int penalty = 0;           // For Break Points: cost of breaking here
  int pre_flat_length = 0;   // Metric: flat length of group
  int pre_explode_cost = 0;  // Metric: total penalty of exploded children
  int pre_force_counter = 0; // Metric: tracks mandatory breaks inside
  int pre_group_end = -1;    // Metric: index of matching group end
};

// The IR buffer is just a growable vector of tokens; std::vector subsumes the
// old {data, size, capacity} triple and its manual realloc/free.
using TokenBuffer = std::vector<Token>;

struct PrpfmtState;

/******************************************************************************
 * 1. Token IR Emitters                                                       *
 ******************************************************************************/
void emit_token(PrpfmtState &st, std::string_view text);
void emit_space(PrpfmtState &st);
void emit_blank_line(PrpfmtState &st);
void emit_break_point(PrpfmtState &st, int penalty);
void emit_soft_break(PrpfmtState &st, int penalty);
void emit_soft_space(PrpfmtState &st);
void emit_indent_inc(PrpfmtState &st);
void emit_indent_dec(PrpfmtState &st);
void emit_group_start(PrpfmtState &st, bool force_explode, bool propagates);
void emit_group_end(PrpfmtState &st);
void emit_align_group_start(PrpfmtState &st);
void emit_align_group_end(PrpfmtState &st);
void emit_align_operator(PrpfmtState &st, std::string_view text);
void emit_align_relational(PrpfmtState &st, std::string_view text);
void emit_align_math(PrpfmtState &st, std::string_view text);
void emit_align_comment(PrpfmtState &st, std::string_view text);
void emit_anchor(PrpfmtState &st);
void emit_anchor_off(PrpfmtState &st);
void emit_line_break(PrpfmtState &st);
void emit_force_break(PrpfmtState &st);

/******************************************************************************
 * 2. Layout Rendering                                                        *
 ******************************************************************************/
void prpfmt_solve(PrpfmtState &st);
void prpfmt_render(PrpfmtState &st);

#endif // PRP_IR_H
