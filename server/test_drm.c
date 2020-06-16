#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <drm_fourcc.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_scanout.h>
#include <cube_compositor.h>


struct scandev {
	struct cb_event_loop *loop;
	bool run;
	struct plane *primary, *cursor, *overlay;
	struct dma_buf *dumb_buf[2];
	u32 work_index;
	struct cb_buffer *buffer[2];
	struct scanout_commit_info *commit;
	struct cb_rect src_rc, dst_rc;
	void *sd;
	struct scanout *so;
};

struct scandev *dev;

struct dma_buf {
	struct cb_buffer_info info;
	u32 handles[4];
	s32 fd;
};

static void xrgb_buf_destroy(struct dma_buf *buf)
{
	struct drm_mode_destroy_dumb destroy_arg;

	if (buf->info.maps[0] && buf->info.sizes[0]) {
		munmap(buf->info.maps[0], buf->info.sizes[0]);
	}

	if (buf->handles[0]) {
		memset(&destroy_arg, 0, sizeof(destroy_arg));
		destroy_arg.handle = buf->handles[0];
		drmIoctl(buf->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
	}

	if (buf->info.fd[0] > 0)
		close(buf->info.fd[0]);

	if (buf->fd > 0)
		close(buf->fd);

	free(buf);
}

static struct dma_buf *xrgb_buf_create(u32 width, u32 height)
{
	struct dma_buf *buf = calloc(1, sizeof(*buf));
	struct drm_mode_create_dumb create_arg;
	struct drm_mode_map_dumb map_arg;
	s32 ret;

	buf->fd = open("/dev/dri/card0", O_RDWR, 0644);
	buf->info.pix_fmt = CB_PIX_FMT_XRGB8888;
	buf->info.width = width;
	buf->info.height = height;

	memset(&create_arg, 0, sizeof(create_arg));
	create_arg.bpp = 32;
	create_arg.width = (width + 16 - 1) & ~(16 - 1);
	create_arg.height = (height + 16 - 1) & ~(16 - 1);
	ret = drmIoctl(buf->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg);

	if (ret) {
		printf("failed to create dumb buffer. (%s)\n", strerror(errno));
		close(buf->fd);
		return NULL;
	}

	buf->info.sizes[0] = create_arg.size;
	buf->info.strides[0] = create_arg.pitch;
	buf->info.offsets[0] = 0;
	buf->info.planes = 1;

	ret = drmPrimeHandleToFD(buf->fd, create_arg.handle, 0,
				 &buf->info.fd[0]);
	if (ret) {
		printf("failed to export buffer. (%s)\n", strerror(errno));
		close(buf->fd);
		return NULL;
	}

	buf->handles[0] = create_arg.handle;

	memset(&map_arg, 0, sizeof(map_arg));
	map_arg.handle = buf->handles[0];
	ret = drmIoctl(buf->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg);
	if (ret) {
		printf("failed to map dumb. (%s)\n", strerror(errno));
		return NULL;
	}
	buf->info.maps[0] = mmap(NULL, buf->info.sizes[0],
				 PROT_WRITE, MAP_SHARED,
				 buf->fd, map_arg.offset);
	if (buf->info.maps[0] == MAP_FAILED) {
		printf("failed to mmap. (%s)\n", strerror(errno));
		return NULL;
	}

	return buf;
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

static void head_changed_cb(struct cb_listener *listener, void *data)
{
	struct head *head = data;

	printf("head changed. (%s)\n", head->connected
		? "connected" : "disconnected");
	if (head->connected) {
		printf("Head %s is connected\n", head->connector_name);
		head->output->enable(head->output, NULL);
	} else {
		head->output->disable(head->output);
	}
}

static void output_complete_cb(struct cb_listener *listener, void *data)
{
	struct output *output = data;

	dev->work_index = 1 - dev->work_index;
	fill_dumb(dev->dumb_buf[dev->work_index]->info.maps[0],
			  dev->dumb_buf[dev->work_index]->info.width,
			  dev->dumb_buf[dev->work_index]->info.height,
			  dev->dumb_buf[dev->work_index]->info.strides[0]);

	dev->commit = scanout_commit_info_alloc();
	scanout_commit_add_fb_info(dev->commit, dev->buffer[dev->work_index],
				   output, dev->primary, &dev->src_rc,
				   &dev->dst_rc,
				   dev->primary->zpos);
	dev->sd = dev->so->scanout_data_alloc(dev->so);
	dev->so->fill_scanout_data(dev->so, dev->sd, dev->commit);
	dev->so->do_scanout(dev->so, dev->sd);
	scanout_commit_info_free(dev->commit);
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
		}
	} while (plane);
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

s32 main(s32 argc, char **argv)
{
	struct cb_listener output_l = {
		.notify = output_complete_cb,
	};
	struct cb_mode *preferred;
	struct pipeline cfg = {
		.head_index = 0,
		.output_index = 0,
		.primary_plane_index = 0,
		.cursor_plane_index = 1,
		.overlay_plane_index = -1,
	};
	struct output *output;
	struct cb_listener l = {
		.notify = head_changed_cb,
	};
	
	if (argc == 6) {
		cfg.head_index = atoi(argv[1]);
		cfg.output_index = atoi(argv[2]);
		cfg.primary_plane_index = atoi(argv[3]);
		cfg.cursor_plane_index = atoi(argv[4]);
		cfg.overlay_plane_index = atoi(argv[5]);
	}

	cb_log_init("/tmp/cube_log_server-0");
	dev = calloc(1, sizeof(*dev));
	dev->loop = cb_event_loop_create();
	dev->so = scanout_create("/dev/dri/card0", dev->loop);
	if (!dev->so)
		goto out;

	output = dev->so->pipeline_create(dev->so, &cfg);
	if (!output)
		goto out;
	output->head->add_head_changed_notify(output->head, &l);
	output->add_output_complete_notify(output, &output_l);

	parse_plane_info(output, &dev->primary, &dev->cursor, &dev->overlay);

	dev->run = true;
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
		output->enable(output, NULL);
		dev->buffer[0] = dev->so->import_buffer(dev->so,
						&dev->dumb_buf[0]->info);
		dev->buffer[1] = dev->so->import_buffer(dev->so,
						&dev->dumb_buf[1]->info);
		dev->commit = scanout_commit_info_alloc();
		scanout_commit_add_fb_info(dev->commit, dev->buffer[0],
					   output, dev->primary, &dev->src_rc,
					   &dev->dst_rc,
					   dev->primary->zpos);
		dev->sd = dev->so->scanout_data_alloc(dev->so);
		dev->so->fill_scanout_data(dev->so, dev->sd, dev->commit);
		dev->so->do_scanout(dev->so, dev->sd);
		scanout_commit_info_free(dev->commit);

		dev->commit = scanout_commit_info_alloc();
		scanout_commit_add_fb_info(dev->commit, dev->buffer[1],
					   output, dev->primary, &dev->src_rc,
					   &dev->dst_rc,
					   dev->primary->zpos);
		dev->sd = dev->so->scanout_data_alloc(dev->so);
		dev->so->fill_scanout_data(dev->so, dev->sd, dev->commit);
		dev->so->do_scanout(dev->so, dev->sd);
		scanout_commit_info_free(dev->commit);
		dev->work_index = 1;
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
