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
#ifndef CUBE_RENDERER_H
#define CUBE_RENDERER_H

#include <stdint.h>
#include <stdbool.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_log.h>
#include <cube_compositor.h>
#include <cube_signal.h>

struct renderer;

struct r_output {
	void (*destroy)(struct r_output *o);

	bool (*repaint)(struct r_output *o, struct list_head *views);

	void (*layout_changed)(struct r_output *o,
			       struct cb_rect *render_area,
			       u32 disp_w, u32 disp_h);
};

struct renderer {
	void (*destroy)(struct renderer *r);

	struct r_output *(*output_create)(struct renderer *r,
					 void *window_for_legacy,
					 void *window,
					 s32 *formats,
					 s32 count_fmts,
					 s32 *vid,
					 struct cb_rect *render_area,
					 u32 disp_w, u32 disp_h,
					 s32 pipe);

	struct cb_buffer *(*import_dmabuf)(struct renderer *r,
					   struct cb_buffer_info *info);

	void (*release_dmabuf)(struct renderer *r,
			       struct cb_buffer *buffer);

	/* change the surface's current buffer */
	void (*attach_buffer)(struct renderer *r,
			      struct cb_surface *surface,
			      struct cb_buffer *buffer);

	/* it is used for shm buffer's partial update */
	void (*flush_damage)(struct renderer *r, struct cb_surface *surface);

	/* set debug level */
	void (*set_dbg_level)(struct renderer *r, enum cb_log_level level);
};

struct renderer *renderer_create(struct compositor *c,
				 u32 *formats, s32 count_fmts,
				 bool no_winsys, void *native_window, s32 *vid);

#endif

