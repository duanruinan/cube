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
#ifndef CUBE_IPC_H
#define CUBE_IPC_H

#include <cube_utils.h>

#define MAX_FDS_COUNT 32
#define CLEN (CMSG_LEN(MAX_FDS_COUNT * sizeof(s32)))

struct cb_fds {
	s32 fds[MAX_FDS_COUNT];
	u32 count;
};

#ifdef __cplusplus
extern "C" {
#endif

s32 cb_socket_cloexec(void);
s32 cb_socket_nonblock(s32 sock);
s32 cb_sendmsg(s32 sock, u8 *buf, size_t sz, struct cb_fds *fds);
s32 cb_recvmsg(s32 sock, u8 *buf, size_t sz, struct cb_fds *fds);
s32 cb_socket_bind_listen(const s32 sock, const char *name);
s32 cb_socket_accept(const s32 sock);
s32 cb_socket_connect(const s32 sock, const char *remote);

#ifdef __cplusplus
}
#endif

#endif

