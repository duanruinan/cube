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
#ifndef CUBE_CLIENT_AGENT_H
#define CUBE_CLIENT_AGENT_H
#include <assert.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_ipc.h>
#include <cube_compositor.h>

struct cb_server;

#ifndef CB_IPC_BUF_MAX_LEN
#define CB_IPC_BUF_MAX_LEN (1 << 19)
#endif

struct cb_client_agent {
	u8 ipc_buf[CB_IPC_BUF_MAX_LEN];
	size_t ipc_sz, byts_to_rd;
	u8 *cursor;
	struct cb_fds ipc_fds;
	s32 sock;
	struct cb_event_source *sock_source;
	struct cb_event_source *destroy_idle_source;
	struct cb_event_loop *loop;
	struct list_head link;
	struct compositor *c;

	/* list of surfaces */
	struct list_head surfaces;

	/* list of buffers */
	struct list_head buffers;

	u64 capability;
	struct cb_listener mc_flipped_l;
	bool raw_input_en;

	u8 *surface_id_created_tx_cmd_t;
	u8 *surface_id_created_tx_cmd;
	u32 surface_id_created_tx_len;

	u8 *view_id_created_tx_cmd_t;
	u8 *view_id_created_tx_cmd;
	u32 view_id_created_tx_len;

	u8 *bo_id_created_tx_cmd_t;
	u8 *bo_id_created_tx_cmd;
	u32 bo_id_created_tx_len;

	u8 *bo_commit_ack_tx_cmd_t;
	u8 *bo_commit_ack_tx_cmd;
	u32 bo_commit_ack_tx_len;

	u8 *bo_flipped_tx_cmd_t;
	u8 *bo_flipped_tx_cmd;
	u32 bo_flipped_tx_len;

	u8 *bo_complete_tx_cmd_t;
	u8 *bo_complete_tx_cmd;
	u32 bo_complete_tx_len;

	u8 *hpd_tx_cmd_t;
	u8 *hpd_tx_cmd;
	u32 hpd_tx_len;

	u8 *mc_commit_ack_tx_cmd_t;
	u8 *mc_commit_ack_tx_cmd;
	u32 mc_commit_ack_tx_len;

	u8 *mc_flipped_tx_cmd_t;
	u8 *mc_flipped_tx_cmd;
	u32 mc_flipped_tx_len;

	u8 *shell_tx_cmd_t;
	u8 *shell_tx_cmd;
	u32 shell_tx_len;

	u8 *kbd_led_status_ack_cmd_t;
	u8 *kbd_led_status_ack_cmd;
	u32 kbd_led_status_ack_len;

	void (*send_surface_ack)(struct cb_client_agent *client, void *s);
	void (*send_view_ack)(struct cb_client_agent *client, void *v);
	void (*send_bo_create_ack)(struct cb_client_agent *client, void *bo);
	void (*send_bo_commit_ack)(struct cb_client_agent *client, u64 result);
	void (*send_bo_flipped)(struct cb_client_agent *client, void *bo);
	void (*send_bo_complete)(struct cb_client_agent *client, void *bo);
	void (*send_raw_input_evts)(struct cb_client_agent *client,
				    u8 *evts,
				    u32 count_evts);
	void (*send_hpd_evt)(struct cb_client_agent *client,
			     struct cb_connector_info *conn_info);
	void (*send_mc_commit_ack)(struct cb_client_agent *client, u64 result);
	void (*send_mc_flipped)(struct cb_client_agent *client, void *bo);
	void (*send_shell_cmd)(struct cb_client_agent *client,
			       struct cb_shell_info *s);
	void (*destroy_pending)(struct cb_client_agent *client);
};

void cb_client_agent_destroy(struct cb_client_agent *client);
struct cb_client_agent *cb_client_agent_create(s32 sock,
					       struct compositor *c,
					       struct cb_event_loop *loop);

#endif

