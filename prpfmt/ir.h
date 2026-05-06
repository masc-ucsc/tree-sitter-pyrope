#ifndef PRP_IR_H
#define PRP_IR_H

#include <stdio.h>

typedef enum {
  TOKEN_TEXT,
  TOKEN_SPACE,
  TOKEN_NEWLINE,
  TOKEN_BREAK_POINT,
  TOKEN_INDENT_INC,
  TOKEN_INDENT_DEC
} TokenType;

typedef struct {
  TokenType type;
  char *text;
} Token;

typedef struct {
  Token *data;
  int size;
  int capacity;
} TokenBuffer;

// Forward declaration to avoid circular dependency if needed, 
// though passing FILE* and indent_size directly to render might be cleaner.
// For now, we will pass the buffer and the FILE/indent data.
struct PrpfmtState; 

// Token IR Emitters
void emit_token(struct PrpfmtState *st, const char *text);
void emit_space(struct PrpfmtState *st);
void emit_newline(struct PrpfmtState *st);
void emit_break_point(struct PrpfmtState *st);
void emit_indent_inc(struct PrpfmtState *st);
void emit_indent_dec(struct PrpfmtState *st);

// Rendering
void prpfmt_render(struct PrpfmtState *st);
void prpfmt_free_buffer(struct PrpfmtState *st);

#endif // PRP_IR_H
