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
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>
#include <cube_utils.h>
#include <cube_ipc.h>
#include <cube_network.h>
#include <cube_log.h>
#include <cube_event.h>
#include <cube_protocal.h>

static char short_options[] = "bhs:";

#define MAX_LOG_SIZE (1 << 20)

static struct option long_options[] = {
	{"background", 0, NULL, 'b'},
	{"help", 0, NULL, 'h'},
	{"seat", 1, NULL, 's'},
	{NULL, 0, NULL, 0},
};

#ifndef LOG_BUF_MAX_LEN
#define LOG_BUF_MAX_LEN 4096
#endif

struct log_server;

struct log_client {
	char log_buf[LOG_BUF_MAX_LEN];
	size_t log_sz, byts_to_rd;
	u8 *cursor;
	s32 sock;
	struct cb_event_source *sock_source;
	struct log_server *server;
	struct list_head link;
};

struct tool_client {
	s32 sock;
	struct cb_event_source *sock_source;
	struct log_server *server;
	struct list_head link;
};

struct log_server {
	struct cb_event_loop *loop;
	struct cb_event_source *sock_source;
	struct cb_event_source *sig_int_source;
	struct cb_event_source *sig_tem_source;
	struct cb_event_source *tool_sock_source;
	s32 sock;
	s32 seat;
	s32 exit;
	s32 log_file_fd;
	struct list_head clients;

	/* send log message to log tools (with GUI) by TCP */
	s32 tool_sock;
	u32 tcp_port;
	struct list_head tool_clients;
};

#define TCP_PORT_BASE 8099

static void log_client_destroy(struct log_client *client)
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
	s32 ret;
	size_t byts_rd;
	struct log_client *client = data;
	struct cb_fds fds = {
		.count = 0,
	};
	s32 flag; /* 0: length not received, 1: length received. */
	off_t offs;

	if (client->cursor >= ((u8 *)(client->log_buf) + sizeof(size_t))) {
		flag = 1;
	} else {
		flag = 0;
	}

	do {
		/* printf("try to receive %lu\n", client->byts_to_rd); */
		ret = cb_recvmsg(client->sock, client->cursor,
				 client->byts_to_rd, &fds);
		/* printf("receive return %d\n", ret); */
	} while (ret == -EAGAIN);

	if (ret < 0) {
		fprintf(stderr, "failed to recv log (%s).\n", strerror(-ret));
		return ret;
	} else if (ret == 0) {
		log_client_destroy(client);
		list_del(&client->link);
		return 0;
	}

	client->cursor += ret;
	client->byts_to_rd -= ret;
	byts_rd = ret;

	if (!flag) {
		if (ret >= sizeof(size_t)) {
			/* received the length */
			flag = 1;
			memcpy(&client->byts_to_rd, client->log_buf,
			       sizeof(size_t));
			client->log_sz = client->byts_to_rd;
			/* printf("log size: %lu\n", client->log_sz); */
			/* printf("%p %p\n", client->cursor, client->log_buf);*/
			if ((byts_rd - sizeof(size_t)) > client->log_sz) {
				/* received more than one log */
				client->byts_to_rd = 0;
				assert(0);
			} else {
				client->byts_to_rd -= (byts_rd -sizeof(size_t));
			}
		}
	}

	if (!client->byts_to_rd) {
		client->cursor = (u8 *)client->log_buf;
		client->byts_to_rd = sizeof(size_t);
		/* printf("complete.\n"); */
		if (client->server->log_file_fd > 0) {
			offs = lseek(client->server->log_file_fd,
				     0, SEEK_CUR);
			if (offs > MAX_LOG_SIZE) {
				char cmd[128];
				sprintf(cmd, "cp -f %s/cube_log_%d.txt "
					"%s/cube_log_%d_bak.txt",
					LOG_SERVER_NAME_PREFIX,
					client->server->seat,
					LOG_SERVER_NAME_PREFIX,
					client->server->seat);
				system(cmd);
				lseek(client->server->log_file_fd, 0, SEEK_SET);
				ftruncate(client->server->log_file_fd, 0);
			}
			write(client->server->log_file_fd,
			      (char *)&client->log_buf[0] + sizeof(size_t),
			      client->log_sz);
		}
	}

	return 0;
}

static struct log_client *log_client_create(s32 sock, struct log_server *server)
{
	struct log_client *client;

	if (sock <= 0)
		return NULL;

	client = calloc(1, sizeof(*client));
	if (!client)
		goto err;

	memset(client, 0 ,sizeof(*client));
	client->sock = sock;
	client->server = server;

	client->sock_source = cb_event_loop_add_fd(server->loop, client->sock,
						   CB_EVT_READABLE,
						   client_sock_cb,
						   client);
	if (!client->sock_source)
		goto err;

	client->byts_to_rd = sizeof(size_t);
	client->cursor = (u8 *)(client->log_buf);

	return client;

err:
	log_client_destroy(client);
	return NULL;
}

static s32 server_sock_cb(s32 fd, u32 mask, void *data)
{
	struct log_server *server = data;
	struct log_client *client;
	s32 sock;

	sock = cb_socket_accept(fd);
	if (sock < 0) {
		fprintf(stderr, "failed to accept. (%s)\n", strerror(errno));
		return -errno;
	}
	
	client = log_client_create(sock, server);
	if (!client) {
		fprintf(stderr, "failed to create log client.\n");
		return -1;
	}

	list_add_tail(&client->link, &server->clients);
	return 0;
}

static void log_server_stop(struct log_server *server)
{
	if (!server)
		return;

	server->exit = 1;
}

static void log_server_run(struct log_server *server)
{
	if (!server)
		return;

	server->exit = 0;

	while (!server->exit) {
		cb_event_loop_dispatch(server->loop, -1);
	}
}

static s32 signal_event_proc(s32 signal_number, void *data)
{
	struct log_server *server = data;

	switch (signal_number) {
	case SIGINT:
		log_server_stop(server);
		break;
	case SIGTERM:
		log_server_stop(server);
		break;
	default:
		fprintf(stderr, "Receive unknown signal %d", signal_number);
		return -1;
	}

	return 0;
}

static void tool_client_destroy(struct tool_client *client)
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

static void log_server_destroy(struct log_server *server)
{
	struct log_client *client, *next;
	struct tool_client *tool, *next_tool;
	if (!server)
		return;

	if (server->loop) {
		list_for_each_entry_safe(tool, next_tool, &server->tool_clients,
					 link) {
			list_del(&tool->link);
			tool_client_destroy(tool);
		}

		if (server->tool_sock_source) {
			cb_event_source_remove(server->tool_sock_source);
			server->tool_sock_source = NULL;
		}

		if (server->tool_sock) {
			close(server->tool_sock);
			server->tool_sock = 0;
		}

		list_for_each_entry_safe(client, next, &server->clients, link) {
			list_del(&client->link);
			log_client_destroy(client);
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

		cb_event_loop_destroy(server->loop);
		server->loop = NULL;
	}

	if (server->log_file_fd > 0) {
		close(server->log_file_fd);
		server->log_file_fd = 0;
	}

	free(server);
}

static s32 tool_client_sock_cb(s32 fd, u32 mask, void *data)
{
	return 0;
}

static struct tool_client *tool_client_create(s32 sock,
					      struct log_server *server)
{
	struct tool_client *client;

	if (sock <= 0)
		return NULL;

	client = calloc(1, sizeof(*client));
	if (!client)
		goto err;

	memset(client, 0 ,sizeof(*client));
	client->sock = sock;
	client->server = server;

	client->sock_source = cb_event_loop_add_fd(server->loop, client->sock,
						   CB_EVT_READABLE,
						   tool_client_sock_cb,
						   client);
	if (!client->sock_source)
		goto err;

	return client;

err:
	tool_client_destroy(client);
	return NULL;
}

static s32 server_tool_sock_cb(s32 fd, u32 mask, void *data)
{
	struct log_server *server = data;
	struct tool_client *client;
	s32 sock;

	sock = cb_tcp_socket_accept(fd);
	if (sock < 0) {
		fprintf(stderr, "failed to accept. (%s)\n", strerror(errno));
		return -errno;
	}

	client = tool_client_create(sock, server);
	if (!client) {
		fprintf(stderr, "failed to create log client.\n");
		return -1;
	}

	list_add_tail(&client->link, &server->tool_clients);
	return 0;
}

static struct log_server *log_server_create(s32 seat)
{
	struct log_server *server;
	char name[64];

	server = calloc(1, sizeof(*server));
	if (!server)
		goto err;

	INIT_LIST_HEAD(&server->clients);

	server->seat = seat;

	server->loop = cb_event_loop_create();
	if (!server->loop)
		goto err;

	server->sock = cb_socket_cloexec();
	if (!server->sock)
		goto err;

	snprintf(name, 64, "%s/%s-%d", LOG_SERVER_NAME_PREFIX,
		 LOG_SERVER_SOCK_NAME, server->seat);

	unlink(name);
	cb_socket_bind_listen(server->sock, name);

	server->sock_source = cb_event_loop_add_fd(server->loop, server->sock,
						   CB_EVT_READABLE,
						   server_sock_cb,
						   server);
	if (!server->sock_source)
		goto err;

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

	snprintf(name, 64, "%s/cube_log_%d.txt", LOG_SERVER_NAME_PREFIX,
		 server->seat);
	server->log_file_fd = open(name, O_CREAT | O_TRUNC | O_RDWR, 0644);

	INIT_LIST_HEAD(&server->tool_clients);
	server->tcp_port = TCP_PORT_BASE + server->seat;
	server->tool_sock = cb_tcp_socket_cloexec();
	if (!server->tool_sock)
		goto err;

	cb_tcp_socket_bind_listen(server->sock, server->tcp_port);

	server->tool_sock_source = cb_event_loop_add_fd(
					server->loop, server->tool_sock,
					CB_EVT_READABLE,
					server_tool_sock_cb,
					server);
	if (!server->tool_sock_source)
		goto err;
	
	return server;

err:
	log_server_destroy(server);
	return NULL;
}

static void usage(void)
{
	printf("Usage:\n");
	printf("cube_log [options]\n");
	printf("\toptions:\n");
	printf("\t\t-b, --background, run background.\n");
	printf("\t\t-h, --help, show this message.\n");
	printf("\t\t-s, --seat=ID, log server's instance ID.\n");
}

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

s32 main(s32 argc, char **argv)
{
	s32 ch;
	s32 run_as_background = 0;
	struct log_server *server;
	s32 seat = 0;

	while ((ch = getopt_long(argc, argv, short_options,
				 long_options, NULL)) != -1) {
		switch (ch) {
		case 'b':
			run_as_background = 1;
			break;
		case 'h':
			usage();
			return 0;
		case 's':
			seat = atoi(optarg);
			break;
		default:
			usage();
			return -1;
		}
	}

	if (run_as_background)
		run_background();

	server = log_server_create(seat);
	if (!server)
		goto err;

	log_server_run(server);
err:
	log_server_destroy(server);

	return 0;
}

