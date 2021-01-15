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
#include <unistd.h> /* usleep */
#include <errno.h>
#include <getopt.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_ipc.h>
#include <cube_event.h>
#include <cube_protocal.h>
#include <cube_client.h>

void usage(void)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "cube_manager --log func_level\n");
	fprintf(stderr, "\tSet log level.\n");
	fprintf(stderr, "\t\tcube_manager --log module0,level0:module1,level\n");
	fprintf(stderr, "\t\t\tModule Name:clia/comp/sc/rd/client/touch/"
			"joystick\n");
	fprintf(stderr, "\t\t\tLevel:0-3\n");
	fprintf(stderr, "cube_manager --info\n");
	fprintf(stderr, "\tGet canvas information\n");
	fprintf(stderr, "cube_manager --set-layout=layout_desc\n");
	fprintf(stderr, "\tSet screen layout\n");
	fprintf(stderr, "\t\tLayout_desc: Mode:Output-0:Output-1\n");
	fprintf(stderr, "\t\t\tMode: duplicated/extended\n");
	fprintf(stderr, "\t\t\tOutput-N: x,y/WidthxHeight\n");
	fprintf(stderr, "\t\t\te.g. cube_manager "
			"--set-layout=0,0/1920x1200-0,0/65536x65536@0"
			":0,0/1920x1200-0,0/65536x65536@0\n");
	fprintf(stderr, "\t\t\te.g. cube_manager --set-layout="
			"0,0/2560x1440-0,0/40330x65536@1"
			":2560,0/1600x900-40330,0/25206x40960@1\n");
	fprintf(stderr, "cube_manager --enumerate\n");
	fprintf(stderr, "\tEnumerate all timings for all outputs supported\n");
	fprintf(stderr, "cube_manager --create-mode timing_string\n");
	fprintf(stderr, "\ttiming_string:\n");
	fprintf(stderr,
		"\t\tpipe:clock(khz):hact:hsync_start:hsync_end:htotal:hskew:\n"
		"\t\theight:vsync_start:vsync_end:vtotal:vscan:\n"
		"\t\tvrefresh:interlaced:hsync_polarity:vsync_polarity\n");
	fprintf(stderr, "\t\t1366x768@60 72MHz RB Timing:\n"
			"\t\t\t1:72000:1366:1380:1436:1500:0"
			":768:769:772:800:0:60:0:1:1\n");
	fprintf(stderr, "cube_manager --edid pipe\n");
	fprintf(stderr, "\tGet E-EDID by given connector index.\n");
	fprintf(stderr, "cube_manager --detect-monitor\n");
}

static struct option options[] = {
	{"log", 1, NULL, 'l'},
	{"info", 0, NULL, 'i'},
	{"set-layout", 1, NULL, 's'},
	{"enumerate", 0, NULL, 'e'},
	{"create-mode", 1, NULL, 'c'},
	{"detect-monitor", 0, NULL, 'd'},
	{"edid", 1, NULL, 'x'},
	{NULL, 0, NULL, 0},
};

static char short_options[] = "l:is:ec:dx:";

struct cube_manager {
	struct cb_client *client;
	char param_str[1024];
	struct cb_debug_flags dbg;
	bool log_pending;
	bool query_layout_pending;
	bool query_edid_pending;
	bool enumerate_pending;
	s32 enumerate_pipe;
	s32 edid_pipe;
	bool change_layout_pending;
	bool create_mode_pending;
	struct mode_info custom_mode;
	s32 custom_mode_pipe;
	bool detect_mode;
};

static void parse_log_param(char *param, struct cb_debug_flags *dbg)
{
	char *opt = malloc(256);
	char *p, *src;
	u8 *pflag;

	strcpy(opt, param);
	src = opt;
	while (1) {
		p = strtok(src, ",:");
		src = NULL;
		if (!p)
			break;
		if (!strcmp(p, "clia"))
			pflag = &dbg->clia_flag;
		else if (!strcmp(p, "comp"))
			pflag = &dbg->comp_flag;
		else if (!strcmp(p, "sc"))
			pflag = &dbg->sc_flag;
		else if (!strcmp(p, "rd"))
			pflag = &dbg->rd_flag;
		else if (!strcmp(p, "client"))
			pflag = &dbg->client_flag;
		else if (!strcmp(p, "touch"))
			pflag = &dbg->touch_flag;
		else if (!strcmp(p, "joystick"))
			pflag = &dbg->joystick_flag;
		else {
			fprintf(stderr, "illegal module name %s\n", p);
			exit(1);
		}
		p = strtok(src, ",:");
		if (!p) {
			usage();
			exit(1);
		}
		*pflag = (u8)atoi(p);
		if ((*pflag) > 3) {
			fprintf(stderr, "log level out of range %u\n", *pflag);
			exit(1);
		}
	}
	printf("DEBUG SETTING:\n");
	printf("\tclient agent: %u\n", dbg->clia_flag);
	printf("\tcompositor: %u\n", dbg->comp_flag);
	printf("\tscanout: %u\n", dbg->sc_flag);
	printf("\trenderer: %u\n", dbg->rd_flag);
	printf("\tclient: %u\n", dbg->client_flag);
	free(opt);
}

static void parse_mode_create_param(char *param, struct cb_client *client,
				    struct mode_info *mode, s32 *pipe)
{
	char *opt = malloc(256);
	char *p, *src;

	strcpy(opt, param);
	src = opt;

	p = strtok(src, ":");
	if (src)
		src = NULL;
	if (!p)
		return;
	*pipe = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->clock = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->width = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->hsync_start = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->hsync_end = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->htotal = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->hskew = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->height = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->vsync_start = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->vsync_end = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->vtotal = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->vscan = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->vrefresh = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->interlaced = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->pos_hsync = atoi(p);

	p = strtok(src, ":");
	if (!p)
		return;
	mode->pos_vsync = atoi(p);

	printf("pipe: %d\n", *pipe);
	printf("Size: %ux%u\n", mode->width, mode->height);
	printf("HSync Start: %u HSync End: %u HTotal: %u HSkew: %u\n",
		mode->hsync_start, mode->hsync_end, mode->htotal, mode->hskew);
	printf("VSync Start: %u VSync End: %u VTotal: %u VScan: %u\n",
		mode->vsync_start, mode->vsync_end, mode->vtotal, mode->vscan);
	printf("Vrefresh: %u\n", mode->vrefresh);
	printf("HSync Polarity: %c VSync Polarity: %c\n",
		mode->pos_hsync ? '+' : '-',
		mode->pos_vsync ? '+' : '-');
	mode->preferred = false;

	free(opt);
}

static void parse_layout_param(char *param, struct cb_client *client)
{
	char *opt = malloc(256);
	char *p, *src;
	s32 i = 0, mode_index, j;
	struct cb_client_mode_desc *m;

	strcpy(opt, param);
	src = opt;

	while (1) {
		p = strtok(src, ",");
		if (src)
			src = NULL;
		if (!p)
			break;
		client->displays[i].desktop_rc.pos.x = atoi(p);
		
		p = strtok(NULL, "/");
		if (!p)
			break;
		client->displays[i].desktop_rc.pos.y = atoi(p);
		
		p = strtok(NULL, "x");
		if (!p)
			break;
		client->displays[i].desktop_rc.w = atoi(p);

		p = strtok(NULL, "-");
		if (!p)
			break;
		client->displays[i].desktop_rc.h = atoi(p);

		p = strtok(NULL, ",");
		if (!p)
			break;
		client->displays[i].input_rc.pos.x = atoi(p);
		
		p = strtok(NULL, "/");
		if (!p)
			break;
		client->displays[i].input_rc.pos.y = atoi(p);
		
		p = strtok(NULL, "x");
		if (!p)
			break;
		client->displays[i].input_rc.w = atoi(p);

		p = strtok(NULL, "@");
		if (!p)
			break;
		client->displays[i].input_rc.h = atoi(p);

		p = strtok(NULL, ":");
		if (!p)
			break;
		mode_index = atoi(p);

		printf("desktop[%d]: %d,%d %ux%u\n", i,
			client->displays[i].desktop_rc.pos.x,
			client->displays[i].desktop_rc.pos.y,
			client->displays[i].desktop_rc.w,
			client->displays[i].desktop_rc.h);
		printf("input[%d]: %d,%d %ux%u\n", i,
			client->displays[i].input_rc.pos.x,
			client->displays[i].input_rc.pos.y,
			client->displays[i].input_rc.w,
			client->displays[i].input_rc.h);
		j = 0;
		list_for_each_entry(m, &client->displays[i].modes, link) {
			if (j == mode_index)
				break;
			j++;
		}
		if (mode_index != -1) {
			if (mode_index == 999) {
				client->displays[i].pending_mode =
					client->displays[i].mode_custom;
			} else {
				printf("mode index: %d %ux%u@%u %p\n",
					mode_index,
					m->info.width, m->info.height,
					m->info.vrefresh,
					m->handle);
				client->displays[i].pending_mode = m->handle;
			}
		} else {
			client->displays[i].pending_mode = NULL;
		}
		i++;
	}
	free(opt);
}

static void ready_cb(void *userdata)
{
	struct cube_manager *manager = userdata;
	struct cb_client *client = manager->client;
	s32 ret;

	client->set_client_cap(client,
			       CB_CLIENT_CAP_NOTIFY_LAYOUT |
			       CB_CLIENT_CAP_HPD);
	if (manager->log_pending) {
		manager->log_pending = false;
		ret = client->set_server_dbg(client, &manager->dbg);
		/* wait server receive this message */
		usleep(10000);
		client->stop(client);
		if (ret < 0) {
			fprintf(stderr, "failed to set debug level.\n");
			return;
		}
	}
	if (manager->query_layout_pending) {
		manager->query_layout_pending = false;
		ret = client->query_layout(client);
		if (ret < 0) {
			fprintf(stderr, "failed to query layout.\n");
			return;
		}
	}
}

static void layout_query_cb(void *userdata)
{
	struct cube_manager *manager = userdata;
	struct cb_client *client = manager->client;
	s32 i, ret;
	struct cb_client_display *disp;

	for (i = 0; i < client->count_displays; i++) {
		disp = &client->displays[i];
		printf("disp[%d]:\n", i);
		printf("\tpipe: %d\n", disp->pipe);
		printf("\tdesktop_rc: %d,%d %ux%u\n",
			disp->desktop_rc.pos.x, disp->desktop_rc.pos.y,
			disp->desktop_rc.w, disp->desktop_rc.h);
		printf("\tinput_rc: %d,%d %ux%u\n",
			disp->input_rc.pos.x, disp->input_rc.pos.y,
			disp->input_rc.w, disp->input_rc.h);
		printf("\tmode_current: %p\n", disp->mode_current);
		printf("\tmode_custom: %p\n", disp->mode_custom);
		printf("\tConnector name: %s\n", disp->connector_name);
		printf("\tMonitor name: %s\n", disp->monitor_name);
		printf("\tPreferred: %ux%u@%u %0.3fMHz\n",
			disp->width_preferred, disp->height_preferred,
			disp->vrefresh_preferred,
			disp->pixel_freq_preferred / 1000.0f);
		printf("\tenabled: %d\n", disp->enabled);
	}

	if (manager->query_edid_pending) {
		manager->query_edid_pending = false;
		ret = client->send_get_edid(client, (u64)manager->edid_pipe);
		if (ret) {
			fprintf(stderr, "failed to send get edid cmd.\n");
		}
		return;
	}

	if (manager->enumerate_pending) {
		ret = client->enumerate_mode(client, manager->enumerate_pipe,
					     NULL, false, NULL);
		if (ret) {
			fprintf(stderr, "failed to enumerate mode\n");
			return;
		}
	} else if (!manager->detect_mode) {
		client->stop(client);
	}
}

static void layout_changed_cb(void *userdata)
{
	struct cube_manager *manager = userdata;
	struct cb_client *client = manager->client;
	s32 i;
	struct cb_client_display *disp;

	printf("Layout changed:\n");
	for (i = 0; i < client->count_displays; i++) {
		disp = &client->displays[i];
		printf("disp[%d]:\n", i);
		printf("\tpipe: %d\n", disp->pipe);
		printf("\tdesktop_rc: %d,%d %ux%u\n",
			disp->desktop_rc.pos.x, disp->desktop_rc.pos.y,
			disp->desktop_rc.w, disp->desktop_rc.h);
		printf("\tinput_rc: %d,%d %ux%u\n",
			disp->input_rc.pos.x, disp->input_rc.pos.y,
			disp->input_rc.w, disp->input_rc.h);
		printf("\tmode_current: %p\n", disp->mode_current);
		printf("\tmode_custom: %p\n", disp->mode_custom);
		printf("\tConnector name: %s\n", disp->connector_name);
		printf("\tMonitor name: %s\n", disp->monitor_name);
		printf("\tPreferred: %ux%u@%u %0.3fMHz\n",
			disp->width_preferred, disp->height_preferred,
			disp->vrefresh_preferred,
			disp->pixel_freq_preferred / 1000.0f);
		printf("\tenabled: %d\n", disp->enabled);
	}
	client->stop(client);
}

static void enumerate_mode_cb(void *userdata, struct cb_client_mode_desc *mode)
{
	struct cube_manager *manager = userdata;
	struct cb_client *client = manager->client;
	struct cb_client_mode_desc *m;
	s32 ret, i, j;
	bool flag = false;

	if (!mode) {
		manager->enumerate_pending = false;
		for (i = 0; i < client->count_displays; i++) {
			if (client->displays[i].pipe == manager->enumerate_pipe)
				break;
		}
		printf("index\thandle\t\tWxH@vrefresh\tPixel clock\tPreferred\t"
			"Current\n");
		j = 0;
		list_for_each_entry(m, &client->displays[i].modes, link) {
			printf("%d\t%p:\t%ux%u@%u\t%0.3fMHz\t%c\t\t%c\n",
				j++, m->handle, m->info.width, m->info.height,
				m->info.vrefresh, m->info.clock / 1000.0f,
				m->info.preferred ? 'P' : ' ',
				m->handle == client->displays[i].mode_current ?
					'C' : ' ');
		}
		manager->enumerate_pipe++;
		if (manager->enumerate_pipe == client->count_displays) {
			if (manager->change_layout_pending) {
				parse_layout_param(manager->param_str, client);
				manager->change_layout_pending = false;
				ret = client->change_layout(client);
				if (ret < 0) {
					printf("failed to change layout %s\n",
						strerror(-ret));
					client->stop(client);
					return;
				}
				flag = true;
			}
			if (manager->create_mode_pending) {
				parse_mode_create_param(
					manager->param_str,
					client,
					&manager->custom_mode,
					&manager->custom_mode_pipe);
				client->create_mode(client,
						    &manager->custom_mode,
						    manager->custom_mode_pipe);
				flag = true;
			}
			if (!flag)
				client->stop(client);
			return;
		}
		ret = client->enumerate_mode(client, manager->enumerate_pipe,
					     NULL, false, NULL);
		if (ret) {
			fprintf(stderr, "failed to enumerate mode\n");
		}
	} else {
		ret = client->enumerate_mode(client, manager->enumerate_pipe,
					     mode->handle, false, NULL);
		if (ret) {
			fprintf(stderr, "failed to enumerate mode\n");
		}
	}
}

static void mode_created_cb(bool success, void *userdata)
{
	struct cube_manager *manager = userdata;
	struct cb_client *client = manager->client;

	if (!success) {
		fprintf(stderr, "failed to create mode.\n");
		return;
	}

	printf("create custom successful.\n");
	client->stop(client);
}

static void hpd_cb(void *userdata, struct cb_connector_info *info)
{
	struct cb_connector_info conn_info;

	memcpy(&conn_info, info, sizeof(*info));
	printf("*** Pipe %d hotplug occur: %s\n",
		conn_info.pipe,
		conn_info.enabled ? "Enabled" : "Disabled");
	printf("*** Connector name: %s, Monitor name: %s\n",
		conn_info.connector_name,
		conn_info.monitor_name);
	printf("*** Preferred mode: %ux%u@%u %0.3fMHz\n",
		conn_info.width, conn_info.height,
		conn_info.vrefresh,
		conn_info.pixel_freq / 1000.0f);
	printf("*** Current mode: %ux%u@%u %0.3fMHz\n",
		conn_info.width_cur, conn_info.height_cur,
		conn_info.vrefresh_cur,
		conn_info.pixel_freq_cur / 1000.0f);
}

static void query_edid_cb(void *userdata, u64 pipe, u8 *edid, size_t edid_len)
{
	s32 i;

	printf("EDID pipe: %lu Size: %lu\n", pipe, edid_len);
	for (i = 0; i < edid_len; i++) {
		printf("%02X ", edid[i]);
		if (!((i+1) % 16))
			printf("\n");
	}
}

s32 main(s32 argc, char **argv)
{
	s32 ch;
	struct cube_manager *manager;
	struct cb_client *client;

	manager = calloc(1, sizeof(*manager));
	if (!manager)
		return -ENOMEM;
	manager->log_pending = false;
	manager->query_layout_pending = false;
	manager->enumerate_pending = false;
	manager->change_layout_pending = false;
	manager->create_mode_pending = false;

	manager->client = cb_client_create(0);
	if (!manager->client)
		goto out;
	client = manager->client;

	client->set_ready_cb(client, manager, ready_cb);
	client->set_layout_query_cb(client, manager, layout_query_cb);
	client->set_layout_changed_cb(client, manager, layout_changed_cb);
	client->set_enumerate_mode_cb(client, manager, enumerate_mode_cb);
	client->set_create_mode_cb(client, manager, mode_created_cb);
	client->set_hpd_cb(client, manager, hpd_cb);
	client->set_get_edid_cb(client, manager, query_edid_cb);

	while ((ch = getopt_long(argc, argv, short_options,
				 options, NULL)) != -1) {
		switch (ch) {
		case 'l':
			parse_log_param(optarg, &manager->dbg);
			manager->log_pending = true;
			break;
		case 'i':
			manager->query_layout_pending = true;
			break;
		case 'e':
			manager->query_layout_pending = true;
			manager->enumerate_pipe = 0;
			manager->enumerate_pending = true;
			break;
		case 's':
			manager->query_layout_pending = true;
			manager->enumerate_pipe = 0;
			manager->enumerate_pending = true;
			strcpy(manager->param_str, optarg);
			manager->change_layout_pending = true;
			break;
		case 'c':
			manager->query_layout_pending = true;
			manager->enumerate_pipe = 0;
			manager->enumerate_pending = true;
			manager->create_mode_pending = true;
			strcpy(manager->param_str, optarg);
			break;
		case 'd':
			manager->query_layout_pending = true;
			manager->detect_mode = true;
			break;
		case 'x':
			manager->query_layout_pending = true;
			manager->query_edid_pending = true;
			manager->edid_pipe = atoi(optarg);
			break;
		default:
			usage();
			client->stop(client);
			goto out;
		}
	}

out:
	if (manager->client)
		client->run(client);
	client->destroy(client);
	free(manager);

	return 0;
}

