#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ir.h"
#include "prpfmt.h" // Needed because emit functions access PrpfmtState internals

// Initialize buffer
static void ensure_capacity(struct PrpfmtState *st) {
    if (st->buffer.size >= st->buffer.capacity) {
        st->buffer.capacity = st->buffer.capacity == 0 ? 1024 : st->buffer.capacity * 2;
        st->buffer.data = realloc(st->buffer.data, st->buffer.capacity * sizeof(Token));
    }
}

void emit_token(struct PrpfmtState *st, const char *text) {
    ensure_capacity(st);
    st->buffer.data[st->buffer.size].type = TOKEN_TEXT;
    st->buffer.data[st->buffer.size].text = strdup(text);
    st->buffer.size++;
}

void emit_space(struct PrpfmtState *st) {
    ensure_capacity(st);
    st->buffer.data[st->buffer.size].type = TOKEN_SPACE;
    st->buffer.data[st->buffer.size].text = NULL;
    st->buffer.size++;
}

void emit_newline(struct PrpfmtState *st) {
    ensure_capacity(st);
    st->buffer.data[st->buffer.size].type = TOKEN_NEWLINE;
    st->buffer.data[st->buffer.size].text = NULL;
    st->buffer.size++;
}

void emit_break_point(struct PrpfmtState *st) {
    ensure_capacity(st);
    st->buffer.data[st->buffer.size].type = TOKEN_BREAK_POINT;
    st->buffer.data[st->buffer.size].text = NULL;
    st->buffer.size++;
}

void emit_indent_inc(struct PrpfmtState *st) {
    ensure_capacity(st);
    st->buffer.data[st->buffer.size].type = TOKEN_INDENT_INC;
    st->buffer.data[st->buffer.size].text = NULL;
    st->buffer.size++;
}

void emit_indent_dec(struct PrpfmtState *st) {
    ensure_capacity(st);
    st->buffer.data[st->buffer.size].type = TOKEN_INDENT_DEC;
    st->buffer.data[st->buffer.size].text = NULL;
    st->buffer.size++;
}

void prpfmt_render(struct PrpfmtState *st) {
    int current_indent = 0;
    int at_start_of_line = 1; // Start of file is effectively start of line

    for (int i = 0; i < st->buffer.size; i++) {
        Token *t = &st->buffer.data[i];
        switch (t->type) {
            case TOKEN_TEXT:
                if (at_start_of_line) {
                    for (int j = 0; j < current_indent * st->indent_size; j++) {
                        fprintf(st->outfile, " ");
                    }
                    at_start_of_line = 0;
                }
                fprintf(st->outfile, "%s", t->text);
                break;
            case TOKEN_SPACE:
                if (at_start_of_line) {
                   // Skip leading spaces on a line, let indentation handle it
                } else {
                    fprintf(st->outfile, " ");
                }
                break;
            case TOKEN_NEWLINE:
                fprintf(st->outfile, "\n");
                at_start_of_line = 1;
                break;
            case TOKEN_BREAK_POINT:
                if (!at_start_of_line) {
                    fprintf(st->outfile, " ");
                }
                break;
            case TOKEN_INDENT_INC:
                current_indent++;
                break;
            case TOKEN_INDENT_DEC:
                current_indent--;
                break;
        }
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
