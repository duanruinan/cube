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
#ifndef CUBE_COMPOSITOR_H
#define CUBE_COMPOSITOR_H

#include <stdbool.h>
#include <cube_utils.h>
#include <cube_signal.h>
#include <cube_region.h>
#include <cube_protocal.h>
#include <cube_client_agent.h>

struct cb_buffer {
	struct cb_buffer_info info;
	struct cb_signal destroy_signal;
	/* scanout backend emit flip signal when the buffer is commited into
	 * all outputs which it should be displayed on */
	struct cb_signal flip_signal;
	/* scanout backend emit complete signal when display do not use buffer
	 * any more */
	struct cb_signal complete_signal;
	/* userdata is set by compositor, and it is used in listener. */
	void *userdata;
	/*
	 * 'dirty' is the outputs' bitmap. it indicates which output the buffer
	 * should be displayed on.
	 * Each bit of it is cleared when corresponding output is flipped.
	 * Each bit is set when the buffer's content is changed. the compositor
	 * should decide which output it should be displayed on at this time.
	 */
	u32 dirty;
};

struct shm_buffer {
	struct cb_buffer base;
	struct cb_shm shm;
	char name[128];
};

struct pipeline {
	s32 head_index;
	s32 output_index;
	s32 primary_plane_index;
	s32 cursor_plane_index;
};

/* video timing filter pattern */
enum cb_mode_filter_mode {
	CB_MODE_FILTER_MODE_SIZE_OR_CLOCK = 0,
	CB_MODE_FILTER_MODE_SIZE_AND_CLOCK,
};

struct cb_mode_filter {
	enum cb_mode_filter_mode mode;
	u32 min_width, max_width;
	u32 min_height, max_height;
	u32 min_clock, max_clock;
};

struct compositor;

struct cb_surface {
	struct compositor *c;
	void *renderer_state; /* surface state */
	bool is_opaque;
	struct cb_signal destroy_signal;
	struct cb_view *view;
	struct cb_region damage; /* used for texture upload */
	struct cb_region opaque; /* opaque area */
	u32 width, height; /* surface size */
	/* output bitmap */
	/* primary output index */
	/* client agent */
};

struct cb_view {
	struct cb_surface *surface;
	struct list_head link; /* link to compositor's views */
	struct cb_rect area; /* desktop coordinates */
	s32 zpos; /* zpos */
	struct plane *plane; /* plane assigned by compositor */
	float alpha;
	bool dirty; /* need repaint or not */
	struct cb_buffer *buf_cur;
	/* output bitmap */
};

struct compositor {
	/*
	 * return 0 on success, -EAGAIN means to call the destroyer later.
	 */
	s32 (*destroy)(struct compositor *c);

	/*
	 * return 0 on success, -EAGAIN means to call the destroyer later.
	 */
	s32 (*suspend)(struct compositor *c);

	/* re-enable all */
	void (*resume)(struct compositor *c);

	/*
	 * register a callback to notify the server about the ready event of
	 * the compositor
	 */
	s32 (*register_ready_cb)(struct compositor *c,
				 struct cb_listener *l);
	/* return the current status */
	bool (*head_connected)(struct compositor *c, s32 pipe);

	/* register head changed cb */
	s32 (*register_head_status_cb)(struct compositor *c,
				       s32 pipe,
				       struct cb_listener *l);

	/*
	 * Retrieve EDID blob from Monitor by IIC DDC channel or other virtual
	 * DDC channel (e.g. DP AUX).
	 */
	s32 (*retrieve_edid)(struct compositor *c, s32 pipe,
			     u8 *data, size_t *length);

	/* get connector's name */
	const char *(*get_connector_name)(struct compositor *c, s32 pipe);

	/* get monitor's name */
	const char *(*get_monitor_name)(struct compositor *c, s32 pipe);

	/* get custom video timing if exist */
	struct cb_mode *(*get_custom_timing)(struct compositor *c, s32 pipe);

	/* get current video timing if it is set */
	struct cb_mode *(*get_current_timing)(struct compositor *c, s32 pipe);

	/*
	 * Enumerate video timing supported by monitor.
	 * If the filter is given, the mode which is filtered will not be
	 * enumerated.
	 */
	struct cb_mode *(*enumerate_timing)(struct compositor *c, s32 pipe,
					    struct cb_mode *last_mode,
					    struct cb_mode_filter *filter);

	/* create custom video timing */
	struct cb_mode *(*create_custom_timing)(struct compositor *c,
						s32 pipe,
						u32 clock,
						u16 width,
						u16 hsync_start,
						u16 hsync_end,
						u16 htotal,
						u16 hskew,
						u16 height,
						u16 vsync_start,
						u16 vsync_end,
						u16 vtotal,
						u16 vscan,
						u32 vrefresh,
						bool interlaced,
						bool pos_hsync,
						bool pos_vsync,
						char *mode_name);

	/*
	 * Switch video timing
	 * It is a asynchronous progress.
	 * 0 on success, -EAGAIN means to call switch_timing again.
	 */
	s32 (*switch_timing)(struct compositor *c,
			     s32 pipe,
			     struct cb_mode *mode);

	/*
	 * Set desktop canvas layout.
	 * 	canvas: an array of cb_rect, each element represent a output's
	 * 	        size and position in global canvas.
	 * 	        The order or the element is the pipe number from 0 - N
	 */
	void (*set_desktop_layout)(struct compositor *c,
				   struct cb_rect *canvas);

	/* hide mouse cursor */
	s32 (*hide_mouse_cursor)(struct compositor *c);

	/* show cursor */
	s32 (*show_mouse_cursor)(struct compositor *c);

	/* set mouse cursor shape, only ARGB is supported */
	s32 (*set_mouse_cursor)(struct compositor *c,
				u8 *data, u32 width, u32 height,
				u32 stride,
				s32 hot_x, s32 hot_y,
				bool alpha_blended);

	/* set mouse cursor update complete notify */
	bool (*set_mouse_updated_notify)(struct compositor *c,
					 struct cb_listener *mc_updated_l);
};

/* compositor creator */
struct compositor *compositor_create(char *device_name,
				     struct cb_event_loop *loop,
				     struct pipeline *pipecfgs,
				     s32 count_outputs);

#endif

