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

#include <cube_utils.h>
#include <cube_event.h>
#include <cube_ipc.h>

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
	struct cb_event_loop *loop;
	struct list_head link;
};

void cb_client_destroy(struct cb_client_agent *client);
struct cb_client_agent *cb_client_create(s32 sock, struct cb_event_loop *loop);

#endif

