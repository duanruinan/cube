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

#define MAX_NR_OUTPUTS 32

struct cb_buffer {
	struct cb_buffer_info info;
	struct cb_signal destroy_signal;
	/* scanout backend emit flip signal when the buffer is commited into
	 * all outputs which it should be displayed on */
	struct cb_signal flip_signal;
	/* scanout backend emit complete signal when display do not use buffer
	 * any more */
	struct cb_signal complete_signal;
	/*
	 * 'dirty' is the outputs' bitmap. it indicates which output the buffer
	 * should be displayed on.
	 * Each bit of it is cleared when corresponding output is flipped.
	 * Each bit is set when the buffer's content is changed. the compositor
	 * should decide which output it should be displayed on at this time.
	 */
	u32 dirty;

	/* link to client's buffer list */
	struct list_head link;

	struct cb_surface *surface;

	struct cb_listener dma_buf_flipped_l, dma_buf_completed_l;

	/* prevent to add buffer's complete listener more than one time */
	bool completed_l_added;
};

struct shm_buffer {
	struct cb_buffer base;
	struct cb_shm shm;
};

struct pipeline {
	s32 head_index;
	s32 output_index;
	s32 primary_plane_index;
	s32 cursor_plane_index;
};

struct compositor;

struct cb_surface {
	struct cb_compositor *c;
	void *client_agent;

	bool use_renderer;

	/*
	 * The new buffer should be rendered into renderer's surface.
	 * The protocal engine set the field.
	 */
	struct cb_buffer *buffer_pending;

	/*
	 * The buffer should be displayed (attached)
	 * 
	 * The commiter set the field.
	 *     for renderable buffer the compositor attach the buffer this 
	 *         field indicated to the surface, flush the texture
	 *         (if the buffer is SHM-BUF), repaint it finally.
	 *     for DMA-BUF buffer the compositor import the buffer to scanout
	 *         device, commit it directly to scanout device.
	 */
	struct cb_buffer *buffer_cur;
	
	struct cb_buffer *buffer_last;

	bool is_opaque;
	struct cb_signal destroy_signal;

	/* where it should to be displayed */
	struct cb_view *view;

	/*
	 * surface size
	 * it is the size of the buffer be attached currently,
	 * so it is a dynamically value.
	 */
	u32 width, height;

	struct cb_listener flipped_l;

	/*
	 * surface state
	 *
	 * it is used by renderer.
	 *     the current attached buffer (shm or DMA-BUF)
	 *     the shader and textures the renderer used to draw
	 */
	void *renderer_state;

	struct cb_region damage; /* used for texture partial upload */
	struct cb_region opaque; /* opaque area */

	struct cb_output *output; /* the output generate vblank signal */
	/************************************************************/

	/* link to client agent */
	struct list_head link;
};

struct cb_view {
	/* link to surface */
	struct cb_surface *surface;

	bool direct_show;

	/* link to compositor's view list */
	struct list_head link;

	/* display area in desktop coordinates */
	struct cb_rect area;

	s32 zpos; /* zpos */

	s32 pipe_locked;

	/* use this field to do alpha blending in renderer */
	float alpha;

	/*
	 * bitmap of outputs where the view should be displayed on
	 * it is used for judgeing whether this view should be repaint on
	 * a ro, and it is also used for setting dirty bit for DMA-BUF.
	 */
	u32 output_mask;

	/*
	 * used for DMA-BUF direct show surface
	 * in pipe order
	 */
	struct plane *planes[MAX_NR_OUTPUTS];

	/* used for DMA-BUF direct show (in pipe order ) */
	struct cb_rect dst_areas[MAX_NR_OUTPUTS];
	struct cb_rect src_areas[MAX_NR_OUTPUTS];

	bool painted;

	/* for float view: server do not send focus on / lost message */
	bool float_view;

	bool focus_on;
};

struct compositor {
	/*
	 * return 0 on success, -EAGAIN means to call the destroyer later.
	 */
	s32 (*destroy)(struct compositor *c);

	/*
	 * notify hotplug event to all clients
	 */
	void (*dispatch_hotplug_event)(struct compositor *c, s32 pipe);

	/*
	 * return 0 on success, -EAGAIN means to call the destroyer later.
	 */
	s32 (*suspend)(struct compositor *c);

	/* re-enable all */
	void (*resume)(struct compositor *c);

	/* add client */
	struct cb_client_agent *(*add_client)(struct compositor *c, s32 sock);

	/* set initial client debug level */
	void (*init_client_dbg)(struct compositor *c,
				struct cb_client_agent *client);

	/* remove client */
	void (*rm_client)(struct compositor *c, struct cb_client_agent *client);

	/* commit client's operations */
	void (*commit_surface)(struct compositor *c, struct cb_surface *s);

	/* add view to compositor's view list */
	void (*add_view_to_comp)(struct compositor *c, struct cb_view *v);

	/* remove view to compositor's view list */
	void (*rm_view_from_comp)(struct compositor *c, struct cb_view *v);

	/* commit client's DMA-BUF operations */
	s32 (*commit_dmabuf)(struct compositor *c, struct cb_surface *s);

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
	 * Get desktop canvas layout.
	 * 	layout: an array of desktop rectangles, pipe (hardware index)
	 *              and mode handles.
	 *              mode_handle can be used to retrieve mode info.
	 */
	void (*get_desktop_layout)(struct compositor *c,
				   struct cb_canvas_layout *layout);

	/*
	 * Set desktop canvas layout.
	 * 	layout: an array of desktop rectangles, pipe (hardware index)
	 *              and mode handles.
	 *              if the mode_handle is NULL, keep the video timing with
	 *              no change.
	 */
	void (*set_desktop_layout)(struct compositor *c,
				   struct cb_canvas_layout *layout);

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

	/* write keyboard led status */
	void (*set_kbd_led_status)(struct compositor *c, u32 led_status);
	
	/* get keyboard led status */
	s32 (*get_kbd_led_status)(struct compositor *c, u32 *led_status);

	/* debug set */
	void (*set_dbg_level)(struct compositor *c, enum cb_log_level level);
	void (*set_sc_dbg_level)(struct compositor *c, enum cb_log_level level);
	void (*set_rd_dbg_level)(struct compositor *c, enum cb_log_level level);
	void (*set_client_dbg_level)(struct compositor *c,
				     enum cb_log_level level);
	void (*set_touch_dbg_level)(struct compositor *c,
				    enum cb_log_level level);
	void (*set_joystick_dbg_level)(struct compositor *c,
				       enum cb_log_level level);

	/* import DMA-BUF for renderer */
	struct cb_buffer *(*import_rd_dmabuf)(struct compositor *c,
					      struct cb_buffer_info *info);
	/* import DMA-BUF for direct show */
	struct cb_buffer *(*import_so_dmabuf)(struct compositor *c,
					      struct cb_buffer_info *info);

	/* release renderer DMA-BUF */
	void (*release_rd_dmabuf)(struct compositor *c,
				  struct cb_buffer *buffer);

	/* release direct show DMA-BUF */
	void (*release_so_dmabuf)(struct compositor *c,
				  struct cb_buffer *buffer);
};

/* compositor creator */
struct compositor *compositor_create(char *device_name,
				     struct cb_event_loop *loop,
				     struct pipeline *pipecfgs,
				     s32 count_outputs,
				     s32 touch_pipe,
				     float mc_accel);

#endif

