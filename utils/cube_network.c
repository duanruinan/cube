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
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <errno.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_ipc.h>

s32 cb_tcp_socket_cloexec(void)
{
	s32 fd, buf_sz, ret;

	fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd >= 0)
		return fd;
	if (errno != EINVAL) {
		return -1;
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	buf_sz = 20 * 1024 * 1024;
	ret = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&buf_sz,
			 sizeof(s32));
	if (ret) {
		return -1;
	}

	buf_sz = 20 * 1024 * 1024;
	ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&buf_sz,
			 sizeof(s32));
	if (ret) {
		return -1;
	}
	return cb_set_cloexec_or_close(fd);
}

s32 cb_tcp_socket_bind_listen(const s32 sock, u32 port)
{
	struct sockaddr_in sin;
	s32 on = 1;

	/* avoid err: Address already in use */
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(port);
	if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		return -errno;
	}

	if (listen(sock, 10) < 0) {
		return -errno;
	}

	return 0;
}

s32 cb_tcp_socket_accept(const s32 sock)
{
	struct sockaddr_in pin;
	u32 address_size;
	s32 sk;

	do {
		address_size = sizeof(pin);
		sk = accept(sock, (struct sockaddr *)&pin, &address_size);
	} while (sk < 0 && errno == EINTR);

	if (sk < 0) {
		return -errno;
	}

	return sk;
}

s32 cb_tcp_send(s32 sock, void *buf, size_t sz)
{
	size_t byts_to_wr = sz;
	u8 *p = buf;
	s32 ret;

	if (!buf || !sz)
		return 0;

	while (byts_to_wr) {
		do {
			ret = send(sock, p, byts_to_wr, MSG_NOSIGNAL);
		} while (ret < 0 && (errno == EINTR || errno == EAGAIN));

		if (ret <= 0) {
			return -errno;
		}

		p += ret;
		byts_to_wr -= ret;
	}

	return 0;
}

s32 cb_tcp_recv(s32 sock, void *buf, size_t sz)
{
	size_t byts_to_rd = sz;
	u8 *p = buf;
	s32 ret;

	while (byts_to_rd) {
		do {
			ret = recv(sock, p, byts_to_rd, 0);
			if (ret == 0) {
				return -1;
			}
		} while (ret < 0 && (errno == EINTR || errno == EAGAIN));

		if (ret <= 0) {
			return -errno;
		}

		p += ret;
		byts_to_rd -= ret;
	}

	return 0;
}


