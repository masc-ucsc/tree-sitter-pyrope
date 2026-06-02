#ifndef PRP_IR_H
#define PRP_IR_H

#include <stdio.h>
#include <stdbool.h>

typedef enum {
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
} TokenType;

typedef struct {
  TokenType type;
  char *text;
  bool exploded;           // For Groups: should this group wrap?
  bool propagates;         // For Groups: should explosion propagate to children?
  int target_col;          // For Alignment: which column should we jump to?
  int penalty;             // For Break Points: cost of breaking here
  int pre_flat_length;     // Metric: flat length of group
  int pre_explode_cost;    // Metric: total penalty of exploded children
  int pre_force_counter;   // Metric: tracks mandatory breaks inside
  int pre_group_end;       // Metric: index of matching group end
} Token;

typedef struct {
  Token *data;             // Array of IR tokens
  int size;                // Current number of tokens in the buffer
  int capacity;            // Total allocated capacity
} TokenBuffer;

struct PrpfmtState;

/******************************************************************************
 * 1. Token IR Emitters                                                       *
 ******************************************************************************/
void emit_token(struct PrpfmtState *st, const char *text);
void emit_space(struct PrpfmtState *st);
void emit_blank_line(struct PrpfmtState *st);
void emit_break_point(struct PrpfmtState *st, int penalty);
void emit_soft_break(struct PrpfmtState *st, int penalty);
void emit_soft_space(struct PrpfmtState *st);
void emit_indent_inc(struct PrpfmtState *st);
void emit_indent_dec(struct PrpfmtState *st);
void emit_group_start(struct PrpfmtState *st, bool force_explode, bool propagates);
void emit_group_end(struct PrpfmtState *st);
void emit_align_group_start(struct PrpfmtState *st);
void emit_align_group_end(struct PrpfmtState *st);
void emit_align_operator(struct PrpfmtState *st, const char *text);
void emit_align_relational(struct PrpfmtState *st, const char *text);
void emit_align_math(struct PrpfmtState *st, const char *text);
void emit_align_comment(struct PrpfmtState *st, const char *text);
void emit_anchor(struct PrpfmtState *st);
void emit_anchor_off(struct PrpfmtState *st);
void emit_line_break(struct PrpfmtState *st);
void emit_force_break(struct PrpfmtState *st);

/******************************************************************************
 * 2. Layout Rendering                                                        *
 ******************************************************************************/
void prpfmt_solve(struct PrpfmtState *st);
void prpfmt_render(struct PrpfmtState *st);
void prpfmt_free_buffer(struct PrpfmtState *st);

#endif // PRP_IR_H
