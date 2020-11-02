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
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/utsname.h>
#include <cube_utils.h>
#include <cube_ipc.h>
#include <cube_log.h>
#include <cube_event.h>
#include <cube_protocal.h>
#include <cube_client_agent.h>
#include <cube_compositor.h>

static enum cb_log_level serv_dbg = CB_LOG_DEBUG;

#define serv_debug(fmt, ...) do { \
	if (serv_dbg >= CB_LOG_DEBUG) { \
		cb_tlog("[SERV][DEBUG ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define serv_info(fmt, ...) do { \
	if (serv_dbg >= CB_LOG_INFO) { \
		cb_tlog("[SERV][INFO  ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define serv_notice(fmt, ...) do { \
	if (serv_dbg >= CB_LOG_NOTICE) { \
		cb_tlog("[SERV][NOTICE] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define serv_warn(fmt, ...) do { \
	cb_tlog("[SERV][WARN  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define serv_err(fmt, ...) do { \
	cb_tlog("[SERV][ERROR ] " fmt, ##__VA_ARGS__); \
} while (0);

static char short_options[] = "bhs:d:";

static struct option long_options[] = {
	{"background", 0, NULL, 'b'},
	{"help", 0, NULL, 'h'},
	{"seat", 1, NULL, 's'},
	{"device", 1, NULL, 'd'},
	{NULL, 0, NULL, 0},
};

static struct pipeline pipe_cfg[2] = {
	{
		.head_index = 0,
		.output_index = 0,
		.primary_plane_index = 0,
		.cursor_plane_index = 1,
	},
	{
		.head_index = 1,
		.output_index = 1,
		.primary_plane_index = 4,
		.cursor_plane_index = 5,
	}
};

static s32 pipe_nr = 2;

struct cb_display {
	s32 pipe;
	bool connected;
	struct cb_listener head_status_l;
	struct cb_server *server;
};

struct cb_server {
	struct cb_display **disp;
	s32 disp_nr;
	struct cb_listener compositor_ready_l;
	struct cb_event_loop *loop;
	struct cb_event_source *sock_source;
	struct cb_event_source *sig_int_source;
	struct cb_event_source *sig_tem_source;
	struct cb_event_source *sig_stp_source;
	struct cb_event_source *comp_destroy_timer;
#ifdef TEST_MC
	struct cb_event_source *mc_chg_timer;
	struct cb_event_source *mc_collect_timer;
	struct cb_event_source *test_hide_mc_timer;
	struct cb_event_source *set_cursor_idle_source;
	u32 mc_cnt;
	bool mc_pending;
	u8 *mc_bufs[2];
	s32 mc_cur;
	struct cb_listener mc_flipped_listener;
	bool mc_busy;
#endif
	s32 sock;
	s32 seat;
	bool compositor_ready;
	bool exit;
	struct compositor *c;

	u8 *linkid_created_ack_tx_cmd_t;
	u8 *linkid_created_ack_tx_cmd;
	u32 linkid_created_ack_tx_len;
};

static void usage(void)
{
	printf("Usage:\n");
	printf("cube_server [options]\n");
	printf("\toptions:\n");
	printf("\t\t-b, --background, run background.\n");
	printf("\t\t-h, --help, show this message.\n");
	printf("\t\t-s, --seat=ID, cube server's instance ID.\n");
	printf("\t\t-d, --device=/dev/dri/cardX, device name.\n");
}

#ifdef TEST_MC
static void set_cursor_idle_proc(void *data)
{
	struct cb_server *server = data;
	s32 ret;

	if (server->mc_chg_timer)
		cb_event_source_timer_update(server->mc_chg_timer, 0, 0);
	ret = server->c->set_mouse_cursor(server->c,
					  server->mc_bufs[server->mc_cur],
					  16, 16, 64, 0, 0, false);
	if (ret) {
		printf("ret = %d\n", ret);
		if (server->mc_chg_timer)
			cb_event_source_timer_update(server->mc_chg_timer,
				     8, 0);
	} else {
		server->mc_cnt++;
		server->mc_cur = 1 - server->mc_cur;
	}
	server->set_cursor_idle_source = NULL;
}

static void start_set_cursor_idle_task(struct cb_server *server)
{
	server->set_cursor_idle_source = cb_event_loop_add_idle(server->loop,
					set_cursor_idle_proc,
					server);
}
#endif

static void run_background(void)
{
	s32 pid;

	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	pid = fork();
	if (pid > 0)
		exit(0);
	else if (pid < 0)
		exit(1);

	setsid();

	pid = fork();
	if (pid > 0) {
		exit(0);
	} else if (pid < 0) {
		exit(1);
	}

	chdir("/");

	umask(0);

	signal(SIGCHLD, SIG_IGN);
}

static void cb_server_stop(struct cb_server *server)
{
	if (!server)
		return;

	server->exit = true;
}

static void cb_server_run(struct cb_server *server)
{
	if (!server)
		return;

	server->exit = false;

	while (!server->exit) {
		cb_event_loop_dispatch(server->loop, -1);
	}
}

static s32 signal_event_proc(s32 signal_number, void *data)
{
	struct cb_server *server = data;

	serv_notice("received signal: %d, exit.", signal_number);
	cb_server_stop(server);

	return 0;
}

static s32 cb_server_prepare_destroy(struct cb_server *server)
{
	if (!server)
		return 0;

	if (server->loop) {
#ifdef TEST_MC
		if (server->test_hide_mc_timer) {
			cb_event_source_remove(server->test_hide_mc_timer);
			server->test_hide_mc_timer = NULL;
		}
		if (server->mc_collect_timer) {
			cb_event_source_remove(server->mc_collect_timer);
			server->mc_collect_timer = NULL;
		}

		if (server->mc_chg_timer) {
			cb_event_source_remove(server->mc_chg_timer);
			server->mc_chg_timer = NULL;
		}
#endif

		if (server->c) {
			if (server->c) {
				if (server->c->destroy(server->c) < 0) {
					cb_event_source_timer_update(
						server->comp_destroy_timer,
						3, 0);
					return -EAGAIN;
				}
				server->c = NULL;
			}
		}

		if (server->comp_destroy_timer) {
			cb_event_source_remove(server->comp_destroy_timer);
			server->comp_destroy_timer = NULL;
		}
	}

	return 0;
}

static void cb_server_destroy(struct cb_server *server)
{
	s32 i;

	if (!server)
		return;

	if (server->loop) {
#ifdef TEST_MC
		free(server->mc_bufs[0]);
		free(server->mc_bufs[1]);
#endif

		if (server->sig_stp_source) {
			cb_event_source_remove(server->sig_stp_source);
			server->sig_stp_source = NULL;
		}

		if (server->sig_tem_source) {
			cb_event_source_remove(server->sig_tem_source);
			server->sig_tem_source = NULL;
		}

		if (server->sig_int_source) {
			cb_event_source_remove(server->sig_int_source);
			server->sig_int_source = NULL;
		}

		if (server->sock_source) {
			cb_event_source_remove(server->sock_source);
			server->sock_source = NULL;
		}

		if (server->sock) {
			close(server->sock);
			server->sock = 0;
		}

		if (server->disp) {
			for (i = 0; i < server->disp_nr; i++) {
				free(server->disp[i]);
			}
			free(server->disp);
		}

		cb_event_loop_destroy(server->loop);
		server->loop = NULL;
	}

	if (server->linkid_created_ack_tx_cmd_t)
		free(server->linkid_created_ack_tx_cmd_t);
	if (server->linkid_created_ack_tx_cmd)
		free(server->linkid_created_ack_tx_cmd);

	cb_log_fini();

	free(server);
}

static s32 server_sock_cb(s32 fd, u32 mask, void *data)
{
	struct cb_server *server = data;
	s32 sock, ret;
	size_t len;
	struct cb_client_agent *client;

	sock = cb_socket_accept(fd);
	if (sock <= 0) {
		serv_err("failed to accept. (%s)", strerror(errno));
		return -errno;
	}
	
	client = server->c->add_client(server->c, sock);
	if (!client)
		return -1;

	cb_dup_linkup_cmd(server->linkid_created_ack_tx_cmd,
			  server->linkid_created_ack_tx_cmd_t,
			  server->linkid_created_ack_tx_len,
			  (u64)client);

	len = server->linkid_created_ack_tx_len;
	/* printf("send length: %lu\n", len); */
	do {
		ret = cb_sendmsg(sock, (u8 *)&len, sizeof(size_t), NULL);
	} while (ret == -EAGAIN);

	/* printf("send link id: 0x%016lX\n", (u64)client); */
	do {
		ret = cb_sendmsg(sock, server->linkid_created_ack_tx_cmd,
				 server->linkid_created_ack_tx_len, NULL);
	} while (ret == -EAGAIN);

	return 0;
}

#ifdef TEST_MC
static void mc_flipped_cb(struct cb_listener *listener, void *data)
{
	struct cb_server *server = container_of(listener, struct cb_server,
						mc_flipped_listener);
	start_set_cursor_idle_task(server);
}
#endif

static void head_changed_cb(struct cb_listener *listener, void *data)
{
	struct cb_display *disp = container_of(listener, struct cb_display,
						head_status_l);
	struct compositor *c = disp->server->c;

	disp->connected = c->head_connected(c, disp->pipe);
	printf("***** head: %s connected: %d, monitor: %s\n",
			c->get_connector_name(c, disp->pipe),
			disp->connected,
			c->get_monitor_name(c, disp->pipe));

	c->dispatch_hotplug_event(c, disp->pipe);

	if (disp->connected) {
#ifdef TEST_MC
		printf("head change Set cursor\n");
		start_set_cursor_idle_task(disp->server);
#endif
	} else {
//		cb_event_source_timer_update(disp->server->mc_chg_timer,
//					     0, 0);
	}
}

static void compositor_ready_cb(struct cb_listener *listener, void *data)
{
	struct cb_server *server = container_of(listener,
						struct cb_server,
						compositor_ready_l);
	struct compositor *c = server->c;
	s32 i;
	u32 size;

	list_del(&listener->link);
	server->compositor_ready = true;
	printf("Compositor ready\n");

	server->disp = calloc(server->disp_nr, sizeof(struct cb_display *));
	if (!server->disp)
		return;
	for (i = 0; i < server->disp_nr; i++) {
		server->disp[i] = calloc(1, sizeof(struct cb_display));
		if (!server->disp[i])
			return;
		server->disp[i]->server = server;
		server->disp[i]->pipe = pipe_cfg[i].head_index;
		server->disp[i]->head_status_l.notify = head_changed_cb;
		server->disp[i]->connected = server->c->head_connected(
				server->c, i);
		server->c->register_head_status_cb(
					server->c,
					pipe_cfg[i].head_index,
					&server->disp[i]->head_status_l);
		printf("***** init head: %s connected: %d, monitor: %s\n",
			c->get_connector_name(c, i),
			server->disp[i]->connected,
			c->get_monitor_name(c, i));
	}

	/* begin listen */
	server->sock_source = cb_event_loop_add_fd(server->loop, server->sock,
						   CB_EVT_READABLE,
						   server_sock_cb,
						   server);

	server->linkid_created_ack_tx_cmd_t
		= cb_server_create_linkup_cmd(0, &size);
	server->linkid_created_ack_tx_cmd = malloc(size);
	server->linkid_created_ack_tx_len = size;

#ifdef TEST_MC
	server->mc_flipped_listener.notify = mc_flipped_cb;
	server->c->set_mouse_updated_notify(server->c,
					    &server->mc_flipped_listener);

	printf("Set cursor\n");
//	start_set_cursor_idle_task(server);
#endif
}

#ifdef TEST_MC
static s32 mc_collect_proc(void *data)
{
	struct cb_server *server = data;

	printf("-----------mc update cnt: %u---------------\n", server->mc_cnt);
	cb_event_source_timer_update(server->mc_collect_timer, 1000, 0);
	server->mc_cnt = 0;

	return 0;
}
#endif

static s32 comp_destroy_delayed_proc(void *data)
{
	struct cb_server *server = data;

	printf("delay destroy\n");
	if (cb_server_prepare_destroy(server) < 0) {
		cb_event_source_timer_update(
			server->comp_destroy_timer, 3, 0);
	} else {
		printf("exit loop\n");
		server->exit = true;
	}
	return 0;
}

#ifdef TEST_MC
static s32 test_hide_mc_proc(void *data)
{
	struct cb_server *server = data;
	static s32 cnt = 1;

	if (cnt == 1) {
		cnt--;
		/* hide */
		printf("hide cursor !\n");
		server->c->hide_mouse_cursor(server->c);
		cb_event_source_timer_update(server->test_hide_mc_timer,
					5000, 0);
	} else {
		/* show */
		printf("show cursor !\n");
		server->c->show_mouse_cursor(server->c);
	}

	return 0;
}

static s32 change_mc_delay_proc(void *data)
{
	struct cb_server *server = data;

	start_set_cursor_idle_task(server);

	return 0;
}
#endif

static struct cb_server *cb_server_create(s32 seat, char *dev)
{
	struct cb_server *server;
	char name[64];
	struct utsname usys;

	server = calloc(1, sizeof(*server));
	if (!server)
		goto err;

	memset(name, 0, 64);
	snprintf(name, 64, "%s/%s-%d", LOG_SERVER_NAME_PREFIX,
		 LOG_SERVER_SOCK_NAME, server->seat);

	if (cb_log_init(name) < 0)
		goto err;

	uname(&usys);
	serv_notice("OS: %s, Release: %s, Version: %s, Machine: %s",
		    usys.sysname, usys.release, usys.version, usys.machine);

	server->seat = seat;

	server->loop = cb_event_loop_create();
	if (!server->loop)
		goto err;

	server->sock = cb_socket_cloexec();
	if (!server->sock)
		goto err;

	memset(name, 0, 64);
	snprintf(name, 64, "%s/%s-%d", CB_SERVER_NAME_PREFIX,
		 SERVER_NAME, server->seat);

	unlink(name);
	cb_socket_bind_listen(server->sock, name);

	server->sig_int_source = cb_event_loop_add_signal(server->loop, SIGINT,
							  signal_event_proc,
							  server);
	if (!server->sig_int_source)
		goto err;

	server->sig_tem_source = cb_event_loop_add_signal(server->loop, SIGTERM,
							  signal_event_proc,
							  server);
	if (!server->sig_tem_source)
		goto err;

	server->sig_stp_source = cb_event_loop_add_signal(server->loop, SIGSTOP,
							  signal_event_proc,
							  server);
	if (!server->sig_stp_source)
		goto err;

	server->disp_nr = pipe_nr;
	server->c = compositor_create(dev, server->loop, pipe_cfg, pipe_nr);
	if (!server->c)
		goto err;

	server->compositor_ready_l.notify = compositor_ready_cb;
	server->c->register_ready_cb(server->c, &server->compositor_ready_l);

#ifdef TEST_MC
	server->mc_chg_timer = cb_event_loop_add_timer(server->loop,
						       change_mc_delay_proc,
						       server);
	if (!server->mc_chg_timer)
		goto err;

	server->mc_collect_timer = cb_event_loop_add_timer(server->loop,
						       mc_collect_proc,
						       server);
	if (!server->mc_collect_timer)
		goto err;
#endif

	server->comp_destroy_timer = cb_event_loop_add_timer(server->loop,
						comp_destroy_delayed_proc,
						server);
	if (!server->comp_destroy_timer)
		goto err;

#ifdef TEST_MC
	server->mc_cnt = 0;
	cb_event_source_timer_update(server->mc_collect_timer, 1000, 0);

	server->test_hide_mc_timer = cb_event_loop_add_timer(server->loop,
						test_hide_mc_proc,
						server);
	if (!server->test_hide_mc_timer)
		goto err;
	cb_event_source_timer_update(server->test_hide_mc_timer, 10000, 0);

	server->mc_cur = 0;
	server->mc_bufs[0] = malloc(16*16*4);
	{
		s32 i;
		u32 *pixel = (u32 *)(server->mc_bufs[0]);
		for (i = 0; i < 16 * 16; i++) {
			pixel[i] = 0x80FF0000;
		}
	}
	server->mc_bufs[1] = malloc(16*16*4);
	{
		s32 i;
		u32 *pixel = (u32 *)(server->mc_bufs[1]);
		for (i = 0; i < 16 * 16; i++) {
			pixel[i] = 0x800000FF;
		}
	}
#endif

	return server;

err:
	cb_server_destroy(server);
	return NULL;
}

static char device_name[64] = "/dev/dri/card0";

s32 main(s32 argc, char **argv)
{
	s32 ch;
	bool run_as_background = false;
	struct cb_server *server;
	s32 seat = 0;

	while ((ch = getopt_long(argc, argv, short_options,
				 long_options, NULL)) != -1) {
		switch (ch) {
		case 'b':
			run_as_background = true;
			break;
		case 'h':
			usage();
			return 0;
		case 's':
			seat = atoi(optarg);
			break;
		case 'd':
			strcpy(device_name, optarg);
			break;
		default:
			usage();
			return -1;
		}
	}

	if (run_as_background)
		run_background();

	server = cb_server_create(seat, device_name);
	if (!server)
		goto err;

	serv_notice("Run cube server ...");
	cb_server_run(server);
err:
	serv_notice("Cube server stopped.");
	server->exit = false;
	while (cb_server_prepare_destroy(server) < 0) {
		cb_server_run(server);
	}
	cb_server_destroy(server);

	return 0;
}

