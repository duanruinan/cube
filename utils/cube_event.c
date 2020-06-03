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
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_signal.h>
#include <cube_event.h>

struct cb_event_source_idle {
	struct cb_event_source base;
	cb_event_loop_idle_cb_t cb;
};

struct cb_event_source_fd {
	struct cb_event_source base;
	cb_event_loop_fd_cb_t cb;
	s32 fd;
};

struct cb_event_source_timer {
	struct cb_event_source base;
	cb_event_loop_timer_cb_t cb;
};

struct cb_event_source_signal {
	struct cb_event_source base;
	cb_event_loop_signal_cb_t cb;
	s32 signal_number;
};

struct cb_event_source_interface idle_source_interface = {
	NULL,
};

static s32 epoll_create_cloexec(void)
{
	s32 fd;

#ifdef EPOLL_CLOEXEC
	fd = epoll_create1(EPOLL_CLOEXEC);
	if (fd >= 0)
		return fd;
	if (errno != EINVAL) {
		fprintf(stderr, "failed to epoll_create1 %m\n");
		return -1;
	}
#endif
	fd = epoll_create(1);
	return cb_set_cloexec_or_close(fd);
}

static void process_destroy_list(struct cb_event_loop *loop)
{
	struct cb_event_source *source, *next;

	list_for_each_entry_safe(source, next, &loop->destroy_list, link)
		free(source);

	INIT_LIST_HEAD(&loop->destroy_list);
}

struct cb_event_loop * cb_event_loop_create(void)
{
	struct cb_event_loop *loop;

	loop = calloc(1, sizeof(*loop));
	if (!loop) {
		fprintf(stderr, "not enough memory to alloc event loop.\n");
		return NULL;
	}

	loop->epoll_fd = epoll_create_cloexec();
	if (loop->epoll_fd < 0) {
		free(loop);
		return NULL;
	}

	INIT_LIST_HEAD(&loop->idle_list);
	INIT_LIST_HEAD(&loop->destroy_list);
	cb_signal_init(&loop->destroy_signal);

	return loop;
}

void cb_event_loop_destroy(struct cb_event_loop *loop)
{
	if (!loop)
		return;

	cb_signal_emit(&loop->destroy_signal, loop);
	process_destroy_list(loop);
	close(loop->epoll_fd);
	free(loop);
	loop = NULL;
}

void cb_event_source_remove(struct cb_event_source *source)
{
	if (!source)
		return;

	if (source->fd >= 0) {
		epoll_ctl(source->loop->epoll_fd, EPOLL_CTL_DEL, source->fd,
			  NULL);
		close(source->fd);
		source->fd = -1;
	}
	list_del(&source->link);
	list_add_tail(&source->link, &source->loop->destroy_list);
}

static void cb_event_idle_source_remove(struct cb_event_source *source)
{
	assert(source->fd < 0);
	list_del(&source->link);
	free(source);
}

struct cb_event_source * cb_event_loop_add_idle(struct cb_event_loop *loop,
						cb_event_loop_idle_cb_t cb,
						void *data)
{
	struct cb_event_source_idle *source;

	source = calloc(1, sizeof(*source));
	if (!source)
		return NULL;

	source->base.interface = &idle_source_interface;
	source->base.loop = loop;
	source->base.fd = -1;
	source->cb = cb;
	source->base.data = data;
	list_add_tail(&source->base.link, &loop->idle_list);

	return &source->base;
}

void cb_event_loop_dispatch_idle(struct cb_event_loop *loop)
{
	struct cb_event_source_idle *source;

	while (!list_empty(&loop->idle_list)) {
		source = container_of(loop->idle_list.next,
				      struct cb_event_source_idle,
				      base.link);
		source->cb(source->base.data);
		cb_event_idle_source_remove(&source->base);
	}
}

s32 cb_event_loop_dispatch(struct cb_event_loop *loop, s32 timeout)
{
	struct cb_event_source *source;
	struct epoll_event ep[32];
	s32 i, n;
	
	cb_event_loop_dispatch_idle(loop);

retry:
	n = epoll_wait(loop->epoll_fd, ep, ARRAY_SIZE(ep), timeout);
	if (n < 0) {
		if (errno == EINTR)
			goto retry;
		return -1;
	}

	for (i = 0; i < n; i++) {
		source = ep[i].data.ptr;
		if (source->fd > 0)
			source->interface->dispatch(source, &ep[i]);
	}
	
	process_destroy_list(loop);

	cb_event_loop_dispatch_idle(loop);

	return 0;
}

static struct cb_event_source * cb_event_loop_add_source(
						struct cb_event_loop *loop,
						struct cb_event_source *source,
						u32 mask,
						void *data)
{
	struct epoll_event ep;

	if (source->fd < 0) {
		free(source);
		return NULL;
	}

	source->loop = loop;
	source->data = data;
	INIT_LIST_HEAD(&source->link);

	memset(&ep, 0, sizeof(ep));
	if (mask & CB_EVT_READABLE)
		ep.events |= EPOLLIN;
	if (mask & CB_EVT_WRITABLE)
		ep.events |= EPOLLOUT;
	ep.data.ptr = source;

	if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, source->fd, &ep) < 0) {
		fprintf(stderr, "epoll_ctl failed. %m\n");
		fprintf(stderr, "epoll_fd = %d source->fd = %d\n",
			loop->epoll_fd, source->fd);
		close(source->fd);
		free(source);
		return NULL;
	}

	return source;
}

static s32 cb_event_source_fd_dispatch(struct cb_event_source *source,
				       struct epoll_event *ep)
{
	struct cb_event_source_fd *fd_source;
	u32 mask = 0;

	fd_source = container_of(source, struct cb_event_source_fd, base);
	if (ep->events & EPOLLIN)
		mask |= CB_EVT_READABLE;
	if (ep->events & EPOLLOUT)
		mask |= CB_EVT_WRITABLE;
	if (ep->events & EPOLLHUP)
		mask |= CB_EVT_HANGUP;
	if (ep->events & EPOLLERR)
		mask |= CB_EVT_ERROR;

	return fd_source->cb(fd_source->fd, mask, source->data);
}

static struct cb_event_source_interface fd_source_interface = {
	cb_event_source_fd_dispatch,
};

s32 cb_set_cloexec_or_close(s32 fd)
{
	s32 flags;

	if (fd == -1)
		return -1;

	flags = fcntl(fd, F_GETFD);
	if (flags == -1)
		goto err;

	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
		fprintf(stderr, "failed to fcntl setfd cloexec. %m\n");
		goto err;
	}

	return fd;

err:
	close(fd);
	return -1;
}

s32 cb_dupfd_cloexec(s32 fd, s32 minfd)
{
	s32 newfd;

	newfd = fcntl(fd, F_DUPFD_CLOEXEC, minfd);
	if (newfd >= 0)
		return newfd;

	if (errno != EINVAL) {
		fprintf(stderr, "failed to fcntl dupfd. %m\n");
		return -1;
	}

	newfd = fcntl(fd, F_DUPFD, minfd);
	return cb_set_cloexec_or_close(newfd);
}

struct cb_event_source * cb_event_loop_add_fd(struct cb_event_loop *loop,
					      s32 fd,
					      u32 mask,
					      cb_event_loop_fd_cb_t cb,
					      void *data)
{
	struct cb_event_source_fd *source;
	
	source = calloc(1, sizeof(*source));
	if (!source)
		return NULL;

	source->base.interface = &fd_source_interface;
	source->fd = fd;
	source->base.fd = cb_dupfd_cloexec(fd, 0);
	source->cb = cb;
	return cb_event_loop_add_source(loop, &source->base, mask, data);
}

s32 cb_event_source_fd_update_mask(struct cb_event_source *source, u32 mask)
{
	struct epoll_event ep;

	memset(&ep, 0, sizeof(ep));
	if (mask & CB_EVT_READABLE)
		ep.events |= EPOLLIN;
	if (mask & CB_EVT_WRITABLE)
		ep.events |= EPOLLOUT;
	ep.data.ptr = source;

	return epoll_ctl(source->loop->epoll_fd, EPOLL_CTL_MOD, source->fd,&ep);
}

static s32 cb_event_source_timer_dispatch(struct cb_event_source *source,
					  struct epoll_event *ep)
{
	struct cb_event_source_timer *timer_source;
	u64 expires;
	u32 len;

	timer_source = container_of(source,
				    struct cb_event_source_timer,
				    base);
	len = read(source->fd, &expires, sizeof(expires));
	if (!(len == -1 && errno == EAGAIN) && len != sizeof(expires))
		fprintf(stderr, "failed to read timerfd: %m\n");

	return timer_source->cb(source->data);
}

static struct cb_event_source_interface timer_source_interface = {
	cb_event_source_timer_dispatch,
};

struct cb_event_source * cb_event_loop_add_timer(struct cb_event_loop *loop,
						 cb_event_loop_timer_cb_t cb,
						 void *data)
{
	struct cb_event_source_timer *source;

	source = calloc(1, sizeof(*source));
	if (!source)
		return NULL;

	source->base.fd = timerfd_create(CLOCK_MONOTONIC,
					TFD_CLOEXEC | TFD_NONBLOCK);
	source->cb = cb;
	source->base.interface = &timer_source_interface;
	return cb_event_loop_add_source(loop, &source->base,
					CB_EVT_READABLE, data);
}

s32 cb_event_source_timer_update(struct cb_event_source *source, s32 ms, s32 us)
{
	struct itimerspec its;

	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = ms / 1000;
	its.it_value.tv_nsec = (ms % 1000) * 1000 * 1000 + us * 1000;

	if (timerfd_settime(source->fd, 0, &its, NULL) < 0) {
		fprintf(stderr, "failed to timerfd_settime: %s "
			"fd = %d %ld %ld\n",
			strerror(errno), source->fd, its.it_value.tv_sec,
			its.it_value.tv_nsec);
		return -1;
	}

	return 0;
}

static s32 cb_event_source_signal_dispatch(struct cb_event_source *source,
					   struct epoll_event *ep)
{
	struct cb_event_source_signal *signal_source;
	struct signalfd_siginfo siginfo;
	u32 len;

	signal_source = container_of(source,
				     struct cb_event_source_signal,
				     base);
	len = read(source->fd, &siginfo, sizeof(siginfo));
	if (!(len == -1 && errno == EAGAIN) && len != sizeof(siginfo))
		fprintf(stderr, "failed to read signalfd: %m\n");

	return signal_source->cb(signal_source->signal_number, source->data);
}

static struct cb_event_source_interface signal_source_interface = {
	cb_event_source_signal_dispatch,
};

struct cb_event_source * cb_event_loop_add_signal(
				struct cb_event_loop *loop,
				s32 signal_number,
				cb_event_loop_signal_cb_t cb,
				void *data)
{
	struct cb_event_source_signal *source;
	sigset_t mask;

	source = calloc(1, sizeof(*source));
	if (!source)
		return NULL;

	sigemptyset(&mask);
	sigaddset(&mask, signal_number);
	source->base.fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	source->cb = cb;
	source->base.interface = &signal_source_interface;
	source->signal_number = signal_number;

	return cb_event_loop_add_source(loop, &source->base,
					CB_EVT_READABLE, data);
}

void cb_event_loop_add_destroy_listener(struct cb_event_loop *loop,
					struct cb_listener *listener)
{
	cb_signal_add(&loop->destroy_signal, listener);
}

struct cb_listener * cb_event_loop_get_destroy_listener(
					struct cb_event_loop *loop,
					cb_notify_cb_t notify)
{
	return cb_signal_get(&loop->destroy_signal, notify);
}

