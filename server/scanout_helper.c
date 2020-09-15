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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_log.h>
#include <cube_compositor.h>
#include <cube_scanout.h>

/* commit info helper functions */
struct scanout_commit_info *scanout_commit_info_alloc(void)
{
	struct scanout_commit_info *info = calloc(1, sizeof(*info));

	if (!info)
		return NULL;
	INIT_LIST_HEAD(&info->fb_commits);

	return info;
}

void *scanout_commit_add_fb_info(struct scanout_commit_info *commit,
				 struct cb_buffer *buffer,
				 struct output *output,
				 struct plane *plane,
				 struct cb_rect *src,
				 struct cb_rect *dst,
				 s32 zpos)
{
	struct fb_info *info;

	if (!commit)
		return NULL;

	info = calloc(1, sizeof(*info));
	if (!info)
		return NULL;

	info->buffer = buffer;
	info->output = output;
	info->plane = plane;
	info->src = *src;
	info->dst = *dst;
	info->zpos = zpos;

	list_add_tail(&info->link, &commit->fb_commits);

	return info;
}

void scanout_commit_mod_fb_info(struct scanout_commit_info *commit,
				void *fb_info,
				struct cb_buffer *buffer,
				struct output *output,
				struct plane *plane,
				struct cb_rect *src,
				struct cb_rect *dst,
				s32 zpos)
{
	struct fb_info *info;

	if (!commit)
		return;

	if (!fb_info)
		return;

	info = fb_info;

	info->buffer = buffer;
	info->output = output;
	info->plane = plane;
	info->src = *src;
	info->dst = *dst;
	info->zpos = zpos;
}

void scanout_commit_info_free(struct scanout_commit_info *commit)
{
	struct fb_info *info, *next_info;

	if (!commit)
		return;

	list_for_each_entry_safe(info, next_info, &commit->fb_commits, link) {
		list_del(&info->link);
		free(info);
	}

	free(commit);
}

void scanout_buffer_dirty_init(struct cb_buffer *buffer)
{
	buffer->dirty = 0;
}

void scanout_set_buffer_dirty(struct cb_buffer *buffer, struct output *output)
{
	/* omit disconnected output */
	if (!output->head->connected)
		return;
	buffer->dirty |= (1U << output->index);
}

bool scanout_clr_buffer_dirty(struct cb_buffer *buffer, struct output *output)
{
	if (!buffer->dirty)
		return false;

	buffer->dirty &= (~(1U << output->index));
	printf("Clear %u: %08X\n", output->index, buffer->dirty);

	if (!buffer->dirty)
		return true;

	return false;
}

