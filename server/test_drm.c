#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
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
};

static void head_changed_cb(struct cb_listener *listener, void *data)
{
	struct head *head = data;

	printf("head changed. (%s)\n", head->connected
		? "connected" : "disconnected");
	if (head->connected) {
		head->output->enable(head->output, NULL, 1920, 1080);
	} else {
		head->output->disable(head->output);
	}
}

s32 main(s32 argc, char **argv)
{
	struct scandev *dev;
	struct scanout *so;
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
	so = scanout_create("/dev/dri/card0", dev->loop);

	output = so->pipeline_create(so, &cfg);
	output->head->add_head_changed_notify(output->head, &l);
	dev->run = true;
	if (output->head->connected)
		output->enable(output, NULL, 1920, 1080);
	while (dev->run) {
		cb_event_loop_dispatch(dev->loop, -1);
	}
	cb_event_loop_destroy(dev->loop);

	free(dev);
	cb_log_fini();
	return 0;
}
