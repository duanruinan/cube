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
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
/*
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
*/
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_ipc.h>
#include <cube_shm.h>
#include <cube_event.h>
#include <cube_protocal.h>

#ifndef CB_IPC_BUF_MAX_LEN
#define CB_IPC_BUF_MAX_LEN (1 << 19)
#endif

struct cb_client;

struct client_buffer {
	struct cb_buffer_info info;
	u64 remote_id;
};

struct client_window {
	struct cb_client *client;
	u32 width, height;

	struct client_buffer *buffers;
	s32 count_buffers;

	struct cb_surface_info s;
	struct cb_view_info v;
	struct cb_buffer_info b;
	struct cb_commit_info c;
};

struct cb_client {
	struct cb_event_loop *loop;
	u64 link_id;

	u8 ipc_buf[CB_IPC_BUF_MAX_LEN];
	size_t ipc_sz, byts_to_rd;
	u8 *cursor;
	struct cb_fds ipc_fds;

	s32 sock;
	struct cb_event_source *sock_source;
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

	u8 *commit_tx_cmd_t;
	u8 *commit_tx_cmd;
	u32 commit_tx_len;

	u8 *terminate_tx_cmd_t;
	u8 *terminate_tx_cmd;
	u32 terminate_tx_len;
};

static char short_options[] = "dsw:h:x:y:o:";

static struct option options[] = {
	{"dma-buf", 0, NULL, 'd'},
	{"shm", 0, NULL, 's'},
	{"width", 1, NULL, 'w'},
	{"height", 1, NULL, 'h'},
	{"left", 1, NULL, 'x'},
	{"top", 1, NULL, 'y'},
	{"output", 1, NULL, 'o'},
};

void usage(void)
{
	printf("clover_simple_client [options]\n");
	printf("\toptions:\n");
	printf("\t\t-d, --dma-buf\n");
	printf("\t\t\tRun client with DMA-BUF\n");
	printf("\t\t-s, --shm\n");
	printf("\t\t\tRun client with share memory\n");
	printf("\t\t-x, --left=x position\n");
	printf("\t\t-y, --top=y position\n");
	printf("\t\t-w, --width=window width\n");
	printf("\t\t-h, --height=window height\n");
	printf("\t\t-o, --output=primary output\n");
}

static void cb_client_destroy(struct cb_client *client)
{
	if (!client)
		return;

	if (client->loop) {
		if (client->sock_source)
			cb_event_source_remove(client->sock_source);

		if (client->sock > 0)
			close(client->sock);

		cb_event_loop_destroy(client->loop);
	}

	free(client);
}

static void cb_client_run(struct cb_client *client)
{
	if (!client)
		return;

	while (!client->exit) {
		cb_event_loop_dispatch(client->loop, -1);	
	}
}

static void cb_client_stop(struct cb_client *client)
{
	if (!client)
		return;

	client->exit = true;
}

static void protocal_proc(struct cb_client *client, u8 *buf, size_t size,
			  struct cb_fds *fds)
{
	s32 i;
	struct cb_tlv *tlv;
	u32 flag;
	enum cb_tag tag;
	u64 id;

	flag = *((u32 *)buf);
	tlv = (struct cb_tlv *)(buf + sizeof(flag));

	tag = tlv->tag;
	printf("protocal tag: 0x%08X, length: %u\n", tag, tlv->length);

	switch (tag) {
	case CB_TAG_RAW_INPUT:
		break;
	case CB_TAG_WIN:
		if (flag & (1 << CB_CMD_LINK_ID_ACK_SHIFT)) {
			id = cb_client_parse_link_id(buf);
			if (id == 0) {
				fprintf(stderr, "failed to create client\n");
				return;
			}
			client->link_id = id;
			printf("link_id: 0x%08lX\n", client->link_id);
			/* create surface */
		} else if (flag & (1 << CB_CMD_CREATE_SURFACE_ACK_SHIFT)) {
			id = cb_client_parse_surface_id(buf);
			if (id == 0) {
				fprintf(stderr, "failed to create surface\n");
				return;
			}
			/* create view */
		} else if (flag & (1 << CB_CMD_CREATE_VIEW_ACK_SHIFT)) {
			id = cb_client_parse_view_id(buf);
			if (id == 0) {
				fprintf(stderr, "failed to create view\n");
				return;
			}
			/* create buffers */
		} else if (flag & (1 << CB_CMD_CREATE_BO_ACK_SHIFT)) {
			id = cb_client_parse_bo_id(buf);
			if (id == 0) {
				fprintf(stderr, "failed to create buffer\n");
				return;
			}
			/* ready to draw ? */
		} else if (flag & (1 << CB_CMD_COMMIT_ACK_SHIFT)) {
			id = cb_client_parse_commit_ack_cmd(buf);
		} else if (flag & (1 << CB_CMD_BO_FLIPPED_SHIFT)) {
			id = cb_client_parse_bo_flipped_cmd(buf);
		} else if (flag & (1 << CB_CMD_BO_COMPLETE_SHIFT)) {
			id = cb_client_parse_bo_complete_cmd(buf);
		} else if (flag & (1 << CB_CMD_SHELL_SHIFT)) {
			printf("receive shell event\n");
		} else if (flag & (1 << CB_CMD_DESTROY_ACK_SHIFT)) {
			printf("receive destroy ack\n");
			cb_server_parse_destroy_bo_cmd(buf);
		} else if (flag & (1 << CB_CMD_HPD_SHIFT)) {
			printf("receive plug in/out\n");
		} else {
			fprintf(stderr, "unknown command 0x%08X\n", flag);
			return;
		}
		break;
	default:
		fprintf(stderr, "illegal top level tag. 0x%08X\n", tag);
		return;
	}
}

static s32 sock_cb(s32 fd, u32 mask, void *data)
{
	struct cb_client *client = data;
	size_t byts_rd;
	s32 ret;
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
		fprintf(stderr, "failed to recv from server (%s).\n",
			strerror(-ret));
		sleep(10);
		return ret;
	} else if (ret == 0) {
		cb_client_stop(client);
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
		protocal_proc(client, &client->ipc_buf[0] + sizeof(size_t),
			      client->ipc_sz, &client->ipc_fds);
	}

	return 0;
}

static struct cb_client *cb_client_create(char *server_name)
{
	struct cb_client *client;
	s32 ret;

	client = calloc(1, sizeof(*client));
	if (!client)
		goto err;

	client->loop = cb_event_loop_create();
	if (!client->loop)
		goto err;

	client->sock = cb_socket_cloexec();
	if (!client->sock)
		goto err;

	ret = cb_socket_connect(client->sock, server_name);
	if (ret < 0) {
		fprintf(stderr, "failed to connect to cube server %s. (%s)\n",
			server_name, strerror(-ret));
		goto err;
	}

	client->sock_source = cb_event_loop_add_fd(client->loop,
						   client->sock,
						   CB_EVT_READABLE,
						   sock_cb,
						   client);
	if (!client->sock_source)
		goto err;

	client->byts_to_rd = sizeof(size_t);
	client->cursor = client->ipc_buf;
	client->ipc_sz = 0;

	return client;

err:
	if (client)
		cb_client_destroy(client);
	return NULL;
}

s32 main(s32 argc, char **argv)
{
	struct cb_client *client;
	s32 ch;
	cpu_set_t set;
	bool use_dma_buf = true;
	u32 width, height;
	s32 x, y;
	s32 pipe;

	CPU_ZERO(&set);
	CPU_SET(0, &set);
	sched_setaffinity(4, sizeof(set), &set);

	while ((ch = getopt_long(argc, argv, short_options,
				 options, NULL)) != -1) {
		switch (ch) {
		case 'd':
			use_dma_buf = true;
			break;
		case 's':
			use_dma_buf = false;
			break;
		case 'w':
			width = atoi(optarg);
			break;
		case 'h':
			height = atoi(optarg);
			break;
		case 'x':
			x = atoi(optarg);
			break;
		case 'y':
			y = atoi(optarg);
			break;
		case 'o':
			pipe = atoi(optarg);
			break;
		default:
			usage();
			return -1;
		}
	}

	client = cb_client_create("/tmp/cube_server-0");

	cb_client_run(client);

	cb_client_destroy(client);

	return 0;
}
