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

static struct option options[] = {
	{"picture", 1, NULL, 'p'},
	{"seat", 1, NULL, 's'},
	{NULL, 0, NULL, 0},
};

static char short_options[] = "p:s:";

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

struct head {
	s32 index;
	struct cb_rect rc;
	s32 pipe;
	bool enabled;
};

struct cube_desktop {
	bool duplicated;
	struct cb_client *cli;
	s32 count_bos;
	struct bo_info *bos[2];
	s32 bo_cur;

	s32 heads_nr;
	struct head heads[4];

	struct cb_rect desktop_rc;

	void *signal_handler;

	struct cb_surface_info s;
	struct cb_view_info v;

	s32 pipe;
};

static void usage(void)
{
	fprintf(stderr, "cube_desktop --seat seat number "
			"--picture background picture file.\n");
}

static void cube_desktop_destroy(struct cube_desktop *desktop)
{
	s32 i;
	struct bo_info *bo_info;
	struct cb_client *cli;

	if (!desktop)
		return;

	cli = desktop->cli;
	if (cli) {
		for (i = 0; i < 2; i++) {
			bo_info = desktop->bos[i];
			if (!bo_info)
				continue;
			if (bo_info->bo) {
				if (bo_info->bo_id) {
					printf("destroy bo\n");
					cli->destroy_bo(cli, bo_info->bo_id);
				}
				printf("destroy shm bo\n");
				cb_client_shm_bo_destroy(bo_info->bo);
			}
			free(bo_info);
		}

		if (desktop->signal_handler)
			cli->rm_handler(cli, desktop->signal_handler);
	}
	usleep(20000);

	free(desktop);
}

static s32 signal_cb(s32 signal_number, void *userdata)
{
	struct cube_desktop *desktop = userdata;
	struct cb_client *cli = desktop->cli;

	cli->stop(cli);
	return 0;
}

static void fill_def_bg(u8 *data, u32 width, u32 height, u32 stride,
			struct cb_pos *offset)
{
	s32 i, j;
	u32 interval = 96;
	u32 *pixel = (u32 *)data + offset->y * (stride >> 2) + offset->x;
	bool white;

	for (i = 0; i < height; i++) {
		if (!(i % interval))
			white = true;
		else
			white = false;
		for (j = 0; j < width; j++) {
			if (white) {
				if ((j % 6) < 3)
					pixel[j] = 0xFFFFFFFF;
				else
					pixel[j] = 0xFF7F7F7F;
			} else {
				if (!(j % interval)) {
					if ((i % 6) < 3)
						pixel[j] = 0xFFFFFFFF;
					else
						pixel[j] = 0xFF7F7F7F;
				} else {
					pixel[j] = 0xFF7F7F7F;
				}
			}
		}
		pixel += (stride >> 2);
	}
}

static void update_desktop_rc(struct cube_desktop *desktop)
{
	s32 i;
	s32 min_x, max_x;
	s32 min_y, max_y;

	if (!desktop)
		return;

	if (desktop->duplicated) {
		memcpy(&desktop->desktop_rc, &desktop->heads[0].rc,
		       sizeof(struct cb_rect));
	} else {
		min_x = 0;
		min_y = 0;
		max_x = 0;
		max_y = 0;
		for (i = 0; i < desktop->heads_nr; i++) {
			if (desktop->heads[i].rc.pos.x < min_x) {
				min_x = desktop->heads[i].rc.pos.x;
			}
			if (desktop->heads[i].rc.pos.y < min_y) {
				min_y = desktop->heads[i].rc.pos.y;
			}
			if ((desktop->heads[i].rc.pos.x
					+ desktop->heads[i].rc.w) >= max_x) {
				max_x = desktop->heads[i].rc.pos.x
						 + desktop->heads[i].rc.w;
			}
			if ((desktop->heads[i].rc.pos.y
					+ desktop->heads[i].rc.h) >= max_y) {
				max_y = desktop->heads[i].rc.pos.y
						 + desktop->heads[i].rc.h;
			}
		}
		desktop->desktop_rc.pos.x = min_x;
		desktop->desktop_rc.pos.y = min_y;
		desktop->desktop_rc.w = max_x - min_x;
		desktop->desktop_rc.h = max_y - min_y;
	}
	printf("Desktop RC: %d,%d %ux%u\n",
		desktop->desktop_rc.pos.x,
		desktop->desktop_rc.pos.y,
		desktop->desktop_rc.w,
		desktop->desktop_rc.h);
}

static void layout_changed_cb(void *userdata)
{
	struct cube_desktop *desktop = userdata;
	struct cb_client *cli = desktop->cli;
	s32 i, j, ret;
	struct cb_client_display *disp;
	struct bo_info *bo_info;

	printf("Layout changed:\n");
	for (i = 0; i < cli->count_displays; i++) {
		disp = &cli->displays[i];
		for (j = 0; j < desktop->heads_nr; j++) {
			if (desktop->heads[j].pipe != disp->pipe) {
				continue;
			}
			break;
		}
		if (j == desktop->heads_nr) {
			fprintf(stderr, "cannot find pipe %d\n", disp->pipe);
			break;
		}
		memcpy(&desktop->heads[j].rc, &disp->desktop_rc,
		       sizeof(struct cb_rect));
		desktop->heads[j].enabled = disp->enabled;
		printf("Index: %d, Pipe: %d, Rect: %d,%d %ux%u, Enabled: %c\n",
			desktop->heads[j].index, desktop->heads[j].pipe,
			desktop->heads[j].rc.pos.x, desktop->heads[j].rc.pos.y,
			desktop->heads[j].rc.w, desktop->heads[j].rc.h,
			desktop->heads[j].enabled ? 'Y' : 'N');
	}

	for (i = 1; i < desktop->heads_nr; i++) {
		if (memcmp(&desktop->heads[i].rc,
			   &desktop->heads[0].rc,
			    sizeof(struct cb_rect))) {
			break;
		}
	}

	if (i < desktop->heads_nr) {
		desktop->duplicated = false;
		printf("Extended mode.\n");
	} else {
		desktop->duplicated = true;
		printf("Duplicated mode.\n");
	}
	update_desktop_rc(desktop);

	printf("switch bo cur\n");
	desktop->bo_cur = 1 - desktop->bo_cur;
	bo_info = desktop->bos[desktop->bo_cur];
	bo_info->bo = cb_client_shm_bo_create(CB_PIX_FMT_ARGB8888,
					desktop->desktop_rc.w,
					desktop->desktop_rc.h,
					0,
					0,
					&bo_info->count_fds,
					&bo_info->count_planes,
					bo_info->fds,
					bo_info->maps,
					bo_info->pitches,
					bo_info->offsets,
					bo_info->sizes);
	if (!bo_info->bo) {
		fprintf(stderr, "failed to create shm bo\n");
		goto err;
	}
	bo_info->width = desktop->desktop_rc.w;
	bo_info->height = desktop->desktop_rc.h;
	if (desktop->duplicated) {
		printf("Fill %d,%d %ux%u\n", desktop->desktop_rc.pos.x,
			desktop->desktop_rc.pos.y,
			bo_info->width, bo_info->height);
		fill_def_bg(bo_info->maps[0],
			   bo_info->width,
			   bo_info->height,
			   bo_info->pitches[0],
			   &desktop->desktop_rc.pos);
	} else {
		for (i = 0; i < desktop->heads_nr; i++) {
			printf("Fill %d,%d %ux%u\n", desktop->heads[i].rc.pos.x,
				desktop->heads[i].rc.pos.y,
				desktop->heads[i].rc.w, desktop->heads[i].rc.h);
			fill_def_bg(bo_info->maps[0],
				desktop->heads[i].rc.w,
				desktop->heads[i].rc.h,
				bo_info->pitches[0],
				&desktop->heads[i].rc.pos);
		}
	}

	ret = cli->create_bo(cli, bo_info->bo);
	if (ret < 0) {
		fprintf(stderr, "failed to create bo.\n");
		goto err;
	}

	return;
err:
	if (bo_info->bo) {
		cb_client_shm_bo_destroy(bo_info->bo);
		memset(bo_info, 0, sizeof(*bo_info));
	}
}

static void surface_info_update(struct cube_desktop *desktop)
{
	if (!desktop)
		return;

	desktop->s.surface_id = 0;
	memcpy(&desktop->s.damage, &desktop->desktop_rc,sizeof(struct cb_rect));
	memcpy(&desktop->s.opaque, &desktop->desktop_rc,sizeof(struct cb_rect));
	desktop->s.width = desktop->desktop_rc.w;
	desktop->s.height = desktop->desktop_rc.h;
}

static void view_info_update(struct cube_desktop *desktop)
{
	if (!desktop)
		return;

	desktop->v.view_id = 0;
	desktop->v.alpha = 1.0f;
	desktop->v.zpos = 0;
	memcpy(&desktop->v.area, &desktop->desktop_rc, sizeof(struct cb_rect));
	desktop->v.float_view = false;
}

static void bo_flipped_cb(void *userdata, u64 bo_id, u64 surface_id)
{
	struct cube_desktop *desktop = userdata;

	assert(surface_id == desktop->s.surface_id);
	printf("receive bo flipped: %lX\n", bo_id);
}

static void bo_completed_cb(void *userdata, u64 bo_id, u64 surface_id)
{
	struct cube_desktop *desktop = userdata;

	printf("receive bo complete: %lX surface %lX\n", bo_id, surface_id);
	assert(surface_id == desktop->s.surface_id);
	//assert(desktop->bos[desktop->bo_cur]->bo_id == bo_id);
}

static void bo_commited_cb(bool success, void *userdata, u64 bo_id,
			   u64 surface_id)
{
	struct cube_desktop *desktop = userdata;

	assert(surface_id == desktop->s.surface_id);
	if (bo_id == (u64)(-1)) {
		printf("failed to commit bo: %lX\n", bo_id);
	} else if (bo_id == COMMIT_REPLACE) {
		printf("replace last buffer\n");
	} else {
		printf("commit ok\n");
	}
}

static void bo_created_cb(bool success, void *userdata, u64 bo_id)
{
	struct cube_desktop *desktop = userdata;
	struct cb_client *cli = desktop->cli;
	struct bo_info *bo_info;
	struct cb_commit_info c;
	s32 ret;

	if (!success) {
		fprintf(stderr, "failed to create bo\n");
		cli->stop(cli);
		return;
	}

	bo_info = desktop->bos[desktop->bo_cur];
	printf("bo_id: %016lX\n", bo_id);
	bo_info->bo_id = bo_id;
	cli->set_commit_bo_cb(cli, desktop, bo_commited_cb);
	cli->set_bo_flipped_cb(cli, desktop, bo_flipped_cb);
	cli->set_bo_completed_cb(cli, desktop, bo_completed_cb);

	c.bo_id = bo_info->bo_id;
	c.surface_id = desktop->s.surface_id;
	memcpy(&c.bo_damage, &desktop->desktop_rc, sizeof(struct cb_rect));
	memcpy(&c.bo_opaque, &desktop->desktop_rc, sizeof(struct cb_rect));
	printf("Damage: %d,%d %ux%u\n", c.bo_damage.pos.x, c.bo_damage.pos.y,
		c.bo_damage.w, c.bo_damage.h);
	c.view_x = desktop->desktop_rc.pos.x;
	c.view_y = desktop->desktop_rc.pos.y;
	c.view_width = desktop->desktop_rc.w;
	c.view_height = desktop->desktop_rc.h;
	c.pipe_locked = -1;
	ret = cli->commit_bo(cli, &c);
	if (ret < 0) {
		fprintf(stderr, "failed to commit bo %lX\n", c.bo_id);
		cli->stop(cli);
		return;
	}

	bo_info = desktop->bos[1 - desktop->bo_cur];
	if (bo_info->bo && bo_info->bo_id) {
		printf("Destroy old bo\n");
		cli->destroy_bo(cli, bo_info->bo_id);
		cb_client_shm_bo_destroy(bo_info->bo);
		memset(bo_info, 0, sizeof(*bo_info));
	}
}

static void view_created_cb(bool success, void *userdata, u64 view_id)
{
	struct cube_desktop *desktop = userdata;
	struct cb_client *cli = desktop->cli;
	struct bo_info *bo_info;
	s32 i, ret;

	if (success) {
		printf("create view succesfull\n");
		desktop->v.view_id = view_id;
	} else {
		fprintf(stderr, "failed to create view.\n");
		goto err;
	}

	desktop->bo_cur = 0;
	for (i = 0; i < 2; i++) {
		desktop->bos[i] = calloc(1, sizeof(struct bo_info));
		if (!desktop->bos[i])
			goto err;
	}
	bo_info = desktop->bos[desktop->bo_cur];
	bo_info->bo = cb_client_shm_bo_create(CB_PIX_FMT_ARGB8888,
					desktop->desktop_rc.w,
					desktop->desktop_rc.h,
					0,
					0,
					&bo_info->count_fds,
					&bo_info->count_planes,
					bo_info->fds,
					bo_info->maps,
					bo_info->pitches,
					bo_info->offsets,
					bo_info->sizes);
	if (!bo_info->bo) {
		fprintf(stderr, "failed to create shm bo\n");
		goto err;
	}
	bo_info->width = desktop->desktop_rc.w;
	bo_info->height = desktop->desktop_rc.h;
	if (desktop->duplicated) {
		printf("Fill %d,%d %ux%u\n", desktop->desktop_rc.pos.x,
			desktop->desktop_rc.pos.y,
			bo_info->width, bo_info->height);
		fill_def_bg(bo_info->maps[0],
			   bo_info->width,
			   bo_info->height,
			   bo_info->pitches[0],
			   &desktop->desktop_rc.pos);
	} else {
		for (i = 0; i < desktop->heads_nr; i++) {
			printf("Fill %d,%d %ux%u\n", desktop->heads[i].rc.pos.x,
				desktop->heads[i].rc.pos.y,
				desktop->heads[i].rc.w, desktop->heads[i].rc.h);
			fill_def_bg(bo_info->maps[0],
				desktop->heads[i].rc.w,
				desktop->heads[i].rc.h,
				bo_info->pitches[0],
				&desktop->heads[i].rc.pos);
		}
	}

	cli->set_create_bo_cb(cli, desktop, bo_created_cb);
	ret = cli->create_bo(cli, bo_info->bo);
	if (ret < 0) {
		fprintf(stderr, "failed to create bo.\n");
		goto err;
	}

	return;
err:
	for (i = 0; i < 2; i++) {
		if (desktop->bos[i]) {
			free(desktop->bos[i]);
			desktop->bos[i] = NULL;
		}
	}
	cli->stop(cli);
}

static void surface_created_cb(bool success, void *userdata, u64 surface_id)
{
	struct cube_desktop *desktop = userdata;
	struct cb_client *cli = desktop->cli;
	s32 ret;

	if (success) {
		printf("create surface succesfull\n");
		desktop->s.surface_id = surface_id;
		desktop->v.surface_id = surface_id;
		cli->set_create_view_cb(cli, desktop, view_created_cb);
		ret = cli->create_view(cli, &desktop->v);
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

static void layout_query_cb(void *userdata)
{
	struct cube_desktop *desktop = userdata;
	struct cb_client *cli = desktop->cli;
	s32 i, ret;
	struct cb_client_display *disp;

	desktop->heads_nr = cli->count_displays;
	for (i = 0; i < cli->count_displays; i++) {
		disp = &cli->displays[i];
		desktop->heads[i].index = i;
		desktop->heads[i].pipe = disp->pipe;
		memcpy(&desktop->heads[i].rc, &disp->desktop_rc,
		       sizeof(struct cb_rect));
		desktop->heads[i].enabled = disp->enabled;
		printf("Index: %d, Pipe: %d, Rect: %d,%d %ux%u, Enabled: %c\n",
			i, desktop->heads[i].pipe,
			desktop->heads[i].rc.pos.x, desktop->heads[i].rc.pos.y,
			desktop->heads[i].rc.w, desktop->heads[i].rc.h,
			desktop->heads[i].enabled ? 'Y' : 'N');
	}

	for (i = 1; i < desktop->heads_nr; i++) {
		if (memcmp(&desktop->heads[i].rc,
			   &desktop->heads[0].rc,
			    sizeof(struct cb_rect))) {
			break;
		}
	}
	if (i < desktop->heads_nr) {
		desktop->duplicated = false;
		printf("Extended mode.\n");
	} else {
		desktop->duplicated = true;
		printf("Duplicated mode.\n");
	}
	update_desktop_rc(desktop);

	surface_info_update(desktop);
	view_info_update(desktop);
	cli->set_create_surface_cb(cli, desktop, surface_created_cb);
	ret = cli->create_surface(cli, &desktop->s);
	if (ret < 0) {
		fprintf(stderr, "failed to create surface.\n");
		cli->stop(cli);
		return;
	}
}

static void ready_cb(void *userdata)
{
	struct cube_desktop *desktop = userdata;
	struct cb_client *cli = desktop->cli;

	cli->set_layout_query_cb(cli, desktop, layout_query_cb);
	cli->set_layout_changed_cb(cli, desktop, layout_changed_cb);
	cli->set_client_cap(cli, CB_CLIENT_CAP_NOTIFY_LAYOUT);
	cli->query_layout(cli);
}

static struct cube_desktop *cube_desktop_create(s32 seat)
{
	struct cube_desktop *desktop;
	struct cb_client *cli;

	desktop = calloc(1, sizeof(*desktop));
	if (!desktop)
		goto err;

	desktop->cli = cb_client_create(seat);
	cli = desktop->cli;

	cli->set_ready_cb(cli, desktop, ready_cb);
	desktop->signal_handler = cli->add_signal_handler(cli, desktop,
							  SIGINT,
							  signal_cb);

	return desktop;
err:
	cube_desktop_destroy(desktop);
	return NULL;
}

s32 main(s32 argc, char *argv[])
{
	s32 ch, seat = 0;
	char *pic_path = NULL;
	struct cube_desktop *desktop;

	while ((ch = getopt_long(argc, argv, short_options,
				 options, NULL)) != -1) {
		switch (ch) {
		case 's':
			seat = atoi(optarg);
			break;
		case 'p':
			pic_path = (char *)malloc(strlen(optarg) + 1);
			memset(pic_path, 0, strlen(optarg) + 1);
			strcpy(pic_path, optarg);
			break;
		default:
			usage();
			return -1;
		}
	}

	desktop = cube_desktop_create(seat);
	if (!desktop)
		goto out;

	desktop->cli->run(desktop->cli);

out:
	cube_desktop_destroy(desktop);
	free(pic_path);

	return 0;
}

