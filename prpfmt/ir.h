#ifndef PRP_IR_H
#define PRP_IR_H

#include <stdio.h>
#include <stdbool.h>

typedef enum {
  TOKEN_TEXT,
  TOKEN_SPACE,
  TOKEN_NEWLINE,
  TOKEN_BREAK_POINT,        // Optional break (space when flat, newline when exploded)
  TOKEN_SOFT_BREAK,         // Optional break (empty when flat, newline when exploded)
  TOKEN_INDENT_INC,
  TOKEN_INDENT_DEC,
  TOKEN_GROUP_START,        // Start of a smart-wrapping group
  TOKEN_GROUP_END,          // End of a smart-wrapping group
  TOKEN_ALIGN_GROUP_START,  // Start of an alignment block
  TOKEN_ALIGN_GROUP_END,    // End of an alignment block
  TOKEN_ALIGN_OPERATOR,     // Operator for alignment (e.g., =)
  TOKEN_ALIGN_RELATIONAL,   // Relational operator for alignment (e.g., ==, !=)
  TOKEN_ALIGN_COMMENT,      // Comment for alignment
  TOKEN_ANCHOR,             // Sets a vertical anchor for hanging indent without aligning
  TOKEN_ANCHOR_OFF,         // Explicitly disables the current anchor
  TOKEN_FORCE_BREAK         // Forces a newline
} TokenType;

typedef struct {
  TokenType type;
  char *text;
  bool exploded;    // For Groups: should this group wrap?
  bool propagates;  // For Groups: should explosion propagate to children?
  int target_col;   // For Alignment: which column should we jump to?
  int penalty;      // For Break Points: cost of breaking here
} Token;

typedef struct {
  Token *data;
  int size;
  int capacity;
} TokenBuffer;

struct PrpfmtState;

// Token IR Emitters
void emit_token(struct PrpfmtState *st, const char *text);
void emit_space(struct PrpfmtState *st);
void emit_blank_line(struct PrpfmtState *st);
void emit_break_point(struct PrpfmtState *st, int penalty);
void emit_soft_break(struct PrpfmtState *st, int penalty);
void emit_indent_inc(struct PrpfmtState *st);
void emit_indent_dec(struct PrpfmtState *st);
void emit_group_start(struct PrpfmtState *st, bool force_explode, bool propagates);
void emit_group_end(struct PrpfmtState *st);
void emit_align_group_start(struct PrpfmtState *st);
void emit_align_group_end(struct PrpfmtState *st);
void emit_align_operator(struct PrpfmtState *st, const char *text);
void emit_align_relational(struct PrpfmtState *st, const char *text);
void emit_align_comment(struct PrpfmtState *st, const char *text);
void emit_anchor(struct PrpfmtState *st);
void emit_anchor_off(struct PrpfmtState *st);
void emit_line_break(struct PrpfmtState *st);
void emit_force_break(struct PrpfmtState *st);

// Rendering
void prpfmt_solve(struct PrpfmtState *st);
void prpfmt_render(struct PrpfmtState *st);
void prpfmt_free_buffer(struct PrpfmtState *st);

#endif // PRP_IR_H
