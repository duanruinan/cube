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
#include <sys/un.h>
#include <errno.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_ipc.h>

s32 cb_socket_cloexec(void)
{
	s32 fd;

	fd = socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd >= 0)
		return fd;
	if (errno != EINVAL) {
		fprintf(stderr, "failed to create socket fd. %m\n");
		return -1;
	}

	fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	return cb_set_cloexec_or_close(fd);
}

s32 cb_socket_nonblock(s32 sock)
{
	s32 ret, flag;

	flag = fcntl(sock, F_GETFL, 0);
	if (flag < 0) {
		fprintf(stderr, "failed to fcntl %m\n");
		return -errno;
	}

	ret = fcntl(sock, F_SETFL, flag | O_NONBLOCK);
	if (ret < 0) {
		fprintf(stderr, "failed to fcntl %m\n");
		return -errno;
	}

	return 0;
}

/*
 * need to resend when errno == EAGAIN
 * return:
 * > 0: success
 * < 0: failerrno != EAGAIN
 */
s32 cb_sendmsg(s32 sock, u8 *buf, size_t sz, struct cb_fds *fds)
{
	s32 ret;
	struct msghdr msg;
	struct cmsghdr *p_cmsg;
	struct iovec vec;
	char cmsgbuf[CLEN];
	s32 clen;

	p_cmsg = (struct cmsghdr *)(&cmsgbuf[0]);
	p_cmsg->cmsg_level = SOL_SOCKET;
	p_cmsg->cmsg_type = SCM_RIGHTS;
	if (fds && fds->count > 0) {
		if (fds->count > MAX_FDS_COUNT) {
			fprintf(stderr, "fds out of range %d\n", fds->count);
			return -ERANGE;
		}
		p_cmsg->cmsg_len = CMSG_LEN(sizeof(s32) * fds->count);
		clen = p_cmsg->cmsg_len;
		memcpy(CMSG_DATA(p_cmsg), fds->fds, sizeof(s32) * fds->count);
	} else {
		clen = 0;
	}

	vec.iov_base = buf;
	vec.iov_len = sz;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = (clen > 0) ? cmsgbuf : NULL;
	msg.msg_controllen = clen;
	msg.msg_flags = 0;
	
	do {
		ret = sendmsg(sock, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0) {
		if (errno != EAGAIN) {
			fprintf(stderr, "failed to sendmsg %s.\n",
				strerror(errno));
		}
		return -errno;
	}

	/* close fd ? */
	return ret;
}

/*
 * need to rerecv when errno == EAGAIN
 * return
 *   > 0: success.
 *     0: connection broken.
 *    -1: fail errno != EAGAIN
 */
s32 cb_recvmsg(s32 sock, u8 *buf, size_t sz, struct cb_fds *fds)
{
	s32 ret;
	struct msghdr msg;
	struct iovec vec;
	char cmsgbuf[CLEN];
	struct cmsghdr *p_cmsg;
	u8 *data;
	s32 *p_fd, *end, i;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	vec.iov_base = buf;
	vec.iov_len = sz;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	msg.msg_flags = 0;

	do {
		ret = recvmsg(sock, &msg, MSG_DONTWAIT);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0) {
		if (errno != EAGAIN) {
			fprintf(stderr, "failed to recvmsg. sock: %d (%s) %s\n",
				sock, strerror(errno), __func__);
			fprintf(stderr, "buf = %p, sz = %lu, fds %p %u\n",
				buf, sz, fds, fds ? fds->count : 0);
		}
		return -errno;
	}

	if (fds)
		fds->count = 0;

	if (!msg.msg_control || msg.msg_controllen == 0)
		return ret;

	if (!fds)
		return ret;

	i = 0;

	for (p_cmsg = CMSG_FIRSTHDR(&msg); p_cmsg != NULL;
	     p_cmsg = CMSG_NXTHDR(&msg, p_cmsg)) {
		if (p_cmsg->cmsg_level != SOL_SOCKET
		    || p_cmsg->cmsg_type != SCM_RIGHTS)
			continue;

		data = CMSG_DATA(p_cmsg);
		end = (s32 *)(data + p_cmsg->cmsg_len - CMSG_LEN(0));
		for (p_fd = (s32 *)data; p_fd < end; p_fd++) {
			*p_fd = cb_set_cloexec_or_close(*p_fd);
			if (i < MAX_FDS_COUNT) {
				fds->fds[i++] = *p_fd;
			}
		}
	}

	fds->count = i;

	return ret;
}

s32 cb_socket_bind_listen(const s32 sock, const char *name)
{
	struct sockaddr_un servaddr;

	unlink(name);
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sun_family = AF_LOCAL;
	strcpy(servaddr.sun_path, name);
	if (bind(sock, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
		fprintf(stderr, "bind socket failed. %m\n");
		return -errno;
	}

	if (listen(sock, 200) < 0) {
		fprintf(stderr, "listen socket failed. %m\n");
		return -errno;
	}

	return 0;
}

s32 cb_socket_accept(const s32 sock)
{
	s32 sk;

	if ((sk = accept(sock, NULL, NULL)) < 0) {
		fprintf(stderr, "accept socket failed. %m\n");
		return -errno;
	}

	return sk;
}

s32 cb_socket_connect(const s32 sock, const char *remote)
{
	struct sockaddr_un servaddr;
	s32 i;

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sun_family = AF_LOCAL;
	strcpy(servaddr.sun_path, remote);
	for (i = 0; i < 500; i++) {
		if (connect(sock, (struct sockaddr*)&servaddr,
			    sizeof(servaddr)) < 0) {
			fprintf(stderr, "cannot connect to %s %m try:(%d)\n",
				remote, i);
			usleep(30000);
			continue;
		}
		return 0;
	}

	fprintf(stderr, "failed to connect %m %s\n", remote);
	return -errno;
}

