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
	bool preferred;
};

struct fb_info {
	struct cb_buffer *buffer;
	struct output *output;
	struct plane *plane;
	struct cb_rect src, dst;
	s32 zpos;
	struct list_head link;
};

struct scanout_commit_info {
	struct list_head fb_commits;
};

/* commit info helper functions */
struct scanout_commit_info *scanout_commit_info_alloc(void);
void *scanout_commit_add_fb_info(struct scanout_commit_info *commit,
				 struct cb_buffer *buffer,
				 struct output *output,
				 struct plane *plane,
				 struct cb_rect *src,
				 struct cb_rect *dst,
				 s32 zpos);
void scanout_commit_mod_fb_info(struct scanout_commit_info *commit,
				void *fb_info,
				struct cb_buffer *buffer,
				struct output *output,
				struct plane *plane,
				struct cb_rect *src,
				struct cb_rect *dst,
				s32 zpos);
void scanout_commit_info_free(struct scanout_commit_info *commit);

/* a output represent a LCDC/CRTC */
struct output {
	/* the sink of this output */
	struct head *head;

	/* enumerate plane list, so we can then get plane info. */
	struct plane *(*enumerate_plane)(struct output *o, struct plane *last);

	/* enumerate plane by fmt */
	struct plane *(*enumerate_plane_by_fmt)(struct output *o,
						struct plane *last,
						enum cb_pix_fmt fmt);

	/* enumerate mode list */
	struct cb_mode *(*enumerate_mode)(struct output *o,
					  struct cb_mode *last);

	/* create custom video timing */
	struct cb_mode *(*create_mode)(struct output *o,
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

	/* switch video mode timing */
	s32 (*switch_mode)(struct output *o, struct cb_mode *mode);

	/* enable video output */
	s32 (*enable)(struct output *o, struct cb_mode *mode);

	/* disable video output, may be because the monitor is unpluged. */
	s32 (*disable)(struct output *o);

	/* create native surface for renderer */
	void *(*native_surface_create)(struct output *o);

	/* destroy native surface */
	void (*native_surface_destroy)(struct output *o, void *surface);

};

enum dpms_state {
	DPMS_OFF = 0,
	DPMS_ON,
};

/* connector for monitor. e.g. HDMI/DP */
struct head {
	/* monitor connected or not */
	bool connected;

	/* the source of this head */
	struct output *output;

	/* connector type string (const), set by backend. */
	const char *connector_name;

	/* monitor's name, set by backend. */
	const char *monitor_name;

	/* get monitor's EDID */
	s32 (*retrieve_edid)(struct head *h, u8 *data, size_t *length);

	/*
	 * Add callback function to get notification of the monitor changed
	 * event.
	 */
	s32 (*add_head_changed_notify)(struct head *h, struct cb_listener *l);
/*
	s32 (*get_brightness)(struct head *h);
	void (*set_brightness)(struct head *h, s32 val);
	s32 (*get_contrast)(struct head *h);
	void (*set_contrast)(struct head *h, s32 val);
	s32 (*get_saturation)(struct head *h);
	void (*set_saturation)(struct head *h, s32 val);
	s32 (*get_hue)(struct head *h);
	void (*set_hue)(struct head *h, s32 val);
	enum cb_colorimetry (*get_colorimetry)(struct head *h);
	void (*set_colorimetry)(struct head *h, enum cb_colorimetry val);
	enum cb_quant_range (*get_quant_range)(struct head *h);
	void (*set_quant_range)(struct head *h, enum cb_quant_range val);
*/
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

	/* the sink of this plane */
	struct output *output;

	/* input pixel format supported */
	s32 count_formats;
	u32 *formats;

	u64 zpos;

	/* source has been alpha blended or not */
	u32 alpha_src;
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

	void *(*scanout_data_alloc)(struct scanout *so);

	s32 (*fill_scanout_data)(struct scanout *so,
				 void *scanout_data,
				 struct scanout_commit_info *commit);

	/* commit user settings to scanout */
	void (*do_scanout)(struct scanout *so, void *scanout_data);

	/* get native device */
	void *(*get_native_dev)(struct scanout *so);

	/* get native pixel format */
	u32 (*get_native_format)(struct scanout *so);

	/* import buffer from external DMA-BUF fd */
	struct cb_buffer *(*import_dmabuf)(struct scanout *so,
					   struct cb_buffer_info *info);

	/* release external DMA-BUF */
	void (*release_dmabuf)(struct scanout *so, struct cb_buffer *buffer);

	/* retrieve active buffer from surface */
	struct cb_buffer *(*import_surface_buf)(struct scanout *so,
						void *surface);

	/* return buffer to surface */
	void (*release_surface_buf)(struct scanout *so,
				    struct cb_buffer *buffer);

	/* add buffer flip cb */
	s32 (*add_buffer_flip_notify)(struct scanout *so,
				      struct cb_buffer *buffer,
				      struct cb_listener *l);

	/* add buffer complete cb */
	s32 (*add_buffer_complete_notify)(struct scanout *so,
					  struct cb_buffer *buffer,
					  struct cb_listener *l);

	/* debug set */
	void (*set_dbg_level)(struct scanout *so, enum cb_log_level level);
};

struct scanout *scanout_create(const char *dev_path, struct cb_event_loop *l);

#endif

