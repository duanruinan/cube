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
	s32 count_fds;
	s32 planes;
	s32 fds[4];
	void *maps[4];
	u32 pitches[4];
	u32 offsets[4];
	u32 sizes[4];
};

struct cube_input {
	struct cb_client *client;
	struct mc_bo_info bo;
	
	void *mc_repaint_timer;
	void *update_led_timer;
	bool capsl_led;
	void *signal_handler;
	bool shown;

	s32 state;
	s32 repaint_sleep_1;
	s32 repaint_sleep_2;
	s32 repaint_sleep_3;
	s32 repaint_sleep_4;
	bool loop;
};

static s32 signal_cb(s32 signal_number, void *userdata)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;

	printf("[TEST_MC] receive signal\n");
	client->stop(client);
	return 0;
}

static void mc_commited_cb(bool success, void *userdata, u64 bo_id)
{
	if (!success) {
		printf("[TEST_MC] failed to commit mc %ld\n", (s64)bo_id);
	}
}

static s32 mc_repaint_cb(void *userdata)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;
	struct cb_mc_info mc_info;
	s32 ret, i;
	u32 *pixel;

	printf("[TEST_MC] mc repaint\n");

	switch (input->state) {
	case 0:
		printf("[TEST_MC] mc repaint to red\n");
		pixel = (u32 *)(input->bo.maps[0]);
		for (i = 0; i < 64 * 64; i++) {
			pixel[i] = 0x80FF0000;
		}
		mc_info.cursor.hot_x = 5;
		mc_info.cursor.hot_y = 5;
		mc_info.cursor.w = 64;
		mc_info.cursor.h = 64;
		mc_info.type = MC_CMD_TYPE_SET_CURSOR;
		mc_info.bo_id = input->bo.bo_id;
		mc_info.alpha_src_pre_mul = false;
		ret = client->commit_mc(client, &mc_info);
		if (ret < 0) {
			fprintf(stderr, "[TEST_MC] failed to commit rd mc\n");
		}
		if (!input->repaint_sleep_1)
			return 0;
		input->state = 1;
		client->timer_update(client, input->mc_repaint_timer,
					     input->repaint_sleep_1, 0);
		break;
	case 1:
		printf("[TEST_MC] mc repaint to green\n");
		pixel = (u32 *)(input->bo.maps[0]);
		for (i = 0; i < 64 * 64; i++) {
			pixel[i] = 0x8000FF00;
		}
		mc_info.cursor.hot_x = 5;
		mc_info.cursor.hot_y = 5;
		mc_info.cursor.w = 64;
		mc_info.cursor.h = 64;
		mc_info.type = MC_CMD_TYPE_SET_CURSOR;
		mc_info.bo_id = input->bo.bo_id;
		mc_info.alpha_src_pre_mul = false;
		ret = client->commit_mc(client, &mc_info);
		if (ret < 0) {
			fprintf(stderr, "[TEST_MC] failed to commit gr mc\n");
		}
		if (!input->repaint_sleep_2)
			return 0;
		input->state = 2;
		client->timer_update(client, input->mc_repaint_timer,
					     input->repaint_sleep_2, 0);
		break;
	case 2:
		printf("[TEST_MC] mc repaint to blue\n");
		pixel = (u32 *)(input->bo.maps[0]);
		for (i = 0; i < 64 * 64; i++) {
			pixel[i] = 0x800000FF;
		}
		mc_info.cursor.hot_x = 5;
		mc_info.cursor.hot_y = 5;
		mc_info.cursor.w = 64;
		mc_info.cursor.h = 64;
		mc_info.type = MC_CMD_TYPE_SET_CURSOR;
		mc_info.bo_id = input->bo.bo_id;
		mc_info.alpha_src_pre_mul = false;
		ret = client->commit_mc(client, &mc_info);
		if (ret < 0) {
			fprintf(stderr, "[TEST_MC] failed to commit bl mc\n");
		}
		if (!input->repaint_sleep_3)
			return 0;
		input->state = 3;
		client->timer_update(client, input->mc_repaint_timer,
					     input->repaint_sleep_3, 0);
		break;
	case 3:
		if (input->shown) {
			printf("Hide mc cursor\n");
			mc_info.type = MC_CMD_TYPE_HIDE;
			ret = client->commit_mc(client, &mc_info);
			if (ret < 0) {
				fprintf(stderr, "[TEST_MC] failed to hide "
					"cursor\n");
			}
			input->shown = false;
			if (!input->repaint_sleep_4)
				return 0;
			client->timer_update(client,
					     input->mc_repaint_timer,
					     input->repaint_sleep_4, 0);
		} else {
			printf("Show mc cursor\n");
			mc_info.type = MC_CMD_TYPE_SHOW;
			ret = client->commit_mc(client, &mc_info);
			if (ret < 0) {
				fprintf(stderr, "[TEST_MC] failed to show "
					"cursor\n");
			}
			input->shown = true;
			if (input->loop) {
				input->state = 0;
				client->timer_update(client,
						     input->mc_repaint_timer,
						     input->repaint_sleep_4, 0);
			}
		}
		
		break;
	default:
		fprintf(stderr, "[TEST_MC] unknown state %d\n", input->state);
		break;
	}

	return 0;
}

static void mc_bo_created_cb(bool success, void *userdata, u64 bo_id)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;

	if (!success) {
		fprintf(stderr, "[TEST_MC] failed to create bo\n");
		exit(1);
	}

	printf("[TEST_MC] bo_id: %016lX\n", bo_id);
	input->bo.bo_id = bo_id;

	printf("[TEST_MC] create mc bo complete.\n");

	client->set_commit_mc_cb(client, input, mc_commited_cb);

	client->timer_update(client, input->mc_repaint_timer, 100, 0);
	input->state = 0;
	
	return;
}

static void set_caps_lock(struct cube_input *input)
{
	struct cb_client *client = input->client;

	printf("[TEST_MC] >>> Set capslock on\n");
	client->send_set_kbd_led_st(client, (1 << CB_KBD_LED_STATUS_CAPSL));
	input->capsl_led = !input->capsl_led;
}

static s32 update_led(void *userdata)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;

	if (input->capsl_led) {
		printf("[TEST_MC] >>> Set capslock on\n");
		client->send_set_kbd_led_st(client,
					    (1 << CB_KBD_LED_STATUS_CAPSL));
	} else {
		printf("[TEST_MC] >>> Set numlock on\n");
		client->send_set_kbd_led_st(client,
					    (1 << CB_KBD_LED_STATUS_NUML));
	}
	input->capsl_led = !input->capsl_led;
	printf("[TEST_MC] <<< Get kbd led status\n");
	client->send_get_kbd_led_st(client);

	return 0;
}

static void kbd_led_st_cb(void *userdata, u32 led_status)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;

	printf("[TEST_MC][READ] LED status: %08X\n", led_status);
	client->timer_update(client, input->update_led_timer, 2000, 0);
}

static void ready_cb(void *userdata)
{
	struct cube_input *input = userdata;
	struct cb_client *client = input->client;
	struct mc_bo_info *bo_info;
	s32 ret, i;
	u32 *pixel;

	client->set_client_cap(client,
			       CB_CLIENT_CAP_RAW_INPUT |
			       CB_CLIENT_CAP_MC);
	client->set_raw_input_en(client, true);

	bo_info = &input->bo;
	bo_info->bo = cb_client_shm_bo_create(CB_PIX_FMT_ARGB8888, 64, 64,
					      64, 64,
					      &bo_info->count_fds,
					      &bo_info->planes,
					      bo_info->fds,
					      bo_info->maps,
					      bo_info->pitches,
					      bo_info->offsets,
					      bo_info->sizes);
	if (!bo_info->bo) {
		fprintf(stderr, "[TEST_MC] failed to create shm mc bo\n");
		client->stop(client);
		return;
	}
	printf("[TEST_MC] maps: %p %p %p %p\n",
		bo_info->maps[0],
		bo_info->maps[1],
		bo_info->maps[2],
		bo_info->maps[3]);
	printf("[TEST_MC] pitches: %u %u %u %u\n",
		bo_info->pitches[0],
		bo_info->pitches[1],
		bo_info->pitches[2],
		bo_info->pitches[3]);
	printf("[TEST_MC] offsets: %u %u %u %u\n",
		bo_info->offsets[0],
		bo_info->offsets[1],
		bo_info->offsets[2],
		bo_info->offsets[3]);
	printf("[TEST_MC] sizes: %u %u %u %u\n",
		bo_info->sizes[0],
		bo_info->sizes[1],
		bo_info->sizes[2],
		bo_info->sizes[3]);
	client->set_create_bo_cb(client, input, mc_bo_created_cb);
	ret = client->create_bo(client, bo_info->bo);
	if (ret < 0) {
		fprintf(stderr, "[TEST_MC] failed to create bo.\n");
		cb_client_shm_bo_destroy(input->bo.bo);
		client->stop(client);
		return;
	}
	pixel = (u32 *)(bo_info->maps[0]);
	for (i = 0; i < 64 * 64; i++) {
		pixel[i] = 0x80FF0000;
	}
	set_caps_lock(input);
	client->set_kbd_led_st_cb(client, input, kbd_led_st_cb);
	printf("[TEST_MC] <<< Get kbd led status\n");
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

s32 main(s32 argc, char **argv)
{
	struct cube_input *input;
	struct cb_client *client;

	if (argc < 5) {
		fprintf(stderr, "test_mc sleep1 sleep2 sleep3 sleep4 "
			"loop_or_not\n");
		return -1;
	}

	input = calloc(1, sizeof(*input));
	if (!input)
		return -ENOMEM;

	input->client = cb_client_create(0);
	if (!input->client)
		goto out;

	input->shown = true;

	input->repaint_sleep_1 = atoi(argv[1]);
	input->repaint_sleep_2 = atoi(argv[2]);
	input->repaint_sleep_3 = atoi(argv[3]);
	input->repaint_sleep_4 = atoi(argv[4]);
	input->loop = false;
	if (argc == 6) {
		if (!strcmp(argv[5], "Y")) {
			input->loop = true;
		}
	}

	client = input->client;

	input->mc_repaint_timer = client->add_timer_handler(client, input,
							    mc_repaint_cb);
	if (!input->mc_repaint_timer)
		goto out;

	input->signal_handler = client->add_signal_handler(client, input,
							   SIGINT,
							   signal_cb);
	input->update_led_timer = client->add_timer_handler(client, input,
							    update_led);
	if (!input->update_led_timer)
		goto out;

	input->capsl_led = true;

	client->set_ready_cb(client, input, ready_cb);
	client->set_raw_input_evts_cb(client, input, raw_input_evts_cb);

	if (input->client)
		client->run(client);
out:
	client->rm_handler(client, input->mc_repaint_timer);
	client->rm_handler(client, input->signal_handler);
	client->rm_handler(client, input->update_led_timer);
	client->destroy_bo(client, input->bo.bo_id);
	cb_client_shm_bo_destroy(input->bo.bo);
	client->destroy(client);
	free(input);
	return 0;
}

