/*
 * Copyright © 2020 Ruinan Duan, duanruinan@zoho.com 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef CUBE_ARRAY_H
#define CUBE_ARRAY_H

#include <cube_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cb_array {
	u32 size;
	u32 alloc;
	void *data;
};

void cb_array_init(struct cb_array *array);
void cb_array_release(struct cb_array *array);
void *cb_array_add(struct cb_array *array, u32 size);
s32 cb_array_copy(struct cb_array *dst, struct cb_array *src);

#define cb_array_for_each_entry(p, array) \
	for (p = (array)->data; \
	    (const char *)p < ((const char *)(array)->data + (array)->size); \
	    (p)++)

#ifdef __cplusplus
}
#endif

#endif

