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
#ifndef CUBE_REGION_H
#define CUBE_REGION_H

#include <cube_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cb_region_data {
	u32 size;
	u32 count_boxes;
	struct cb_box boxes[0];
};

struct cb_region {
	struct cb_box extents;
	struct cb_region_data *data;
};

void cb_region_init(struct cb_region *region);

void cb_region_init_rect(struct cb_region *region, s32 x, s32 y,u32 w, u32 h);

s32 cb_region_init_boxes(struct cb_region *region,
			 const struct cb_box *boxes, s32 count);

void cb_region_init_with_extents(struct cb_region *region,
				 struct cb_box *extents);

void cb_region_fini(struct cb_region *region);

void cb_region_translate(struct cb_region *region, s32 x, s32 y);

s32 cb_region_copy(struct cb_region *dst, struct cb_region *src);

s32 cb_region_intersect(struct cb_region *n, struct cb_region *r1,
			struct cb_region *r2);

s32 cb_region_intersect_rect(struct cb_region *dst, struct cb_region *src,
			     s32 x, s32 y, u32 w, u32 h);

s32 cb_region_union(struct cb_region *n, struct cb_region *r1,
		    struct cb_region *r2);

s32 cb_region_union_rect(struct cb_region *dst, struct cb_region *src,
			 s32 x, s32 y, u32 w, u32 h);

s32 cb_region_subtract(struct cb_region *n, struct cb_region *r1,
		       struct cb_region *r2);

s32 cb_region_count_boxes(struct cb_region *region);

struct cb_box * cb_region_extents(struct cb_region *region);

s32 cb_region_is_not_empty(struct cb_region *region);

struct cb_box * cb_region_boxes(struct cb_region *region, s32 *count_boxes);

void cb_region_clear(struct cb_region *region);

#ifdef __cplusplus
}
#endif

#endif

