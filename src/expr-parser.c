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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <glib.h>
#include "expr-scanner.h"
#include "expr-parser.h"

static Node *node;
GString *kernel;

static void getnode(void)
{
    node++;
}

static void emit(const char *s)
{
    g_string_append(kernel, s);
}

static void emitf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    g_string_append_vprintf(kernel, format, ap);
    va_end(ap);
}

static int accept(Symbol s)
{
    if (node->sym == s) {
        getnode(); 
        return 1;
    }
    return 0;
}

static int expect(Symbol s)
{
    if (accept(s))
        return 1;
    printf("Error: expected %i\n", s);
    return 0;
}

static void term(void)
{
    if (node->sym == INTEGER) {
        emitf("%i", node->value.i);
        getnode();
    }
    else if (node->sym == FLOAT) {
        emitf("%f", node->value.f);
        getnode();
    }
    else if (accept(IDENT_X))
        emit("x[idx]");
    else if (accept(IDENT_Y))
        emit("y[idx]");
    else {
        printf("Error: expected number or identifier\n");
        getnode();
    }
}

static void expression(void)
{
    if (accept(LPAREN)) {
        emit("(");
        expression();
        expect(RPAREN);
        emit(")");
    }
    else if (node->sym == FUNC) {
        emitf(" %s", node->value.s);
        getnode();
        expect(LPAREN);
        emit("(");
        expression();
        expect(RPAREN);
        emit(")");
    }
    else if (node->sym == OP_ADD || node->sym == OP_SUB) {
        emit("+-");
        getnode();
        expression();
    }
    else if (node->sym == END) {
        return; 
    }
    else {
        term();
        switch (node->sym) {
            case OP_ADD: 
                emit("+");
                break;
            case OP_SUB:
                emit("-");
                break;
            case OP_MUL:
                emit("*");
                break;
            case OP_DIV:
                emit("/");
                break;
            default:
                /* in case of RPAREN, return */
                return;
        }
        getnode();
        expression();
    }
}


/**
 * parse_expression:
 * @expr: Mathematical expression to build a kernel from.
 *
 * Returns: A string containing the transformed OpenCL kernel. The string must
 * be freed by the caller.
 *
 * Note: This function is not thread-safe.
 */
char *parse_expression(const char *expr)
{
    kernel = g_string_new("__kernel void binary_foo_kernel_2b03c582(__global float *x, __global float *y, __global float *out)\n {\nint idx = get_global_id(1)*get_global_size(0)+get_global_id(0);\nout[idx] = ");
    node = tokenize_expression(expr); 
    expression();
    g_string_append(kernel, ";\n}");
    char *k = g_strdup(kernel->str);
    g_string_free(kernel, TRUE);
    return k;
}
