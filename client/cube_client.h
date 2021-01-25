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
#ifndef CUBE_CLIENT_H
#define CUBE_CLIENT_H

#include <cube_utils.h>
#include <cube_protocal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* helper functions for cube display server */

#define MAX_NR_KBD_LED_EVTS 64

struct cb_client_mode_desc {
	struct mode_info info;
	void *handle;
	struct list_head link;
};

#define MAX_DISP_NR 8

struct cb_client_display {
	s32 pipe; /* read only */
	struct cb_rect desktop_rc, input_rc; /* read / write */
	struct mode_req mr; /* write only */
	void *pending_mode; /* write only */
	void *mode_current; /* read only */
	void *mode_custom; /* read only */
	bool enabled; /* read only */
	char monitor_name[CB_MONITOR_NAME_MAX_LEN]; /* read only */
	char connector_name[CB_CONNECTOR_NAME_MAX_LEN]; /* read only */

	/*
	 * read only, preferred size.
	 * It is the monitor's preferred size.
	 */
	u16 width_preferred, height_preferred;
	u32 vrefresh_preferred;
	u32 pixel_freq_preferred;

	bool hotplug_occur; /* read only, auto cleared after cb */
	struct list_head modes; /* read only */
};

struct cb_client {
	struct cb_client_display displays[MAX_DISP_NR];
	s32 count_displays;

	void (*destroy)(struct cb_client *client);
	void (*run)(struct cb_client *client);
	void (*stop)(struct cb_client *client);

	s32 (*set_server_dbg)(struct cb_client *client,
			      struct cb_debug_flags *dbg_flags);

	s32 (*set_client_cap)(struct cb_client *client, u64 cap);

	s32 (*set_input_msg_cb)(struct cb_client *client,
				void *userdata,
				void (*input_msg_cb)(
					void *userdata,
					struct cb_gui_input_msg *msg,
					u32 count_msg));

	s32 (*set_raw_input_evts_cb)(struct cb_client *client,
				     void *userdata,
				     void (*raw_input_evts_cb)(
				     		void *userdata,
				     		struct cb_raw_input_event *evts,
				     		u32 count_evts));

	s32 (*set_raw_touch_evts_cb)(struct cb_client *client,
				     void *userdata,
				     void (*raw_touch_evts_cb)(
				     		void *userdata,
				     		struct touch_event *evts,
				     		u32 sz));

	s32 (*set_ready_cb)(struct cb_client *client, void *userdata,
			    void (*ready_cb)(void *userdata));

	s32 (*set_destroyed_cb)(struct cb_client *client, void *userdata,
				void (*destroyed_cb)(void *userdata));
	void (*set_raw_input_en)(struct cb_client *client, bool en);
	s32 (*send_get_edid)(struct cb_client *client, u64 pipe);
	s32 (*set_get_edid_cb)(struct cb_client *client, void *userdata,
			       void (*get_edid_cb)(void *userdata,
			       			   u64 pipe,
			       			   u8 *edid,
			       			   size_t edid_len));
	s32 (*send_set_kbd_led_st)(struct cb_client *client,u32 led_status);
	s32 (*send_get_kbd_led_st)(struct cb_client *client);
	s32 (*set_kbd_led_st_cb)(struct cb_client *client, void *userdata,
				 void (*kbd_led_st_cb)(void *userdata,
				 		       u32 led_status));
	/*
	 * modify desktop_rc, input_rc and pending_mode, then call this function
	 */
	s32 (*change_layout)(struct cb_client *client);

	s32 (*query_layout)(struct cb_client *client);
	s32 (*set_layout_query_cb)(struct cb_client *client, void *userdata,
				   void (*layout_query_cb)(void *userdata));

	s32 (*set_layout_changed_cb)(struct cb_client *client, void *userdata,
				     void (*layout_changed_cb)(void *userdata));

	s32 (*enumerate_mode)(struct cb_client *client, s32 pipe,
			      void *last_mode, bool filter_en,
			      struct cb_mode_filter *filter);
	s32 (*set_enumerate_mode_cb)(struct cb_client *client, void *userdata,
				     void (*enumerate_mode_cb)(void *userdata,
				     		struct cb_client_mode_desc *));

	s32 (*create_mode)(struct cb_client *client, struct mode_info *info,
			   s32 index);
	s32 (*set_create_mode_cb)(struct cb_client *client, void *userdata,
				  void (*mode_created_cb)(
				  	bool success, void *userdata));

	s32 (*create_surface)(struct cb_client *client,
			      struct cb_surface_info *s);
	s32 (*set_create_surface_cb)(struct cb_client *client, void *userdata,
				     void (*surface_created_cb)(
				     	bool success, void *userdata, u64 id));

	s32 (*create_view)(struct cb_client *client, struct cb_view_info *v);
	s32 (*set_create_view_cb)(struct cb_client *client, void *userdata,
				  void (*view_created_cb)(
				  	bool success, void *userdata, u64 id));

	s32 (*create_bo)(struct cb_client *client, void *bo);
	s32 (*set_create_bo_cb)(struct cb_client *client,
				void *userdata,
				void (*bo_created_cb)(
				    bool success, void *userdata, u64 bo_id));

	s32 (*destroy_bo)(struct cb_client *client, u64 bo_id);

	u8 *(*alloc_af_commit_info_buffer)(struct cb_client *client);
	struct cb_af_commit_info *(*get_af_commit_info)(
					struct cb_client *client, u8 *buffer);
	s32 (*af_commit_bo)(struct cb_client *client, u8 *buffer);

	s32 (*commit_bo)(struct cb_client *client, struct cb_commit_info *c);
	s32 (*set_commit_bo_cb)(struct cb_client *client, void *userdata,
				void (*bo_commited_cb)(
				    bool success, void *userdata, u64 bo_id,
				    u64 surface_id));
	s32 (*set_bo_flipped_cb)(struct cb_client *client, void *userdata,
				 void (*bo_flipped_cb)(
				 	void *userdata, u64 bo_id,
				 	u64 surface_id));
	s32 (*set_bo_completed_cb)(struct cb_client *client, void *userdata,
				   void (*bo_completed_cb)(
				   	void *userdata, u64 bo_id,
				   	u64 surface_id));

	s32 (*commit_mc)(struct cb_client *client, struct cb_mc_info *c);
	s32 (*set_commit_mc_cb)(struct cb_client *client, void *userdata,
				void (*mc_commited_cb)(
				    bool success, void *userdata, u64 bo_id));

	s32 (*set_hpd_cb)(struct cb_client *client,
			  void *userdata,
			  void (*hpd_cb)(void *userdata,
					 struct cb_connector_info *info));

	s32 (*set_view_focus_chg_cb)(struct cb_client *client,
				     void *userdata,
				     void (*view_focus_chg_cb)(void *userdata,
				     			       u64 view_id,
				     			       bool on));

	void (*set_connection_lost_cb)(struct cb_client *client, void *userdata,
				       void (*connection_lost_cb)(void *));

	void (*add_idle_task)(struct cb_client *client, void *userdata,
			      void (*idle_task)(void *data));

	void *(*add_timer_handler)(struct cb_client *client, void *userdata,
				   s32 (*timer_cb)(void *data));

	void (*timer_update)(struct cb_client *client, void *handle,
			     u32 ms, u32 us);

	void *(*add_fd_handler)(struct cb_client *client, void *userdata,
				s32 fd, u32 mask,
				s32 (*fd_cb)(s32 fd, u32 mask, void *data));

	void *(*add_signal_handler)(struct cb_client *client, void *userdata,
				    s32 signal_number,
				    s32 (*signal_cb)(s32 signal_number,
				    		     void *data));

	void (*rm_handler)(struct cb_client *client, void *handle);
};

struct cb_client *cb_client_create(s32 seat);


/*
 * helper functions for operations of rockchip's GEM bo and SHM bo
 * 
 * MC (mouse cursor) bo must be SHM bo.
 * NV12/NV16/NV24/AR24/XR24/RG24 can be DMA-BUF bo.
 * The bo which will be used in compositor's renderer must be SHM bo or
 * NV12/NV16/AR24 DMA-BUF bo.
 * (RK MALI only support render NV12/NV16/AR24 bo as external texture)
 */
s32 cb_drm_device_open(const char *devnode);

void cb_drm_device_close(s32 drmfd);

void *cb_client_dma_buf_bo_create(s32 drmfd,
				  enum cb_pix_fmt pix_fmt,
				  u32 width,
				  u32 height,
				  u32 hstride,
				  u32 vstride,
				  bool map,
				  bool cachable,
				  s32 *count_fds, /* output */
				  s32 *count_planes, /* output */
				  s32 fds[4], /* output */
				  void *maps[4], /* output */
				  u32 pitches[4], /* output */
				  u32 offsets[4],
				  bool composed);

/*
 * If the bo is mapped to cpu, and software will change the bo's content,
 * the bo should be cachable and be synced before display
 * 
 * create bo with cachable flag ->
 * loop:
 * 	cb_client_dma_buf_bo_sync_begin ->
 * 	modify bo's content ->
 * 	cb_client_dma_buf_bo_sync_end ->
 * 	submit ->
 * 	wait last frame's flipped signal ->
 * 	goto loop
 */
void cb_client_dma_buf_bo_sync_begin(void *bo);
void cb_client_dma_buf_bo_sync_end(void *bo);

void cb_client_dma_buf_bo_destroy(void *bo);

void *cb_client_shm_bo_create(enum cb_pix_fmt pix_fmt,
			      u32 width,
			      u32 height,
			      u32 hstride,
			      u32 vstride,
			      s32 *count_fds, /* output */
			      s32 *count_planes, /* output */
			      s32 fds[4], /* output */
			      void *maps[4], /* output */
			      u32 pitches[4], /* output */
			      u32 offsets[4],
			      u32 sizes[4]);

void cb_client_shm_bo_destroy(void *bo);

void *cb_client_gbm_bo_create(s32 drmfd,
			      void *gbm,
			      enum cb_pix_fmt pix_fmt,
			      u32 width,
			      u32 height,
			      s32 *count_fds, /* output */
			      s32 *count_planes, /* output */
			      u32 strides[4], /* output */
			      s32 fds[4], /* output */
			      bool composed);
void cb_client_gbm_bo_destroy(void *bo);
void *cb_gbm_open(s32 drmfd);
void cb_gbm_close(void *gbm);

#ifdef __cplusplus
}
#endif

#endif

