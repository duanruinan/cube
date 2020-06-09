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
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_log.h>
#include <cube_compositor.h>
#include <cube_scanout.h>

static enum cb_log_level drm_dbg = CB_LOG_DEBUG;

#define drm_debug(fmt, ...) do { \
	if (drm_dbg >= CB_LOG_DEBUG) { \
		cb_tlog("[DRM ][DEBUG ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define drm_info(fmt, ...) do { \
	if (drm_dbg >= CB_LOG_INFO) { \
		cb_tlog("[DRM ][INFO  ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define drm_notice(fmt, ...) do { \
	if (drm_dbg >= CB_LOG_NOTICE) { \
		cb_tlog("[DRM ][NOTICE] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define drm_warn(fmt, ...) do { \
	cb_tlog("[DRM ][WARN  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define drm_err(fmt, ...) do { \
	cb_tlog("[DRM ][ERROR ] " fmt, ##__VA_ARGS__); \
} while (0);

struct drm_prop_enum_info {
	const char *name;
	bool supported;
	u64 value;
};

struct drm_prop_info {
	const char *name;
	u32 prop_id;
	u32 flags;
	u32 count_enum_values;
	struct drm_prop_enum_info *enum_values;
	u32 count_range_values;
	u64 range_values[2];
};

enum drm_crtc_prop {
	CRTC_ACTIVE = 0,
	CRTC_MODE_ID,
	CRTC_PROP_COUNT,
};

enum drm_conn_prop {
	CONNECTOR_CRTC_ID = 0,
	CONNECTOR_DPMS,
	CONNECTOR_EDID,
	CONNECTOR_HDMI_QUANT_RANGE,
	CONNECTOR_PROP_COUNT,
};

enum drm_plane_prop {
	PLANE_CRTC_ID = 0,
	PLANE_TYPE,
	PLANE_FB_ID,
	PLANE_SRC_X,
	PLANE_SRC_Y,
	PLANE_SRC_W,
	PLANE_SRC_H,
	PLANE_CRTC_X,
	PLANE_CRTC_Y,
	PLANE_CRTC_W,
	PLANE_CRTC_H,
	PLANE_IN_FORMATS,
	PLANE_IN_FENCE_FD,
	PLANE_IN_DAMAGE_CLIPS,
	PLANE_IN_COLOR_SPACE,
	PLANE_ZPOS,
	PLANE_PROP_COUNT,
};

static const struct drm_prop_info crtc_props[] = {
	[CRTC_ACTIVE] = {
		.name = "ACTIVE",
	},
	[CRTC_MODE_ID] = {
		.name = "MODE_ID",
	},
};

enum drm_dpms_state {
	DPMS_STATE_ON = 0,
	DPMS_STATE_STANDBY,
	DPMS_STATE_SUSPEND,
	DPMS_STATE_OFF,
	DPMS_STATE_COUNT,
};

struct drm_prop_enum_info dpms_state_enums[] = {
	[DPMS_STATE_ON] = {
		.name = "On",
	},
	[DPMS_STATE_STANDBY] = {
		.name = "Standby",
	},
	[DPMS_STATE_SUSPEND] = {
		.name = "Suspend",
	},
	[DPMS_STATE_OFF] = {
		.name = "Off",
	},
};

static const struct drm_prop_info connector_props[] = {
	[CONNECTOR_CRTC_ID] = {
		.name = "CRTC_ID",
	},
	[CONNECTOR_DPMS] = {
		.name = "DPMS",
		.enum_values = dpms_state_enums,
		.count_enum_values = DPMS_STATE_COUNT,
	},
	[CONNECTOR_EDID] = {
		.name = "EDID",
	},
	[CONNECTOR_HDMI_QUANT_RANGE] = {
		.name = "HDMI_QUANT_RANGE",
	},
};

enum drm_plane_type {
	DRM_PLANE_TP_PRIMARY = 0,
	DRM_PLANE_TP_CURSOR,
	DRM_PLANE_TP_OVERLAY,
	DRM_PLANE_TP_COUNT,
};

static struct drm_prop_enum_info plane_type_enums[] = {
	[DRM_PLANE_TP_PRIMARY] = {
		.name = "Primary",
	},
	[DRM_PLANE_TP_CURSOR] = {
		.name = "Cursor",
	},
	[DRM_PLANE_TP_OVERLAY] = {
		.name = "Overlay",
	},
};

static const struct drm_prop_info plane_props[] = {
	[PLANE_CRTC_ID] = {
		.name = "CRTC_ID",
	},
	[PLANE_TYPE] = {
		.name = "TYPE",
		.enum_values = plane_type_enums,
		.count_enum_values = DRM_PLANE_TP_COUNT,
	},
	[PLANE_FB_ID] = {
		.name = "FB_ID",
	},
	[PLANE_SRC_X] = {
		.name = "SRC_X",
	},
	[PLANE_SRC_Y] = {
		.name = "SRC_Y",
	},
	[PLANE_SRC_W] = {
		.name = "SRC_W",
	},
	[PLANE_SRC_H] = {
		.name = "SRC_H",
	},
	[PLANE_CRTC_X] = {
		.name = "CRTC_X",
	},
	[PLANE_CRTC_Y] = {
		.name = "CRTC_Y",
	},
	[PLANE_CRTC_W] = {
		.name = "CRTC_W",
	},
	[PLANE_CRTC_H] = {
		.name = "CRTC_H",
	},
	[PLANE_IN_FORMATS] = {
		.name = "IN_FORMATS",
	},
	[PLANE_IN_FENCE_FD] = {
		.name = "IN_FENCE_FD",
	},
	[PLANE_IN_DAMAGE_CLIPS] = {
		.name = "FB_DAMAGE_CLIPS",
	},
	[PLANE_IN_COLOR_SPACE] = {
		.name = "COLOR_SPACE",
	},
	[PLANE_ZPOS] = {
		.name = "ZPOS",
	},
};

struct drm_output {
	struct output base;

	u32 crtc_id;

	struct list_head link;
	struct drm_plane *primary;
	struct drm_plane *cursor;
	struct drm_plane *overlay;
};

struct drm_head {
	struct head base;

	u32 conector_id;
	drmModeConnectorPtr connector;

	struct list_head link;
};

struct drm_plane {
	struct plane base;

	u32 plane_id;

	struct list_head link;
};

struct drm_scanout {
	struct scanout base;

	s32 fd;
	drmModeResPtr res;
	drmModePlaneResPtr pres;

	struct list_head outputs;
	struct list_head heads;
};

static inline struct drm_scanout *to_dev(struct scanout *so)
{
	return container_of(so, struct drm_scanout, base);
}

static void drm_scanout_set_dbg_level(struct scanout *so,
				      enum cb_log_level level)
{
	if (level >= CB_LOG_ERR && level <= CB_LOG_DEBUG)
		drm_dbg = level;
}

static void drm_scanout_destroy(struct scanout *so)
{
	struct drm_scanout *dev;

	if (!so)
		return;

	dev = to_dev(so);

	if (dev->fd > 0)
		close(dev->fd);

	free(dev);
}

struct scanout *scanout_create(const char *dev_path, struct cb_event_loop *loop)
{
	struct drm_scanout *dev = NULL;

	if (!dev_path) {
		drm_err("dev_path is null");
		goto err;
	}

	if (!loop) {
		drm_err("loop is null");
		goto err;
	}

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		goto err;

	dev->base.destroy = drm_scanout_destroy;
	dev->base.set_dbg_level = drm_scanout_set_dbg_level;

	dev->fd = open(dev_path, O_RDWR | O_CLOEXEC, 0644);
	if (!dev->fd)
		goto err;

	return &dev->base;

err:
	if (dev) {
		dev->base.destroy(&dev->base);
	}
	return NULL;
}

