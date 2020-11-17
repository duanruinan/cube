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
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <cube_utils.h>
#include <cube_ipc.h>
#include <cube_protocal.h>
#include <cube_log.h>
#include <cube_event.h>
#include <cube_shm.h>
#include <cube_cache.h>
#include <cube_client.h>

#ifndef CB_IPC_BUF_MAX_LEN
#define CB_IPC_BUF_MAX_LEN (1 << 19)
#endif

struct client {
	struct cb_client base;

	struct cb_event_loop *loop;

	struct cb_surface_info s;
	struct cb_view_info v;
	struct cb_commit_info c;
	struct cb_mc_info mc;
	struct cb_shell_info shell;

	/* ID of client */
	u64 link_id;

	/* for IPC */
	u8 ipc_buf[CB_IPC_BUF_MAX_LEN];
	size_t ipc_sz, byts_to_rd;
	u8 *cursor;
	struct cb_fds ipc_fds;
	s32 sock;
	struct cb_event_source *sock_source;
	struct cb_event_source *destroy_idle_source;

	/* main loop exit or not */
	bool exit;

	struct client_window *win;

	u8 *create_surface_tx_cmd_t;
	u8 *create_surface_tx_cmd;
	u32 create_surface_tx_len;

	u8 *create_view_tx_cmd_t;
	u8 *create_view_tx_cmd;
	u32 create_view_tx_len;

	u8 *create_bo_tx_cmd_t;
	u8 *create_bo_tx_cmd;
	u32 create_bo_tx_len;

	u8 *destroy_bo_tx_cmd_t;
	u8 *destroy_bo_tx_cmd;
	u32 destroy_bo_tx_len;

	u8 *commit_mc_tx_cmd_t;
	u8 *commit_mc_tx_cmd;
	u32 commit_mc_tx_len;

	u8 *commit_tx_cmd_t;
	u8 *commit_tx_cmd;
	u32 commit_tx_len;

	u8 *terminate_tx_cmd_t;
	u8 *terminate_tx_cmd;
	u32 terminate_tx_len;

	u8 *shell_tx_cmd_t;
	u8 *shell_tx_cmd;
	u32 shell_tx_len;

	u8 *get_edid_cmd_t;
	u8 *get_edid_cmd;
	u32 get_edid_len;

	u8 edid[512];
	size_t edid_sz;
	u64 edid_pipe;

	u8 *set_kbd_led_st_cmd_t;
	u8 *set_kbd_led_st_cmd;
	u32 set_kbd_led_st_len;

	u8 *get_kbd_led_st_cmd;
	u32 get_kbd_led_st_len;

	u8 *raw_input_en_cmd_t;
	u8 *raw_input_en_cmd;
	u32 raw_input_en_len;

	void *get_edid_cb_userdata;
	void (*get_edid_cb)(void *userdata, u64 pipe, u8 *edid,
			    size_t edid_len);

	void *raw_input_evts_cb_userdata;
	void (*raw_input_evts_cb)(void *userdata, struct cb_raw_input_event *,
				  u32 count_evts);

	void *ready_cb_userdata;
	void (*ready_cb)(void *userdata);

	void *destroyed_cb_userdata;
	void (*destroyed_cb)(void *userdata);

	void *input_cb_userdata;
	void (*input_cb)(struct cb_raw_input_event *evts,
			 u32 count_evts,
			 void *userdata);

	void *enumerate_mode_cb_userdata;
	void (*enumerate_mode_cb)(void *userdata, struct cb_client_mode_desc *);

	void *layout_query_cb_userdata;
	void (*layout_query_cb)(void *userdata);

	void *layout_changed_cb_userdata;
	void (*layout_changed_cb)(void *userdata);

	void *mode_created_cb_userdata;
	void (*mode_created_cb)(bool success, void *userdata);

	void *surface_created_cb_userdata;
	void (*surface_created_cb)(bool success, void *userdata, u64 id);

	void *view_created_cb_userdata;
	void (*view_created_cb)(bool success, void *userdata, u64 id);

	void *bo_created_cb_userdata;
	void (*bo_created_cb)(bool success, void *userdata, u64 bo_id);

	void *bo_commited_cb_userdata;
	void (*bo_commited_cb)(bool success, void *userdata, u64 bo_id);

	void *bo_flipped_cb_userdata;
	void (*bo_flipped_cb)(void *userdata, u64 bo_id);

	void *bo_completed_cb_userdata;
	void (*bo_completed_cb)(void *userdata, u64 bo_id);

	void *mc_commited_cb_userdata;
	void (*mc_commited_cb)(bool success, void *userdata, u64 bo_id);

	void *mc_flipped_cb_userdata;
	void (*mc_flipped_cb)(void *userdata, u64 bo_id);

	void *hpd_cb_userdata;
	void (*hpd_cb)(void *userdata, struct cb_connector_info *info);

	void *kbd_led_st_cb_userdata;
	void (*kbd_led_st_cb)(void *userdata, u32 led_status);
};

struct client_buffer {
	struct cb_buffer_info info;
	u32 fourcc;
	u64 bo_id;
	s32 count_fds;
	u32 handles[4];
	s32 drmfd;
	struct gbm_bo *client_bo;
};

static inline struct client *to_client(struct cb_client *client)
{
	return container_of(client, struct client, base);
}

static void destroy(struct cb_client *client)
{
	struct client *cli = to_client(client);
	struct cb_client_mode_desc *mode, *next;
	s32 i;
	struct cb_client_display *disp;

	if (!client)
		return;

	if (cli->loop) {
		for (i = 0; i < cli->base.count_displays; i++) {
			disp = &cli->base.displays[i];
			list_for_each_entry_safe(mode, next, &disp->modes,
						 link) {
				list_del(&mode->link);
				free(mode);
			}
		}
		if (cli->raw_input_en_cmd_t)
			free(cli->raw_input_en_cmd_t);

		if (cli->raw_input_en_cmd)
			free(cli->raw_input_en_cmd);

		if (cli->shell_tx_cmd_t)
			free(cli->shell_tx_cmd_t);

		if (cli->shell_tx_cmd)
			free(cli->shell_tx_cmd);

		if (cli->get_edid_cmd_t)
			free(cli->get_edid_cmd_t);

		if (cli->get_edid_cmd)
			free(cli->get_edid_cmd);

		if (cli->set_kbd_led_st_cmd_t)
			free(cli->set_kbd_led_st_cmd_t);

		if (cli->set_kbd_led_st_cmd)
			free(cli->set_kbd_led_st_cmd);

		if (cli->get_kbd_led_st_cmd)
			free(cli->get_kbd_led_st_cmd);

		if (cli->terminate_tx_cmd_t)
			free(cli->terminate_tx_cmd_t);

		if (cli->terminate_tx_cmd)
			free(cli->terminate_tx_cmd);

		if (cli->commit_tx_cmd_t)
			free(cli->commit_tx_cmd_t);

		if (cli->commit_tx_cmd)
			free(cli->commit_tx_cmd);

		if (cli->commit_mc_tx_cmd_t)
			free(cli->commit_mc_tx_cmd_t);

		if (cli->commit_mc_tx_cmd)
			free(cli->commit_mc_tx_cmd);

		if (cli->destroy_bo_tx_cmd_t)
			free(cli->destroy_bo_tx_cmd_t);

		if (cli->destroy_bo_tx_cmd)
			free(cli->destroy_bo_tx_cmd);

		if (cli->create_bo_tx_cmd_t)
			free(cli->create_bo_tx_cmd_t);

		if (cli->create_bo_tx_cmd)
			free(cli->create_bo_tx_cmd);

		if (cli->create_view_tx_cmd_t)
			free(cli->create_view_tx_cmd_t);

		if (cli->create_view_tx_cmd)
			free(cli->create_view_tx_cmd);

		if (cli->create_surface_tx_cmd_t);
			free(cli->create_surface_tx_cmd_t);

		if (cli->create_surface_tx_cmd);
			free(cli->create_surface_tx_cmd);

		if (cli->sock_source)
			cb_event_source_remove(cli->sock_source);

		if (cli->sock_source)
			cb_event_source_remove(cli->sock_source);

		if (cli->sock > 0)
			close(cli->sock);

		cb_event_loop_destroy(cli->loop);
	}

	free(cli);
}

static void run(struct cb_client *client)
{
	struct client *cli = to_client(client);

	while (!cli->exit) {
		cb_event_loop_dispatch(cli->loop, -1);
	}
}

static void stop(struct cb_client *client)
{
	struct client *cli = to_client(client);

	printf("client exit.\n");
	cli->exit = true;
}

static void set_raw_input_en(struct cb_client *client, bool en)
{
	struct client *cli = to_client(client);
	size_t length;
	u8 *p;
	s32 ret;

	p = cb_dup_raw_input_en_cmd(cli->raw_input_en_cmd,
				cli->raw_input_en_cmd_t,
				cli->raw_input_en_len, en);
	if (!p) {
		fprintf(stderr, "failed to dup raw input enable\n");
		return;
	}
	
	length = cli->raw_input_en_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send raw input en length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->raw_input_en_cmd, length,NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send raw input en. %s\n",
			strerror(errno));
		stop(&cli->base);
	}
}

static s32 send_get_kbd_led_st(struct cb_client *client)
{
	struct client *cli = to_client(client);
	size_t length;
	s32 ret;

	if (!client)
		return -EINVAL;

	length = cli->get_kbd_led_st_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send get kbd led st length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->get_kbd_led_st_cmd, length,
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send get kbd led st cmd. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_kbd_led_st_cb(struct cb_client *client, void *userdata,
			     void (*kbd_led_st_cb)(void *userdata,
			     			   u32 led_status))
{
	struct client *cli = to_client(client);

	if (!kbd_led_st_cb || !client)
		return -EINVAL;

	cli->kbd_led_st_cb_userdata = userdata;
	cli->kbd_led_st_cb = kbd_led_st_cb;

	return 0;
}

static s32 send_get_edid(struct cb_client *client, u64 pipe)
{
	struct client *cli = to_client(client);
	u8 *cmd;
	size_t length;
	s32 ret;

	if (!client)
		return -EINVAL;

	cmd = cb_dup_get_edid_cmd(cli->get_edid_cmd,
				  cli->get_edid_cmd_t,
				  cli->get_edid_len,
				  pipe);
	if (!cmd)
		return -EINVAL;

	length = cli->get_edid_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send get edid length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->get_edid_cmd, length,
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send get edid cmd. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 send_set_kbd_led_st(struct cb_client *client, u32 led_status)
{
	struct client *cli = to_client(client);
	u8 *cmd;
	size_t length;
	s32 ret;

	if (!client)
		return -EINVAL;

	cmd = cb_dup_set_kbd_led_st_cmd(cli->set_kbd_led_st_cmd,
					cli->set_kbd_led_st_cmd_t,
					cli->set_kbd_led_st_len,
					led_status);
	if (!cmd)
		return -EINVAL;

	length = cli->set_kbd_led_st_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send set kbd led st length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->set_kbd_led_st_cmd, length,
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send set kbd led st cmd. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_client_cap(struct cb_client *client, u64 cap)
{
	struct client *cli = to_client(client);
	size_t length;
	s32 ret;
	u8 *set_cap_tx_cmd;
	u32 n;

	if (!client || !cap)
		return -EINVAL;

	set_cap_tx_cmd = cb_client_create_set_cap_cmd(cap, &n);
	
	length = n;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send client cap length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, set_cap_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send client cap cmd (dbg). %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	free(set_cap_tx_cmd);

	return 0;
}

static s32 set_server_dbg(struct cb_client *client,
			  struct cb_debug_flags *dbg_flags)
{
	struct client *cli = to_client(client);
	size_t length;
	u8 *p;
	s32 ret;

	if (!client || !dbg_flags)
		return -EINVAL;

	cli->shell.cmd = CB_SHELL_DEBUG_SETTING;
	memcpy(&cli->shell.value.dbg_flags, dbg_flags, sizeof(*dbg_flags));
	p = cb_dup_shell_cmd(cli->shell_tx_cmd, cli->shell_tx_cmd_t,
			     cli->shell_tx_len, &cli->shell);
	if (!p) {
		fprintf(stderr, "failed to dup shell cmd (dbg)\n");
		return -EINVAL;
	}
	
	length = cli->shell_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send shell cmd (dbg) length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->shell_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send shell cmd (dbg). %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_raw_input_evts_cb(struct cb_client *client,
				 void *userdata,
				 void (*raw_input_evts_cb)(
				 		void *userdata,
				 		struct cb_raw_input_event *,
				 		u32 count_evts))
{
	struct client *cli = to_client(client);

	if (!client || !raw_input_evts_cb)
		return -EINVAL;

	cli->raw_input_evts_cb_userdata = userdata;
	cli->raw_input_evts_cb = raw_input_evts_cb;
	return 0;
}

static s32 set_ready_cb(struct cb_client *client, void *userdata,
			void (*ready_cb)(void *userdata))
{
	struct client *cli = to_client(client);

	if (!client || !ready_cb)
		return -EINVAL;

	cli->ready_cb_userdata = userdata;
	cli->ready_cb = ready_cb;
	return 0;
}

static s32 set_get_edid_cb(struct cb_client *client, void *userdata,
			   void (*get_edid_cb)(void *userdata,
			   		       u64 pipe,
			   		       u8 *edid,
			   		       size_t edid_len))
{
	struct client *cli = to_client(client);

	if (!client || !get_edid_cb)
		return -EINVAL;

	cli->get_edid_cb_userdata = userdata;
	cli->get_edid_cb = get_edid_cb;
	return 0;
}

static s32 set_destroyed_cb(struct cb_client *client, void *userdata,
			    void (*destroyed_cb)(void *userdata))
{
	struct client *cli = to_client(client);

	if (!client || !destroyed_cb)
		return -EINVAL;

	cli->destroyed_cb_userdata = userdata;
	cli->destroyed_cb = destroyed_cb;
	return 0;
}

static s32 change_layout(struct cb_client *client)
{
	struct client *cli = to_client(client);
	size_t length;
	u8 *p;
	s32 ret, i;
	struct cb_client_display *disp;

	if (!client)
		return -EINVAL;

	cli->shell.cmd = CB_SHELL_CANVAS_LAYOUT_SETTING;
	cli->shell.value.layout.count_heads = client->count_displays;
	for (i = 0; i < client->count_displays; i++) {
		disp = &client->displays[i];
		memcpy(&cli->shell.value.layout.cfg[i].desktop_rc,
		       &disp->desktop_rc, sizeof(struct cb_rect));
		memcpy(&cli->shell.value.layout.cfg[i].input_rc,
		       &disp->input_rc, sizeof(struct cb_rect));
		cli->shell.value.layout.cfg[i].mode_handle = disp->pending_mode;
	}

	p = cb_dup_shell_cmd(cli->shell_tx_cmd, cli->shell_tx_cmd_t,
			     cli->shell_tx_len, &cli->shell);
	if (!p) {
		fprintf(stderr, "failed to dup shell cmd\n");
		return -EINVAL;
	}
	
	length = cli->shell_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send shell cmd length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->shell_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send shell cmd (layout). %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 enumerate_mode(struct cb_client *client, s32 pipe, void *last_mode,
			  bool filter_en, struct cb_mode_filter *filter)
{
	struct client *cli = to_client(client);
	size_t length;
	s32 ret, i;
	u8 *p;
	struct cb_client_display *disp;
	struct cb_client_mode_desc *mode, *next;

	if (!client || pipe < 0)
		return -EINVAL;

	cli->shell.cmd = CB_SHELL_OUTPUT_VIDEO_TIMING_ENUMERATE;
	cli->shell.value.ote.pipe = pipe;
	cli->shell.value.ote.handle_last = last_mode;
	if (!last_mode) {
		/* clear modes */
		for (i = 0; i < cli->base.count_displays; i++)
			if (cli->base.displays[i].pipe == pipe)
				break;
		disp = &cli->base.displays[i];
		list_for_each_entry_safe(mode, next, &disp->modes, link) {
			list_del(&mode->link);
			free(mode);
		}
	}
	cli->shell.value.ote.handle_cur = NULL;
	cli->shell.value.ote.filter_en = filter_en;
	if (filter_en)
		memcpy(&cli->shell.value.ote.enum_filter, filter,
			sizeof(*filter));
	p = cb_dup_shell_cmd(cli->shell_tx_cmd, cli->shell_tx_cmd_t,
			     cli->shell_tx_len, &cli->shell);
	if (!p) {
		fprintf(stderr, "failed to dup shell cmd (enumerate mode)\n");
		return -EINVAL;
	}
	
	length = cli->shell_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send shell cmd (enum) length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->shell_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send shell cmd (enum). %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_enumerate_mode_cb(struct cb_client *client, void *userdata,
				 void (*enumerate_mode_cb)(void *userdata,
				 	struct cb_client_mode_desc *))
{
	struct client *cli = to_client(client);

	if (!client || !enumerate_mode_cb)
		return -EINVAL;

	cli->enumerate_mode_cb_userdata = userdata;
	cli->enumerate_mode_cb = enumerate_mode_cb;

	return 0;
}

static s32 query_layout(struct cb_client *client)
{
	struct client *cli = to_client(client);
	size_t length;
	s32 ret;
	u8 *p;

	if (!client)
		return -EINVAL;

	cli->shell.cmd = CB_SHELL_CANVAS_LAYOUT_QUERY;
	p = cb_dup_shell_cmd(cli->shell_tx_cmd, cli->shell_tx_cmd_t,
			     cli->shell_tx_len, &cli->shell);
	if (!p) {
		fprintf(stderr, "failed to dup shell cmd (query layout)\n");
		return -EINVAL;
	}

	printf("Q *** %d %d %d\n",
		cli->shell.value.layout.count_heads,
		cli->shell.value.layout.cfg[0].pipe,
		cli->shell.value.layout.cfg[1].pipe);
	
	length = cli->shell_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send shell cmd length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->shell_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send shell cmd (query layout). %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_layout_query_cb(struct cb_client *client, void *userdata,
			       void (*layout_query_cb)(void *userdata))
{
	struct client *cli = to_client(client);

	if (!client || !layout_query_cb)
		return -EINVAL;

	cli->layout_query_cb_userdata = userdata;
	cli->layout_query_cb = layout_query_cb;

	return 0;
}

static s32 set_layout_changed_cb(struct cb_client *client, void *userdata,
				 void (*layout_changed_cb)(void *userdata))
{
	struct client *cli = to_client(client);

	if (!client || !layout_changed_cb)
		return -EINVAL;

	cli->layout_changed_cb_userdata = userdata;
	cli->layout_changed_cb = layout_changed_cb;

	return 0;
}

static s32 create_mode(struct cb_client *client, struct mode_info *info,
		       s32 index)
{
	struct client *cli = to_client(client);
	size_t length;
	u8 *p;
	s32 ret;

	if (!client || !info)
		return -EINVAL;

	cli->shell.cmd = CB_SHELL_OUTPUT_VIDEO_TIMING_CREAT;
	memcpy(&cli->shell.value.mode, info, sizeof(*info));
	cli->shell.value.modeset_pipe = client->displays[index].pipe;
	p = cb_dup_shell_cmd(cli->shell_tx_cmd, cli->shell_tx_cmd_t,
			     cli->shell_tx_len, &cli->shell);
	if (!p) {
		fprintf(stderr, "failed to dup shell cmd\n");
		return -EINVAL;
	}
	
	length = cli->shell_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send shell cmd length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->shell_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send shell cmd (create mode). %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_create_mode_cb(struct cb_client *client, void *userdata,
			      void (*mode_created_cb)(
			      		bool success, void *userdata))
{
	struct client *cli = to_client(client);

	if (!client || !mode_created_cb)
		return -EINVAL;

	cli->mode_created_cb_userdata = userdata;
	cli->mode_created_cb = mode_created_cb;

	return 0;
}

static s32 create_surface(struct cb_client *client, struct cb_surface_info *s)
{
	struct client *cli = to_client(client);
	size_t length;
	u8 *p;
	s32 ret;

	if (!client || !s)
		return -EINVAL;

	memcpy(&cli->s, s, sizeof(*s));
	p = cb_dup_create_surface_cmd(cli->create_surface_tx_cmd,
				      cli->create_surface_tx_cmd_t,
				      cli->create_surface_tx_len, &cli->s);
	if (!p) {
		fprintf(stderr, "failed to dup create surface cmd\n");
		return -EINVAL;
	}

	length = cli->create_surface_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send create surf length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->create_surface_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send create surf cmd. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_create_surface_cb(struct cb_client *client, void *userdata,
				 void (*surface_created_cb)(
				 	bool success, void *userdata, u64 id))
{
	struct client *cli = to_client(client);

	if (!client || !surface_created_cb)
		return -EINVAL;

	cli->surface_created_cb_userdata = userdata;
	cli->surface_created_cb = surface_created_cb;
	return 0;
}

static s32 create_view(struct cb_client *client, struct cb_view_info *v)
{
	struct client *cli = to_client(client);
	size_t length;
	u8 *p;
	s32 ret;

	if (!client || !v)
		return -EINVAL;

	memcpy(&cli->v, v, sizeof(*v));
	p = cb_dup_create_view_cmd(cli->create_view_tx_cmd,
				   cli->create_view_tx_cmd_t,
				   cli->create_view_tx_len, &cli->v);
	if (!p) {
		fprintf(stderr, "failed to dup create view cmd\n");
		return -EINVAL;
	}

	length = cli->create_view_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send create view length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->create_view_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send create view cmd. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_create_view_cb(struct cb_client *client, void *userdata,
			      void (*view_created_cb)(
			      		bool success, void *userdata, u64 id))
{
	struct client *cli = to_client(client);

	if (!client || !view_created_cb)
		return -EINVAL;

	cli->view_created_cb_userdata = userdata;
	cli->view_created_cb = view_created_cb;
	return 0;
}

static s32 create_bo(struct cb_client *client, void *bo)
{
	struct client *cli = to_client(client);
	struct client_buffer *buffer = bo;
	size_t length;
	u8 *p;
	s32 ret, i;
	struct cb_fds fds;
	bool send_fd = false;

	if (!client || !bo)
		return -EINVAL;

	if (buffer->info.type == CB_BUF_TYPE_DMA)
		send_fd = true;
	p = cb_dup_create_bo_cmd(cli->create_bo_tx_cmd,
				 cli->create_bo_tx_cmd_t,
				 cli->create_bo_tx_len,
				 &buffer->info);
	if (!p) {
		fprintf(stderr, "failed to dup create bo cmd\n");
		return -EINVAL;
	}

	length = cli->create_bo_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send create bo length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		if (send_fd) {
			fds.count = buffer->count_fds;
			printf("count fds: %d\n", fds.count);
			for (i = 0; i < fds.count; i++) {
				fds.fds[i] = buffer->info.fd[i];
				printf("fd: %d\n", fds.fds[i]);
			}
			ret = cb_sendmsg(cli->sock, cli->create_bo_tx_cmd,
					 length, &fds);
		} else {
			ret = cb_sendmsg(cli->sock, cli->create_bo_tx_cmd,
					 length, NULL);
		}
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send create bo cmd. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_create_bo_cb(struct cb_client *client,
			    void *userdata,
			    void (*bo_created_cb)(
			    	bool success, void *userdata, u64 bo_id))
{
	struct client *cli = to_client(client);

	if (!client || !bo_created_cb)
		return -EINVAL;

	cli->bo_created_cb_userdata = userdata;
	cli->bo_created_cb = bo_created_cb;
	return 0;
}

static s32 destroy_bo(struct cb_client *client, u64 bo_id)
{
	struct client *cli = to_client(client);
	size_t length;
	u8 *p;
	s32 ret;

	if (!client || !bo_id)
		return -EINVAL;

	p = cb_dup_destroy_bo_cmd(cli->destroy_bo_tx_cmd,
				  cli->destroy_bo_tx_cmd_t,
				  cli->destroy_bo_tx_len,
				  bo_id);
	if (!p) {
		fprintf(stderr, "failed to dup destroy bo cmd\n");
		return -EINVAL;
	}

	length = cli->destroy_bo_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send destroy bo length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->destroy_bo_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send destroy bo cmd. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 commit_bo(struct cb_client *client, struct cb_commit_info *c)
{
	struct client *cli = to_client(client);
	size_t length;
	u8 *p;
	s32 ret;

	if (!client || !c)
		return -EINVAL;

	memcpy(&cli->c, c, sizeof(*c));
	p = cb_dup_commit_req_cmd(cli->commit_tx_cmd,
				  cli->commit_tx_cmd_t,
				  cli->commit_tx_len,
				  &cli->c);
	if (!p) {
		fprintf(stderr, "failed to dup commit bo cmd\n");
		return -EINVAL;
	}

	length = cli->commit_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send commit bo length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->commit_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send commit bo cmd. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_commit_bo_cb(struct cb_client *client, void *userdata,
			    void (*bo_commited_cb)(
			    	bool success, void *userdata, u64 bo_id))
{
	struct client *cli = to_client(client);

	if (!client || !bo_commited_cb)
		return -EINVAL;

	cli->bo_commited_cb_userdata = userdata;
	cli->bo_commited_cb = bo_commited_cb;
	return 0;
}

static s32 set_bo_flipped_cb(struct cb_client *client, void *userdata,
			     void (*bo_flipped_cb)(
			     		void *userdata, u64 bo_id))
{
	struct client *cli = to_client(client);

	if (!client || !bo_flipped_cb)
		return -EINVAL;

	cli->bo_flipped_cb_userdata = userdata;
	cli->bo_flipped_cb = bo_flipped_cb;
	return 0;
}

static s32 set_bo_completed_cb(struct cb_client *client, void *userdata,
			       void (*bo_completed_cb)(
			       		void *userdata, u64 bo_id))
{
	struct client *cli = to_client(client);

	if (!client || !bo_completed_cb)
		return -EINVAL;

	cli->bo_completed_cb_userdata = userdata;
	cli->bo_completed_cb = bo_completed_cb;
	return 0;
}

static s32 commit_mc(struct cb_client *client, struct cb_mc_info *mc)
{
	struct client *cli = to_client(client);
	size_t length;
	u8 *p;
	s32 ret;

	if (!client || !mc)
		return -EINVAL;

	memcpy(&cli->mc, mc, sizeof(*mc));
	p = cb_dup_mc_commit_cmd(cli->commit_mc_tx_cmd,
				 cli->commit_mc_tx_cmd_t,
				 cli->commit_mc_tx_len,
				 &cli->mc);
	if (!p) {
		fprintf(stderr, "failed to dup commit mc cmd\n");
		return -EINVAL;
	}

	length = cli->commit_mc_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send commit mc length. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->commit_mc_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		fprintf(stderr, "failed to send commit mc cmd. %s\n",
			strerror(errno));
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_commit_mc_cb(struct cb_client *client, void *userdata,
			    void (*mc_commited_cb)(
			    	bool success, void *userdata, u64 bo_id))
{
	struct client *cli = to_client(client);

	if (!client || !mc_commited_cb)
		return -EINVAL;

	cli->mc_commited_cb_userdata = userdata;
	cli->mc_commited_cb = mc_commited_cb;
	return 0;
}

static s32 set_mc_flipped_cb(struct cb_client *client, void *userdata,
			     void (*mc_flipped_cb)(
			     		void *userdata, u64 bo_id))
{
	struct client *cli = to_client(client);

	if (!client || !mc_flipped_cb)
		return -EINVAL;

	cli->mc_flipped_cb_userdata = userdata;
	cli->mc_flipped_cb = mc_flipped_cb;
	return 0;
}

s32 set_hpd_cb(struct cb_client *client, void *userdata,
	       void (*hpd_cb)(void *userdata, struct cb_connector_info *info))
{
	struct client *cli = to_client(client);

	if (!client || !hpd_cb)
		return -EINVAL;

	cli->hpd_cb_userdata = userdata;
	cli->hpd_cb = hpd_cb;
	return 0;
}

static s32 shell_proc(struct client *cli, u8 *buf)
{
	s32 i, j;
	struct output_config *cfg;
	struct cb_client_display *disp;
	u64 ret;
	struct cb_client_mode_desc *mode;

	ret = cb_parse_shell_cmd(buf, &cli->shell);
	if (ret) {
		fprintf(stderr, "shell failed. %ld\n", ret);
		return -EINVAL;
	}

	switch (cli->shell.cmd) {
	case CB_SHELL_CANVAS_LAYOUT_CHANGED_NOTIFY:
		if (cli->base.count_displays == 0) {
			cli->base.count_displays = 
				cli->shell.value.layout.count_heads;
			for (i = 0; i < cli->base.count_displays; i++) {
				cfg = &cli->shell.value.layout.cfg[i];
				disp = &cli->base.displays[i];
				disp->pipe = cfg->pipe;
				memcpy(&disp->desktop_rc, &cfg->desktop_rc,
				       sizeof(struct cb_rect));
				memcpy(&disp->input_rc, &cfg->input_rc,
				       sizeof(struct cb_rect));
				disp->mode_current = cfg->mode_handle;
				disp->mode_custom = cfg->custom_mode_handle;
				disp->width_preferred =
					cfg->width_preferred;
				disp->height_preferred =
					cfg->height_preferred;
				disp->vrefresh_preferred =
					cfg->vrefresh_preferred;
				disp->pixel_freq_preferred =
					cfg->pixel_freq_preferred;
				strncpy(disp->monitor_name,
					cfg->monitor_name,
					CB_MONITOR_NAME_MAX_LEN - 1);
				strncpy(disp->connector_name,
					cfg->connector_name,
					CB_CONNECTOR_NAME_MAX_LEN - 1);
				if (!disp->mode_current)
					disp->enabled = false;
				else
					disp->enabled = true;
			}
			if (cli->layout_changed_cb) {
				printf("layout changed notify\n");
				cli->layout_changed_cb(
					cli->layout_changed_cb_userdata);
			}
			return 0;
		}
		for (j = 0; j < cli->shell.value.layout.count_heads; j++) {
			cfg = &cli->shell.value.layout.cfg[j];
			for (i = 0; i < cli->base.count_displays; i++) {
				disp = &cli->base.displays[i];
				if (cfg->pipe == disp->pipe) {
					memcpy(&disp->desktop_rc,
					       &cfg->desktop_rc,
					       sizeof(struct cb_rect));
					memcpy(&disp->input_rc,
					       &cfg->input_rc,
					       sizeof(struct cb_rect));
					disp->mode_current = cfg->mode_handle;
					disp->mode_custom
						= cfg->custom_mode_handle;
					disp->width_preferred =
						cfg->width_preferred;
					disp->height_preferred =
						cfg->height_preferred;
					disp->vrefresh_preferred =
						cfg->vrefresh_preferred;
					disp->pixel_freq_preferred =
						cfg->pixel_freq_preferred;
					strncpy(disp->monitor_name,
						cfg->monitor_name,
						CB_MONITOR_NAME_MAX_LEN - 1);
					strncpy(disp->connector_name,
						cfg->connector_name,
						CB_CONNECTOR_NAME_MAX_LEN - 1);
					if (!disp->mode_current)
						disp->enabled = false;
					else
						disp->enabled = true;
					break;
				}
			}
		}
		if (cli->layout_changed_cb) {
			printf("layout changed notify\n");
			cli->layout_changed_cb(
				cli->layout_changed_cb_userdata);
		}
		
		break;
	case CB_SHELL_CANVAS_LAYOUT_QUERY:
		if (cli->base.count_displays == 0) {
			cli->base.count_displays = 
				cli->shell.value.layout.count_heads;
			for (i = 0; i < cli->base.count_displays; i++) {
				cfg = &cli->shell.value.layout.cfg[i];
				disp = &cli->base.displays[i];
				disp->pipe = cfg->pipe;
				memcpy(&disp->desktop_rc, &cfg->desktop_rc,
				       sizeof(struct cb_rect));
				memcpy(&disp->input_rc, &cfg->input_rc,
				       sizeof(struct cb_rect));
				disp->mode_current = cfg->mode_handle;
				disp->mode_custom = cfg->custom_mode_handle;
				disp->width_preferred =
					cfg->width_preferred;
				disp->height_preferred =
					cfg->height_preferred;
				disp->vrefresh_preferred =
					cfg->vrefresh_preferred;
				disp->pixel_freq_preferred =
					cfg->pixel_freq_preferred;
				strncpy(disp->monitor_name,
					cfg->monitor_name,
					CB_MONITOR_NAME_MAX_LEN - 1);
				strncpy(disp->connector_name,
					cfg->connector_name,
					CB_CONNECTOR_NAME_MAX_LEN - 1);
				if (!disp->mode_current)
					disp->enabled = false;
				else
					disp->enabled = true;
			}
			if (cli->layout_query_cb) {
				printf("layout query notify\n");
				cli->layout_query_cb(
					cli->layout_query_cb_userdata);
			}
			return 0;
		}
		for (j = 0; j < cli->shell.value.layout.count_heads; j++) {
			cfg = &cli->shell.value.layout.cfg[j];
			for (i = 0; i < cli->base.count_displays; i++) {
				disp = &cli->base.displays[i];
				if (cfg->pipe == disp->pipe) {
					memcpy(&disp->desktop_rc,
					       &cfg->desktop_rc,
					       sizeof(struct cb_rect));
					memcpy(&disp->input_rc,
					       &cfg->input_rc,
					       sizeof(struct cb_rect));
					disp->mode_current = cfg->mode_handle;
					disp->mode_custom
						= cfg->custom_mode_handle;
					disp->width_preferred =
						cfg->width_preferred;
					disp->height_preferred =
						cfg->height_preferred;
					disp->vrefresh_preferred =
						cfg->vrefresh_preferred;
					disp->pixel_freq_preferred =
						cfg->pixel_freq_preferred;
					strncpy(disp->monitor_name,
						cfg->monitor_name,
						CB_MONITOR_NAME_MAX_LEN - 1);
					strncpy(disp->connector_name,
						cfg->connector_name,
						CB_CONNECTOR_NAME_MAX_LEN - 1);
					if (!disp->mode_current)
						disp->enabled = false;
					else
						disp->enabled = true;
					break;
				}
			}
		}
		if (cli->layout_query_cb) {
			printf("layout query notify\n");
			cli->layout_query_cb(cli->layout_query_cb_userdata);
		}
		
		break;
	case CB_SHELL_OUTPUT_VIDEO_TIMING_ENUMERATE:
		for (i = 0; i < cli->base.count_displays; i++) {
			if (cli->base.displays[i].pipe ==
			    cli->shell.value.ote.pipe)
				break;
		}
		if (i == cli->base.count_displays) {
			fprintf(stderr, "illegal pipe in shell (enum). %d %d\n",
				cli->base.count_displays,
				cli->shell.value.ote.pipe);
			cb_cmd_dump(buf);
			return -EINVAL;
		}
		if (cli->shell.value.ote.handle_cur) {
			mode = calloc(1, sizeof(*mode));
			mode->handle = cli->shell.value.ote.handle_cur;
			memcpy(&mode->info, &cli->shell.value.mode,
				sizeof(struct mode_info));
			list_add_tail(&mode->link,&cli->base.displays[i].modes);
			if (cli->enumerate_mode_cb) {
				cli->enumerate_mode_cb(
					cli->enumerate_mode_cb_userdata,
					mode);
			}
		} else {
			if (cli->enumerate_mode_cb) {
				cli->enumerate_mode_cb(
					cli->enumerate_mode_cb_userdata, NULL);
			}
		}
		break;
	case CB_SHELL_OUTPUT_VIDEO_TIMING_CREAT:
		for (i = 0; i < cli->base.count_displays; i++) {
			if (cli->base.displays[i].pipe ==
			    cli->shell.value.modeset_pipe)
				break;
		}
		if (i == cli->base.count_displays) {
			fprintf(stderr, "illegal pipe in shell (create).\n");
			return -EINVAL;
		}
		if (!cli->shell.value.new_mode_handle) {
			fprintf(stderr, "failed to create cust mode\n");
			if (cli->mode_created_cb) {
				cli->mode_created_cb(false,
					cli->mode_created_cb_userdata);
			}
		} else {
			printf("create cust mode ok.\n");
			cli->base.displays[i].mode_custom =
				cli->shell.value.new_mode_handle;
			if (cli->mode_created_cb) {
				cli->mode_created_cb(true,
					cli->mode_created_cb_userdata);
			}
		}
		break;
	default:
		fprintf(stderr, "unknown shell cmd %d\n",
			cli->shell.cmd);
		return -EINVAL;
	}

	return 0;
}

static void client_ipc_proc(struct client *cli)
{
	u8 *buf;
	size_t ipc_sz;
	u32 flag, ret;
	struct cb_tlv *tlv;
	u64 id;
	struct cb_raw_input_event *evts;
	u32 count_evts, led_status;

	if (!cli)
		return;

	ipc_sz = cli->ipc_sz;
	buf = &cli->ipc_buf[0] + sizeof(size_t);
	flag = *((u32 *)buf);
	tlv = (struct cb_tlv *)(buf + sizeof(u32));
	assert(ipc_sz == (tlv->length + sizeof(*tlv) + sizeof(flag)));
	assert(tlv->tag == CB_TAG_WIN || tlv->tag == CB_TAG_RAW_INPUT ||
		tlv->tag == CB_TAG_GET_KBD_LED_STATUS_ACK ||
		tlv->tag == CB_TAG_GET_EDID_ACK);

	if (tlv->tag == CB_TAG_RAW_INPUT) {
		evts = cb_client_parse_raw_input_evt_cmd(buf, &count_evts);
		if (!evts) {
			fprintf(stderr, "failed to parse raw input evts.\n");
			return;
		}
		if (cli->raw_input_evts_cb) {
			cli->raw_input_evts_cb(cli->raw_input_evts_cb_userdata,
					       evts, count_evts);
		}
		return;
	}

	if (tlv->tag == CB_TAG_GET_EDID_ACK) {
		ret = cb_client_parse_get_edid_ack_cmd(buf,
						       &cli->edid_pipe,
						       &cli->edid[0],
						       &cli->edid_sz);
		if (ret < 0) {
			if (ret == -ENOENT) {
				fprintf(stderr,
					"edid of pipe %lu not available\n",
					cli->edid_pipe);
				cli->edid_sz = 0;
			} else {
				return;
			}
		}

		if (cli->get_edid_cb) {
			cli->get_edid_cb(cli->get_edid_cb_userdata,
					 cli->edid_pipe,
					 cli->edid, cli->edid_sz);
		}
		return;
	}

	if (tlv->tag == CB_TAG_GET_KBD_LED_STATUS_ACK) {
		if (cb_client_parse_get_kbd_led_st_ack_cmd(buf,
							   &led_status) < 0) {
			fprintf(stderr, "failed to parse kbd led st ack.\n");
			return;
		}
		if (cli->kbd_led_st_cb) {
			cli->kbd_led_st_cb(cli->kbd_led_st_cb_userdata,
					   led_status);
		}
		return;
	}

	if (flag & (1 << CB_CMD_LINK_ID_ACK_SHIFT)) {
		id = cb_client_parse_link_id(buf);
		cli->link_id = id;
		printf("link_id: 0x%08lX\n", cli->link_id);
		if (cli->ready_cb) {
			cli->ready_cb(cli->ready_cb_userdata);
		}
	}
	if (flag & (1 << CB_CMD_CREATE_SURFACE_ACK_SHIFT)) {
		printf("receive create surface ack\n");
		id = cb_client_parse_surface_id(buf);
		cli->s.surface_id = id;
		if (cli->surface_created_cb) {
			if (!id) {
				fprintf(stderr, "failed to create surface.\n");
				cli->surface_created_cb(false,
					cli->surface_created_cb_userdata, 0UL);
				return;
			}
			cli->surface_created_cb(true,
				cli->surface_created_cb_userdata, id);
		}
	}
	if (flag & (1 << CB_CMD_CREATE_VIEW_ACK_SHIFT)) {
		printf("receive create view ack\n");
		id = cb_client_parse_view_id(buf);
		cli->v.view_id = id;
		if (cli->view_created_cb) {
			if (!id) {
				fprintf(stderr, "failed to create view.\n");
				cli->view_created_cb(false,
					cli->view_created_cb_userdata, 0UL);
				return;
			}
			cli->view_created_cb(true,
				cli->view_created_cb_userdata, id);
		}
	}
	if (flag & (1 << CB_CMD_CREATE_BO_ACK_SHIFT)) {
		printf("receive create bo ack\n");
		id = cb_client_parse_bo_id(buf);
		if (cli->bo_created_cb) {
			if (!id) {
				fprintf(stderr, "failed to create bo.\n");
				cli->bo_created_cb(false,
					cli->bo_created_cb_userdata, 0UL);
				return;
			}
			cli->bo_created_cb(true,
				cli->bo_created_cb_userdata, id);
		}
	}
	if (flag & (1 << CB_CMD_COMMIT_ACK_SHIFT)) {
		id = cb_client_parse_commit_ack_cmd(buf);
		if (cli->bo_commited_cb) {
			if (id == (u64)(-1)) {
				fprintf(stderr, "failed to commit bo.\n");
				cli->bo_commited_cb(false,
					cli->bo_commited_cb_userdata, (u64)-1);
				return;
			}
			cli->bo_commited_cb(true,
				cli->bo_commited_cb_userdata, id);
		}
	}
	if (flag & (1 << CB_CMD_BO_FLIPPED_SHIFT)) {
		id = cb_client_parse_bo_flipped_cmd(buf);
		if (cli->bo_flipped_cb) {
			if (id == (u64)(-1)) {
				fprintf(stderr, "Unknown bo flipped.\n");
				return;
			}
			cli->bo_flipped_cb(cli->bo_flipped_cb_userdata, id);
		}
	}
	if (flag & (1 << CB_CMD_BO_COMPLETE_SHIFT)) {
		id = cb_client_parse_bo_complete_cmd(buf);
		if (cli->bo_completed_cb) {
			if (id == (u64)(-1)) {
				fprintf(stderr, "Unknown bo completed.\n");
				return;
			}
			cli->bo_completed_cb(cli->bo_completed_cb_userdata, id);
		}
	}
	if (flag & (1 << CB_CMD_DESTROY_ACK_SHIFT)) {
		printf("receive destroy ack\n");
		cb_server_parse_destroy_bo_cmd(buf);
		if (cli->destroyed_cb)
			cli->destroyed_cb(cli->destroyed_cb_userdata);
		return;
	}
	if (flag & (1 << CB_CMD_SHELL_SHIFT)) {
		if (shell_proc(cli, buf) < 0)
			return;
	}
	if (flag & (1 << CB_CMD_HPD_SHIFT)) {
		s32 i;
		struct cb_connector_info conn_info;

		printf("receive hpd cmd\n");
		id = cb_client_parse_hpd_cmd(buf, &conn_info);
		if (id) {
			fprintf(stderr, "unknown hotplug message.\n");
			return;
		}
		if (cli->base.count_displays == 0) {
			fprintf(stderr, "receive hotplug message too early.\n");
			return;
		}
		for (i = 0; i < cli->base.count_displays; i++) {
			if (cli->base.displays[i].pipe != conn_info.pipe)
				continue;
			cli->base.displays[i].enabled = conn_info.enabled;
			strncpy(cli->base.displays[i].connector_name,
				conn_info.connector_name,
				CB_CONNECTOR_NAME_MAX_LEN);
			strncpy(cli->base.displays[i].monitor_name,
				conn_info.monitor_name,
				CB_MONITOR_NAME_MAX_LEN);
			cli->base.displays[i].hotplug_occur = true;
			if (cli->hpd_cb) {
				cli->hpd_cb(cli->hpd_cb_userdata, &conn_info);
			}
			cli->base.displays[i].hotplug_occur = false;
		}
	}
	if (flag & (1 << CB_CMD_MC_COMMIT_ACK_SHIFT)) {
		id = cb_client_parse_mc_commit_ack_cmd(buf);
		if (cli->mc_commited_cb) {
			if (id == (u64)(-1)) {
				cli->mc_commited_cb(false,
					cli->mc_commited_cb_userdata, 0UL);
				return;
			}
			cli->mc_commited_cb(true,
				cli->mc_commited_cb_userdata, id);
		}
	}
	if (flag & (1 << CB_CMD_MC_FLIPPED_SHIFT)) {
		id = cb_client_parse_mc_flipped_cmd(buf);
		if (cli->mc_flipped_cb) {
			cli->mc_flipped_cb(cli->mc_flipped_cb_userdata, id);
		}
	}
}

static s32 sock_cb(s32 fd, u32 mask, void *data)
{
	struct client *cli = data;
	size_t byts_rd;
	s32 ret, i;
	s32 flag; /* 0: length not received, 1: length received. */
	struct cb_fds ipc_fds;
	s32 *p;

	cli->ipc_fds.count = 0;
	if (cli->cursor >= ((u8 *)(cli->ipc_buf) + sizeof(size_t))) {
		flag = 1;
	} else {
		flag = 0;
	}

	do {
		/* bytes_to_rd is set when client create: sizeof(size_t) */
		/* printf("try to receive %lu\n", client->byts_to_rd); */
		ret = cb_recvmsg(cli->sock, cli->cursor,
				 cli->byts_to_rd, &ipc_fds);
		/* printf("receive return %d\n", ret); */
	} while (ret == -EAGAIN);

	if (ret < 0) {
		fprintf(stderr, "failed to recv from server (%s).\n",
			strerror(-ret));
		stop(&cli->base);
		return ret;
	} else if (ret == 0) {
		fprintf(stderr, "connection lost.\n");
		stop(&cli->base);
		return 0;
	}

	if (ipc_fds.count > 0) {
		p = &cli->ipc_fds.fds[cli->ipc_fds.count];
		for (i = 0; i < ipc_fds.count; i++) {
			p[i] = ipc_fds.fds[i];
		}
		cli->ipc_fds.count += ipc_fds.count;
	}

	cli->cursor += ret;
	cli->byts_to_rd -= ret;
	byts_rd = ret;

	if (!flag) {
		if (ret >= sizeof(size_t)) {
			/* received the length */
			flag = 1;
			memcpy(&cli->byts_to_rd, cli->ipc_buf, sizeof(size_t));
			cli->ipc_sz = cli->byts_to_rd;
			/* printf("ipc size: %lu\n", client->ipc_sz); */
			/* printf("%p %p\n", client->cursor, client->ipc_buf);*/
			if ((byts_rd - sizeof(size_t)) > cli->ipc_sz) {
				/* received more than one ipc message */
				cli->byts_to_rd = 0;
			} else {
				cli->byts_to_rd -= (byts_rd -sizeof(size_t));
			}
		}
	}

	if (!cli->byts_to_rd) {
		cli->cursor = (u8 *)cli->ipc_buf;
		cli->byts_to_rd = sizeof(size_t);
		/* printf("complete.\n"); */
		/* TODO &client->ipc_buf[0] + sizeof(size_t), ipc_sz */
		/*
		printf("proc ipc message: %p, %lu\n",
			&cli->ipc_buf[0] + sizeof(size_t), cli->ipc_sz);
		printf("ipc received %d fds.\n", cli->ipc_fds.count);
		*/
		for (i = 0; i < cli->ipc_fds.count; i++) {
			printf("fds[%d]: %d\n", i, cli->ipc_fds.fds[i]);
		}
		client_ipc_proc(cli);
		cli->ipc_fds.count = 0;
	}
	return 0;
}

static void add_idle_task(struct cb_client *client, void *userdata,
			  void (*idle_task)(void *data))
{
	struct client *cli = to_client(client);

	if (!client || !idle_task)
		return;

	cb_event_loop_add_idle(cli->loop, idle_task, userdata);
}

static void *add_timer_handler(struct cb_client *client, void *userdata,
			       s32 (*timer_cb)(void *data))
{
	struct client *cli = to_client(client);

	if (!client || !timer_cb)
		return NULL;

	return cb_event_loop_add_timer(cli->loop, timer_cb, userdata);
}

static void timer_update(struct cb_client *client, void *handle, u32 ms, u32 us)
{
	if (!client || !handle)
		return;

	cb_event_source_timer_update(handle, ms, us);
}

static void *add_fd_handler(struct cb_client *client, void *userdata,
			    s32 fd, u32 mask,
			    s32 (*fd_cb)(s32 fd, u32 mask, void *data))
{
	struct client *cli = to_client(client);

	if (fd < 0 || !client || !fd_cb)
		return NULL;

	return cb_event_loop_add_fd(cli->loop, fd, mask, fd_cb, userdata);
}

static void *add_signal_handler(struct cb_client *client, void *userdata,
				s32 signal_number,
				s32 (*signal_cb)(s32 signal_number,
						 void *data))
{
	struct client *cli = to_client(client);

	if (!signal_number || !client || !signal_cb)
		return NULL;

	return cb_event_loop_add_signal(cli->loop, signal_number, signal_cb,
					userdata);
}

static void rm_handler(struct cb_client *client, void *handle)
{
	if (handle)
		cb_event_source_remove(handle);
}

struct cb_client *cb_client_create(s32 seat)
{
	struct client *cli;
	s32 ret, i;
	char name[64];

	cli = calloc(1, sizeof(*cli));
	if (!cli)
		return NULL;

	for (i = 0; i < MAX_DISP_NR; i++) {
		INIT_LIST_HEAD(&cli->base.displays[i].modes);
	}

	cli->base.destroy = destroy;

	cli->loop = cb_event_loop_create();
	if (!cli->loop)
		goto err;

	cli->sock = cb_socket_cloexec();
	if (!cli->sock)
		goto err;

	memset(name, 0, 64);
	snprintf(name, 64, "%s/%s-%d", CB_SERVER_NAME_PREFIX,SERVER_NAME, seat);
	ret = cb_socket_connect(cli->sock, name);
	if (ret < 0) {
		fprintf(stderr, "failed to connect to cube server %s. (%s)\n",
			name, strerror(-ret));
		goto err;
	}

	cli->sock_source = cb_event_loop_add_fd(cli->loop,
						cli->sock,
						CB_EVT_READABLE,
						sock_cb,
						cli);
	if (!cli->sock_source)
		goto err;

	cli->byts_to_rd = sizeof(size_t);
	cli->cursor = cli->ipc_buf;
	cli->ipc_sz = 0;

	cli->create_surface_tx_cmd_t = cb_client_create_surface_cmd(NULL,
						&cli->create_surface_tx_len);
	if (!cli->create_surface_tx_cmd_t)
		goto err;
	cli->create_surface_tx_cmd = malloc(cli->create_surface_tx_len);

	cli->create_view_tx_cmd_t = cb_client_create_view_cmd(NULL,
						&cli->create_view_tx_len);
	if (!cli->create_view_tx_cmd_t)
		goto err;
	cli->create_view_tx_cmd = malloc(cli->create_view_tx_len);

	cli->create_bo_tx_cmd_t = cb_client_create_bo_cmd(NULL,
						&cli->create_bo_tx_len);
	if (!cli->create_bo_tx_cmd_t)
		goto err;
	cli->create_bo_tx_cmd = malloc(cli->create_bo_tx_len);

	cli->destroy_bo_tx_cmd_t = cb_client_destroy_bo_cmd(0UL,
						&cli->destroy_bo_tx_len);
	if (!cli->destroy_bo_tx_cmd_t)
		goto err;
	cli->destroy_bo_tx_cmd = malloc(cli->destroy_bo_tx_len);

	cli->commit_mc_tx_cmd_t = cb_client_create_mc_commit_cmd(NULL,
						&cli->commit_mc_tx_len);
	if (!cli->commit_mc_tx_cmd_t)
		goto err;
	cli->commit_mc_tx_cmd = malloc(cli->commit_mc_tx_len);

	cli->commit_tx_cmd_t = cb_client_create_commit_req_cmd(NULL,
						&cli->commit_tx_len);
	if (!cli->commit_tx_cmd_t)
		goto err;
	cli->commit_tx_cmd = malloc(cli->commit_tx_len);

	cli->terminate_tx_cmd_t = cb_client_create_destroy_cmd(0UL,
						&cli->terminate_tx_len);
	if (!cli->terminate_tx_cmd_t)
		goto err;
	cli->terminate_tx_cmd = malloc(cli->terminate_tx_len);

	cli->shell_tx_cmd_t = cb_create_shell_cmd(NULL, &cli->shell_tx_len);
	if (!cli->shell_tx_cmd_t)
		goto err;
	cli->shell_tx_cmd = malloc(cli->shell_tx_len);

	cli->get_edid_cmd_t = cb_client_create_get_edid_cmd(
					0, &cli->get_edid_len);
	if (!cli->get_edid_cmd_t)
		goto err;
	cli->get_edid_cmd = malloc(cli->get_edid_len);

	cli->set_kbd_led_st_cmd_t = cb_client_create_set_kbd_led_st_cmd(
					0, &cli->set_kbd_led_st_len);
	if (!cli->set_kbd_led_st_cmd_t)
		goto err;
	cli->set_kbd_led_st_cmd = malloc(cli->set_kbd_led_st_len);

	cli->get_kbd_led_st_cmd = cb_client_create_get_kbd_led_st_cmd(
					&cli->get_kbd_led_st_len);
	if (!cli->get_kbd_led_st_cmd)
		goto err;

	cli->raw_input_en_cmd_t = cb_client_create_raw_input_en_cmd(0UL,
						&cli->raw_input_en_len);
	if (!cli->raw_input_en_cmd_t)
		goto err;
	cli->raw_input_en_cmd = malloc(cli->raw_input_en_len);

	cli->base.run = run;
	cli->base.stop = stop;
	cli->base.set_server_dbg = set_server_dbg;
	cli->base.set_client_cap = set_client_cap;
	cli->base.set_raw_input_evts_cb = set_raw_input_evts_cb;
	cli->base.set_ready_cb = set_ready_cb;
	cli->base.set_destroyed_cb = set_destroyed_cb;
	cli->base.set_raw_input_en = set_raw_input_en;
	cli->base.send_get_edid = send_get_edid;
	cli->base.set_get_edid_cb = set_get_edid_cb;
	cli->base.send_set_kbd_led_st = send_set_kbd_led_st;
	cli->base.send_get_kbd_led_st = send_get_kbd_led_st;
	cli->base.set_kbd_led_st_cb = set_kbd_led_st_cb;
	cli->base.enumerate_mode = enumerate_mode;
	cli->base.set_enumerate_mode_cb = set_enumerate_mode_cb;
	cli->base.query_layout = query_layout;
	cli->base.set_layout_query_cb = set_layout_query_cb;
	cli->base.set_layout_changed_cb = set_layout_changed_cb;
	cli->base.change_layout = change_layout;
	cli->base.create_mode = create_mode;
	cli->base.set_create_mode_cb = set_create_mode_cb;
	cli->base.create_surface = create_surface;
	cli->base.set_create_surface_cb = set_create_surface_cb;
	cli->base.create_view = create_view;
	cli->base.set_create_view_cb = set_create_view_cb;
	cli->base.create_bo = create_bo;
	cli->base.set_create_bo_cb = set_create_bo_cb;
	cli->base.destroy_bo = destroy_bo;
	cli->base.commit_bo = commit_bo;
	cli->base.set_commit_bo_cb = set_commit_bo_cb;
	cli->base.set_bo_flipped_cb = set_bo_flipped_cb;
	cli->base.set_bo_completed_cb = set_bo_completed_cb;
	cli->base.commit_mc = commit_mc;
	cli->base.set_commit_mc_cb = set_commit_mc_cb;
	cli->base.set_mc_flipped_cb = set_mc_flipped_cb;
	cli->base.set_hpd_cb = set_hpd_cb;
	cli->base.rm_handler = rm_handler;
	cli->base.add_fd_handler = add_fd_handler;
	cli->base.add_signal_handler = add_signal_handler;
	cli->base.add_timer_handler = add_timer_handler;
	cli->base.timer_update = timer_update;
	cli->base.add_idle_task = add_idle_task;

	return &cli->base;
err:
	destroy(&cli->base);
	return NULL;
}

s32 cb_drm_device_open(const char *devnode)
{
	s32 drmfd;

	if (!devnode)
		return -EINVAL;

	drmfd = open(devnode, O_RDWR | O_CLOEXEC, 0644);
	if (drmfd < 0) {
		fprintf(stderr, "failed to open drmfd. %s", strerror(errno));
		return -errno;
	}

	return drmfd;
}

void cb_drm_device_close(s32 drmfd)
{
	if (drmfd > 0)
		close(drmfd);
}

void *cb_gbm_open(s32 drmfd)
{
	return gbm_create_device(drmfd);
}

void cb_gbm_close(void *gbm)
{
	gbm_device_destroy(gbm);
}

void *cb_client_gbm_bo_create(s32 drmfd,
			      void *gbm,
			      enum cb_pix_fmt pix_fmt,
			      u32 width,
			      u32 height,
			      s32 *count_fds, /* output */
			      s32 *count_planes, /* output */
			      u32 strides[4], /* output */
			      s32 fds[4] /* output */)
{
	struct client_buffer *buffer;

	if (drmfd < 0)
		return NULL;

	if (!gbm || !width || !height || !fds || !count_fds || drmfd < 0 ||
	    !count_planes || !strides)
		return NULL;

	buffer = calloc(1, sizeof(*buffer));
	if (!buffer)
		return NULL;
	buffer->drmfd = drmfd;

	switch (pix_fmt) {
	/**
	 * 32-bit ARGB format. B [7:0]  G [15:8]  R [23:16] A [31:24]
	 */
	case CB_PIX_FMT_ARGB8888:
		buffer->fourcc = mk_fourcc('A', 'R', '2', '4');
		break;
	/**
	 * 32-bit XRGB format. B [7:0]  G [15:8]  R [23:16] X [31:24]
	 */
	case CB_PIX_FMT_XRGB8888:
		buffer->fourcc = mk_fourcc('X', 'R', '2', '4');
		break;
	/**
	 * 24-bit RGB 888 format. B [7:0]  G [15:8]  R [23:16]
	 */
	case CB_PIX_FMT_RGB888:
		buffer->fourcc = mk_fourcc('R', 'G', '2', '4');
		break;
	/**
	 * 2 plane YCbCr format, 2x2 subsampled Cb:Cr plane
	 */
	case CB_PIX_FMT_NV12:
	/**
	 * 2 plane YCbCr format, 2x1 subsampled Cb:Cr plane
	 */
	case CB_PIX_FMT_NV16:
	/**
	 * 2 plane YCbCr format, non-subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_NV24:
	/**
	 * 16-bit RGB 565 format. B [4:0]  G [10:5]  R [15:11]
	 */
	case CB_PIX_FMT_RGB565:
	/**
	 * packed YCbCr format, Y0Cb0 Y1Cr0 Y2Cb2 Y3Cr2
	 */
	case CB_PIX_FMT_YUYV:
	/**
	 * 3 plane YCbCr format, 2x2 subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_YUV420:
	/**
	 * 3 plane YCbCr format, 2x1 subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_YUV422:
	/**
	 * 3 plane YCbCr format, non-subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_YUV444:
	default:
		fprintf(stderr, "unsupport fmt\n");
		return NULL;
	}

	buffer->info.pix_fmt = pix_fmt;
	buffer->info.type = CB_BUF_TYPE_DMA;
	buffer->info.width = width;
	buffer->info.height = height;

	buffer->client_bo = gbm_bo_create(gbm, buffer->info.width,
					  buffer->info.height,
					  buffer->fourcc,
					  GBM_BO_USE_RENDERING |
					  	GBM_BO_USE_SCANOUT);
	if (!buffer->client_bo) {
		free(buffer);
		return NULL;
	}

	if (buffer->info.pix_fmt == CB_PIX_FMT_NV12 ||
	    buffer->info.pix_fmt == CB_PIX_FMT_NV16 ||
	    buffer->info.pix_fmt == CB_PIX_FMT_NV24) {
		/* TODO */
	} else if (buffer->info.pix_fmt == CB_PIX_FMT_ARGB8888 ||
		   buffer->info.pix_fmt == CB_PIX_FMT_RGB888 ||
		   buffer->info.pix_fmt == CB_PIX_FMT_XRGB8888) {
		buffer->info.strides[0] = gbm_bo_get_stride(buffer->client_bo);
		buffer->info.offsets[0] = 0;
		buffer->info.planes = 1;
		buffer->count_fds = 1;
		buffer->info.fd[0] = gbm_bo_get_fd(buffer->client_bo);
		*count_fds = 1;
		*count_planes = 1;
		strides[0] = buffer->info.strides[0];
		fds[0] = buffer->info.fd[0];
	}

	return buffer;
}

void cb_client_gbm_bo_destroy(void *bo)
{
	struct client_buffer *buffer = bo;

	if (!bo)
		return;

	gbm_bo_destroy(buffer->client_bo);
	free(buffer);
}

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
				  u32 offsets[4])
{
	struct client_buffer *buffer;
	s32 ret, i;
	struct drm_mode_create_dumb create_arg;
	struct drm_mode_map_dumb map_arg;
	struct drm_mode_destroy_dumb destroy_arg;
	u32 w, h;

	if (drmfd < 0)
		return NULL;

	if (!width || !height || !count_planes || !maps || !pitches ||
	    !offsets || !count_fds || !fds)
		return NULL;

	buffer = calloc(1, sizeof(*buffer));
	if (!buffer)
		return NULL;
	buffer->drmfd = drmfd;

	memset(&create_arg, 0, sizeof(create_arg));
	switch (pix_fmt) {
	/**
	 * 32-bit ARGB format. B [7:0]  G [15:8]  R [23:16] A [31:24]
	 */
	case CB_PIX_FMT_ARGB8888:
		buffer->fourcc = mk_fourcc('A', 'R', '2', '4');
		create_arg.bpp = 32;
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		create_arg.width = w;
		create_arg.height = h;
		break;
	/**
	 * 32-bit XRGB format. B [7:0]  G [15:8]  R [23:16] X [31:24]
	 */
	case CB_PIX_FMT_XRGB8888:
		printf("XR24\n");
		buffer->fourcc = mk_fourcc('X', 'R', '2', '4');
		create_arg.bpp = 32;
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		create_arg.width = w;
		create_arg.height = h;
		break;

	/**
	 * 2 plane YCbCr format, 2x2 subsampled Cb:Cr plane
	 */
	case CB_PIX_FMT_NV12:
		buffer->fourcc = mk_fourcc('N', 'V', '1', '2');
		create_arg.bpp = 8;
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		create_arg.width = w;
		/*
		 * rk's codec need more space to store someting
		 * so replace h * 3 / 2 as h * 2
		 */
		create_arg.height = h * 2;
		break;

	/**
	 * 2 plane YCbCr format, 2x1 subsampled Cb:Cr plane
	 */
	case CB_PIX_FMT_NV16:
		buffer->fourcc = mk_fourcc('N', 'V', '1', '6');
		create_arg.bpp = 8;
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		create_arg.width = w;
		create_arg.height = h * 2;
		break;

	/**
	 * 2 plane YCbCr format, non-subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_NV24:
		buffer->fourcc = mk_fourcc('N', 'V', '2', '4');
		create_arg.bpp = 8;
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		create_arg.width = w;
		create_arg.height = h * 3;
		if (cachable)
			create_arg.flags = 2;
		break;

	/**
	 * 24-bit RGB 888 format. B [7:0]  G [15:8]  R [23:16]
	 */
	case CB_PIX_FMT_RGB888:
	/**
	 * 16-bit RGB 565 format. B [4:0]  G [10:5]  R [15:11]
	 */
	case CB_PIX_FMT_RGB565:
	/**
	 * packed YCbCr format, Y0Cb0 Y1Cr0 Y2Cb2 Y3Cr2
	 */
	case CB_PIX_FMT_YUYV:
	/**
	 * 3 plane YCbCr format, 2x2 subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_YUV420:
	/**
	 * 3 plane YCbCr format, 2x1 subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_YUV422:
	/**
	 * 3 plane YCbCr format, non-subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_YUV444:
	default:
		fprintf(stderr, "unsupport fmt\n");
		return NULL;
	}

	*count_fds = 0;

	ret = drmIoctl(drmfd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg);
	if (ret) {
		fprintf(stderr, "failed to create dumb buffer. (%s)\n",
			strerror(errno));
		goto err_free;
	}

	buffer->info.pix_fmt = pix_fmt;
	buffer->info.type = CB_BUF_TYPE_DMA;
	buffer->info.width = width;
	buffer->info.height = height;
	if (buffer->info.pix_fmt == CB_PIX_FMT_NV12 ||
	    buffer->info.pix_fmt == CB_PIX_FMT_NV16 ||
	    buffer->info.pix_fmt == CB_PIX_FMT_NV24) {
		buffer->info.sizes[0] = create_arg.size;
		buffer->info.strides[0] = create_arg.pitch;
		buffer->info.offsets[0] = 0;
		if (buffer->info.pix_fmt == CB_PIX_FMT_NV24)
			buffer->info.strides[1] = create_arg.pitch * 2;
		else
			buffer->info.strides[1] = create_arg.pitch;
		buffer->info.offsets[1] = create_arg.pitch * h;
		buffer->info.planes = 1;
		*count_planes = 2;
		pitches[0] = buffer->info.strides[0];
		offsets[0] = 0;
		pitches[1] = buffer->info.strides[1];
		offsets[1] = create_arg.pitch * h;
	} else if (buffer->info.pix_fmt == CB_PIX_FMT_ARGB8888 ||
		   buffer->info.pix_fmt == CB_PIX_FMT_XRGB8888) {
		buffer->info.sizes[0] = create_arg.size;
		buffer->info.strides[0] = create_arg.pitch;
		buffer->info.offsets[0] = 0;
		buffer->info.planes = 1;
		*count_planes = 1;
		pitches[0] = buffer->info.strides[0];
		offsets[0] = 0;
	}

	buffer->handles[0] = create_arg.handle;
	*count_fds = 1;

	ret = drmPrimeHandleToFD(drmfd, create_arg.handle, 0,
				 &buffer->info.fd[0]);
	if (ret) {
		fprintf(stderr, "failed to export buffer. (%s)\n",
			strerror(errno));
		goto err_destroy_dumb;
	}

	buffer->count_fds = 1;
	fds[0] = buffer->info.fd[0];

	if (map) {
		memset(&map_arg, 0, sizeof(map_arg));
		map_arg.handle = create_arg.handle;
		ret = drmIoctl(drmfd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg);
		if (ret) {
			fprintf(stderr, "failed to map dumb. (%s)\n",
				strerror(errno));
			goto err_close_fd;
		}

		buffer->info.maps[0] = mmap(NULL, buffer->info.sizes[0],
					    PROT_WRITE, MAP_SHARED,
					    drmfd, map_arg.offset);
		if (buffer->info.maps[0] == MAP_FAILED) {
			fprintf(stderr, "failed to mmap. (%s)\n",
				strerror(errno));
			goto err_close_fd;
		}
		maps[0] = buffer->info.maps[0];
	}

	return buffer;

err_close_fd:
	for (i = 0; i < *count_fds; i++) {
		if (buffer->handles[i]) {
			if (buffer->info.fd[i]) {
				close(buffer->info.fd[i]);
			}
		}
	}

err_destroy_dumb:
	for (i = 0; i < *count_fds; i++) {
		memset(&destroy_arg, 0, sizeof(destroy_arg));
		destroy_arg.handle = buffer->handles[i];
		drmIoctl(drmfd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
	}

err_free:
	free(buffer);

	return NULL;
}

struct dma_buf_sync {
	u64 flags;
};

#define DMA_BUF_SYNC_READ      (1 << 0)
#define DMA_BUF_SYNC_WRITE     (2 << 0)
#define DMA_BUF_SYNC_RW        (DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START     (0 << 2)
#define DMA_BUF_SYNC_END       (1 << 2)
#define DMA_BUF_SYNC_VALID_FLAGS_MASK \
	(DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END)

#define DMA_BUF_BASE		'b'
#define DMA_BUF_IOCTL_SYNC	_IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)

void cb_client_dma_buf_bo_sync_begin(void *bo)
{
	struct client_buffer *buffer = bo;
	struct dma_buf_sync sync;
	s32 ret;

	if (!bo)
		return;

	do {
		sync.flags = DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_START;
		ret = ioctl(buffer->info.fd[0], DMA_BUF_IOCTL_SYNC, &sync);
	} while (ret < 0 && (errno == EAGAIN || errno == EINTR));

	if (ret < 0) {
		fprintf(stderr, "failed to sync DMA-BUF with sync start. "
			"(%s)\n", strerror(errno));
	}
}

void cb_client_dma_buf_bo_sync_end(void *bo)
{
	struct client_buffer *buffer = bo;
	struct dma_buf_sync sync;
	s32 ret;

	if (!bo)
		return;

	do {
		sync.flags = DMA_BUF_SYNC_WRITE | DMA_BUF_SYNC_END;
		ret = ioctl(buffer->info.fd[0], DMA_BUF_IOCTL_SYNC, &sync);
	} while (ret < 0 && (errno == EAGAIN || errno == EINTR));

	if (ret < 0) {
		fprintf(stderr, "failed to sync DMA-BUF with sync end. (%s)\n",
			strerror(errno));
	}
}

void cb_client_dma_buf_bo_destroy(void *bo)
{
	s32 i;
	struct drm_mode_destroy_dumb destroy_arg;
	struct client_buffer *buffer = bo;

	for (i = 0; i < buffer->count_fds; i++) {
		if (buffer->info.maps[i])
			munmap(buffer->info.maps[i], buffer->info.sizes[i]);
	
		if (buffer->handles[i]) {
			if (buffer->info.fd[i]) {
				close(buffer->info.fd[i]);
			}
			memset(&destroy_arg, 0, sizeof(destroy_arg));
			destroy_arg.handle = buffer->handles[i];
			drmIoctl(buffer->drmfd,
				 DRM_IOCTL_MODE_DESTROY_DUMB,
				 &destroy_arg);
		}
	}

	free(buffer);
}

void *cb_client_shm_bo_create(const char *name,
			      enum cb_pix_fmt pix_fmt,
			      u32 width,
			      u32 height,
			      u32 hstride,
			      u32 vstride,
			      s32 *count_planes, /* output */
			      void *maps[4], /* output */
			      u32 pitches[4], /* output */
			      u32 offsets[4],
			      u32 sizes[4])
{
	struct client_buffer *buffer;
	s32 ret;
	u32 w, h, size;

	if (!width || !height || !count_planes || !maps || !pitches ||
	    !offsets || !sizes)
		return NULL;

	buffer = calloc(1, sizeof(*buffer));
	if (!buffer)
		return NULL;

	switch (pix_fmt) {
	/**
	 * 32-bit ARGB format. B [7:0]  G [15:8]  R [23:16] A [31:24]
	 */
	case CB_PIX_FMT_ARGB8888:
		buffer->fourcc = mk_fourcc('A', 'R', '2', '4');
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		size = w * h * 4;
		break;
	/**
	 * 32-bit XRGB format. B [7:0]  G [15:8]  R [23:16] X [31:24]
	 */
	case CB_PIX_FMT_XRGB8888:
		buffer->fourcc = mk_fourcc('X', 'R', '2', '4');
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		size = w * h * 4;
		break;

	/**
	 * 2 plane YCbCr format, 2x2 subsampled Cb:Cr plane
	 */
	case CB_PIX_FMT_NV12:
		buffer->fourcc = mk_fourcc('N', 'V', '1', '2');
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		size = w * h * 3 / 2;
		break;

	/**
	 * 2 plane YCbCr format, 2x1 subsampled Cb:Cr plane
	 */
	case CB_PIX_FMT_NV16:
		buffer->fourcc = mk_fourcc('N', 'V', '1', '6');
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		size = w * h * 2;
		break;

	/**
	 * 2 plane YCbCr format, non-subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_NV24:
		buffer->fourcc = mk_fourcc('N', 'V', '2', '4');
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		size = w * h * 3;
		break;

	/**
	 * 3 plane YCbCr format, 2x2 subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_YUV420:
		buffer->fourcc = mk_fourcc('Y', 'U', '1', '2');
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		size = w * h * 3 / 2;
		break;

	/**
	 * 3 plane YCbCr format, non-subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_YUV444:
		buffer->fourcc = mk_fourcc('Y', 'U', '2', '4');
		if (!hstride)
			w = (width + 16 - 1) & ~(16 - 1);
		else
			w = hstride;
		if (!vstride)
			h = (height + 16 - 1) & ~(16 - 1);
		else
			h = vstride;
		size = w * h * 3;
		break;

	/**
	 * 24-bit RGB 888 format. B [7:0]  G [15:8]  R [23:16]
	 */
	case CB_PIX_FMT_RGB888:
	/**
	 * 16-bit RGB 565 format. B [4:0]  G [10:5]  R [15:11]
	 */
	case CB_PIX_FMT_RGB565:
	/**
	 * packed YCbCr format, Y0Cb0 Y1Cr0 Y2Cb2 Y3Cr2
	 */
	case CB_PIX_FMT_YUYV:
	/**
	 * 3 plane YCbCr format, 2x1 subsampled Cb and Cr planes
	 */
	case CB_PIX_FMT_YUV422:
	default:
		fprintf(stderr, "unsupport fmt\n");
		return NULL;
	}

	buffer->info.pix_fmt = pix_fmt;
	buffer->info.type = CB_BUF_TYPE_SHM;
	buffer->info.width = width;
	buffer->info.height = height;

	ret = cb_shm_init(&buffer->info.shm, name, size, 1);
	if (ret < 0) {
		fprintf(stderr, "failed to create shm buffer %s.\n", name);
		goto err;
	}

	buffer->info.shm_size = size;
	sizes[0] = size;
	maps[0] = buffer->info.shm.map;
	strncpy(buffer->info.shm_name, name, CB_SHM_NM_MAX_LEN - 1);

	if (buffer->info.pix_fmt == CB_PIX_FMT_NV12 ||
	    buffer->info.pix_fmt == CB_PIX_FMT_NV16 ||
	    buffer->info.pix_fmt == CB_PIX_FMT_NV24) {
		buffer->info.sizes[0] = w * h;
		buffer->info.strides[0] = w;
		buffer->info.offsets[0] = 0;
		buffer->info.strides[1] = w;
		buffer->info.offsets[1] = buffer->info.sizes[0];
		buffer->info.planes = 1;
		*count_planes = 2;
		pitches[0] = buffer->info.strides[0];
		offsets[0] = 0;
		pitches[1] = buffer->info.strides[0];
		offsets[1] = buffer->info.sizes[0];
	} else if (buffer->info.pix_fmt == CB_PIX_FMT_ARGB8888 ||
		   buffer->info.pix_fmt == CB_PIX_FMT_XRGB8888) {
		buffer->info.sizes[0] = size;
		buffer->info.strides[0] = w * 4;
		buffer->info.offsets[0] = 0;
		buffer->info.planes = 1;
		*count_planes = 1;
		pitches[0] = buffer->info.strides[0];
		offsets[0] = 0;
	} else if (buffer->info.pix_fmt == CB_PIX_FMT_YUV420) {
		buffer->info.sizes[0] = w * h;
		buffer->info.strides[0] = w;
		buffer->info.offsets[0] = 0;

		buffer->info.sizes[1] = w * h / 4;
		buffer->info.strides[1] = w / 2;
		buffer->info.offsets[1] = buffer->info.offsets[0]
				+ buffer->info.sizes[0];

		buffer->info.sizes[2] = w * h / 4;
		buffer->info.strides[2] = w / 2;
		buffer->info.offsets[2] = buffer->info.offsets[1]
					+ buffer->info.sizes[1];

		buffer->info.planes = 1;
		*count_planes = 3;
		pitches[0] = buffer->info.strides[0];
		offsets[0] = buffer->info.offsets[0];
		pitches[1] = buffer->info.strides[1];
		offsets[1] = buffer->info.offsets[1];
		pitches[2] = buffer->info.strides[2];
		offsets[2] = buffer->info.offsets[2];
	} else if (buffer->info.pix_fmt == CB_PIX_FMT_YUV444) {
		buffer->info.sizes[0] = w * h;
		buffer->info.strides[0] = w;
		buffer->info.offsets[0] = 0;

		buffer->info.sizes[1] = w * h;
		buffer->info.strides[1] = w;
		buffer->info.offsets[1] = buffer->info.offsets[0]
				+ buffer->info.sizes[0];

		buffer->info.sizes[2] = w * h;
		buffer->info.strides[2] = w;
		buffer->info.offsets[2] = buffer->info.offsets[1]
					+ buffer->info.sizes[1];

		buffer->info.planes = 1;
		*count_planes = 3;
		pitches[0] = buffer->info.strides[0];
		offsets[0] = buffer->info.offsets[0];
		pitches[1] = buffer->info.strides[1];
		offsets[1] = buffer->info.offsets[1];
		pitches[2] = buffer->info.strides[2];
		offsets[2] = buffer->info.offsets[2];
	}

	return buffer;

err:
	free(buffer);
	return NULL;
}

void cb_client_shm_bo_destroy(void *bo)
{
	struct client_buffer *buffer = bo;

	cb_shm_release(&buffer->info.shm);
	free(buffer);
}

