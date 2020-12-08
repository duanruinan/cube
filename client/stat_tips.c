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
#include <syslog.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/input.h>
#include <assert.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
//#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cube_region.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_ipc.h>
#include <cube_event.h>
#include <cube_protocal.h>
#include <cube_client.h>
#include "stat_tips.h"

struct bo_info {
	void *bo;
	u64 bo_id;
	s32 count_planes;
	s32 count_fds;
	s32 fds[4];
	u32 width, height;
	u32 strides[4];

	EGLImageKHR image;
	GLuint gl_texture;
	GLuint gl_fbo;
};

struct character {
	GLuint texture;
	u32 w, h; /* glyph */
	struct cb_pos offs; /* offset from baseline to left/top of glyph */
	GLuint advance; /* horizontal offset to advance to next glyph */
};

struct ipc_client {
	u8 ipc_buf[4096];
	size_t ipc_sz, byts_to_rd;
	u8 *cursor;
	s32 sock;
	void *sock_source;
	struct cube_client *client;
	struct list_head link;
};

enum client_state {
	CLI_ST_SHOWN = 0,
	CLI_ST_SLIDING_IN_TIMER_LAUNCHED,
	CLI_ST_PREPARE_SLIDING_IN,
	CLI_ST_SLIDING_IN,
	CLI_ST_HIDE,
	CLI_ST_SLIDING_OUT,
};

struct cube_client {
	struct cb_client *cli;
	s32 count_bos;
	struct bo_info bos[2];
	s32 work_bo;

	struct dashboard_info dashboard;

	s32 mc_x, mc_y;
	s32 x, y;
	u32 width, height;

	enum cb_pix_fmt pix_fmt;

	s32 dev_fd;
	struct gbm_device *gbm_device;

	struct {
		EGLDisplay display;
		EGLContext context;
		PFNEGLCREATEIMAGEKHRPROC create_image;
		PFNEGLDESTROYIMAGEKHRPROC destroy_image;
		PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
	} egl;

	struct {
		GLuint program;
		u32 VAO, VBO;
	} gl;

	s32 zpos;

	void *signal_handler;

	struct cb_surface_info s;
	struct cb_view_info v;

	s32 sock, client_sock;
	void *sock_source;
	void *repaint_timer;
	void *slide_into_timer;

	enum client_state state;
	s32 delta;

	bool bo_switched;

	struct list_head ipc_clients;
	u32 ipc_clients_cnt;

	char font_path[256];

	/* char bitmap */
	u32 font_sz;
	struct character ch[128];

	s32 render_needed;
};

static void usage(void)
{
	fprintf(stderr, "Usage:\n"
		"\tstat_tips --width w --height h --zpos zpos "
		"--font fontfile\n");
}

static struct option options[] = {
	{"width", 1, NULL, 'w'},
	{"height", 1, NULL, 'h'},
	{"zpos", 1, NULL, 'z'},
	{"font", 1, NULL, 'f'},
	{NULL, 0, NULL, 0},
};

static char short_options[] = "w:h:z:f:";

static void render_text(struct cube_client *client, float color[3],
			char *string, s32 x, s32 y, float scale)
{
	u8 c;
	s32 i = 0;
	struct character *ch;

	glBindVertexArray(client->gl.VAO);

	do {
		c = string[i++];
		if (!c)
			break;
		ch = &client->ch[c];

		float xpos = x + ch->offs.x * scale;
		float ypos = y - (ch->h - ch->offs.y) * scale;

		float w = ch->w * scale;
		float h = ch->h * scale;

		glBindTexture(GL_TEXTURE_2D, client->ch[c].texture);
	
		GLfloat vertices[6][4] = {
			{ xpos,		ypos + h,	0.0f,	0.0f },
			{ xpos,		ypos,		0.0f,	1.0f },
			{ xpos + w,	ypos,		1.0f,	1.0f },
	
			{ xpos,		ypos + h,	0.0f,	0.0f },
			{ xpos + w,	ypos,		1.0f,	1.0f },
			{ xpos + w,	ypos + h,	1.0f,	0.0f },
		};
		glBindBuffer(GL_ARRAY_BUFFER, client->gl.VBO);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUniform3f(glGetUniformLocation(client->gl.program,
			    "textcolor"), color[0], color[1], color[2]);
		glDrawArrays(GL_TRIANGLE_FAN, 0, 6);
		x += (ch->advance >> 6) * scale;
	} while (c);

	glBindTexture(GL_TEXTURE_2D, 0);
}

static void render_gpu(struct cube_client *client, struct bo_info *bo_info)
{
	s32 x, y;
	float color[3] = {0.0f, 0.0f, 0.0f};
	float scale = 0.72f;
	char msg[64];

	if (!client->render_needed)
		return;

	x = client->font_sz * scale;
	y = client->height - client->font_sz * scale * 1.5f;

	/* Direct all GL draws to the buffer through the FBO */
	glBindFramebuffer(GL_FRAMEBUFFER, bo_info->gl_fbo);

	glClearColor(0.1f, 0.1f, 0.1f, 0.5f);
	glClear(GL_COLOR_BUFFER_BIT);

	glViewport(0, 0, client->width, client->height);

	glActiveTexture(GL_TEXTURE0);

	color[0] = 0.0f;
	color[1] = 0.0f;
	color[2] = 0.8f;
	render_text(client, color, "           Dashboard            ",
		    x, y, scale);
	x = client->font_sz * scale;
	y -= client->font_sz * scale * 1.5f;

	color[0] = 0.0f;
	color[1] = 1.0f;
	color[2] = 0.0f;
	render_text(client, color, "[Notice]: Move cursor to top left of the ",
		    x, y, scale * 0.8f);
	x = client->font_sz * scale;
	y -= client->font_sz * scale * 1.0f;
	render_text(client, color, "          screen to show this dashboard.",
		    x, y, scale * 0.8f);
	x = client->font_sz * scale;
	y -= client->font_sz * scale * 1.5f;
	color[0] = 0.0f;
	color[1] = 0.0f;
	color[2] = 0.8f;
	memset(msg, 0, 64);
	sprintf(msg, "IP:              %s", client->dashboard.ip);
	render_text(client, color, msg,
		    x, y, scale * 0.8f);
	x = client->font_sz * scale;
	y -= client->font_sz * scale * 1.5f;
	memset(msg, 0, 64);
	sprintf(msg, "Deployment site: %s", client->dashboard.deployment_site);
	render_text(client, color, msg,
		    x, y, scale * 0.8f);
	x = client->font_sz * scale;
	y -= client->font_sz * scale * 1.5f;
	memset(msg, 0, 64);
	sprintf(msg, "Rate:            %s", client->dashboard.rate);
	render_text(client, color, msg,
		    x, y, scale * 0.8f);
	x = client->font_sz * scale;
	y -= client->font_sz * scale * 1.5f;
	memset(msg, 0, 64);
	sprintf(msg, "Latency:         %s", client->dashboard.latency);
	render_text(client, color, msg,
		    x, y, scale * 0.8f);
	x = client->font_sz * scale;
	y -= client->font_sz * scale * 1.5f;

	glFinish();

	client->render_needed--;
}

static s32 signal_cb(s32 signal_number, void *userdata)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;

	printf("receive signal\n");
	printf("receive signal\n");
	printf("receive signal\n");
	printf("receive signal\n");
	cli->stop(cli);
	return 0;
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
	client->v.alpha = 0.5f;
	client->v.zpos = client->zpos;
	client->v.area.pos.x = client->x;
	client->v.area.pos.y = client->y;
	client->v.area.w = client->width;
	client->v.area.h = client->height;
}

static void bo_commited_cb(bool success, void *userdata, u64 bo_id,
			   u64 surface_id)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;

	assert(surface_id == client->s.surface_id);
	if (bo_id == (u64)(-1)) {
		printf("failed to commit bo\n");
		cli->stop(cli);
	}
}

static void client_state_fsm(struct cube_client *client)
{
	struct cb_client *cli = client->cli;

	if ((client->mc_x >= 0 && client->mc_x < 1000) &&
	    (client->mc_y >= 0 && client->mc_y < 1000)) {
		switch (client->state) {
		case CLI_ST_SHOWN:
			/* keep shown */
			client->state = CLI_ST_SHOWN;
			client->delta = 0;
//			printf("--- keep shown\n");
			break;
		case CLI_ST_SLIDING_IN_TIMER_LAUNCHED:
			/* stop timer */
			client->state = CLI_ST_SHOWN;
			client->delta = 0;
			cli->timer_update(cli,
					  client->slide_into_timer,
					  0, 0);
//			printf("--- slide in sched -> shown\n");
			break;
		case CLI_ST_PREPARE_SLIDING_IN:
			/*turn from prepare slide out to shown*/
			client->state = CLI_ST_SHOWN;
			client->delta = 0;
//			printf("--- prepare slide in -> shown ---\n");
			cli->timer_update(cli,
					  client->slide_into_timer,
					  0, 0);
			break;
		case CLI_ST_SLIDING_IN:
			/* turn from slide in into slide out */
			client->state = CLI_ST_SLIDING_OUT;
			client->delta = 2;
//			printf("--- slide in -> out ---\n");
			cli->timer_update(cli,
					  client->slide_into_timer,
					  0, 0);
			break;
		case CLI_ST_HIDE:
			/* begin to slide out */
			client->state = CLI_ST_SLIDING_OUT;
			client->delta = 2;
//			printf("--- hide -> slide out ---\n");
			cli->timer_update(cli,
					  client->slide_into_timer,
					  0, 0);
			break;
		case CLI_ST_SLIDING_OUT:
			/* continue to slide out */
			cli->timer_update(cli,
					  client->slide_into_timer,
					  0, 0);
			if (client->x == 0) {
				client->state = CLI_ST_SHOWN;
				client->delta = 0;
//				printf("--- slide out -> shown\n");
			} else {
				client->state = CLI_ST_SLIDING_OUT;
				client->delta = 2;
//				printf("continue to slide out\n");
			}
			break;
		default:
			break;
		}
	} else {
		switch (client->state) {
		case CLI_ST_SHOWN:
			client->state = CLI_ST_SLIDING_IN_TIMER_LAUNCHED;
			client->delta = 0;
			/* start slide out timer */
			cli->timer_update(cli,
					  client->slide_into_timer,
					  5000, 0);
//			printf("--- shown -> slide in sched\n");
			break;
		case CLI_ST_SLIDING_IN_TIMER_LAUNCHED:
			client->state = CLI_ST_SLIDING_IN_TIMER_LAUNCHED;
			client->delta = 0;
			break;
		case CLI_ST_PREPARE_SLIDING_IN:
			client->state = CLI_ST_SLIDING_IN;
			client->delta = -2;
//			printf("--- prepare sliding in -> sliding in\n");
			break;
		case CLI_ST_SLIDING_IN:
			if (client->x == (-client->width + 2)) {
				client->state = CLI_ST_HIDE;
				client->delta = 0;
//				printf("--- sliding in -> hide\n");
			} else {
				client->state = CLI_ST_SLIDING_IN;
				client->delta = -2;
//				printf("--- keep sliding in\n");
			}
			break;
		case CLI_ST_HIDE:
			client->state = CLI_ST_HIDE;
			client->delta = 0;
//			printf("--- keep hide\n");
			break;
		case CLI_ST_SLIDING_OUT:
			if (client->x == 0) {
				client->state = CLI_ST_SHOWN;
				client->delta = 0;
			} else {
				client->state = CLI_ST_SLIDING_OUT;
				client->delta = 2;
//				printf("--- keep sliding out til shown\n");
			}
			break;
		default:
			break;
		}
	}
}

static s32 repaint_cb(void *userdata)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct cb_commit_info c;
	s32 ret;

	/* syslog(LOG_ERR, "[stat_tips] commit bo[%d]\n", client->work_bo); */
	c.bo_id = client->bos[client->work_bo].bo_id;
	c.surface_id = client->s.surface_id;

	c.bo_damage.pos.x = 0;
	c.bo_damage.pos.y = 0;
	c.bo_damage.w = client->width;
	c.bo_damage.h = client->height;

	client->x += client->delta;
	if (client->delta > 0) {
		client->x = MIN(client->x, 0);
	} else if (client->delta < 0) {
		client->x = MAX(client->x, (-client->width + 2));
	}
	client_state_fsm(client);
	
	/* printf("x = %d\n", client->x); */

	c.view_x = client->x;
	c.view_y = client->y;
	c.view_width = client->width;
	c.view_height = client->height;
	c.pipe_locked = 0;

	ret = cli->commit_bo(cli, &c);
	if (ret < 0) {
		fprintf(stderr, "failed to commit bo %s\n", __func__);
		cli->stop(cli);
	}

	return 0;
}

static void bo_flipped_cb(void *userdata, u64 bo_id, u64 surface_id)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;

	assert(surface_id == client->s.surface_id);
	if (bo_id == client->bos[0].bo_id) {
		/* printf("[stat_tips] bo[0] flipped.\n"); */
	} else if (bo_id == client->bos[1].bo_id) {
		/* printf("[stat_tips] bo[1] flipped.\n"); */
	} else {
		fprintf(stderr, "unknown buffer flipped.\n");
	}

	cli->timer_update(cli, client->repaint_timer, 5, 0);
	client->bo_switched = false;
}

static void bo_completed_cb(void *userdata, u64 bo_id, u64 surface_id)
{
	struct cube_client *client = userdata;
	struct bo_info *bo_info;
	s32 bo_index_prev = client->work_bo;

	assert(surface_id == client->s.surface_id);
	/* printf("[stat_tips] work_bo: %d\n", bo_index_prev); */
	if (bo_id == client->bos[0].bo_id) {
		/* printf("[stat_tips] bo[0] completed.\n"); */
		client->work_bo = 0;
	} else if (bo_id == client->bos[1].bo_id) {
		/* printf("[stat_tips] bo[1] completed.\n"); */
		client->work_bo = 1;
	} else {
		fprintf(stderr, "unknown bo completed.\n");
	}

	if (bo_index_prev != client->work_bo) {
		client->bo_switched = true;
		/* printf("[stat_tips] render bo[%d]\n", client->work_bo); */
		bo_info = &client->bos[client->work_bo];
		render_gpu(client, bo_info);
	}
}

static void view_created_cb(bool success, void *userdata, u64 view_id)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct cb_commit_info c;
	struct bo_info *bo_info;
	s32 ret;

	if (success) {
		printf("create view succesfull\n");
		client->v.view_id = view_id;
		client->work_bo = 0;
		cli->set_commit_bo_cb(cli, client, bo_commited_cb);
		cli->set_bo_flipped_cb(cli, client, bo_flipped_cb);
		cli->set_bo_completed_cb(cli, client, bo_completed_cb);

		c.bo_id = client->bos[client->work_bo].bo_id;
		c.surface_id = client->s.surface_id;
		c.bo_damage.pos.x = 0;
		c.bo_damage.pos.y = 0;
		c.bo_damage.w = client->width;
		c.bo_damage.h = client->height;
		c.view_x = client->x;
		c.view_y = client->y;
		c.view_width = client->width;
		c.view_height = client->height;
		c.pipe_locked = 0;

		bo_info = &client->bos[client->work_bo];
		client->render_needed = 2;
		render_gpu(client, bo_info);
		bo_info = &client->bos[1 - client->work_bo];
		render_gpu(client, bo_info);
		printf("commit 0\n");
		ret = cli->commit_bo(cli, &c);
		client->work_bo = 1;
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

static s32 slide_into_cb(void *userdata)
{
	struct cube_client *client = userdata;

	assert(client->state == CLI_ST_SLIDING_IN_TIMER_LAUNCHED);
	client->state = CLI_ST_PREPARE_SLIDING_IN;
	return 0;
}

static void raw_input_evts_cb(void *userdata,
			      struct cb_raw_input_event *evts,
			      u32 count_evts)
{
	s32 i;
	struct cube_client *client = userdata;

	for (i = 0; i < count_evts; i++) {
		switch (evts[i].type) {
		case EV_ABS:
			client->mc_x = evts[i].v.pos.x;
			client->mc_y = evts[i].v.pos.y;
			client_state_fsm(client);
			break;
		case EV_SYN:
			break;
		case EV_KEY:
			break;
		case EV_REP:
			break;
		case EV_REL:
			assert(evts[i].code == REL_WHEEL);
			break;
		default:
			printf("%04X %04X %08X\n", evts[i].type, evts[i].code,
				evts[i].v.value);
			break;
		}
	}
}

static void ready_cb(void *userdata)
{
	struct cube_client *client = userdata;
	struct cb_client *cli = client->cli;
	struct bo_info *bo_info = &client->bos[0];
	s32 ret;

	cli->set_client_cap(cli, CB_CLIENT_CAP_RAW_INPUT);
	cli->set_raw_input_en(cli, true);
	cli->set_create_bo_cb(cli, client, bo_created_cb);
	ret = cli->create_bo(cli, bo_info->bo);
	if (ret < 0) {
		fprintf(stderr, "failed to create bo.\n");
		cli->stop(cli);
		return;
	}
	client->count_bos++;
}

static s32 match_config_to_visual(EGLDisplay egl_display, EGLint visual_id,
				  EGLConfig *configs, s32 count)
{
	s32 i;
	EGLint id;

	for (i = 0; i < count; i++) {
		if (!eglGetConfigAttrib(egl_display, configs[i],
					EGL_NATIVE_VISUAL_ID, &id)) {
			fprintf(stderr, "get VISUAL_ID failed.\n");
			continue;
		} else {
			fprintf(stderr, "get VISUAL_ID ok. %u %u\n",
				id, visual_id);
		}
		if (id == visual_id)
			return i;
	}

	return -1;
}

static s32 egl_choose_config(struct cube_client *client, const EGLint *attribs,
			     const EGLint *visual_ids, const s32 count_ids,
			     EGLConfig *config_matched, EGLint *vid)
{
	EGLint count_configs = 0;
	EGLint count_matched = 0;
	EGLConfig *configs;
	s32 i, config_index = -1;

	if (!eglGetConfigs(client->egl.display, NULL, 0, &count_configs)) {
		fprintf(stderr, "Cannot get EGL configs.\n");
		return -1;
	}
	printf("count_configs = %d\n", count_configs);

	configs = calloc(count_configs, sizeof(*configs));
	if (!configs)
		return -ENOMEM;

	if (!eglChooseConfig(client->egl.display, attribs, configs,
			     count_configs, &count_matched)
	    || !count_matched) {
		fprintf(stderr, "cannot select appropriate configs.\n");
		goto out1;
	}
	printf("count_matched = %d\n", count_matched);

	if (!visual_ids || count_ids == 0)
		config_index = 0;

	for (i = 0; config_index == -1 && i < count_ids; i++) {
		config_index = match_config_to_visual(client->egl.display,
						      visual_ids[i],
						      configs,
						      count_matched);
		printf("config_index = %d i = %d count_ids = %d\n",
		       config_index, i, count_ids);
	}

	if (config_index != -1)
		*config_matched = configs[config_index];

out1:
	if (visual_ids) {
		*vid = visual_ids[i - 1];
	} else {
		for (i = 0; i < count_matched; i++) {
			if (!eglGetConfigAttrib(client->egl.display, configs[0],
						EGL_NATIVE_VISUAL_ID, vid)) {
				fprintf(stderr, "Get visual id failed.\n");
				continue;
			}
			break;
		}
	}
	free(configs);
	if (config_index == -1)
		return -1;

	if (i > 1)
		printf("Unable to use first choice EGL config with ID "
			"0x%x, succeeded with alternate ID 0x%x\n",
			visual_ids[0], visual_ids[i - 1]);

	return 0;
}

static const char *egl_strerror(EGLint err)
{
#define EGLERROR(x) case x: return #x;
	switch (err) {
	EGLERROR(EGL_SUCCESS)
	EGLERROR(EGL_NOT_INITIALIZED)
	EGLERROR(EGL_BAD_ACCESS)
	EGLERROR(EGL_BAD_ALLOC)
	EGLERROR(EGL_BAD_ATTRIBUTE)
	EGLERROR(EGL_BAD_CONTEXT)
	EGLERROR(EGL_BAD_CONFIG)
	EGLERROR(EGL_BAD_CURRENT_SURFACE)
	EGLERROR(EGL_BAD_DISPLAY)
	EGLERROR(EGL_BAD_SURFACE)
	EGLERROR(EGL_BAD_MATCH)
	EGLERROR(EGL_BAD_PARAMETER)
	EGLERROR(EGL_BAD_NATIVE_PIXMAP)
	EGLERROR(EGL_BAD_NATIVE_WINDOW)
	EGLERROR(EGL_CONTEXT_LOST)
	default:
		return "unknown";
	}
#undef EGLERROR
}

static void egl_error_state(void)
{
	EGLint err;

	err = eglGetError();
	fprintf(stderr, "EGL err: %s (0x%04lX)\n", egl_strerror(err), (u64)err);
}

static const char *vert_shader_text =
	"#version 300 es\n"
	"precision mediump float;\n"
	"layout(location = 0) in vec4 vertex;\n"
	"out vec2 texcoord;\n"
	"uniform mat4 projection;\n"
	"void main()\n"
	"{\n"
	"    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);\n"
	"    texcoord = vertex.zw;\n"
	"}\n";

static const char *frag_shader_text =
	"#version 300 es\n"
	"precision mediump float;\n"
	"in vec2 texcoord;\n"
	"out vec4 color;\n"
	"uniform sampler2D tex_char;\n"
	"uniform vec3 textcolor;\n"
	"void main()\n"
	"{\n"  
	"    vec4 s = vec4(1.0, 1.0, 1.0, (texture(tex_char, texcoord).r));\n"
	"    color = vec4(textcolor, 1.0) * s;\n"
	"}\n";

static GLuint create_and_link_program(GLuint vert, GLuint frag)
{
	GLint status;
	GLuint program = glCreateProgram();

	glAttachShader(program, vert);
	glAttachShader(program, frag);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%*s\n", len, log);
		return 0;
	}

	return program;
}

static GLuint create_shader(const char *source, GLenum shader_type)
{
	GLuint shader;
	GLint status;

	shader = glCreateShader(shader_type);
	assert(shader != 0);

	glShaderSource(shader, 1, (const char **) &source, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetShaderInfoLog(shader, 1000, &len, log);
		fprintf(stderr, "Error: compiling %s: %*s\n",
			shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
			len, log);
		return 0;
	}

	return shader;
}

static s32 setup_gl(struct cube_client *client)
{
	GLuint vert = create_shader(
		vert_shader_text,
		GL_VERTEX_SHADER);
	GLuint frag = create_shader(
		frag_shader_text,
		GL_FRAGMENT_SHADER);
	GLfloat projmat_yinvert[16] = { /* transpose */
		 2.0f / client->width,  0.0f, 0.0f, 0.0f,
		 0.0f, -2.0f / client->height, 0.0f, 0.0f,
		 0.0f,  0.0f, 1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f, 1.0f,
	};

	client->gl.program = create_and_link_program(vert, frag);

	glUseProgram(client->gl.program);
	glUniformMatrix4fv(glGetUniformLocation(client->gl.program,
			"projection"), 1, GL_FALSE, projmat_yinvert);

	glGenVertexArrays(1, &client->gl.VAO);
	glGenBuffers(1, &client->gl.VBO);
	glBindVertexArray(client->gl.VAO);
	glBindBuffer(GL_ARRAY_BUFFER, client->gl.VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 6 * 4, NULL,
		     GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	glDeleteShader(vert);

	glDeleteShader(frag);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	return client->gl.program == 0;
}

static s32 create_fbo_for_buffer(struct cube_client *client,
				 struct bo_info *bo_info)
{
	static const s32 general_attribs = 3;
	static const s32 plane_attribs = 5;
	static const s32 entries_per_attrib = 2;
	EGLint attribs[(general_attribs + plane_attribs * 4) *
			entries_per_attrib + 1];
	u32 atti = 0;

	attribs[atti++] = EGL_WIDTH;
	attribs[atti++] = bo_info->width;
	attribs[atti++] = EGL_HEIGHT;
	attribs[atti++] = bo_info->height;
	attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
	attribs[atti++] = DRM_FORMAT_ARGB8888;

#define ADD_PLANE_ATTRIBS(plane_idx) { \
	attribs[atti++] = EGL_DMA_BUF_PLANE ## plane_idx ## _FD_EXT; \
	attribs[atti++] = bo_info->fds[0]; \
	attribs[atti++] = EGL_DMA_BUF_PLANE ## plane_idx ## _OFFSET_EXT; \
	attribs[atti++] = 0; \
	attribs[atti++] = EGL_DMA_BUF_PLANE ## plane_idx ## _PITCH_EXT; \
	attribs[atti++] = bo_info->strides[0]; \
	printf("attribs[%d] = %u\n", atti-1, attribs[atti-1]); \
	}

	ADD_PLANE_ATTRIBS(0);

#undef ADD_PLANE_ATTRIBS

	attribs[atti] = EGL_NONE;

	assert(atti < ARRAY_SIZE(attribs));

	bo_info->image = client->egl.create_image(client->egl.display,
						  EGL_NO_CONTEXT,
						  EGL_LINUX_DMA_BUF_EXT,
						  NULL, attribs);
	if (bo_info->image == EGL_NO_IMAGE_KHR) {
		egl_error_state();
		return -1;
	}

	eglMakeCurrent(client->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       client->egl.context);

	glGenTextures(1, &bo_info->gl_texture);
	glBindTexture(GL_TEXTURE_2D, bo_info->gl_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	client->egl.image_target_texture_2d(GL_TEXTURE_2D, bo_info->image);

	glGenFramebuffers(1, &bo_info->gl_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, bo_info->gl_fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, bo_info->gl_texture, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER)
			!= GL_FRAMEBUFFER_COMPLETE) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &bo_info->gl_fbo);
		glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteTextures(1, &bo_info->gl_texture);
		client->egl.destroy_image(client->egl.display, bo_info->image);
		bo_info->image = EGL_NO_IMAGE_KHR;
		fprintf(stderr, "FBO creation failed\n");
		return -1;
	}

	printf("create fbo success.\n");

	return 0;
}

static void destroy_fbo_for_buffer(struct cube_client *client,
				   struct bo_info *bo_info)
{
	if (!client || !bo_info)
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &bo_info->gl_fbo);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDeleteTextures(1, &bo_info->gl_texture);

	if (bo_info->image != EGL_NO_IMAGE_KHR) {
		client->egl.destroy_image(client->egl.display, bo_info->image);
		bo_info->image = EGL_NO_IMAGE_KHR;
	}
}

static s32 text_init(struct cube_client *client)
{
	FT_Library ft;
	FT_Face face;
	GLuint texture;
	u8 i;

	if (FT_Init_FreeType(&ft)) {
		fprintf(stderr, "failed to init freetype.\n");
		return -1;
	}
	printf("init freetype success.\n");

	printf("Font: %s\n", client->font_path);
	if (FT_New_Face(ft, client->font_path, 0, &face)) {
		fprintf(stderr, "failed to load font\n");
		FT_Done_FreeType(ft);
		return -1;
	}
	printf("Load font ok.\n");

	printf("Req font size %dx%d\n", client->font_sz, client->font_sz);
	FT_Set_Pixel_Sizes(face, 0, client->font_sz);

	for (i = 0; i < 128; i++) {
		if (FT_Load_Char(face, i, FT_LOAD_RENDER |
					  FT_LOAD_TARGET_NORMAL)) {
			fprintf(stderr, "failed to load char.\n");
			FT_Done_Face(face);
			FT_Done_FreeType(ft);
			return -1;
		}

		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8_EXT,
			     face->glyph->bitmap.width,
			     face->glyph->bitmap.rows,
			     0,
			     GL_RED_EXT, GL_UNSIGNED_BYTE,
			     face->glyph->bitmap.buffer);
		
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
				GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
				GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
				GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
				GL_LINEAR);
		client->ch[i].texture = texture;
		client->ch[i].w = face->glyph->bitmap.width;
		client->ch[i].h = face->glyph->bitmap.rows;
		client->ch[i].offs.x = face->glyph->bitmap_left;
		client->ch[i].offs.y = face->glyph->bitmap_top;
		client->ch[i].advance = face->glyph->advance.x;
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	FT_Done_Face(face);
	FT_Done_FreeType(ft);

	return 0;
}

static s32 client_sock_cb(s32 fd, u32 mask, void *data)
{
	struct ipc_client *ipc_client = data;
	struct cube_client *client = ipc_client->client;
	struct cb_client *cli = client->cli;
	struct dashboard_info *dashboard;

	s32 ret;
	size_t byts_rd;
	s32 flag; /* 0: length not received, 1: length received. */

	if (ipc_client->cursor >= ((u8 *)(ipc_client->ipc_buf)
			+ sizeof(size_t))) {
		flag = 1;
	} else {
		flag = 0;
	}

	do {
		ret = cb_recvmsg(ipc_client->sock, ipc_client->cursor,
				 ipc_client->byts_to_rd, NULL);
	} while (ret == -EAGAIN);

	if (ret < 0) {
		syslog(LOG_ERR, "failed to recv (%s).\n", strerror(-ret));
		close(ipc_client->sock);
		list_del(&ipc_client->link);
		cli->rm_handler(cli, ipc_client->sock_source);
		free(ipc_client);
		client->ipc_clients_cnt--;
		syslog(LOG_ERR, "ipc_clients_cnt: %u\n",
			client->ipc_clients_cnt);
		return ret;
	} else if (ret == 0) {
		syslog(LOG_ERR, "connection lost.\n");
		close(ipc_client->sock);
		list_del(&ipc_client->link);
		cli->rm_handler(cli, ipc_client->sock_source);
		free(ipc_client);
		client->ipc_clients_cnt--;
		syslog(LOG_ERR, "ipc_clients_cnt: %u\n",
			client->ipc_clients_cnt);
		return 0;
	}

	ipc_client->cursor += ret;
	ipc_client->byts_to_rd -= ret;
	byts_rd = ret;

	if (!flag) {
		if (ret >= sizeof(size_t)) {
			/* received the length */
			flag = 1;
			memcpy(&ipc_client->byts_to_rd, ipc_client->ipc_buf,
			       sizeof(size_t));
			ipc_client->ipc_sz = ipc_client->byts_to_rd;
			if ((byts_rd - sizeof(size_t)) > ipc_client->ipc_sz) {
				/* received more than one ipc message */
				ipc_client->byts_to_rd = 0;
			} else {
				ipc_client->byts_to_rd -=
					(byts_rd -sizeof(size_t));
			}
		}
	}

	if (!ipc_client->byts_to_rd) {
		ipc_client->cursor = (u8 *)ipc_client->ipc_buf;
		ipc_client->byts_to_rd = sizeof(size_t);
		dashboard =
			(struct dashboard_info *)(ipc_client->ipc_buf
							+ sizeof(size_t));
		memcpy(&client->dashboard, dashboard, sizeof(*dashboard));
		client->render_needed = 2;
	}

	return 0;
}

static s32 server_sock_cb(s32 fd, u32 mask, void *data)
{
	struct ipc_client *ipc_client;
	struct cube_client *client = data;
	struct cb_client *cli = client->cli;

	client->ipc_clients_cnt++;
	syslog(LOG_DEBUG, "a new client comming. %u\n",
		client->ipc_clients_cnt);
	ipc_client = calloc(1, sizeof(*ipc_client));
	ipc_client->sock = cb_socket_accept(fd);
	syslog(LOG_DEBUG, "new sock %d\n", ipc_client->sock);
	ipc_client->client = client;
	ipc_client->byts_to_rd = sizeof(size_t);
	ipc_client->cursor = (u8 *)(ipc_client->ipc_buf);
	ipc_client->ipc_sz = 0;
	ipc_client->sock_source = cli->add_fd_handler(cli, ipc_client,
						      ipc_client->sock,
						      CB_EVT_READABLE,
						      client_sock_cb);
	list_add_tail(&ipc_client->link, &client->ipc_clients);
	return 0;
}

static s32 client_init(struct cube_client *client)
{
	char ipc_name[64] = {0};
	struct cb_client *cli;
	struct bo_info *bo_info;
	s32 i, j;
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};
	static const EGLint opaque_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};
	EGLint major, minor;
	static PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = NULL;
	EGLConfig egl_config;
	s32 format = GBM_FORMAT_ARGB8888;
	s32 vid;

	client->dev_fd = cb_drm_device_open("/dev/dri/renderD128");
	client->gbm_device = cb_gbm_open(client->dev_fd);
	if (client->gbm_device == NULL) {
		fprintf(stderr, "failed to create gbm device.\n");
		return -errno;
	}

	if (!get_platform_display) {
		get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        		eglGetProcAddress("eglGetPlatformDisplayEXT");
	}

	client->egl.display = get_platform_display(EGL_PLATFORM_GBM_KHR,
						   client->gbm_device, NULL);
	if (client->egl.display == EGL_NO_DISPLAY) {
		fprintf(stderr, "Failed to create EGLDisplay\n");
		goto err_buf_alloc;
	}

	if (eglInitialize(client->egl.display, &major, &minor) == EGL_FALSE) {
		fprintf(stderr, "Failed to initialize EGLDisplay\n");
		goto err_buf_alloc;
	}

	if (egl_choose_config(client, opaque_attribs, &format, 1,
			      &egl_config, &vid) < 0) {
		fprintf(stderr, "failed to choose config\n");
		goto err_buf_alloc;
	}
	printf("vid = %08X\n", vid);

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		fprintf(stderr, "Failed to bind OpenGL ES API\n");
		goto err_buf_alloc;
	}

	client->egl.context = eglCreateContext(client->egl.display,
					       egl_config,
					       EGL_NO_CONTEXT,
					       context_attribs);
	if (client->egl.context == EGL_NO_CONTEXT) {
		fprintf(stderr, "Failed to create EGLContext\n");
		goto err_buf_alloc;
	}

	eglMakeCurrent(client->egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       client->egl.context);

	client->egl.create_image =
		(void *)eglGetProcAddress("eglCreateImageKHR");
	assert(client->egl.create_image);

	client->egl.destroy_image =
		(void *)eglGetProcAddress("eglDestroyImageKHR");
	assert(client->egl.destroy_image);

	client->egl.image_target_texture_2d =
		(void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	assert(client->egl.image_target_texture_2d);

	for (i = 0; i < 2; i++) {
		printf("create dma buffer\n");
		bo_info = &client->bos[i];
		bo_info->bo = cb_client_gbm_bo_create(
					client->dev_fd,
					client->gbm_device,
					CB_PIX_FMT_ARGB8888,
					client->width,
					client->height,
					&bo_info->count_fds,
					&bo_info->count_planes,
					bo_info->strides,
					bo_info->fds,
					false);
		if (!bo_info->bo) {
			fprintf(stderr, "failed to create dmabuf bo\n");
			goto err_buf_alloc;
		}
		printf("fds: %d %d %d %d\n",
			bo_info->fds[0],
			bo_info->fds[1],
			bo_info->fds[2],
			bo_info->fds[3]);
		bo_info->width = client->width;
		bo_info->height = client->height;

		if (create_fbo_for_buffer(client, bo_info) < 0) {
			fprintf(stderr, "failed to create fbo for buffer\n");
			i++;
			goto err_buf_alloc;
		}
	}

	assert(setup_gl(client) == 0);

	client->cli = cb_client_create(0);
	if (!client->cli)
		goto err_buf_alloc;

	cli = client->cli;
	client->signal_handler = cli->add_signal_handler(cli, client,
							 SIGTERM,
							 signal_cb);
	client->repaint_timer = cli->add_timer_handler(cli, client,
						       repaint_cb);
	client->slide_into_timer = cli->add_timer_handler(cli, client,
							  slide_into_cb);
	cli->set_ready_cb(cli, client, ready_cb);
	cli->set_raw_input_evts_cb(cli, client, raw_input_evts_cb);

	client->sock = cb_socket_cloexec();

	strcpy(ipc_name, "/tmp/stat_tips");

	unlink(ipc_name);
	cb_socket_bind_listen(client->sock, ipc_name);
	client->sock_source = cli->add_fd_handler(cli, client,
						  client->sock,
						  CB_EVT_READABLE,
						  server_sock_cb);

	surface_info_init(client);
	view_info_init(client);

	//client->x = -client->width + 2;
	client->x = 0;
	client->y = 0;

	strcpy(client->dashboard.ip, "127.0.0.1");

	INIT_LIST_HEAD(&client->ipc_clients);

	client->state = CLI_ST_SHOWN;

	client->render_needed = 2;

	return 0;

err_buf_alloc:
	for (j = 0; j < i; j++) {
		bo_info = &client->bos[j];
		if (bo_info->bo)
			cb_client_gbm_bo_destroy(bo_info->bo);
		client->bos[j].bo = NULL;
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

	while (client->count_bos) {
		printf("destroy bo\n");
		cli->destroy_bo(cli,
				client->bos[client->count_bos - 1].bo_id);
		client->count_bos--;
	}	

	cli->rm_handler(cli, client->repaint_timer);
	cli->rm_handler(cli, client->slide_into_timer);
	cli->rm_handler(cli, client->signal_handler);
	cli->rm_handler(cli, client->sock_source);

	cli->destroy(cli);

	surface_info_fini(client);
	view_info_fini(client);

	for (i = 0; i < 2; i++) {
		bo_info = &client->bos[i];
		destroy_fbo_for_buffer(client, bo_info);
		if (bo_info->bo)
			cb_client_gbm_bo_destroy(bo_info->bo);
		client->bos[i].bo = NULL;
	}

	if (client->egl.context != EGL_NO_CONTEXT)
		eglDestroyContext(client->egl.display, client->egl.context);

	if (client->egl.display != EGL_NO_DISPLAY)
		eglTerminate(client->egl.display);

	if (client->gbm_device)
		cb_gbm_close(client->gbm_device);

	if (client->dev_fd)
		cb_drm_device_close(client->dev_fd);
}

s32 main(s32 argc, char **argv)
{
	s32 ch;
	struct cube_client *client;
	struct cb_client *cli;
	u32 w, h;
	s32 zpos;
	char font_path[256] = {0};

	w = 450;
	h = 255;
	strcpy(font_path, "/tmp/NotoSansMono-Bold.ttf");
	while ((ch = getopt_long(argc, argv, short_options,
				 options, NULL)) != -1) {
		switch (ch) {
		case 'w':
			w = atoi(optarg);
			break;
		case 'h':
			h = atoi(optarg);
			break;
		case 'z':
			zpos = atoi(optarg);
			break;
		case 'f':
			strcpy(font_path, optarg);
			break;
		default:
			usage();
			exit(1);
		}
	}

	printf("Size: %ux%u ZPOS: %d\n", w, h, zpos);

	client = calloc(1, sizeof(*client));
	if (!client)
		return -ENOMEM;

	client->width = w;
	client->height = h;
	client->zpos = zpos;
	strcpy(client->font_path, font_path);

	if (client_init(client) < 0)
		goto out;

	client->font_sz = 32;
	text_init(client);

	cli = client->cli;

	cli->run(cli);
out:
	client_fini(client);
	free(client);

	return 0;
}

