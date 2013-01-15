/*
 * Copyright (C) 2011-2013 Karlsruhe Institute of Technology
 *
 * This file is part of Ufo.
 *
 * This library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

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

