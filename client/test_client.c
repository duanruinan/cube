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
#include <time.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/input.h>
#include <assert.h>
#include <cube_region.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_ipc.h>
#include <cube_event.h>
#include <cube_protocal.h>
#include <cube_client.h>

#define BO_NR 4

static void usage(void)
{
	fprintf(stderr, "test_client --type dma/shm --x x_pos --y y_pos "
			"--width w --height h "
			"--hstride hs --vstride vs --pixel-fmt fourcc "
			"--dmabuf-zpos zpos "
			"--pipe-locked pipe "
			"--composed Y/N\n");
}

static struct option options[] = {
	{"type", 1, NULL, 't'},
	{"x", 1, NULL, 'x'},
	{"y", 1, NULL, 'y'},
	{"width", 1, NULL, 'w'},
	{"height", 1, NULL, 'h'},
	{"hstride", 1, NULL, 'p'},
	{"vstride", 1, NULL, 'v'},
	{"pixel-fmt", 1, NULL, 'f'},
	{"dmabuf-zpos", 1, NULL, 'z'},
	{"pipe-locked", 1, NULL, 'l'},
	{"composed", 1, NULL, 'c'},
	{NULL, 0, NULL, 0},
};

static char short_options[] = "t:x:y:w:h:p:v:f:z:l:c:";

struct bo_info {
	void *bo;
	u64 bo_id;
	s32 count_planes;
	s32 count_fds;
	s32 fds[4];
	void *maps[4];
	u32 width, height;
	u32 pitches[4];
	u32 offsets[4];
	u32 sizes[4];
	struct list_head link;
};

struct cube_client {
	struct cb_client *cli;
	s32 count_bos;
	struct bo_info bos[BO_NR];

	bool use_dmabuf;
	bool composed;
	s32 x, y;
	u32 width, height;
	u32 hstride, vstride;
	enum cb_pix_fmt pix_fmt;

	s32 dev_fd;

	s32 zpos;

	bool commit_ack_received ;

	s32 pipe_locked;

	void *signal_handler;

	struct cb_surface_info s;
	struct cb_view_info v;

	void *repaint_timer;
	void *collect_timer;

	u32 last_frame_cnt, frame_cnt;
	u32 last_drop_cnt, drop_cnt;

	struct list_head free_bos;

	bool replace_flag;

	bool run_background;
};

static s32 signal_cb(s32 signal_number, void *userdata)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;

	printf("receive signal\n");
	cli->stop(cli);
	return 0;
}

static void fill_argb_colorbar(u8 *data, u32 width, u32 height, u32 stride)
{
	const u32 ccolors[] = {
		0xFFFFFFFF, /* white */
		0xFF00FFFF, /* yellow */
		0xFFFFFF00, /* cyan */
		0xFF00FF00, /* green */
		0xFFFF00FF, /* perple */
		0xFF0000FF, /* red */
		0xFF000000, /* black */
		0xFFFF0000, /* blue */
	};

	s32 i, j;
	u32 bar;
	u32 interval = width / ARRAY_SIZE(ccolors);
	u32 *pixel = (u32 *)data;
	static u32 delta = 0;

	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			bar = (j + delta) / interval;
			bar %= ARRAY_SIZE(ccolors);
			pixel[j] = ccolors[bar];
		}
		pixel += (stride >> 2);
	}
	delta += 2;
	if (delta >= width)
		delta = 0;
}

static void fill_argb_colorbar_m(u8 *data, u32 width, u32 height, u32 stride)
{
	const u32 ccolors[] = {
		0xFFFFFFFF, /* white */
		0xFF00FFFF, /* yellow */
		0xFFFFFF00, /* cyan */
		0xFF00FF00, /* green */
		0xFFFF00FF, /* perple */
		0xFF0000FF, /* red */
		0xFF000000, /* black */
		0xFFFF0000, /* blue */
	};

	s32 i, j;
	u32 bar;
	u32 interval = width / ARRAY_SIZE(ccolors);
	u32 *pixel = (u32 *)data + (stride >> 2) * height / 8 * 3;
	static u32 delta = 2;

	for (i = 0; i < height / 8 * 2; i++) {
		for (j = 0; j < width; j++) {
			bar = (j + delta) / interval;
			bar %= ARRAY_SIZE(ccolors);
			pixel[j] = ccolors[bar];
		}
		pixel += (stride >> 2);
	}
	delta += 2;
	if (delta >= width)
		delta = 0;
}

static void fill_nv12(u8 *data, u32 width, u32 height, u32 hstride, u32 vstride)
{
	static bool green = true;
	if (green) {
		memset(data, 0x00, hstride * height);
		memset(data + hstride * vstride, 0x00, hstride * height / 2);
		green = false;
	} else {
		memset(data, 0x7F, hstride * height);
		memset(data + hstride * vstride, 0x7F, hstride * height / 2);
		green = true;
	}
}

static void fill_nv16(u8 *data, u32 width, u32 height, u32 hstride, u32 vstride)
{
	static bool green = true;
	if (green) {
		memset(data, 0x00, hstride * height);
		memset(data + hstride * vstride, 0x00, hstride * height);
		green = false;
	} else {
		memset(data, 0x7F, hstride * height);
		memset(data + hstride * vstride, 0x7F, hstride * height);
		green = true;
	}
}

static struct bo_info *get_free_bo(struct cube_client *client)
{
	struct bo_info *bo, *bo_next;

	if (!client)
		return NULL;

	list_for_each_entry_safe(bo, bo_next, &client->free_bos, link) {
		list_del(&bo->link);
		return bo;
	}

	printf("[TEST_CLIENT] cannot find a free bo!!!!!\n");
	return NULL;
}

static void put_free_bo(struct cube_client *client, struct bo_info *bo)
{
	if (!client)
		return;

	if (!bo)
		return;

	list_add_tail(&bo->link, &client->free_bos);
}

static void bo_commited_cb(bool success, void *userdata, u64 bo_id,
			   u64 surface_id)
{
	struct cube_client *client = userdata;
	/* struct cb_client *cli = client->cli; */

	assert(surface_id == client->s.surface_id);
	if (bo_id == (u64)(-1)) {
		printf("[TEST_CLIENT][commit] failed to commit bo: %lX\n", bo_id);
		client->commit_ack_received = true;
	} else if (bo_id == COMMIT_REPLACE) {
		printf("[TEST_CLIENT][commit] replace last buffer\n");
		client->drop_cnt++;
		client->replace_flag = true;
		/* wait 2 flipped
		cli->timer_update(cli, client->repaint_timer, 0, 0);
		client->sync_cnt = 2;
		*/
	} else {
		printf("[TEST_CLIENT][commit] ok\n");
		client->replace_flag = false;
		client->commit_ack_received = true;
	}
}

static s32 collect_cb(void *userdata)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;

	printf("frame cnt: %d\n", client->frame_cnt - client->last_frame_cnt);
	printf("drop cnt: %d\n", client->drop_cnt - client->last_drop_cnt);
	client->last_frame_cnt = client->frame_cnt;
	client->last_drop_cnt = client->drop_cnt;
	cli->timer_update(cli, client->collect_timer, 1000, 0);
	return 0;
}

static s32 repaint_cb(void *userdata)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct cb_commit_info c;
	struct bo_info *bo_info;
	s32 ret;
	struct timespec now;
#if 0
	static bool first = true;

	if (first)
		first = false;
	else
		return 0;
#endif
	if (!client->commit_ack_received) {
		/* not received ack yet. */
		cli->timer_update(cli, client->repaint_timer, 2, 0);
		return 0;
	}

	bo_info = get_free_bo(client);
	if (!bo_info) {
		cli->timer_update(cli, client->repaint_timer, 16, 667);
		return -EINVAL;
	}

	if (client->replace_flag)
		usleep(7000);
	if (!client->run_background)
		cli->timer_update(cli, client->repaint_timer, 16, 667);
	
	if (client->use_dmabuf) {
		cb_client_dma_buf_bo_sync_begin(bo_info->bo);

		if (client->pix_fmt == CB_PIX_FMT_ARGB8888 ||
		    client->pix_fmt == CB_PIX_FMT_XRGB8888) {
			fill_argb_colorbar(bo_info->maps[0],
				   bo_info->width,
				   bo_info->height,
				   bo_info->pitches[0]);
		} else if (client->pix_fmt == CB_PIX_FMT_NV12) {
			fill_nv12(bo_info->maps[0],
				  bo_info->width,
				  bo_info->height,
				  bo_info->pitches[0],
				  bo_info->offsets[1] / bo_info->pitches[0]);
		} else if (client->pix_fmt == CB_PIX_FMT_NV16) {
			fill_nv16(bo_info->maps[0],
				  bo_info->width,
				  bo_info->height,
				  bo_info->pitches[0],
				  bo_info->offsets[1] / bo_info->pitches[0]);
		}

		cb_client_dma_buf_bo_sync_end(bo_info->bo);
	} else {
		if (client->pix_fmt == CB_PIX_FMT_ARGB8888 ||
		    client->pix_fmt == CB_PIX_FMT_XRGB8888) {
			fill_argb_colorbar_m(bo_info->maps[0],
				   bo_info->width,
				   bo_info->height,
				   bo_info->pitches[0]);
		}
	}

	c.bo_id = bo_info->bo_id;
	c.surface_id = client->s.surface_id;
	if (!client->use_dmabuf) {
		c.bo_damage.pos.x = 0;
		c.bo_damage.pos.y = client->height / 8 * 3;
		c.bo_damage.w = client->width;
		c.bo_damage.h = client->height / 8 * 2;
	} else {
		c.bo_damage.pos.x = 0;
		c.bo_damage.pos.y = 0;
		c.bo_damage.w = client->width;
		c.bo_damage.h = client->height;
	}
	c.view_x = client->x;
	c.view_y = client->y;
	c.pipe_locked = client->pipe_locked;
	c.view_width = client->width;
	c.view_height = client->height;

	clock_gettime(CLOCK_MONOTONIC, &now);
	printf("[TEST_CLIENT][%05lu:%06lu] commit bo: %lX\n",
	       now.tv_sec % 86400l, now.tv_nsec / 1000l, c.bo_id);
	client->commit_ack_received = false;
	ret = cli->commit_bo(cli, &c);
	if (ret < 0) {
		fprintf(stderr, "[TEST_CLIENT] failed to commit bo: %lX, "
			"ret: %d\n", c.bo_id, ret);
		cli->stop(cli);
	}

	return 0;
}

static void bo_flipped_cb(void *userdata, u64 bo_id, u64 surface_id)
{
	struct cube_client *client = userdata;
	/* struct cb_client *cli = client->cli; */
	struct timespec now;

	assert(surface_id == client->s.surface_id);
	clock_gettime(CLOCK_MONOTONIC, &now);
	printf("[TEST_CLIENT][%05lu:%06lu] receive bo flipped: %lX\n",
		now.tv_sec % 86400l, now.tv_nsec / 1000l, bo_id);
	client->frame_cnt++;
/*
	if (client->sync_cnt) {
		client->sync_cnt--;
		if (!client->sync_cnt) {
			cli->timer_update(cli, client->repaint_timer, 1, 0);
		}
	}
*/
}

static void bo_completed_cb(void *userdata, u64 bo_id, u64 surface_id)
{
	struct bo_info *bo;
	struct cube_client *client = userdata;
	s32 i;

	printf("[TEST_CLIENT] receive bo complete: %lX surface %lX\n", bo_id,
								surface_id);
	assert(surface_id == client->s.surface_id);
	for (i = 0; i < BO_NR; i++) {
		bo = &client->bos[i];
		if (bo_id == bo->bo_id) {
			put_free_bo(client, bo);
			return;
		}
	}

	printf("[TEST_CLIENT] failed to find bo %lX\n", bo_id);
	exit(1);
}

static void view_created_cb(bool success, void *userdata, u64 view_id)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct cb_commit_info c;
	s32 ret;
	struct bo_info *bo;
	struct timespec now;

	if (success) {
		printf("create view succesfull\n");
		client->v.view_id = view_id;
		cli->set_commit_bo_cb(cli, client, bo_commited_cb);
		cli->set_bo_flipped_cb(cli, client, bo_flipped_cb);
		cli->set_bo_completed_cb(cli, client, bo_completed_cb);

		bo = get_free_bo(client);
		if (!bo) {
			fprintf(stderr,"[TEST_CLIENT] failed to get free bo.\n");
			cli->stop(cli);
			return;
		}

		c.bo_id = bo->bo_id;
		c.surface_id = client->s.surface_id;
		c.bo_damage.pos.x = 0;
		c.bo_damage.pos.y = 0;
		c.bo_damage.w = client->width;
		c.bo_damage.h = client->height;
		c.view_x = client->x;
		c.view_y = client->y;
		c.view_width = client->width;
		c.view_height = client->height;
		c.pipe_locked = client->pipe_locked;

		clock_gettime(CLOCK_MONOTONIC, &now);
		printf("[TEST_CLIENT][%05lu:%06lu] commit bo: %lX\n",
		       now.tv_sec % 86400l, now.tv_nsec / 1000l, c.bo_id);
		ret = cli->commit_bo(cli, &c);
		if (ret < 0) {
			fprintf(stderr, "[TEST_CLIENT] failed to "
				"commit bo %lX\n", c.bo_id);
			cli->stop(cli);
		}
	} else {
		fprintf(stderr, "failed to create view.\n");
		cli->stop(cli);
	}
}

static void view_focus_chg_cb(void *userdata, u64 view_id, bool on)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;

	printf("[TEST_CLIENT] ------- View %16lX Focus %s --------\n", view_id,
		on ? "On" : "Lost");
	if (on) {
		client->run_background = false;
		cli->timer_update(cli, client->repaint_timer, 16, 667);
	} else {
		client->run_background = true;
	}
}

static void surface_created_cb(bool success, void *userdata, u64 surface_id)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	s32 ret;

	if (success) {
		printf("create surface succesfull\n");
		client->s.surface_id = surface_id;
		client->v.surface_id = surface_id;
		cli->set_view_focus_chg_cb(cli, client, view_focus_chg_cb);
		cli->set_create_view_cb(cli, client, view_created_cb);
		ret = cli->create_view(cli, &client->v);
		if (ret < 0) {
			fprintf(stderr, "failed to create view.\n");
			cli->stop(cli);
			return;
		}
	} else {
		fprintf(stderr, "failed to create surface.\n");
		cli->stop(cli);
	}
}

static void bo_created_cb(bool success, void *userdata, u64 bo_id)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct bo_info *bo_info;
	s32 ret;

	if (!success) {
		fprintf(stderr, "failed to create bo\n");
		cli->stop(cli);
		return;
	}

	bo_info = &client->bos[client->count_bos - 1];
	printf("bo_id: %016lX\n", bo_id);
	bo_info->bo_id = bo_id;

	if (client->count_bos == BO_NR) {
		printf("create bo complete. <<<\n");
		cli->set_create_surface_cb(cli, client, surface_created_cb);
		ret = cli->create_surface(cli, &client->s);
		if (ret < 0) {
			fprintf(stderr, "failed to create surface.\n");
			cli->stop(cli);
			return;
		}
	} else {
		bo_info = &client->bos[client->count_bos];
		printf("[TEST_CLIENT] create bo\n");
		ret = cli->create_bo(cli, bo_info->bo);
		if (ret < 0) {
			fprintf(stderr, "failed to create bo.\n");
			cli->stop(cli);
			return;
		}
		client->count_bos++;
	}
}

static void ready_cb(void *userdata)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct bo_info *bo_info = &client->bos[0];
	s32 ret;

	cli->set_create_bo_cb(cli, client, bo_created_cb);
	printf("[TEST_CLIENT] create bo\n");
	ret = cli->create_bo(cli, bo_info->bo);
	if (ret < 0) {
		fprintf(stderr, "failed to create bo.\n");
		cli->stop(cli);
		return;
	}
	client->count_bos++;
}

static void surface_info_fini(struct cube_client *client)
{
	if (!client)
		return;
}

static void surface_info_init(struct cube_client *client)
{
	if (!client)
		return;

	client->s.surface_id = 0;
	client->s.is_opaque = true;
	client->s.damage.pos.x = 0;
	client->s.damage.pos.y = 0;
	client->s.damage.w = client->width;
	client->s.damage.h = client->height;
	client->s.opaque.pos.x = 0;
	client->s.opaque.pos.y = 0;
	client->s.opaque.w = client->width;
	client->s.opaque.h = client->height;
	client->s.width = client->width;
	client->s.height = client->height;
}

static void view_info_fini(struct cube_client *client)
{
	if (!client)
		return;
	/* TODO */
}

static void view_info_init(struct cube_client *client)
{
	if (!client)
		return;

	client->v.view_id = 0;
	client->v.alpha = 1.0f;
	client->v.zpos = client->zpos;
	client->v.area.pos.x = client->x;
	client->v.area.pos.y = client->y;
	client->v.area.w = client->width;
	client->v.area.h = client->height;
	client->v.float_view = false;
}

static s32 client_init(struct cube_client *client)
{
	struct cb_client *cli;
	struct bo_info *bo_info;
	s32 i, j;

	client->dev_fd = cb_drm_device_open("/dev/dri/renderD128");

	INIT_LIST_HEAD(&client->free_bos);

	if (!client->use_dmabuf) {
		for (i = 0; i < BO_NR; i++) {
			bo_info = &client->bos[i];
			printf("create shm buffer\n");
			bo_info->bo = cb_client_shm_bo_create(
						client->pix_fmt,
						client->width,
						client->height,
						client->hstride,
						client->vstride,
						&bo_info->count_fds,
						&bo_info->count_planes,
						bo_info->fds,
						bo_info->maps,
						bo_info->pitches,
						bo_info->offsets,
						bo_info->sizes);
			if (!bo_info->bo) {
				fprintf(stderr, "failed to create shm bo\n");
				goto err_buf_alloc;
			}
			bo_info->width = client->width;
			bo_info->height = client->height;
			fill_argb_colorbar(bo_info->maps[0],
					   bo_info->width,
					   bo_info->height,
					   bo_info->pitches[0]);
			printf("count fds: %d\n", bo_info->count_fds);
			printf("fds[0]: %d\n", bo_info->fds[0]);
			printf("maps: %p %p %p %p\n",
				bo_info->maps[0],
				bo_info->maps[1],
				bo_info->maps[2],
				bo_info->maps[3]);
			printf("pitches: %u %u %u %u\n",
				bo_info->pitches[0],
				bo_info->pitches[1],
				bo_info->pitches[2],
				bo_info->pitches[3]);
			printf("offsets: %u %u %u %u\n",
				bo_info->offsets[0],
				bo_info->offsets[1],
				bo_info->offsets[2],
				bo_info->offsets[3]);
			printf("sizes: %u %u %u %u\n",
				bo_info->sizes[0],
				bo_info->sizes[1],
				bo_info->sizes[2],
				bo_info->sizes[3]);
			list_add_tail(&bo_info->link, &client->free_bos);
		}
	} else {
		for (i = 0; i < BO_NR; i++) {
			printf("create dma buffer\n");
			bo_info = &client->bos[i];
			bo_info->bo = cb_client_dma_buf_bo_create(
						client->dev_fd,
						client->pix_fmt,
						client->width,
						client->height,
						client->hstride,
						client->vstride,
						true,
						true,
						&bo_info->count_fds,
						&bo_info->count_planes,
						bo_info->fds,
						bo_info->maps,
						bo_info->pitches,
						bo_info->offsets,
						client->composed);
			if (!bo_info->bo) {
				fprintf(stderr, "failed to create dmabuf bo\n");
				goto err_buf_alloc;
			}
			printf("fds: %d %d %d %d\n",
				bo_info->fds[0],
				bo_info->fds[1],
				bo_info->fds[2],
				bo_info->fds[3]);
			printf("maps: %p %p %p %p\n",
				bo_info->maps[0],
				bo_info->maps[1],
				bo_info->maps[2],
				bo_info->maps[3]);
			printf("pitches: %u %u %u %u\n",
				bo_info->pitches[0],
				bo_info->pitches[1],
				bo_info->pitches[2],
				bo_info->pitches[3]);
			printf("offsets: %u %u %u %u\n",
				bo_info->offsets[0],
				bo_info->offsets[1],
				bo_info->offsets[2],
				bo_info->offsets[3]);
			printf("sizes: %u %u %u %u\n",
				bo_info->sizes[0],
				bo_info->sizes[1],
				bo_info->sizes[2],
				bo_info->sizes[3]);
			bo_info->width = client->width;
			bo_info->height = client->height;
			cb_client_dma_buf_bo_sync_begin(bo_info->bo);
			fill_argb_colorbar(bo_info->maps[0],
					   bo_info->width,
					   bo_info->height,
					   bo_info->pitches[0]);
			cb_client_dma_buf_bo_sync_end(bo_info->bo);
			list_add_tail(&bo_info->link, &client->free_bos);
		}
	}

	client->cli = cb_client_create(0);
	if (!client->cli)
		goto err_buf_alloc;

	cli = client->cli;
	client->signal_handler = cli->add_signal_handler(cli, client,
							 SIGINT,
							 signal_cb);
	client->repaint_timer = cli->add_timer_handler(cli, client,
						       repaint_cb);
	client->collect_timer = cli->add_timer_handler(cli, client,
						       collect_cb);
	cli->timer_update(cli, client->collect_timer, 1000, 0);
	client->run_background = false;
	cli->timer_update(cli, client->repaint_timer, 35, 0);
	cli->set_ready_cb(cli, client, ready_cb);

	surface_info_init(client);
	view_info_init(client);

	return 0;

err_buf_alloc:
	if (!client->use_dmabuf) {
		for (j = 0; j < i; j++) {
			bo_info = &client->bos[j];
			list_del(&bo_info->link);
			if (bo_info->bo)
				cb_client_shm_bo_destroy(bo_info->bo);
			client->bos[j].bo = NULL;
		}
	} else {
		for (j = 0; j < i; j++) {
			bo_info = &client->bos[j];
			list_del(&bo_info->link);
			if (bo_info->bo)
				cb_client_dma_buf_bo_destroy(bo_info->bo);
			client->bos[j].bo = NULL;
		}
	}
	return -1;
}

static void client_fini(struct cube_client *client)
{
	struct cb_client *cli;
	struct bo_info *bo_info;
	s32 i;

	if (!client)
		return;

	cli = client->cli;
	if (!cli)
		return;

	surface_info_fini(client);
	view_info_fini(client);

	while (client->count_bos) {
		printf("destroy bo\n");
		list_del(&client->bos[client->count_bos - 1].link);
		cli->destroy_bo(cli,
				client->bos[client->count_bos - 1].bo_id);
		client->count_bos--;
	}

	cli->rm_handler(cli, client->repaint_timer);
	cli->rm_handler(cli, client->signal_handler);
	cli->rm_handler(cli, client->collect_timer);

	cli->destroy(cli);

	if (!client->use_dmabuf) {
		for (i = 0; i < BO_NR; i++) {
			bo_info = &client->bos[i];
			if (bo_info->bo)
				cb_client_shm_bo_destroy(bo_info->bo);
			client->bos[i].bo = NULL;
		}
	} else {
		for (i = 0; i < BO_NR; i++) {
			bo_info = &client->bos[i];
			if (bo_info->bo)
				cb_client_dma_buf_bo_destroy(bo_info->bo);
			client->bos[i].bo = NULL;
		}
	}

	if (client->dev_fd)
		cb_drm_device_close(client->dev_fd);
}

s32 main(s32 argc, char **argv)
{
	s32 ch;
	struct cube_client *client;
	u32 fourcc;
	struct cb_client *cli;

	client = calloc(1, sizeof(*client));
	if (!client)
		return -ENOMEM;

	client->zpos = -1;
	client->pipe_locked = -1;
	client->composed = false;
	while ((ch = getopt_long(argc, argv, short_options,
				 options, NULL)) != -1) {
		switch (ch) {
		case 'x':
			client->x = atoi(optarg);
			break;
		case 'y':
			client->y = atoi(optarg);
			break;
		case 't':
			if (!strcmp(optarg, "dma"))
				client->use_dmabuf = true;
			break;
		case 'w':
			client->width = atoi(optarg);
			break;
		case 'h':
			client->height = atoi(optarg);
			break;
		case 'p':
			client->hstride = atoi(optarg);
			break;
		case 'v':
			client->vstride = atoi(optarg);
			break;
		case 'f':
			fourcc = mk_fourcc(optarg[0], optarg[1],
					   optarg[2], optarg[3]);
			client->pix_fmt = fourcc_to_cb_pix_fmt(fourcc);
			if (client->pix_fmt == CB_PIX_FMT_UNKNOWN) {
				printf("unknown fourcc [%4.4s], exit\n",
					(char *)&fourcc);
				return -1;
			}
			break;
		case 'z':
			client->zpos = atoi(optarg);
			break;
		case 'l':
			client->pipe_locked = atoi(optarg);
			break;
		case 'c':
			if (!strcmp(optarg, "Y"))
				client->composed = true;
			else
				client->composed = false;
			break;
		default:
			usage();
			free(client);
			return -1;
		}
	}

	if (!client->use_dmabuf)
		client->zpos = -1;
	printf("Buffer type: %s\n", client->use_dmabuf ? "DMA-BUF" : "SHM");
	printf("Pixel format: %4.4s\n", (char *)&fourcc);
	printf("Pos %d, %d\n", client->x, client->y);
	printf("Size %ux%u %ux%u\n", client->width, client->height,
		client->hstride, client->vstride);
	printf("ZPOS: %d\n", client->zpos);
	printf("Pipe locked: %c [%d]\n", client->pipe_locked == -1 ? 'N' : 'Y',
			client->pipe_locked);
	printf("Composed: %c\n", client->composed ? 'Y' : 'N');

	if (client_init(client) < 0)
		goto out;
	cli = client->cli;

	cli->run(cli);
out:
	usleep(5000);
	client_fini(client);
	free(client);
	return 0;
}

