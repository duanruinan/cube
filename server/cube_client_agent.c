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
#include <stdint.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_event.h>
#include <cube_client_agent.h>

static enum cb_log_level clia_dbg = CB_LOG_DEBUG;

#define clia_debug(fmt, ...) do { \
	if (clia_dbg >= CB_LOG_DEBUG) { \
		cb_tlog("[CLIA][DEBUG ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define clia_info(fmt, ...) do { \
	if (clia_dbg >= CB_LOG_INFO) { \
		cb_tlog("[CLIA][INFO  ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define clia_notice(fmt, ...) do { \
	if (clia_dbg >= CB_LOG_NOTICE) { \
		cb_tlog("[CLIA][NOTICE] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define clia_warn(fmt, ...) do { \
	cb_tlog("[CLIA][WARN  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define clia_err(fmt, ...) do { \
	cb_tlog("[CLIA][ERROR ] " fmt, ##__VA_ARGS__); \
} while (0);

void cb_client_destroy(struct cb_client_agent *client)
{
	if (!client)
		return;

	if (client->sock_source) {
		cb_event_source_remove(client->sock_source);
		client->sock_source = NULL;
	}

	if (client->sock) {
		close(client->sock);
		client->sock = 0;
	}

	free(client);
}

static s32 client_sock_cb(s32 fd, u32 mask, void *data)
{
	s32 ret, i;
	size_t byts_rd;
	struct cb_client_agent *client = data;
	s32 flag; /* 0: length not received, 1: length received. */

	client->ipc_fds.count = 0;
	if (client->cursor >= ((u8 *)(client->ipc_buf) + sizeof(size_t))) {
		flag = 1;
	} else {
		flag = 0;
	}

	do {
		/* bytes_to_rd is set when client create: sizeof(size_t) */
		/* printf("try to receive %lu\n", client->byts_to_rd); */
		ret = cb_recvmsg(client->sock, client->cursor,
				 client->byts_to_rd, &client->ipc_fds);
		/* printf("receive return %d\n", ret); */
	} while (ret == -EAGAIN);

	if (ret < 0) {
		clia_err("failed to recv client (%s).", strerror(-ret));
		return ret;
	} else if (ret == 0) {
		clia_warn("connection lost.");
		list_del(&client->link);
		cb_client_destroy(client);
		return 0;
	}

	client->cursor += ret;
	client->byts_to_rd -= ret;
	byts_rd = ret;

	if (!flag) {
		if (ret >= sizeof(size_t)) {
			/* received the length */
			flag = 1;
			memcpy(&client->byts_to_rd, client->ipc_buf,
			       sizeof(size_t));
			client->ipc_sz = client->byts_to_rd;
			/* printf("ipc size: %lu\n", client->ipc_sz); */
			/* printf("%p %p\n", client->cursor, client->ipc_buf);*/
			if ((byts_rd - sizeof(size_t)) > client->ipc_sz) {
				/* received more than one ipc message */
				client->byts_to_rd = 0;
			} else {
				client->byts_to_rd -= (byts_rd -sizeof(size_t));
			}
		}
	}

	if (!client->byts_to_rd) {
		client->cursor = (u8 *)client->ipc_buf;
		client->byts_to_rd = sizeof(size_t);
		/* printf("complete.\n"); */
		/* TODO &client->ipc_buf[0] + sizeof(size_t), ipc_sz */
		printf("proc ipc message: %p, %lu\n",
			&client->ipc_buf[0] + sizeof(size_t), client->ipc_sz);
		printf("ipc received %d fds.\n", client->ipc_fds.count);
		for (i = 0; i < client->ipc_fds.count; i++) {
			printf("fds[%d]: %d\n", i, client->ipc_fds.fds[i]);
		}
	}

	return 0;
}

struct cb_client_agent *cb_client_create(s32 sock, struct cb_event_loop *loop)
{
	struct cb_client_agent *client;

	if (sock <= 0)
		return NULL;

	client = calloc(1, sizeof(*client));
	if (!client)
		goto err;

	memset(client, 0 ,sizeof(*client));
	client->sock = sock;
	client->loop = loop;

	client->sock_source = cb_event_loop_add_fd(client->loop, client->sock,
						   CB_EVT_READABLE,
						   client_sock_cb,
						   client);
	if (!client->sock_source)
		goto err;

	client->byts_to_rd = sizeof(size_t);
	client->cursor = (u8 *)(client->ipc_buf);
	client->ipc_sz = 0;

	return client;

err:
	cb_client_destroy(client);
	return NULL;
}

