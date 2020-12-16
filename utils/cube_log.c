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
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <cube_utils.h>
#include <cube_ipc.h>
#include <cube_log.h>

#define LOG_BUF_MAX_LEN 4096

struct log_client {
	char log_buf[LOG_BUF_MAX_LEN];
	s32 sock;
};

static struct log_client *client = NULL;

void cb_log_fini(void)
{
	if (client) {
		if (client->sock > 0) {
			close(client->sock);
			client->sock = 0;
		}

		free(client);
		client = NULL;
	}
}

s32 cb_log_init(const char *log_server_name)
{
	s32 e;

	if (!log_server_name)
		return -EINVAL;

	client = calloc(1, sizeof(*client));
	if (!client)
		return -ENOMEM;

	memset(client->log_buf, 0, LOG_BUF_MAX_LEN);
	client->sock = cb_socket_cloexec();
	if (client->sock < 0) {
		fprintf(stderr, "failed to create socket. (%s) %s.\n",
			strerror(errno), __func__);
		e = -errno;
		goto err;
	}

	e = cb_socket_connect(client->sock, log_server_name);
	if (e < 0) {
		fprintf(stderr, "failed to connect to log server %s. (%s)\n",
			log_server_name, strerror(-e));
		goto err;
	}

	return 0;

err:
	cb_log_fini();

	return e;
}

void cb_log(const char *fmt, ...)
{
	va_list ap;
	size_t *len, byts_to_wr;
	char *p;
	struct cb_fds fds = {
		.count = 0,
	};
	s32 ret;

	if (!client)
		return;

	va_start(ap, fmt);
	p = &client->log_buf[0] + sizeof(size_t);
	len = (size_t *)(&client->log_buf[0]);
	vsnprintf(p, LOG_BUF_MAX_LEN - sizeof(size_t), fmt, ap);
	va_end(ap);
	*len = strlen(p);

	byts_to_wr = *len + sizeof(size_t);
	p = &client->log_buf[0];

	do {
		do {
			ret = cb_sendmsg(client->sock, (u8 *)p, byts_to_wr,
					 &fds);
		} while (ret == -EAGAIN);

		if (ret < 0)
			return;

		p += ret;
		byts_to_wr -= ret;
	} while (byts_to_wr > 0);
}

void cb_client_log_fini(void *handle)
{
	struct log_client *cli;

	if (handle) {
		cli = (struct log_client *)handle;
		if (cli->sock > 0) {
			close(cli->sock);
			cli->sock = 0;
		}

		free(cli);
		cli = NULL;
	}
}

void *cb_client_log_init(const char *log_server_name)
{
	struct log_client *cli;
	s32 e;

	if (!log_server_name)
		return NULL;

	cli = calloc(1, sizeof(*cli));
	if (!cli)
		return NULL;

	memset(cli->log_buf, 0, LOG_BUF_MAX_LEN);
	cli->sock = cb_socket_cloexec();
	if (cli->sock < 0) {
		fprintf(stderr, "failed to create socket. (%s) %s.\n",
			strerror(errno), __func__);
		goto err;
	}

	e = cb_socket_connect(cli->sock, log_server_name);
	if (e < 0) {
		fprintf(stderr, "failed to connect to log server %s. (%s)\n",
			log_server_name, strerror(-e));
		goto err;
	}

	return cli;

err:
	cb_log_fini();

	return NULL;
}

void cb_client_log(void *handle, const char *fmt, ...)
{
	va_list ap;
	size_t *len, byts_to_wr;
	char *p;
	struct cb_fds fds = {
		.count = 0,
	};
	struct log_client *cli;
	s32 ret;

	if (!handle)
		return;

	cli = (struct log_client *)handle;

	va_start(ap, fmt);
	p = &cli->log_buf[0] + sizeof(size_t);
	len = (size_t *)(&cli->log_buf[0]);
	vsnprintf(p, LOG_BUF_MAX_LEN - sizeof(size_t), fmt, ap);
	va_end(ap);
	*len = strlen(p);

	byts_to_wr = *len + sizeof(size_t);
	p = &cli->log_buf[0];

	do {
		do {
			ret = cb_sendmsg(cli->sock, (u8 *)p, byts_to_wr, &fds);
		} while (ret == -EAGAIN);

		if (ret < 0)
			return;

		p += ret;
		byts_to_wr -= ret;
	} while (byts_to_wr > 0);
}

