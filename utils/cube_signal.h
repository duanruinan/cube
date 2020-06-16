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
#ifndef CUBE_SIGNAL_H
#define CUBE_SIGNAL_H

#include <cube_utils.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cb_listener;

typedef void (*cb_notify_cb_t)(struct cb_listener *listener, void *data);

struct cb_listener {
	struct list_head link;
	cb_notify_cb_t notify;
};

struct cb_signal {
	struct list_head listener_list;
};

void cb_signal_init(struct cb_signal *signal);
void cb_signal_fini(struct cb_signal *signal);
void cb_signal_add(struct cb_signal *signal, struct cb_listener *listener);
void cb_signal_rm(struct cb_signal *signal, struct cb_listener *listener);
struct cb_listener * cb_signal_get(struct cb_signal *signal,
				   cb_notify_cb_t notify);
void cb_signal_emit(struct cb_signal *signal, void *data);

#ifdef __cplusplus
}
#endif

#endif

