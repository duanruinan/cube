#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <inttypes.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <drm_fourcc.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <gbm.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_scanout.h>
#include <cube_compositor.h>

#define TEST_ROUTE_PLANE 1

struct scandev {
	bool enabled;
	struct cb_event_loop *loop;
	struct cb_event_source *sig_int_source;
	struct cb_event_source *sig_tem_source;
	struct output *output;
	struct cb_event_source *repaint_timer_0;
	struct cb_event_source *repaint_timer_1;
	struct cb_event_source *disable_timer;
	bool run;
	struct plane *primary, *cursor, *overlay;
	struct cb_buffer *dumb_buf[2];
	u32 work_index;
	struct cb_buffer *buffer[2];
	struct scanout_commit_info *commit;
	struct cb_rect src_rc, dst_rc;
	void *sd;
	struct scanout *so;
	void *surface[2];
	struct cb_buffer *sf_buf[2];
};

struct scandev *dev;

struct dumb_buf {
	struct cb_buffer_info info;
	u32 handles[4];
	s32 fd;
};

static s32 signal_event_proc(s32 signal_number, void *data)
{
	struct scandev *dev = data;

	switch (signal_number) {
	case SIGINT:
		fprintf(stderr, "received SIGINT\n");
		dev->run = false;
		break;
	case SIGTERM:
		fprintf(stderr, "received SIGTERM\n");
		dev->run = false;
		break;
	default:
		fprintf(stderr, "Receive unknown signal %d", signal_number);
		return -1;
	}

	return 0;
}

static void xrgb_buf_destroy(struct cb_buffer *buf)
{
	dev->so->dumb_buffer_destroy(dev->so, buf);
}

static struct cb_buffer *xrgb_buf_create(u32 width, u32 height)
{
	struct cb_buffer_info info;
	struct cb_buffer *buffer;

	memset(&info, 0, sizeof(info));
	info.pix_fmt = CB_PIX_FMT_XRGB8888;
	info.width = width;
	info.height = height;

	buffer = dev->so->dumb_buffer_create(dev->so, &info);
	return buffer;
}

static void fill_dumb(u8 *data, u32 width, u32 height, u32 stride)
{
	const u32 ccolors[] = {
		0xFFFFFFFF, /* white */
		0xFF00FFFF, /* yellow */
		0xFFFFFF00, /* cyan */
		0xFF00FF00, /* green */
		0xFFFF00FF, /* perple */
		0xFF0000FF, /* red */
		0xFFFF0000, /* blue */
		0xFF000000, /* black */
	};

	static u32 colors[] = {
		0xFFFFFFFF, /* white */
		0xFF00FFFF, /* yellow */
		0xFFFFFF00, /* cyan */
		0xFF00FF00, /* green */
		0xFFFF00FF, /* perple */
		0xFF0000FF, /* red */
		0xFFFF0000, /* blue */
		0xFF000000, /* black */
	};

	const u32 delta[] = {
		0x010101, /* white */
		0x000101, /* yellow */
		0x010100, /* cyan */
		0x000100, /* green */
		0x010001, /* perple */
		0x000001, /* red */
		0x010000, /* blue */
		0x000000, /* black */
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

static struct cb_mode *parse_mode_info(struct output *output)
{
	struct cb_mode *last_mode = NULL, *mode, *preferred = NULL;

	do {
		mode = output->enumerate_mode(output, last_mode);
		last_mode = mode;
		if (mode) {
			printf("Mode: %ux%u@%u %.2fMHz %s\n", mode->width,
				mode->height, mode->vrefresh,
				mode->pixel_freq / 1000.0f,
				mode->preferred ? "Preferred" : "");
			if (mode->preferred)
				preferred = mode;
		}
	} while (mode);

	return preferred;
}

void calc_edge(u32 crtc_w, u32 crtc_h, struct cb_rect *src, struct cb_rect *dst)
{
	u32 width, height;
	s32 calc, left, top;

	calc = crtc_w * src->h / src->w;
	if (calc <= crtc_h) {
		left = 0;
		top = (crtc_h - calc) / 2;
		width = crtc_w;
		height = calc;
	} else {
		calc = src->w * crtc_h / src->h;
		left = (crtc_w - calc) / 2;
		top = 0;
		width = calc;
		height = crtc_h;
	}

	dst->pos.x = left;
	dst->pos.y = top;
	dst->w = width;
	dst->h = height;
	printf("%d,%d %ux%u\n", dst->pos.x, dst->pos.y, dst->w, dst->h);
}

static void head_changed_cb(struct cb_listener *listener, void *data)
{
	struct head *head = data;
	struct output *output;
	struct cb_mode *preferred;

	printf("head changed. (%s)\n", head->connected
		? "connected" : "disconnected");
	if (head->connected) {
		printf("Head %s is connected\n", head->connector_name);
		output = head->output;
		preferred = parse_mode_info(output);
		dev->src_rc.pos.x = 0;
		dev->src_rc.pos.y = 0;
		dev->src_rc.w = 640;
		dev->src_rc.h = 480;
		calc_edge(preferred->width, preferred->height,
			  &dev->src_rc, &dev->dst_rc);
		head->output->enable(head->output, NULL);
		dev->enabled = true;
		fill_dumb(dev->dumb_buf[dev->work_index]->info.maps[0],
			  dev->dumb_buf[dev->work_index]->info.width,
			  dev->dumb_buf[dev->work_index]->info.height,
			  dev->dumb_buf[dev->work_index]->info.strides[0]);

		dev->commit = scanout_commit_info_alloc();
		scanout_buffer_dirty_init(dev->buffer[dev->work_index]);
		scanout_set_buffer_dirty(dev->buffer[dev->work_index], output);
		scanout_commit_add_fb_info(dev->commit, dev->buffer[dev->work_index],
				   output, dev->overlay, &dev->src_rc,
				   &dev->dst_rc,
				   dev->overlay->zpos, true);
		dev->sd = dev->so->scanout_data_alloc(dev->so);
		dev->so->fill_scanout_data(dev->so, dev->sd, dev->commit);
		//printf("[APP] commit %d\n", dev->work_index);
		dev->so->do_scanout(dev->so, dev->sd);
		dev->work_index = 1 - dev->work_index;
		scanout_commit_info_free(dev->commit);
	} else {
		output = head->output;
		printf("[APP] try to disable\n");
		dev->enabled = false;
		if (output->disable(output) < 0) {
			cb_event_source_timer_update(dev->disable_timer, 2, 0);
		}
	}
}

static void buffer_complete_cb(struct cb_listener *listener,
			       void *data)
{
	if (data == dev->buffer[0]) {
		printf("[APP] buffer[0] complete. %d->%d\n", dev->work_index, 0);
#ifdef TEST_ONE_BUFFER
		dev->work_index = 1;
#else
		dev->work_index = 0;
#endif
	} else if (data == dev->buffer[1]) {
		printf("[APP] buffer[1] complete. %d->%d\n", dev->work_index, 1);
#ifdef TEST_ONE_BUFFER
		dev->work_index = 0;
#else
		dev->work_index = 1;
#endif
	} else {
//		printf("illegal buffer address.\n");
		return;
	}

	fill_dumb(dev->dumb_buf[dev->work_index]->info.maps[0],
			  dev->dumb_buf[dev->work_index]->info.width,
			  dev->dumb_buf[dev->work_index]->info.height,
			  dev->dumb_buf[dev->work_index]->info.strides[0]);
}

static void buffer_flipped_cb(struct cb_listener *listener,
			      void *data)
{
	if (data == dev->buffer[0]) {
		printf("[APP] buffer[0] flipped.\n");
		if (dev->enabled)
			cb_event_source_timer_update(dev->repaint_timer_0, 8,
				0);
	} else if (data == dev->buffer[1]) {
		printf("[APP] buffer[1] flipped.\n");
		if (dev->enabled)
			cb_event_source_timer_update(dev->repaint_timer_1, 8,
				0);
	} else {
		printf("illegal buffer address.\n");
		return;
	}
}

static void parse_plane_info(struct output *output,
			     struct plane **primary,
			     struct plane **cursor,
			     struct plane **overlay)
{
	struct plane *last_plane = NULL, *plane;
	s32 i;

	do {
		plane = output->enumerate_plane(output, last_plane);
		last_plane = plane;
		if (plane) {
			printf("Plane Info:\n");
			switch (plane->type) {
			case PLANE_TYPE_PRIMARY:
				printf("\tPrimary\n");
				*primary = plane;
				break;
			case PLANE_TYPE_CURSOR:
				printf("\tCursor\n");
				*cursor = plane;
				break;
			case PLANE_TYPE_OVERLAY:
				printf("\tOverlay\n");
				*overlay = plane;
				break;
			default:
				printf("\tUnknown type\n");
				break;
			}
			for (i = 0; i < plane->count_formats; i++)
				printf("\tFormat: %4.4s\n",
					(char *)&plane->formats[i]);
			printf("\tZPOS: %"PRId64"\n", (s64)plane->zpos);
			printf("\tALPHA_SRC: %u\n", plane->alpha_src_pre_mul);
		}
	} while (plane);

	printf("ARGB8888 plane\n");
	last_plane = NULL;
	do {
		plane = output->enumerate_plane_by_fmt(output, last_plane,
						       CB_PIX_FMT_ARGB8888);
		last_plane = plane;
		if (plane) {
			printf("Plane Info:\n");
			switch (plane->type) {
			case PLANE_TYPE_PRIMARY:
				printf("\tPrimary\n");
				*primary = plane;
				break;
			case PLANE_TYPE_CURSOR:
				printf("\tCursor\n");
				*cursor = plane;
				break;
			case PLANE_TYPE_OVERLAY:
				printf("\tOverlay\n");
				*overlay = plane;
				break;
			default:
				printf("\tUnknown type\n");
				break;
			}
		}
	} while (plane);

	printf("NV24 plane\n");
	last_plane = NULL;
	do {
		plane = output->enumerate_plane_by_fmt(output, last_plane,
						       CB_PIX_FMT_NV24);
		last_plane = plane;
		if (plane) {
			printf("Plane Info:\n");
			switch (plane->type) {
			case PLANE_TYPE_PRIMARY:
				printf("\tPrimary\n");
				*primary = plane;
				break;
			case PLANE_TYPE_CURSOR:
				printf("\tCursor\n");
				*cursor = plane;
				break;
			case PLANE_TYPE_OVERLAY:
				printf("\tOverlay\n");
				*overlay = plane;
				break;
			default:
				printf("\tUnknown type\n");
				break;
			}
		}
	} while (plane);
}

static s32 disable_timer_handler(void *data)
{
	s32 ret;
	printf("[APP] try to disable in timer\n");
	ret = dev->output->disable(dev->output);
	if (ret < 0)
		cb_event_source_timer_update(dev->disable_timer,
					2, 0);
	return 0;
}

static s32 output_repaint_timer_handler(void *data)
{
	//struct cb_buffer *buffer = data;
#ifdef TEST_ROUTE_PLANE
	static s32 zpos = 0;
	struct plane *plane;
#endif
	if (!dev->enabled)
		return 0;

	dev->commit = scanout_commit_info_alloc();
/*
	if (buffer == dev->buffer[0]) {
		printf("[APP] buffer 0 flipped.\n");
	} else if (buffer == dev->buffer[1]) {
		printf("[APP] buffer 1 flipped.\n");
	}
*/

#ifdef TEST_ROUTE_PLANE
	zpos = 1 - zpos;
	if (zpos == 1)
		plane = dev->overlay;
	else
		plane = dev->primary;
#endif
	scanout_buffer_dirty_init(dev->buffer[dev->work_index]);
	scanout_set_buffer_dirty(dev->buffer[dev->work_index], dev->output);
	scanout_commit_add_fb_info(dev->commit,
			   dev->buffer[dev->work_index],
			   dev->output,
#ifdef TEST_ROUTE_PLANE
			   plane,
#else
			   dev->overlay,
#endif
			   &dev->src_rc,
			   &dev->dst_rc,
#ifdef TEST_ROUTE_PLANE
			   zpos, true);
#else
			   dev->overlay->zpos, true);
#endif
	
	dev->sd = dev->so->scanout_data_alloc(dev->so);
	dev->so->fill_scanout_data(dev->so, dev->sd, dev->commit);
	//printf("[APP] commit %d\n", dev->work_index);
	dev->so->do_scanout(dev->so, dev->sd);
	scanout_commit_info_free(dev->commit);
	return 0;
}

s32 main(s32 argc, char **argv)
{
	struct cb_mode *preferred;
	struct pipeline cfg = {
		.head_index = 0,
		.output_index = 0,
		.primary_plane_index = 0,
		.cursor_plane_index = 1,
	};
	struct output *output;
	struct cb_listener l = {
		.notify = head_changed_cb,
	};
	struct cb_listener buffer_flipped_l_0 = {
		.notify = buffer_flipped_cb,
	};
	struct cb_listener buffer_flipped_l_1 = {
		.notify = buffer_flipped_cb,
	};
	struct cb_listener buffer_complete_l_0 = {
		.notify = buffer_complete_cb,
	};
	struct cb_listener buffer_complete_l_1 = {
		.notify = buffer_complete_cb,
	};

	INIT_LIST_HEAD(&buffer_flipped_l_0.link);
	INIT_LIST_HEAD(&buffer_flipped_l_1.link);
	INIT_LIST_HEAD(&buffer_complete_l_0.link);
	INIT_LIST_HEAD(&buffer_complete_l_1.link);
	
	if (argc == 6) {
		cfg.head_index = atoi(argv[1]);
		cfg.output_index = atoi(argv[2]);
		cfg.primary_plane_index = atoi(argv[3]);
		cfg.cursor_plane_index = atoi(argv[4]);
	}

	cb_log_init("/tmp/cube_log_server-0");
	dev = calloc(1, sizeof(*dev));
	dev->loop = cb_event_loop_create();
	dev->sig_int_source = cb_event_loop_add_signal(dev->loop, SIGINT,
							signal_event_proc,
							dev);
	dev->sig_tem_source = cb_event_loop_add_signal(dev->loop, SIGTERM,
							signal_event_proc,
							dev);
	dev->so = scanout_create("/dev/dri/card0", dev->loop);
	if (!dev->so)
		goto out;

	output = dev->so->pipeline_create(dev->so, &cfg);
	if (!output)
		goto out;
	dev->output = output;
	output->head->add_head_changed_notify(output->head, &l);

	parse_plane_info(output, &dev->primary, &dev->cursor, &dev->overlay);

	dev->run = true;
	dev->dumb_buf[0] = xrgb_buf_create(640, 480);
	dev->dumb_buf[1] = xrgb_buf_create(640, 480);
	fill_dumb(dev->dumb_buf[0]->info.maps[0],
		  dev->dumb_buf[0]->info.width,
		  dev->dumb_buf[0]->info.height,
		  dev->dumb_buf[0]->info.strides[0]);
	fill_dumb(dev->dumb_buf[1]->info.maps[0],
		  dev->dumb_buf[1]->info.width,
		  dev->dumb_buf[1]->info.height,
		  dev->dumb_buf[1]->info.strides[0]);
	dev->work_index = 0;
	dev->buffer[0] = dev->so->import_dmabuf(dev->so,
					&dev->dumb_buf[0]->info);
	dev->so->add_buffer_complete_notify(dev->so,
					    dev->buffer[0],
					    &buffer_complete_l_0);
	dev->so->add_buffer_flip_notify(dev->so,
					dev->buffer[0],
					&buffer_flipped_l_0);
	dev->buffer[1] = dev->so->import_dmabuf(dev->so,
					&dev->dumb_buf[1]->info);
	dev->so->add_buffer_complete_notify(dev->so,
					    dev->buffer[1],
					    &buffer_complete_l_1);
	dev->so->add_buffer_flip_notify(dev->so,
					dev->buffer[1],
					&buffer_flipped_l_1);
	dev->repaint_timer_0 = cb_event_loop_add_timer(dev->loop,
				output_repaint_timer_handler, dev->buffer[0]);
	dev->repaint_timer_1 = cb_event_loop_add_timer(dev->loop,
				output_repaint_timer_handler, dev->buffer[1]);
	dev->disable_timer = cb_event_loop_add_timer(dev->loop,
				disable_timer_handler, dev);
	if (output->head->connected) {
		printf("Head %s is connected, show mode information.\n",
			output->head->connector_name);
		preferred = parse_mode_info(output);
		dev->src_rc.pos.x = 0;
		dev->src_rc.pos.y = 0;
		dev->src_rc.w = 640;
		dev->src_rc.h = 480;
		calc_edge(preferred->width, preferred->height,
			  &dev->src_rc, &dev->dst_rc);
		output->enable(output, NULL);
		dev->enabled = true;
		
		dev->commit = scanout_commit_info_alloc();
		scanout_buffer_dirty_init(dev->buffer[0]);
		scanout_set_buffer_dirty(dev->buffer[0], output);
		scanout_commit_add_fb_info(dev->commit, dev->buffer[0],
					   output, dev->overlay, &dev->src_rc,
					   &dev->dst_rc,
					   dev->overlay->zpos, true);
		dev->sd = dev->so->scanout_data_alloc(dev->so);
		dev->so->fill_scanout_data(dev->so, dev->sd, dev->commit);
		//printf("[APP] commit %d\n", dev->work_index);
#ifndef TEST_ONE_BUFFER
		dev->work_index = 1 - dev->work_index;
#endif
		dev->so->do_scanout(dev->so, dev->sd);
		scanout_commit_info_free(dev->commit);
	}

	while (dev->run) {
		cb_event_loop_dispatch(dev->loop, -1);
	}
	
	cb_event_loop_destroy(dev->loop);

out:
	xrgb_buf_destroy(dev->dumb_buf[0]);
	xrgb_buf_destroy(dev->dumb_buf[1]);
	if (dev->so)
		dev->so->destroy(dev->so);

	if (dev)
		free(dev);
	cb_log_fini();
	return 0;
}
