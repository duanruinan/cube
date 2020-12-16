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
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/param.h>
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

#define MAIN_ARG_MAX_NR 16
#define MAIN_ARG_MAX_LEN 128

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

static char short_options[] = "bhs:d:t:a:";

static struct option long_options[] = {
	{"background", 0, NULL, 'b'},
	{"help", 0, NULL, 'h'},
	{"seat", 1, NULL, 's'},
	{"device", 1, NULL, 'd'},
	{"touch-pipe", 1, NULL, 't'},
	{"mc-accel", 1, NULL, 'a'},
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
	printf("\t\t-t, --touch-pipe=pipe number, touch screen index.\n");
	printf("\t\t-t, --mc-accel=mouse accelerator, default 1.0.\n");
}

struct child_process {
	s32 pid;
	s32 argc;
	char argv[MAIN_ARG_MAX_NR][MAIN_ARG_MAX_LEN];
	s32 start_delay_ms;
	struct list_head link;
};

static s32 find_server_pid(struct list_head *processes)
{
	struct child_process *p;

	list_for_each_entry(p, processes, link) {
		if (strstr(p->argv[0], "cube_server"))
			return p->pid;
	}

	return 0;
}

static void start_child_process(struct list_head *processes, s32 argc,
				char *argv[], s32 ms)
{
	struct child_process *p;
	s32 pid, i;

	p = calloc(1, sizeof(*p));
	p->start_delay_ms = ms;
	p->argc = argc;

	for (i = 0; i < argc; i++)
		strcpy(p->argv[i], argv[i]);

	pid = fork();
	if (pid == 0) {
		chdir("/");
		umask(0);
		for (i = 0; i < NOFILE; i++)
			close(i);
		execv(p->argv[0], argv);
	} else if (pid > 0) {
		p->pid = pid;
		list_add_tail(&p->link, processes);
		usleep(ms * 1000);
		printf("Process %s started with pid %d.\n", p->argv[0], p->pid);
	} else {
		fprintf(stderr, "failed to create process %s\n", p->argv[0]);
	}
}

static s32 child_processes_proc(s32 signal_number, void *data)
{
	struct list_head *processes = data;
	s32 state, pid, server_pid;
	struct child_process *p, *n;
	char *av[MAIN_ARG_MAX_NR] = {NULL};
	s32 i;

	while ((pid = waitpid(-1, &state, WNOHANG)) > 0) {
		list_for_each_entry_safe(p, n, processes, link) {
			if (pid == p->pid) {
				list_del(&p->link);
				fprintf(stderr, "Process %s hangup.\n",
					p->argv[0]);
				if (strstr(p->argv[0], "cube_log")) {
					server_pid = find_server_pid(processes);
					printf("Kill cube server. %d\n",
						server_pid);
					kill(server_pid, SIGKILL);
				}
				for (i = 0; i < p->argc; i++) {
					av[i] = p->argv[i];
					printf("av[%d]: %s\n", i, av[i]);
				}
				av[i] = NULL;
				start_child_process(processes, p->argc, av, 10);
				free(p);
			}
		}
	}

	return 0;
}

static void run_background(s32 log_argc, char *log_argv[],
			   s32 server_argc, char *server_argv[])
{
	s32 pid;
	struct list_head child_processes;
	struct cb_event_loop *loop;

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

	INIT_LIST_HEAD(&child_processes);
	start_child_process(&child_processes, log_argc, log_argv, 10);
	start_child_process(&child_processes, server_argc, server_argv, 10);

	loop = cb_event_loop_create();
	(void)cb_event_loop_add_signal(loop, SIGCHLD,
				       child_processes_proc,
				       &child_processes);
	while (1) {
		cb_event_loop_dispatch(loop, -1);
	}
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

static void head_changed_cb(struct cb_listener *listener, void *data)
{
	struct cb_display *disp = container_of(listener, struct cb_display,
						head_status_l);
	struct compositor *c = disp->server->c;

	disp->connected = c->head_connected(c, disp->pipe);
	serv_notice("***** head: %s connected: %d, monitor: %s",
		    c->get_connector_name(c, disp->pipe),
		    disp->connected,
		    c->get_monitor_name(c, disp->pipe));

	c->dispatch_hotplug_event(c, disp->pipe);
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
	serv_notice("Compositor ready");

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
		serv_notice("***** init head: %s connected: %d, monitor: %s",
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
}

static s32 comp_destroy_delayed_proc(void *data)
{
	struct cb_server *server = data;

	serv_notice("delay destroy");
	if (cb_server_prepare_destroy(server) < 0) {
		cb_event_source_timer_update(
			server->comp_destroy_timer, 3, 0);
	} else {
		serv_notice("exit loop");
		server->exit = true;
	}
	return 0;
}

static struct cb_server *cb_server_create(s32 seat, char *dev, s32 touch_pipe,
					  float mc_accel)
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
	server->c = compositor_create(dev, server->loop, pipe_cfg, pipe_nr,
				      touch_pipe, mc_accel);
	if (!server->c)
		goto err;

	server->compositor_ready_l.notify = compositor_ready_cb;
	server->c->register_ready_cb(server->c, &server->compositor_ready_l);

	server->comp_destroy_timer = cb_event_loop_add_timer(server->loop,
						comp_destroy_delayed_proc,
						server);
	if (!server->comp_destroy_timer)
		goto err;

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
	s32 touch_pipe = 0;
	float mc_accel = 1.0f;
	char log_argv0[MAIN_ARG_MAX_LEN];
	char log_argv2[MAIN_ARG_MAX_LEN];
	char *log_argv[MAIN_ARG_MAX_NR] = {NULL};
	char *server_argv[MAIN_ARG_MAX_NR] = {NULL};
	char *p;
	char touch_pipe_s[MAIN_ARG_MAX_LEN];
	char mc_accel_s[MAIN_ARG_MAX_LEN];

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
		case 't':
			touch_pipe = atoi(optarg);
			break;
		case 'a':
			mc_accel = atof(optarg);
			break;
		default:
			usage();
			return -1;
		}
	}

	if (run_as_background) {
		strcpy(log_argv0, argv[0]);
		p = strstr(log_argv0, "cube_server");
		*p = '\0';
		strcat(p, "cube_log");
		sprintf(log_argv2, "%d", seat);
		log_argv[0] = log_argv0;
		log_argv[1] = "-s";
		log_argv[2] = log_argv2;
		log_argv[3] = NULL;
		server_argv[0] = argv[0];
		server_argv[1] = "-s";
		server_argv[2] = log_argv2;
		server_argv[3] = "-d";
		server_argv[4] = device_name;
		memset(touch_pipe_s, 0, MAIN_ARG_MAX_LEN);
		sprintf(touch_pipe_s, "%d", touch_pipe);
		server_argv[5] = "-t";
		server_argv[6] = touch_pipe_s;
		server_argv[7] = "-a";
		memset(mc_accel_s, 0, MAIN_ARG_MAX_LEN);
		sprintf(mc_accel_s, "%1.1f", mc_accel);
		server_argv[8] = mc_accel_s;
		server_argv[9] = NULL;
		run_background(3, log_argv, 9, server_argv);
	}

	server = cb_server_create(seat, device_name, touch_pipe, mc_accel);
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

