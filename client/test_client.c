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

static void usage(void)
{
	fprintf(stderr, "test_client --type dma/shm --x x_pos --y y_pos "
			"--width w --height h "
			"--hstride hs --vstride vs --pixel-fmt fourcc\n");
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
	{NULL, 0, NULL, 0},
};

static char short_options[] = "t:x:y:w:h:p:v:f:z:";

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
};

struct cube_client {
	struct cb_client *cli;
	s32 count_bos;
	struct bo_info bos[2];
	s32 work_bo;

	bool use_dmabuf;
	s32 x, y;
	u32 width, height;
	u32 hstride, vstride;
	enum cb_pix_fmt pix_fmt;

	s32 dev_fd;

	s32 zpos;

	void *signal_handler;

	struct cb_surface_info s;
	struct cb_view_info v;

	void *timeout_timer;
	void *collect_timer;

	u32 last_frame_cnt, frame_cnt;
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

	static u32 colors[] = {
		0xFFFFFFFF, /* white */
		0xFF00FFFF, /* yellow */
		0xFFFFFF00, /* cyan */
		0xFF00FF00, /* green */
		0xFFFF00FF, /* perple */
		0xFF0000FF, /* red */
		0xFF000000, /* black */
		0xFFFF0000, /* blue */
	};

	const u32 delta[] = {
		0x010101, /* white */
		0x000101, /* yellow */
		0x010100, /* cyan */
		0x000100, /* green */
		0x010001, /* perple */
		0x000001, /* red */
		0x000000, /* black */
		0x010000, /* blue */
	};

	s32 i, j;
	u32 bar;
	u32 interval = width / ARRAY_SIZE(colors);
	u32 *pixel = (u32 *)data;

	for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			bar = j / interval;
			pixel[j] = colors[bar] - delta[bar];
		}
		pixel += (stride >> 2);
	}

	for (i = 0; i < ARRAY_SIZE(colors); i++) {
		colors[i] -= delta[i];
		if (colors[i] == 0xFF000000)
			colors[i] = ccolors[i];
	}
}

static void bo_commited_cb(bool success, void *userdata, u64 bo_id)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;

	if (bo_id == (u64)(-1)) {
		printf("failed to commit bo\n");
		cli->stop(cli);
	} else {
		cli->timer_update(cli, client->timeout_timer, 500, 0);
	}
}

static s32 collect_cb(void *userdata)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;

	printf("frame cnt: %d\n", client->frame_cnt - client->last_frame_cnt);
	client->last_frame_cnt = client->frame_cnt;
	cli->timer_update(cli, client->collect_timer, 1000, 0);
	return 0;
}

static s32 timeout_cb(void *userdata)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct cb_commit_info c;
	s32 ret;

	printf("timeout\n");

	c.bo_id = client->bos[1 - client->work_bo].bo_id;
	c.surface_id = client->s.surface_id;
	if (!client->use_dmabuf) {
		c.bo_damage.pos.x = client->width / 4;
		c.bo_damage.pos.y = client->height / 4;
		c.bo_damage.w = client->width / 2;
		c.bo_damage.h = client->height / 2;
	} else {
		c.bo_damage.pos.x = 0;
		c.bo_damage.pos.y = 0;
		c.bo_damage.w = client->width;
		c.bo_damage.h = client->height;
	}
	c.view_x = client->x;
	c.view_y = client->y;
	c.view_width = client->width;
	c.view_height = client->height;

	ret = cli->commit_bo(cli, &c);
	if (ret < 0) {
		fprintf(stderr, "failed to commit bo %s\n", __func__);
		cli->stop(cli);
	}

	return 0;
}

static void bo_flipped_cb(void *userdata, u64 bo_id)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct cb_commit_info c;
	struct bo_info *bo_info;
	s32 ret;

	client->frame_cnt++;
	cli->timer_update(cli, client->timeout_timer, 500, 0);
	client->work_bo = 1 - client->work_bo;

	c.bo_id = client->bos[1 - client->work_bo].bo_id;
	c.surface_id = client->s.surface_id;
	if (!client->use_dmabuf) {
		c.bo_damage.pos.x = client->width / 4;
		c.bo_damage.pos.y = client->height / 4;
		c.bo_damage.w = client->width / 2;
		c.bo_damage.h = client->height / 2;
	} else {
		c.bo_damage.pos.x = 0;
		c.bo_damage.pos.y = 0;
		c.bo_damage.w = client->width;
		c.bo_damage.h = client->height;
	}
	c.view_x = client->x;
	c.view_y = client->y;
	c.view_width = client->width;
	c.view_height = client->height;

	ret = cli->commit_bo(cli, &c);
	if (ret < 0) {
		fprintf(stderr, "failed to commit bo %s\n", __func__);
		cli->stop(cli);
	}
	bo_info = &client->bos[client->work_bo];
	if (client->use_dmabuf)
		cb_client_dma_buf_bo_sync_begin(bo_info->bo);
	fill_argb_colorbar(bo_info->maps[0],
			   bo_info->width,
			   bo_info->height,
			   bo_info->pitches[0]);
	if (client->use_dmabuf)
		cb_client_dma_buf_bo_sync_end(bo_info->bo);
}

static void bo_completed_cb(void *userdata, u64 bo_id)
{
	
}

static void view_created_cb(bool success, void *userdata, u64 view_id)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct cb_commit_info c;
	s32 ret;

	if (success) {
		printf("create view succesfull\n");
		client->v.view_id = view_id;
		client->work_bo = 0;
		cli->set_commit_bo_cb(cli, client, bo_commited_cb);
		cli->set_bo_flipped_cb(cli, client, bo_flipped_cb);
		cli->set_bo_completed_cb(cli, client, bo_completed_cb);

		c.bo_id = client->bos[1 - client->work_bo].bo_id;
		c.surface_id = client->s.surface_id;
		c.bo_damage.pos.x = 0;
		c.bo_damage.pos.y = 0;
		c.bo_damage.w = client->width;
		c.bo_damage.h = client->height;
		c.view_x = client->x;
		c.view_y = client->y;
		c.view_width = client->width;
		c.view_height = client->height;

		ret = cli->commit_bo(cli, &c);
		if (ret < 0) {
			fprintf(stderr, "failed to commit bo %s\n", __func__);
			cli->stop(cli);
		}
	} else {
		fprintf(stderr, "failed to create view.\n");
		cli->stop(cli);
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
		ret = cli->create_view(cli, &client->v);
		if (ret < 0) {
			fprintf(stderr, "failed to create view.\n");
			cli->stop(cli);
			return;
		}
		cli->set_create_view_cb(cli, client, view_created_cb);
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

	if (client->count_bos == 2) {
		printf("create bo complete.\n");
		ret = cli->create_surface(cli, &client->s);
		if (ret < 0) {
			fprintf(stderr, "failed to create surface.\n");
			cli->stop(cli);
			return;
		}
		cli->set_create_surface_cb(cli, client, surface_created_cb);
	} else {
		while (client->count_bos < 2) {
			bo_info = &client->bos[client->count_bos];
			ret = cli->create_bo(cli, bo_info->bo);
			if (ret < 0) {
				fprintf(stderr, "failed to create bo.\n");
				cli->stop(cli);
				return;
			}
			client->count_bos++;
		}
	}
}

static void ready_cb(void *userdata)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct bo_info *bo_info = &client->bos[0];
	s32 ret;

	cli->set_create_bo_cb(cli, client, bo_created_cb);
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
}

static s32 client_init(struct cube_client *client)
{
	struct cb_client *cli;
	char name[64];
	struct bo_info *bo_info;
	s32 i, j;

	client->dev_fd = cb_drm_device_open("/dev/dri/renderD128");

	if (!client->use_dmabuf) {
		for (i = 0; i < 2; i++) {
			sprintf(name, "test_client-%d-%d", getpid(), i);
			bo_info = &client->bos[i];
			printf("create shm buffer\n");
			bo_info->bo = cb_client_shm_bo_create(
						name,
						client->pix_fmt,
						client->width,
						client->height,
						client->hstride,
						client->vstride,
						&bo_info->count_planes,
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
		}
	} else {
		for (i = 0; i < 2; i++) {
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
						bo_info->offsets);
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
		}
	}

	client->cli = cb_client_create(0);
	if (!client->cli)
		goto err_buf_alloc;

	cli = client->cli;
	client->signal_handler = cli->add_signal_handler(cli, client,
							 SIGINT,
							 signal_cb);
	client->timeout_timer = cli->add_timer_handler(cli, client,
						       timeout_cb);
	client->collect_timer = cli->add_timer_handler(cli, client,
						       collect_cb);
	cli->timer_update(cli, client->collect_timer, 1000, 0);
	cli->set_ready_cb(cli, client, ready_cb);

	surface_info_init(client);
	view_info_init(client);

	return 0;

err_buf_alloc:
	if (!client->use_dmabuf) {
		for (j = 0; j < i; j++) {
			bo_info = &client->bos[j];
			if (bo_info->bo)
				cb_client_shm_bo_destroy(bo_info->bo);
			client->bos[j].bo = NULL;
		}
	} else {
		for (j = 0; j < i; j++) {
			bo_info = &client->bos[j];
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
		cli->destroy_bo(cli,
				client->bos[client->count_bos - 1].bo_id);
		client->count_bos--;
	}

	cli->rm_handler(cli, client->signal_handler);
	cli->rm_handler(cli, client->timeout_timer);
	cli->rm_handler(cli, client->collect_timer);

	cli->destroy(cli);

	if (!client->use_dmabuf) {
		for (i = 0; i < 2; i++) {
			bo_info = &client->bos[i];
			if (bo_info->bo)
				cb_client_shm_bo_destroy(bo_info->bo);
			client->bos[i].bo = NULL;
		}
	} else {
		for (i = 0; i < 2; i++) {
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

