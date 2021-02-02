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
#include <fcntl.h>
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
	{"logo", 1, NULL, 'l'},
	{"seat", 1, NULL, 's'},
	{NULL, 0, NULL, 0},
};

#define FILE_PATH_LEN 512

struct logo_desc {
	char file_path[FILE_PATH_LEN];
	u32 offs;
	u32 width;
	s32 height;
	u8 *vaddr;
	bool y_invert;
	u16 planes;
	u16 count_bits;
	u32 compression;
	u32 size;
	s32 fd;
};

static char short_options[] = "l:s:";

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
	struct logo_desc *logo;

	bool create_bo_pending;
};

static void usage(void)
{
	fprintf(stderr, "cube_desktop --seat seat number "
			"--logo 24 bit (.BMP) logo picture file "
			"(centerized).\n");
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

	if (desktop->logo && desktop->logo->vaddr)
		free(desktop->logo->vaddr);
	free(desktop);
}

static s32 signal_cb(s32 signal_number, void *userdata)
{
	struct cube_desktop *desktop = userdata;
	struct cb_client *cli = desktop->cli;

	cli->stop(cli);
	return 0;
}

static void fill_logo(u8 *data, u32 width, u32 height, u32 stride,
		      struct cb_pos *offset, struct logo_desc *logo)
{
	s32 i, j;
	u32 *pixel = (u32 *)data + offset->y * (stride >> 2) + offset->x;
	u8 *src;
	u8 *dst;
	u32 h = logo->height;
	u32 w = logo->width;
	u32 v_offs = (height - h) / 2;
	u32 h_offs = (width - w) / 2;

	dst = (u8 *)pixel + v_offs * stride + h_offs * 4;
	if (logo->y_invert) {
		src = logo->vaddr + logo->size - logo->width * 3;
		for (i = 0; i < h; i++) {
			for (j = 0; j < w; j++) {
				*(dst + j * 4) = *(src + j * 3);
				*(dst + j * 4 + 1) = *(src + j * 3 + 1);
				*(dst + j * 4 + 2) = *(src + j * 3 + 2);
				*(dst + j * 4 + 3) = 0xFF;
			}
			src -= logo->width * 3;
			dst += stride;
		}
	} else {
		src = logo->vaddr;
		for (i = 0; i < h; i++) {
			for (j = 0; j < w; j++) {
				*(dst + j * 4) = *(src + j * 3);
				*(dst + j * 4 + 1) = *(src + j * 3 + 1);
				*(dst + j * 4 + 2) = *(src + j * 3 + 2);
				*(dst + j * 4 + 3) = 0xFF;
			}
			src += logo->width * 3;
			dst += stride;
		}
	}
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

static void hpd_cb(void *userdata, struct cb_connector_info *info)
{
	struct cube_desktop *desktop = userdata;
	struct cb_client *cli = desktop->cli;
	struct cb_commit_info c;
	struct bo_info *bo_info;
	s32 ret;

	printf("HPD happended, reflush\n");
	bo_info = desktop->bos[desktop->bo_cur];

	c.bo_id = bo_info->bo_id;
	c.surface_id = desktop->s.surface_id;
	memcpy(&c.bo_damage, &desktop->desktop_rc, sizeof(struct cb_rect));
	memcpy(&c.bo_opaque, &desktop->desktop_rc, sizeof(struct cb_rect));
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
	if (desktop->create_bo_pending) {
		printf("There is a create bo pending state already.\n");
		return;
	}
	printf("switch bo cur\n");
	desktop->bo_cur = 1 - desktop->bo_cur;
	bo_info = desktop->bos[desktop->bo_cur];
	if (bo_info->bo && bo_info->bo_id) {
		printf("Destroy old bo directly\n");
		cli->destroy_bo(cli, bo_info->bo_id);
		cb_client_shm_bo_destroy(bo_info->bo);
		memset(bo_info, 0, sizeof(*bo_info));
	}
	desktop->create_bo_pending = true;
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
	memset(bo_info->maps[0], 0, bo_info->sizes[0]);
	bo_info->width = desktop->desktop_rc.w;
	bo_info->height = desktop->desktop_rc.h;
	if (desktop->duplicated) {
		printf("Fill %d,%d %ux%u\n", desktop->desktop_rc.pos.x,
			desktop->desktop_rc.pos.y,
			bo_info->width, bo_info->height);
		if (desktop->logo) {
			fill_logo(bo_info->maps[0],
				bo_info->width,
				bo_info->height,
				bo_info->pitches[0],
				&desktop->desktop_rc.pos,
				desktop->logo);
		} else {
			fill_def_bg(bo_info->maps[0],
				bo_info->width,
				bo_info->height,
				bo_info->pitches[0],
				&desktop->desktop_rc.pos);
		}
	} else {
		for (i = 0; i < desktop->heads_nr; i++) {
			printf("Fill %d,%d %ux%u\n", desktop->heads[i].rc.pos.x,
				desktop->heads[i].rc.pos.y,
				desktop->heads[i].rc.w, desktop->heads[i].rc.h);
			if (desktop->logo) {
				fill_logo(bo_info->maps[0],
					desktop->heads[i].rc.w,
					desktop->heads[i].rc.h,
					bo_info->pitches[0],
					&desktop->heads[i].rc.pos,
					desktop->logo);
			} else {
				fill_def_bg(bo_info->maps[0],
					desktop->heads[i].rc.w,
					desktop->heads[i].rc.h,
					bo_info->pitches[0],
					&desktop->heads[i].rc.pos);
			}
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
	desktop->v.root_view = true;
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

	printf("Bo created.\n");
	desktop->create_bo_pending = false;
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
		memset(desktop->bos[i], 0, sizeof(struct bo_info));
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
	memset(bo_info->maps[0], 0, bo_info->sizes[0]);
	bo_info->width = desktop->desktop_rc.w;
	bo_info->height = desktop->desktop_rc.h;
	if (desktop->duplicated) {
		printf("Fill %d,%d %ux%u\n", desktop->desktop_rc.pos.x,
			desktop->desktop_rc.pos.y,
			bo_info->width, bo_info->height);
		if (desktop->logo) {
			printf("Fill logo\n");
			fill_logo(bo_info->maps[0],
				bo_info->width,
				bo_info->height,
				bo_info->pitches[0],
				&desktop->desktop_rc.pos,
				desktop->logo);
		} else {
			printf("Fill bg\n");
			fill_def_bg(bo_info->maps[0],
				bo_info->width,
				bo_info->height,
				bo_info->pitches[0],
				&desktop->desktop_rc.pos);
		}
	} else {
		for (i = 0; i < desktop->heads_nr; i++) {
			printf("Fill %d,%d %ux%u\n", desktop->heads[i].rc.pos.x,
				desktop->heads[i].rc.pos.y,
				desktop->heads[i].rc.w, desktop->heads[i].rc.h);
			if (desktop->logo) {
				fill_logo(bo_info->maps[0],
					desktop->heads[i].rc.w,
					desktop->heads[i].rc.h,
					bo_info->pitches[0],
					&desktop->heads[i].rc.pos,
					desktop->logo);
			} else {
				fill_def_bg(bo_info->maps[0],
					desktop->heads[i].rc.w,
					desktop->heads[i].rc.h,
					bo_info->pitches[0],
					&desktop->heads[i].rc.pos);
			}
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
	cli->set_client_cap(cli, CB_CLIENT_CAP_NOTIFY_LAYOUT |
				 CB_CLIENT_CAP_HPD);
	cli->set_hpd_cb(cli, desktop, hpd_cb);
	cli->query_layout(cli);
}

static struct cube_desktop *cube_desktop_create(s32 seat,
						struct logo_desc *logo)
{
	struct cube_desktop *desktop;
	struct cb_client *cli;

	desktop = calloc(1, sizeof(*desktop));
	if (!desktop)
		goto err;
	desktop->logo = logo;

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
	struct cube_desktop *desktop;
	struct logo_desc logo;
	bool use_logo = false;

	while ((ch = getopt_long(argc, argv, short_options,
				 options, NULL)) != -1) {
		switch (ch) {
		case 's':
			seat = atoi(optarg);
			break;
		case 'l':
			memset(&logo, 0, sizeof(logo));
			strcpy(logo.file_path, optarg);
			logo.fd = open(logo.file_path, O_RDONLY, 0644);
			if (logo.fd < 0) {
				fprintf(stderr, "logo %s not exist.\n",
					logo.file_path);
			} else {
				lseek(logo.fd, 10, SEEK_SET);
				read(logo.fd, &logo.offs, sizeof(u32));
				printf("logo offset: %u\n", logo.offs);
				lseek(logo.fd, 18, SEEK_SET);
				read(logo.fd, &logo.width, sizeof(u32));
				read(logo.fd, &logo.height, sizeof(u32));
				read(logo.fd, &logo.planes, sizeof(short));
				read(logo.fd, &logo.count_bits, sizeof(short));
				read(logo.fd, &logo.compression, sizeof(u32));
				read(logo.fd, &logo.size, sizeof(u32));
				if (logo.height > 0) {
					printf("Y-invert\n");
					logo.y_invert = true;
				} else {
					logo.y_invert = false;
					logo.height = (-logo.height);
				}
				printf("logo widthxheight: %ux%d\n", logo.width,
					logo.height);
				printf("logo planes: %u\n", logo.planes);
				printf("logo count_bits: %u\n",
					logo.count_bits);
				printf("logo compression: %u\n",
					logo.compression);
				printf("logo size: %u\n", logo.size);
				if (logo.width > 480 ||
				    logo.height > 320 ||
				    logo.planes > 1 ||
				    logo.count_bits != 24 ||
				    logo.compression) {
					fprintf(stderr, "Not supported.\n");
					printf("Only support:\n");
					printf("\twidth <= 480 height <= 320\n");
					printf("\tplanes: 1\n");
					printf("\tcount_bits: 24\n");
					printf("\tcompression: No\n");
					use_logo = false;
				} else {
					u32 bytes_to_rd = logo.size;
					u8 *p;
					s32 ret;

					use_logo = true;
					logo.vaddr = malloc(logo.size);
					p = logo.vaddr;
					lseek(logo.fd, logo.offs, SEEK_SET);
					while (bytes_to_rd) {
						ret = read(logo.fd, p,
							   bytes_to_rd);
						if (ret < 0)
							break;
						bytes_to_rd -= ret;
						p += ret;
					}
				}
				close(logo.fd);
			}
			break;
		default:
			usage();
			return -1;
		}
	}

	if (!use_logo) {
		printf("logo: NULL\n");
		desktop = cube_desktop_create(seat, NULL);
	} else {
		printf("logo: %p\n", &logo);
		desktop = cube_desktop_create(seat, &logo);
	}
	if (!desktop)
		goto out;

	desktop->cli->run(desktop->cli);

out:
	cube_desktop_destroy(desktop);

	return 0;
}

