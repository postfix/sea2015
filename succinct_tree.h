/******************************************************************************
 * succinct_tree.h
 *
 * Parallel construction of succinct trees
 * For more information: http://www.inf.udec.cl/~josefuentes/sea2015/
 *
 ******************************************************************************
 * Copyright (C) 2015 José Fuentes Sepúlveda <jfuentess@udec.cl>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *****************************************************************************/

#ifndef SUCCINCT_TREE_H
#define SUCCINCT_TREE_H

#include "bit_array.h"

#include "lookup_tables.h"

lookup_table *T;
unsigned int heigh;

void st_create(BIT_ARRAY* B, unsigned long n);

int32_t find_close(BIT_ARRAY* B, int32_t i);
int32_t fwd_search(BIT_ARRAY* B, int32_t i, int8_t excess);

#endif // SUCCINCT_TREE_H