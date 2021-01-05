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
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <dirent.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <linux/uinput.h>
#include <linux/kd.h>
#include <linux/input.h>
#include <libudev.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_scanout.h>
#include <cube_renderer.h>
#include <cube_compositor.h>
#include <cube_protocal.h>
#include <cube_cache.h>
#include <cube_client_agent.h>
#include <cube_def_cursor.h>
#include <cube_vkey_map.h>

#define DUMMY_WIDTH 640
#define DUMMY_HEIGHT 480

#define SW_KBD_NAME "cube kbd led virtual dev"

#define MC_MAX_WIDTH 64
#define MC_MAX_HEIGHT 64

#define GLOBAL_DESKTOP_SZ 65536.0f

#define OUTPUT_DISABLE_DELAYED_MS 1
#define OUTPUT_DISABLE_DELAYED_US 500
#define DISABLE_ALL_DELAYED_MS 2
#define DISABLE_ALL_DELAYED_US 0

#define CONN_STATUS_DB_TIME 500

static enum cb_log_level comp_dbg = CB_LOG_NOTICE;
static enum cb_log_level client_dbg = CB_LOG_NOTICE;
static enum cb_log_level touch_dbg = CB_LOG_NOTICE;
static enum cb_log_level joystick_dbg = CB_LOG_NOTICE;

#define comp_debug(fmt, ...) do { \
	if (comp_dbg >= CB_LOG_DEBUG) { \
		cb_tlog("[COMP][DEBUG ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define comp_info(fmt, ...) do { \
	if (comp_dbg >= CB_LOG_INFO) { \
		cb_tlog("[COMP][INFO  ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define comp_notice(fmt, ...) do { \
	if (comp_dbg >= CB_LOG_NOTICE) { \
		cb_tlog("[COMP][NOTICE] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define comp_warn(fmt, ...) do { \
	cb_tlog("[COMP][WARN  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define comp_err(fmt, ...) do { \
	cb_tlog("[COMP][ERROR ] " fmt, ##__VA_ARGS__); \
} while (0);

#define touch_debug(fmt, ...) do { \
	if (touch_dbg >= CB_LOG_DEBUG) { \
		cb_tlog("[TOUC][DEBUG ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define touch_info(fmt, ...) do { \
	if (touch_dbg >= CB_LOG_INFO) { \
		cb_tlog("[TOUC][INFO  ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define touch_notice(fmt, ...) do { \
	if (touch_dbg >= CB_LOG_NOTICE) { \
		cb_tlog("[TOUC][NOTICE] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define touch_warn(fmt, ...) do { \
	cb_tlog("[TOUC][WARN  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define touch_err(fmt, ...) do { \
	cb_tlog("[TOUC][ERROR ] " fmt, ##__VA_ARGS__); \
} while (0);

#define joystick_debug(fmt, ...) do { \
	if (joystick_dbg >= CB_LOG_DEBUG) { \
		cb_tlog("[JS  ][DEBUG ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define joystick_info(fmt, ...) do { \
	if (joystick_dbg >= CB_LOG_INFO) { \
		cb_tlog("[JS  ][INFO  ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define joystick_notice(fmt, ...) do { \
	if (joystick_dbg >= CB_LOG_NOTICE) { \
		cb_tlog("[JS  ][NOTICE] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define joystick_warn(fmt, ...) do { \
	cb_tlog("[JS  ][WARN  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define joystick_err(fmt, ...) do { \
	cb_tlog("[JS  ][ERROR ] " fmt, ##__VA_ARGS__); \
} while (0);

#define LBTN_DRAG_TIMEOUT 50
#define DCLK_INTERVAL 500

struct cb_compositor;

struct scanout_task {
	struct cb_buffer *buffer;
	struct plane *plane;
	struct cb_rect *src, *dst;
	s32 zpos;
	bool alpha_src_pre_mul;
	struct list_head link;
};

/* each cb_output represent a display pipe */
struct cb_output {
	struct cb_compositor *c;

	/* index */
	s32 pipe;

	/*
	 * crtc's primary plane and cursor plane,
	 * a crtc must at least own two planes.
	 * the two plane pointer here are used to accelerate plane selection.
	 */
	struct plane *primary_plane;
	struct plane *cursor_plane;

	/* planes except cursor plane and primary plane */
	struct list_head free_planes;
	/*
	 * if primary plane is occupied by renderer, it cannot be used to
	 * display DMA-BUF direct show surface.
	 */
	bool primary_occupied_by_renderer;
	bool primary_renderer_disabled;
	bool primary_renderer_disable_pending;
	bool primary_renderer_enable_pending;
	bool vflipped_pending;
	struct cb_event_source *primary_vflipped_timer;

	/* desktop size */
	struct cb_rect desktop_rc;
	/* desktop scale:
	 *     for top/bottom border: desktop width / crtc width
	 *     for left/right border: desktop height / crtc height
	 */
	float scale;
	/* global coordinates e.g. (0 - 65535) rect */
	struct cb_rect g_desktop_rc;

	/* physical output size */
	u32 crtc_w, crtc_h;
	/* view port in physical area */
	struct cb_rect crtc_view_port;
	/* renderer's surface fb src rect */
	struct cb_rect native_surface_src;

	/* scanout output */
	struct output *output;

	/* native surface */
	void *native_surface;

	/* renderer's output */
	struct r_output *ro;

	/* display connector */
	struct head *head;

	/* enabled or not (non-debounced) */
	bool enabled;

	/* disable in progress */
	bool disable_pending;
	/* timer for disable asynchronous progress */
	struct cb_event_source *disable_timer;

	/* video timing switching in progress */
	bool switch_mode_pending;
	/* modeset pending mode */
	struct cb_mode *pending_mode;
	/* listener of modeset complete event */
	struct cb_listener switch_mode_l;
	/* signal of modeset complete event */
	struct cb_signal switch_mode_signal;


	/* head status (non-debounced) */
	/* listener of head changed event */
	struct cb_listener conn_st_chg_changed_l;

	/* head status (debounced) */
	bool conn_st_db;
	/* signal of debounced head changed event */
	struct cb_signal conn_st_chg_db_signal;
	/* event source of head change debounce timer */
	struct cb_event_source *conn_st_chg_db_timer;

	/* save the mode before suspend */
	struct cb_mode mode_save;
	bool mode_saved;

	/* dummy buffer */
	struct cb_buffer *dummy;
	/* dummy buffer source rect */
	struct cb_rect dummy_src;
	/* queue dummy buffer pageflip in progress */
	bool dummy_flipped_pending;
	/* dummy buffer flipped event listener */
	struct cb_listener dummy_flipped_l;

	/* mouse cursor view port in physical area */
	struct cb_rect mc_view_port;
	/* mouse cursor on screen or not */
	bool mc_on_screen;
	/* cursor buffers */
	struct cb_buffer *mc_buf[2];
	
	/*
	 * cursor buf current (prepare to be or already committed into kernel)
	 * buffer index
	 */
	s32 mc_buf_cur;
	bool mc_damaged;

	/* scanout's output pageflip listener */
	struct cb_listener output_flipped_l;

	/* repaint related */
	/* list of scanout tasks */
	struct list_head so_tasks;
	/* repaint status */
	enum {
		REPAINT_NOT_SCHEDULED = 0,
		REPAINT_START_FROM_IDLE,
		REPAINT_SCHEDULED,
		REPAINT_WAIT_COMPLETION,
	} repaint_status;

	/* used to schedule a repaint */
	struct cb_event_source *idle_repaint_source;

	struct timespec next_repaint;

	/* used to generate shm buffer flipped message */
	struct cb_signal surface_flipped_signal;

	bool show_rendered_buffer;

	/* current renderable buffer */
	struct cb_buffer *rbuf_cur;

	/* renderable buffer changed */
	bool renderable_buffer_changed;
};

struct cb_compositor {
	struct compositor base;

	/* list of clients */
	struct list_head clients;

	/* event loop */
	struct cb_event_loop *loop;

	/* wait for initial plug in/out debounce */
	struct cb_signal ready_signal;
	/* timer used for waiting initial hotplug debounce */
	struct cb_event_source *ready_timer;

	/* scanout interface */
	struct scanout *so;

	/* renderer interface */
	struct renderer *r;

	/* surface pixel format */
	u32 native_fmt;

	/* native device */
	void *native_dev;

	/* views in z-order from top to bottom */
	struct list_head views;

	struct cb_view *top_view;

	/* disable all outputs pending */
	s32 disable_all_pending;
	/* timer for suspend (asynchronous progress) */
	struct cb_event_source *suspend_timer;
	/* there is a suspend command pending */
	bool suspend_pending;
	/* it is used when outputs are suspend */
	bool disable_head_detect;

	/* mouse cursor's desktop position */
	struct cb_pos mc_desktop_pos;
	/* global coordinate mouse cursor's desktop position */
	struct cb_pos mc_g_desktop_pos;
	/* hide cursor or not */
	bool mc_hide;
	/* mouse cursor's hot point position */
	struct cb_pos mc_hot_pos;
	/* cursor buffer source rect */
	struct cb_rect mc_src;
	/* mouse cursor's alpha is blended or not */
	bool mc_alpha_src_pre_mul;

	/* repaint timer */
	struct cb_event_source *repaint_timer;

	/* outputs */
	s32 count_outputs;
	struct cb_output **outputs;

	/* repaint clock */
	u32 clock_type;

	/* scanout task cache */
	void *so_task_cache;

	/* for input event */
	struct udev *udev;
	struct udev_monitor *udev_monitor;
	struct cb_event_source *udev_source;
	struct list_head input_devs;

	s32 uinput_fd;

	struct input_event *raw_input_buffer;
	size_t raw_input_buffer_sz;
	u8 *raw_input_tx_buffer;
	size_t raw_input_tx_buffer_sz;
	u8 *input_tx_buffer;
	size_t input_tx_buffer_sz;

	float mc_accel;
	s32 touch_pipe;

	/* touchscreen attached (only one touch screen is supported) */
	bool touchscreen_attached;

	/* for debug tool change dbg level */
	s32 debug_inotify_fd;
	struct cb_event_source *dbg_source;
};

#define MAX_SLOT_NR 32

struct touch_slot {
	u8 id;
	bool pressed; /* reset when commit */
	bool pos_x_changed; /* reset when commit */
	bool pos_y_changed; /* reset when commit */
	bool commit_pending; /* reset when commit */
	u16 pos_x;
	u16 pos_y;
};

struct touch_status {
	s32 st_x, st_y;
	bool st_changed;
	struct touch_slot slots[MAX_SLOT_NR];
	struct touch_slot *last_slot; /* reset when commit */
};

enum touch_type {
	ST_TOUCH = 0,
	MT_TOUCH,
};

struct touch_info {
	enum touch_type type;
	s32 fix_min_x, fix_max_x, fix_min_y, fix_max_y;
	s32 min_abs_x, min_abs_y, max_abs_x, max_abs_y;
	s32 min_abs_mt_position_x, max_abs_mt_position_x;
	s32 min_abs_mt_position_y, max_abs_mt_position_y;
	s32 min_abs_mt_slot, max_abs_mt_slot;
	u32 count_slots;
	s32 min_x, max_x, min_y, max_y;
	bool fix_min_max_pending;
};

enum input_type {
	INPUT_TYPE_UNKNOWN = 0,
	INPUT_TYPE_MOUSE,
	INPUT_TYPE_KBD,
	INPUT_TYPE_KBD_LED_VDEV,
	INPUT_TYPE_TOUCH,
	INPUT_TYPE_JOYSTICK,
};

struct input_device {
	s32 fd;
	enum input_type type;
	char name[256];
	char devpath[256];
	struct cb_event_source *input_source;
	struct cb_compositor *c;
	struct list_head link;

	/* for touch screen */
	struct touch_info tinfo;
	struct touch_status ts;

	/* used for gui */
	struct cb_event_source *lbtn_down_timer;
	bool drag_flag;
	bool draging;
	struct cb_event_source *lbtn_clk_timer;
	s32 lbtn_clk_cnt;
	struct cb_event_source *mbtn_clk_timer;
	s32 mbtn_clk_cnt;
	struct cb_event_source *rbtn_clk_timer;
	s32 rbtn_clk_cnt;

	struct cb_event_source *tbtn_down_timer;
	bool tdrag_flag;
	bool tdraging;
	struct cb_pos tpos;
};

#define NSEC_PER_SEC 1000000000
static inline void timespec_sub(struct timespec *r,
				const struct timespec *a,
				const struct timespec *b)
{
	r->tv_sec = a->tv_sec - b->tv_sec;
	r->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

static inline s64 timespec_to_nsec(const struct timespec *a)
{
	return (s64)a->tv_sec * NSEC_PER_SEC + a->tv_nsec;
}

static inline struct cb_compositor *to_cb_c(struct compositor *c)
{
	return container_of(c, struct cb_compositor, base);
}

static inline void timespec_add_nsec(struct timespec *r,
				     const struct timespec *a, s64 b)
{
	r->tv_sec = a->tv_sec + (b / NSEC_PER_SEC);
	r->tv_nsec = a->tv_nsec + (b % NSEC_PER_SEC);

	if (r->tv_nsec >= NSEC_PER_SEC) {
		r->tv_sec++;
		r->tv_nsec -= NSEC_PER_SEC;
	} else if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

static inline void timespec_add_msec(struct timespec *r,
				     const struct timespec *a, s64 b)
{
	return timespec_add_nsec(r, a, b * 1000000l);
}

static inline s64 timespec_sub_to_nsec(const struct timespec *a,
				       const struct timespec *b)
{
	struct timespec r;
	timespec_sub(&r, a, b);
	return timespec_to_nsec(&r);
}

static inline s64 timespec_sub_to_msec(const struct timespec *a,
				       const struct timespec *b)
{
	return timespec_sub_to_nsec(a, b) / 1000000;
}

static void output_free_planes_finish(struct cb_output *output)
{
	struct plane *plane, *plane_next;

	list_for_each_entry_safe(plane, plane_next, &output->free_planes, link)
		list_del(&plane->link);
}

static void show_free_planes(struct cb_output *o)
{
	struct plane *plane;
	s32 i;

	list_for_each_entry(plane, &o->free_planes, link) {
		comp_notice("Ouput %d plane type: %d", o->pipe, plane->type);
		comp_notice("\tFormats:");
		for (i = 0; i < plane->count_formats; i++) {
			comp_notice("\t\t%4.4s", (char *)&plane->formats[i]);
		}
	}
}

static struct plane *get_free_output_plane(struct cb_output *o,
					   struct plane *plane)
{
	list_del(&plane->link);
	return plane;
}

static void put_free_output_plane(struct cb_output *o, struct plane *plane)
{
	struct plane *p;
	bool find_pos = false;

	list_for_each_entry(p, &o->free_planes, link) {
		if (p->zpos > plane->zpos) {
			find_pos = true;
			break;
		}
	}

	if (find_pos) {
		/* in zpos inc order */
		list_add(&plane->link, p->link.prev);
	} else {
		list_add_tail(&plane->link, &o->free_planes);
	}
}

static bool primary_support_fmt(struct cb_output *o, enum cb_pix_fmt fmt)
{
	char fourcc[4] = {0};
	struct plane *plane = o->primary_plane;
	s32 i;

	switch (fmt) {
	case CB_PIX_FMT_ARGB8888:
		memcpy(fourcc, "AR24", 4);
		break;
	case CB_PIX_FMT_XRGB8888:
		memcpy(fourcc, "XR24", 4);
		break;
	case CB_PIX_FMT_NV12:
		memcpy(fourcc, "NV12", 4);
		break;
	case CB_PIX_FMT_NV16:
		memcpy(fourcc, "NV16", 4);
		break;
	case CB_PIX_FMT_NV24:
		memcpy(fourcc, "NV24", 4);
		break;
	case CB_PIX_FMT_RGB888:
		memcpy(fourcc, "RG24", 4);
		break;
	case CB_PIX_FMT_RGB565:
		memcpy(fourcc, "RG16", 4);
		break;
	case CB_PIX_FMT_YUYV:
		memcpy(fourcc, "YUYV", 4);
		break;
	case CB_PIX_FMT_YUV420:
		memcpy(fourcc, "YU12", 4);
		break;
	case CB_PIX_FMT_YUV422:
		memcpy(fourcc, "YU16", 4);
		break;
	case CB_PIX_FMT_YUV444:
		memcpy(fourcc, "YU24", 4);
		break;
	default:
		comp_err("unsupported format %4.4s surpported.", fourcc);
		return false;
	}

	for (i = 0; i < plane->count_formats; i++) {
		if (!memcmp((char *)&plane->formats[i], fourcc, 4))
			return true;
	}

	return false;
}

static struct plane *find_free_output_plane(struct cb_output *o,
					    enum cb_pix_fmt fmt,
					    s32 zpos)
{
	char fourcc[4] = {0};
	struct plane *plane;
	s32 i;

	switch (fmt) {
	case CB_PIX_FMT_ARGB8888:
		memcpy(fourcc, "AR24", 4);
		break;
	case CB_PIX_FMT_XRGB8888:
		memcpy(fourcc, "XR24", 4);
		break;
	case CB_PIX_FMT_NV12:
		memcpy(fourcc, "NV12", 4);
		break;
	case CB_PIX_FMT_NV16:
		memcpy(fourcc, "NV16", 4);
		break;
	case CB_PIX_FMT_NV24:
		memcpy(fourcc, "NV24", 4);
		break;
	case CB_PIX_FMT_RGB888:
		memcpy(fourcc, "RG24", 4);
		break;
	case CB_PIX_FMT_RGB565:
		memcpy(fourcc, "RG16", 4);
		break;
	case CB_PIX_FMT_YUYV:
		memcpy(fourcc, "YUYV", 4);
		break;
	case CB_PIX_FMT_YUV420:
		memcpy(fourcc, "YU12", 4);
		break;
	case CB_PIX_FMT_YUV422:
		memcpy(fourcc, "YU16", 4);
		break;
	case CB_PIX_FMT_YUV444:
		memcpy(fourcc, "YU24", 4);
		break;
	default:
		comp_err("unsupported format %4.4s surpported.", fourcc);
		return NULL;
	}

	list_for_each_entry(plane, &o->free_planes, link) {
		if (zpos != -1 && plane->zpos != zpos)
			continue;
		for (i = 0; i < plane->count_formats; i++) {
			if (!memcmp((char *)&plane->formats[i], fourcc, 4)) {
				return plane;
			}
		}
	}

	comp_err("cannot find plane which supports format %4.4s.", fourcc);

	return NULL;
}

static void output_free_planes_prepare(struct cb_output *output)
{
	struct output *o = output->output;
	struct plane *last_plane = NULL, *plane;

	do {
		plane = o->enumerate_plane(o, last_plane);
		last_plane = plane;
		if (plane) {
			switch (plane->type) {
			case PLANE_TYPE_OVERLAY:
				list_add_tail(&plane->link,
					      &output->free_planes);
				comp_debug("stack plane [zpos]: %lu",
					   plane->zpos);
				break;
			default:
				break;
			}
		}
	} while (plane);
}

static void cb_output_destroy(struct cb_output *output)
{
	struct cb_compositor *c;
	s32 i;

	if (!output)
		return;

	c = output->c;

	if (output->primary_vflipped_timer)
		cb_event_source_remove(output->primary_vflipped_timer);

	cb_signal_fini(&output->surface_flipped_signal);

	if (output->conn_st_chg_db_timer)
		cb_event_source_remove(output->conn_st_chg_db_timer);

	cb_signal_fini(&output->conn_st_chg_db_signal);

	cb_signal_fini(&output->switch_mode_signal);

	if (output->disable_timer)
		cb_event_source_remove(output->disable_timer);

	if (output->dummy && c && c->so)
		c->so->dumb_buffer_destroy(c->so, output->dummy);
	for (i = 0; i < 2; i++) {
		if (output->mc_buf[i] && c && c->so) {
			comp_notice("destroy cursor bo");
			c->so->cursor_bo_destroy(c->so, output->mc_buf[i]);
		}
	}

	output_free_planes_finish(output);

	if (output->output && c && c->so)
		output->c->so->pipeline_destroy(c->so, output->output);

	free(output);
}

static s32 check_mouse_pos(struct cb_compositor *c, s32 x, s32 y)
{
	s32 i;
	struct cb_rect *rc;
	bool valid = false;

	for (i = 0; i < c->count_outputs; i++) {
		rc = &c->outputs[i]->desktop_rc;
		if (x >= rc->pos.x &&
		    x < (rc->pos.x + (s32)(rc->w)) &&
		    y >= rc->pos.y &&
		    y < (rc->pos.y + (s32)(rc->h))) {
			valid = true;
			break;
		}
	}

	if (valid)
		return i;
	else
		return -1;
}

static void normalize_mouse_pos(struct cb_compositor *c, s32 cur_screen,
				s32 dx, s32 dy)
{
	struct cb_rect *rc = &c->outputs[cur_screen]->desktop_rc;
	s32 index;

	index = check_mouse_pos(c, c->mc_desktop_pos.x + dx,
				c->mc_desktop_pos.y + dy);
	if (index < 0) {
		if (check_mouse_pos(c, c->mc_desktop_pos.x + dx,
				    c->mc_desktop_pos.y)
				< 0) {
			if ((c->mc_desktop_pos.x + dx)
					>= (rc->pos.x + (s32)(rc->w))) {
				c->mc_desktop_pos.x = rc->pos.x + rc->w - 1;
			} else if ((c->mc_desktop_pos.x + dx) < rc->pos.x) {
				c->mc_desktop_pos.x = rc->pos.x;
			}
		} else {
			c->mc_desktop_pos.x += dx;
		}
		if (check_mouse_pos(c, c->mc_desktop_pos.x,
				    c->mc_desktop_pos.y + dy) < 0) {
			if ((c->mc_desktop_pos.y + dy)
					>= (rc->pos.y + (s32)(rc->h))) {
				c->mc_desktop_pos.y = rc->pos.y + rc->h - 1;
			} else if ((c->mc_desktop_pos.y + dy) < rc->pos.y) {
				c->mc_desktop_pos.y = rc->pos.y;
			}
		} else {
			c->mc_desktop_pos.y += dy;
		}
		index = cur_screen;
	} else {
		c->mc_desktop_pos.x += dx;
		c->mc_desktop_pos.y += dy;
	}
}

static void gen_global_pos(struct cb_output *o)
{
	s32 dx, dy;
	s32 abs_dx, abs_dy;
	struct cb_compositor *c = o->c;

	if (!o->enabled)
		return;
	if (!o->mc_on_screen)
		return;
	dx = c->mc_desktop_pos.x - o->desktop_rc.pos.x;
	dy = c->mc_desktop_pos.y - o->desktop_rc.pos.y;
	if (dx < 0 || dy < 0)
		return;

	/*
	 *    dx            w
	 * --------  =  ---------
	 *  abs_dx        abs_w
	 */
	abs_dx = (float)dx * o->g_desktop_rc.w / o->desktop_rc.w;
	/*
	 *    dy            h
	 * --------  =  ---------
	 *  abs_dy        abs_h
	 */
	abs_dy = (float)dy * o->g_desktop_rc.h / o->desktop_rc.h;
	c->mc_g_desktop_pos.x = abs_dx + o->g_desktop_rc.pos.x;
	c->mc_g_desktop_pos.y = abs_dy + o->g_desktop_rc.pos.y;
	comp_debug("%d, %d", c->mc_g_desktop_pos.x, c->mc_g_desktop_pos.y);
}

/*
 * Calculate mouse cursor display position on screen.
 * it depend update_crtc_view_port to calculate desktop scaler
 */
static void update_mc_view_port(struct cb_output *output, bool gen_g_pos)
{
	struct cb_compositor *c = output->c;
	s32 dx, dy;

	if (((c->mc_desktop_pos.x - c->mc_hot_pos.x) >=
			(s32)(output->desktop_rc.pos.x - MC_MAX_WIDTH)) &&
	    ((c->mc_desktop_pos.x - c->mc_hot_pos.x) <
			(s32)(output->desktop_rc.pos.x
				+ output->desktop_rc.w)) &&
	    ((c->mc_desktop_pos.y - c->mc_hot_pos.y) >=
			(s32)(output->desktop_rc.pos.y - MC_MAX_HEIGHT)) &&
	    ((c->mc_desktop_pos.y - c->mc_hot_pos.y) <
			(s32)(output->desktop_rc.pos.y
				+ output->desktop_rc.h))) {
		output->mc_on_screen = true;
		if (gen_g_pos)
			gen_global_pos(output);
		output->mc_view_port.pos.x = output->crtc_view_port.pos.x;
		output->mc_view_port.pos.y = output->crtc_view_port.pos.y;
		dx = c->mc_desktop_pos.x - output->desktop_rc.pos.x;
		dy = c->mc_desktop_pos.y - output->desktop_rc.pos.y;
		output->mc_view_port.pos.x += (s32)((float)dx / output->scale);
		output->mc_view_port.pos.x -= c->mc_hot_pos.x;
		output->mc_view_port.pos.y += (s32)((float)dy / output->scale);
		output->mc_view_port.pos.y -= c->mc_hot_pos.y;
		output->mc_view_port.w = MC_MAX_WIDTH;
		output->mc_view_port.h = MC_MAX_WIDTH;
	} else {
		output->mc_on_screen = false;
	}
}

static void set_fix_touch_min_max_pending(struct input_device *dev);

/*
 * Calculate rectangle on crtc coordinates for desktop canvas.
 * desktop canvas's size is the real full screen buffer's size.
 */
static void update_crtc_view_port(struct cb_output *output)
{
	struct cb_mode *mode;
	s32 calc;
	struct input_device *dev;
	struct cb_compositor *c;

	mode = output->output->get_current_mode(output->output);
	if (!mode) {
		output->crtc_w = output->crtc_h = 0;
		memset(&output->crtc_view_port, 0,
			sizeof(output->crtc_view_port));
	} else {
		output->native_surface_src.w = mode->width;
		output->native_surface_src.h = mode->height;
		output->native_surface_src.pos.x = 0;
		output->native_surface_src.pos.y = 0;
		output->crtc_w = mode->width;
		output->crtc_h = mode->height;
		calc = output->crtc_w * output->desktop_rc.h
				/ output->desktop_rc.w;
		if (calc <= output->crtc_h) {
			output->scale = (float)output->desktop_rc.w
						/ output->crtc_w;
			output->crtc_view_port.pos.x = 0;
			output->crtc_view_port.pos.y =
				(output->crtc_h - calc) / 2;
			output->crtc_view_port.w = output->crtc_w;
			output->crtc_view_port.h = calc;
		} else {
			output->scale = (float)output->desktop_rc.h
				/ output->crtc_h;
			calc = output->desktop_rc.w * output->crtc_h
				/ output->desktop_rc.h;
			output->crtc_view_port.pos.x =
				(output->crtc_w - calc) / 2;
			output->crtc_view_port.pos.y = 0;
			output->crtc_view_port.w = calc;
			output->crtc_view_port.h = output->crtc_h;
		}
		c = output->c;
		if (c->touchscreen_attached) {
			list_for_each_entry(dev, &c->input_devs, link) {
				if (dev->type == INPUT_TYPE_TOUCH) {
					set_fix_touch_min_max_pending(dev);
				}
			}
		}
	}
	comp_notice("desktop (%d,%d - %ux%u) crtc_view_port (%d,%d - %ux%u)",
			output->desktop_rc.pos.x, output->desktop_rc.pos.y,
			output->desktop_rc.w, output->desktop_rc.h,
			output->crtc_view_port.pos.x,
			output->crtc_view_port.pos.y,
			output->crtc_view_port.w,
			output->crtc_view_port.h);

	update_mc_view_port(output, true);
}

static void cb_compositor_repaint_by_output(struct cb_output *output);
static void cb_compositor_repaint(struct cb_compositor *c);
static bool setup_view_output_mask(struct cb_view *view,
				   struct cb_compositor *c);
static void surface_flipped_cb(struct cb_listener *listener, void *data);

static struct cb_output *find_surface_main_output(struct cb_surface *surface,
						  u32 mask)
{
	struct cb_output *o, *max_o = NULL;
	struct cb_compositor *c = surface->c;
	struct cb_view *v;
	u32 max_refresh_nsec = 0;
	s32 i;

	v = surface->view;
	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (v && v->pipe_locked && o->pipe == v->pipe_locked) {
			return o;
		}

		if ((mask & (1U << o->pipe)) &&
		    (o->output->refresh_nsec > max_refresh_nsec)) {
			max_refresh_nsec = o->output->refresh_nsec;
			max_o = o;
		}
	}

	if (surface->output != max_o && max_o) {
		comp_debug("[--- old output [%d] ---> new output [%d] "
			   "fresh: %u",
			   surface->output ? surface->output->pipe : -1,
			   max_o->pipe,
			   max_o->output->refresh_nsec);
	}

	return max_o;
}

static void cancel_renderer_surface(struct cb_surface *surface, bool force)
{
	struct cb_compositor *c = surface->c;
	struct cb_output *o;

	surface->view->painted = false;
	setup_view_output_mask(surface->view, surface->c);
	
	comp_debug("surface buffer flipped %p from output %d",
		   surface->buffer_cur, surface->output->pipe);
	
	list_del(&surface->flipped_l.link);

	if (!force) {
		o = find_surface_main_output(surface,
					     surface->view->output_mask);
		if (surface->output)
			list_del(&surface->flipped_l.link);
		surface->output = o;
		if (o) {
			surface->flipped_l.notify = surface_flipped_cb;
			cb_signal_add(&o->surface_flipped_signal,
				      &surface->flipped_l);
		}
		cb_compositor_repaint(c);
	}
}

static void cancel_so_tasks(struct cb_output *output)
{
	struct scanout_task *sot, *sot_next;
	struct cb_compositor *c = output->c;

	comp_warn("sot remains:");
	list_for_each_entry_safe(sot, sot_next, &output->so_tasks, link) {
		list_del(&sot->link);
		comp_warn("\t%d,%d %ux%u -> %d,%d %ux%u, zpos: %d, dirty:%08X",
			   sot->src->pos.x, sot->src->pos.y,
			   sot->src->w, sot->src->h,
			   sot->dst->pos.x, sot->dst->pos.y,
			   sot->dst->w, sot->dst->h, sot->zpos,
			   sot->buffer->dirty);
		if (sot->buffer && (sot->buffer->dirty & (1U << output->pipe))){
			sot->buffer->dirty &= (~(1U << output->pipe));
		}
		cb_cache_put(sot, c->so_task_cache);
	}

	output->renderable_buffer_changed = false;
	comp_warn("Clear output %d renderable_buffer_changed", output->pipe);
}

static void disable_output_render(struct cb_output *o)
{
	if (o->ro) {
		o->ro->destroy(o->ro);
		comp_debug("set output %d's rbuf_cur NULL", o->pipe);
		o->rbuf_cur = NULL;
		o->ro = NULL;
	}
	if (o->native_surface) {
		o->output->native_surface_destroy(o->output, o->native_surface);
		o->native_surface = NULL;
	}
}

static s32 suspend(struct cb_compositor *c)
{
	struct cb_output *o;
	s32 i;
	struct cb_mode *mode;

	comp_notice("Suspend >>>>>>>>>>>>>>");
	if (!c || !c->outputs)
		return -EINVAL;

	if (c->suspend_pending) {
		comp_notice("suspend pending, try later.");
		return -EAGAIN;
	}

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (!o)
			continue;

		if (o->enabled) {
			o->disable_pending = true;
			o->enabled = false;
			mode = o->output->get_current_mode(o->output);
			if (mode) {
				comp_notice("Save mode %ux%u@%u",
					    mode->width, mode->height,
					    mode->vrefresh);
				memcpy(&o->mode_save, mode, sizeof(*mode));
				o->mode_saved = true;
			} else {
				o->mode_saved = false;
			}
			comp_notice("Try to disable output: %d", o->pipe);
			if (o->output->disable(o->output) < 0) {
				cb_event_source_timer_update(o->disable_timer,
						     OUTPUT_DISABLE_DELAYED_MS,
						     OUTPUT_DISABLE_DELAYED_US);
				c->disable_all_pending++;
			} else {
				disable_output_render(o);
				/* update view port when output is disabled. */
				update_crtc_view_port(o);
				o->disable_pending = false;
				o->repaint_status = REPAINT_NOT_SCHEDULED;
				cancel_so_tasks(o);
			}
		} else {
			if (o->disable_pending) {
				comp_notice("[output:%d] disabled in progress.",
					    o->pipe);
				c->disable_all_pending++;
			} else {
				comp_notice("[output: %d] already disabled.",
					    o->pipe);
			}
		}
	}

	if (c->disable_all_pending) {
		comp_notice("suspend pending: %d", c->disable_all_pending);
		c->suspend_pending = true;
		cb_event_source_timer_update(c->suspend_timer,
					     DISABLE_ALL_DELAYED_MS,
					     DISABLE_ALL_DELAYED_US);
		return -EAGAIN;
	}

	c->suspend_pending = false;
	/* disable head detect */
	c->disable_head_detect = true;

	/* TODO notify clients about the disable event */

	return 0;
}

static s32 cb_compositor_suspend(struct compositor *comp)
{
	struct cb_compositor *c = to_cb_c(comp);

	if (c)
		return suspend(c);

	return -EINVAL;
}

static void cb_compositor_input_fini(struct cb_compositor *c);

static void cb_compositor_rm_client(struct compositor *comp,
				    struct cb_client_agent *client)
{
	if (!client || !comp)
		return;

	list_del(&client->link);
	client->destroy_pending(client);
}

static struct cb_client_agent *cb_compositor_add_client(struct compositor *comp,
							s32 sock)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_client_agent *client;

	client = cb_client_agent_create(sock, comp, c->loop);
	if (!client) {
		comp_err("failed to create client agent.");
		return NULL;
	}

	list_add_tail(&client->link, &c->clients);
	return client;
}

static s32 cb_compositor_destroy(struct compositor *comp)
{
	struct cb_client_agent *client, *next;
	struct cb_compositor *c = to_cb_c(comp);
	s32 i, ret;
	struct cb_view *view, *view_next;

	if (c->dbg_source) {
		cb_event_source_remove(c->dbg_source);
		c->dbg_source = NULL;
	}

	if (c->debug_inotify_fd > 0) {
		close(c->debug_inotify_fd);
		c->debug_inotify_fd = 0;
	}

	list_for_each_entry_safe(client, next, &c->clients, link) {
		list_del(&client->link);
		cb_client_agent_destroy(client);
	}

	ret = suspend(c);
	if (ret == -EAGAIN || c->suspend_pending) {
		comp_notice("suspend pending !!!");
		return -EAGAIN;
	}

	cb_compositor_input_fini(c);

	if (c->repaint_timer)
		cb_event_source_remove(c->repaint_timer);

	if (c->suspend_timer)
		cb_event_source_remove(c->suspend_timer);

	if (c->ready_timer)
		cb_event_source_remove(c->ready_timer);

	cb_signal_fini(&c->ready_signal);

	if (c->outputs && c->count_outputs) {
		for (i = 0; i < c->count_outputs; i++) {
			if (c->outputs[i]) {
				cb_output_destroy(c->outputs[i]);
			}
		}
		free(c->outputs);
	}

	if (c->r)
		c->r->destroy(c->r);

	list_for_each_entry_safe(view, view_next, &c->views, link) {
		list_del(&view->link);
	}

	if (c->so)
		c->so->destroy(c->so);

	if (c->so_task_cache)
		cb_cache_destroy(c->so_task_cache);

	free(c);

	return 0;
}

static s32 cb_compositor_register_ready_cb(struct compositor *comp,
					   struct cb_listener *l)
{
	struct cb_compositor *c = to_cb_c(comp);

	if (!l || !comp)
		return -EINVAL;

	cb_signal_add(&c->ready_signal, l);

	cb_event_source_timer_update(c->ready_timer,
				     CONN_STATUS_DB_TIME * 2, 0);

	return 0;
}

static void cb_compositor_dispatch_hpd(struct compositor *comp, s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	struct cb_client_agent *client;
	s32 i;
	struct cb_connector_info conn_info;
	struct cb_mode *mode;

	if (!comp)
		return;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (pipe == o->pipe)
			break;
	}

	if (i == c->count_outputs)
		return;

	memset(&conn_info, 0, sizeof(conn_info));
	conn_info.pipe = pipe;
	conn_info.enabled = o->enabled;
	strncpy(conn_info.connector_name,
		comp->get_connector_name(comp, pipe),
		CB_CONNECTOR_NAME_MAX_LEN);
	if (o->enabled) {
		strncpy(conn_info.monitor_name,
			comp->get_monitor_name(comp, pipe),
			CB_MONITOR_NAME_MAX_LEN);
		if (o->switch_mode_pending && o->pending_mode) {
			mode = o->pending_mode;
		} else {
			mode = o->output->get_current_mode(o->output);
		}
		if (mode) {
			conn_info.width_cur = mode->width;
			conn_info.height_cur = mode->height;
			conn_info.vrefresh_cur = mode->vrefresh;
			conn_info.pixel_freq_cur = mode->pixel_freq;
		}
		mode = o->output->get_preferred_mode(o->output);
		if (mode) {
			conn_info.width = mode->width;
			conn_info.height = mode->height;
			conn_info.vrefresh = mode->vrefresh;
			conn_info.pixel_freq = mode->pixel_freq;
		}
	}
	list_for_each_entry(client, &c->clients, link) {
		if (client->capability & CB_CLIENT_CAP_HPD) {
			client->send_hpd_evt(client, &conn_info);
		}
	}
}

static s32 ready_timer_cb(void *data)
{
	struct cb_compositor *c = data;

	comp_debug("Compositor ready.");
	cb_signal_emit(&c->ready_signal, NULL);

	return 0;
}

static void fill_dummy(u8 *data, u32 width, u32 height, u32 stride)
{
	s32 i, j;
	u32 *pixel = (u32 *)data;

	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			pixel[j] = 0xFF000000;
		}
		pixel += (stride >> 2);
	}
}

static void fill_cursor(struct cb_compositor *c, struct cb_buffer *b,
			u8 *data, u32 width, u32 height, u32 stride)
{
	c->so->cursor_bo_update(c->so, b,
				data, width, height, stride);
}

static void dummy_flipped_cb(struct cb_listener *listener, void *data)
{
	struct cb_output *output;

	output = container_of(listener, struct cb_output,
			      dummy_flipped_l);
	output->dummy_flipped_pending = false;
	comp_notice("[output: %d] Dummy flipped", output->pipe);
}

static void show_dummy(struct cb_output *output)
{
	struct cb_compositor *c = output->c;
	struct scanout_commit_info *commit;
	void *sd;

	commit = scanout_commit_info_alloc();
	output->dummy_src.pos.x = output->dummy_src.pos.y = 0;
	output->dummy_src.w = DUMMY_WIDTH;
	output->dummy_src.h = DUMMY_HEIGHT;
	comp_notice("show dummy: %ux%u, %d,%d %ux%u",
		DUMMY_WIDTH, DUMMY_HEIGHT,
		output->crtc_view_port.pos.x,
		output->crtc_view_port.pos.y,
		output->crtc_view_port.w,
		output->crtc_view_port.h);
	scanout_commit_add_fb_info(commit, output->dummy,
				   output->output, output->primary_plane,
				   &output->dummy_src, &output->crtc_view_port,
				   0, true);
	if (!c->mc_hide && output->mc_on_screen) {
		comp_debug("Add FB dummy mc %p",
			   output->mc_buf[output->mc_buf_cur]);
		if (output->mc_damaged) {
			output->mc_buf_cur = 1 - output->mc_buf_cur;
			output->mc_damaged = false;
		}
		scanout_commit_add_fb_info(commit,
			output->mc_buf[output->mc_buf_cur],
			output->output,
			output->cursor_plane,
			&c->mc_src,
			&output->mc_view_port,
			-1,
			c->mc_alpha_src_pre_mul);
	}
	sd = c->so->scanout_data_alloc(c->so);
	c->so->fill_scanout_data(c->so, sd, commit);
	output->dummy_flipped_pending = true;
	c->so->do_scanout(c->so, sd);
	scanout_commit_info_free(commit);
	output->repaint_status = REPAINT_WAIT_COMPLETION;
}

static void disable_primary_renderer(struct cb_output *o)
{
	if (o->primary_renderer_disabled || o->primary_renderer_disable_pending)
		return;

	comp_warn("disable primary renderer");
	o->primary_renderer_disable_pending = true;
}

static void enable_primary_renderer(struct cb_output *o)
{
	if (!o->primary_renderer_disabled || o->primary_renderer_enable_pending)
		return;

	comp_warn("enable primary renderer");
	o->primary_renderer_enable_pending = true;
}

static bool prepare_dma_buf_planes(struct cb_surface *surface,
				   struct cb_buffer *buffer)
{
	struct cb_view *view = surface->view;
	struct cb_compositor *c = surface->c;
	struct cb_output *o;
	struct plane *plane;
	u32 mask = view->output_mask;
	s32 i, pipe, pipe_locked;
	bool lock_pipe;

	pipe_locked = view->pipe_locked;
	lock_pipe = (pipe_locked == -1 ? false : true);
	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];

		/* keep plane info when connector is disconnected */
		if (!o->enabled)
			continue;
		if (lock_pipe && o->pipe != pipe_locked)
			continue;
		
		pipe = o->pipe;
		plane = view->planes[o->pipe];

		if (!(mask & (1U << o->pipe))) {
			if (plane) {
				if (plane != o->primary_plane)
					put_free_output_plane(o, plane);
				else
					enable_primary_renderer(o);
				view->planes[pipe] = NULL;
			}
			continue;
		}

		if (plane) {
			if (plane != o->primary_plane)
				put_free_output_plane(o, plane);
			view->planes[pipe] = NULL;
		}

		if (!(plane && plane == o->primary_plane)) {
			comp_debug("find plane for fmt %d, zpos %d",
				   buffer->info.pix_fmt, view->zpos);
			plane = find_free_output_plane(o, buffer->info.pix_fmt,
					       view->zpos);
		}
		if (!plane) {
			comp_warn("cannot find plane.");
			if (primary_support_fmt(o, buffer->info.pix_fmt)) {
				plane = o->primary_plane;
				if (o->primary_renderer_disabled) {
					comp_warn("output %d's primary already "
						  "be used", o->pipe);
					return false;
				}
				comp_warn("Use primary");
				disable_primary_renderer(o);
				view->planes[pipe] = plane;
				if (!plane->scale_support) {
					u32 w, h;

					if (view->src_areas[pipe].pos.x == 0) {
						w = o->crtc_view_port.w +
						  o->crtc_view_port.pos.x -
						  view->dst_areas[pipe].pos.x;
						w = MIN(w, buffer->info.width);
						view->src_areas[pipe].w = w;
					}

					if (view->src_areas[pipe].pos.y == 0) {
						h = o->crtc_view_port.h +
						  o->crtc_view_port.pos.y -
						  view->dst_areas[pipe].pos.y;
						h = MIN(h, buffer->info.height);
						view->src_areas[pipe].h = h;
					}
					
					view->dst_areas[pipe].w =
						view->src_areas[pipe].w;
					view->dst_areas[pipe].h =
						view->src_areas[pipe].h;
				}
			} else {
				return false;
			}
		} else {
			comp_debug("get this plane zpos: %d, type %d",
				   plane->zpos, plane->type);
			get_free_output_plane(o, plane);
			view->planes[pipe] = plane;
			if (!plane->scale_support) {
				u32 w, h;

				if (view->src_areas[pipe].pos.x == 0) {
					w = o->crtc_view_port.w +
					    o->crtc_view_port.pos.x -
						view->dst_areas[pipe].pos.x;
					w = MIN(w, buffer->info.width);
					view->src_areas[pipe].w = w;
				}

				if (view->src_areas[pipe].pos.y == 0) {
					h = o->crtc_view_port.h +
					    o->crtc_view_port.pos.y -
						view->dst_areas[pipe].pos.y;
					h = MIN(h, buffer->info.height);
					view->src_areas[pipe].h = h;
				}

				view->dst_areas[pipe].w =
					view->src_areas[pipe].w;
				view->dst_areas[pipe].h =
					view->src_areas[pipe].h;
			}
		}
	}

	return true;
}

static void dma_buf_flipped_cb(struct cb_listener *listener, void *data)
{
	struct cb_buffer *buffer = data;
	struct cb_surface *surface = buffer->surface;
	struct cb_client_agent *client = surface->client_agent;

	comp_debug("del dma buf flipped listener begin");
	list_del(&listener->link);
	client->send_bo_flipped(client, buffer, (u64)surface);
	comp_debug("del dma buf flipped listener end");
}

static void dma_buf_completed_cb(struct cb_listener *listener, void *data)
{
	struct cb_buffer *buffer = data;
	struct cb_surface *surface = buffer->surface;
	struct cb_client_agent *client = surface->client_agent;

	comp_debug("del dma buf complete listener begin");
	list_del(&listener->link);
	client->send_bo_complete(client, buffer, (u64)surface);
	buffer->completed_l_added = false;
	comp_debug("del dma buf complete listener end");
}

static void head_changed_cb(struct cb_listener *listener, void *data)
{
	struct cb_output *o = container_of(listener, struct cb_output,
					   conn_st_chg_changed_l);
	struct head *head = o->head;
	struct output *output = o->output;
	struct cb_compositor *c = o->c;
	s32 vid;

	if (o->c->disable_head_detect)
		return;

	printf("[output: %d] conncted: %s\n", o->pipe,
		head->connected ? "Y" : "N");
	comp_notice("[output: %d] conncted: %s", o->pipe,
		head->connected ? "Y" : "N");

	if (head->connected) {
		printf("Enable output: %d\n", o->pipe);
		comp_notice("Enable output: %d", o->pipe);
		output->enable(output, NULL);
		o->enabled = true;
		/* update view port before show dummy */
		update_crtc_view_port(o);
		show_dummy(o);
		o->native_surface = output->native_surface_create(output);
		if (!o->native_surface) {
			comp_err("failed to create native surface");
			assert(o->native_surface);
		}
		o->ro = c->r->output_create(c->r, NULL, o->native_surface,
					    (s32 *)(&c->native_fmt), 1, &vid,
					    &o->desktop_rc,
					    o->crtc_w, o->crtc_h, o->pipe);
		if (!o->ro) {
			comp_err("failed to create renderer output");
			assert(o->ro);
		}
		printf("output %d is enabled. repaint_status: %d.\n",
			o->pipe, o->repaint_status);
		comp_notice("output %d is enabled. repaint_status: %d",
			    o->pipe, o->repaint_status);
		/*
		 * force to repaint all surface.
		 * surface buffer is released.
		 */
		o->renderable_buffer_changed = true;
		/* scanout_pending_dma_buf_surface(c); */
	} else {
		/* update view port when connector is disconnected */
		update_crtc_view_port(o);
		o->disable_pending = true;
		o->enabled = false;
		printf("Try to disable output: %d\n", o->pipe);
		comp_notice("Try to disable output: %d", o->pipe);
		if (output->disable(output) < 0) {
			cb_event_source_timer_update(o->disable_timer,
						     OUTPUT_DISABLE_DELAYED_MS,
						     OUTPUT_DISABLE_DELAYED_US);
		} else {
			disable_output_render(o);
			o->disable_pending = false;
			printf("output %d is disabled. repaint_status: %d\n",
				o->pipe, o->repaint_status);
			comp_debug("output %d is disabled. repaint_status: %d",
				   o->pipe, o->repaint_status);
			o->repaint_status = REPAINT_NOT_SCHEDULED;
			cancel_so_tasks(o);
		}
	}

	cb_event_source_timer_update(o->conn_st_chg_db_timer,
				     CONN_STATUS_DB_TIME, 0);
}

static void switch_mode_cb(struct cb_listener *listener, void *data)
{
	struct cb_output *output;

	output = container_of(listener, struct cb_output, switch_mode_l);
	comp_notice("[output: %d] Switch mode complete.", output->pipe);
	printf("[output: %d] Switch mode complete.\n", output->pipe);
	/* TODO notify clients about timing change event */
}

static s32 conn_st_chg_db_timer_cb(void *data)
{
	struct cb_output *o = data;
	bool last_state = o->conn_st_db;

	if (o->head->connected != last_state) {
		o->conn_st_db = o->head->connected;
		comp_notice("[output: %d] connector debounced status: %s",
			    o->pipe, o->conn_st_db ? "Y" : "N");
		printf("[output: %d] connector debounced status: %s\n",
			    o->pipe, o->conn_st_db ? "Y" : "N");
		cb_signal_emit(&o->conn_st_chg_db_signal, NULL);
	}
	cb_event_source_timer_update(o->conn_st_chg_db_timer, 0, 0);

	return 0;
}

static s32 suspend_timer_cb(void *data)
{
	struct cb_compositor *c = data;

	if (c->disable_all_pending) {
		cb_event_source_timer_update(c->suspend_timer,
					     DISABLE_ALL_DELAYED_MS,
					     DISABLE_ALL_DELAYED_US);
	} else {
		c->suspend_pending = false;
	}
	return 0;
}

static s32 output_disable_timer_cb(void *data)
{
	struct cb_output *o = data;
	struct cb_compositor *c = o->c;
	struct output *output = o->output;
	s32 vid;

	printf("Try to disable output: %d\n", o->pipe);
	comp_notice("Try to disable output: %d", o->pipe);
	if (output->disable(output) < 0) {
		cb_event_source_timer_update(o->disable_timer,
					     OUTPUT_DISABLE_DELAYED_MS,
					     OUTPUT_DISABLE_DELAYED_US);
	} else {
		if (o->switch_mode_pending && o->pending_mode) {
			disable_output_render(o);
			output->switch_mode(output, o->pending_mode);
			output->enable(output, o->pending_mode);
			o->repaint_status = REPAINT_NOT_SCHEDULED;
			cancel_so_tasks(o);
			o->enabled = true;
			/* update view port before show dummy */
			update_crtc_view_port(o);
			show_dummy(o);
			o->native_surface = output->native_surface_create(
						output);
			if (!o->native_surface) {
				comp_err("failed to create native surface");
				assert(o->native_surface);
			}
			o->ro = c->r->output_create(c->r, NULL,
						    o->native_surface,
						    (s32 *)(&c->native_fmt),
						    1, &vid,
						    &o->desktop_rc,
						    o->crtc_w, o->crtc_h,
						    o->pipe);
			if (!o->ro) {
				comp_err("failed to create renderer output");
				assert(o->ro);
			}
			o->switch_mode_pending = false;
			o->pending_mode = NULL;
			cb_signal_emit(&o->switch_mode_signal, NULL);
		} else {
			disable_output_render(o);
			printf("output %d is disabled. repaint_status: %d\n",
				o->pipe, o->repaint_status);
			comp_debug("output %d is disabled. repaint_status: %d",
				o->pipe, o->repaint_status);
			o->repaint_status = REPAINT_NOT_SCHEDULED;
			cancel_so_tasks(o);
			/* update view port when output is disabled. */
			update_crtc_view_port(o);
		}
		o->disable_pending = false;
		if (o->c->disable_all_pending)
			o->c->disable_all_pending--;
	}
	
	return 0;
}

static s32 get_primary_and_cursor_plane(struct cb_output *output)
{
	struct output *o = output->output;
	struct plane *last_plane = NULL, *plane;
	s32 i, ret = 0;

	do {
		plane = o->enumerate_plane(o, last_plane);
		last_plane = plane;
		if (plane) {
			switch (plane->type) {
			case PLANE_TYPE_PRIMARY:
				output->primary_plane = NULL;
				for (i = 0; i < plane->count_formats; i++) {
					if (!memcmp((char *)&plane->formats[i],
						    "XR24", 4))
						output->primary_plane = plane;
				}
				if (!output->primary_plane) {
					comp_err("Primary plane do not support "
						 "XRGB8888 pixel format");
					ret = -EINVAL;
				}
				break;
			case PLANE_TYPE_CURSOR:
				output->cursor_plane = NULL;
				for (i = 0; i < plane->count_formats; i++) {
					if (!memcmp((char *)&plane->formats[i],
						    "AR24", 4))
						output->cursor_plane = plane;
				}
				if (!output->cursor_plane) {
					comp_err("Cursor plane do not support "
						 "ARGB8888 pixel format");
					ret = -EINVAL;
				}
				break;
			default:
				break;
			}
		}
	} while (plane);

	return ret;
}

static void update_repaint_timer(struct cb_compositor *c)
{
	struct timespec now;
	struct cb_output *o;
	bool any_should_repaint = false;
	s64 msec_to_next = INT64_MAX;
	s64 msec_to_this;
	s32 i;

	clock_gettime(c->clock_type, &now);
	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->repaint_status != REPAINT_SCHEDULED)
			continue;
		if (!o->enabled) {
			printf("output %d is not enabled.\n", o->pipe);
			comp_warn("output %d is not enabled.", o->pipe);
			o->repaint_status = REPAINT_NOT_SCHEDULED;
			continue;
		}
		if (!o->head->connected) {
			printf("output %d is not connected.\n", o->pipe);
			comp_warn("output %d is not connected.", o->pipe);
			o->repaint_status = REPAINT_NOT_SCHEDULED;
			continue;
		}
		msec_to_this = timespec_sub_to_msec(&o->next_repaint, &now);
		if (!any_should_repaint || msec_to_this < msec_to_next)
			msec_to_next = msec_to_this;
		any_should_repaint = true;
	}

	if (!any_should_repaint) {
		return;
	}

	if (msec_to_next < 1)
		msec_to_next = 1;

	/* update repaint timer */
	comp_debug("update repaint timer %ld ms", msec_to_next);
	cb_event_source_timer_update(c->repaint_timer, msec_to_next, 0);
}

static void schedule_repaint(struct cb_output *o, struct timespec *last)
{
	struct timespec now;
	struct output *output = o->output;
	s64 msec_rel;

	if (o->repaint_status != REPAINT_WAIT_COMPLETION) {
		printf("output %d's repaint status: %d\n", o->pipe,
			o->repaint_status);
		comp_err("output %d's repaint status: %d", o->pipe,
			 o->repaint_status);
	}
	assert(o->repaint_status == REPAINT_WAIT_COMPLETION);
	clock_gettime(o->c->clock_type, &now);
	if (!last) {
		o->next_repaint = now;
		goto direct_repaint;
	}
/*
	last.tv_sec = output->sec;
	last.tv_nsec = output->usec * 1000l;
*/

	timespec_add_nsec(&o->next_repaint, last, output->refresh_nsec);
	timespec_add_msec(&o->next_repaint, &o->next_repaint, -7);

	msec_rel = timespec_sub_to_msec(&o->next_repaint, &now);
	if (msec_rel < -1000 || msec_rel > 1000) {
		comp_warn("[OUTPUT: %u] repaint delay is insane: %ld msec",
			  output->index, msec_rel);
		o->next_repaint = now;
	}

	if (msec_rel < 0) {
		comp_warn("[OUTPUT: %u] msec_rel < 0 %ld, next: %ld, %ld "
			  "now: %ld, %ld",
			  output->index, msec_rel, o->next_repaint.tv_sec,
			  o->next_repaint.tv_nsec / 1000000l,
			  now.tv_sec, now.tv_nsec / 1000000l);
		while (timespec_sub_to_nsec(&o->next_repaint, &now) < 0) {
			timespec_add_nsec(&o->next_repaint,
					  &o->next_repaint,
					  output->refresh_nsec);
		}
	}

/*
	comp_debug("[OUTPUT: %u] msec_rel: %ld, next_repaint: %ld, %ld",
		   output->index, msec_rel, o->next_repaint.tv_sec,
		   o->next_repaint.tv_nsec / 1000000l);
*/
direct_repaint:
	o->repaint_status = REPAINT_SCHEDULED;
	update_repaint_timer(o->c);
}

static void output_flipped_cb(struct cb_listener *listener, void *data)
{
	struct timespec last;
	struct cb_output *o = container_of(listener, struct cb_output,
					   output_flipped_l);
	struct output *output = o->output;

	
	comp_debug("--------------- OUTPUT %d flipped ---------------",
		   o->pipe);
	if (!o->primary_renderer_disabled) {
		if (o->primary_renderer_disable_pending) {
			o->primary_renderer_disable_pending = false;
			o->primary_renderer_disabled = true;
			printf("Switch to output %d virtual primary flipped\n",
			       o->pipe);
			comp_warn("Switch to output %d virtual primary flipped",
				  o->pipe);
		}
		cb_signal_emit(&o->surface_flipped_signal, NULL);
	}

	last.tv_sec = output->sec;
	last.tv_nsec = output->usec * 1000l;

	schedule_repaint(o, &last);
}

static s32 vflipped_timer_cb(void *data)
{
	struct cb_output *o = data;

	comp_debug("--------------- OUTPUT %d vflipped ---------------",
		   o->pipe);
	if (o->primary_renderer_disabled) {
		if (o->primary_renderer_enable_pending) {
			o->primary_renderer_enable_pending = false;
			o->primary_renderer_disabled = false;
			printf("Switch to output %d real primary flipped\n",
			       o->pipe);
			comp_warn("Switch to output %d real primary flipped",
				  o->pipe);
		}
		cb_signal_emit(&o->surface_flipped_signal, NULL);
		/*
		 * force to repaint all surface.
		 * (surface buffer may be released)
		 */
		o->renderable_buffer_changed = true;
	}
	o->vflipped_pending = false;

	return 0;
}

static struct cb_output *cb_output_create(struct cb_compositor *c,
					  struct pipeline *pipecfg)
{
	struct cb_output *output = NULL;
	struct scanout *so;
	struct cb_buffer_info info;
	s32 vid, i;

	if (!c || !pipecfg)
		goto err;

	output = calloc(1, sizeof(*output));
	output->c = c;
	output->pipe = pipecfg->output_index;

	INIT_LIST_HEAD(&output->so_tasks);

	/* create scanout pipeline */
	so = c->so;
	output->output = so->pipeline_create(so, pipecfg);
	if (!output->output)
		goto err;
	output->head = output->output->head;

	/* select the primary plane and cursor plane */
	if (get_primary_and_cursor_plane(output) < 0)
		goto err;

	/* prepare free planes list */
	INIT_LIST_HEAD(&output->free_planes);
	output_free_planes_prepare(output);
	show_free_planes(output);
	output->primary_occupied_by_renderer = false;

	/* prepare mc buffer */
	memset(&info, 0, sizeof(info));
	info.pix_fmt = CB_PIX_FMT_ARGB8888;
	info.width = MC_MAX_WIDTH;
	info.height = MC_MAX_HEIGHT;
	for (i = 0; i < 2; i++) {
		output->mc_buf[i] = so->cursor_bo_create(so, &info);
		if (!output->mc_buf[i])
			goto err;

		fill_cursor(c, output->mc_buf[i],
			    DEF_MC_DAT, DEF_MC_WIDTH, DEF_MC_HEIGHT,
			    (DEF_MC_WIDTH << 2));
	}

	output->mc_buf_cur = 0;

	/* prepare dummy buffer */
	memset(&info, 0, sizeof(info));
	info.pix_fmt = CB_PIX_FMT_XRGB8888;
	info.width = DUMMY_WIDTH;
	info.height = DUMMY_HEIGHT;
	output->dummy = so->dumb_buffer_create(so, &info);
	if (!output->dummy)
		goto err;

	/* fill dummy buffer with color black */
	fill_dummy(output->dummy->info.maps[0], output->dummy->info.width,
		   output->dummy->info.height, output->dummy->info.strides[0]);

	/* register output's page flip handler */
	output->output_flipped_l.notify = output_flipped_cb;
	INIT_LIST_HEAD(&output->output_flipped_l.link);
	output->output->add_page_flip_notify(output->output,
					     &output->output_flipped_l);

	/* register dummy page flip handler */
	output->dummy_flipped_l.notify = dummy_flipped_cb;
	INIT_LIST_HEAD(&output->dummy_flipped_l.link);
	so->add_buffer_flip_notify(so, output->dummy, &output->dummy_flipped_l);

	/* set desktop size as DUMMY SIZE */
	/* duplicated */
	output->desktop_rc.pos.x = 0;
	output->desktop_rc.pos.y = 0;
	output->desktop_rc.w = DUMMY_WIDTH;
	output->desktop_rc.h = DUMMY_HEIGHT;
	output->g_desktop_rc.pos.x = 0;
	output->g_desktop_rc.pos.y = 0;
	output->g_desktop_rc.w = GLOBAL_DESKTOP_SZ;
	output->g_desktop_rc.h = GLOBAL_DESKTOP_SZ;

	/* prepare disable timer */
	output->disable_pending = false;
	output->disable_timer = cb_event_loop_add_timer(c->loop,
							output_disable_timer_cb,
							output);
	if (!output->disable_timer)
		goto err;

	/* prepare for mode switch */
	cb_signal_init(&output->switch_mode_signal);
	output->switch_mode_l.notify = switch_mode_cb;
	output->switch_mode_pending = false;
	output->pending_mode = NULL;
	cb_signal_add(&output->switch_mode_signal, &output->switch_mode_l);

	/* prepare head status changed event */
	cb_signal_init(&output->conn_st_chg_db_signal);
	output->conn_st_chg_db_timer = cb_event_loop_add_timer(c->loop,
							conn_st_chg_db_timer_cb,
							output);
	if (!output->conn_st_chg_db_timer)
		goto err;

	output->conn_st_chg_changed_l.notify = head_changed_cb;
	output->head->add_head_changed_notify(output->head,
					      &output->conn_st_chg_changed_l);

	output->primary_vflipped_timer = cb_event_loop_add_timer(c->loop,
							vflipped_timer_cb,
							output);
	if (!output->primary_vflipped_timer)
		goto err;

	/* draw dummy buffer if monitor is connected */
	if (output->head->connected) {
		/* use preferred mode */
		output->output->enable(output->output, NULL);
		/*
		 * update crtc view port according to crtc size and desktop
		 * size
		 */
		update_crtc_view_port(output);
		output->enabled = true;
		show_dummy(output);
		output->native_surface = output->output->native_surface_create(
						output->output);
		if (!output->native_surface) {
			comp_err("failed to create native surface");
			assert(output->native_surface);
		}
		output->ro = c->r->output_create(c->r, NULL,
					output->native_surface,
					(s32 *)(&c->native_fmt), 1, &vid,
					&output->desktop_rc,
					output->crtc_w, output->crtc_h,
					output->pipe);
		if (!output->ro) {
			comp_err("failed to create renderer output");
			assert(output->ro);
		}
		/* set initial debounced head status as 'plug in' */
		output->conn_st_db = true;
	} else {
		/* update view port when connector is disconnected */
		update_crtc_view_port(output);
		output->enabled = false;
		/* set initial debounced head status as 'plug out' */
		output->conn_st_db = false;
	}
	/* start to debounce initial head status */
	cb_event_source_timer_update(output->conn_st_chg_db_timer,
				     CONN_STATUS_DB_TIME, 0);

	/* init surface buffer flipped signal */
	cb_signal_init(&output->surface_flipped_signal);

	return output;
err:
	if (output)
		cb_output_destroy(output);

	return NULL;
}

static void cb_compositor_resume(struct compositor *comp)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	struct cb_mode *mode = NULL;
	s32 i, vid;

	if (!comp)
		return;

	/* enable head detect */
	c->disable_head_detect = false;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (!o)
			continue;
		if (o->head->connected) {
			if (o->mode_saved) {
				/* use backuped mode */
				do {
					mode = o->output->enumerate_mode(
							o->output, mode);
					if (!mode)
						break;
					if (mode->width == o->mode_save.width
					      &&
					    mode->height == o->mode_save.height
					      &&
					    mode->vrefresh
					      == o->mode_save.vrefresh) {
						o->mode_saved = false;
						break;
					}
				} while (mode);
				o->output->enable(o->output, mode);
			} else {
				/* use preferred mode */
				o->output->enable(o->output, NULL);
			}
			/*
			 * update crtc view port according to crtc size and
			 * desktop size
			 */
			update_crtc_view_port(o);
			o->enabled = true;
			show_dummy(o);
			o->native_surface = o->output->native_surface_create(
							o->output);
			if (!o->native_surface) {
				comp_err("failed to create native surface");
				assert(o->native_surface);
			}
			o->ro = c->r->output_create(c->r, NULL,
						o->native_surface,
						(s32 *)(&c->native_fmt), 1,
						&vid,
						&o->desktop_rc,
						o->crtc_w, o->crtc_h, o->pipe);
			if (!o->ro) {
				comp_err("failed to create renderer output");
				assert(o->ro);
			}
			/* set initial debounced head status as 'plug in' */
			o->conn_st_db = true;
		} else {
			/* update view port when connector is disconnected */
			update_crtc_view_port(o);
			o->enabled = false;
			/* set initial debounced head status as 'plug out' */
			o->conn_st_db = false;
		}
		/* start to debounce initial head status */
		cb_event_source_timer_update(o->conn_st_chg_db_timer,
					     CONN_STATUS_DB_TIME, 0);
	}
	/* TODO notify clients about the initial connector status */
}

static s32 cb_compositor_switch_mode(struct compositor *comp, s32 pipe,
				     struct cb_mode *mode)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	s32 vid, i;

	if (!comp || !mode || pipe < 0)
		return -EINVAL;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->pipe == pipe)
			break;
	}

	if (i == c->count_outputs)
		return -EINVAL;

	if (o->disable_pending)
		return -EAGAIN;

	if (o->switch_mode_pending)
		return -EINVAL;

	o->disable_pending = true;
	o->switch_mode_pending = true;
	o->pending_mode = mode;
	o->enabled = false;
	printf("Try to disable output: %d\n", o->pipe);
	comp_notice("Try to disable output: %d", o->pipe);
	if (o->output->disable(o->output) < 0) {
		cb_event_source_timer_update(o->disable_timer,
					     OUTPUT_DISABLE_DELAYED_MS,
					     OUTPUT_DISABLE_DELAYED_US);
	} else {
		disable_output_render(o);
		o->output->switch_mode(o->output, mode);
		o->output->enable(o->output, mode);
		o->repaint_status = REPAINT_NOT_SCHEDULED;
		cancel_so_tasks(o);
		o->enabled = true;
		/* update view port before show dummy */
		update_crtc_view_port(o);
		show_dummy(o);
		o->native_surface = o->output->native_surface_create(o->output);
		if (!o->native_surface) {
			comp_err("failed to create native surface");
			assert(o->native_surface);
		}
		o->ro = c->r->output_create(c->r, NULL, o->native_surface,
					    (s32 *)(&c->native_fmt), 1, &vid,
					    &o->desktop_rc,
					    o->crtc_w, o->crtc_h, o->pipe);
		if (!o->ro) {
			comp_err("failed to create renderer output");
			assert(o->ro);
		}
		o->pending_mode = NULL;
		o->switch_mode_pending = false;
		o->disable_pending = false;
		cb_signal_emit(&o->switch_mode_signal, NULL);
	}

	return 0;
}

static s32 cb_compositor_register_head_status_cb(struct compositor *comp,
						 s32 pipe,
						 struct cb_listener *l)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	s32 i;

	if (!comp || pipe < 0 || !l)
		return -EINVAL;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->pipe == pipe)
			break;
	}

	if (i == c->count_outputs)
		return -EINVAL;

	cb_signal_add(&o->conn_st_chg_db_signal, l);
	return 0;
}

static bool cb_compositor_get_head_status(struct compositor *comp, s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	s32 i;

	if (!comp || pipe < 0)
		return -EINVAL;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->pipe == pipe)
			break;
	}

	if (i == c->count_outputs)
		return -EINVAL;

	return o->conn_st_db;
}

static s32 cb_compositor_retrieve_edid(struct compositor *comp, s32 pipe,
				       u8 *data, size_t *length)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	s32 i;

	if (!comp || pipe < 0 || !data || !length)
		return -EINVAL;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->pipe == pipe)
			break;
	}

	if (i == c->count_outputs)
		return -EINVAL;

	return o->head->retrieve_edid(o->head, data, length);
}

static const char *cb_compositor_get_connector_name(struct compositor *comp,
						    s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	s32 i;

	if (!comp || pipe < 0)
		return NULL;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->pipe == pipe)
			break;
	}

	if (i == c->count_outputs)
		return NULL;

	return o->head->connector_name;
}

static const char *cb_compositor_get_monitor_name(struct compositor *comp,
						  s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	s32 i;

	if (!comp || pipe < 0)
		return NULL;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->pipe == pipe)
			break;
	}

	if (i == c->count_outputs)
		return NULL;

	return o->head->monitor_name;
}

static struct cb_mode *cb_compositor_get_custom_timing(struct compositor *comp,
						       s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	s32 i;

	if (!comp || pipe < 0)
		return NULL;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->pipe == pipe)
			break;
	}

	if (i == c->count_outputs)
		return NULL;

	return o->output->get_custom_mode(o->output);
}

static struct cb_mode *cb_compositor_get_current_timing(struct compositor *comp,
							s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	s32 i;

	if (!comp || pipe < 0)
		return NULL;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->pipe == pipe)
			break;
	}

	if (i == c->count_outputs)
		return NULL;

	return o->output->get_current_mode(o->output);
}

static struct cb_mode *cb_compositor_enumerate_timing(struct compositor *comp,
						      s32 pipe,
						      struct cb_mode *last_mode,
						      struct cb_mode_filter *f)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	struct cb_mode *mode, *prev;
	s32 i;

	if (!comp || pipe < 0)
		return NULL;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->pipe == pipe)
			break;
	}

	if (i == c->count_outputs)
		return NULL;

	prev = last_mode;

retry:
	mode = o->output->enumerate_mode(o->output, prev);
	if (f) {
		switch (f->mode) {
		case CB_MODE_FILTER_MODE_SIZE_OR_CLOCK:
			if (((mode->width >= f->min_width) &&
			     (mode->width <= f->max_width) &&
			     (mode->height >= f->min_height) &&
			     (mode->height <= f->max_height)) ||
			    ((mode->pixel_freq >= f->min_clock) &&
			     (mode->pixel_freq <= f->max_clock))) {
				return mode;
			}
			break;
		case CB_MODE_FILTER_MODE_SIZE_AND_CLOCK:
			if (((mode->width >= f->min_width) &&
			     (mode->width <= f->max_width) &&
			     (mode->height >= f->min_height) &&
			     (mode->height <= f->max_height)) &&
			    ((mode->pixel_freq >= f->min_clock) &&
			     (mode->pixel_freq <= f->max_clock))) {
				return mode;
			}
			break;
		default:
			break;
		}
		prev = mode;
		goto retry;
	}

	return mode;
}

static struct cb_mode *cb_compositor_create_custom_timing(
		struct compositor *comp,
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
		char *mode_name)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	s32 i;

	if (!comp || pipe < 0)
		return NULL;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->pipe == pipe)
			break;
	}

	if (i == c->count_outputs)
		return NULL;

	return o->output->create_custom_mode(o->output,
					     clock,
					     width,
					     hsync_start,
					     hsync_end,
					     htotal,
					     hskew,
					     height,
					     vsync_start,
					     vsync_end,
					     vtotal,
					     vscan,
					     vrefresh,
					     interlaced,
					     pos_hsync,
					     pos_vsync,
					     mode_name);
}

static void idle_repaint(void *data)
{
	struct cb_output *output = data;
	struct timespec ts, now, vbl2now;
	s32 ret;

	assert(output->repaint_status == REPAINT_START_FROM_IDLE);
	ret = output->output->query_vblank(output->output, &ts);
	if (!ret) {
		clock_gettime(output->c->clock_type, &now);
		timespec_sub(&vbl2now, &now, &ts);
		if (timespec_to_nsec(&vbl2now) < output->output->refresh_nsec) {
			output->repaint_status = REPAINT_WAIT_COMPLETION;
			schedule_repaint(output, &ts);
			return;
		}
	}

	comp_warn("cannot query vblank from scanout backend, schedule now.");
	output->repaint_status = REPAINT_WAIT_COMPLETION;
	output->idle_repaint_source = NULL;
	schedule_repaint(output, NULL);
}

static void cb_compositor_repaint_by_output(struct cb_output *output)
{
	if (output->repaint_status != REPAINT_NOT_SCHEDULED) {
		return;
	}

	if (!output->enabled)
		return;

	output->idle_repaint_source = cb_event_loop_add_idle(output->c->loop,
							     idle_repaint,
							     output);

	output->repaint_status = REPAINT_START_FROM_IDLE;
}

static void cb_compositor_repaint(struct cb_compositor *c)
{
	s32 i = 0;

	for (i = 0; i < c->count_outputs; i++) {
		if (c->outputs[i]->enabled) {
			cb_compositor_repaint_by_output(c->outputs[i]);
		}
	}
}

static void cb_compositor_get_desktop_layout(struct compositor *comp,
					     struct cb_canvas_layout *layout)
{
	s32 i;
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;
	struct cb_mode *mode;
	const char *name;

	if (!layout)
		return;

	layout->count_heads = c->count_outputs;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		layout->cfg[i].pipe = o->pipe;
		if (o->switch_mode_pending && o->pending_mode)
			layout->cfg[i].mode_handle = o->pending_mode;
		else
			layout->cfg[i].mode_handle
				= cb_compositor_get_current_timing(
					comp, o->pipe);
		layout->cfg[i].custom_mode_handle =
				cb_compositor_get_custom_timing(
					comp, o->pipe);
		memcpy(&layout->cfg[i].desktop_rc, &o->desktop_rc,
			sizeof(struct cb_rect));
		memcpy(&layout->cfg[i].input_rc, &o->g_desktop_rc,
			sizeof(struct cb_rect));
		name = comp->get_monitor_name(comp, o->pipe);
		if (name)
			strncpy(layout->cfg[i].monitor_name,
				name,
				CB_MONITOR_NAME_MAX_LEN - 1);
		else
			memset(layout->cfg[i].monitor_name, 0,
			       CB_MONITOR_NAME_MAX_LEN);
		name = comp->get_connector_name(comp, o->pipe);
		if (name)
			strncpy(layout->cfg[i].connector_name,
				name,
				CB_CONNECTOR_NAME_MAX_LEN - 1);
		else
			memset(layout->cfg[i].connector_name, 0,
			       CB_CONNECTOR_NAME_MAX_LEN);
		mode = o->output->get_preferred_mode(o->output);
		if (mode) {
			layout->cfg[i].width_preferred = mode->width;
			layout->cfg[i].height_preferred = mode->height;
			layout->cfg[i].vrefresh_preferred = mode->vrefresh;
			layout->cfg[i].pixel_freq_preferred = mode->pixel_freq;
		}
	}
}

static void broadcast_layout_changed_event(struct cb_compositor *c)
{
	struct cb_client_agent *client, *next_client;
	struct cb_shell_info shell;

	memset(&shell, 0, sizeof(shell));
	shell.cmd = CB_SHELL_CANVAS_LAYOUT_CHANGED_NOTIFY;
	cb_compositor_get_desktop_layout(&c->base, &shell.value.layout);

	list_for_each_entry_safe(client, next_client, &c->clients, link) {
		if (client->capability & CB_CLIENT_CAP_NOTIFY_LAYOUT) {
			client->send_shell_cmd(client, &shell);
		}
	}
}

static void cb_compositor_set_desktop_layout(struct compositor *comp,
					     struct cb_canvas_layout *layout)
{
	s32 i, j, pipe, ret;
	struct cb_rect *rc_src, *rc_dst, *rc_src_input, *rc_dst_input;
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;

	for (i = 0; i < layout->count_heads; i++) {
		pipe = layout->cfg[i].pipe;
		for (j = 0; j < c->count_outputs; j++) {
			o = c->outputs[j];
			if (o->pipe != pipe)
				continue;
			rc_dst = &o->desktop_rc;
			rc_src = &layout->cfg[i].desktop_rc;
			rc_dst_input = &o->g_desktop_rc;
			rc_src_input = &layout->cfg[i].input_rc;
			if (!rc_src->w || !rc_src->h) {
				memset(rc_dst, 0, sizeof(*rc_dst));
			} else {
				memcpy(rc_dst, rc_src, sizeof(*rc_dst));
			}
			printf("\t----- desktop[%d]: %d,%d %ux%u\n", pipe,
				rc_dst->pos.x, rc_dst->pos.y,
				rc_dst->w, rc_dst->h);
			if (!rc_src_input->w || !rc_src_input->h) {
				memset(rc_dst_input, 0, sizeof(*rc_dst_input));
			} else {
				memcpy(rc_dst_input, rc_src_input,
				       sizeof(*rc_dst_input));
			}
			printf("\t----- input[%d]: %d,%d %ux%u\n", pipe,
				rc_dst_input->pos.x, rc_dst_input->pos.y,
				rc_dst_input->w, rc_dst_input->h);
			update_crtc_view_port(o);
			/* update renderer's layout */
			if (o->enabled) {
				o->ro->layout_changed(o->ro, rc_dst,
					      o->crtc_w, o->crtc_h);
			}
			if (layout->cfg[i].mode_handle) {
				comp_notice("client request to switch mode");
				ret = cb_compositor_switch_mode(comp, pipe,
						layout->cfg[i].mode_handle);
				if (ret) {
					comp_err("failed to switch mode");
				}
			}
			break;
		}
	}
	cb_compositor_repaint(c);

	/* notify all client about the change */
	broadcast_layout_changed_event(c);
}

static s32 cb_compositor_set_mouse_cursor(struct compositor *comp,
					  u8 *data, u32 width, u32 height,
					  u32 stride,
					  s32 hot_x, s32 hot_y,
					  bool alpha_src_pre_mul)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_buffer *buffer;
	struct cb_output *o;
	s32 i, work_index;

	if (!data || !width || width > MC_MAX_WIDTH || !height ||
	    height > MC_MAX_HEIGHT || !stride || !comp) {
		comp_err("illegal mc param");
		return -EINVAL;
	}

	c->mc_hot_pos.x = hot_x;
	c->mc_hot_pos.y = hot_y;
	c->mc_alpha_src_pre_mul = alpha_src_pre_mul;
	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		work_index = 1 - o->mc_buf_cur;
		buffer = o->mc_buf[work_index];
		scanout_buffer_dirty_init(buffer);
		fill_cursor(c, buffer, data, width, height, stride);
		o->mc_damaged = true;
		update_mc_view_port(o, false);
		cb_compositor_repaint_by_output(o);
	}

	return 0;
}

#define LONG_BITS (sizeof(long) * 8)
#define NLONGS(x) (((x) + LONG_BITS - 1) / LONG_BITS)

static enum input_type test_dev(const char *dev)
{
	s32 fd;
	u8 evbit[EV_MAX/8 + 1];
	char buffer[64];
	u32 i;
	u64 props[NLONGS(INPUT_PROP_CNT)];

	comp_notice("begin test %s", dev);
	fd = open(dev, O_RDWR | O_CLOEXEC, 0644);
	if (fd < 0)
		return INPUT_TYPE_UNKNOWN;

	memset(buffer,0, sizeof(buffer));
	ioctl(fd, EVIOCGNAME(sizeof(buffer) - 1), buffer);
	comp_notice("Test input device %s", buffer);

#ifndef test_bit
#define test_bit(bit, array)    (array[bit/8] & (1<<(bit%8)))
#endif
	ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit);

	if (test_bit(EV_ABS, evbit)) {
		memset(props, 0, sizeof(props));
		if (ioctl(fd, EVIOCGPROP(sizeof(props)), props) < 0) {
			comp_err("failed to get device's properties. (%s)",
				 strerror(errno));
		}
	}

	close(fd);

	for (i = 0; i < EV_MAX; i++) {
		if (test_bit(i, evbit)) {
			switch (i) {
			case EV_KEY:
				comp_notice("cap: key");
				break;
			case EV_REL:
				comp_notice("cap: rel");
				break;
			case EV_ABS:
				comp_notice("cap: abs");
				break;
			case EV_MSC:
				comp_notice("cap: msc");
				break;
			case EV_LED:
				comp_notice("cap: led");
				break;
			case EV_SND:
				comp_notice("cap: sound");
				break;
			case EV_REP:
				comp_notice("cap: repeat");
				break;
			case EV_FF:
				comp_notice("cap: feedback");
				break;
			case EV_SYN:
				comp_notice("cap: sync");
				break;
			}
		}
	}

	if (!strcmp(buffer, SW_KBD_NAME)) {
		comp_notice("device %s (%s) is software kbd", buffer, dev);
		return INPUT_TYPE_KBD_LED_VDEV;
	}
	
	if (test_bit(EV_KEY, evbit) && test_bit(EV_REL, evbit)
			&& test_bit(EV_SYN, evbit)) {
		comp_notice("device %s (%s) is mouse", buffer, dev);
		return INPUT_TYPE_MOUSE;
	} else if (test_bit(EV_KEY, evbit) && test_bit(EV_REP, evbit)
	    && test_bit(EV_LED, evbit)) {
		comp_notice("device %s (%s) is keyboard", buffer, dev);
		return INPUT_TYPE_KBD;
	} else if (test_bit(EV_KEY, evbit) && test_bit(EV_ABS, evbit) &&
		   test_bit(EV_FF, evbit)) {
		comp_notice("device %s (%s) is joystick", buffer, dev);
		return INPUT_TYPE_JOYSTICK;
	} else if (test_bit(EV_ABS, evbit)) {
		if (!!(props[INPUT_PROP_DIRECT / LONG_BITS]
				& (1LL << (INPUT_PROP_DIRECT % LONG_BITS)))) {
			comp_notice("device %s (%s) is touch", buffer, dev);
			return INPUT_TYPE_TOUCH;
		}
	}

	comp_notice("end test %s", dev);

	return INPUT_TYPE_UNKNOWN;
}

#define CURSOR_ACCEL_THRESHOLD_A 20
#define CURSOR_ACCEL_THRESHOLD_B 30
#define CURSOR_ACCEL_THRESHOLD_C 40
#define CURSOR_ACCEL_THRESHOLD_D 50

static void cursor_accel_set(s32 *dx, s32 *dy, float factor)
{
	float fx, fy;

	if (factor <= 1.0f)
		return;

	fx = factor - 1.0f;

	if ((*dx >= CURSOR_ACCEL_THRESHOLD_D)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_D))
		fx = 1 + fx;
	else if ((*dx >= CURSOR_ACCEL_THRESHOLD_C)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_C))
		fx = 1 + fx / 4 * 3;
	else if ((*dx >= CURSOR_ACCEL_THRESHOLD_B)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_B))
		fx = 1 + fx / 4 * 2;
	else if ((*dx >= CURSOR_ACCEL_THRESHOLD_A)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_A))
		fx = 1 + fx / 4;

	*dx = *dx * fx;

	fy = factor - 1.0f;

	if ((*dx >= CURSOR_ACCEL_THRESHOLD_D)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_D))
		fy = 1 + fy;
	else if ((*dx >= CURSOR_ACCEL_THRESHOLD_C)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_C))
		fy = 1 + fy / 4 * 3;
	else if ((*dx >= CURSOR_ACCEL_THRESHOLD_B)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_B))
		fy = 1 + fy / 4 * 2;
	else if ((*dx >= CURSOR_ACCEL_THRESHOLD_A)
	    || (*dx <= -CURSOR_ACCEL_THRESHOLD_A))
		fy = 1 + fy / 4;

	*dy = *dy * fy;
}

static void reset_mouse_pos(struct cb_compositor *c)
{
	c->mc_desktop_pos.x = c->mc_desktop_pos.y = 0;
}

static void set_mc_desktop_pos(struct cb_compositor *c, s32 dx, s32 dy)
{
	s32 cur_screen, i;

	comp_debug(">>> dx: %d, dy: %d", dx, dy);
	cur_screen = check_mouse_pos(c, c->mc_desktop_pos.x,
				     c->mc_desktop_pos.y);
	if (cur_screen < 0) {
		reset_mouse_pos(c);
	} else {
		normalize_mouse_pos(c, cur_screen, dx, dy);
	}

	for (i = 0; i < c->count_outputs; i++) {
		if (!c->outputs[i]->enabled)
			continue;
		update_mc_view_port(c->outputs[i], true);
	}

	cb_compositor_repaint(c);
}

static s32 cb_compositor_hide_mouse_cursor(struct compositor *comp)
{
	struct cb_compositor *c = to_cb_c(comp);

	c->mc_hide = true;
	set_mc_desktop_pos(c, 0, 0);

	return 0;
}

static s32 cb_compositor_show_mouse_cursor(struct compositor *comp)
{
	struct cb_compositor *c = to_cb_c(comp);

	c->mc_hide = false;
	set_mc_desktop_pos(c, 0, 0);

	return 0;
}

static s32 tbtn_down_timeout_cb(void *userdata)
{
	struct input_device *dev = userdata;

	dev->tdrag_flag = true;
	return 0;
}

static s32 lbtn_down_timeout_cb(void *userdata)
{
	struct input_device *dev = userdata;

	dev->drag_flag = true;
	return 0;
}

static s32 lbtn_clk_timeout_cb(void *userdata)
{
	struct input_device *dev = userdata;

	dev->lbtn_clk_cnt = 0;
	return 0;
}

static s32 mbtn_clk_timeout_cb(void *userdata)
{
	struct input_device *dev = userdata;

	dev->mbtn_clk_cnt = 0;
	return 0;
}

static s32 rbtn_clk_timeout_cb(void *userdata)
{
	struct input_device *dev = userdata;

	dev->rbtn_clk_cnt = 0;
	return 0;
}

static void view_switch(struct cb_compositor *c, s32 x, s32 y)
{
	struct cb_view *top_view, *view, *v, *first_normal_view;
	struct cb_client_agent *client;

	top_view = c->top_view;
	/*
	 * Do not change top view if the current top view is DMA-BUF view.
	 */
	if (top_view && top_view->direct_show)
		return;

	/* Not in the top view's area, check other view's area */
	view = NULL;
	list_for_each_entry(v, &c->views, link) {
		if (v->float_view)
			continue;
		if (x >= v->area.pos.x &&
		    x < (v->area.pos.x + v->area.w) &&
		    y >= v->area.pos.y &&
		    y < (v->area.pos.y + v->area.h)) {
			/* in area */
			view = v;
			break;
		}
	}

	if (!view) {
		/* no desktop ? */
		return;
	}

	if (top_view != view) {
		comp_notice("New top view: %lX -> %lX", (u64)top_view,
			    (u64)view);
		client = view->surface->client_agent;
		client->send_view_focus_chg(client, view, true);
		client = top_view->surface->client_agent;
		if (top_view) {
			client->send_view_focus_chg(client, top_view, false);
		}
		if (!view->root_view) {
			list_del(&view->link);
			first_normal_view = NULL;
			list_for_each_entry(v, &c->views, link) {
				if (!v->float_view) {
					first_normal_view = v;
					break;
				}
			}
			if (first_normal_view) {
				list_add(&view->link,
					 first_normal_view->link.prev);
			} else {
				list_add_tail(&view->link, &c->views);
			}
		}
		c->top_view = view;
		cb_compositor_repaint(c);
	}
}

static void event_proc(struct input_device *dev, struct input_event *evts,
		       s32 cnt)
{
	struct cb_client_agent *client, *next_client;
	s32 src, dst, gm_dst;
	s32 dx, dy;
	struct cb_raw_input_event *tx_evt;
	struct cb_gui_input_msg *tx_gm;
	struct cb_compositor *c = dev->c;

	dx = dy = 0;
	dst = 0;
	gm_dst = 0;

	tx_evt = (struct cb_raw_input_event *)(c->raw_input_tx_buffer
					       + sizeof(u32)
					       + sizeof(struct cb_tlv));
	tx_gm = (struct cb_gui_input_msg *)(c->input_tx_buffer
						+ sizeof(u32)
						+ sizeof(struct cb_tlv));
	for (src = 0; src < cnt; src++) {
		switch (evts[src].type) {
		case EV_SYN:
			if (evts[src].value == 1)
				break;
			if (dx || dy) {
				cursor_accel_set(&dx, &dy, c->mc_accel);
				set_mc_desktop_pos(c, dx, dy);
				tx_evt[dst].type = EV_ABS;
				tx_evt[dst].code = ABS_X | ABS_Y;
				tx_evt[dst].v.pos.x = c->mc_g_desktop_pos.x;
				tx_evt[dst].v.pos.y = c->mc_g_desktop_pos.y;
				tx_evt[dst].v.pos.dx = (s16)dx;
				tx_evt[dst].v.pos.dy = (s16)dy;
				dst++;
			}

			tx_evt[dst].type = EV_SYN;
			tx_evt[dst].code = evts[src].code;
			tx_evt[dst].v.value = evts[src].value;

			dst++;

			if (dx || dy) {
				if (dev->drag_flag) {
					if (!dev->draging) {
						dev->draging = true;
						tx_gm[gm_dst].tag =
							CB_GUI_INP_DRAG_BEGIN;
						tx_gm[gm_dst].code =
							CB_GUI_MOUSE_BTN_LEFT;
						tx_gm[gm_dst].v.abs.x =
							c->mc_desktop_pos.x;
						tx_gm[gm_dst].v.abs.y =
							c->mc_desktop_pos.y;
						gm_dst++;
					}
					tx_gm[gm_dst].tag =
						CB_GUI_INP_DRAG;
					tx_gm[gm_dst].code =
						CB_GUI_MOUSE_BTN_LEFT;
					tx_gm[gm_dst].v.abs.x =
						c->mc_desktop_pos.x;
					tx_gm[gm_dst].v.abs.y =
						c->mc_desktop_pos.y;
					gm_dst++;
				}

				tx_gm[gm_dst].tag = CB_GUI_INP_MOUSE_MOVE;
				tx_gm[gm_dst].v.abs.x = c->mc_desktop_pos.x;
				tx_gm[gm_dst].v.abs.y = c->mc_desktop_pos.y;
				gm_dst++;
			}
			break;
		case EV_MSC:
			break;
		case EV_LED:
			break;
		case EV_KEY:
			tx_evt[dst].type = EV_KEY;
			tx_evt[dst].code = evts[src].code;
			tx_evt[dst].v.value = evts[src].value;
			dst++;
			switch (evts[src].code) {
			case BTN_LEFT:
				if (evts[src].value &&
						dev->type == INPUT_TYPE_MOUSE) {
					tx_gm[gm_dst].tag =
						CB_GUI_INP_MOUSE_BTN_DOWN;
					tx_gm[gm_dst].v.abs.x =
						c->mc_desktop_pos.x;
					tx_gm[gm_dst].v.abs.y =
						c->mc_desktop_pos.y;
					cb_event_source_timer_update(
						dev->lbtn_down_timer,
						LBTN_DRAG_TIMEOUT, 0);
					view_switch(c, c->mc_desktop_pos.x,
						    c->mc_desktop_pos.y);
				} else {
					if (dev->drag_flag) {
						dev->drag_flag = false;
						dev->draging = false;
						tx_gm[gm_dst].tag =
							CB_GUI_INP_DRAG_END;
						tx_gm[gm_dst].code =
							CB_GUI_MOUSE_BTN_LEFT;
						tx_gm[gm_dst].v.abs.x =
							c->mc_desktop_pos.x;
						tx_gm[gm_dst].v.abs.y =
							c->mc_desktop_pos.y;
						gm_dst++;
					}
					cb_event_source_timer_update(
							dev->lbtn_down_timer,
							0, 0);
					tx_gm[gm_dst].tag =
						CB_GUI_INP_MOUSE_BTN_UP;
					tx_gm[gm_dst].v.abs.x =
						c->mc_desktop_pos.x;
					tx_gm[gm_dst].v.abs.y =
						c->mc_desktop_pos.y;
					tx_gm[gm_dst].code =
						CB_GUI_MOUSE_BTN_LEFT;
					gm_dst++;
					dev->lbtn_clk_cnt++;
					if (dev->lbtn_clk_cnt == 2) {
						dev->lbtn_clk_cnt = 0;
						tx_gm[gm_dst].tag =
						    CB_GUI_INP_MOUSE_BTN_DCLK;
						tx_gm[gm_dst].v.abs.x =
							c->mc_desktop_pos.x;
						tx_gm[gm_dst].v.abs.y =
							c->mc_desktop_pos.y;
						tx_gm[gm_dst].code =
							CB_GUI_MOUSE_BTN_LEFT;
						gm_dst++;
						break;
					} else {
						cb_event_source_timer_update(
							dev->lbtn_clk_timer,
							DCLK_INTERVAL, 0);
					}
					tx_gm[gm_dst].tag =
						CB_GUI_INP_MOUSE_BTN_CLK;
					tx_gm[gm_dst].v.abs.x =
						c->mc_desktop_pos.x;
					tx_gm[gm_dst].v.abs.y =
						c->mc_desktop_pos.y;
					cb_event_source_timer_update(
						dev->lbtn_down_timer, 0, 0);
				}
				tx_gm[gm_dst].code = CB_GUI_MOUSE_BTN_LEFT;
				gm_dst++;
				break;
			case BTN_RIGHT:
				if (evts[src].value) {
					tx_gm[gm_dst].tag =
						CB_GUI_INP_MOUSE_BTN_DOWN;
					tx_gm[gm_dst].v.abs.x =
						c->mc_desktop_pos.x;
					tx_gm[gm_dst].v.abs.y =
						c->mc_desktop_pos.y;
				} else {
					tx_gm[gm_dst].tag =
						CB_GUI_INP_MOUSE_BTN_UP;
					tx_gm[gm_dst].v.abs.x =
						c->mc_desktop_pos.x;
					tx_gm[gm_dst].v.abs.y =
						c->mc_desktop_pos.y;
					tx_gm[gm_dst].code =
						CB_GUI_MOUSE_BTN_RIGHT;
					gm_dst++;
					dev->rbtn_clk_cnt++;
					if (dev->rbtn_clk_cnt == 2) {
						dev->rbtn_clk_cnt = 0;
						tx_gm[gm_dst].tag =
						    CB_GUI_INP_MOUSE_BTN_DCLK;
						tx_gm[gm_dst].v.abs.x =
							c->mc_desktop_pos.x;
						tx_gm[gm_dst].v.abs.y =
							c->mc_desktop_pos.y;
						tx_gm[gm_dst].code =
							CB_GUI_MOUSE_BTN_RIGHT;
						gm_dst++;
						break;
					} else {
						cb_event_source_timer_update(
							dev->rbtn_clk_timer,
							DCLK_INTERVAL, 0);
					}
					tx_gm[gm_dst].tag =
						CB_GUI_INP_MOUSE_BTN_CLK;
					tx_gm[gm_dst].v.abs.x =
						c->mc_desktop_pos.x;
					tx_gm[gm_dst].v.abs.y =
						c->mc_desktop_pos.y;
				}
				tx_gm[gm_dst].code = CB_GUI_MOUSE_BTN_RIGHT;
				gm_dst++;
				break;
			case BTN_MIDDLE:
				if (evts[src].value) {
					tx_gm[gm_dst].tag =
						CB_GUI_INP_MOUSE_BTN_DOWN;
					tx_gm[gm_dst].v.abs.x =
						c->mc_desktop_pos.x;
					tx_gm[gm_dst].v.abs.y =
						c->mc_desktop_pos.y;
				} else {
					tx_gm[gm_dst].tag =
						CB_GUI_INP_MOUSE_BTN_UP;
					tx_gm[gm_dst].v.abs.x =
						c->mc_desktop_pos.x;
					tx_gm[gm_dst].v.abs.y =
						c->mc_desktop_pos.y;
					tx_gm[gm_dst].code =
						CB_GUI_MOUSE_BTN_MIDDLE;
					gm_dst++;
					dev->mbtn_clk_cnt++;
					if (dev->mbtn_clk_cnt == 2) {
						dev->mbtn_clk_cnt = 0;
						tx_gm[gm_dst].tag =
						    CB_GUI_INP_MOUSE_BTN_DCLK;
						tx_gm[gm_dst].v.abs.x =
							c->mc_desktop_pos.x;
						tx_gm[gm_dst].v.abs.y =
							c->mc_desktop_pos.y;
						tx_gm[gm_dst].code =
							CB_GUI_MOUSE_BTN_MIDDLE;
						gm_dst++;
						break;
					} else {
						cb_event_source_timer_update(
							dev->mbtn_clk_timer,
							DCLK_INTERVAL, 0);
					}
					tx_gm[gm_dst].tag =
						CB_GUI_INP_MOUSE_BTN_CLK;
					tx_gm[gm_dst].v.abs.x =
						c->mc_desktop_pos.x;
					tx_gm[gm_dst].v.abs.y =
						c->mc_desktop_pos.y;
				}
				tx_gm[gm_dst].code = CB_GUI_MOUSE_BTN_MIDDLE;
				gm_dst++;
				break;
			default:
				if (evts[src].value) {
					tx_gm[gm_dst].tag = CB_GUI_INP_KEY_DOWN;
					tx_gm[gm_dst].v.kv = CB_GUI_KEY_DOWN;
				} else {
					tx_gm[gm_dst].tag = CB_GUI_INP_KEY_UP;
					tx_gm[gm_dst].v.kv = CB_GUI_KEY_UP;
				}
				tx_gm[gm_dst].code = vk_map[evts[src].code];
				gm_dst++;
				if (!evts[src].value) {
					tx_gm[gm_dst].tag =
						CB_GUI_INP_KEY_PRESS;
					tx_gm[gm_dst].v.kv = CB_GUI_KEY_UP;
					gm_dst++;
				}
				break;
			}
			break;
		case EV_REP:
			tx_evt[dst].type = EV_REP;
			tx_evt[dst].code = evts[src].code;
			tx_evt[dst].v.value = evts[src].value;
			dst++;
			break;
		case EV_REL:
			switch (evts[src].code) {
			case REL_WHEEL:
				tx_evt[dst].type = EV_REL;
				tx_evt[dst].code = REL_WHEEL;
				tx_evt[dst].v.value = evts[src].value;
				dst++;
				tx_gm[gm_dst].tag = CB_GUI_INP_MOUSE_SCROLL;
				tx_gm[gm_dst].code = 0;
				tx_gm[gm_dst].v.sd.d = evts[src].value;
				gm_dst++;
				break;
			case REL_X:
				dx = evts[src].value;
				break;
			case REL_Y:
				dy = evts[src].value;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}

	if (!dst)
		return;

	/* send raw event */
	list_for_each_entry_safe(client, next_client, &c->clients, link) {
		if (!(client->capability & CB_CLIENT_CAP_RAW_INPUT))
			continue;
		if (!client->raw_input_en)
			continue;
		client->send_raw_input_evts(client, c->raw_input_tx_buffer,dst);
	}

	if (c->top_view) {
		if (!c->top_view->surface)
			return;
		if (c->top_view->direct_show)
			return;
		client = c->top_view->surface->client_agent;
		if (!client)
			return;
		if (!(client->capability & CB_CLIENT_CAP_INPUT))
			return;
		client->send_input_msg(client, c->input_tx_buffer, gm_dst);
	}
}

static void touch_proc(struct input_device *dev, struct input_event *evts,
		       s32 cnt);

static s32 read_input_event(s32 fd, u32 mask, void *data)
{
	struct input_device *dev = data;
	struct cb_compositor *c = dev->c;
	s32 ret;

	ret = read(fd, c->raw_input_buffer, c->raw_input_buffer_sz);
	if (ret <= 0) {
		return ret;
	}

	switch (dev->type) {
	case INPUT_TYPE_TOUCH:
		touch_proc(dev, c->raw_input_buffer,
			   ret / sizeof(struct input_event));
		break;
	case INPUT_TYPE_JOYSTICK:
		break;
	case INPUT_TYPE_KBD:
	case INPUT_TYPE_MOUSE:
		event_proc(dev, c->raw_input_buffer,
			   ret / sizeof(struct input_event));
		break;
	default:
		break;
	}

	return 0;
}

static void input_device_destroy(struct input_device *dev)
{
	if (!dev)
		return;

	comp_debug("Destroy input %s", dev->devpath);
	list_del(&dev->link);
	if (dev->type == INPUT_TYPE_TOUCH && dev->tbtn_down_timer)
		cb_event_source_remove(dev->tbtn_down_timer);
	if (dev->type == INPUT_TYPE_MOUSE && dev->lbtn_down_timer)
		cb_event_source_remove(dev->lbtn_down_timer);
	if (dev->type == INPUT_TYPE_MOUSE && dev->lbtn_clk_timer)
		cb_event_source_remove(dev->lbtn_clk_timer);
	if (dev->type == INPUT_TYPE_MOUSE && dev->rbtn_clk_timer)
		cb_event_source_remove(dev->rbtn_clk_timer);
	if (dev->type == INPUT_TYPE_MOUSE && dev->mbtn_clk_timer)
		cb_event_source_remove(dev->mbtn_clk_timer);
	if (dev->input_source)
		cb_event_source_remove(dev->input_source);
	close(dev->fd);
	free(dev);
}

static char *input_type_str[] = {
	[INPUT_TYPE_UNKNOWN] = "Unknown",
	[INPUT_TYPE_MOUSE] = "Mouse",
	[INPUT_TYPE_KBD] = "Keyboard",
	[INPUT_TYPE_KBD_LED_VDEV] = "Uinput",
	[INPUT_TYPE_TOUCH] = "Touchscreen",
	[INPUT_TYPE_JOYSTICK] = "Joystick",
};

static void remove_input_device(struct cb_compositor *c, const char *devpath)
{
	struct input_device *dev, *next;

	list_for_each_entry_safe(dev, next, &c->input_devs, link) {
		if (!strcmp(dev->devpath, devpath)) {
			comp_notice("Remove %s (%s), type %s", dev->name,
				    devpath,
				    input_type_str[dev->type]);
			if (dev->type == INPUT_TYPE_TOUCH) {
				c->touchscreen_attached = false;
			} else if (dev->type == INPUT_TYPE_JOYSTICK) {
			}
			input_device_destroy(dev);
			return;
		}
	}
}

static void retrieve_touch_info(struct input_device *dev)
{
	struct input_absinfo absinfo;
	s32 ret, i, start;

	ret = ioctl(dev->fd, EVIOCGABS(ABS_X), &absinfo);
	if (ret < 0 || absinfo.maximum <= absinfo.minimum) {
		touch_err("failed to get abs_x range. (%s)", strerror(errno));
		assert(0);
	}
	dev->tinfo.min_abs_x = absinfo.minimum;
	dev->tinfo.max_abs_x = absinfo.maximum;
	touch_debug("ABS_X (%d-%d)", dev->tinfo.min_abs_x,
		    dev->tinfo.max_abs_x);

	ret = ioctl(dev->fd, EVIOCGABS(ABS_Y), &absinfo);
	if (ret < 0 || absinfo.maximum <= absinfo.minimum) {
		touch_err("failed to get abs_x range. (%s)", strerror(errno));
		assert(0);
	}
	dev->tinfo.min_abs_y = absinfo.minimum;
	dev->tinfo.max_abs_y = absinfo.maximum;
	touch_debug("ABS_Y (%d-%d)", dev->tinfo.min_abs_y,
		    dev->tinfo.max_abs_y);
	dev->tinfo.type = ST_TOUCH;
	dev->tinfo.count_slots = 1;
	start = 0;

	ret = ioctl(dev->fd, EVIOCGABS(ABS_MT_POSITION_X), &absinfo);
	if (ret < 0 || absinfo.maximum <= absinfo.minimum) {
		touch_err("failed to get abs_mt_position_x. (%s)",
			  strerror(errno));
		goto out;
	}
	dev->tinfo.min_abs_mt_position_x = absinfo.minimum;
	dev->tinfo.max_abs_mt_position_x = absinfo.maximum;
	touch_debug("ABS_MT_POSITION_X (%d-%d)",
		    dev->tinfo.min_abs_mt_position_x,
		    dev->tinfo.max_abs_mt_position_x);

	ret = ioctl(dev->fd, EVIOCGABS(ABS_MT_POSITION_Y), &absinfo);
	if (ret < 0 || absinfo.maximum <= absinfo.minimum) {
		touch_err("failed to get abs_mt_position_y. (%s)",
			  strerror(errno));
		goto out;
	}
	dev->tinfo.min_abs_mt_position_y = absinfo.minimum;
	dev->tinfo.max_abs_mt_position_y = absinfo.maximum;
	touch_debug("ABS_MT_POSITION_Y (%d-%d)",
		    dev->tinfo.min_abs_mt_position_y,
		    dev->tinfo.max_abs_mt_position_y);
	dev->tinfo.type = MT_TOUCH;

	ret = ioctl(dev->fd, EVIOCGABS(ABS_MT_SLOT), &absinfo);
	if (ret < 0 || absinfo.maximum <= absinfo.minimum) {
		touch_err("failed to get abs_mt_slot. (%s)",
			  strerror(errno));
		touch_err("MT protocol type A is obsolete, all kernel drivers "
			  "have been converted to use type B.");
		/*
		 * MT protocol type A is obsolete, all kernel drivers have
		 * been converted to use type B.
		 */
		assert(0);
	}
	dev->tinfo.min_abs_mt_slot = absinfo.minimum;
	dev->tinfo.max_abs_mt_slot = absinfo.maximum;
	touch_debug("SLOT (%d-%d)", dev->tinfo.min_abs_mt_slot,
		    dev->tinfo.max_abs_mt_slot);
	dev->tinfo.count_slots = absinfo.maximum - absinfo.minimum + 1;
	start = absinfo.minimum;

out:
	for (i = 0; i < dev->tinfo.count_slots; i++, start++)
		dev->ts.slots[i].id = start;
	dev->ts.last_slot = &dev->ts.slots[0];
	touch_info("Count slot: %u", dev->tinfo.count_slots);
}

static void set_fix_touch_min_max_pending(struct input_device *dev)
{
	if (dev->type == INPUT_TYPE_TOUCH) {
		if (dev->tinfo.type == ST_TOUCH) {
			dev->tinfo.fix_min_x = dev->tinfo.min_abs_x;
			dev->tinfo.fix_max_x = dev->tinfo.max_abs_x;
			dev->tinfo.fix_min_y = dev->tinfo.min_abs_y;
			dev->tinfo.fix_max_y = dev->tinfo.max_abs_y;
		} else {
			dev->tinfo.fix_min_x = dev->tinfo.min_abs_mt_position_x;
			dev->tinfo.fix_max_x = dev->tinfo.max_abs_mt_position_x;
			dev->tinfo.fix_min_y = dev->tinfo.min_abs_mt_position_y;
			dev->tinfo.fix_max_y = dev->tinfo.max_abs_mt_position_y;
		}
		touch_debug(">> set [fix_min_max_pending]");
		dev->tinfo.fix_min_max_pending = true;
	}
}

/*
 * Multi-screen for touch is not implemented.
 */
static void touch_pos_proc(struct input_device *dev, s32 x, s32 y,
			   u16 *gx, u16 *gy)
{
	struct cb_compositor *c = dev->c;
	struct cb_output *o;
	s64 lx, ly;
	s32 min_x, max_x, min_y, max_y, i;
	s32 left, top;
	u32 width, height;

	min_x = dev->tinfo.fix_min_x;
	max_x = dev->tinfo.fix_max_x;
	min_y = dev->tinfo.fix_min_y;
	max_y = dev->tinfo.fix_max_y;

	if (dev->tinfo.fix_min_max_pending) {
		i = c->touch_pipe;
		o = c->outputs[i];
		if (!o->enabled)
			return;

		left = o->crtc_view_port.pos.x;
		top = o->crtc_view_port.pos.y;
		width = o->crtc_view_port.w;
		height = o->crtc_view_port.h;
		if (!width || !height)
			return;

		if (dev->tinfo.type == MT_TOUCH) {
			dev->tinfo.fix_min_x
			   = dev->tinfo.min_abs_mt_position_x;
			dev->tinfo.fix_max_x
			   = dev->tinfo.max_abs_mt_position_x;
			dev->tinfo.fix_min_y
			   = dev->tinfo.min_abs_mt_position_y;
			dev->tinfo.fix_max_y
			   = dev->tinfo.max_abs_mt_position_y;
		} else {
			dev->tinfo.fix_min_x
			   = dev->tinfo.min_abs_x;
			dev->tinfo.fix_max_x
			   = dev->tinfo.max_abs_x;
			dev->tinfo.fix_min_y
			   = dev->tinfo.min_abs_y;
			dev->tinfo.fix_max_y
			   = dev->tinfo.max_abs_y;
		}
/*
		left + left + width        max_x - min_x + 1
		-------------------- = --------------------
		left                       fix_min_x - min_x

		left + left + width        max_x - min_x + 1
		-------------------- = --------------------
		left + width              fix_max_x - min_x


		top + top + height         max_y - min_y + 1
		-------------------- = ---------------------
		top                        fix_min_y - min_y

		top + top + height         max_y - min_y + 1
		-------------------- = ---------------------
		top + height              fix_max_y - min_y
*/
		if (left) {
			dev->tinfo.fix_min_x
				= left * (max_x - min_x + 1)
				/ (left + left + width) + min_x;
			dev->tinfo.fix_max_x
				= (left + width) * (max_x - min_x + 1)
				/ (left + left + width) + min_x;
			touch_debug("min_x %d -> %d", min_x,
				    dev->tinfo.fix_min_x);
			touch_debug("max_x %d -> %d", max_x,
				    dev->tinfo.fix_max_x);
			min_x = dev->tinfo.fix_min_x;
			max_x = dev->tinfo.fix_max_x;
		} else if (top) {
			dev->tinfo.fix_min_y
				= top * (max_y - min_y + 1)
				/ (top + top + height) + min_y;
			dev->tinfo.fix_max_y
				= (top + height) * (max_y - min_y + 1)
				/ (top + top + height) + min_y;
			touch_debug("min_y %d -> %d", min_y,
				    dev->tinfo.fix_min_y);
			touch_debug("max_y %d -> %d", max_y,
				    dev->tinfo.fix_max_y);
			min_y = dev->tinfo.fix_min_y;
			max_y = dev->tinfo.fix_max_y;
		}
		touch_debug(">> clear [fix_min_max_pending]");
		dev->tinfo.fix_min_max_pending = false;
	}

	if (gx && x != -1) {
		if (x < min_x) {
			x = min_x;
			touch_warn("x = %d min_x = %d", x, min_x);
		}
		if (x > max_x) {
			x = max_x;
			touch_warn("x = %d", x);
		}
		lx = x;
/*
 *          max_x - min_x + 1              lx - min_x
 *   ------------------------------ = ----------------------------
 *              65536                          ?
 *
 */
		lx = (lx - min_x) * 65536 / (max_x - min_x + 1);
		*gx = (u16)lx;
	}

	if (gy && y != -1) {
		if (y < min_y)
			y = min_y;
		if (y > max_y)
			y = max_y;
		ly = y;
/*
 *          max_y - min_y + 1              ly - min_y
 *   ------------------------------ = ----------------------------
 *              65536                          ?
 *
 */
		ly = (ly - min_y) * 65536 / (max_y - min_y + 1);
		*gy = (u16)ly;
	}
}

static void dump_touch(struct touch_event *te, u32 sz)
{
	struct slot_info *si;
	u32 i;
	bool xc, yc;
	u16 *pos;
	u8 *p = (u8 *)te;

	for (i = 0; i < sz; i++) {
		touch_debug("%02X", p[i]);
	}

	touch_debug("--- TE Head ---");
	touch_debug("--- count slots: %u", te->count_slots);
	touch_debug("--- payload sz: %u", te->payload_sz);
	if (te->payload_sz == 0) {
		touch_debug("program exist after one second!!!!!");
		sleep(1);
		exit(0);
	}
	assert(te->payload_sz);

	si = (struct slot_info *)&te->payload[0];
	for (i = 0; i < te->count_slots + 1; i++) {
		touch_debug("--- SLOT INFO ---");
		touch_debug("--- SLOT_ID: %u", si->slot_id);
		touch_debug("--- Pressed: %u", si->pressed);
		touch_debug("--- pos_x_changed: %u", si->pos_x_changed);
		if (si->pos_x_changed)
			xc = true;
		else
			xc = false;
		touch_debug("--- pos_y_changed: %u", si->pos_y_changed);
		if (si->pos_y_changed)
			yc = true;
		else
			yc = false;
		pos = &si->pos[0];
		if (xc) {
			touch_debug("--- pos_x: %u", *pos);
			pos++;
		}
		if (yc) {
			touch_debug("--- pos_y: %u", *pos);
			pos++;
		}
		si = (struct slot_info *)pos;
	}
}

static void touch_tpos(struct cb_compositor *c, u16 x, u16 y,
		       struct cb_pos *pos, bool is_x)
{
	struct cb_output *o = c->outputs[c->touch_pipe];

	if (is_x) {
		/*
		 *        x             g_desktop_rc.w
		 * --------------- = --------------------
		 *      pos->x           desktop_rc.w
		 */
		pos->x = x * o->desktop_rc.w / o->g_desktop_rc.w;
	} else {
		/*
		 *        y             g_desktop_rc.h
		 * --------------- = --------------------
		 *      pos->y           desktop_rc.h
		 */
		pos->y = y * o->desktop_rc.h / o->g_desktop_rc.h;
	}
}

static void touch_syn_proc(struct input_device *dev, struct input_event *event,
			   u8 **cur,
			   struct cb_gui_input_msg *tx_gm, s32 *tx_gm_index)
{
	struct touch_event *te;
	struct slot_info *si;
	u16 *pp;
	s32 i, *index = tx_gm_index;
	u8 count_slots = 0;

	if (!cur || !(*cur))
		return;

	te = (struct touch_event *)(*cur);

	switch (event->code) {
	case SYN_REPORT:
		touch_debug("EV_SYN SYN_REPORT %08X", event->value);
		if (dev->tinfo.type == ST_TOUCH) {
			te->count_slots = 0;
			te->payload_sz = 0;

			si = (struct slot_info *)(&te->payload[0]);
			if (dev->ts.slots[0].pos_x_changed ||
			    dev->ts.slots[0].pos_y_changed) {
				if (dev->tdrag_flag) {
					if (!dev->tdraging) {
						dev->tdraging = true;
						tx_gm[*index].tag =
							CB_GUI_INP_DRAG_BEGIN;
						tx_gm[*index].code =
							CB_GUI_TOUCH_BTN;
						tx_gm[*index].v.abs.x
							= dev->tpos.x;
						tx_gm[*index].v.abs.y
							= dev->tpos.y;
						(*index)++;
					}
					tx_gm[*index].tag =
						CB_GUI_INP_DRAG;
					tx_gm[*index].code = CB_GUI_TOUCH_BTN;
					tx_gm[*index].v.abs.x = dev->tpos.x;
					tx_gm[*index].v.abs.y = dev->tpos.y;
					(*index)++;
				}
			}
			si->slot_id = 0;
			si->pressed = dev->ts.slots[0].pressed;
			te->payload_sz += sizeof(*si);
			pp = &si->pos[0];

			if (dev->ts.slots[0].pos_x_changed) {
				si->pos_x_changed = 1;
				*pp = dev->ts.slots[0].pos_x;
				te->payload_sz += sizeof(u16);
				pp++;
			} else {
				si->pos_x_changed = 0;
			}

			if (dev->ts.slots[0].pos_y_changed) {
				si->pos_y_changed = 1;
				*pp = dev->ts.slots[0].pos_y;
				te->payload_sz += sizeof(u16);
				pp++;
			} else {
				si->pos_y_changed = 0;
			}

			/* reset state */
			dev->ts.slots[0].pressed = false;
			dev->ts.slots[0].pos_x_changed = false;
			dev->ts.slots[0].pos_y_changed = false;
			dev->ts.slots[0].commit_pending = false;
			dev->ts.last_slot = &dev->ts.slots[0];

			/* update cur */
			*cur = (u8 *)pp;
		} else {
			count_slots = 0;
			te->payload_sz = 0;
			si = (struct slot_info *)(&te->payload[0]);
			if (dev->ts.slots[0].pos_x_changed ||
			    dev->ts.slots[0].pos_y_changed) {
				if (dev->tdrag_flag) {
					if (!dev->tdraging) {
						dev->tdraging = true;
						tx_gm[*index].tag =
							CB_GUI_INP_DRAG_BEGIN;
						tx_gm[*index].code =
							CB_GUI_TOUCH_BTN;
						tx_gm[*index].v.abs.x
							= dev->tpos.x;
						tx_gm[*index].v.abs.y
							= dev->tpos.y;
						(*index)++;
					}
					tx_gm[*index].tag =
						CB_GUI_INP_DRAG;
					tx_gm[*index].code = CB_GUI_TOUCH_BTN;
					tx_gm[*index].v.abs.x = dev->tpos.x;
					tx_gm[*index].v.abs.y = dev->tpos.y;
					(*index)++;
				}
			}

			for (i = 0; i < dev->tinfo.count_slots; i++) {
				if (!dev->ts.slots[i].commit_pending)
					continue;

				count_slots++;
				si->slot_id = i; /* logical id */
				si->pressed = dev->ts.slots[i].pressed;
				te->payload_sz += sizeof(*si);

				pp = &si->pos[0];

				if (dev->ts.slots[i].pos_x_changed) {
					si->pos_x_changed = 1;
					*pp = dev->ts.slots[i].pos_x;
					te->payload_sz += sizeof(u16);
					pp++;
				} else {
					si->pos_x_changed = 0;
				}

				if (dev->ts.slots[i].pos_y_changed) {
					si->pos_y_changed = 1;
					*pp = dev->ts.slots[i].pos_y;
					te->payload_sz += sizeof(u16);
					pp++;
				} else {
					si->pos_y_changed = 0;
				}

				si = (struct slot_info *)pp;

				/* reset state */
				/* dev->ts.slots[i].pressed = false; */
				dev->ts.slots[i].pos_x_changed = false;
				dev->ts.slots[i].pos_y_changed = false;
				dev->ts.slots[i].commit_pending = false;
			}

			assert(count_slots);
			te->count_slots = count_slots - 1;
			touch_debug("count_slots = %u", te->count_slots);

			for (i = 0; i < dev->tinfo.count_slots; i++) {
				if (dev->ts.slots[i].pressed)
					break;
			}

			if (i == dev->tinfo.count_slots) {
				dev->ts.last_slot = &dev->ts.slots[0];
				touch_debug("reset last slot");
			}

			/* update cur */
			*cur = (u8 *)pp;
		}
		break;
	case SYN_MT_REPORT:
		touch_debug("EV_SYN SYN_MT_REPORT %08X", event->value);
		touch_err("Rev A ?");
		assert(0);
		return;
	default:
		touch_err("unknown code %04X", event->code);
		return;
	}
}

static void touch_abs_proc(struct input_device *dev, struct input_event *event)
{
	u16 gx, gy;
	s32 i;
	s16 pressed = -1;
	struct cb_compositor *c = dev->c;

	switch (event->code) {
	case ABS_X:
		touch_debug("EV_ABS ABS_X %d", event->value);
		touch_pos_proc(dev, event->value, -1, &gx, NULL);
		dev->ts.st_x = gx;
		dev->ts.st_changed = true;
		if (dev->tinfo.type == ST_TOUCH) {
			dev->ts.slots[0].pos_x_changed = true;
			dev->ts.slots[0].pos_x = gx;
			dev->ts.slots[0].commit_pending = true;
		}
		break;
	case ABS_Y:
		touch_debug("EV_ABS ABS_Y %d", event->value);
		touch_pos_proc(dev, -1, event->value, NULL, &gy);
		dev->ts.st_y = gy;
		dev->ts.st_changed = true;
		if (dev->tinfo.type == ST_TOUCH) {
			dev->ts.slots[0].pos_y_changed = true;
			dev->ts.slots[0].pos_y = gy;
			dev->ts.slots[0].commit_pending = true;
		}
		break;
	case ABS_MT_TRACKING_ID:
		touch_debug("EV_ABS ABS_MT_TRACKING_ID %08X", event->value);
		pressed = dev->ts.last_slot->pressed ? 1 : -1;
		if ((pressed == (s16)(-1)) && (event->value == (s32)(-1))) {
			/* no change, not pressed */
			touch_warn("Pressed status not changed ! 0");
		} else if ((pressed == 1) && (event->value != (s32)(-1))) {
			/* no change, pressed */
			touch_warn("Pressed status not changed ! 1");
		}
		if (event->value == (s32)(-1)) {
			dev->ts.last_slot->commit_pending = true;
			dev->ts.last_slot->pressed = 0;
			touch_debug("Pressed status: 0");
		} else {
			dev->ts.last_slot->commit_pending = true;
			dev->ts.last_slot->pressed = 1;
			touch_debug("Pressed status: 1");
		}
		break;
	case ABS_MT_POSITION_X:
		touch_debug("EV_ABS ABS_MT_POSITION_X %d", event->value);
		touch_pos_proc(dev, event->value, -1, &gx, NULL);
		dev->ts.last_slot->commit_pending = true;
		dev->ts.last_slot->pos_x = gx;
		dev->ts.last_slot->pos_x_changed = true;
		if (dev->ts.last_slot == &dev->ts.slots[0]) {
			/*
			 * calc desktop pos for touch
			 * touch_pipe -> output
			 */
			touch_tpos(c, dev->ts.slots[0].pos_x,
				   dev->ts.slots[0].pos_y,
				   &dev->tpos, true);
		}
		break;
	case ABS_MT_POSITION_Y:
		touch_debug("EV_ABS ABS_MT_POSITION_Y %d", event->value);
		touch_pos_proc(dev, -1, event->value, NULL, &gy);
		dev->ts.last_slot->commit_pending = true;
		dev->ts.last_slot->pos_y = gy;
		dev->ts.last_slot->pos_y_changed = true;
		if (dev->ts.last_slot == &dev->ts.slots[0]) {
			/*
			 * calc desktop pos for touch
			 * touch_pipe -> output
			 */
			touch_tpos(c, dev->ts.slots[0].pos_x,
				   dev->ts.slots[0].pos_y,
				   &dev->tpos, false);
		}
		break;
	case ABS_MT_SLOT:
		touch_debug("EV_ABS ABS_MT_SLOT %d", event->value);
		for (i = 0; i < dev->tinfo.count_slots; i++)
			if (dev->ts.slots[i].id == event->value)
				break;
		if (i == dev->tinfo.count_slots) {
			touch_err("cannot found slot %d", event->value);
		} else {
			dev->ts.last_slot = &dev->ts.slots[i];
		}
		break;
	default:
		touch_err("unknown code %04X", event->code);
		return;
	}
}

static void touch_key_proc(struct input_device *dev, struct input_event *event,
			   struct cb_gui_input_msg *tx_gm, s32 *tx_gm_index)
{
	u8 pressed = 0;
	s32 *index = tx_gm_index;
	struct cb_compositor *c = dev->c;

	switch (event->code) {
	case BTN_TOUCH:
		touch_debug("EV_KEY BTN_TOUCH %08X", event->value);
		if (dev->tinfo.type == MT_TOUCH) {
			pressed = dev->ts.slots[0].pressed;
			if (event->value == pressed) {
				touch_warn("Pressed status not changed ! %d",
					   pressed);
			}
			if (event->value) {
				dev->ts.slots[0].pressed = 1;
			} else {
				dev->ts.slots[0].pressed = 0;
			}
			dev->ts.slots[0].commit_pending = true;
		}
		if (event->value) {
			cb_event_source_timer_update(
						dev->tbtn_down_timer,
						LBTN_DRAG_TIMEOUT, 0);
			view_switch(c, dev->tpos.x, dev->tpos.y);
		} else {
			if (dev->tdrag_flag) {
				dev->tdrag_flag = false;
				dev->tdraging = false;
				tx_gm[*index].tag = CB_GUI_INP_DRAG_END;
				tx_gm[*index].v.abs.x = dev->tpos.x;
				tx_gm[*index].v.abs.y = dev->tpos.y;
				tx_gm[*index].code = CB_GUI_TOUCH_BTN;
				(*index)++;
			}
			cb_event_source_timer_update(
						dev->tbtn_down_timer,
						0, 0);
			tx_gm[*index].tag = CB_GUI_INP_TOUCH_BTN_CLK;
			tx_gm[*index].v.abs.x = dev->tpos.x;
			tx_gm[*index].v.abs.y = dev->tpos.y;
			tx_gm[*index].code = CB_GUI_TOUCH_BTN;
			(*index)++;
		}
		break;
	default:
		touch_err("unknown code %04X", event->code);
		return;
	}
}

static void touch_proc(struct input_device *dev, struct input_event *evts,
		       s32 cnt)
{
	struct cb_client_agent *client, *next_client;
	struct cb_compositor *c = dev->c;
	s32 src, gm_dst;
	u8 *cur, *start;
	u32 sz;
	struct cb_gui_input_msg *tx_gm;

	gm_dst = 0;
	tx_gm = (struct cb_gui_input_msg *)(c->input_tx_buffer
						+ sizeof(u32)
						+ sizeof(struct cb_tlv));
	start = c->raw_input_tx_buffer + sizeof(u32) + sizeof(struct cb_tlv);
	cur = start;
	for (src = 0; src < cnt; src++) {
		switch (evts[src].type) {
		case EV_SYN:
			touch_syn_proc(dev, &evts[src], &cur,
				       &tx_gm[gm_dst], &gm_dst);
			break;
		case EV_ABS:
			touch_abs_proc(dev, &evts[src]);
			break;
		case EV_KEY:
			touch_key_proc(dev, &evts[src],
				       &tx_gm[gm_dst], &gm_dst);
			break;
		default:
			touch_err("unknown type %04X", evts[src].type);
			return;
		}
	}

	if (cur == start) {
		touch_err("nothing to send!");
		return;
	}

	sz = cur - start;
	touch_debug("touch event size: %u %ld %ld", sz,
		    sizeof(struct touch_event), sizeof(struct slot_info));

	dump_touch((struct touch_event *)start, sz);

	list_for_each_entry_safe(client, next_client, &c->clients, link) {
		if (!(client->capability & CB_CLIENT_CAP_RAW_INPUT))
			continue;
		if (!client->raw_input_en)
			continue;
		client->send_raw_touch_evts(client, c->raw_input_tx_buffer, sz);
	}

	if (c->top_view) {
		if (!c->top_view->surface)
			return;
		if (c->top_view->direct_show)
			return;
		client = c->top_view->surface->client_agent;
		if (!client)
			return;
		if (!(client->capability & CB_CLIENT_CAP_INPUT))
			return;
		client->send_input_msg(client, c->input_tx_buffer, gm_dst);
	}
}

static void add_input_device(struct cb_compositor *c, const char *devpath)
{
	struct input_device *dev, *b;
	enum input_type type;
	s32 fd, ret;

	type = test_dev(devpath);

	if (type == INPUT_TYPE_UNKNOWN)
		return;

	if (type == INPUT_TYPE_TOUCH && c->touchscreen_attached)
		return;

	fd = open(devpath, O_RDWR | O_CLOEXEC, 0644);
	if (fd < 0) {
		comp_err("cannot open %s, %s", devpath, strerror(errno));
		return;
	}

	list_for_each_entry(b, &c->input_devs, link) {
		if (!strcmp(b->devpath, devpath)) {
			comp_err("already add device %s, skip it.", devpath);
			close(fd);
			return;
		}
	}

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return;
	memset(dev, 0, sizeof(*dev));
	dev->fd = fd;
	dev->type = type;
	dev->c = c;
	ret = ioctl(fd, EVIOCGNAME(sizeof(dev->name) - 1), &dev->name);
	if (ret == 0 || ret < 0) {
		comp_warn("Get not get input device's name. ret = %d.", ret);
		dev->name[0] = '\0';
	}
	memset(dev->devpath, 0, 256);
	strcpy(dev->devpath, devpath);
	if (type == INPUT_TYPE_KBD_LED_VDEV) {
		dev->input_source = NULL;
	} else {
		dev->input_source = cb_event_loop_add_fd(
						c->loop,
						dev->fd,
						CB_EVT_READABLE,
						read_input_event, dev);
		if (!dev->input_source) {
			close(dev->fd);
			free(dev);
			return;
		}

		if (dev->type == INPUT_TYPE_MOUSE) {
			dev->lbtn_down_timer = cb_event_loop_add_timer(
							c->loop,
							lbtn_down_timeout_cb,
							dev);
			if (!dev->lbtn_down_timer) {
				cb_event_source_remove(dev->input_source);
				close(dev->fd);
				free(dev);
				return;
			}

			dev->lbtn_clk_timer = cb_event_loop_add_timer(
							c->loop,
							lbtn_clk_timeout_cb,
							dev);
			if (!dev->lbtn_clk_timer) {
				cb_event_source_remove(dev->lbtn_down_timer);
				cb_event_source_remove(dev->input_source);
				close(dev->fd);
				free(dev);
				return;
			}

			dev->rbtn_clk_timer = cb_event_loop_add_timer(
							c->loop,
							rbtn_clk_timeout_cb,
							dev);
			if (!dev->rbtn_clk_timer) {
				cb_event_source_remove(dev->lbtn_clk_timer);
				cb_event_source_remove(dev->lbtn_down_timer);
				cb_event_source_remove(dev->input_source);
				close(dev->fd);
				free(dev);
				return;
			}

			dev->mbtn_clk_timer = cb_event_loop_add_timer(
							c->loop,
							mbtn_clk_timeout_cb,
							dev);
			if (!dev->mbtn_clk_timer) {
				cb_event_source_remove(dev->rbtn_clk_timer);
				cb_event_source_remove(dev->lbtn_clk_timer);
				cb_event_source_remove(dev->lbtn_down_timer);
				cb_event_source_remove(dev->input_source);
				close(dev->fd);
				free(dev);
				return;
			}
		} else if (dev->type == INPUT_TYPE_TOUCH) {
			dev->tbtn_down_timer = cb_event_loop_add_timer(
							c->loop,
							tbtn_down_timeout_cb,
							dev);
			if (!dev->tbtn_down_timer) {
				cb_event_source_remove(dev->input_source);
				close(dev->fd);
				free(dev);
				return;
			}
		}
	}

	list_add_tail(&dev->link, &c->input_devs);
	comp_notice("Add %s (%s), type %s",
		    dev->name,
		    dev->devpath, input_type_str[type]);

	if (type == INPUT_TYPE_JOYSTICK) {
	} else if (type == INPUT_TYPE_TOUCH) {
		c->touchscreen_attached = true;
		retrieve_touch_info(dev);
		set_fix_touch_min_max_pending(dev);
	}
}

static s32 udev_input_hotplug_event_proc(s32 fd, u32 mask, void *data)
{
	struct udev_device *device;
	struct cb_compositor *c = data;
	const char *action, *devname;

	device = udev_monitor_receive_device(c->udev_monitor);
	action = udev_device_get_property_value(device, "ACTION");
	devname = udev_device_get_property_value(device, "DEVNAME");
	if (action && devname) {
		if (!strcmp(action, "add")) {
			add_input_device(c, devname);
		} else if (!strcmp(action, "remove")) {
			remove_input_device(c, devname);
		}
	}
	udev_device_unref(device);

	return 0;
}

static void cb_compositor_input_fini(struct cb_compositor *c)
{
	struct input_device *dev, *next;

	if (!c)
		return;

	if (c->raw_input_buffer) {
		free(c->raw_input_buffer);
		c->raw_input_buffer = NULL;
		c->raw_input_buffer_sz = 0;
	}

	if (c->raw_input_tx_buffer) {
		free(c->raw_input_tx_buffer);
		c->raw_input_tx_buffer = NULL;
		c->raw_input_tx_buffer_sz = 0;
	}

	if (c->input_tx_buffer) {
		free(c->input_tx_buffer);
		c->input_tx_buffer = NULL;
		c->input_tx_buffer_sz = 0;
	}

	list_for_each_entry_safe(dev, next, &c->input_devs, link) {
		input_device_destroy(dev);
	}

	if (c->udev_source)
		cb_event_source_remove(c->udev_source);

	if (c->udev)
		udev_unref(c->udev);

	if (c->udev_monitor)
		udev_monitor_unref(c->udev_monitor);

	if (c->uinput_fd > 0) {
		ioctl(c->uinput_fd, UI_DEV_DESTROY);
		close(c->uinput_fd);
		c->uinput_fd = 0;
	}
}

static s32 get_kbd_led_status(struct compositor *comp, u32 *led_status)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct input_device *dev;
	s32 i;
	u8 led_bits[(LED_MAX + 1) / 8];
	bool found = false;

	list_for_each_entry(dev, &c->input_devs, link) {
		if (dev->type == INPUT_TYPE_KBD) {
			found = true;
			break;
		}
	}

	if (!found) {
		comp_err("no kbd connected.");
		*led_status = 0;
		return 0;
	}

	if (ioctl(dev->fd, EVIOCGLED(sizeof(led_bits)), led_bits) < 0) {
		comp_err("EVIOCGLED failed. %s", strerror(errno));
		return -errno;
	}

	*led_status = 0;
	for (i = 0; i < LED_MAX; i++) {
		if (led_bits[i/8] & (1<<(i%8))) {
			switch (i) {
            		case LED_NUML:
				*led_status |= (1 << CB_KBD_LED_STATUS_NUML);
				break;
            		case LED_CAPSL:
				*led_status |= (1 << CB_KBD_LED_STATUS_CAPSL);
				break;
            		case LED_SCROLLL:
				*led_status |= (1 << CB_KBD_LED_STATUS_SCROLLL);
				break;
			}
		}
	}

	comp_notice("current keyboard led state: %08X", *led_status);

	return 0;
}

static void emit_evt(int fd, int type, int code, int val)
{
	struct input_event ie;

	ie.type = type;
	ie.code = code;
	ie.value = val;
	ie.time.tv_sec = 0;
	ie.time.tv_usec = 0;

	write(fd, &ie, sizeof(ie));
}

static void set_kbd_led_status(struct compositor *comp, u32 led_status)
{
	struct cb_compositor *c = to_cb_c(comp);
	u32 led_status_cur;

	if (!comp)
		return;

	led_status &= (~(1U << CB_KBD_LED_STATUS_SCROLLL));

	get_kbd_led_status(comp, &led_status_cur);
	led_status_cur &= (~(1U << CB_KBD_LED_STATUS_SCROLLL));
	comp_debug("led_status_cur: %08X, req: %08X", led_status_cur,
		   led_status);

	if ((led_status_cur & (1U << CB_KBD_LED_STATUS_NUML)) !=
	    (led_status & (1U << CB_KBD_LED_STATUS_NUML))) {
		/* numlock down */
		comp_debug("Set numberlock");
		emit_evt(c->uinput_fd, EV_KEY, KEY_NUMLOCK, 1);
		emit_evt(c->uinput_fd, EV_SYN, SYN_REPORT, 0);
		emit_evt(c->uinput_fd, EV_KEY, KEY_NUMLOCK, 0);
		emit_evt(c->uinput_fd, EV_SYN, SYN_REPORT, 0);
	}

	if ((led_status_cur & (1U << CB_KBD_LED_STATUS_CAPSL)) !=
	    (led_status & (1U << CB_KBD_LED_STATUS_CAPSL))) {
		/* capslock down */
		comp_debug("Set capslock");
		emit_evt(c->uinput_fd, EV_KEY, KEY_CAPSLOCK, 1);
		emit_evt(c->uinput_fd, EV_SYN, SYN_REPORT, 0);
		emit_evt(c->uinput_fd, EV_KEY, KEY_CAPSLOCK, 0);
		emit_evt(c->uinput_fd, EV_SYN, SYN_REPORT, 0);
	}

	usleep(2000);
}

static void scan_input_devs(struct cb_compositor *c, const char *input_dir)
{
	char *devname;
	char *filename;
	DIR *dir;
	struct dirent *de;

	dir = opendir(input_dir);
	if(dir == NULL)
		return;

	devname = (char *)malloc(1024);
	memset(devname, 0, 1024);
	strcpy(devname, input_dir);
	filename = devname + strlen(devname);
	*filename++ = '/';

	while ((de = readdir(dir))) {
		if(de->d_name[0] == '.' &&
			(de->d_name[1] == '\0' ||
			(de->d_name[1] == '.' && de->d_name[2] == '\0')))
			continue;

		strcpy(filename, de->d_name);
		add_input_device(c, devname);
	}

	closedir(dir);
	free(devname);
}

static s32 cb_compositor_input_init(struct cb_compositor *c)
{
	struct uinput_user_dev uud;

	c->raw_input_buffer_sz = sizeof(struct input_event) * 4096;
	c->raw_input_buffer = (struct input_event *)malloc(
					c->raw_input_buffer_sz);
	if (!c->raw_input_buffer)
		goto err;

	c->raw_input_tx_buffer_sz = sizeof(struct cb_raw_input_event) * 4096
			+ sizeof(struct cb_tlv) + sizeof(u32);
	c->raw_input_tx_buffer = (u8 *)malloc(c->raw_input_tx_buffer_sz);
	if (!c->raw_input_tx_buffer)
		goto err;

	memset(c->raw_input_buffer, 0, c->raw_input_buffer_sz);

	c->input_tx_buffer_sz = sizeof(struct cb_gui_input_msg) * 4096
			+ sizeof(struct cb_tlv) + sizeof(u32);
	c->input_tx_buffer = (u8 *)malloc(c->input_tx_buffer_sz);
	if (!c->input_tx_buffer)
		goto err;
	memset(c->input_tx_buffer, 0, c->input_tx_buffer_sz);

	INIT_LIST_HEAD(&c->input_devs);

	c->udev = udev_new();
	if (!c->udev)
		goto err;

	c->udev_monitor = udev_monitor_new_from_netlink(c->udev, "udev");
	if (!c->udev_monitor)
		goto err;

	udev_monitor_filter_add_match_subsystem_devtype(c->udev_monitor,
							"input", NULL);
	udev_monitor_enable_receiving(c->udev_monitor);

	c->udev_source = cb_event_loop_add_fd(
					c->loop,
					udev_monitor_get_fd(c->udev_monitor),
					CB_EVT_READABLE,
					udev_input_hotplug_event_proc, c);
	if (!c->udev_source)
		goto err;

	c->uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (c->uinput_fd < 0)
		goto err;

	ioctl(c->uinput_fd, UI_SET_EVBIT, EV_KEY);
	ioctl(c->uinput_fd, UI_SET_KEYBIT, KEY_CAPSLOCK);
	ioctl(c->uinput_fd, UI_SET_KEYBIT, KEY_NUMLOCK);
	ioctl(c->uinput_fd, UI_SET_KEYBIT, KEY_SCROLLLOCK);

	memset(&uud, 0, sizeof(uud));
	snprintf(uud.name, UINPUT_MAX_NAME_SIZE, SW_KBD_NAME);
	write(c->uinput_fd, &uud, sizeof(uud));

	ioctl(c->uinput_fd, UI_DEV_CREATE);

	scan_input_devs(c, "/dev/input");

	return 0;

err:
	cb_compositor_input_fini(c);
	return -1;
}

static void add_renderer_buffer_to_task(struct cb_output *o,
					struct cb_buffer *buffer)
{
	struct scanout_task *sot;

	sot = cb_cache_get(o->c->so_task_cache, false);
	sot->buffer = buffer;
	sot->plane = o->primary_plane;
	sot->zpos = -1;
	sot->src = &o->native_surface_src;
	sot->dst = &o->native_surface_src;
	sot->alpha_src_pre_mul = true;
	list_add_tail(&sot->link, &o->so_tasks);

	if (sot->buffer == NULL) {
		if (o->primary_occupied_by_renderer) {
			printf("--- output [%d]'s primary is released by ---"
				"renderer.\n", o->pipe);
			o->primary_occupied_by_renderer = false;
		}
	} else {
		if (!o->primary_occupied_by_renderer) {
			printf("--- output [%d]'s primary is occupied by ---"
				"renderer.\n", o->pipe);
			o->primary_occupied_by_renderer = true;
		}
	}
}

static bool setup_view_output_mask(struct cb_view *view,
				   struct cb_compositor *c)
{
	struct cb_output *o;
	s32 i, pipe;
	struct cb_region view_area, output_area;
	bool lock_pipe;

	if (!view)
		return false;

	lock_pipe = (view->pipe_locked == -1 ? false : true);
	view->output_mask = 0;
	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		pipe = o->pipe;
		if (!o->enabled)
			continue;
		if (lock_pipe && o->pipe != view->pipe_locked)
			continue;
		cb_region_init_rect(&view_area,
				    view->area.pos.x,
				    view->area.pos.y,
				    view->area.w,
				    view->area.h);
		cb_region_init_rect(&output_area,
				    o->desktop_rc.pos.x,
				    o->desktop_rc.pos.y,
				    o->desktop_rc.w,
				    o->desktop_rc.h);
		cb_region_intersect(&view_area, &view_area,
				    &output_area);
		if (!cb_region_is_not_empty(&view_area)) {
			view->output_mask &= (~(1U << o->pipe));
		} else {
			if (view->direct_show) {
				view->src_areas[pipe].pos.x =
					view_area.extents.p1.x -
					view->area.pos.x;
				view->src_areas[pipe].pos.y =
					view_area.extents.p1.y -
					view->area.pos.y;
				view->src_areas[pipe].w =
					view_area.extents.p2.x -
					view_area.extents.p1.x;
				view->src_areas[pipe].h =
					view_area.extents.p2.y -
					view_area.extents.p1.y;

				cb_region_translate(&view_area,
						    -o->desktop_rc.pos.x,
						    -o->desktop_rc.pos.y);

				view->dst_areas[pipe].pos.x = 
				  (float)(view_area.extents.p1.x) *
				  o->crtc_view_port.w / o->desktop_rc.w
				  + o->crtc_view_port.pos.x;
				view->dst_areas[pipe].pos.y =
				  (float)(view_area.extents.p1.y) *
				  o->crtc_view_port.h / o->desktop_rc.h
				  + o->crtc_view_port.pos.y;
				view->dst_areas[pipe].w =
				  (float)(view_area.extents.p2.x -
				          view_area.extents.p1.x) *
				         o->crtc_view_port.w / o->desktop_rc.w;
				view->dst_areas[pipe].h =
				  (float)(view_area.extents.p2.y -
				          view_area.extents.p1.y) *
				         o->crtc_view_port.h / o->desktop_rc.h;
				/*
				printf("SRC [%d]: %d,%d %ux%u\n", o->pipe,
					view->src_areas[pipe].pos.x,
					view->src_areas[pipe].pos.y,
					view->src_areas[pipe].w,
					view->src_areas[pipe].h);

				printf("DST [%d]: %d,%d %ux%u\n", o->pipe,
					view->dst_areas[pipe].pos.x,
					view->dst_areas[pipe].pos.y,
					view->dst_areas[pipe].w,
					view->dst_areas[pipe].h);
				*/
			}
			view->output_mask |= (1U << o->pipe);
		}
		cb_region_fini(&output_area);
		cb_region_fini(&view_area);
	}

	return (view->output_mask != 0);
}

static void surface_flipped_cb(struct cb_listener *listener, void *data)
{
	struct cb_surface *surface = container_of(listener, struct cb_surface,
						  flipped_l);
	struct cb_view *view = surface->view;
	struct cb_client_agent *client = surface->client_agent;

	comp_debug("surface_flipped_cb");
	if (view->painted) {
		view->painted = false;
		cancel_renderer_surface(surface, true);
		client->send_bo_flipped(client, NULL, (u64)surface);
	}
}

static void set_renderable_buffer_changed(struct cb_compositor *c,
					  struct cb_view *view,
					  u32 mask_diff)
{
	struct cb_output *o;
	bool in_area;
	s32 i;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		/*
		 * refressh conditions:
		 * 	1. output enabled.
		 * 	2. has something to draw now.
		 * 	3. has something to draw before,
		 * 	   but now there is no need to draw.
		 */
		if (!o->enabled)
			continue;
		in_area = ((view->output_mask & (1U << o->pipe)) != 0);
		if (in_area ||
		    ((!in_area) && ((1U << o->pipe) & mask_diff))) {
			o->renderable_buffer_changed = true;
			comp_debug("Set output %d renderable_buffer_changed %d",
				   o->pipe, o->renderable_buffer_changed);
		}
	}
}

static void update_top_view_according_to_rm(struct cb_compositor *c,
					    struct cb_view *v)
{
	struct cb_view *view_prev, *top_view;
	struct cb_client_agent *client;

	if (v == c->top_view) {
		comp_notice("rm top view %lX", (u64)v);
		client = v->surface->client_agent;
		client->send_view_focus_chg(client, v, false);
		top_view = NULL;
		list_for_each_entry(view_prev, &c->views, link) {
			if (view_prev->float_view)
				continue;
			top_view = view_prev;
			break;
		}
		c->top_view = top_view;
		if (top_view) {
			client = top_view->surface->client_agent;
			client->send_view_focus_chg(client, top_view, true);
		}
		comp_notice("New top view: %lX -> %lX", (u64)v, (u64)top_view);
	}
}

static void cb_compositor_commit_surface(struct compositor *comp,
					 struct cb_surface *surface)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_view *view = surface->view;
	struct cb_client_agent *client = surface->client_agent;
	struct cb_output *o;
	u32 mask, diff;

	comp_debug("commit surface %p's buffer %p", surface,
		   surface->buffer_pending);
	surface->c = c;
	view->direct_show = false;
	mask = view->output_mask;
	if (!surface->buffer_pending) {
		/* remove view */
		comp_notice("remove view link");
		list_del(&view->link);
		cancel_renderer_surface(surface, true);
		cb_compositor_repaint(c);
	} else {
		surface->buffer_pending->surface = surface;
		/*
		 * view changed, may be position changed or buffer changed.
		 * for renderable surface, moving view also need to repaint.
		 */
		setup_view_output_mask(view, c);
		diff = mask ^ view->output_mask;
		if (mask != view->output_mask) {
			comp_debug("mask: %08X ---> %08X (%08X)", mask,
				   view->output_mask, diff);
		}

		surface->width = surface->buffer_pending->info.width;
		surface->height = surface->buffer_pending->info.height;

		if (surface->buffer_pending != surface->buffer_cur) {
			/* buffer changed */
			c->r->attach_buffer(c->r, surface,
					    surface->buffer_pending);
		}
		/* DMA-BUF for renderer do not need to flush damage */
		if (surface->buffer_pending->info.type == CB_BUF_TYPE_SHM) {
			c->r->flush_damage(c->r, surface);
		}

		surface->buffer_cur = surface->buffer_pending;

		o = find_surface_main_output(surface,
					     view->output_mask);
		if (surface->output)
			list_del(&surface->flipped_l.link);
		surface->output = o;
		if (o) {
			surface->flipped_l.notify = surface_flipped_cb;
			comp_debug("add flipped listener %d", o->pipe);
			cb_signal_add(&o->surface_flipped_signal,
				      &surface->flipped_l);
		}
		cb_compositor_repaint(c);
	}

	set_renderable_buffer_changed(c, view, diff);

	client->send_bo_complete(client, surface->buffer_pending, (u64)surface);
	surface->buffer_pending = NULL;
}

static bool is_yuv(enum cb_pix_fmt pix_fmt)
{
	switch (pix_fmt) {
	case CB_PIX_FMT_NV12:
	case CB_PIX_FMT_NV16:
	case CB_PIX_FMT_NV24:
		return true;
	default:
		return false;
	}
}

static void add_dma_buf_to_task(struct cb_output *o, struct cb_surface *surface,
				struct cb_buffer *buffer, struct plane *plane)
{
	struct cb_compositor *c = surface->c;
	struct scanout_task *sot;
	struct cb_view *view = surface->view;
	bool find = false;

	if (!plane)
		return;

	list_for_each_entry(sot, &o->so_tasks, link) {
		if (sot->plane == plane) {
			find = true;
			sot->buffer = buffer;
			break;
		}
	}

	if (!find) {
		sot = cb_cache_get(c->so_task_cache, false);
		sot->buffer = buffer;
		sot->plane = plane;
		sot->zpos = view->zpos;
		sot->src = &view->src_areas[o->pipe];
		if (sot->buffer) {
			if (is_yuv(sot->buffer->info.pix_fmt) &&
			    sot->src->pos.x % 2) {
				sot->src->pos.x &= (~1U);
			}
		}
		sot->dst = &view->dst_areas[o->pipe];
		sot->alpha_src_pre_mul = true;
		list_add_tail(&sot->link, &o->so_tasks);
	}
}

static s32 cb_compositor_commit_dma_buf(struct compositor *comp,
					struct cb_surface *surface)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_view *view = surface->view;
	struct cb_buffer *buffer;
	struct cb_output *o;
	u32 mask, diff;
	s32 i, pipe, pipe_locked;
	struct plane *plane;
	bool empty, lock_pipe;

	comp_debug("commit DMA-BUF direct show surface %p's buffer %p",
		   surface, surface->buffer_pending);
	surface->c = c;
	view->direct_show = true;
	mask = view->output_mask;
	if (!surface->buffer_pending) {
		/* remove view */
		comp_notice("remove DMA-BUF direct show view link");
		printf("remove DMA-BUF direct show view link\n");
		list_del(&view->link);
		cb_compositor_repaint(c);
		comp_warn("view->output_mask: %08X", view->output_mask);
		/* put plane it used. */
		for (i = 0; i < c->count_outputs; i++) {
			o = c->outputs[i];
			pipe = o->pipe;
			plane = view->planes[pipe];
			if (view->output_mask & (1U << o->pipe)) {
				if (plane) {
					if (plane != o->primary_plane) {
						comp_warn("put plane zpos: %d, "
							  "type %d",
							  plane->zpos,
							  plane->type);
						/* crash ? plane is null
						 * when dp is out */
						put_free_output_plane(o, plane);
					} else {
						comp_warn("enable primary "
							  "render");
						enable_primary_renderer(o);
					}
				}
				view->planes[pipe] = NULL;
			}
		}
	} else {
		pipe_locked = view->pipe_locked;
		lock_pipe = (pipe_locked == -1 ? false : true);
		surface->buffer_pending->surface = surface;
		surface->width = surface->buffer_pending->info.width;
		surface->height = surface->buffer_pending->info.height;

		/*
		 * view changed, may be position changed or buffer changed.
		 */
		setup_view_output_mask(view, c);
		diff = mask ^ view->output_mask;

		prepare_dma_buf_planes(surface, surface->buffer_pending);

		empty = true;
		scanout_buffer_dirty_init(surface->buffer_pending);
		for (i = 0; i < c->count_outputs; i++) {
			o = c->outputs[i];
			if (!o->enabled)
				continue;
			if (lock_pipe && o->pipe != pipe_locked)
				continue;
			comp_debug("view->output_mask: %08X",view->output_mask);
			if (view->output_mask & (1U << o->pipe)) {
				/* Set buffer dirty */
				scanout_set_buffer_dirty(
					surface->buffer_pending,
					o->output);
			} else {
				if (!((1U << o->pipe) & diff)) {
					continue;
				} else {
					/*
					printf("not in output %d's area\n",
						o->pipe);
					*/
				}
			}
			cb_compositor_repaint_by_output(o);
			empty = false;
		}

		if (empty) {
			comp_warn("commit DMA-BUF %lX failed. (ENOENT).",
				  (u64)(surface->buffer_pending));
			comp_warn("pipe locked: %d", view->pipe_locked);
			surface->buffer_pending = NULL;
			surface->buffer_cur = NULL;
			return -ENOENT;
		}
		surface->buffer_cur = surface->buffer_pending;
		comp_debug("set buffer_cur: %lX", (u64)(surface->buffer_cur));
		/* printf("set buffer_cur: %lX\n",
			(u64)(surface->buffer_cur)); */
		surface->buffer_last = surface->buffer_cur;
		comp_debug("set buffer last: %lX",
			   (u64)(surface->buffer_last));
		buffer = surface->buffer_pending;
		buffer->dma_buf_flipped_l.notify = dma_buf_flipped_cb;
		comp_debug("add flipped listener for dma-buf");
		c->so->add_buffer_flip_notify(c->so, buffer,
					      &buffer->dma_buf_flipped_l);

		if (!buffer->completed_l_added) {
			buffer->dma_buf_completed_l.notify
					= dma_buf_completed_cb;
			comp_debug("add completed listener for dma-buf");
			c->so->add_buffer_complete_notify(c->so,
						  buffer,
						  &buffer->dma_buf_completed_l);
			buffer->completed_l_added = true;
		}
	}

	surface->buffer_pending = NULL;

	return 0;
}

static struct cb_buffer *cb_compositor_import_rd_dmabuf(
			struct compositor *comp, struct cb_buffer_info *info)
{
	struct cb_compositor *c = to_cb_c(comp);

	if (!comp)
		return NULL;

	if (!info)
		return NULL;

	return c->r->import_dmabuf(c->r, info);
}

static struct cb_buffer *cb_compositor_import_so_dmabuf(
			struct compositor *comp, struct cb_buffer_info *info)
{
	struct cb_compositor *c = to_cb_c(comp);

	if (!comp)
		return NULL;

	if (!info)
		return NULL;

	return c->so->import_dmabuf(c->so, info);
}

static void cb_compositor_release_rd_dmabuf(struct compositor *comp,
					    struct cb_buffer *b)
{
	struct cb_compositor *c = to_cb_c(comp);

	if (!comp)
		return;

	if (!b)
		return;

	return c->r->release_dmabuf(c->r, b);
}

static void cb_compositor_release_so_dmabuf(struct compositor *comp,
					    struct cb_buffer *b)
{
	struct cb_compositor *c = to_cb_c(comp);

	if (!comp)
		return;

	if (!b)
		return;

	c->so->release_dmabuf(c->so, b);
	if (b->surface && b->surface->buffer_cur == b) {
		b->surface->buffer_cur = NULL;
		comp_warn("clear buffer_cur: %lX",
			  (u64)(b->surface->buffer_cur));
	}
}

static void cb_compositor_add_view(struct compositor *comp, struct cb_view *v)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_view *top_view, *fv, *first_normal_view;
	struct cb_client_agent *client;

	if (!comp)
		return;

	if (!v)
		return;

	if (v->float_view) {
		list_add(&v->link, &c->views);
		return;
	} else {
		first_normal_view = NULL;
		list_for_each_entry(fv, &c->views, link) {
			if (!fv->float_view) {
				first_normal_view = fv;
				break;
			}
		}
		if (first_normal_view) {
			list_add(&v->link, first_normal_view->link.prev);
		} else {
			list_add_tail(&v->link, &c->views);
		}
	}

	top_view = c->top_view;
	c->top_view = v;
	if (top_view != c->top_view) {
		comp_notice("New top view: %lX -> %lX", (u64)top_view,
			    (u64)c->top_view);
		client = v->surface->client_agent;
		client->send_view_focus_chg(client, v, true);
		if (top_view) {
			client = top_view->surface->client_agent;
			client->send_view_focus_chg(client, top_view, false);
		}
	}
}

static void cb_compositor_rm_view(struct compositor *comp, struct cb_view *v)
{
	struct cb_compositor *c = to_cb_c(comp);

	if (!comp)
		return;

	if (v) {
		list_del(&v->link);
		update_top_view_according_to_rm(c, v);
	}

	v->surface->buffer_pending = NULL;
	if (v->surface && v->surface->use_renderer)
		cb_compositor_commit_surface(comp, v->surface);
	else
		cb_compositor_commit_dma_buf(comp, v->surface);
}

static void destroy_surface_fb_cb(struct cb_buffer *b, void *userdata)
{
	struct cb_output *o = userdata;

	if (!o)
		return;

	if (b == o->rbuf_cur) {
		o->rbuf_cur = NULL;
		comp_warn("clear output %d's obuf: %lX", o->pipe, o->rbuf_cur);
	}
}

static void do_virtual_renderer_repaint(struct cb_output *o)
{
	struct cb_compositor *c = o->c;
	struct cb_view *view;
	struct r_output *ro = o->ro;
	u32 ms, us;
	u32 refresh_nsec;
	bool repainted;

	if (o->vflipped_pending)
		return;

	if (o->renderable_buffer_changed) {
		if (list_empty(&c->views)) {
			goto out;
		}
		repainted = ro->repaint(ro, &c->views);
		if (!repainted) {
			o->rbuf_cur = NULL;
			goto out;
		}
	}
out:

	if (o->enabled)
		refresh_nsec = o->output->refresh_nsec;
	else
		refresh_nsec = 16666667;

	ms = refresh_nsec / 1000000;
	us = refresh_nsec % 1000;
	comp_debug("start output %d's vflipped timer, %d, %d", o->pipe, ms, us);
	cb_event_source_timer_update(o->primary_vflipped_timer, ms, us);
	o->vflipped_pending = true;
	list_for_each_entry(view, &c->views, link) {
		if (!view->direct_show)
			view->painted = true;
	}
	o->renderable_buffer_changed = false;
}

static void do_renderer_repaint(struct cb_output *o)
{
	struct cb_compositor *c = o->c;
	struct r_output *ro = o->ro;
	struct cb_buffer *buffer;
	bool repainted;

	comp_debug("do_renderer_repaint %d", o->pipe);

	if (o->primary_renderer_disabled) {
		do_virtual_renderer_repaint(o);
		return;
	}

	if (o->renderable_buffer_changed) {
		if (list_empty(&c->views)) {
			comp_debug("set output %d's rbuf_cur NULL", o->pipe);
			o->rbuf_cur = NULL;
			goto out;
		}

		repainted = ro->repaint(ro, &c->views);
		if (!repainted) {
			comp_debug("output %d repaint empty.", o->pipe);
			comp_debug("set output %d's rbuf_cur NULL", o->pipe);
			o->rbuf_cur = NULL;
			goto out;
		}

		buffer = c->so->get_surface_buf(c->so, o->native_surface,
						destroy_surface_fb_cb,
						o);
		if (!buffer) {
			comp_err("failed to get surface buffer.");
			goto out;
		}
		comp_debug("set output %d's rbuf_cur %p", o->pipe, buffer);
		/* printf("set output %d's rbuf_cur %p\n", o->pipe, buffer); */
		o->rbuf_cur = buffer;
	}/* else {
		return;
	}*/

out:
	
	comp_debug("Add rbuf_cur %p to task for output %d changed: %d",
		   o->rbuf_cur, o->pipe, o->renderable_buffer_changed);
	
	add_renderer_buffer_to_task(o, o->rbuf_cur);
	o->renderable_buffer_changed = false;
	comp_debug("Set output %d renderable_buffer_changed %d",
		   o->pipe, o->renderable_buffer_changed);
}

static void do_dma_buf_repaint(struct cb_output *o)
{
	struct cb_compositor *c = o->c;
	struct cb_view *view;
	struct cb_surface *surface;
	/*
	struct timespec now;
	*/

	if (list_empty(&c->views))
		return;

	list_for_each_entry_reverse(view, &c->views, link) {
		if (!view->direct_show)
			continue;
		surface = view->surface;
		
		if (view->output_mask & (1U << o->pipe)) {
			/* add buffer to output's so task list */
			/*
			clock_gettime(c->clock_type, &now);
			printf("[%05lu:%06lu] Add DMA-BUF %lX to output [%d]\n",
				now.tv_sec % 86400l, now.tv_nsec / 1000l,
				(u64)(surface->buffer_cur), o->pipe);
			*/
			comp_debug("Use buffer %lX for surface %p",
				   (u64)(surface->buffer_cur), surface);
			/* printf("Use buffer %lX for surface %p\n",
				   (u64)(surface->buffer_cur), surface); */
			add_dma_buf_to_task(o, surface, surface->buffer_cur,
					    view->planes[o->pipe]);

			surface->buffer_last = NULL;
			comp_debug("clear buffer last: %lX",
				   (u64)(surface->buffer_last));
			/* printf("clear buffer last: %lX\n",
				   (u64)(surface->buffer_last)); */
		} else {
			add_dma_buf_to_task(o, surface, NULL,
					    view->planes[o->pipe]);
		}
	}
}

/* repaint timer proc */
static s32 output_repaint_timer_handler(void *data)
{
	struct cb_compositor *c = data;
	struct cb_output *o;
	struct scanout_task *sot, *sot_next;
	s32 i;
	struct scanout_commit_info *commit;
	void *sd;
	s64 msec_to_repaint;
	struct timespec now;
	bool output_empty, empty = true;

	commit = scanout_commit_info_alloc();

	/*
	 * deal with the output that has been scheduled repainting.
	 * if the output has nothing to be repainted,
	 *     reset repaint_status as REPAINT_NOT_SCHEDULED
	 * else
	 *     set repaint_status as REPAINT_WAIT_COMPLETION
	 *         (waiting for page flip)
	 */
	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->repaint_status != REPAINT_SCHEDULED) {
			continue;
		}

		clock_gettime(c->clock_type, &now);
		msec_to_repaint = timespec_sub_to_msec(&o->next_repaint, &now);
		if (msec_to_repaint > 1) {
			/* the timer cb is not alarmed by this output */
			continue;
		} else if (msec_to_repaint < -4) {
			comp_warn("output %d msec_to_repaint (%ld) < -4",
				  o->pipe, msec_to_repaint);
		}
		output_empty = true;

		do_dma_buf_repaint(o);

		/* do renderer's repaint */
		do_renderer_repaint(o);

		list_for_each_entry_safe(sot, sot_next, &o->so_tasks, link) {
			list_del(&sot->link);
			if (sot->buffer == NULL) {
				cb_cache_put(sot, c->so_task_cache);
				continue;
			}
			output_empty = false;
			empty = false;
			scanout_commit_add_fb_info(commit,
					   sot->buffer,
					   o->output,
					   sot->plane,
					   sot->src,
					   sot->dst,
					   -1,
					   sot->alpha_src_pre_mul);
			cb_cache_put(sot, c->so_task_cache);
		}

		if (!c->mc_hide && o->mc_on_screen) {
			if (o->mc_damaged) {
				o->mc_buf_cur = 1 - o->mc_buf_cur;
				o->mc_damaged = false;
			}
			scanout_commit_add_fb_info(commit,
				   o->mc_buf[o->mc_buf_cur],
				   o->output,
				   o->cursor_plane,
				   &c->mc_src,
				   &o->mc_view_port,
				   -1,
				   c->mc_alpha_src_pre_mul);
			output_empty = false;
			empty = false;
		}

		if (output_empty) {
			scanout_commit_add_fb_info(commit, o->dummy,
				o->output, o->primary_plane,
				&o->dummy_src, &o->crtc_view_port,
				0, true);
			output_empty = false;
			empty = false;
		}

		o->repaint_status = REPAINT_WAIT_COMPLETION;
	}

	if (empty) {
		printf("empty\n");
		goto out;
	}

	sd = c->so->scanout_data_alloc(c->so);
	c->so->fill_scanout_data(c->so, sd, commit);
	c->so->do_scanout(c->so, sd);
out:
	scanout_commit_info_free(commit);

	update_repaint_timer(c);
	return 0;
}

static void cb_compositor_set_dbg_level(struct compositor *comp,
					enum cb_log_level level)
{
	if (level >= CB_LOG_ERR && level <= CB_LOG_DEBUG)
		comp_dbg = level;
}

static void cb_compositor_set_sc_dbg_level(struct compositor *comp,
					   enum cb_log_level level)
{
	struct cb_compositor *c = to_cb_c(comp);

	c->so->set_dbg_level(c->so, level);
}

static void cb_compositor_set_rd_dbg_level(struct compositor *comp,
					   enum cb_log_level level)
{
	struct cb_compositor *c = to_cb_c(comp);

	c->r->set_dbg_level(c->r, level);
}

static void cb_compositor_set_touch_dbg_level(struct compositor *comp,
					      enum cb_log_level level)
{
	touch_dbg = level;
}

static void cb_compositor_set_joystick_dbg_level(struct compositor *comp,
						 enum cb_log_level level)
{
	joystick_dbg = level;
}

static void cb_compositor_set_client_dbg_level(struct compositor *comp,
					       enum cb_log_level level)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_client_agent *client, *next_client;
	struct cb_shell_info shell;

	/* broadcast client debug level to all cube clients */
	memset(&shell, 0, sizeof(shell));
	shell.cmd = CB_SHELL_DEBUG_SETTING;
	comp_warn("set client dbg level as %d", level);
	shell.value.dbg_flags.client_flag = level;
	client_dbg = level;
	list_for_each_entry_safe(client, next_client, &c->clients, link) {
		client->send_shell_cmd(client, &shell);
	}
}

static s32 dbg_changed(s32 fd, u32 mask, void *data)
{
	struct cb_compositor *c = data;
	s32 dbg_fd, ret, pos = 0, event_sz;
	u8 flag[7] = {1, 1, 1, 1, 1, 1, 1};
	u8 event_buf[64];
	struct cb_client_agent *client, *next_client;
	struct cb_shell_info shell;
	struct inotify_event *event;
	bool changed = false;

	if (!c)
		return -1;

	ret = read(fd, event_buf, sizeof(event_buf));
	if (ret < sizeof(*event))
		return -1;

	while (ret >= sizeof(*event)) {
		event = (struct inotify_event *)(event_buf + pos);
		if (event->len) {
			if (event->mask & IN_MODIFY) {
				comp_notice("file: %s changed", event->name);
				if (strcmp(event->name, DEBUG_FLAG)) {
					comp_notice("debug changed.");
					changed = true;
				}
			}
		}
		event_sz = sizeof(*event) + event->len;
		ret -= event_sz;
		pos += event_sz;
	}

	if (!changed)
		return -1;

	dbg_fd = open(DEBUG_FLAG, O_RDONLY, 0644);
	if (dbg_fd < 0)
		return -1;

	read(dbg_fd, &flag[0], 7);
	close(dbg_fd);
	comp_notice("debug: %02X %02X %02X %02X %02X %02X %02X",
		    flag[0], flag[1], flag[2], flag[3], flag[4],
		    flag[5], flag[6]);

	comp_dbg = flag[0];
	c->base.set_client_dbg_level(&c->base, flag[1]);
	c->base.set_sc_dbg_level(&c->base, flag[2]);
	c->base.set_rd_dbg_level(&c->base, flag[3]);
	client_dbg = flag[4];
	touch_dbg = flag[5];
	joystick_dbg = flag[6];

	/* broadcast client debug level to all cube clients */
	memset(&shell, 0, sizeof(shell));
	shell.cmd = CB_SHELL_DEBUG_SETTING;
	shell.value.dbg_flags.client_flag = flag[4];
	list_for_each_entry_safe(client, next_client, &c->clients, link) {
		client->send_shell_cmd(client, &shell);
	}

	return 0;
}

static void cb_compositor_init_client_dbg(struct compositor *comp,
					  struct cb_client_agent *client)
{
	struct cb_shell_info shell;

	comp_warn("set client init debug level: %08X", client_dbg);
	memset(&shell, 0, sizeof(shell));
	shell.cmd = CB_SHELL_DEBUG_SETTING;
	shell.value.dbg_flags.client_flag = client_dbg;
	client->send_shell_cmd(client, &shell);
}

struct compositor *compositor_create(char *device_name,
				     struct cb_event_loop *loop,
				     struct pipeline *pipecfgs,
				     s32 count_outputs,
				     s32 touch_pipe,
				     float mc_accel)
{
	struct cb_compositor *c;
	s32 i, vid;

	c = calloc(1, sizeof(*c));
	if (!c)
		return NULL;

	c->touch_pipe = touch_pipe;
	c->mc_accel = mc_accel;

	c->loop = loop;

	c->so_task_cache = cb_cache_create(sizeof(struct scanout_task), 128);
	if (!c->so_task_cache)
		goto err;

	c->base.destroy = cb_compositor_destroy;

	c->so = scanout_create(device_name, loop);
	if (!c->so)
		goto err;

	c->native_fmt = c->so->get_native_format(c->so);
	c->native_dev = c->so->get_native_dev(c->so);

	c->r = renderer_create(&c->base, &c->native_fmt, 1, true, c->native_dev,
			       &vid);
	if (!c->r)
		goto err;

	INIT_LIST_HEAD(&c->clients);
	INIT_LIST_HEAD(&c->views);

	c->count_outputs = count_outputs;
	c->outputs = calloc(count_outputs, sizeof(struct cb_output *));
	if (!c->outputs)
		goto err;

	/* mouse cursor 0,0 */
	memset(&c->mc_desktop_pos, 0, sizeof(c->mc_desktop_pos));
	c->mc_desktop_pos.x = 0;
	c->mc_desktop_pos.y = 0;
	c->mc_hot_pos.x = DEF_MC_HOT_X;
	c->mc_hot_pos.y = DEF_MC_HOT_Y;
	c->mc_src.pos.x = c->mc_src.pos.y = 0;
	c->mc_src.w = MC_MAX_WIDTH;
	c->mc_src.h = MC_MAX_HEIGHT;

	c->mc_hide = false;

	for (i = 0; i < count_outputs; i++) {
		c->outputs[i] = cb_output_create(c, &pipecfgs[i]);
		if (!c->outputs[i])
			goto err;
	}

	cb_signal_init(&c->ready_signal);

	c->ready_timer = cb_event_loop_add_timer(c->loop, ready_timer_cb, c);
	if (!c->ready_timer)
		goto err;

	/* prepare suspend timer */
	c->disable_all_pending = 0;
	c->suspend_timer = cb_event_loop_add_timer(c->loop,
						   suspend_timer_cb,
						   c);
	if (!c->suspend_timer)
		goto err;

	/* get clock type. clock is used to launch repaint */
	c->clock_type = c->so->get_clock_type(c->so);

	/* create repaint timer */
	c->repaint_timer = cb_event_loop_add_timer(c->loop,
					output_repaint_timer_handler, c);
	if (!c->repaint_timer)
		goto err;

	/* prepare input devices */
	if (cb_compositor_input_init(c) < 0)
		goto err;

	c->debug_inotify_fd = inotify_init();
	if (c->debug_inotify_fd < 0)
		goto err;

	inotify_add_watch(c->debug_inotify_fd, DEBUG_PATH, IN_MODIFY);

	c->dbg_source = cb_event_loop_add_fd(c->loop, c->debug_inotify_fd,
					     CB_EVT_READABLE,
					     dbg_changed, c);
	if (!c->dbg_source)
		goto err;

	c->base.register_ready_cb = cb_compositor_register_ready_cb;
	c->base.commit_surface = cb_compositor_commit_surface;
	c->base.commit_dmabuf = cb_compositor_commit_dma_buf;
	c->base.import_rd_dmabuf = cb_compositor_import_rd_dmabuf;
	c->base.import_so_dmabuf = cb_compositor_import_so_dmabuf;
	c->base.release_rd_dmabuf = cb_compositor_release_rd_dmabuf;
	c->base.release_so_dmabuf = cb_compositor_release_so_dmabuf;
	c->base.add_view_to_comp = cb_compositor_add_view;
	c->base.rm_view_from_comp = cb_compositor_rm_view;
	c->base.dispatch_hotplug_event = cb_compositor_dispatch_hpd;
	c->base.suspend = cb_compositor_suspend;
	c->base.resume = cb_compositor_resume;
	c->base.register_head_status_cb = cb_compositor_register_head_status_cb;
	c->base.head_connected = cb_compositor_get_head_status;
	c->base.retrieve_edid = cb_compositor_retrieve_edid;
	c->base.get_connector_name = cb_compositor_get_connector_name;
	c->base.get_monitor_name = cb_compositor_get_monitor_name;
	c->base.get_custom_timing = cb_compositor_get_custom_timing;
	c->base.get_current_timing = cb_compositor_get_current_timing;
	c->base.enumerate_timing = cb_compositor_enumerate_timing;
	c->base.create_custom_timing = cb_compositor_create_custom_timing;
	c->base.switch_timing = cb_compositor_switch_mode;
	c->base.set_desktop_layout = cb_compositor_set_desktop_layout;
	c->base.get_desktop_layout = cb_compositor_get_desktop_layout;
	c->base.hide_mouse_cursor = cb_compositor_hide_mouse_cursor;
	c->base.show_mouse_cursor = cb_compositor_show_mouse_cursor;
	c->base.set_mouse_cursor = cb_compositor_set_mouse_cursor;
	c->base.add_client = cb_compositor_add_client;
	c->base.init_client_dbg = cb_compositor_init_client_dbg;
	c->base.rm_client = cb_compositor_rm_client;
	c->base.set_kbd_led_status = set_kbd_led_status;
	c->base.get_kbd_led_status = get_kbd_led_status;
	c->base.set_dbg_level = cb_compositor_set_dbg_level;
	c->base.set_sc_dbg_level = cb_compositor_set_sc_dbg_level;
	c->base.set_rd_dbg_level = cb_compositor_set_rd_dbg_level;
	c->base.set_client_dbg_level = cb_compositor_set_client_dbg_level;
	c->base.set_touch_dbg_level = cb_compositor_set_touch_dbg_level;
	c->base.set_joystick_dbg_level = cb_compositor_set_joystick_dbg_level;

	return &c->base;

err:
	if (c)
		c->base.destroy(&c->base);

	return NULL;
}

