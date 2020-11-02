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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <cube_utils.h>
#include <cube_ipc.h>
#include <syslog.h>
#include "stat_tips.h"

static s32 ipc_sock = 0;

void dashboard_connect(void)
{
	if (ipc_sock <= 0) {
		ipc_sock = cb_socket_cloexec();
		if (ipc_sock <= 0) {
			syslog(LOG_ERR, "create socket failed.\n");
			return;
		}
	} else {
		syslog(LOG_ERR, "dashboard socket already init.\n");
		return;
	}
	
	cb_socket_connect(ipc_sock, "/tmp/stat_tips");
}

void dashboard_disconnect(void)
{
	syslog(LOG_DEBUG, "dashboard disconnect\n");
	close(ipc_sock);
	ipc_sock = 0;
}

void set_dashboard(struct dashboard_info *dashboard)
{
	s32 ret;
	size_t length;

	if (!dashboard)
		return;

	if (ipc_sock <= 0) {
		syslog(LOG_ERR, "dashboard socket not init.\n");
		dashboard_connect();
		if (ipc_sock <= 0) {
			return;
		}
	}

	length = sizeof(struct dashboard_info);
	do {
		ret = cb_sendmsg(ipc_sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		syslog(LOG_ERR, "failed to send dashboard length. %s\n",
			strerror(errno));
		printf("failed to send dashboard length. %s\n",
			strerror(errno));
		return;
	}

	do {
		ret = cb_sendmsg(ipc_sock, (u8 *)dashboard, length, NULL);
	} while (ret == -EAGAIN);
	if (ret < 0) {
		syslog(LOG_ERR, "failed to send dashboard. %s\n",
			strerror(errno));
		printf("failed to send dashboard. %s\n",
			strerror(errno));
	}
}

