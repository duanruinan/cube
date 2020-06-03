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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_array.h>

void cb_array_init(struct cb_array *array)
{
	memset(array, 0, sizeof(*array));
}

void cb_array_release(struct cb_array *array)
{
	if (array->data)
		free(array->data);
	array->data = NULL;
	memset(array, 0, sizeof(*array));
}

void * cb_array_add(struct cb_array *array, u32 size)
{
	u32 alloc;
	void *data, *p;

	if (!array || !size)
		return NULL;

	if (array->alloc > 0)
		alloc = array->alloc;
	else
		alloc = 16;

	while (alloc < array->size + size)
		alloc *= 2;

	if (array->alloc < alloc) {
		if (array->alloc > 0)
			data = realloc(array->data, alloc);
		else
			data = malloc(alloc);

		if (data == NULL)
			return NULL;
		array->data = data;
		array->alloc = alloc;
	}

	p = array->data + array->size;
	array->size += size;

	return p;
}

s32 cb_array_copy(struct cb_array *dst, struct cb_array *src)
{
	if (!dst || !src)
		return -EINVAL;

	if (dst->size < src->size) {
		if (!cb_array_add(dst, src->size - dst->size))
			return -1;
	} else {
		dst->size = src->size;
	}

	memcpy(dst->data, src->data, src->size);
	return 0;
}

