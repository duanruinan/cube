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

/*
#define MC_DEBUG 1
#define MC_DEBUG_BO_COMPLETE 1
*/
/*
#define MC_SINGLE_SYNC 1
*/
#define DUMMY_WIDTH 1280
#define DUMMY_HEIGHT 720

#define MC_MAX_WIDTH 64
#define MC_MAX_HEIGHT 64

#define GLOBAL_DESKTOP_SZ 65536.0f

#define OUTPUT_DISABLE_DELAYED_MS 1
#define OUTPUT_DISABLE_DELAYED_US 500
#define DISABLE_ALL_DELAYED_MS 2
#define DISABLE_ALL_DELAYED_US 0

#define CONN_STATUS_DB_TIME 500

#define TEST_DUMMY_VIEW 1
//#define TEST_DUMMY_YUV444P 1
#define TEST_DUMMY_NV24 1

#define TEST_DUMMY_DMA_BUF 1
#define TEST_DUMMY_DMA_BUF_NV12 1

static enum cb_log_level comp_dbg = CB_LOG_DEBUG;

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
	 * a crtc must at least own the two planes.
	 * the two plane pointer here are used to accelerate plane selection.
	 */
	struct plane *primary_plane;
	struct plane *cursor_plane;

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

	/* event loop */
	struct cb_event_loop *loop;

	/* wait for initial plug in/out debounce */
	struct cb_signal ready_signal;
	/* timer used for waiting initial hugplug debounce */
	struct cb_event_source *ready_timer;

	/* scanout interface */
	struct scanout *so;

	/* renderer interface */
	struct renderer *r;

	/* surface pixel format */
	u32 native_fmt;

	/* native device */
	void *native_dev;

#ifdef TEST_DUMMY_VIEW
	struct cb_surface dummy_surf;
	struct cb_view dummy_view;
	struct shm_buffer dummy_buf[2];
	s32 dummy_index;
	struct cb_event_source *test_rm_view_timer;
#endif
	/* views in z-order from top to bottom */
	struct list_head views;

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
	/* mouse cursor's hot point position */
	struct cb_pos mc_hot_pos;
	/* mouse cursor's alpha is blended or not */
	bool mc_alpha_src_pre_mul;
	/* hide cursor or not */
	bool mc_hide;
	/* cursor buffers */
	struct cb_buffer *mc_buf[2];
#ifdef MC_DEBUG_BO_COMPLETE
	/* cursor buffer complete */
	struct cb_listener mc_buf_l[2];
#endif
	/* cursor buffer source rect */
	struct cb_rect mc_src;
	/*
	 * cursor buf current (prepare to be or already committed into kernel)
	 * buffer index
	 */
	s32 mc_buf_cur;
	/* mouse cursor update complete signal (committed into kernel) */
	struct cb_signal mc_update_complete_signal;
	/* mc flipped event listener */
	struct cb_listener mc_flipped_l[2];
	/* mouse cursor has been committed all outputs or not */
	bool mc_update_pending;

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

	struct input_event *buffer;
	size_t buffer_sz;
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

static void cb_output_destroy(struct cb_output *output)
{
	if (!output)
		return;

	cb_signal_fini(&output->surface_flipped_signal);

	if (output->conn_st_chg_db_timer)
		cb_event_source_remove(output->conn_st_chg_db_timer);

	cb_signal_fini(&output->conn_st_chg_db_signal);

	cb_signal_fini(&output->switch_mode_signal);

	if (output->disable_timer)
		cb_event_source_remove(output->disable_timer);

	if (output->dummy)
		output->c->so->dumb_buffer_destroy(output->c->so,
						   output->dummy);

	if (output->output && output->c && output->c->so)
		output->c->so->pipeline_destroy(output->c->so, output->output);

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
	/* printf("%d, %d\n", c->mc_g_desktop_pos.x, c->mc_g_desktop_pos.y); */
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

/*
 * Calculate rectangle on crtc coordinates for desktop canvas.
 * desktop canvas's size is the real full screen buffer's size.
 */
static void update_crtc_view_port(struct cb_output *output)
{
	struct cb_mode *mode;
	s32 calc;

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
			printf("%s: %d  %u / %u = %f\n", __func__, __LINE__,
				output->desktop_rc.w, output->crtc_w,
				output->scale);
			output->crtc_view_port.pos.x = 0;
			output->crtc_view_port.pos.y =
				(output->crtc_h - calc) / 2;
			output->crtc_view_port.w = output->crtc_w;
			output->crtc_view_port.h = calc;
		} else {
			output->scale = (float)output->desktop_rc.h
				/ output->crtc_h;
			printf("%s: %d  %u / %u = %f\n", __func__, __LINE__,
				output->desktop_rc.w, output->crtc_w,
				output->scale);
			calc = output->desktop_rc.w * output->crtc_h
				/ output->desktop_rc.h;
			output->crtc_view_port.pos.x =
				(output->crtc_w - calc) / 2;
			output->crtc_view_port.pos.y = 0;
			output->crtc_view_port.w = calc;
			output->crtc_view_port.h = output->crtc_h;
		}
	}
	comp_notice("desktop (%d,%d - %ux%u) crtc_view_port (%d,%d - %ux%u)",
			output->desktop_rc.pos.x, output->desktop_rc.pos.y,
			output->desktop_rc.w, output->desktop_rc.h,
			output->crtc_view_port.pos.x,
			output->crtc_view_port.pos.y,
			output->crtc_view_port.w,
			output->crtc_view_port.h);
	printf("desktop (%d,%d - %ux%u) crtc_view_port (%d,%d - %ux%u)\n",
			output->desktop_rc.pos.x, output->desktop_rc.pos.y,
			output->desktop_rc.w, output->desktop_rc.h,
			output->crtc_view_port.pos.x,
			output->crtc_view_port.pos.y,
			output->crtc_view_port.w,
			output->crtc_view_port.h);

	update_mc_view_port(output, true);
}

static void cb_compositor_commit_surface(struct compositor *comp,
					struct cb_surface *surface);

static void cancel_renderer_surface(struct cb_surface *surface)
{
	surface->view->painted = false;
	list_del(&surface->flipped_l.link);
	
	/* printf("surface buffer flipped %p\n", surface->buffer_cur); */
	
	if (surface->buffer_last) {
		/*
		printf("surface buffer %p complete.\n",
			surface->buffer_last);
		*/
	}
	surface->buffer_last = surface->buffer_cur;
}

static void cancel_so_tasks(struct cb_output *output)
{
	struct scanout_task *sot, *sot_next;
	struct cb_compositor *c = output->c;
	struct cb_view *view;
	struct cb_surface *surface;

	comp_debug("sot remains:");
	printf("sot remains:\n");
	list_for_each_entry_safe(sot, sot_next, &output->so_tasks, link) {
		list_del(&sot->link);
		comp_debug("\t%d,%d %ux%u -> %d,%d %ux%u, zpos: %d, dirty:%08X",
			   sot->src->pos.x, sot->src->pos.y,
			   sot->src->w, sot->src->h,
			   sot->dst->pos.x, sot->dst->pos.y,
			   sot->dst->w, sot->dst->h, sot->zpos,
			   sot->buffer->dirty);
		printf("\t%d,%d %ux%u -> %d,%d %ux%u, zpos: %d, dirty:0x%08X\n",
			   sot->src->pos.x, sot->src->pos.y,
			   sot->src->w, sot->src->h,
			   sot->dst->pos.x, sot->dst->pos.y,
			   sot->dst->w, sot->dst->h, sot->zpos,
			   sot->buffer->dirty);
		if (sot->buffer && (sot->buffer->dirty & (1U << output->pipe))){
			sot->buffer->dirty &= (~(1U << output->pipe));
			if (!sot->buffer->dirty) {
				if (sot->plane == output->cursor_plane) {
					if (c->mc_update_pending) {
						c->mc_update_pending = false;
						cb_signal_emit(
						  &c->mc_update_complete_signal,
						  NULL);
					}
				}
			}
		}
		cb_cache_put(sot, c->so_task_cache);
	}

	list_for_each_entry(view, &c->views, link) {
		surface = view->surface;
		if (!surface)
			continue;
		if (surface->output == output) {
			printf("view %p's output is %d\n", view, output->pipe);
			cancel_renderer_surface(surface);
#ifdef TEST_DUMMY_VIEW
			c->dummy_index = 1 - c->dummy_index;
			c->dummy_surf.buffer_pending =
				&c->dummy_buf[c->dummy_index].base;
			cb_compositor_commit_surface(&c->base, surface);
#endif
		}
	}
}

static void disable_output_render(struct cb_output *o)
{
	if (o->ro) {
		o->ro->destroy(o->ro);
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

	printf("Suspend >>>>>>>>>>>>>>\n");
	if (!c || !c->outputs)
		return -EINVAL;

	if (c->suspend_pending) {
		comp_notice("suspend pending, try later.");
		printf("suspend pending, try later.");
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
				printf("Save mode %ux%u@%u\n",
					mode->width, mode->height,
					mode->vrefresh);
				memcpy(&o->mode_save, mode, sizeof(*mode));
				o->mode_saved = true;
			} else {
				o->mode_saved = false;
			}
			printf("Try to disable output: %d\n", o->pipe);
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

static s32 cb_compositor_destroy(struct compositor *comp)
{
	struct cb_compositor *c = to_cb_c(comp);
	s32 i, ret;
	struct cb_view *view, *view_next;

	ret = suspend(c);
	if (ret == -EAGAIN || c->suspend_pending) {
		printf("suspend pending !!!\n");
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

	cb_signal_fini(&c->mc_update_complete_signal);

	for (i = 0; i < 2; i++) {
		if (c->mc_buf[i]) {
			printf("destroy cursor bo\n");
			c->so->cursor_bo_destroy(c->so, c->mc_buf[i]);
		}
	}

	if (c->r)
		c->r->destroy(c->r);

	list_for_each_entry_safe(view, view_next, &c->views, link) {
		list_del(&view->link);
#ifdef TEST_DUMMY_VIEW
		if (view == &c->dummy_view) {
			cb_shm_release(&c->dummy_buf[0].shm);
			cb_shm_release(&c->dummy_buf[1].shm);
			if (view->surface && view->surface->renderer_state) {
				free(view->surface->renderer_state);
				view->surface->renderer_state = NULL;
			}
		}
#endif
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
			pixel[j] = 0xFF0000FF;
		}
		pixel += (stride >> 2);
	}
}

static void fill_cursor(struct cb_compositor *c,
			u8 *data, u32 width, u32 height, u32 stride)
{
	c->so->cursor_bo_update(c->so, c->mc_buf[1 - c->mc_buf_cur],
				data, width, height, stride);
}

static void dummy_flipped_cb(struct cb_listener *listener, void *data)
{
	struct cb_output *output;

	output = container_of(listener, struct cb_output,
			      dummy_flipped_l);
	output->dummy_flipped_pending = false;
	printf("[output: %d] Dummy flipped\n", output->pipe);
	comp_notice("[output: %d] Dummy flipped", output->pipe);
	//output->enabled = true;
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
	printf("show dummy: %ux%u, %d,%d %ux%u\n", DUMMY_WIDTH, DUMMY_HEIGHT,
		output->crtc_view_port.pos.x,
		output->crtc_view_port.pos.y,
		output->crtc_view_port.w,
		output->crtc_view_port.h);
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
		c->mc_update_pending = false;
		printf("Add FB dummy mc %p\n", c->mc_buf[c->mc_buf_cur]);
		scanout_commit_add_fb_info(commit,
					   c->mc_buf[c->mc_buf_cur],
					   output->output,
					   output->cursor_plane,
					   &c->mc_src,
					   &output->mc_view_port, -1,
					   c->mc_alpha_src_pre_mul);
#ifdef MC_DEBUG
		printf("Commit MC []: %d\n", c->mc_buf_cur);
#endif
	}
	sd = c->so->scanout_data_alloc(c->so);
	c->so->fill_scanout_data(c->so, sd, commit);
	output->dummy_flipped_pending = true;
	c->so->do_scanout(c->so, sd);
	scanout_commit_info_free(commit);
	output->repaint_status = REPAINT_WAIT_COMPLETION;
}

static void add_mc_buffer_to_task(struct cb_compositor *c,
				  struct cb_buffer *buffer,
				  u32 mask)
{
	s32 i;
	struct cb_output *o;
	struct scanout_task *sot;
	bool find;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (!(mask & (1U << o->pipe)))
			continue;
		find = false;
		list_for_each_entry(sot, &o->so_tasks, link) {
			if (sot->plane == o->cursor_plane) {
				// printf("!!!! %p %p\n", sot->buffer, buffer);
				sot->buffer = buffer;
				find = true;
				break;
			}
		}
		if (!find) {
			sot = cb_cache_get(c->so_task_cache, false);
			sot->buffer = buffer;
			sot->plane = o->cursor_plane;
			sot->zpos = -1;
			sot->src = &c->mc_src;
			sot->dst = &o->mc_view_port;
			sot->alpha_src_pre_mul = c->mc_alpha_src_pre_mul;
			list_add_tail(&sot->link, &o->so_tasks);
		}
	}
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
		printf("output %d is enabled. repaint_status: %d\n",
			o->pipe, o->repaint_status);
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
					comp_debug("Primary Format: %4.4s\n",
						   (char *)&plane->formats[i]);
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
					comp_debug("Cursor Format: %4.4s\n",
						   (char *)&plane->formats[i]);
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

static void mc_flipped_cb(struct cb_listener *listener, void *data)
{
	struct cb_buffer *buffer = data;
	s64 index = (s64)(buffer->userdata);
	struct cb_compositor *c = container_of(listener, struct cb_compositor,
					       mc_flipped_l[index]);

	//printf(">>> Cursor bo %ld flipped.\n", index);
	//printf(">>> Cursor dirty: %d\n", buffer->dirty);
#ifdef MC_DEBUG
	printf("MC bo %ld flipped\n", index);
	comp_debug("MC bo %ld flipped", index);
#endif
	assert(!buffer->dirty);
	c->mc_update_pending = false;
	cb_signal_emit(&c->mc_update_complete_signal, NULL);
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

	if (!any_should_repaint)
		return;

	if (msec_to_next < 1)
		msec_to_next = 1;

	/* update repaint timer */
	cb_event_source_timer_update(c->repaint_timer, msec_to_next, 0);
}

static void schedule_repaint(struct cb_output *o, struct timespec *last)
{
	struct timespec now;
	struct output *output = o->output;
	s64 msec_rel;

	if (o->repaint_status != REPAINT_WAIT_COMPLETION)
		printf("output %d's repaint status: %d\n", o->pipe,
			o->repaint_status);
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

	/*
	printf("--------------- OUTPUT %d flipped ---------------\n", o->pipe);
	*/
	cb_signal_emit(&o->surface_flipped_signal, NULL);

	last.tv_sec = output->sec;
	last.tv_nsec = output->usec * 1000l;

	schedule_repaint(o, &last);
}

static struct cb_output *cb_output_create(struct cb_compositor *c,
					  struct pipeline *pipecfg, s32 pipe)
{
	struct cb_output *output = NULL;
	struct scanout *so;
	struct cb_buffer_info info;
	s32 vid;

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

	/* prepare dummy buffer */
	memset(&info, 0, sizeof(info));
	info.pix_fmt = CB_PIX_FMT_XRGB8888;
	info.width = DUMMY_WIDTH;
	info.height = DUMMY_HEIGHT;
	output->dummy = so->dumb_buffer_create(so, &info);
	if (!output->dummy)
		goto err;

	/* fill dummy buffer with color blue */
	fill_dummy(output->dummy->info.maps[0], output->dummy->info.width,
		   output->dummy->info.height, output->dummy->info.strides[0]);

	/* register output's page flip handler */
	output->output_flipped_l.notify = output_flipped_cb;
	output->output->add_page_flip_notify(output->output,
					     &output->output_flipped_l);

	/* register dummy page flip handler */
	output->dummy_flipped_l.notify = dummy_flipped_cb;
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

	/* extended */
	if (output->pipe == 0) {
		output->desktop_rc.pos.x = 0;
		output->desktop_rc.pos.y = 0;
		output->desktop_rc.w = 2560;
		output->desktop_rc.h = 1440;
		output->g_desktop_rc.pos.x = 0;
		output->g_desktop_rc.pos.y = 0;
		output->g_desktop_rc.w = GLOBAL_DESKTOP_SZ
				/ (2560 + 1600) * 2560;
		output->g_desktop_rc.h = GLOBAL_DESKTOP_SZ;
	} else {
		output->desktop_rc.pos.x = 2560;
		output->desktop_rc.pos.y = 0;
		output->desktop_rc.w = 1600;
		output->desktop_rc.h = 900;
		output->g_desktop_rc.pos.x = GLOBAL_DESKTOP_SZ /
				(2560 + 1600) * 2560;
		output->g_desktop_rc.pos.y = 0;
		output->g_desktop_rc.w = GLOBAL_DESKTOP_SZ
				- output->g_desktop_rc.pos.x;
		output->g_desktop_rc.h = GLOBAL_DESKTOP_SZ
				/ 1440 * 900;
		printf("******************** %u,%u *******************\n",
			output->g_desktop_rc.w, output->g_desktop_rc.h);
	}

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
	s32 vid;

	if (!comp || !mode || pipe < 0)
		return -EINVAL;

	o = c->outputs[pipe];
	if (!o)
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

	if (!comp || pipe < 0 || !l)
		return -EINVAL;

	o = c->outputs[pipe];
	if (!o)
		return -EINVAL;

	cb_signal_add(&o->conn_st_chg_db_signal, l);
	return 0;
}

static bool cb_compositor_get_head_status(struct compositor *comp, s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;

	if (!comp || pipe < 0)
		return -EINVAL;

	o = c->outputs[pipe];
	if (!o)
		return -EINVAL;

	return o->conn_st_db;
}

static s32 cb_compositor_retrieve_edid(struct compositor *comp, s32 pipe,
				       u8 *data, size_t *length)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;

	if (!comp || pipe < 0 || !data || !length)
		return -EINVAL;

	o = c->outputs[pipe];
	if (!o)
		return -EINVAL;

	return o->head->retrieve_edid(o->head, data, length);
}

static const char *cb_compositor_get_connector_name(struct compositor *comp,
						    s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;

	if (!comp || pipe < 0)
		return NULL;

	o = c->outputs[pipe];
	if (!o)
		return NULL;

	return o->head->connector_name;
}

static const char *cb_compositor_get_monitor_name(struct compositor *comp,
						  s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;

	if (!comp || pipe < 0)
		return NULL;

	o = c->outputs[pipe];
	if (!o)
		return NULL;

	return o->head->monitor_name;
}

static struct cb_mode *cb_compositor_get_custom_timing(struct compositor *comp,
						       s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;

	if (!comp || pipe < 0)
		return NULL;

	o = c->outputs[pipe];
	if (!o)
		return NULL;

	return o->output->get_custom_mode(o->output);
}

static struct cb_mode *cb_compositor_get_current_timing(struct compositor *comp,
							s32 pipe)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_output *o;

	if (!comp || pipe < 0)
		return NULL;

	o = c->outputs[pipe];
	if (!o)
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

	if (!comp || pipe < 0)
		return NULL;

	o = c->outputs[pipe];
	if (!o)
		return NULL;

	prev = last_mode;

retry:
	mode = o->output->enumerate_mode(o->output, prev);
	if (f) {
		if (f->mode == CB_MODE_FILTER_MODE_SIZE_OR_CLOCK) {
			if (((mode->width >= f->min_width) &&
			     (mode->width <= f->max_width) &&
			     (mode->height >= f->min_height) &&
			     (mode->height <= f->max_height)) ||
			    ((mode->pixel_freq >= f->min_clock) &&
			     (mode->pixel_freq <= f->max_clock))) {
				return mode;
			}
		} else {
			if (((mode->width >= f->min_width) &&
			     (mode->width <= f->max_width) &&
			     (mode->height >= f->min_height) &&
			     (mode->height <= f->max_height)) &&
			    ((mode->pixel_freq >= f->min_clock) &&
			     (mode->pixel_freq <= f->max_clock))) {
				return mode;
			}
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

	if (!comp || pipe < 0)
		return NULL;

	o = c->outputs[pipe];
	if (!o)
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

static void cb_compositor_set_desktop_layout(struct compositor *comp,
					     struct cb_rect *canvas)
{

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
	if (output->enabled) {
		if (output->repaint_status != REPAINT_NOT_SCHEDULED) {
			return;
		}
	}

	//printf("repaint output %d\n", output->pipe);
	/* comp_debug("repaint output %d", output->pipe); */
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

static void cancel_mc_buffer(struct cb_output *o)
{
	struct scanout_task *sot, *sot_next;
	struct cb_compositor *c = o->c;

	list_for_each_entry_safe(sot, sot_next, &o->so_tasks, link) {
		if (sot->plane == o->cursor_plane) {
			if (!sot->buffer)
				continue;
			if (sot->buffer->dirty & (1U << o->pipe)) {
				list_del(&sot->link);
				sot->buffer->dirty &= ~(1U << o->pipe);
				printf("Cancel a mc buffer %08X pending %d\n",
					sot->buffer->dirty,
					c->mc_update_pending);
				if (!sot->buffer->dirty) {
					if (c->mc_update_pending) {
						c->mc_update_pending = false;
						cb_signal_emit(
						  &c->mc_update_complete_signal,
						  NULL);
					}
				}
				cb_cache_put(sot, c->so_task_cache);
			}
		}
	}
}

static s32 cb_compositor_set_mouse_cursor(struct compositor *comp,
					  u8 *data, u32 width, u32 height,
					  u32 stride,
					  s32 hot_x, s32 hot_y,
					  bool alpha_src_pre_mul)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_buffer *buffer;
	bool dirty = false;
	bool mc_on_screen;
	s32 i;
#ifdef MC_SINGLE_SYNC
	bool sync_output_found;
#endif

	if (!data || !width || width > MC_MAX_WIDTH || !height ||
	    height > MC_MAX_HEIGHT || !stride || !comp) {
		printf("illegal param\n");
		return -EINVAL;
	}

	if (c->mc_update_pending) {
		printf("mc update pending\n");
		return -EBUSY;
	}

	//printf("...........fill cursor %d, %p\n", 1 - c->mc_buf_cur,
	//	c->mc_buf[1 - c->mc_buf_cur]);
	fill_cursor(c, data, width, height, stride);

	c->mc_hot_pos.x = hot_x;
	c->mc_hot_pos.y = hot_y;
	c->mc_alpha_src_pre_mul = alpha_src_pre_mul;

	/* if mouse is hide, just update cursor data */
	if (c->mc_hide) {
		printf("cursor is hide\n");
		return -ENODEV;
	}

	buffer = c->mc_buf[1 - c->mc_buf_cur];

	/* switch buffer index */
	c->mc_buf_cur = 1 - c->mc_buf_cur;
#ifdef MC_DEBUG
	printf("Switch mc_buf_cur -> %d\n", c->mc_buf_cur);
#endif

	scanout_buffer_dirty_init(buffer);
	for (i = 0; i < c->count_outputs; i++) {
		/* if monitor is pluged out, do not set dirty bit */
		if (!c->outputs[i]->enabled)
			continue;
#ifdef MC_SINGLE_SYNC
		sync_output_found = false;
#endif
		/*
		 * re-calculate mouse display position, because the hot point
		 * may be changed.
		 */
		mc_on_screen = c->outputs[i]->mc_on_screen;
		update_mc_view_port(c->outputs[i], false);
		if (c->outputs[i]->mc_on_screen) {
#ifdef MC_SINGLE_SYNC
			if (!sync_output_found) {
				sync_output_found = true;
				scanout_set_buffer_dirty(buffer,
							 c->outputs[i]->output);
			}
#else
			scanout_set_buffer_dirty(buffer, c->outputs[i]->output);
#endif
		}
		/* if mouse cursor is not on the screen (e.g. extended screen),
		 * do not set dirty bit */
		if (!c->outputs[i]->mc_on_screen) {
			if (mc_on_screen) {
				cancel_mc_buffer(c->outputs[i]);
				add_mc_buffer_to_task(c, NULL,
						1U << c->outputs[i]->pipe);
			} else {
				continue;
			}
		}

		/****************************************************
		 * repaint
		 * sot = cb_cache_get(c->so_task_cache, false);
		 * sot->buffer = c->mc_buf[c->mc_buf_cur];
		 * sot->plane = c->outputs[i]->cursor_plane;
		 * mask mouse cursor update pending
		 ****************************************************/
		add_mc_buffer_to_task(c, c->mc_buf[c->mc_buf_cur],
				      1U << c->outputs[i]->pipe);
		//printf("repaint output %d's cursor\n", c->outputs[i]->pipe);
		cb_compositor_repaint_by_output(c->outputs[i]);
		dirty = true;
	}
	//printf(">>>>>> Cursor dirty: %d\n", buffer->dirty);

	if (dirty) {
		c->mc_update_pending = true;
	} else {
#ifdef MC_DEBUG
		printf("no dirty\n");
#endif
		return -EINVAL;
	}

	return 0;
}

static bool cb_compositor_set_mouse_updated_notify(
		struct compositor *comp, struct cb_listener *mc_updated_l)
{
	struct cb_compositor *c = to_cb_c(comp);

	if (!c || !mc_updated_l)
		return true;

	cb_signal_add(&c->mc_update_complete_signal, mc_updated_l);
	return c->mc_update_pending;
}

enum input_type {
	INPUT_TYPE_UNKNOWN = 0,
	INPUT_TYPE_MOUSE,
	INPUT_TYPE_KBD,
};

struct input_device {
	s32 fd;
	enum input_type type;
	char name[256];
	struct cb_event_source *input_source;
	struct cb_compositor *c;
	struct list_head link;
};

static enum input_type test_dev(const char *dev)
{
	s32 fd;
	u8 evbit[EV_MAX/8 + 1];
	char buffer[64];
	u32 i;

	comp_debug("begin test %s", dev);
	fd = open(dev, O_RDWR | O_CLOEXEC, 0644);
	if (fd < 0)
		return INPUT_TYPE_UNKNOWN;

	memset(buffer,0, sizeof(buffer));
	ioctl(fd, EVIOCGNAME(sizeof(buffer) - 1), buffer);

#ifndef test_bit
#define test_bit(bit, array)    (array[bit/8] & (1<<(bit%8)))
#endif
	ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit);
	close(fd);

	for (i = 0; i < EV_MAX; i++) {
		if (test_bit(i, evbit)) {
			switch (i) {
			case EV_KEY:
				comp_debug("cap: key");
				break;
			case EV_REL:
				comp_debug("cap: rel");
				break;
			case EV_ABS:
				comp_debug("cap: abs");
				break;
			case EV_MSC:
				comp_debug("cap: msc");
				break;
			case EV_LED:
				comp_debug("cap: led");
				break;
			case EV_SND:
				comp_debug("cap: sound");
				break;
			case EV_REP:
				comp_debug("cap: repeat");
				break;
			case EV_FF:
				comp_debug("cap: feedback");
				break;
			case EV_SYN:
				comp_debug("cap: sync");
				break;
			}
		}
	}
	
	if (test_bit(EV_KEY, evbit) && test_bit(EV_REL, evbit)
			&& test_bit(EV_SYN, evbit)) {
		return INPUT_TYPE_MOUSE;
	} else if(test_bit(EV_KEY, evbit) && test_bit(EV_REP, evbit)
	    && test_bit(EV_LED, evbit)) {
		return INPUT_TYPE_KBD;
	}
	comp_debug("end test %s", dev);

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

static void refresh_mc_desktop_pos(struct cb_compositor *c, s32 dx, s32 dy,
				   bool hide)
{
	s32 i, cur_screen;
	bool mc_on_screen;

	cur_screen = check_mouse_pos(c, c->mc_desktop_pos.x,
				     c->mc_desktop_pos.y);
	if (cur_screen < 0) {
		reset_mouse_pos(c);
	} else {
		normalize_mouse_pos(c, cur_screen, dx, dy);
	}

	/* mask mouse cursor update pending */
	for (i = 0; i < c->count_outputs; i++) {
		/* if monitor is pluged out, do not set dirty bit */
		if (!c->outputs[i]->enabled)
			continue;
		mc_on_screen = c->outputs[i]->mc_on_screen;
		update_mc_view_port(c->outputs[i], true);
		if (c->mc_hide)
			continue;

		if (!c->mc_hide && hide) {
			if (mc_on_screen) {
				cancel_mc_buffer(c->outputs[i]);
				add_mc_buffer_to_task(c, NULL,
				      1U << c->outputs[i]->pipe);
			}
			continue;
		}
		if (!c->outputs[i]->mc_on_screen) {
			if (mc_on_screen) {
				cancel_mc_buffer(c->outputs[i]);
				add_mc_buffer_to_task(c, NULL,
				      1U << c->outputs[i]->pipe);
			}
			continue;
		}
		add_mc_buffer_to_task(c, c->mc_buf[c->mc_buf_cur],
				      1U << c->outputs[i]->pipe);
	}

	if (!c->mc_hide && hide)
		c->mc_hide = true;

	cb_compositor_repaint(c);
}

static s32 cb_compositor_hide_mouse_cursor(struct compositor *comp)
{
	struct cb_compositor *c = to_cb_c(comp);

	refresh_mc_desktop_pos(c, 0, 0, true);

	return 0;
}

static s32 cb_compositor_show_mouse_cursor(struct compositor *comp)
{
	struct cb_compositor *c = to_cb_c(comp);

	c->mc_hide = false;
	refresh_mc_desktop_pos(c, 0, 0, false);

	return 0;
}

static void event_proc(struct cb_compositor *c, struct input_event *evts,
		       s32 cnt)
{
	s32 src, dst;
	s32 dx, dy;

	dx = dy = 0;
	dst = 0;

	for (src = 0; src < cnt; src++) {
		switch (c->buffer[src].type) {
		case EV_SYN:
			if (dx || dy) {
				cursor_accel_set(&dx, &dy, 2.0f);
				refresh_mc_desktop_pos(c, dx, dy, false);
/*
				mouse_move_proc(disp, dx, dy);
				disp->tx_buf[dst].type = EV_ABS;
				disp->tx_buf[dst].code = ABS_X | ABS_Y;
				disp->tx_buf[dst].v.pos.x = disp->abs_x;
				disp->tx_buf[dst].v.pos.y = disp->abs_y;
				disp->tx_buf[dst].v.pos.dx = (s16)dx;
				disp->tx_buf[dst].v.pos.dy = (s16)dy;
*/
				dst++;
			}
/*
			disp->tx_buf[dst].type = EV_SYN;
			disp->tx_buf[dst].code = disp->buffer[src].code;
			disp->tx_buf[dst].v.value = disp->buffer[src].value;
*/
			dst++;
			break;
		case EV_MSC:
			break;
		case EV_LED:
			break;
		case EV_KEY:
/*
			disp->tx_buf[dst].type = EV_KEY;
			disp->tx_buf[dst].code = disp->buffer[src].code;
			disp->tx_buf[dst].v.value = disp->buffer[src].value;
*/
			dst++;
			break;
		case EV_REP:
/*
			disp->tx_buf[dst].type = EV_REP;
			disp->tx_buf[dst].code = disp->buffer[src].code;
			disp->tx_buf[dst].v.value = disp->buffer[src].value;
*/
			dst++;
			break;
		case EV_REL:
			switch (c->buffer[src].code) {
			case REL_WHEEL:
/*
				disp->tx_buf[dst].type = EV_REL;
				disp->tx_buf[dst].code = REL_WHEEL;
				disp->tx_buf[dst].v.value =
					disp->buffer[src].value;
*/
				dst++;
				break;
			case REL_X:
				dx = c->buffer[src].value;
				break;
			case REL_Y:
				dy = c->buffer[src].value;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}

	/* send raw event */
/*
	list_for_each_entry(client, &disp->clients, link) {
		u32 len = sizeof(struct clv_input_event) * dst;
		clv_send(client->sock, &len, sizeof(u32));
		clv_send(client->sock, disp->tx_buf, len);
	}
*/
}

static s32 read_input_event(s32 fd, u32 mask, void *data)
{
	struct input_device *dev = data;
	struct cb_compositor *c = dev->c;
	s32 ret;

	ret = read(fd, c->buffer, c->buffer_sz);
	if (ret <= 0) {
		return ret;
	}
	event_proc(c, c->buffer, ret / sizeof(struct input_event));

	return 0;
}

static void input_device_destroy(struct input_device *dev)
{
	if (!dev)
		return;

	comp_debug("Destroy input %s", dev->name);
	list_del(&dev->link);
	if (dev->input_source)
		cb_event_source_remove(dev->input_source);
	close(dev->fd);
	free(dev);
}

/*
 * update kbd sysfs device path
 * search around the whole device list, search for TYPE==INPUT_TYPE_KBD.
 * If there is still a keyboard, update the file path, otherwise remove
 * the file /tmp/kbd_name.
 */
static void update_kbd_name(struct cb_compositor *c)
{
	struct input_device *dev;
	bool kbd_empty = true;
	char cmd[64];

	list_for_each_entry(dev, &c->input_devs, link) {
		if (dev->type == INPUT_TYPE_KBD) {
			kbd_empty = false;
			break;
		}
	}

	if (kbd_empty) {
		system("rm -f /tmp/kbd_name");
		comp_debug("no kbd left.\n");
	} else {
		memset(cmd, 0, 64);
		sprintf(cmd, "echo %s > /tmp/kbd_name", dev->name);
		system(cmd);
		comp_debug("update kbd_name as %s", dev->name);
	}
}

static void remove_input_device(struct cb_compositor *c, const char *devpath)
{
	struct input_device *dev_present, *next;

	list_for_each_entry_safe(dev_present, next, &c->input_devs, link) {
		if (!strcmp(dev_present->name, devpath)) {
			comp_debug("Remove %s, type %s", devpath,
				dev_present->type == INPUT_TYPE_MOUSE?"M":"K");
			input_device_destroy(dev_present);
			update_kbd_name(c);
			return;
		}
	}
}

static void add_input_device(struct cb_compositor *c, const char *devpath)
{
	struct input_device *dev, *b;
	enum input_type type;
	s32 fd;
	char cmd[64];

	type = test_dev(devpath);
	if (type == INPUT_TYPE_UNKNOWN)
		return;

	fd = open(devpath, O_RDWR | O_CLOEXEC, 0644);
	if (fd < 0) {
		comp_err("cannot open %s, %s", devpath, strerror(errno));
		return;
	}

	list_for_each_entry(b, &c->input_devs, link) {
		if (!strcmp(b->name, devpath)) {
			comp_err("already add device %s, skip it.", devpath);
			close(fd);
			return;
		}
	}

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return;
	dev->fd = fd;
	dev->type = type;
	dev->c = c;
	memset(dev->name, 0, 256);
	strcpy(dev->name, devpath);
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

	list_add_tail(&dev->link, &c->input_devs);
	comp_debug("Add %s, type %s",
		   dev->name,type == INPUT_TYPE_MOUSE? "M":"K");
	if (type == INPUT_TYPE_KBD) {
		memset(cmd, 0, 64);
		sprintf(cmd, "echo %s > /tmp/kbd_name", devpath);
		system(cmd);
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

	if (c->buffer) {
		free(c->buffer);
		c->buffer = NULL;
		c->buffer_sz = 0;
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
}

static void scan_input_devs(struct cb_compositor *c, const char *input_dir)
{
	char *devname;
	char *filename;
	DIR *dir;
	struct dirent *de;
	enum input_type type;

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
		type = test_dev(devname);
		if (type == INPUT_TYPE_UNKNOWN)
			continue;
		add_input_device(c, devname);
	}

	closedir(dir);
	free(devname);
}

static s32 cb_compositor_input_init(struct cb_compositor *c)
{
	c->buffer_sz = sizeof(struct input_event) * 4096;
	c->buffer = (struct input_event *)malloc(c->buffer_sz);
	if (!c->buffer)
		goto err;

	memset(c->buffer, 0, c->buffer_sz);

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
	sot->dst = &o->crtc_view_port;
	sot->alpha_src_pre_mul = true;
	list_add_tail(&sot->link, &o->so_tasks);
}

static bool setup_view_output_mask(struct cb_view *view,
				   struct cb_compositor *c)
{
	struct cb_output *o;
	s32 i;
	struct cb_region view_area, output_area;

	view->output_mask = 0;
	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (!o->enabled)
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
			view->output_mask |= (1U << o->pipe);
		}
		cb_region_fini(&output_area);
		cb_region_fini(&view_area);
	}

	return (view->output_mask != 0);
}

static void cb_compositor_commit_surface(struct compositor *comp,
					struct cb_surface *surface);

static void surface_flipped_cb(struct cb_listener *listener, void *data)
{
	struct cb_surface *surface = container_of(listener, struct cb_surface,
						  flipped_l);
	struct cb_view *view = surface->view;
#ifdef TEST_DUMMY_VIEW
	struct cb_compositor *c = surface->c;
#endif

	if (view->painted) {
		cancel_renderer_surface(surface);
#if 1
#ifdef TEST_DUMMY_VIEW
		c->dummy_index = 1 - c->dummy_index;
		c->dummy_surf.buffer_pending = &c->dummy_buf[c->dummy_index].base;
		/* printf("commit dummy surface\n"); */
		cb_compositor_commit_surface(&c->base, surface);
#endif
#endif
	}
}

static void cb_compositor_commit_surface(struct compositor *comp,
					 struct cb_surface *surface)
{
	struct cb_compositor *c = to_cb_c(comp);
	s32 i;
	struct cb_output *o;

	/* printf("commit surface %p's buffer %p\n", surface->buffer_pending);*/
	if (!surface->buffer_pending) {
		/* remove view */
		printf("remove view link\n");
		list_del(&surface->view->link);
		cancel_renderer_surface(surface);
		cb_compositor_repaint(c);
	} else if (surface->buffer_pending == surface->buffer_cur) {
		/* view changed */

		setup_view_output_mask(surface->view, c);
		
		cb_compositor_repaint(c);
	} else {
		/* buffer changed */

		setup_view_output_mask(surface->view, c);
		/*
		printf("bind buffer %p\n", surface->buffer_pending);
		*/
		c->r->attach_buffer(c->r, surface, surface->buffer_pending);
		if (surface->buffer_pending->info.type == CB_BUF_TYPE_SHM)
			c->r->flush_damage(c->r, surface);
		surface->width = surface->buffer_pending->info.width;
		surface->height = surface->buffer_pending->info.height;
		surface->buffer_cur = surface->buffer_pending;

		for (i = 0; i < c->count_outputs; i++) {
			o = c->outputs[i];
			if (surface->view->output_mask &
			    (1U << o->pipe)) {
				if (surface->output && (surface->output != o)) {
					printf("[--- output route %d -> %d ]\n",
						surface->output->pipe,
						o->pipe);
				}
				surface->output = o;
				surface->flipped_l.notify = surface_flipped_cb;
				/*
				printf("add flipped listener %d\n", o->pipe);
				*/
				cb_signal_add(&o->surface_flipped_signal,
					      &surface->flipped_l);
				break;
			}
		}
#ifdef TEST_DUMMY_VIEW
		surface->c = c;
#endif
		cb_compositor_repaint(c);
	}

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (o->enabled &&
		    (surface->view->output_mask & (1U << o->pipe))) {
			o->renderable_buffer_changed = true;
		}
	}
}

static void *try_to_assign_hwplane(struct cb_output *o,
				   struct cb_buffer *buffer,
				   struct cb_rect *src,
				   struct cb_rect *dst)
{
	return NULL;
}

static void add_dma_buf_to_task(struct cb_compositor *c,
				struct cb_buffer *buffer,
				struct cb_rect *src,
				struct cb_rect *dst,
				bool alpha_src_pre_mul,
				u32 mask)
{
	s32 i;
	struct cb_output *o;
	struct scanout_task *sot;
	bool find;
	struct plane *plane;

	for (i = 0; i < c->count_outputs; i++) {
		o = c->outputs[i];
		if (!(mask & (1U << o->pipe)))
			continue;
		//plane = assign_plane(o, buffer, src, dst);
		if (plane != o->primary_plane) {
			o->show_rendered_buffer = true;
		} else {
			o->show_rendered_buffer = false;
			/* TODO hide rendered buffer */
		}
		find = false;
		list_for_each_entry(sot, &o->so_tasks, link) {
			if (sot->plane == plane) {
				sot->buffer = buffer;
				find = true;
				break;
			}
		}
		if (!find) {
			sot = cb_cache_get(c->so_task_cache, false);
			sot->buffer = buffer;
			sot->plane = plane;
			sot->zpos = -1;
			sot->src = src;
			sot->dst = dst;
			sot->alpha_src_pre_mul = alpha_src_pre_mul;
			list_add_tail(&sot->link, &o->so_tasks);
		}
	}
}

static void cb_compositor_commit_dma_buf(struct compositor *comp,
					 struct cb_surface *surface)
{
	struct cb_compositor *c = to_cb_c(comp);
	struct cb_view *view = surface->view;
	s32 i;
	struct cb_output *o;

	view->direct_show = true;
	if (!surface->buffer_pending) {
		/* remove view */
		list_del(&view->link);
		/* TODO cancel buffer, add NULL task */
		cb_compositor_repaint(c);
	} else if (surface->buffer_pending == surface->buffer_cur) {
		/* view changed */
		setup_view_output_mask(view, c);
		cb_compositor_repaint(c);
	} else {
		/* buffer changed */
		setup_view_output_mask(view, c);
		/*
		printf("bind buffer %p\n", surface->buffer_pending);
		*/
		surface->width = surface->buffer_pending->info.width;
		surface->height = surface->buffer_pending->info.height;
		surface->buffer_cur = surface->buffer_pending;

		surface->output = NULL;
		for (i = 0; i < c->count_outputs; i++) {
			o = c->outputs[i];
			if (surface->view->output_mask &
			    (1U << o->pipe)) {
				scanout_set_buffer_dirty(surface->buffer_cur,
					o->output);
			}
		}
/*
		add_dma_buf_to_task(c, surface->buffer_cur,
				    surface->view->plane,
				    NULL,
				    NULL,
				bool alpha_src_pre_mul,
				u32 mask)
*/
		cb_compositor_repaint(c);
	}
}

static void do_renderer_repaint(struct cb_output *o)
{
	struct cb_compositor *c = o->c;
	struct r_output *ro = o->ro;
	struct cb_buffer *buffer;
	bool repainted;

	if (o->renderable_buffer_changed) {
		if (list_empty(&c->views)) {
			printf("set output %d's rbuf_cur NULL\n", o->pipe);
			o->rbuf_cur = NULL;
			goto out;
		}

		repainted = ro->repaint(ro, &c->views);
		if (!repainted)
			goto out;

		buffer = c->so->get_surface_buf(c->so, o->native_surface);
		if (!buffer) {
			comp_err("failed to get surface buffer.");
			goto out;
		}
		o->rbuf_cur = buffer;
	}

out:
	add_renderer_buffer_to_task(o, o->rbuf_cur);
	o->renderable_buffer_changed = false;
}

#if 0
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
	bool primary_empty, output_empty, cursor_empty, empty = true;

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
		if (o->repaint_status != REPAINT_SCHEDULED)
			continue;

		clock_gettime(c->clock_type, &now);
		msec_to_repaint = timespec_sub_to_msec(&o->next_repaint, &now);
		if (msec_to_repaint > 1) {
			/* the timer cb is not alarmed by this output */
			continue;
		} else if (msec_to_repaint < -1) {
			comp_warn("output %d msec_to_repaint (%ld) < -1 recalc",
				  o->pipe, msec_to_repaint);
		}
		primary_empty = true;
		output_empty = true;
		cursor_empty = true;

		/* do renderer's repaint */
		do_renderer_repaint(o);

		list_for_each_entry_safe(sot, sot_next, &o->so_tasks, link) {
			list_del(&sot->link);
			if (sot->plane == o->primary_plane &&
			    sot->buffer != NULL)
				primary_empty = false;
			output_empty = false;
			if (sot->buffer == NULL) {
				cb_cache_put(sot, c->so_task_cache);
				continue;
			}
			if (o->cursor_plane == sot->plane) {
#ifdef MC_DEBUG
				printf("Commit MC: %ld, cur %d\n",
					(s64)(sot->buffer->userdata),
					c->mc_buf_cur);
				comp_debug("Commit MC: %ld, cur %d",
					(s64)(sot->buffer->userdata),
					c->mc_buf_cur);
				/*
				printf("Commit Dirty: %08X\n",
					sot->buffer->dirty);
				*/
#endif
				cursor_empty = false;
			}
			/*
			printf("commit fb: %d,%d %ux%u\n", sot->src->pos.x,
					sot->src->pos.y,
					sot->src->w,
					sot->src->h);
			*/
			scanout_commit_add_fb_info(commit,
					   sot->buffer,
					   o->output,
					   sot->plane,
					   sot->src,
					   sot->dst,
					   -1,
					   sot->alpha_src_pre_mul);
			cb_cache_put(sot, c->so_task_cache);
			empty = false;
		}

		if (!output_empty) {
			if (primary_empty) {
				/* printf("primary empty %d\n", o->pipe); */
				scanout_commit_add_fb_info(commit, o->dummy,
					o->output, o->primary_plane,
					&o->dummy_src, &o->crtc_view_port,
					0, true);
				empty = false;
			}
			if (cursor_empty) {
				if (!c->mc_hide && o->mc_on_screen) {
					/* printf("cursor empty\n"); */
					scanout_commit_add_fb_info(commit,
						c->mc_buf[c->mc_buf_cur],
						o->output,
						o->cursor_plane,
						&c->mc_src,
						&o->mc_view_port,
						-1, false);
				}
			}
			printf("commit output %d\n", o->pipe);
			o->repaint_status = REPAINT_WAIT_COMPLETION;
		} else {
			o->repaint_status = REPAINT_NOT_SCHEDULED;
		}
	}

	if (empty)
		goto out;

	sd = c->so->scanout_data_alloc(c->so);
	c->so->fill_scanout_data(c->so, sd, commit);
	c->so->do_scanout(c->so, sd);
out:
	scanout_commit_info_free(commit);

	update_repaint_timer(c);
	return 0;
}
#else
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
		} else if (msec_to_repaint < -1) {
			comp_warn("output %d msec_to_repaint (%ld) < -1 recalc",
				  o->pipe, msec_to_repaint);
		}
		output_empty = true;

		/* do renderer's repaint */
		do_renderer_repaint(o);

		list_for_each_entry_safe(sot, sot_next, &o->so_tasks, link) {
			list_del(&sot->link);
			if (sot->buffer == NULL) {
				cb_cache_put(sot, c->so_task_cache);
				continue;
			}
			output_empty = false;
#ifdef MC_DEBUG
			if (o->cursor_plane == sot->plane) {
				printf("Commit MC: %ld, cur %d\n",
					(s64)(sot->buffer->userdata),
					c->mc_buf_cur);
				comp_debug("Commit MC: %ld, cur %d",
					(s64)(sot->buffer->userdata),
					c->mc_buf_cur);
				/*
				printf("Commit Dirty: %08X\n",
					sot->buffer->dirty);
				*/
			}
#endif
			/*
			printf("commit fb: %d,%d %ux%u\n", sot->src->pos.x,
					sot->src->pos.y,
					sot->src->w,
					sot->src->h);
			*/
			scanout_commit_add_fb_info(commit,
					   sot->buffer,
					   o->output,
					   sot->plane,
					   sot->src,
					   sot->dst,
					   -1,
					   sot->alpha_src_pre_mul);
			cb_cache_put(sot, c->so_task_cache);
			empty = false;
		}

		if (output_empty) {
			if (!c->mc_hide && o->mc_on_screen) {
				/* printf("cursor empty\n"); */
				scanout_commit_add_fb_info(commit,
					c->mc_buf[c->mc_buf_cur],
					o->output,
					o->cursor_plane,
					&c->mc_src,
					&o->mc_view_port,
					-1, false);
				output_empty = false;
				empty = false;
			}
		}

		if (output_empty) {
			/* printf("primary empty %d\n", o->pipe); */
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
#endif

#ifdef MC_DEBUG_BO_COMPLETE
static void mc_buf_complete_cb(struct cb_listener *listener, void *data)
{
	struct cb_buffer *buffer = data;
	s64 index = (s64)(buffer->userdata);

	struct cb_compositor *c = container_of(listener, struct cb_compositor,
					       mc_buf_l[index]);
	printf("MC CUR: %u, MC BO %ld Complete.\n", c->mc_buf_cur, index);
}
#endif

#ifdef TEST_DUMMY_VIEW
static void init_dummy_buf(struct cb_compositor *c)
{
	u32 *pixel;

	comp_debug("init background layer...");
	memset(&c->dummy_surf, 0, sizeof(c->dummy_surf));
	c->dummy_surf.is_opaque = true;
	c->dummy_surf.view = &c->dummy_view;
	cb_region_init_rect(&c->dummy_surf.damage, 0, 0, 1024, 768);
	cb_region_init_rect(&c->dummy_surf.opaque, 0, 0, 1024, 768);
	cb_signal_init(&c->dummy_surf.destroy_signal);

	memset(&c->dummy_view, 0, sizeof(c->dummy_view));
	c->dummy_view.surface = &c->dummy_surf;
	c->dummy_view.area.pos.x = 2000;
	c->dummy_view.area.pos.y = 0;
	c->dummy_view.area.w = 1024;
	c->dummy_view.area.h = 768;
	c->dummy_view.alpha = 1.0f;
	/* c->dummy_view.output_mask = 0x01; */
	list_add_tail(&c->dummy_view.link, &c->views);

	memset(&c->dummy_buf[0], 0, sizeof(c->dummy_buf[0]));
	c->dummy_buf[0].base.info.type = CB_BUF_TYPE_SHM;
	c->dummy_buf[0].base.info.width = 1024;
	c->dummy_buf[0].base.info.height = 768;
	memset(&c->dummy_buf[1], 0, sizeof(c->dummy_buf[1]));
	c->dummy_buf[1].base.info.type = CB_BUF_TYPE_SHM;
	c->dummy_buf[1].base.info.width = 1024;
	c->dummy_buf[1].base.info.height = 768;
#ifdef TEST_DUMMY_YUV444P
	c->dummy_buf[0].base.info.strides[0] = 1024;
	c->dummy_buf[0].base.info.offsets[0] = 0;
	c->dummy_buf[0].base.info.sizes[0] = c->dummy_buf[0].base.info.strides[0]
					* c->dummy_buf[0].base.info.height;
	c->dummy_buf[0].base.info.strides[1] = 1024;
	c->dummy_buf[0].base.info.offsets[1] = c->dummy_buf[0].base.info.sizes[0];
	c->dummy_buf[0].base.info.sizes[1] = c->dummy_buf[0].base.info.strides[1]
					* c->dummy_buf[0].base.info.height;
	c->dummy_buf[0].base.info.strides[2] = 1024;
	c->dummy_buf[0].base.info.offsets[2] = c->dummy_buf[0].base.info.sizes[0]
				+ c->dummy_buf[0].base.info.sizes[1];
	c->dummy_buf[0].base.info.sizes[2] = c->dummy_buf[0].base.info.strides[2]
					* c->dummy_buf[0].base.info.height;
	c->dummy_buf[0].base.info.pix_fmt = CB_PIX_FMT_YUV444;
	c->dummy_buf[0].base.info.planes = 3;
	strcpy(c->dummy_buf[0].name, "shm_dummy_buf");
	unlink(c->dummy_buf[0].name);
	cb_shm_init(&c->dummy_buf[0].shm, c->dummy_buf[0].name,
		    c->dummy_buf[0].base.info.sizes[0] * 3, 1);
	pixel = (u32 *)c->dummy_buf[0].shm.map;

	c->dummy_buf[1].base.info.strides[0] = 1024;
	c->dummy_buf[1].base.info.offsets[0] = 0;
	c->dummy_buf[1].base.info.sizes[0] = c->dummy_buf[1].base.info.strides[0]
					* c->dummy_buf[1].base.info.height;
	c->dummy_buf[1].base.info.strides[1] = 1024;
	c->dummy_buf[1].base.info.offsets[1] = c->dummy_buf[1].base.info.sizes[0];
	c->dummy_buf[1].base.info.sizes[1] = c->dummy_buf[1].base.info.strides[1]
					* c->dummy_buf[1].base.info.height;
	c->dummy_buf[1].base.info.strides[2] = 1024;
	c->dummy_buf[1].base.info.offsets[2] = c->dummy_buf[1].base.info.sizes[0]
				+ c->dummy_buf[1].base.info.sizes[1];
	c->dummy_buf[1].base.info.sizes[2] = c->dummy_buf[1].base.info.strides[2]
					* c->dummy_buf[1].base.info.height;
	c->dummy_buf[1].base.info.pix_fmt = CB_PIX_FMT_YUV444;
	c->dummy_buf[1].base.info.planes = 3;
	strcpy(c->dummy_buf[1].name, "shm_dummy_buf");
	unlink(c->dummy_buf[1].name);
	cb_shm_init(&c->dummy_buf[1].shm, c->dummy_buf[1].name,
		    c->dummy_buf[1].base.info.sizes[0] * 3, 1);
#else
#ifdef TEST_DUMMY_NV24
	c->dummy_buf[0].base.info.strides[0] = 1024;
	c->dummy_buf[0].base.info.offsets[0] = 0;
	c->dummy_buf[0].base.info.sizes[0] = c->dummy_buf[0].base.info.strides[0]
					* c->dummy_buf[0].base.info.height;
	c->dummy_buf[0].base.info.strides[1] = 1024;
	c->dummy_buf[0].base.info.offsets[1] = c->dummy_buf[0].base.info.sizes[0];
	c->dummy_buf[0].base.info.sizes[1] = c->dummy_buf[0].base.info.strides[1]
					* c->dummy_buf[0].base.info.height;
	c->dummy_buf[0].base.info.pix_fmt = CB_PIX_FMT_NV24;
	c->dummy_buf[0].base.info.planes = 2;
	strcpy(c->dummy_buf[0].name, "shm_dummy_buf[0]");
	unlink(c->dummy_buf[0].name);
	cb_shm_init(&c->dummy_buf[0].shm, c->dummy_buf[0].name,
		    c->dummy_buf[0].base.info.sizes[0] * 3, 1);
	pixel = (u32 *)c->dummy_buf[0].shm.map;

	c->dummy_buf[1].base.info.strides[0] = 1024;
	c->dummy_buf[1].base.info.offsets[0] = 0;
	c->dummy_buf[1].base.info.sizes[0] = c->dummy_buf[1].base.info.strides[0]
					* c->dummy_buf[1].base.info.height;
	c->dummy_buf[1].base.info.strides[1] = 1024;
	c->dummy_buf[1].base.info.offsets[1] = c->dummy_buf[1].base.info.sizes[0];
	c->dummy_buf[1].base.info.sizes[1] = c->dummy_buf[1].base.info.strides[1]
					* c->dummy_buf[1].base.info.height;
	c->dummy_buf[1].base.info.pix_fmt = CB_PIX_FMT_NV24;
	c->dummy_buf[1].base.info.planes = 2;
	strcpy(c->dummy_buf[1].name, "shm_dummy_buf[1]");
	unlink(c->dummy_buf[1].name);
	cb_shm_init(&c->dummy_buf[1].shm, c->dummy_buf[1].name,
		    c->dummy_buf[1].base.info.sizes[0] * 3, 1);
#else
	c->dummy_buf[0].base.info.strides[0] = 1024 * 4;
	c->dummy_buf[0].base.info.sizes[0] = c->dummy_buf[0].base.info.strides[0]
						* c->dummy_buf[0].base.info.height;
	c->dummy_buf[0].base.info.pix_fmt = CB_PIX_FMT_ARGB8888;
	c->dummy_buf[0].base.info.planes = 1;
	strcpy(c->dummy_buf[0].name, "shm_dummy_buf[0]");
	unlink(c->dummy_buf[0].name);
	cb_shm_init(&c->dummy_buf[0].shm, c->dummy_buf[0].name,
		    c->dummy_buf[0].base.info.sizes[0], 1);
	pixel = (u32 *)c->dummy_buf[0].shm.map;

	c->dummy_buf[1].base.info.strides[0] = 1024 * 4;
	c->dummy_buf[1].base.info.sizes[0] = c->dummy_buf[1].base.info.strides[0]
						* c->dummy_buf[1].base.info.height;
	c->dummy_buf[1].base.info.pix_fmt = CB_PIX_FMT_ARGB8888;
	c->dummy_buf[1].base.info.planes = 1;
	strcpy(c->dummy_buf[1].name, "shm_dummy_buf[1]");
	unlink(c->dummy_buf[1].name);
	cb_shm_init(&c->dummy_buf[1].shm, c->dummy_buf[1].name,
		    c->dummy_buf[1].base.info.sizes[0], 1);
#endif
#endif
	
#ifdef TEST_DUMMY_YUV444P
	{
		s32 fd = open("/tmp/1024x768_yuv444p.yuv", O_RDONLY, 0644);
		read(fd, (u8 *)c->dummy_buf[0].shm.map, 
			c->dummy_buf[0].base.info.sizes[0] * 3);
		close(fd);
	}
#else
#ifdef TEST_DUMMY_NV24
	{
		s32 fd = open("/tmp/1024x768_nv24.yuv", O_RDONLY, 0644);
		read(fd, (u8 *)c->dummy_buf[0].shm.map, 
			c->dummy_buf[0].base.info.sizes[0] * 3);
		close(fd);
	}
#else
	for (s32 i = 0; i < c->dummy_buf[0].base.info.sizes[0] / 4; i++)
		pixel[i] = 0xFF404040;
#endif
#endif

	c->dummy_surf.buffer_pending = &c->dummy_buf[0].base;
	c->dummy_index = 0;
/*
	c->r->attach_buffer(c->r, &c->dummy_surf,
			    c->dummy_surf.buffer_pending);
	c->dummy_surf.width = c->dummy_surf.buffer_pending->info.width;
	c->dummy_surf.height = c->dummy_surf.buffer_pending->info.height;
	c->r->flush_damage(c->r, &c->dummy_surf);
*/
}
#endif

#ifdef TEST_DUMMY_VIEW
static s32 test_rm_view_timer_cb(void *data)
{
	struct cb_compositor *c = data;
	struct cb_surface *surface = &c->dummy_surf;

	if (surface->buffer_pending) {
		surface->buffer_pending = NULL;
		printf("remove dummy view!!!!\n");
		cb_event_source_timer_update(c->test_rm_view_timer,
				     15000, 0);
	} else {
		printf("reshow dummy view!!!!\n");
		list_add_tail(&c->dummy_view.link, &c->views);
		c->dummy_index = 1 - c->dummy_index;
		surface->buffer_pending = &c->dummy_buf[c->dummy_index].base;
		cb_event_source_timer_update(c->test_rm_view_timer, 10000, 0);
	}
	/* printf("commit dummy surface in timer\n"); */
	cb_compositor_commit_surface(&c->base, surface);

	return 0;
}
#endif

struct compositor *compositor_create(char *device_name,
				     struct cb_event_loop *loop,
				     struct pipeline *pipecfgs,
				     s32 count_outputs)
{
	struct cb_compositor *c;
	s32 i, vid;
	struct cb_buffer_info info;

	c = calloc(1, sizeof(*c));
	if (!c)
		return NULL;

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

	INIT_LIST_HEAD(&c->views);
#ifdef TEST_DUMMY_VIEW
	init_dummy_buf(c);
	c->test_rm_view_timer = cb_event_loop_add_timer(c->loop,
							test_rm_view_timer_cb,
							c);
	//cb_event_source_timer_update(c->test_rm_view_timer,
	//			     5000, 0);
#endif

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

	/* create cursor bo */
	memset(&info, 0, sizeof(info));
	info.pix_fmt = CB_PIX_FMT_ARGB8888;
	info.width = MC_MAX_WIDTH;
	info.height = MC_MAX_HEIGHT;
	for (i = 0; i < 2; i++) {
		printf("create cursor bo %d\n", i);
		c->mc_buf[i] = c->so->cursor_bo_create(c->so, &info);
		if (!c->mc_buf[i])
			goto err;
		c->mc_buf[i]->userdata = (void *)((s64)(i));
#ifdef MC_DEBUG_BO_COMPLETE
		c->mc_buf_l[i].notify = mc_buf_complete_cb;
		c->so->add_buffer_complete_notify(c->so, c->mc_buf[i],
						  &c->mc_buf_l[i]);
#endif
	}
	c->mc_buf_cur = 1;
	fill_cursor(c, DEF_MC_DAT, DEF_MC_WIDTH, DEF_MC_HEIGHT,
		    (DEF_MC_WIDTH << 2));
	c->mc_buf_cur = 0;

	c->mc_src.pos.x = c->mc_src.pos.y = 0;
	c->mc_src.w = MC_MAX_WIDTH;
	c->mc_src.h = MC_MAX_HEIGHT;

	c->mc_hide = false;

	/* register mc page flip handler */
	for (i = 0; i < 2; i++) {
		c->mc_flipped_l[i].notify = mc_flipped_cb;
		c->so->add_buffer_flip_notify(c->so, c->mc_buf[i],
					      &c->mc_flipped_l[i]);
	}

	/* init mouse cursor committed signal */
	cb_signal_init(&c->mc_update_complete_signal);

	for (i = 0; i < count_outputs; i++) {
		c->outputs[i] = cb_output_create(c, &pipecfgs[i], i);
		if (!c->outputs[i])
			goto err;
	}

	scanout_buffer_dirty_init(c->mc_buf[c->mc_buf_cur]);

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

#ifdef TEST_DUMMY_VIEW
	cb_compositor_commit_surface(&c->base, &c->dummy_surf);
#endif

	c->base.register_ready_cb = cb_compositor_register_ready_cb;
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
	c->base.hide_mouse_cursor = cb_compositor_hide_mouse_cursor;
	c->base.show_mouse_cursor = cb_compositor_show_mouse_cursor;
	c->base.set_mouse_cursor = cb_compositor_set_mouse_cursor;
	c->base.set_mouse_updated_notify=cb_compositor_set_mouse_updated_notify;

	return &c->base;

err:
	if (c)
		c->base.destroy(&c->base);

	return NULL;
}

