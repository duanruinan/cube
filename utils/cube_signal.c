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
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_signal.h>

void cb_signal_init(struct cb_signal *signal)
{
	INIT_LIST_HEAD(&signal->listener_list);
}

void cb_signal_add(struct cb_signal *signal, struct cb_listener *listener)
{
	list_add_tail(&listener->link, &signal->listener_list);
}

struct cb_listener * cb_signal_get(struct cb_signal *signal,
				   cb_notify_cb_t notify)
{
	struct cb_listener *l;

	list_for_each_entry(l, &signal->listener_list, link)
		if (l->notify == notify)
			return l;

	return NULL;
}

void cb_signal_emit(struct cb_signal *signal, void *data)
{
	struct cb_listener *l, *next;

	list_for_each_entry_safe(l, next, &signal->listener_list, link) {
		l->notify(l, data);
	}
}

