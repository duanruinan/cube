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
#include <linux/input.h>
#include <assert.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_ipc.h>
#include <cube_event.h>
#include <cube_protocal.h>
#include <cube_client.h>

struct mc_bo_info {
	void *bo;
	u64 bo_id;
	s32 planes;
	void *maps[4];
	u32 pitches[4];
	u32 offsets[4];
	u32 sizes[4];
};

struct cube_input {
	char name[64];
	struct cb_client *client;
	s32 count_bos;
	struct mc_bo_info bos[2];
	s32 work_bo;
	void *delayed_timer;
	void *timeout_timer;
	void *show_hide_timer;
	void *update_led_timer;
	bool capsl_led;
	void *signal_handler;
	bool shown;
};

static s32 signal_cb(s32 signal_number, void *userdata)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;

	printf("receive signal\n");
	client->stop(client);
	return 0;
}

static void mc_commited_cb(bool success, void *userdata, u64 bo_id)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;

	if (!success) {
		client->timer_update(client, input->delayed_timer, 100, 0);
	} else {
		client->timer_update(client, input->delayed_timer, 0, 0);
		client->timer_update(client, input->timeout_timer,
					     500, 0);
	}
}

static void mc_flipped_cb(void *userdata, u64 bo_id)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;
	struct cb_mc_info mc_info;
	s32 ret;

	client->timer_update(client, input->timeout_timer, 0, 0);
	mc_info.cursor.hot_x = 5;
	mc_info.cursor.hot_y = 5;
	mc_info.cursor.w = 64;
	mc_info.cursor.h = 64;
	mc_info.type = MC_CMD_TYPE_SET_CURSOR;
	input->work_bo = 1 - input->work_bo;
	mc_info.bo_id = input->bos[1 - input->work_bo].bo_id;
	ret = client->commit_mc(client, &mc_info);
	if (ret < 0) {
		fprintf(stderr, "failed to commit mc %s\n", __func__);
		client->stop(client);
	}
}

static void mc_bo_created_cb(bool success, void *userdata, u64 bo_id)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;
	struct mc_bo_info *bo_info;
	char buf[64];
	s32 ret, i;
	u32 *pixel;
	struct cb_mc_info mc_info;

	if (!success) {
		fprintf(stderr, "failed to create bo\n");
err:
		while (input->count_bos) {
			cb_client_shm_bo_destroy(
				input->bos[input->count_bos].bo);
			input->count_bos--;
		}
		client->stop(client);
		return;
	}

	bo_info = &input->bos[input->count_bos - 1];
	printf("bo_id: %016lX\n", bo_id);
	bo_info->bo_id = bo_id;

	if (input->count_bos == 2) {
		printf("create bo complete.\n");
		mc_info.cursor.hot_x = 5;
		mc_info.cursor.hot_y = 5;
		mc_info.cursor.w = 64;
		mc_info.cursor.h = 64;
		mc_info.type = MC_CMD_TYPE_SET_CURSOR;
		// commit 1 work 0
		mc_info.bo_id = input->bos[1 - input->work_bo].bo_id;
		client->set_commit_mc_cb(client, input, mc_commited_cb);
		client->set_mc_flipped_cb(client,
					  input,
					  mc_flipped_cb);
		ret = client->commit_mc(client, &mc_info);
		if (ret < 0) {
			fprintf(stderr, "failed to commit mc %s\n", __func__);
			client->stop(client);
		}
		return;
	}

	while (input->count_bos < 2) {
		memset(buf, 0, 64);
		sprintf(buf, "%s-%d", input->name, input->count_bos);
		bo_info = &input->bos[input->count_bos];
		bo_info->bo = cb_client_shm_bo_create(buf, CB_PIX_FMT_ARGB8888,
						64, 64,
						64, 64, &bo_info->planes,
						bo_info->maps,
						bo_info->pitches,
						bo_info->offsets,
						bo_info->sizes);
		if (!bo_info->bo) {
			fprintf(stderr, "failed to create shm mc bo\n");
			goto err;
		}
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
		ret = client->create_bo(client, bo_info->bo);
		if (ret < 0) {
			fprintf(stderr, "failed to create bo.\n");
			goto err;
		}
		pixel = (u32 *)(bo_info->maps[0]);
		for (i = 0; i < 64 * 64; i++) {
			pixel[i] = 0x8000FF00;
		}
		input->count_bos++;
	}
}

static void set_caps_lock(struct cube_input *input)
{
	struct cb_client *client = input->client;

	printf(">>> Set capslock on\n");
	client->send_set_kbd_led_st(client, (1 << CB_KBD_LED_STATUS_CAPSL));
	input->capsl_led = !input->capsl_led;
}

static s32 update_led(void *userdata)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;

	if (input->capsl_led) {
		printf(">>> Set capslock on\n");
		client->send_set_kbd_led_st(client,
					    (1 << CB_KBD_LED_STATUS_CAPSL));
	} else {
		printf(">>> Set numlock on\n");
		client->send_set_kbd_led_st(client,
					    (1 << CB_KBD_LED_STATUS_NUML));
	}
	input->capsl_led = !input->capsl_led;
	printf("<<< Get kbd led status\n");
	client->send_get_kbd_led_st(client);

	return 0;
}

static void kbd_led_st_cb(void *userdata, u32 led_status)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;

	printf("[READ] LED status: %08X\n", led_status);
	client->timer_update(client, input->update_led_timer, 2000, 0);
}

static void ready_cb(void *userdata)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;
	char buf[64];
	struct mc_bo_info *bo_info;
	s32 ret, i;
	u32 *pixel;

	client->set_client_cap(client,
			       CB_CLIENT_CAP_RAW_INPUT |
			       CB_CLIENT_CAP_MC);
	client->set_raw_input_en(client, true);

	strcpy(input->name, "test_mc");
	memset(buf, 0, 64);
	sprintf(buf, "%s-%d", input->name, input->count_bos);
	bo_info = &input->bos[input->count_bos];
	bo_info->bo = cb_client_shm_bo_create(buf, CB_PIX_FMT_ARGB8888, 64, 64,
					      64, 64, &bo_info->planes,
					      bo_info->maps,
					      bo_info->pitches,
					      bo_info->offsets,
					      bo_info->sizes);
	if (!bo_info->bo) {
		fprintf(stderr, "failed to create shm mc bo\n");
		client->stop(client);
		return;
	}
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
	client->set_create_bo_cb(client, input, mc_bo_created_cb);
	ret = client->create_bo(client, bo_info->bo);
	if (ret < 0) {
		fprintf(stderr, "failed to create bo.\n");
		cb_client_shm_bo_destroy(input->bos[input->count_bos].bo);
		input->count_bos = 0;
		client->stop(client);
		return;
	}
	pixel = (u32 *)(bo_info->maps[0]);
	for (i = 0; i < 64 * 64; i++) {
		pixel[i] = 0x80FF0000;
	}
	input->count_bos++;
	set_caps_lock(input);
	client->set_kbd_led_st_cb(client, input, kbd_led_st_cb);
	printf("<<< Get kbd led status\n");
	client->send_get_kbd_led_st(client);
}

static void raw_input_evts_cb(void *userdata,
			      struct cb_raw_input_event *evts,
			      u32 count_evts)
{
	s32 i;

	for (i = 0; i < count_evts; i++) {
		switch (evts[i].type) {
		case EV_ABS:
			printf("EV_ABS %04X %u,%u %d,%d\n", evts[i].code,
				evts[i].v.pos.x,
				evts[i].v.pos.y,
				evts[i].v.pos.dx,
				evts[i].v.pos.dy);
			break;
		case EV_SYN:
			printf("EV_SYN %04X %08X\n", evts[i].code,
				evts[i].v.value);
			break;
		case EV_KEY:
			printf("EV_KEY %04X %08X\n", evts[i].code,
				evts[i].v.value);
			break;
		case EV_REP:
			printf("EV_REP %04X %08X\n", evts[i].code,
				evts[i].v.value);
			break;
		case EV_REL:
			assert(evts[i].code == REL_WHEEL);
			printf("EV_REL REL_WHEEL %08X\n", evts[i].v.value);
			break;
		default:
			printf("%04X %04X %08X\n", evts[i].type, evts[i].code,
				evts[i].v.value);
			break;
		}
	}
}

static s32 show_hide_cb(void *userdata)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;
	struct cb_mc_info mc_info;
	s32 ret;

	input->shown = !input->shown;
	if (input->shown)
		mc_info.type = MC_CMD_TYPE_SHOW;
	else
		mc_info.type = MC_CMD_TYPE_HIDE;
	ret = client->commit_mc(client, &mc_info);
	if (ret < 0) {
		fprintf(stderr, "failed to commit mc %s\n", __func__);
	}
	client->timer_update(client, input->show_hide_timer, 3000, 0);
	return 0;
}

static s32 timeout_cb(void *userdata)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;
	struct cb_mc_info mc_info;
	s32 ret;

	mc_info.cursor.hot_x = 5;
	mc_info.cursor.hot_y = 5;
	mc_info.cursor.w = 64;
	mc_info.cursor.h = 64;
	mc_info.type = MC_CMD_TYPE_SET_CURSOR;
	mc_info.bo_id = input->bos[1 - input->work_bo].bo_id;
	ret = client->commit_mc(client, &mc_info);
	if (ret < 0) {
		fprintf(stderr, "failed to commit mc %s\n", __func__);
	}
	return 0;
}

static s32 timer_cb(void *userdata)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;
	struct cb_mc_info mc_info;
	s32 ret;

	mc_info.cursor.hot_x = 5;
	mc_info.cursor.hot_y = 5;
	mc_info.cursor.w = 64;
	mc_info.cursor.h = 64;
	mc_info.type = MC_CMD_TYPE_SET_CURSOR;
	mc_info.bo_id = input->bos[1 - input->work_bo].bo_id;
	ret = client->commit_mc(client, &mc_info);
	if (ret < 0) {
		fprintf(stderr, "failed to commit mc %s\n", __func__);
	}
	return 0;
}

s32 main(s32 argc, char **argv)
{
	struct cube_input *input;
	struct cb_client *client;

	input = calloc(1, sizeof(*input));
	if (!input)
		return -ENOMEM;

	input->client = cb_client_create(0);
	if (!input->client)
		goto out;

	client = input->client;

	input->delayed_timer = client->add_timer_handler(client, input,
							 timer_cb);
	input->timeout_timer = client->add_timer_handler(client, input,
							 timeout_cb);
	input->show_hide_timer = client->add_timer_handler(client, input,
							   show_hide_cb);
	input->signal_handler = client->add_signal_handler(client, input,
							   SIGINT,
							   signal_cb);
	input->update_led_timer = client->add_timer_handler(client, input,
							    update_led);
	input->capsl_led = true;
	client->timer_update(client, input->show_hide_timer, 3000, 0);

	client->set_ready_cb(client, input, ready_cb);
	client->set_raw_input_evts_cb(client, input, raw_input_evts_cb);
out:
	if (input->client)
		client->run(client);
	client->rm_handler(client, input->delayed_timer);
	client->rm_handler(client, input->timeout_timer);
	client->rm_handler(client, input->show_hide_timer);
	client->rm_handler(client, input->signal_handler);
	client->rm_handler(client, input->update_led_timer);
	while (input->count_bos) {
		client->destroy_bo(client,
				   input->bos[input->count_bos - 1].bo_id);
		cb_client_shm_bo_destroy(input->bos[input->count_bos - 1].bo);
		input->count_bos--;
	}
	client->destroy(client);
	free(input);
	return 0;
}

