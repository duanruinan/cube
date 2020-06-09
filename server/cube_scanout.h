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
#ifndef CUBE_SCANOUT_H
#define CUBE_SCANOUT_H

#include <stdint.h>
#include <stdbool.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_log.h>
#include <cube_compositor.h>
#include <cube_signal.h>

/* public interfaces */

struct output;
struct head;
struct plane;
struct scanout;

struct cb_mode {
	u32 width;
	u32 height;
	u32 vrefresh;
	u32 pixel_freq;
	bool interlace;
	bool preferred;
};

struct fb_commit_info {
	struct cb_buffer *buffer;
	struct output *output;
	struct plane *plane;
	s32 zpos;
	struct list_head link;
};

struct scanout_commit_info {
	struct list_head fb_commits;
};

/* a output represent a LCDC/CRTC */
struct output {
	/* the sink of this output */
	struct head *head;

	/* enumerate mode list */
	struct cb_mode *(*enumerate_mode)(struct output *o,
					  struct cb_mode *last);

	/* enumerate plane list, so we can then get plane info. */
	struct plane *(*enumerate_plane)(struct output *o, struct plane *last);

	/* switch video mode timing */
	s32 (*switch_mode)(struct output *o, struct cb_mode *mode);

	/* enable video output */
	s32 (*enable)(struct output *o, struct cb_mode *mode,
		      struct cb_rect *rc);

	/* disable video output, may be because the monitor is unpluged. */
	void (*disable)(struct output *o);

	/* add callback function to get notification of the buffer
	 * complete event.
	 */
	s32 (*add_bo_complete_notify)(struct output *o, struct cb_listener *l);
};

/* connector for monitor. e.g. HDMI/DP */
struct head {
	/* monitor connected or not */
	bool connected;

	/* the source of this head */
	struct output *output;

	/* retrieve video timing list from monitor's EDID */
	void (*retrieve_mode)(struct head *h);

	/* add callback function to get notification of the monitor changed
	 * event.
	 */
	s32 (*add_head_change_notify)(struct output *o, struct cb_listener *l);
};

enum plane_type {
	PLANE_TYPE_UNKNOWN = 0,
	PLANE_TYPE_PRIMARY,
	PLANE_TYPE_CURSOR,
	PLANE_TYPE_OVERLAY,
};

struct plane {
	/* plane type: primary / cursor / overlay */
	enum plane_type type;

	/* input pixel format supported */
	s32 count_fmts;
	enum cb_pix_fmt *fmts;
};

struct scanout {
	/* destroy all */
	void (*destroy)(struct scanout *so);

	/* create pipeline according to the given configuration,
	 * so output, head and planes will be created.
	 */
	struct output *(*pipeline_create)(struct scanout *so,
					  struct pipeline *pipeline_cfg);

	/* destroy pipeline */
	void (*pipeline_destroy)(struct scanout *so, struct output *o);

	/* import buffer from external buffer information */
	struct cb_buffer (*import_buffer)(struct scanout *so,
					  struct cb_buffer_info *info);

	/* commit user settings to scanout */
	s32 (*commit)(struct scanout *so, struct scanout_commit_info *info);

	/* debug set */
	void (*set_dbg_level)(struct scanout *so, enum cb_log_level level);
};

struct scanout *scanout_create(const char *dev_path, struct cb_event_loop *l);

#endif

