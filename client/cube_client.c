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

#define client_debug(client, fmt, ...) do { \
	if ((client)->debug_level >= CB_LOG_DEBUG) { \
		cb_client_tlog((client)->pid_name, (client)->log_handle, \
				"[CLI-DEBUG ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define client_info(client, fmt, ...) do { \
	if ((client)->debug_level >= CB_LOG_INFO) { \
		cb_client_tlog((client)->pid_name, (client)->log_handle, \
				"[CLI-INFO  ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define client_notice(client, fmt, ...) do { \
	if ((client)->debug_level >= CB_LOG_NOTICE) { \
		cb_client_tlog((client)->pid_name, (client)->log_handle, \
				"[CLI-NOTICE] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define client_warn(client, fmt, ...) do { \
	cb_client_tlog((client)->pid_name, (client)->log_handle, \
			"[CLI-WARN  ] "fmt, ##__VA_ARGS__); \
} while (0);

#define client_err(client, fmt, ...) do { \
	cb_client_tlog((client)->pid_name, (client)->log_handle, \
			"[CLI-ERROR ] "fmt, ##__VA_ARGS__); \
} while (0);

struct client {
	struct cb_client base;

	struct cb_event_loop *loop;

	void *log_handle;
	char pid_name[9];
	enum cb_log_level debug_level;

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

	void *input_msg_cb_userdata;
	void (*input_msg_cb)(void *userdata, struct cb_gui_input_msg *msg,
			     u32 count_msg);

	void *raw_input_evts_cb_userdata;
	void (*raw_input_evts_cb)(void *userdata, struct cb_raw_input_event *,
				  u32 count_evts);

	void *raw_touch_evts_cb_userdata;
	void (*raw_touch_evts_cb)(void *userdata, struct touch_event *, u32 sz);

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
	void (*bo_commited_cb)(bool success, void *userdata, u64 bo_id,
			       u64 surface_id);

	void (*bo_af_commited_cb)(bool success, void *userdata, u64 bo_id,
				  u64 surface_id);

	void *bo_flipped_cb_userdata;
	void (*bo_flipped_cb)(void *userdata, u64 bo_id, u64 surface_id);

	void *bo_completed_cb_userdata;
	void (*bo_completed_cb)(void *userdata, u64 bo_id, u64 surface_id);

	void *mc_commited_cb_userdata;
	void (*mc_commited_cb)(bool success, void *userdata, u64 bo_id);

	void *hpd_cb_userdata;
	void (*hpd_cb)(void *userdata, struct cb_connector_info *info);

	void *kbd_led_st_cb_userdata;
	void (*kbd_led_st_cb)(void *userdata, u32 led_status);

	void *view_focus_chg_userdata;
	void (*view_focus_chg_cb)(void *userdata, u64 view_id, bool on);

	void *connection_lost_cb_userdata;
	void (*connection_lost_cb)(void *userdata);
};

struct client_buffer {
	struct cb_buffer_info info;
	u32 fourcc;
	u64 bo_id;
	s32 count_fds;
	u32 handles[4];
	s32 drmfd;
	struct cb_shm shm;
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

	if (cli->log_handle) {
		client_notice(cli, "Destroying cube client ...");
	}

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

		if (cli->create_surface_tx_cmd_t)
			free(cli->create_surface_tx_cmd_t);

		if (cli->create_surface_tx_cmd)
			free(cli->create_surface_tx_cmd);

		if (cli->sock_source)
			cb_event_source_remove(cli->sock_source);

		if (cli->sock_source)
			cb_event_source_remove(cli->sock_source);

		if (cli->sock > 0)
			close(cli->sock);

		cb_event_loop_destroy(cli->loop);
	}

	if (cli->log_handle) {
		client_notice(cli, "cube client is destroyed.");
		cb_client_log_fini(cli->log_handle);
		cli->log_handle = NULL;
	}

	free(cli);
}

static void run(struct cb_client *client)
{
	struct client *cli = to_client(client);

	client_debug(cli, "run");
	while (!cli->exit) {
		cb_event_loop_dispatch(cli->loop, -1);
	}
	client_debug(cli, "run exit");
}

static void stop(struct cb_client *client)
{
	struct client *cli = to_client(client);

	client_debug(cli, "stop run");
	cli->exit = true;
}

static void set_raw_input_en(struct cb_client *client, bool en)
{
	struct client *cli = to_client(client);
	size_t length;
	u8 *p;
	s32 ret;

	client_notice(cli, "set raw input enabled: %c", en ? 'Y' : 'N');
	p = cb_dup_raw_input_en_cmd(cli->raw_input_en_cmd,
				cli->raw_input_en_cmd_t,
				cli->raw_input_en_len, en);
	if (!p) {
		client_err(cli, "failed to dup raw input enable");
		return;
	}
	
	length = cli->raw_input_en_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send raw input en length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->raw_input_en_cmd, length,NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send raw input en. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	client_notice(cli, "get kbd led status");

	length = cli->get_kbd_led_st_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send get kbd led st length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->get_kbd_led_st_cmd, length,
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send get kbd led st cmd. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_debug(cli, "set kbd led status cb %p, %p", kbd_led_st_cb,
		     userdata);

	if (!kbd_led_st_cb) {
		client_err(cli, "kbd_led_st_cb is null");
		return -EINVAL;
	}

	cli->kbd_led_st_cb_userdata = userdata;
	cli->kbd_led_st_cb = kbd_led_st_cb;

	return 0;
}

static void set_connection_lost_cb(struct cb_client *client, void *userdata,
				   void (*connection_lost_cb)(void *))
{
	struct client *cli = to_client(client);

	if (!client)
		assert(0);

	cli->connection_lost_cb_userdata = userdata;
	cli->connection_lost_cb = connection_lost_cb;
}

static s32 set_view_focus_chg_cb(struct cb_client *client, void *userdata,
				 void (*view_focus_chg_cb)(void *userdata,
				 			   u64 view_id,
				 			   bool on))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set kbd view focus chg cb %p, %p", view_focus_chg_cb,
		     userdata);

	if (!view_focus_chg_cb) {
		client_err(cli, "view_focus_chg_cb is null");
		return -EINVAL;
	}

	cli->view_focus_chg_userdata = userdata;
	cli->view_focus_chg_cb = view_focus_chg_cb;

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

	client_notice(cli, "send get edid command");
	cmd = cb_dup_get_edid_cmd(cli->get_edid_cmd,
				  cli->get_edid_cmd_t,
				  cli->get_edid_len,
				  pipe);
	if (!cmd) {
		client_err(cli, "failed to dup get edid cmd");
		return -EINVAL;
	}

	length = cli->get_edid_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send get edid length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->get_edid_cmd, length,
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send get edid cmd. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	client_notice(cli, "send set kbd led status %08X", led_status);
	cmd = cb_dup_set_kbd_led_st_cmd(cli->set_kbd_led_st_cmd,
					cli->set_kbd_led_st_cmd_t,
					cli->set_kbd_led_st_len,
					led_status);
	if (!cmd) {
		client_err(cli, "failed to dup set kbd led status cmd");
		return -EINVAL;
	}

	length = cli->set_kbd_led_st_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send set kbd led st length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->set_kbd_led_st_cmd, length,
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send set kbd led st cmd. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_debug(cli, "set client cap %016X", cap);
	if (!cap) {
		client_err(cli, "cap is 0");
		return -EINVAL;
	}

	set_cap_tx_cmd = cb_client_create_set_cap_cmd(cap, &n);
	
	length = n;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send client cap length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, set_cap_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send client cap cmd (dbg). %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_warn(cli, "set server dbg");
	cli->shell.cmd = CB_SHELL_DEBUG_SETTING;
	memcpy(&cli->shell.value.dbg_flags, dbg_flags, sizeof(*dbg_flags));
	p = cb_dup_shell_cmd(cli->shell_tx_cmd, cli->shell_tx_cmd_t,
			     cli->shell_tx_len, &cli->shell);
	if (!p) {
		client_err(cli, "failed to dup shell cmd (dbg)");
		return -EINVAL;
	}
	
	length = cli->shell_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send shell cmd (dbg) length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->shell_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send shell cmd (dbg). %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_input_msg_cb(struct cb_client *client,
			    void *userdata,
			    void (*input_msg_cb)(
			    	void *userdata,
			    	struct cb_gui_input_msg *msg,
			    	u32 count_msg))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set input msg cb %p, %p",
		     input_msg_cb, userdata);

	if (!input_msg_cb) {
		client_err(cli, "input_msg_cb is null");
		return -EINVAL;
	}

	cli->input_msg_cb_userdata = userdata;
	cli->input_msg_cb = input_msg_cb;

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

	if (!client)
		return -EINVAL;

	client_debug(cli, "set raw input events cb %p, %p",
		     raw_input_evts_cb, userdata);

	if (!raw_input_evts_cb) {
		client_err(cli, "raw_input_evts_cb is null");
		return -EINVAL;
	}

	cli->raw_input_evts_cb_userdata = userdata;
	cli->raw_input_evts_cb = raw_input_evts_cb;
	return 0;
}

static s32 set_raw_touch_evts_cb(struct cb_client *client,
				 void *userdata,
				 void (*raw_touch_evts_cb)(
				 		void *userdata,
				 		struct touch_event *,
				 		u32 sz))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set raw touch events cb %p, %p",
		     raw_touch_evts_cb, userdata);

	if (!raw_touch_evts_cb) {
		client_err(cli, "raw_touch_evts_cb is null");
		return -EINVAL;
	}

	cli->raw_touch_evts_cb_userdata = userdata;
	cli->raw_touch_evts_cb = raw_touch_evts_cb;
	return 0;
}

static s32 set_ready_cb(struct cb_client *client, void *userdata,
			void (*ready_cb)(void *userdata))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set ready cb %p, %p", ready_cb, userdata);

	if (!ready_cb) {
		client_err(cli, "ready_cb is null");
		return -EINVAL;
	}

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

	if (!client)
		return -EINVAL;

	client_debug(cli, "set get edid cb %p, %p", get_edid_cb, userdata);

	if (!get_edid_cb) {
		client_err(cli, "get_edid_cb is null");
		return -EINVAL;
	}

	cli->get_edid_cb_userdata = userdata;
	cli->get_edid_cb = get_edid_cb;
	return 0;
}

static s32 set_destroyed_cb(struct cb_client *client, void *userdata,
			    void (*destroyed_cb)(void *userdata))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set destroyed cb %p, %p", destroyed_cb, userdata);

	if (!destroyed_cb) {
		client_err(cli, "destroyed_cb is null");
		return -EINVAL;
	}

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

	client_notice(cli, "change layout");

	cli->shell.cmd = CB_SHELL_CANVAS_LAYOUT_SETTING;
	cli->shell.value.layout.count_heads = client->count_displays;
	client_debug(cli, "count_displays: %d", client->count_displays);
	for (i = 0; i < client->count_displays; i++) {
		disp = &client->displays[i];
		if (disp->desktop_rc.w > 4096 || disp->desktop_rc.h > 2160 ||
		    disp->desktop_rc.w == 0 || disp->desktop_rc.h == 0) {
			client_err(cli, "desktop rc[%d] out of range. %ux%u",
				   i, disp->desktop_rc.w, disp->desktop_rc.h);
			return -ERANGE;
		}
	}
	for (i = 0; i < client->count_displays; i++) {
		disp = &client->displays[i];
		client_debug(cli, "desktop rc[%d] %d,%d %ux%u", i,
			     disp->desktop_rc.pos.x,
			     disp->desktop_rc.pos.y,
			     disp->desktop_rc.w,
			     disp->desktop_rc.h);
		memcpy(&cli->shell.value.layout.cfg[i].desktop_rc,
		       &disp->desktop_rc, sizeof(struct cb_rect));
		client_debug(cli, "input rc[%d] %d,%d %ux%u", i,
			     disp->input_rc.pos.x,
			     disp->input_rc.pos.y,
			     disp->input_rc.w,
			     disp->input_rc.h);
		memcpy(&cli->shell.value.layout.cfg[i].input_rc,
		       &disp->input_rc, sizeof(struct cb_rect));
		cli->shell.value.layout.cfg[i].mode_handle = disp->pending_mode;
	}

	p = cb_dup_shell_cmd(cli->shell_tx_cmd, cli->shell_tx_cmd_t,
			     cli->shell_tx_len, &cli->shell);
	if (!p) {
		client_err(cli, "failed to dup shell cmd");
		return -EINVAL;
	}
	
	length = cli->shell_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send shell cmd length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->shell_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send shell cmd (layout). %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_debug(cli, "enumerate mode of pipe %d, lm %p, f: [%c, %p]",
		     pipe, last_mode, filter_en ? 'Y' : 'N', filter);
	if (pipe < 0) {
		client_err(cli, "pipe %d illegal", pipe);
		return -EINVAL;
	}

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
		client_err(cli, "failed to dup shell cmd (enumerate mode)");
		return -EINVAL;
	}
	
	length = cli->shell_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send shell cmd (enum) length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->shell_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send shell cmd (enum). %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_debug(cli, "set enumerate mode cb %p, %p", enumerate_mode_cb,
		     userdata);

	if (!enumerate_mode_cb) {
		client_err(cli, "enumerate_mode_cb is null");
		return -EINVAL;
	}

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

	client_debug(cli, "query_layout");
	cli->shell.cmd = CB_SHELL_CANVAS_LAYOUT_QUERY;
	p = cb_dup_shell_cmd(cli->shell_tx_cmd, cli->shell_tx_cmd_t,
			     cli->shell_tx_len, &cli->shell);
	if (!p) {
		client_err(cli, "failed to dup shell cmd (query layout)");
		return -EINVAL;
	}

	client_debug(cli, "Query layout heads nr(%d) cfg[%d] cfg[%d]",
		     cli->shell.value.layout.count_heads,
		     cli->shell.value.layout.cfg[0].pipe,
		     cli->shell.value.layout.cfg[1].pipe);
	
	length = cli->shell_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send shell cmd length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->shell_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send shell cmd (query layout). %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_layout_query_cb(struct cb_client *client, void *userdata,
			       void (*layout_query_cb)(void *userdata))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set layout query cb %p, %p", layout_query_cb,
		     userdata);

	if (!layout_query_cb) {
		client_err(cli, "layout_query_cb is null");
		return -EINVAL;
	}

	cli->layout_query_cb_userdata = userdata;
	cli->layout_query_cb = layout_query_cb;

	return 0;
}

static s32 set_layout_changed_cb(struct cb_client *client, void *userdata,
				 void (*layout_changed_cb)(void *userdata))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set layout changed cb %p, %p",
		     layout_changed_cb, userdata);

	if (!layout_changed_cb) {
		client_err(cli, "layout_changed_cb is null");
		return -EINVAL;
	}

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

	if (!client)
		return -EINVAL;

	client_debug(cli, "create custom mode (%p) for index %d", info, index);

	if (!info) {
		client_err(cli, "info is null");
		return -EINVAL;
	}

	client_debug(cli, "\tclock: %u", info->clock);
	client_debug(cli, "\twidth: %u", info->width);
	client_debug(cli, "\thsync_start: %u", info->hsync_start);
	client_debug(cli, "\thsync_end: %u", info->hsync_end);
	client_debug(cli, "\thtotal: %u", info->htotal);
	client_debug(cli, "\thskew: %u", info->hskew);
	client_debug(cli, "\theight: %u", info->height);
	client_debug(cli, "\tvsync_start: %u", info->vsync_start);
	client_debug(cli, "\tvsync_end: %u", info->vsync_end);
	client_debug(cli, "\tvtotal: %u", info->vtotal);
	client_debug(cli, "\tvscan: %u", info->vscan);
	client_debug(cli, "\tvrefresh: %u", info->vrefresh);
	client_debug(cli, "\tinterlaced: %d", info->interlaced);
	client_debug(cli, "\tHSync Polarity: %c", info->pos_hsync ? '+' : '-');
	client_debug(cli, "\tVSync Polarity: %c", info->pos_vsync ? '+' : '-');
	client_debug(cli, "\tPreferred: %c", info->preferred ? 'Y' : 'N');

	cli->shell.cmd = CB_SHELL_OUTPUT_VIDEO_TIMING_CREAT;
	memcpy(&cli->shell.value.mode, info, sizeof(*info));
	cli->shell.value.modeset_pipe = client->displays[index].pipe;
	p = cb_dup_shell_cmd(cli->shell_tx_cmd, cli->shell_tx_cmd_t,
			     cli->shell_tx_len, &cli->shell);
	if (!p) {
		client_err(cli, "failed to dup shell cmd");
		return -EINVAL;
	}
	
	length = cli->shell_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send shell cmd length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->shell_tx_cmd, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send shell cmd (create mode). %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_debug(cli, "set create mode cb %p, %p", mode_created_cb,
		     userdata);

	if (!mode_created_cb) {
		client_err(cli, "mode_created_cb is null");
		return -EINVAL;
	}

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

	if (!client)
		return -EINVAL;

	client_debug(cli, "create surface %p", s);

	if (!s) {
		client_err(cli, "s is null");
		return -EINVAL;
	}

	memcpy(&cli->s, s, sizeof(*s));
	p = cb_dup_create_surface_cmd(cli->create_surface_tx_cmd,
				      cli->create_surface_tx_cmd_t,
				      cli->create_surface_tx_len, &cli->s);
	if (!p) {
		client_err(cli, "failed to dup create surface cmd");
		return -EINVAL;
	}

	length = cli->create_surface_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send create surf length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->create_surface_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send create surf cmd. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_debug(cli, "set create surface cb %p, %p",
		     surface_created_cb, userdata);

	if (!surface_created_cb) {
		client_err(cli, "surface_created_cb is null");
		return -EINVAL;
	}

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

	if (!client)
		return -EINVAL;

	client_debug(cli, "create view %p", v);

	if (!v) {
		client_err(cli, "v is null");
		return -EINVAL;
	}

	memcpy(&cli->v, v, sizeof(*v));
	p = cb_dup_create_view_cmd(cli->create_view_tx_cmd,
				   cli->create_view_tx_cmd_t,
				   cli->create_view_tx_len, &cli->v);
	if (!p) {
		client_err(cli, "failed to dup create view cmd");
		return -EINVAL;
	}

	length = cli->create_view_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send create view length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->create_view_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send create view cmd. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_debug(cli, "set create view cb %p, %p", view_created_cb,
		     userdata);

	if (!view_created_cb) {
		client_err(cli, "view_created_cb is null");
		return -EINVAL;
	}

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

	if (!client)
		return -EINVAL;

	client_debug(cli, "create bo %p", bo);

	if (!bo) {
		client_err(cli, "bo is null");
		return -EINVAL;
	}

	p = cb_dup_create_bo_cmd(cli->create_bo_tx_cmd,
				 cli->create_bo_tx_cmd_t,
				 cli->create_bo_tx_len,
				 &buffer->info);
	if (!p) {
		client_err(cli, "failed to dup create bo cmd");
		return -EINVAL;
	}

	length = cli->create_bo_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send create bo length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		fds.count = buffer->count_fds;
		for (i = 0; i < fds.count; i++) {
			fds.fds[i] = buffer->info.fd[i];
		}
		ret = cb_sendmsg(cli->sock, cli->create_bo_tx_cmd,length, &fds);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send create bo cmd. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_debug(cli, "set create bo cb %p, %p", bo_created_cb, userdata);

	if (!bo_created_cb) {
		client_err(cli, "bo_created_cb is null");
		return -EINVAL;
	}

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

	if (!client)
		return -EINVAL;

	client_debug(cli, "destroy bo %lu", bo_id);

	if (!bo_id) {
		client_err(cli, "bo_id is zero");
		return -EINVAL;
	}

	p = cb_dup_destroy_bo_cmd(cli->destroy_bo_tx_cmd,
				  cli->destroy_bo_tx_cmd_t,
				  cli->destroy_bo_tx_len,
				  bo_id);
	if (!p) {
		client_err(cli, "failed to dup destroy bo cmd");
		return -EINVAL;
	}

	length = cli->destroy_bo_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send destroy bo length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->destroy_bo_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send destroy bo cmd. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_debug(cli, "client commit info %016X", c);

	if (!c) {
		client_err(cli, "c is null");
		return -EINVAL;
	}

	memcpy(&cli->c, c, sizeof(*c));
	p = cb_dup_commit_req_cmd(cli->commit_tx_cmd,
				  cli->commit_tx_cmd_t,
				  cli->commit_tx_len,
				  &cli->c);
	if (!p) {
		client_err(cli, "failed to dup commit bo cmd");
		return -EINVAL;
	}

	length = cli->commit_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send commit bo length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->commit_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send commit bo cmd. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static u8 *alloc_af_commit_info_buffer(struct cb_client *client)
{
	return cb_client_create_af_commit_buffer();
}

struct cb_af_commit_info *get_af_commit_info(struct cb_client *client,
					     u8 *buffer)
{
	struct client *cli = to_client(client);

	if (!buffer) {
		client_err(cli, "buffer is null");
		return NULL;
	}

	return cb_client_get_af_commit_info_from_buffer(buffer);
}

static s32 af_commit_bo(struct cb_client *client, u8 *buffer)
{
	struct client *cli = to_client(client);
	size_t length;
	s32 ret;
	u32 n;

	if (!client)
		return -EINVAL;

	client_debug(cli, "client af commit buffer %016X", buffer);
	if (!buffer) {
		client_err(cli, "buffer is null");
		return -EINVAL;
	}

	ret = cb_gen_af_commit_cmd(buffer, &n);
	if (ret < 0) {
		client_err(cli, "failed to generate af commit command");
		return -EINVAL;
	}
	client_debug(cli, "af commit command length %u", n);

	length = (size_t)n;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send af commit bo length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, buffer, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send af commit bo cmd. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	return 0;
}

static s32 set_commit_bo_cb(struct cb_client *client, void *userdata,
			    void (*bo_commited_cb)(
			    	bool success, void *userdata, u64 bo_id,
			    	u64 surface_id))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set commit bo cb %p, %p", bo_commited_cb, userdata);

	if (!bo_commited_cb) {
		client_err(cli, "bo_commited_cb is null");
		return -EINVAL;
	}

	cli->bo_commited_cb_userdata = userdata;
	cli->bo_commited_cb = bo_commited_cb;
	return 0;
}

static s32 set_bo_flipped_cb(struct cb_client *client, void *userdata,
			     void (*bo_flipped_cb)(
			     		void *userdata, u64 bo_id,
			     		u64 surface_id))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set bo flipped cb %p, %p", bo_flipped_cb, userdata);

	if (!bo_flipped_cb) {
		client_err(cli, "bo_flipped_cb is null");
		return -EINVAL;
	}

	cli->bo_flipped_cb_userdata = userdata;
	cli->bo_flipped_cb = bo_flipped_cb;
	return 0;
}

static s32 set_bo_completed_cb(struct cb_client *client, void *userdata,
			       void (*bo_completed_cb)(
			       		void *userdata, u64 bo_id,
			       		u64 surface_id))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set bo completed cb %p, %p", bo_completed_cb,
		     userdata);

	if (!bo_completed_cb) {
		client_err(cli, "bo_completed_cb is null");
		return -EINVAL;
	}

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

	client_debug(cli, "commit mc %016X", mc);

	if (!mc) {
		client_err(cli, "mc is null");
		return -EINVAL;
	}

	memcpy(&cli->mc, mc, sizeof(*mc));
	p = cb_dup_mc_commit_cmd(cli->commit_mc_tx_cmd,
				 cli->commit_mc_tx_cmd_t,
				 cli->commit_mc_tx_len,
				 &cli->mc);
	if (!p) {
		client_err(cli, "failed to dup commit mc cmd");
		return -EINVAL;
	}

	length = cli->commit_mc_tx_len;

	do {
		ret = cb_sendmsg(cli->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send commit mc length. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return -errno;
	}

	do {
		ret = cb_sendmsg(cli->sock, cli->commit_mc_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		client_err(cli, "failed to send commit mc cmd. %s",
			   strerror(errno));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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

	if (!client)
		return -EINVAL;

	client_debug(cli, "set commit mc cb %p, %p", mc_commited_cb, userdata);

	if (!mc_commited_cb) {
		client_err(cli, "mc_commited_cb is null");
		return -EINVAL;
	}

	cli->mc_commited_cb_userdata = userdata;
	cli->mc_commited_cb = mc_commited_cb;
	return 0;
}

s32 set_hpd_cb(struct cb_client *client, void *userdata,
	       void (*hpd_cb)(void *userdata, struct cb_connector_info *info))
{
	struct client *cli = to_client(client);

	if (!client)
		return -EINVAL;

	client_debug(cli, "set hpd cb %p, %p", hpd_cb, userdata);

	if (!hpd_cb) {
		client_err(cli, "hpd_cb is null");
		return -EINVAL;
	}

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
		client_err(cli, "shell failed. %ld", ret);
		return -EINVAL;
	}

	switch (cli->shell.cmd) {
	case CB_SHELL_DEBUG_SETTING:
		cli->debug_level = cli->shell.value.dbg_flags.client_flag;
		break;
	case CB_SHELL_CANVAS_LAYOUT_CHANGED_NOTIFY:
		client_debug(cli, "received layout changed event.");
		if (cli->base.count_displays == 0) {
			cli->base.count_displays = 
				cli->shell.value.layout.count_heads;
			client_debug(cli, "count of displays: %d",
				     cli->base.count_displays);
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
			client_debug(cli, "prepare to notify, cb: %p",
				     cli->layout_changed_cb);
			if (cli->layout_changed_cb) {
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
		client_debug(cli, "prepare to notify, cb: %p",
			     cli->layout_changed_cb);
		if (cli->layout_changed_cb) {
			cli->layout_changed_cb(
				cli->layout_changed_cb_userdata);
		}
		
		break;
	case CB_SHELL_CANVAS_LAYOUT_QUERY:
		client_debug(cli, "received layout query result.");
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
			client_debug(cli, "prepare to notify query result: %p",
				     cli->layout_query_cb);
			if (cli->layout_query_cb) {
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
		client_debug(cli, "prepare to notify query result: %p",
			     cli->layout_query_cb);
		if (cli->layout_query_cb) {
			cli->layout_query_cb(cli->layout_query_cb_userdata);
		}
		
		break;
	case CB_SHELL_OUTPUT_VIDEO_TIMING_ENUMERATE:
		client_debug(cli, "received enumerate result.");
		for (i = 0; i < cli->base.count_displays; i++) {
			if (cli->base.displays[i].pipe ==
			    cli->shell.value.ote.pipe)
				break;
		}
		if (i == cli->base.count_displays) {
			client_err(cli, "illegal pipe in shell (enum). %d %d",
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
			client_debug(cli, "prepare to notify enum result %p",
				     cli->enumerate_mode_cb);
			if (cli->enumerate_mode_cb) {
				cli->enumerate_mode_cb(
					cli->enumerate_mode_cb_userdata,
					mode);
			}
		} else {
			client_debug(cli, "prepare to notify last enum result "
				     "%p", cli->enumerate_mode_cb);
			if (cli->enumerate_mode_cb) {
				cli->enumerate_mode_cb(
					cli->enumerate_mode_cb_userdata, NULL);
			}
		}
		break;
	case CB_SHELL_OUTPUT_VIDEO_TIMING_CREAT:
		client_debug(cli, "received create custom mode result");
		for (i = 0; i < cli->base.count_displays; i++) {
			if (cli->base.displays[i].pipe ==
			    cli->shell.value.modeset_pipe)
				break;
		}
		if (i == cli->base.count_displays) {
			client_err(cli, "illegal pipe in shell (create).");
			return -EINVAL;
		}
		if (!cli->shell.value.new_mode_handle) {
			client_err(cli, "failed to create cust mode");
			if (cli->mode_created_cb) {
				cli->mode_created_cb(false,
					cli->mode_created_cb_userdata);
			}
		} else {
			cli->base.displays[i].mode_custom =
				cli->shell.value.new_mode_handle;
			client_debug(cli, "create cust mode ok, %p",
				     cli->mode_created_cb);
			if (cli->mode_created_cb) {
				cli->mode_created_cb(true,
					cli->mode_created_cb_userdata);
			}
		}
		break;
	default:
		client_err(cli, "unknown shell cmd %d", cli->shell.cmd);
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
	struct cb_gui_input_msg *msg;
	struct touch_event *tevts;
	u32 count_evts, led_status, touch_sz;
	u32 count_msg;
	u64 surface_id, view_id;
	bool focus_on;

	if (!cli)
		return;

	ipc_sz = cli->ipc_sz;
	buf = &cli->ipc_buf[0] + sizeof(size_t);
	flag = *((u32 *)buf);
	tlv = (struct cb_tlv *)(buf + sizeof(u32));
	assert(ipc_sz == (tlv->length + sizeof(*tlv) + sizeof(flag)));
	assert(tlv->tag == CB_TAG_WIN || tlv->tag == CB_TAG_RAW_INPUT ||
		tlv->tag == CB_TAG_RAW_TOUCH ||
		tlv->tag == CB_TAG_GET_KBD_LED_STATUS_ACK ||
		tlv->tag == CB_TAG_GET_EDID_ACK ||
		tlv->tag == CB_TAG_GUI_INPUT ||
		tlv->tag == CB_TAG_VIEW_FOCUS_CHG);

	if (tlv->tag == CB_TAG_GUI_INPUT) {
		msg = cb_client_parse_input_msg(buf, &count_msg);
		if (!msg) {
			client_err(cli, "failed to parse gui input msg.");
			return;
		}
		client_debug(cli, "input msg: %p %p %u",
			     cli->input_msg_cb, msg, count_msg);
		if (cli->input_msg_cb) {
			cli->input_msg_cb(cli->input_msg_cb_userdata,
					  msg, count_msg);
		}
		return;
	}

	if (tlv->tag == CB_TAG_RAW_INPUT) {
		evts = cb_client_parse_raw_input_evt_cmd(buf, &count_evts);
		if (!evts) {
			client_err(cli, "failed to parse raw input evts.");
			return;
		}
		client_debug(cli, "raw input evts: %p %p %u",
			     cli->raw_input_evts_cb, evts, count_evts);
		if (cli->raw_input_evts_cb) {
			cli->raw_input_evts_cb(cli->raw_input_evts_cb_userdata,
					       evts, count_evts);
		}
		return;
	}

	if (tlv->tag == CB_TAG_RAW_TOUCH) {
		tevts = cb_client_parse_raw_touch_evt_cmd(buf, &touch_sz);
		if (!tevts) {
			client_err(cli, "failed to parse raw touch evts.");
			return;
		}
		client_debug(cli, "raw touch evts: %p %p %u",
			     cli->raw_touch_evts_cb, tevts, touch_sz);
		if (cli->raw_touch_evts_cb) {
			cli->raw_touch_evts_cb(cli->raw_touch_evts_cb_userdata,
					       tevts, touch_sz);
		}
		return;
	}

	if (tlv->tag == CB_TAG_VIEW_FOCUS_CHG) {
		ret = cb_client_parse_view_focus_chg_cmd(buf, &view_id,
							 &focus_on);
		if (ret < 0) {
			client_err(cli, "failed to parse view focus chg "
				   "message. ret = %d", ret);
		} else {
			client_debug(cli, "received view focus chg event "
				     "%016X %c", view_id, focus_on ? 'Y': 'N');
			if (cli->view_focus_chg_cb) {
				cli->view_focus_chg_cb(
					cli->view_focus_chg_userdata,
					view_id,
					focus_on);
			}
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
				client_err(cli, "edid of pipe %lu not "
					   "available", cli->edid_pipe);
				cli->edid_sz = 0;
			} else {
				return;
			}
		}

		client_debug(cli, "received edid, len: %lu, %p",
			     cli->edid_sz, cli->get_edid_cb);
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
			client_err(cli, "failed to parse kbd led st ack.");
			return;
		}
		client_debug(cli, "received kbd led st result %08X, %p",
			     led_status, cli->kbd_led_st_cb);
		if (cli->kbd_led_st_cb) {
			cli->kbd_led_st_cb(cli->kbd_led_st_cb_userdata,
					   led_status);
		}
		return;
	}

	if (flag & (1 << CB_CMD_LINK_ID_ACK_SHIFT)) {
		id = cb_client_parse_link_id(buf);
		cli->link_id = id;
		client_debug(cli, "received link id %016X, %p",
			     id, cli->ready_cb);
		if (cli->ready_cb) {
			cli->ready_cb(cli->ready_cb_userdata);
		}
	}
	if (flag & (1 << CB_CMD_CREATE_SURFACE_ACK_SHIFT)) {
		id = cb_client_parse_surface_id(buf);
		cli->s.surface_id = id;
		client_debug(cli, "received surface created result %016X, %p",
			     id, cli->surface_created_cb);
		if (cli->surface_created_cb) {
			if (!id) {
				client_err(cli, "failed to create surface.");
				cli->surface_created_cb(false,
					cli->surface_created_cb_userdata, 0UL);
				return;
			}
			cli->surface_created_cb(true,
				cli->surface_created_cb_userdata, id);
		}
	}
	if (flag & (1 << CB_CMD_CREATE_VIEW_ACK_SHIFT)) {
		id = cb_client_parse_view_id(buf);
		cli->v.view_id = id;
		client_debug(cli, "received view created result %016X, %p",
			     id, cli->view_created_cb);
		if (cli->view_created_cb) {
			if (!id) {
				client_err(cli, "failed to create view.");
				cli->view_created_cb(false,
					cli->view_created_cb_userdata, 0UL);
				return;
			}
			cli->view_created_cb(true,
				cli->view_created_cb_userdata, id);
		}
	}
	if (flag & (1 << CB_CMD_CREATE_BO_ACK_SHIFT)) {
		id = cb_client_parse_bo_id(buf);
		client_debug(cli, "received bo created result %016X, %p",
			     id, cli->bo_created_cb);
		if (cli->bo_created_cb) {
			if (!id) {
				client_err(cli, "failed to create bo.");
				cli->bo_created_cb(false,
					cli->bo_created_cb_userdata, 0UL);
				return;
			}
			cli->bo_created_cb(true,
				cli->bo_created_cb_userdata, id);
		}
	}
	if (flag & (1 << CB_CMD_COMMIT_ACK_SHIFT)) {
		id = cb_client_parse_commit_ack_cmd(buf, &surface_id);
		client_debug(cli, "received commit ack %016X, %p",
			     id, cli->bo_commited_cb);
		if (cli->bo_commited_cb) {
			if (id == (u64)(-1)) {
				client_err(cli, "failed to commit bo.");
				cli->bo_commited_cb(false,
					cli->bo_commited_cb_userdata, (u64)-1,
					surface_id);
				return;
			}
			cli->bo_commited_cb(true,
				cli->bo_commited_cb_userdata, id, surface_id);
		}
	}
	if (flag & (1 << CB_CMD_BO_FLIPPED_SHIFT)) {
		id = cb_client_parse_bo_flipped_cmd(buf, &surface_id);
		client_debug(cli, "received bo flipped event %016X, %p",
			     id, cli->bo_flipped_cb);
		if (cli->bo_flipped_cb) {
			if (id == (u64)(-1)) {
				client_err(cli, "Unknown bo flipped.");
				return;
			}
			cli->bo_flipped_cb(cli->bo_flipped_cb_userdata, id,
					   surface_id);
		}
	}
	if (flag & (1 << CB_CMD_BO_COMPLETE_SHIFT)) {
		id = cb_client_parse_bo_complete_cmd(buf, &surface_id);
		client_debug(cli, "received bo completed event %016X, %p",
			     id, cli->bo_completed_cb);
		if (cli->bo_completed_cb) {
			if (id == (u64)(-1)) {
				client_err(cli, "Unknown bo completed.");
				return;
			}
			cli->bo_completed_cb(cli->bo_completed_cb_userdata,
					     id, surface_id);
		}
	}
	if (flag & (1 << CB_CMD_DESTROY_ACK_SHIFT)) {
		client_debug(cli, "received destroy result %p",
			     cli->destroyed_cb);
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

		id = cb_client_parse_hpd_cmd(buf, &conn_info);
		if (id) {
			client_err(cli, "unknown hotplug message.");
			return;
		}
		client_debug(cli, "received HPD event %016X, %p",
			     id, cli->hpd_cb);
		if (cli->base.count_displays == 0) {
			client_err(cli, "receive hotplug message too early.");
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
		client_debug(cli, "received mc commited ack %016X, %p",
			     id, cli->mc_commited_cb);
		if (cli->mc_commited_cb) {
			if (id) {
				cli->mc_commited_cb(false,
					cli->mc_commited_cb_userdata, id);
				return;
			}
			cli->mc_commited_cb(true,
				cli->mc_commited_cb_userdata, id);
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
		client_err(cli, "failed to recv from server (%s).",
			   strerror(-ret));
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
		stop(&cli->base);
		return ret;
	} else if (ret == 0) {
		client_err(cli, "connection lost.");
		if (cli->connection_lost_cb)
			cli->connection_lost_cb(
					cli->connection_lost_cb_userdata);
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
		for (i = 0; i < cli->ipc_fds.count; i++) {
			printf("fds[%d]: %d\n", i, cli->ipc_fds.fds[i]);
		}
		*/
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

	cli->debug_level = CB_LOG_NOTICE;

	memset(cli->pid_name, 0, 9);
	sprintf(cli->pid_name, "%08X", getpid());

	memset(name, 0, 64);
	snprintf(name, 64, "%s/%s-%d", LOG_SERVER_NAME_PREFIX,
		 LOG_SERVER_SOCK_NAME, seat);
	cli->log_handle = cb_client_log_init(name);
	if (!cli->log_handle) {
		free(cli);
		return NULL;
	}

	client_notice(cli, "Creating cube client %s ...", cli->pid_name);

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
		client_err(cli, "failed to connect to cube server %s. (%s)",
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
	cli->base.set_input_msg_cb = set_input_msg_cb;
	cli->base.set_raw_input_evts_cb = set_raw_input_evts_cb;
	cli->base.set_raw_touch_evts_cb = set_raw_touch_evts_cb;
	cli->base.set_ready_cb = set_ready_cb;
	cli->base.set_destroyed_cb = set_destroyed_cb;
	cli->base.set_raw_input_en = set_raw_input_en;
	cli->base.send_get_edid = send_get_edid;
	cli->base.set_get_edid_cb = set_get_edid_cb;
	cli->base.send_set_kbd_led_st = send_set_kbd_led_st;
	cli->base.send_get_kbd_led_st = send_get_kbd_led_st;
	cli->base.set_kbd_led_st_cb = set_kbd_led_st_cb;
	cli->base.set_view_focus_chg_cb = set_view_focus_chg_cb;
	cli->base.set_connection_lost_cb = set_connection_lost_cb;
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
	cli->base.af_commit_bo = af_commit_bo;
	cli->base.alloc_af_commit_info_buffer = alloc_af_commit_info_buffer;
	cli->base.get_af_commit_info = get_af_commit_info;
	cli->base.commit_bo = commit_bo;
	cli->base.set_commit_bo_cb = set_commit_bo_cb;
	cli->base.set_bo_flipped_cb = set_bo_flipped_cb;
	cli->base.set_bo_completed_cb = set_bo_completed_cb;
	cli->base.commit_mc = commit_mc;
	cli->base.set_commit_mc_cb = set_commit_mc_cb;
	cli->base.set_hpd_cb = set_hpd_cb;
	cli->base.rm_handler = rm_handler;
	cli->base.add_fd_handler = add_fd_handler;
	cli->base.add_signal_handler = add_signal_handler;
	cli->base.add_timer_handler = add_timer_handler;
	cli->base.timer_update = timer_update;
	cli->base.add_idle_task = add_idle_task;

	client_notice(cli, "cube client %s is created.", cli->pid_name);
	return &cli->base;
err:
	client_err(cli, "failed to create cube client.");
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
			      s32 fds[4], /* output */
			      bool composed)
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
	buffer->info.composed = composed;

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
				  u32 offsets[4],
				  bool composed)
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
	buffer->info.composed = composed;
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

	ret = cb_shm_init(&buffer->shm, size);
	if (ret < 0) {
		fprintf(stderr, "failed to create shm buffer.\n");
		goto err;
	}
	*count_fds = 1;
	buffer->count_fds = 1;
	fds[0] = buffer->info.fd[0] = buffer->shm.fd;

	sizes[0] = (u32)size;
	maps[0] = buffer->shm.map;

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

	cb_shm_release(&buffer->shm);
	free(buffer);
}

