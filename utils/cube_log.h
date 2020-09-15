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
#ifndef CUBE_LOG_H
#define CUBE_LOG_H

#include <time.h>
#include <cube_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_SERVER_NAME_PREFIX "/tmp"
#define LOG_SERVER_SOCK_NAME "cube_log_server"

enum cb_log_level {
	CB_LOG_ERR = 0,
	CB_LOG_NOTICE,
	CB_LOG_INFO,
	CB_LOG_DEBUG,
};

#define cb_tlog(fmt, ...) do { \
	struct timespec now; \
	clock_gettime(CLOCK_MONOTONIC, &now); \
	cb_log("[%05lu:%06lu][%-24s:%05d] " fmt "\n", \
	       now.tv_sec % 86400l, now.tv_nsec / 1000l, \
	       __func__, __LINE__, ##__VA_ARGS__); \
} while (0);

#define cb_log_time_prefix(str, n) do { \
	struct timespec now; \
	clock_gettime(CLOCK_MONOTONIC, &now); \
	snprintf((str), (n), "[%05lu:%06lu]", \
		 now.tv_sec % 86400l, now.tv_nsec / 1000l); \
} while (0);

#define cb_log_func_prefix(str, n) do { \
	snprintf((str), (n), "[%-24s:%05d]", __func__, __LINE__); \
} while (0);

#define cb_log_extend(str, fmt, ...) do { \
	char *p = (str) + strlen(str); \
	sprintf(p, fmt, ##__VA_ARGS__); \
	cb_log("%s", str); \
} while (0);

/*
 * Example:
 * 
 * cb_tlog("[%d] test", i);
 *
 * for (i = 0; i < 256; i++) {
 * 	if (!(i % 16)) {
 * 		p = buf;
 * 		cb_log_time_prefix(p, 64);
 * 		p += strlen(p);
 * 		cb_log_func_prefix(p, 64);
 * 		p += strlen(p);
 * 	}
 * 	sprintf(p, " %02Xh", i);
 * 	p += strlen(p);
 * 	if (!((i + 1) % 16)) {
 * 		cb_log_extend(buf, "\n");
 * 	}
 * }
 * 
 */

extern void cb_log(const char *fmt, ...);
extern s32 cb_log_init(const char *log_server_name);
extern void cb_log_fini(void);

#ifdef __cplusplus
}
#endif

#endif

