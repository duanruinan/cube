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
#include <string.h>
#include <assert.h>
#include <cube_utils.h>
#include <cube_region.h>

static struct cb_box empty_box = {
	.p1 = {
		.x = 0,
		.y = 0,
	},
	.p2 = {
		.x = 0,
		.y = 0,
	},
};

static struct cb_box *empty_box_ptr = &empty_box;

static struct cb_region_data empty_data = {
	.size = 0,
	.count_boxes = 0,
};

static struct cb_region_data *empty_data_ptr = &empty_data;

static struct cb_region_data broken_data = {
	.size = 0,
	.count_boxes = 0,
};

static struct cb_region_data *broken_data_ptr = &broken_data;

#define GOOD_BOX(box) (((box)->p1.x < (box)->p2.x) \
			&& ((box)->p1.y < (box)->p2.y))

#define BAD_BOX(box) (((box)->p1.x > (box)->p2.x) \
			|| ((box)->p1.y < (box)->p2.y))

#define FREE_DATA(reg) if ((reg)->data && (reg)->data->size) free((reg)->data)
#define REGION_NIL(reg) ((reg)->data && !(reg)->data->count_boxes)
#define REGION_NAR(reg) ((reg)->data == broken_data_ptr)
#define REGION_COUNT_BOXES(reg) ((reg)->data ? (reg)->data->count_boxes : 1)
#define REGION_SIZE(reg) ((reg)->data ? (reg)->data->size : 0)
#define REGION_BOXES(reg) \
	((reg)->data ? (struct cb_box *)((reg)->data + 1) \
	    : &(reg)->extents)
#define REGION_BOX_PTR(reg) ((struct cb_box *)((reg)->data + 1))
#define REGION_BOX(reg, i) (&REGION_BOX_PTR(reg)[i])
#define REGION_BOX_TOP(reg) REGION_BOX(reg, (reg)->data->count_boxes)
#define REGION_BOX_END(reg) REGION_BOX(reg, (reg)->data->count_boxes - 1)

#define BOXALLOC_BAIL(region, n, bail) do { \
	if (!(region)->data \
	     || (((region)->data->count_boxes + (n)) \
	          > (region)->data->size)) { \
		if (box_alloc(region, n) < 0) \
			goto bail; \
	} \
} while (0)

#define BOXALLOC(region, n) do { \
	if (!(region)->data \
	     || (((region)->data->count_boxes + (n)) \
	          > (region)->data->size)) { \
		if (box_alloc(region, n) < 0) { \
			return -1; \
		} \
	} \
} while (0)

#define ADDBOX(next_box, nx1, ny1, nx2, ny2) do { \
	next_box->p1.x = nx1; \
	next_box->p1.y = ny1; \
	next_box->p2.x = nx2; \
	next_box->p2.y = ny2; \
	next_box++; \
} while (0)

#define NEWBOX(region, next_box, nx1, ny1, nx2, ny2) do { \
	if (!(region)->data \
	     || ((region)->data->count_boxes == (region)->data->size)) { \
		if (box_alloc(region, 1) < 0) \
			return -1; \
		next_box = REGION_BOX_TOP(region); \
	} \
	ADDBOX(next_box, nx1, ny1, nx2, ny2); \
	region->data->count_boxes++; \
	assert(region->data->count_boxes <= region->data->size); \
} while (0)

#define MERGEBOX(r) do { \
	if (r->p1.x <= x2) { \
		/* Merge with current rectangle */ \
		if (x2 < r->p2.x) \
			x2 = r->p2.x; \
	} else { \
		/* Add current rectangle, start new one */ \
		NEWBOX(region, next_rect, x1, y1, x2, y2); \
		x1 = r->p1.x; \
		x2 = r->p2.x; \
	} \
	r++; \
} while (0)

#define DOWNSIZE(reg, count_boxes) do { \
	if (((count_boxes) < ((reg)->data->size >> 1)) \
			&& ((reg)->data->size > 50)) { \
		struct cb_region_data * new_data; \
		u32 data_size = REGION_SZOF(count_boxes); \
		if (!data_size) { \
			new_data = NULL; \
		} else { \
			new_data = (struct cb_region_data *) \
				realloc((reg)->data, data_size); \
		} \
		if (new_data) { \
			new_data->size = (count_boxes); \
			(reg)->data = new_data; \
		} \
	} \
} while (0)

#define EXCHANGE_BOXES(a, b, boxes) \
	{ \
		struct cb_box t; \
		t = (boxes)[a]; \
		(boxes)[a] = (boxes)[b]; \
		(boxes)[b] = t; \
	}

#define COALESCE(new_reg, prev_band, cur_band) do { \
	if (cur_band - prev_band == new_reg->data->count_boxes - cur_band) \
		prev_band = coalesce(new_reg, prev_band, cur_band); \
	else \
		prev_band = cur_band; \
} while (0)

/*  true iff two Boxes overlap */
#define EXTENTCHECK(r1, r2) \
    (!( ((r1)->p2.x <= (r2)->p1.x) \
     || ((r1)->p1.x >= (r2)->p2.x) \
     || ((r1)->p2.y <= (r2)->p1.y) \
     || ((r1)->p1.y >= (r2)->p2.y) ) )

/* true iff (x,y) is in Box */
#define INBOX(r, x, y) \
    (  ((r)->p2.x >  x) \
    && ((r)->p1.x <= x) \
    && ((r)->p2.y >  y) \
    && ((r)->p1.y <= y) )

/* true iff Box r1 contains Box r2 */
#define SUBSUMES(r1, r2) \
    (   ((r1)->p1.x <= (r2)->p1.x) \
     && ((r1)->p2.x >= (r2)->p2.x) \
     && ((r1)->p1.y <= (r2)->p1.y) \
     && ((r1)->p2.y >= (r2)->p2.y) )

void cb_region_init(struct cb_region *region)
{
	region->extents = *empty_box_ptr;
	region->data = empty_data_ptr;
}

void cb_region_init_rect(struct cb_region *region,
			 s32 x, s32 y,
			 u32 w, u32 h)
{
	region->extents.p1.x = x;
	region->extents.p1.y = y;
	region->extents.p2.x = x + w;
	region->extents.p2.y = y + h;

	if (!GOOD_BOX(&region->extents)) {
		fprintf(stderr, "Invalid rectangle, %d,%d %ux%u\n", x, y, w, h);
		cb_region_init(region);
		return;
	}

	region->data = NULL;
}

static u32 REGION_SZOF(u32 n)
{
	u32 size = n * sizeof(struct cb_box);

	if (n > UINT32_MAX / sizeof(struct cb_box))
		return 0;

	if (sizeof(struct cb_region_data) > UINT32_MAX - size)
		return 0;

	return size + sizeof(struct cb_region_data);
}

static struct cb_region_data * alloc_data(u32 n)
{
	u32 sz = REGION_SZOF(n);

	if (!sz)
		return NULL;

	return malloc(sz);
}

static s32 set_break(struct cb_region *region)
{
	FREE_DATA(region);
	region->extents = *empty_box_ptr;
	region->data = broken_data_ptr;

	return -1;
}

static s32 box_alloc(struct cb_region *region, s32 n)
{
	struct cb_region_data *data;
	u32 data_size;

	if (!region->data) {
		n++;
		region->data = alloc_data(n);
		if (!region->data)
			return set_break(region);
		region->data->count_boxes = 1;
		*(REGION_BOX_PTR(region)) = region->extents;
	} else if (!region->data->size) {
		region->data = alloc_data(n);
		if (!region->data)
			return set_break(region);
		region->data->count_boxes = 0;
	} else {
		if (n == 1) {
			n = region->data->count_boxes;
			if (n > 500)
				n = 250;
		}
		n += region->data->count_boxes;
		data_size = REGION_SZOF(n);
		if (!data_size) {
			data = NULL;
		} else {
			data = (struct cb_region_data *)
				realloc(region->data, REGION_SZOF(n));
		}

		if (!data)
			return set_break(region);

		region->data = data;
	}

	region->data->size = n;

	return 0;
}

static inline s32 coalesce(struct cb_region *region,
			   s32 prev_start, s32 cur_start)
{
	struct cb_box *prev_box, *cur_box;
	s32 count_boxes, y2;

	count_boxes = cur_start - prev_start;
	assert(count_boxes == region->data->count_boxes - cur_start);
	if (!count_boxes)
		return cur_start;

	prev_box = REGION_BOX(region, prev_start);
	cur_box = REGION_BOX(region, cur_start);
	if (prev_box->p2.y != cur_box->p1.y) return cur_start;

	y2 = cur_box->p2.y;

	do {
		if ((prev_box->p1.x != cur_box->p1.x)
		    || (prev_box->p2.x != cur_box->p2.x))
			return cur_start;
		prev_box++;
		cur_box++;
		count_boxes--;
	} while (count_boxes);

	count_boxes = cur_start - prev_start;
	region->data->count_boxes -= count_boxes;

	do {
		prev_box--;
		prev_box->p2.y = y2;
		count_boxes--;
	} while (count_boxes);

	return prev_start;
}

#define FIND_BAND(r, r_band_end, r_end, ry1) do { \
	ry1 = r->p1.y; \
	r_band_end = r + 1; \
	while ((r_band_end != r_end) && (r_band_end->p1.y == ry1)) { \
		r_band_end++; \
	} \
} while (0)

#define APPEND_REGIONS(new_reg, r, r_end) do { \
	s32 new_boxes; \
	if ((new_boxes = r_end - r)) { \
		BOXALLOC_BAIL(new_reg, new_boxes, bail); \
		memmove((char *)REGION_BOX_TOP(new_reg), (char *)r, \
			new_boxes * sizeof(struct cb_box)); \
		new_reg->data->count_boxes += new_boxes; \
	} \
} while (0)

static inline s32 region_append_non_o(struct cb_region *region,
				      struct cb_box *r,
				      struct cb_box *r_end,
				      s32 y1, s32 y2)
{
	struct cb_box *next_box;
	s32 new_boxes;

	new_boxes = r_end - r;

	assert(y1 < y2);
	assert(new_boxes != 0);

	/* Make sure we have enough space for all rectangles to be added */
	BOXALLOC(region, new_boxes);
	next_box = REGION_BOX_TOP(region);
	region->data->count_boxes += new_boxes;

	do {
		assert(r->p1.x < r->p2.x);
		ADDBOX(next_box, r->p1.x, y1, r->p2.x, y2);
		r++;
	} while (r != r_end);

	return 0;
}

typedef s32 (*overlap_proc_ptr)(struct cb_region *region,
				struct cb_box *r1,
				struct cb_box *r1_end,
				struct cb_box *r2,
				struct cb_box *r2_end,
				s32 y1,
				s32 y2);

static s32 region_op(struct cb_region *new_reg,
		     struct cb_region *reg1,
		     struct cb_region *reg2,
		     overlap_proc_ptr overlap_func,
		     s32 append_non1,
		     s32 append_non2)
{
	struct cb_box *r1;       /* Pointer into first region     */
	struct cb_box *r2;       /* Pointer into 2d region	     */
	struct cb_box *r1_end;   /* End of 1st region	     */
	struct cb_box *r2_end;   /* End of 2d region		     */
	s32 ybot;                       /* Bottom of intersection	     */
	s32 ytop;                       /* Top of intersection	     */
	struct cb_region_data *old_data; /* Old data for new_reg     */
	s32 prev_band;                  /* Index of start of
				     * previous band in new_reg       */
	s32 cur_band;                   /* Index of start of current
				     * band in new_reg		     */
	struct cb_box * r1_band_end; /* End of current band in r1     */
	struct cb_box * r2_band_end; /* End of current band in r2     */
	s32 top;                        /* Top of non-overlapping band   */
	s32 bot;                        /* Bottom of non-overlapping band*/
	s32 r1y1;                       /* Temps for r1->y1 and r2->y1   */
	s32 r2y1;
	s32 new_size;
	s32 count_boxes;

	/*
	 * Break any region computed from a broken region
	 */
	if (REGION_NAR(reg1) || REGION_NAR(reg2))
		return set_break(new_reg);

	/*
	 * Initialization:
	 * set r1, r2, r1_end and r2_end appropriately, save the rectangles
	 * of the destination region until the end in case it's one of
	 * the two source regions, then mark the "new" region empty, allocating
	 * another array of rectangles for it to use.
	 */

	r1 = REGION_BOXES(reg1);
	new_size = REGION_COUNT_BOXES(reg1);
	r1_end = r1 + new_size;

	count_boxes = REGION_COUNT_BOXES(reg2);
	r2 = REGION_BOXES(reg2);
	r2_end = r2 + count_boxes;
	
	assert(r1 != r1_end);
	assert(r2 != r2_end);

	old_data = (struct cb_region_data *)NULL;

	if (((new_reg == reg1) && (new_size > 1))
	     || ((new_reg == reg2) && (count_boxes > 1))) {
		old_data = new_reg->data;
		new_reg->data = empty_data_ptr;
	}

	/* guess at new size */
	if (count_boxes > new_size)
		new_size = count_boxes;

	new_size <<= 1;

	if (!new_reg->data)
		new_reg->data = empty_data_ptr;
	else if (new_reg->data->size)
		new_reg->data->count_boxes = 0;

	if (new_size > new_reg->data->size) {
		if (box_alloc(new_reg, new_size) < 0) {
			free(old_data);
			return -1;
		}
	}

	/*
	 * Initialize ybot.
	 * In the upcoming loop, ybot and ytop serve different functions 
	 * depending on whether the band being handled is an overlapping
	 * or non-overlapping band. In the case of a non-overlapping
	 * band (only one of the regions has points in the band), ybot is
	 * the bottom of the most recent intersection and thus clips the
	 * top of the rectangles in that band. ytop is the top of the next
	 * intersection between the two regions and serves to clip the
	 * bottom of the rectangles in the current band.
	 * 	For an overlapping band (where the two regions intersect),
	 * 	ytop clips the top of the rectangles of both regions and
	 * 	ybot clips the bottoms.
	 */

	ybot = MIN(r1->p1.y, r2->p1.y);

	/*
	 * prev_band serves to mark the start of the previous band so rectangles
	 * can be coalesced into larger rectangles. qv. pixman_coalesce, above.
	 * In the beginning, there is no previous band, so prev_band == cur_band
	 * (cur_band is set later on, of course, but the first band will always
	 * start at index 0). prev_band and cur_band must be indices because of
	 * the possible expansion, and resultant moving, of the new region's
	 * array of rectangles.
	 */
	prev_band = 0;

	do {
		/*
		 * This algorithm proceeds one source-band (as opposed to a
		 * destination band, which is determined by where the two
		 * regions intersect) at a time. r1_band_end and r2_band_end
		 * serve to mark the rectangle after the last one in the
		 * current band for their respective regions.
		 */
		assert(r1 != r1_end);
		assert(r2 != r2_end);

		FIND_BAND(r1, r1_band_end, r1_end, r1y1);
		FIND_BAND(r2, r2_band_end, r2_end, r2y1);

		/*
		 * First handle the band that doesn't intersect, if any.
		 *
		 * Note that attention is restricted to one band in the
		 * non-intersecting region at once, so if a region has n
		 * bands between the current position and the next place it
		 * overlaps the other, this entire loop will be passed through
		 * n times.
		 */
		if (r1y1 < r2y1) {
			if (append_non1) {
				top = MAX(r1y1, ybot);
				bot = MIN(r1->p2.y, r2y1);
				if (top != bot) {
					cur_band = new_reg->data->count_boxes;
					if (region_append_non_o(new_reg, r1,
					     r1_band_end, top, bot) < 0)
						goto bail;
					COALESCE(new_reg, prev_band, cur_band);
				}
			}
			ytop = r2y1;
		} else if (r2y1 < r1y1) {
			if (append_non2) {
				top = MAX(r2y1, ybot);
				bot = MIN(r2->p2.y, r1y1);
		
				if (top != bot) {
					cur_band = new_reg->data->count_boxes;

					if (region_append_non_o(new_reg, r2,
					     r2_band_end, top, bot) < 0)
						goto bail;
					COALESCE(new_reg, prev_band, cur_band);
				}
			}
			ytop = r1y1;
		} else {
			ytop = r1y1;
		}

		/*
		 * Now see if we've hit an intersecting band. The two bands only
		 * intersect if ybot > ytop
		 */
		ybot = MIN(r1->p2.y, r2->p2.y);
		if (ybot > ytop) {
			cur_band = new_reg->data->count_boxes;

			if ((*overlap_func)(new_reg, r1, r1_band_end,
					r2, r2_band_end, ytop, ybot) < 0) {
				goto bail;
			}
	    
			COALESCE(new_reg, prev_band, cur_band);
		}

		/*
		 * If we've finished with a band (y2 == ybot) we skip forward
		 * in the region to the next band.
		 */
		if (r1->p2.y == ybot)
			r1 = r1_band_end;

		if (r2->p2.y == ybot)
			r2 = r2_band_end;
	} while (r1 != r1_end && r2 != r2_end);

	/*
	 * Deal with whichever region (if any) still has rectangles left.
	 *
	 * We only need to worry about banding and coalescing for the very first
	 * band left.  After that, we can just group all remaining boxes,
	 * regardless of how many bands, into one final append to the list.
	 */

	if ((r1 != r1_end) && append_non1) {
		/* Do first non_overlap1Func call, which may be able to
		 * coalesce */
		FIND_BAND(r1, r1_band_end, r1_end, r1y1);
	
		cur_band = new_reg->data->count_boxes;
	
		if (region_append_non_o(new_reg, r1, r1_band_end,
				MAX(r1y1, ybot), r1->p2.y) < 0) {
			goto bail;
		}
	
		COALESCE(new_reg, prev_band, cur_band);

		/* Just append the rest of the boxes  */
		APPEND_REGIONS(new_reg, r1_band_end, r1_end);
	} else if ((r2 != r2_end) && append_non2) {
		/* Do first non_overlap2Func call, which may be able to
		 * coalesce */
		FIND_BAND(r2, r2_band_end, r2_end, r2y1);

		cur_band = new_reg->data->count_boxes;

		if (region_append_non_o(new_reg, r2, r2_band_end,
				MAX(r2y1, ybot), r2->p2.y) < 0) {
			goto bail;
		}

		COALESCE(new_reg, prev_band, cur_band);

		/* Append rest of boxes */
		APPEND_REGIONS(new_reg, r2_band_end, r2_end);
	}

	free(old_data);

	if (!(count_boxes = new_reg->data->count_boxes)) {
		FREE_DATA(new_reg);
		new_reg->data = empty_data_ptr;
	} else if (count_boxes == 1) {
		new_reg->extents = *(REGION_BOX_PTR(new_reg));
		FREE_DATA(new_reg);
		new_reg->data = (struct cb_region_data *)NULL;
	} else {
		DOWNSIZE(new_reg, count_boxes);
	}

	return 0;

bail:
	free(old_data);

	return set_break(new_reg);
}

/* sort boxes array into ascending (y1, x1) order */
static void quick_sort_boxes(struct cb_box boxes[], s32 count_boxes)
{
	s32 y1, x1, i, j;
	struct cb_box *r;

	/* always called with count_boxes > 1 */
	do {
		/* the condition of recursive calling's termination */
		if (count_boxes == 2) {
			if (boxes[0].p1.y > boxes[1].p1.y
				|| (boxes[0].p1.y == boxes[1].p1.y
					&& boxes[0].p1.x > boxes[1].p1.x)) {
				EXCHANGE_BOXES(0, 1, boxes);
			}
			return;
		}

		/* place the middle element to the location 0 */
		EXCHANGE_BOXES(0, count_boxes >> 1, boxes);
		x1 = boxes[0].p1.x;
		y1 = boxes[0].p1.y;

		i = 0;
		j = count_boxes;

		do {
			r = &(boxes[i]);
			do {
				r++;
				i++;
			} while (i != count_boxes
				&& (r->p1.y < y1
					|| (r->p1.y == y1
						&& r->p1.x < x1)));

			r = &(boxes[j]);
			do {
				r--;
				j--;
			} while (y1 < r->p1.y
				|| (y1 == r->p1.y && x1 < r->p1.x));

			if (i < j)
				EXCHANGE_BOXES(i, j, boxes);
		} while (i < j);
		EXCHANGE_BOXES(0, j, boxes);

		if (count_boxes - j - 1 > 1)
			quick_sort_boxes(&boxes[j + 1], count_boxes - j - 1);

		count_boxes = j;
	} while (count_boxes > 1);
}

static s32 region_subtract_o(struct cb_region *region,
			     struct cb_box *r1,
			     struct cb_box *r1_end,
			     struct cb_box *r2,
			     struct cb_box *r2_end,
			     s32 y1,
			     s32 y2)
{
	struct cb_box *next_rect;
	s32 x1;

	x1 = r1->p1.x;

	assert(y1 < y2);
	assert(r1 != r1_end && r2 != r2_end);

	next_rect = REGION_BOX_TOP(region);

	do {
		if (r2->p2.x <= x1) {
			/*
			 * Subtrahend entirely to left of minuend: go to next
			 * subtrahend.
			 */
			r2++;
		} else if (r2->p1.x <= x1) {
			/*
			 * Subtrahend precedes minuend: nuke left edge of 
			 * minuend.
			 */
			x1 = r2->p2.x;
			if (x1 >= r1->p2.x) {
				/*
				 * Minuend completely covered: advance to next
				 * minuend and reset left fence to edge of new
				 * minuend.
				 */
				r1++;
				if (r1 != r1_end)
					x1 = r1->p1.x;
			} else {
				/*
				 * Subtrahend now used up since it doesn't
				 * extend beyond minuend
				 */
				r2++;
			}
		} else if (r2->p1.x < r1->p2.x) {
			/*
			 * Left part of subtrahend covers part of minuend: add
			 * uncovered part of minuend to region and skip to
			 * next subtrahend.
			 */
			assert(x1 < r2->p1.x);
			NEWBOX(region, next_rect, x1, y1, r2->p1.x, y2);

			x1 = r2->p2.x;
			if (x1 >= r1->p2.x) {
				/*
				 * Minuend used up: advance to new...
				 */
				r1++;
				if (r1 != r1_end)
					x1 = r1->p1.x;
			} else {
				/*
				 * Subtrahend used up
				 */
				r2++;
			}
		} else {
			/*
			 * Minuend used up: add any remaining piece before
			 * advancing.
			 */
			if (r1->p2.x > x1)
				NEWBOX(region, next_rect, x1, y1, r1->p2.x, y2);

			r1++;

			if (r1 != r1_end)
				x1 = r1->p1.x;
		}
	} while ((r1 != r1_end) && (r2 != r2_end));

	/*
	 * Add remaining minuend rectangles to region.
	 */
	while (r1 != r1_end) {
		assert(x1 < r1->p2.x);

		NEWBOX(region, next_rect, x1, y1, r1->p2.x, y2);

		r1++;
		if (r1 != r1_end)
			x1 = r1->p1.x;
	}
	return 0;
}

static s32 region_intersect_o(struct cb_region *region,
			      struct cb_box *r1,
			      struct cb_box *r1_end,
			      struct cb_box *r2,
			      struct cb_box *r2_end,
			      s32 y1,
			      s32 y2)
{
	s32 x1;
	s32 x2;
	struct cb_box *next_rect;

	next_rect = REGION_BOX_TOP(region);

	assert(y1 < y2);
	assert(r1 != r1_end && r2 != r2_end);

	do {
		x1 = MAX(r1->p1.x, r2->p1.x);
		x2 = MIN(r1->p2.x, r2->p2.x);

		/*
		 * If there's any overlap between the two rectangles, add that
		 * overlap to the new region.
		 */
		if (x1 < x2)
			NEWBOX(region, next_rect, x1, y1, x2, y2);

		/*
		 * Advance the pointer(s) with the leftmost right side, since
		 * the next rectangle on that list may still overlap the other
		 * region's current rectangle.
		 */
		if (r1->p2.x == x2) {
			r1++;
		}
		if (r2->p2.x == x2) {
			r2++;
		}
	} while ((r1 != r1_end) && (r2 != r2_end));

	return 0;
}

static s32 region_union_o(struct cb_region *region,
			  struct cb_box *   r1,
			  struct cb_box *   r1_end,
			  struct cb_box *   r2,
			  struct cb_box *   r2_end,
			  s32            y1,
			  s32            y2)
{
	struct cb_box *next_rect;
	s32 x1;			/* left and right side of current union */
	s32 x2;

	assert(y1 < y2);
	assert(r1 != r1_end && r2 != r2_end);

	next_rect = REGION_BOX_TOP(region);

	/* Start off current rectangle */
	if (r1->p1.x < r2->p1.x) {
		x1 = r1->p1.x;
		x2 = r1->p2.x;
		r1++;
	} else {
		x1 = r2->p1.x;
		x2 = r2->p2.x;
		r2++;
	}

	while (r1 != r1_end && r2 != r2_end) {
		if (r1->p1.x < r2->p1.x)
			MERGEBOX(r1);
		else
			MERGEBOX(r2);
	}

	/* Finish off whoever (if any) is left */
	if (r1 != r1_end) {
		do {
			MERGEBOX(r1);
		} while (r1 != r1_end);
	} else if (r2 != r2_end) {
		do {
			MERGEBOX(r2);
		} while (r2 != r2_end);
	}

	/* Add current rectangle */
	NEWBOX(region, next_rect, x1, y1, x2, y2);

	return 0;
}

static s32 validate(struct cb_region *badreg)
{
	/* Descriptor for regions under construction  in Step 2. */
	typedef struct {
		struct cb_region reg;
		s32 prev_band;
		s32 cur_band;
	} region_info_t;

	region_info_t stack_regions[64];

	s32 count_boxes;                /* Original numRects for badreg	    */
	region_info_t *ri;              /* Array of current regions	    */
	s32 num_ri;                     /* Number of entries used in ri	    */
	s32 size_ri;                    /* Number of entries available in ri*/
	s32 i;                          /* Index into rects		    */
	s32 j;                          /* Index into ri		    */
	region_info_t *rit;             /* &ri[j]			    */
	struct cb_region *reg;             /* ri[j].reg		    */
	struct cb_box *box;                /* Current box in rects	    */
	struct cb_box *ri_box;             /* Last box in ri[j].reg	    */
	struct cb_region *hreg;            /* ri[j_half].reg	    */
	s32 ret = 0;

	if (!badreg->data)
		return 0;

	count_boxes = badreg->data->count_boxes;
	if (!count_boxes) {
		if (REGION_NAR(badreg))
			return -1;
		return 0;
	}
	
	if (badreg->extents.p1.x < badreg->extents.p2.x) {
		if ((count_boxes) == 1) {
			FREE_DATA(badreg);
			badreg->data = (struct cb_region_data *)NULL;
		} else {
			DOWNSIZE(badreg, count_boxes);
		}
		return 0;
	}

	/* Step 1: Sort the rects array into ascending (y1, x1) order */
	quick_sort_boxes(REGION_BOX_PTR(badreg), count_boxes);

	/* Step 2: Scatter the sorted array into the minimum number of regions*/

	/* Set up the first region to be the first rectangle in badreg */
	/* Note that step 2 code will never overflow the ri[0].reg rects array*/
	ri = stack_regions;
	size_ri = sizeof(stack_regions) / sizeof(stack_regions[0]);
	num_ri = 1;
	ri[0].prev_band = 0;
	ri[0].cur_band = 0;
	ri[0].reg = *badreg;
	box = REGION_BOX_PTR(&ri[0].reg);
	ri[0].reg.extents = *box;
	ri[0].reg.data->count_boxes = 1;
	badreg->extents = *empty_box_ptr;
	badreg->data = empty_data_ptr;

	/* Now scatter rectangles into the minimum set of valid regions.  If the
	 * next rectangle to be added to a region would force an existing
	 * rectangle in the region to be split up in order to maintain y-x
	 * banding, just forget it.  Try the next region.  If it doesn't fit
	 * cleanly into any region, make a new one.
	 */

	for (i = count_boxes; --i > 0;) {
		box++;
		/* Look for a region to append box to */
		for (j = num_ri, rit = ri; --j >= 0; rit++) {
			reg = &rit->reg;
			ri_box = REGION_BOX_END (reg);

			if (box->p1.y == ri_box->p1.y
			     && box->p2.y == ri_box->p2.y) {
				/* box is in same band as ri_box. Merge or
				 * append it */
				if (box->p1.x <= ri_box->p2.x) {
					/* Merge it with ri_box */
					if (box->p2.x > ri_box->p2.x)
						ri_box->p2.x = box->p2.x;
				} else {
					BOXALLOC_BAIL(reg, 1, bail);
					*REGION_BOX_TOP(reg) = *box;
					reg->data->count_boxes++;
				}
		
				goto next_rect;   /* So sue me */
			} else if (box->p1.y >= ri_box->p2.y) {
				/* Put box into new band */
				if (reg->extents.p2.x < ri_box->p2.x)
					reg->extents.p2.x = ri_box->p2.x;
		
				if (reg->extents.p1.x > box->p1.x)
					reg->extents.p1.x = box->p1.x;
		
				COALESCE(reg, rit->prev_band, rit->cur_band);
				rit->cur_band = reg->data->count_boxes;
				BOXALLOC_BAIL(reg, 1, bail);
				*REGION_BOX_TOP(reg) = *box;
				reg->data->count_boxes++;

				goto next_rect;
			}
			/* Well, this region was inappropriate.
			 * Try the next one. */
		} /* for j */

		/* Uh-oh.  No regions were appropriate.  Create a new one. */
		if (size_ri == num_ri) {
			u32 data_size;

			/* Oops, allocate space for new region information */
			size_ri <<= 1;

			data_size = size_ri * sizeof(region_info_t);
			if (data_size / size_ri != sizeof(region_info_t))
				goto bail;

			if (ri == stack_regions) {
				rit = malloc(data_size);
				if (!rit)
					goto bail;
				memcpy(rit, ri, num_ri * sizeof(region_info_t));
			} else {
				rit = (region_info_t *)realloc(ri, data_size);
				if (!rit)
					goto bail;
			}
			ri = rit;
			rit = &ri[num_ri];
		}
		num_ri++;
		rit->prev_band = 0;
		rit->cur_band = 0;
		rit->reg.extents = *box;
		rit->reg.data = (struct cb_region_data *)NULL;

		/* MUST force allocation */
		if (box_alloc(&rit->reg, (i + num_ri) / num_ri) < 0)
			goto bail;
next_rect:
		;
	} /* for i */

	/* Make a final pass over each region in order to COALESCE and set
	 * extents.x2 and extents.y2
	 */
	for (j = num_ri, rit = ri; --j >= 0; rit++) {
		reg = &rit->reg;
		ri_box = REGION_BOX_END(reg);
		reg->extents.p2.y = ri_box->p2.y;

		if (reg->extents.p2.x < ri_box->p2.x)
			reg->extents.p2.x = ri_box->p2.x;
	
		COALESCE(reg, rit->prev_band, rit->cur_band);

		if (reg->data->count_boxes == 1) { /* keep unions happy below */
			FREE_DATA(reg);
			reg->data = (struct cb_region_data *)NULL;
		}
	}

	/* Step 3: Union all regions into a single region */
	while (num_ri > 1) {
		s32 half = num_ri / 2;
		for (j = num_ri & 1; j < (half + (num_ri & 1)); j++) {
			reg = &ri[j].reg;
			hreg = &ri[j + half].reg;

			if (region_op(reg, reg, hreg, region_union_o, 1, 1) < 0)
				ret = -1;

			if (hreg->extents.p1.x < reg->extents.p1.x)
				reg->extents.p1.x = hreg->extents.p1.x;

			if (hreg->extents.p1.y < reg->extents.p1.y)
				reg->extents.p1.y = hreg->extents.p1.y;

			if (hreg->extents.p2.x > reg->extents.p2.x)
				reg->extents.p2.x = hreg->extents.p2.x;

			if (hreg->extents.p2.y > reg->extents.p2.y)
				reg->extents.p2.y = hreg->extents.p2.y;

			FREE_DATA(hreg);
		}

		num_ri -= half;

		if (ret < 0)
			goto bail;
	}

	*badreg = ri[0].reg;

	if (ri != stack_regions)
		free(ri);

	return ret;

bail:
	for (i = 0; i < num_ri; i++)
		FREE_DATA(&ri[i].reg);

	if (ri != stack_regions)
		free(ri);

	return set_break(badreg);
}

s32 cb_region_init_boxes(struct cb_region *region,
			 const struct cb_box *boxes, s32 count)
{
	struct cb_box *inner_boxes, *box;
	s32 skip, i;

	if (count == 1) {
		cb_region_init_rect(region, boxes[0].p1.x, boxes[0].p1.y,
					boxes[0].p2.x - boxes[0].p1.x,
					boxes[0].p2.y - boxes[0].p1.y);
		return 0;
	}

	cb_region_init(region);

	if (count == 0)
		return 0;

	if (box_alloc(region, count) < 0)
		return -1;

	inner_boxes = REGION_BOXES(region);

	memcpy(inner_boxes, boxes, sizeof(struct cb_box) * count);
	region->data->count_boxes = count;

	skip = 0;

	for (i = 0; i < count; i++) {
		box = &inner_boxes[i];
		if (box->p1.x >= box->p2.x || box->p1.y >= box->p2.y)
			skip++;
		else if (skip)
			inner_boxes[i - skip] = inner_boxes[i];
	}

	region->data->count_boxes -= skip;

	if (region->data->count_boxes == 0) {
		FREE_DATA(region);
		cb_region_init(region);
		return 0;
	}

	if (region->data->count_boxes == 1) {
		region->extents = inner_boxes[0];
		FREE_DATA(region);
		region->data = NULL;
		return 0;
	}

	region->extents.p1.x = region->extents.p2.x = 0;

	return validate(region);
}

void cb_region_init_with_extents(struct cb_region *region,
				 struct cb_box *extents)
{
	if (!GOOD_BOX(extents)) {
		fprintf(stderr, "Invalid box, %d,%d %d,%d\n", extents->p1.x,
			extents->p1.y, extents->p2.x, extents->p2.y);
		cb_region_init(region);
		return;
	}
	region->extents = *extents;
	region->data = NULL;
}

void cb_region_fini(struct cb_region *region)
{
	FREE_DATA(region);
}

static void set_extents(struct cb_region *region)
{
	struct cb_box *box, *box_end;

	if (!region->data)
		return;

	if (!region->data->size) {
		region->extents.p2.x = region->extents.p1.x;
		region->extents.p2.y = region->extents.p1.y;
		return;
	}

	box = REGION_BOX_PTR(region);
	box_end = REGION_BOX_END(region);

	region->extents.p1.x = box->p1.x;
	region->extents.p1.y = box->p1.y;
	region->extents.p2.x = box_end->p2.x;
	region->extents.p2.y = box_end->p2.y;

	assert(region->extents.p1.y < region->extents.p2.y);
	
	while (box <= box_end) {
		if (box->p1.x < region->extents.p1.x)
			region->extents.p1.x = box->p1.x;
		if (box->p2.x > region->extents.p2.x)
			region->extents.p2.x = box->p2.x;
		box++;
	}

	assert(region->extents.p1.x < region->extents.p2.x);
}

void cb_region_translate(struct cb_region *region, s32 x, s32 y)
{
	u32 count_boxes;
	struct cb_box *box;

	region->extents.p1.x = region->extents.p1.x + x;
	region->extents.p1.y = region->extents.p1.y + y;
	region->extents.p2.x = region->extents.p2.x + x;
	region->extents.p2.y = region->extents.p2.y + y;

	if (region->data && (count_boxes = region->data->count_boxes)) {
		for (box = REGION_BOX_PTR(region); count_boxes--;
				box++) {
			box->p1.x += x;
			box->p1.y += y;
			box->p2.x += x;
			box->p2.y += y;
		}
	}
}

s32 cb_region_copy(struct cb_region *dst, struct cb_region *src)
{
	if (dst == src)
		return 0;
	
	dst->extents = src->extents;

	if (!src->data || !src->data->size) {
		FREE_DATA(dst);
		dst->data = src->data;
		return 0;
	}
	
	if (!dst->data || (dst->data->size < src->data->count_boxes)) {
		FREE_DATA(dst);

		dst->data = alloc_data(src->data->count_boxes);

		if (!dst->data)
			return set_break(dst);

		dst->data->size = src->data->count_boxes;
	}

	dst->data->count_boxes = src->data->count_boxes;

	memmove((char *)REGION_BOX_PTR(dst), (char *)REGION_BOX_PTR(src),
		 dst->data->count_boxes * sizeof(struct cb_box));

	return 0;
}

s32 cb_region_intersect(struct cb_region *new_reg,
			struct cb_region *reg1,
			struct cb_region *reg2)
{
	/* check for trivial reject */
	if (REGION_NIL(reg1) || REGION_NIL (reg2)
		|| !EXTENTCHECK (&reg1->extents, &reg2->extents)) {
		/* Covers about 20% of all cases */
		FREE_DATA(new_reg);
		new_reg->extents.p2.x = new_reg->extents.p1.x;
		new_reg->extents.p2.y = new_reg->extents.p1.y;
		if (REGION_NAR(reg1) || REGION_NAR(reg2)) {
			new_reg->data = broken_data_ptr;
			return -1;
		} else {
			new_reg->data = empty_data_ptr;
		}
	} else if (!reg1->data && !reg2->data) {
		/* Covers about 80% of cases that aren't trivially rejected */
		new_reg->extents.p1.x = MAX(reg1->extents.p1.x,
						reg2->extents.p1.x);
		new_reg->extents.p1.y = MAX(reg1->extents.p1.y,
						reg2->extents.p1.y);
		new_reg->extents.p2.x = MIN(reg1->extents.p2.x,
						reg2->extents.p2.x);
		new_reg->extents.p2.y = MIN(reg1->extents.p2.y,
						reg2->extents.p2.y);

		FREE_DATA(new_reg);

		new_reg->data = (struct cb_region_data *)NULL;
	} else if (!reg2->data && SUBSUMES(&reg2->extents, &reg1->extents)) {
		return cb_region_copy(new_reg, reg1);
	} else if (!reg1->data && SUBSUMES(&reg1->extents, &reg2->extents)) {
		return cb_region_copy(new_reg, reg2);
	} else if (reg1 == reg2) {
		return cb_region_copy(new_reg, reg1);
	} else {
		/* General purpose intersection */

		if (region_op(new_reg, reg1, reg2, region_intersect_o, 0,0) < 0)
			return -1;
	
		set_extents(new_reg);
	}

	return 0;
}

s32 cb_region_intersect_rect(struct cb_region *dst,
			     struct cb_region *src,
			     s32 x, s32 y, u32 w, u32 h)
{
	struct cb_region region;

	region.data = NULL;
	region.extents.p1.x = x;
	region.extents.p1.y = y;
	region.extents.p2.x = x + w;
	region.extents.p2.y = y + h;

	return cb_region_intersect(dst, src, &region);
}

s32 cb_region_union(struct cb_region *new_reg,
		    struct cb_region *reg1,
		    struct cb_region *reg2)
{
	/*  checks all the simple cases */

	/*
	 * Region 1 and 2 are the same
	 */
	if (reg1 == reg2)
		return cb_region_copy(new_reg, reg1);

	/*
	 * Region 1 is empty
	 */
	if (REGION_NIL(reg1)) {
		if (REGION_NAR(reg1))
			return set_break(new_reg);

		if (new_reg != reg2)
			return cb_region_copy(new_reg, reg2);

		return 0;
	}

	/*
	 * Region 2 is empty
	 */
	if (REGION_NIL(reg2)) {
		if (REGION_NAR(reg2))
			return set_break(new_reg);

		if (new_reg != reg1)
			return cb_region_copy(new_reg, reg1);

		return 0;
	}

	/*
	 * Region 1 completely subsumes region 2
	 */
	if (!reg1->data && SUBSUMES(&reg1->extents, &reg2->extents)) {
		if (new_reg != reg1)
		return cb_region_copy(new_reg, reg1);

		return 0;
	}

	/*
	 * Region 2 completely subsumes region 1
	 */
	if (!reg2->data && SUBSUMES(&reg2->extents, &reg1->extents)) {
		if (new_reg != reg2)
		return cb_region_copy(new_reg, reg2);

		return 0;
	}

	if (region_op(new_reg, reg1, reg2, region_union_o, 1, 1) < 0)
		return -1;

	new_reg->extents.p1.x = MIN(reg1->extents.p1.x, reg2->extents.p1.x);
	new_reg->extents.p1.y = MIN(reg1->extents.p1.y, reg2->extents.p1.y);
	new_reg->extents.p2.x = MAX(reg1->extents.p2.x, reg2->extents.p2.x);
	new_reg->extents.p2.y = MAX(reg1->extents.p2.y, reg2->extents.p2.y);
	
	return 0;
}

s32 cb_region_union_rect(struct cb_region *dst,
			 struct cb_region *src,
			 s32 x, s32 y, u32 w, u32 h)
{
	struct cb_region region;

	region.extents.p1.x = x;
	region.extents.p1.y = y;
	region.extents.p2.x = x + w;
	region.extents.p2.y = y + h;

	if (!GOOD_BOX(&region.extents)) {
		if (BAD_BOX(&region.extents))
			fprintf(stderr, "Invalid rectangle passed\n");
		return cb_region_copy(dst, src);
	}

	region.data = NULL;

	return cb_region_union(dst, src, &region);
}

s32 cb_region_subtract(struct cb_region *reg_d, struct cb_region *reg_m,
		       struct cb_region *reg_s)
{
	/* check for trivial rejects */
	if (REGION_NIL(reg_m) || REGION_NIL(reg_s)
	     || !EXTENTCHECK(&reg_m->extents, &reg_s->extents)) {
		if (REGION_NAR(reg_s))
			return set_break(reg_d);
	
		return cb_region_copy(reg_d, reg_m);
	} else if (reg_m == reg_s) {
		FREE_DATA(reg_d);
		reg_d->extents.p2.x = reg_d->extents.p1.x;
		reg_d->extents.p2.y = reg_d->extents.p1.y;
		reg_d->data = empty_data_ptr;

		return 0;
	}

	/* Add those rectangles in region 1 that aren't in region 2,
	   do yucky subtraction for overlaps, and
	   just throw away rectangles in region 2 that aren't in region 1 */
	if (region_op(reg_d, reg_m, reg_s, region_subtract_o, 1, 0) < 0)
		return -1;

	/*
	 * Can't alter reg_d's extents before we call pixman_op because
	 * it might be one of the source regions and pixman_op depends
	 * on the extents of those regions being unaltered. Besides, this
	 * way there's no checking against rectangles that will be nuked
	 * due to coalescing, so we have to examine fewer rectangles.
	 */
	set_extents(reg_d);
	return 0;
}

s32 cb_region_count_boxes(struct cb_region *region)
{
	return REGION_COUNT_BOXES(region);
}

struct cb_box * cb_region_extents(struct cb_region *region)
{
	return &region->extents;
}

s32 cb_region_is_not_empty(struct cb_region *region)
{
	return (!REGION_NIL(region));
}

struct cb_box * cb_region_boxes(struct cb_region *region, s32 *count_boxes)
{
	if (count_boxes)
		*count_boxes = REGION_COUNT_BOXES(region);

	return REGION_BOXES(region);
}

void cb_region_clear(struct cb_region *region)
{
	FREE_DATA(region);

	region->extents = *empty_box_ptr;
	region->data = empty_data_ptr;
}

