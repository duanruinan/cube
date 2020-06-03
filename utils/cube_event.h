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
#ifndef CUBE_EVENT_H
#define CUBE_EVENT_H

#include <sys/epoll.h>
#include <cube_utils.h>
#include <cube_signal.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	CB_EVT_READABLE = 0x01,
	CB_EVT_WRITABLE = 0x02,
	CB_EVT_HANGUP   = 0x04,
	CB_EVT_ERROR    = 0x08,
};

struct cb_event_loop {
	s32 epoll_fd;
	struct list_head idle_list;
	struct list_head destroy_list;
	struct cb_signal destroy_signal;
};

struct cb_event_source;

struct cb_event_source_interface {
	s32 (*dispatch)(struct cb_event_source *source,
			struct epoll_event *ep);
};

typedef void (*cb_event_loop_idle_cb_t)(void *data);
typedef s32 (*cb_event_loop_fd_cb_t)(s32 fd, u32 mask, void *data);
typedef s32 (*cb_event_loop_timer_cb_t)(void *data);
typedef s32 (*cb_event_loop_signal_cb_t)(s32 signal_number, void *data);

struct cb_event_source {
	struct cb_event_source_interface *interface;
	struct cb_event_loop *loop;
	struct list_head link;
	void *data;
	s32 fd;
};

s32 cb_set_cloexec_or_close(s32 fd);
s32 cb_dupfd_cloexec(s32 fd, s32 minfd);
struct cb_event_source * cb_event_loop_add_fd(struct cb_event_loop *loop,
					      s32 fd, u32 mask,
					      cb_event_loop_fd_cb_t cb,
					      void *data);
s32 cb_event_source_fd_update_mask(struct cb_event_source *source, u32 mask);
struct cb_event_source * cb_event_loop_add_timer(
				struct cb_event_loop *loop,
				cb_event_loop_timer_cb_t cb,
				void *data);
s32 cb_event_source_timer_update(struct cb_event_source *source,s32 ms, s32 us);
struct cb_event_source * cb_event_loop_add_signal(
				struct cb_event_loop *loop,
				s32 signal_number,
				cb_event_loop_signal_cb_t cb,
				void *data);

struct cb_event_loop * cb_event_loop_create(void);
void cb_event_loop_destroy(struct cb_event_loop *loop);
void cb_event_source_remove(struct cb_event_source *source);

struct cb_event_source * cb_event_loop_add_idle(struct cb_event_loop *loop,
						cb_event_loop_idle_cb_t cb,
						void *data);

s32 cb_event_loop_dispatch(struct cb_event_loop *loop, s32 timeout);

void cb_event_loop_add_destroy_listener(struct cb_event_loop *loop,
					struct cb_listener *listener);

struct cb_listener * cb_event_loop_get_destroy_listener(
					struct cb_event_loop *loop,
					cb_notify_cb_t notify);

#ifdef __cplusplus
}
#endif

#endif

