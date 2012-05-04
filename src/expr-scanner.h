#ifndef EXPR_SCANNER_H
#define EXPR_SCANNER_H

typedef enum {
    WHITESPACE = 0,
    LPAREN,
    RPAREN,
    FLOAT,
    INTEGER,
    IDENT_X,
    IDENT_Y,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    FUNC,
    END
} Symbol;

typedef struct {
    Symbol sym;
    struct {
        int i;
        float f;
        char c;
        char *s;
    } value;
} Node;

Node *tokenize_expression(const char *input);

#endif

