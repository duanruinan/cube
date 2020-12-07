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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <libudev.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <cube_utils.h>
#include <cube_event.h>
#include <cube_cache.h>
#include <cube_log.h>
#include <cube_compositor.h>
#include <cube_scanout.h>

#define USE_DRM_PRIME 1

/* static enum cb_log_level drm_dbg = CB_LOG_DEBUG; */
static enum cb_log_level drm_dbg = CB_LOG_NOTICE;

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

enum drm_prop_type {
	DRM_PROP_TYPE_UNKNOWN = 0,
	DRM_PROP_TYPE_ENUM,
	DRM_PROP_TYPE_BLOB,
	DRM_PROP_TYPE_RANGE,
	DRM_PROP_TYPE_SIGNED_RANGE,
	DRM_PROP_TYPE_OBJECT,
	DRM_PROP_TYPE_BITMASK,
};

struct enum_value {
	const char *name;
	bool valid;
	u64 value;
};

struct drm_prop {
	const char *name;
	enum drm_prop_type type;
	u32 prop_id;
	bool atomic;
	bool valid;

	union {
		struct {
			u32 count_values;
			struct enum_value *values;
		} ev;

		struct {
			u32 count_values;
			u64 values[2];
		} rv;
	} c;
};

enum {
	CONNECTOR_PROP_EDID = 0,
	CONNECTOR_PROP_CRTC_ID,
	CONNECTOR_PROP_HDMI_QUANT_RANGE,
	CONNECTOR_PROP_HDMI_OUTPUT_COLORIMETRY,
	CONNECTOR_PROP_BRIGHTNESS,
	CONNECTOR_PROP_CONTRAST,
	CONNECTOR_PROP_SATURATION,
	CONNECTOR_PROP_HUE,
	CONNECTOR_PROP_NR,
};

enum {
	CONNECTOR_HDMI_QUANT_RANGE_IDX_DEFAULT,
	CONNECTOR_HDMI_QUANT_RANGE_IDX_LIMIT,
	CONNECTOR_HDMI_QUANT_RANGE_IDX_FULL,
	CONNECTOR_HDMI_QUANT_RANGE_IDX_NR,
};

static struct enum_value connector_hdmi_quant_range_enum[] = {
	[CONNECTOR_HDMI_QUANT_RANGE_IDX_DEFAULT] = {
		.name = "default",
	},
	[CONNECTOR_HDMI_QUANT_RANGE_IDX_LIMIT] = {
		.name = "limit",
	},
	[CONNECTOR_HDMI_QUANT_RANGE_IDX_FULL] = {
		.name = "full",
	},
};

enum {
	CONNECTOR_HDMI_OUTPUT_COLORIMETRY_IDX_NONE,
	CONNECTOR_HDMI_OUTPUT_COLORIMETRY_IDX_ITU_2020,
	CONNECTOR_HDMI_OUTPUT_COLORIMETRY_IDX_NR,
};

static struct enum_value connector_hdmi_output_colorimetry_enum[] = {
	[CONNECTOR_HDMI_OUTPUT_COLORIMETRY_IDX_NONE] = {
		.name = "None",
	},
	[CONNECTOR_HDMI_OUTPUT_COLORIMETRY_IDX_ITU_2020] = {
		.name = "ITU_2020",
	},
};

static const struct drm_prop connector_props[] = {
	[CONNECTOR_PROP_EDID] = {
		.name = "EDID",
		.type = DRM_PROP_TYPE_BLOB,
	},
	[CONNECTOR_PROP_HDMI_QUANT_RANGE] = {
		.name = "HDMI_QUANT_RANGE",
		.type = DRM_PROP_TYPE_ENUM,
		.c = {
			.ev = {
				.count_values
					= CONNECTOR_HDMI_QUANT_RANGE_IDX_NR,
				.values = connector_hdmi_quant_range_enum,
			},
		},
	},
	[CONNECTOR_PROP_HDMI_OUTPUT_COLORIMETRY] = {
		.name = "hdmi_output_colorimetry",
		.type = DRM_PROP_TYPE_ENUM,
		.c = {
			.ev = {
				.count_values
					= CONNECTOR_HDMI_OUTPUT_COLORIMETRY_IDX_NR,
				.values = connector_hdmi_output_colorimetry_enum,
			},
		},
	},
	[CONNECTOR_PROP_CRTC_ID] = {
		.name = "CRTC_ID",
		.type = DRM_PROP_TYPE_OBJECT,
	},
	[CONNECTOR_PROP_BRIGHTNESS] = {
		.name = "brightness",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[CONNECTOR_PROP_CONTRAST] = {
		.name = "contrast",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[CONNECTOR_PROP_SATURATION] = {
		.name = "saturation",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[CONNECTOR_PROP_HUE] = {
		.name = "hue",
		.type = DRM_PROP_TYPE_RANGE,
	},
};

enum {
	PLANE_PROP_TYPE = 0,
	PLANE_PROP_FEATURE,
	PLANE_PROP_FB_ID,
	PLANE_PROP_IN_FENCE_FD,
	PLANE_PROP_CRTC_ID,
	PLANE_PROP_CRTC_X,
	PLANE_PROP_CRTC_Y,
	PLANE_PROP_CRTC_W,
	PLANE_PROP_CRTC_H,
	PLANE_PROP_SRC_X,
	PLANE_PROP_SRC_Y,
	PLANE_PROP_SRC_W,
	PLANE_PROP_SRC_H,
	PLANE_PROP_ZPOS,
	PLANE_PROP_COLOR_SPACE,
	PLANE_PROP_GLOBAL_ALPHA,
	PLANE_PROP_BLEND_MODE,
	PLANE_PROP_ALPHA_SRC_PRE_MUL,
	PLANE_PROP_NR,
};

enum {
	PLANE_FEATURE_IDX_SCALE = 0,
	PLANE_FEATURE_IDX_ALPHA,
	PLANE_FEATURE_IDX_HDR2SDR,
	PLANE_FEATURE_IDX_SDR2HDR,
	PLANE_FEATURE_IDX_AFBDC,
	PLANE_FEATURE_IDX_PDAF_POS,
	PLANE_FEATURE_IDX_NR,
};

static struct enum_value plane_feature_enum[] = {
	[PLANE_FEATURE_IDX_SCALE] = {
		.name = "scale",
	},
	[PLANE_FEATURE_IDX_ALPHA] = {
		.name = "alpha",
	},
	[PLANE_FEATURE_IDX_HDR2SDR] = {
		.name = "hdr2sdr",
	},
	[PLANE_FEATURE_IDX_SDR2HDR] = {
		.name = "sdr2hdr",
	},
	[PLANE_FEATURE_IDX_AFBDC] = {
		.name = "afbdc",
	},
	[PLANE_FEATURE_IDX_PDAF_POS] = {
		.name = "pdaf_pos",
	},
};

enum {
	PLANE_TYPE_IDX_PRIMARY = 0,
	PLANE_TYPE_IDX_OVERLAY,
	PLANE_TYPE_IDX_CURSOR,
	PLANE_TYPE_IDX_NR,
};

static struct enum_value plane_type_enum[] = {
	[PLANE_TYPE_IDX_PRIMARY] = {
		.name = "Primary",
	},
	[PLANE_TYPE_IDX_OVERLAY] = {
		.name = "Overlay",
	},
	[PLANE_TYPE_IDX_CURSOR] = {
		.name = "Cursor",
	},
};

enum {
	PLANE_ALPHA_SRC_PRE_MUL = 0,
	PLANE_ALPHA_SRC_NON_PRE_MUL,
	PLANE_ALPHA_SRC_NR,
};

static struct enum_value plane_alpha_src_enum[] = {
	[PLANE_ALPHA_SRC_PRE_MUL] = {
		.name = "true",
	},
	[PLANE_ALPHA_SRC_NON_PRE_MUL] = {
		.name = "false",
	},
};

static const struct drm_prop plane_props[] = {
	[PLANE_PROP_TYPE] = {
		.name = "type",
		.type = DRM_PROP_TYPE_ENUM,
		.c = {
			.ev = {
				.count_values = PLANE_TYPE_IDX_NR,
				.values = plane_type_enum,
			},
		},
	},
	[PLANE_PROP_FEATURE] = {
		.name = "FEATURE",
		.type = DRM_PROP_TYPE_BITMASK,
		.c = {
			.ev = {
				.count_values = PLANE_FEATURE_IDX_NR,
				.values = plane_feature_enum,
			},
		},
	},
	[PLANE_PROP_FB_ID] = {
		.name = "FB_ID",
		.type = DRM_PROP_TYPE_OBJECT,
	},
	[PLANE_PROP_IN_FENCE_FD] = {
		.name = "IN_FENCE_FD",
		.type = DRM_PROP_TYPE_SIGNED_RANGE,
	},
	[PLANE_PROP_CRTC_ID] = {
		.name = "CRTC_ID",
		.type = DRM_PROP_TYPE_OBJECT,
	},
	[PLANE_PROP_CRTC_X] = {
		.name = "CRTC_X",
		.type = DRM_PROP_TYPE_SIGNED_RANGE,
	},
	[PLANE_PROP_CRTC_Y] = {
		.name = "CRTC_Y",
		.type = DRM_PROP_TYPE_SIGNED_RANGE,
	},
	[PLANE_PROP_CRTC_W] = {
		.name = "CRTC_W",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[PLANE_PROP_CRTC_H] = {
		.name = "CRTC_H",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[PLANE_PROP_SRC_X] = {
		.name = "SRC_X",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[PLANE_PROP_SRC_Y] = {
		.name = "SRC_Y",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[PLANE_PROP_SRC_W] = {
		.name = "SRC_W",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[PLANE_PROP_SRC_H] = {
		.name = "SRC_H",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[PLANE_PROP_ZPOS] = {
		.name = "ZPOS",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[PLANE_PROP_COLOR_SPACE] = {
		.name = "COLOR_SPACE",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[PLANE_PROP_GLOBAL_ALPHA] = {
		.name = "GLOBAL_ALPHA",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[PLANE_PROP_BLEND_MODE] = {
		.name = "BLEND_MODE",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[PLANE_PROP_ALPHA_SRC_PRE_MUL] = {
		.name = "ALPHA_SRC_PRE_MUL",
		.type = DRM_PROP_TYPE_ENUM,
		.c = {
			.ev = {
				.count_values = PLANE_ALPHA_SRC_NR,
				.values = plane_alpha_src_enum,
			},
		},
	},
};

enum {
	CRTC_PROP_ACTIVE = 0,
	CRTC_PROP_MODE_ID,
	CRTC_PROP_LEFT_MARGIN,
	CRTC_PROP_RIGHT_MARGIN,
	CRTC_PROP_TOP_MARGIN,
	CRTC_PROP_BOTTOM_MARGIN,
	CRTC_PROP_ALPHA_SCALE,
	CRTC_PROP_NR,
};

static const struct drm_prop crtc_props[] = {
	[CRTC_PROP_ACTIVE] = {
		.name = "ACTIVE",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[CRTC_PROP_MODE_ID] = {
		.name = "MODE_ID",
		.type = DRM_PROP_TYPE_BLOB,
	},
	[CRTC_PROP_LEFT_MARGIN] = {
		.name = "left margin",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[CRTC_PROP_RIGHT_MARGIN] = {
		.name = "right margin",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[CRTC_PROP_TOP_MARGIN] = {
		.name = "top margin",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[CRTC_PROP_BOTTOM_MARGIN] = {
		.name = "bottom margin",
		.type = DRM_PROP_TYPE_RANGE,
	},
	[CRTC_PROP_ALPHA_SCALE] = {
		.name = "ALPHA_SCALE",
		.type = DRM_PROP_TYPE_RANGE,
	},
};

static const char *drm_connector_name[] = {
	[DRM_MODE_CONNECTOR_Unknown] = "Unknown",
	[DRM_MODE_CONNECTOR_VGA] = "D-SUB",
	[DRM_MODE_CONNECTOR_DVII] = "DVI-I",
	[DRM_MODE_CONNECTOR_DVID] = "DVI-D",
	[DRM_MODE_CONNECTOR_DVIA] = "DVI-A",
	[DRM_MODE_CONNECTOR_Composite] = "Composite",
	[DRM_MODE_CONNECTOR_SVIDEO] = "S-Video",
	[DRM_MODE_CONNECTOR_LVDS] = "LVDS",
	[DRM_MODE_CONNECTOR_Component] = "Component",
	[DRM_MODE_CONNECTOR_9PinDIN] = "9PinDIN",
	[DRM_MODE_CONNECTOR_DisplayPort] = "DisplayPort",
	[DRM_MODE_CONNECTOR_HDMIA] = "HDMI-A",
	[DRM_MODE_CONNECTOR_HDMIB] = "HDMI-B",
	[DRM_MODE_CONNECTOR_TV] = "TV",
	[DRM_MODE_CONNECTOR_eDP] = "eDP",
	[DRM_MODE_CONNECTOR_VIRTUAL] = "Virtual",
	[DRM_MODE_CONNECTOR_DSI] = "DSI",
	[DRM_MODE_CONNECTOR_DPI] = "DPI",
};

struct drm_mode {
	struct cb_mode base;
	drmModeModeInfo internal;
	u32 blob_id;
	struct list_head link; /* link to output's modes */
};

enum drm_fb_type {
	DRM_FB_TYPE_DMABUF = 0,
	DRM_FB_TYPE_GBM_SURFACE,
	DRM_FB_TYPE_DUMB,
	DRM_FB_TYPE_GBM_BO,
};

struct drm_fb {
	struct cb_buffer base;
	struct gbm_bo *bo;
	struct gbm_surface *surface;
	enum drm_fb_type type;
	u32 handles[4];
	u32 fourcc;
	s32 ref_cnt;
	u32 fb_id;
	void *dev;
	void (*destroy_surface_fb_cb)(struct cb_buffer *b, void *userdata);
	void *destroy_surface_fb_cb_userdata;
};

struct drm_scanout;
struct drm_plane;
struct drm_output;

struct drm_plane_state {
	struct drm_fb *fb;
	struct drm_plane *plane;
	struct drm_scanout *dev;
	struct list_head link;
	s32 zpos;
	s32 crtc_x, crtc_y;
	u32 crtc_w, crtc_h;
	u32 src_x, src_y;
	u32 src_w, src_h;
	bool alpha_src_pre_mul;
};

struct drm_output_state {
	struct drm_output *output;
	struct drm_scanout *dev;
	struct list_head link;
	struct list_head plane_states;
};

struct drm_pending_state {
	struct drm_scanout *dev;
	struct list_head output_states;
};

struct drm_output {
	struct output base;

	struct drm_scanout *dev;

	u32 crtc_id;

	u32 index;

	struct drm_prop props[CRTC_PROP_NR];

	bool modeset_pending;
	bool disable_pending;
	struct drm_mode *current_mode, *pending_mode;
	struct list_head modes;
	struct drm_mode *custom_mode;

	struct drm_output_state *state_cur, *state_last;

	bool page_flip_pending;

	/* output page flip signal */
	struct cb_signal flipped_signal;

	struct list_head link;
	struct drm_plane *primary;
	struct drm_plane *cursor;

	struct list_head planes;
};

#define MONITOR_NAME_LEN 13

struct drm_head {
	struct head base;

	struct drm_scanout *dev;

	char monitor_name[MONITOR_NAME_LEN];

	struct {
		u8 *data;
		size_t length;
	} edid;

	struct cb_signal head_changed_signal;

	u32 connector_id;
	struct drm_prop props[CONNECTOR_PROP_NR];
	drmModeConnectorPtr connector;

	struct list_head link;
};

struct drm_plane {
	struct plane base;

	struct drm_scanout *dev;

	struct list_head link;
	struct list_head output_link;

	struct drm_plane_state *state_cur;

	u32 plane_id;

	struct drm_prop props[PLANE_PROP_NR];
};

struct drm_scanout {
	struct scanout base;

	struct cb_event_loop *loop;

	/* for hot plug detect */
	struct udev *udev;
	struct udev_monitor *udev_monitor;
	struct cb_event_source *udev_drm_source;
	s32 sysnum;

	struct gbm_device *gbm;
	u32 gbm_format;

	s32 fd;
	struct cb_event_source *drm_source;
	drmModeResPtr res;
	drmModePlaneResPtr pres;

	struct list_head outputs;
	struct list_head heads;
	struct list_head planes;

	/* cache */
	void *drm_fb_cache;
	void *ps_cache;
	void *os_cache;
	void *pls_cache;
};

static inline struct drm_scanout *to_dev(struct scanout *so)
{
	return container_of(so, struct drm_scanout, base);
}

static inline struct drm_output *to_drm_output(struct output *output)
{
	return container_of(output, struct drm_output, base);
}

static inline struct drm_head *to_drm_head(struct head *head)
{
	return container_of(head, struct drm_head, base);
}

static inline struct drm_plane *to_drm_plane(struct plane *plane)
{
	return container_of(plane, struct drm_plane, base);
}

static inline struct drm_mode *to_drm_mode(struct cb_mode *mode)
{
	return container_of(mode, struct drm_mode, base);
}

static inline struct drm_fb *to_drm_fb(struct cb_buffer *buffer)
{
	return container_of(buffer, struct drm_fb, base);
}

static void drm_scanout_set_dbg_level(struct scanout *so,
				      enum cb_log_level level)
{
	if (level >= CB_LOG_ERR && level <= CB_LOG_DEBUG)
		drm_dbg = level;
}

/*
static enum cb_pix_fmt fourcc_to_cb_pix_fmt(u32 fourcc)
{
	switch (fourcc) {
	case DRM_FORMAT_ARGB8888:
		return CB_PIX_FMT_ARGB8888;
	case DRM_FORMAT_XRGB8888:
		return CB_PIX_FMT_XRGB8888;
	case DRM_FORMAT_RGB888:
		return CB_PIX_FMT_RGB888;
	case DRM_FORMAT_RGB565:
		return CB_PIX_FMT_RGB565;
	case DRM_FORMAT_NV12:
		return CB_PIX_FMT_NV12;
	case DRM_FORMAT_NV16:
		return CB_PIX_FMT_NV16;
	case DRM_FORMAT_NV24:
		return CB_PIX_FMT_NV24;
	case DRM_FORMAT_YUYV:
		return CB_PIX_FMT_YUYV;
	case DRM_FORMAT_YUV420:
		return CB_PIX_FMT_YUV420;
	case DRM_FORMAT_YUV422:
		return CB_PIX_FMT_YUV422;
	case DRM_FORMAT_YUV444:
		return CB_PIX_FMT_YUV444;
	default:
		return CB_PIX_FMT_UNKNOWN;
	}
}
*/

static u32 cb_pix_fmt_to_fourcc(enum cb_pix_fmt fmt)
{
	u32 fourcc;

	switch (fmt) {
	case CB_PIX_FMT_ARGB8888:
		fourcc = DRM_FORMAT_ARGB8888;
		break;
	case CB_PIX_FMT_XRGB8888:
		fourcc = DRM_FORMAT_XRGB8888;
		break;
	case CB_PIX_FMT_RGB888:
		fourcc = DRM_FORMAT_RGB888;
		break;
	case CB_PIX_FMT_RGB565:
		fourcc = DRM_FORMAT_RGB565;
		break;
	case CB_PIX_FMT_NV12:
		fourcc = DRM_FORMAT_NV12;
		break;
	case CB_PIX_FMT_NV16:
		fourcc = DRM_FORMAT_NV16;
		break;
	case CB_PIX_FMT_NV24:
		fourcc = DRM_FORMAT_NV24;
		break;
	case CB_PIX_FMT_YUYV:
		fourcc = DRM_FORMAT_YUYV;
		break;
	case CB_PIX_FMT_YUV420:
		fourcc = DRM_FORMAT_YUV420;
		break;
	case CB_PIX_FMT_YUV422:
		fourcc = DRM_FORMAT_YUV422;
		break;
	case CB_PIX_FMT_YUV444:
		fourcc = DRM_FORMAT_YUV444;
		break;
	default:
		fourcc = 0;
	}

	return fourcc;
}

static u64 drm_get_prop_value(struct drm_prop *prop_info,
			      drmModeObjectProperties *props)
{
	s32 i, j;

	for (i = 0; i < props->count_props; i++) {
		if (prop_info->prop_id != props->props[i])
			continue;

		if (prop_info->type != DRM_PROP_TYPE_ENUM)
			return props->prop_values[i];

		for (j = 0; j < prop_info->c.ev.count_values; j++) {
			if (!prop_info->c.ev.values[j].valid)
				continue;
			if (prop_info->c.ev.values[j].value !=
			    props->prop_values[i])
				continue;
			/* index, not value */
			return j;
		}

		break;
	}

	return (u64)(-1);
}

/*
static u64 *drm_get_range_prop_value(struct drm_prop *prop_info)
{
	if ((prop_info->type != DRM_PROP_TYPE_RANGE) &&
	    (prop_info->type != DRM_PROP_TYPE_SIGNED_RANGE))
		return NULL;

	if (!prop_info->valid)
		return NULL;

	return prop_info->c.rv.values;
}
*/

static void drm_prop_prepare(struct drm_scanout *dev,
			     const struct drm_prop *template,
			     struct drm_prop *dst,
			     u32 count_props,
			     drmModeObjectProperties *props)
{
	drmModePropertyPtr prop;
	s32 i, j, k, m;
	enum drm_prop_type type;

	drm_debug("prepare properties ...");

	for (j = 0; j < count_props; j++) {
		dst[j].valid = false;
		dst[j].type = template[j].type;
		if (template[j].type == DRM_PROP_TYPE_ENUM ||
		    template[j].type == DRM_PROP_TYPE_BITMASK) {
			dst[j].c.ev.count_values
				= template[j].c.ev.count_values;
			dst[j].c.ev.values = calloc(dst[j].c.ev.count_values,
						    sizeof(struct enum_value));
			for (i = 0; i < dst[j].c.ev.count_values; i++) {
				dst[j].c.ev.values[i].name
					= template[j].c.ev.values[i].name;
				dst[j].c.ev.values[i].valid = false;
			}
		}
	}

	for (i = 0; i < props->count_props; i++) {
		type = DRM_PROP_TYPE_UNKNOWN;
		prop = drmModeGetProperty(dev->fd, props->props[i]);
		if (!prop)
			continue;
		for (j = 0; j < count_props; j++) {
			if (strcasecmp(template[j].name, prop->name))
				continue;

			if (prop->flags & DRM_MODE_PROP_RANGE)
				type = DRM_PROP_TYPE_RANGE;
			if (prop->flags & DRM_MODE_PROP_ENUM)
				type = DRM_PROP_TYPE_ENUM;
			if (prop->flags & DRM_MODE_PROP_BITMASK)
				type = DRM_PROP_TYPE_BITMASK;
			if (prop->flags & DRM_MODE_PROP_BLOB)
				type = DRM_PROP_TYPE_BLOB;
			if (prop->flags & DRM_MODE_PROP_OBJECT)
				type = DRM_PROP_TYPE_OBJECT;
			if (prop->flags & DRM_MODE_PROP_SIGNED_RANGE)
				type = DRM_PROP_TYPE_SIGNED_RANGE;

			if (type != template[j].type) {
				drm_warn("prop %s type mismatch %08X %u",
					 prop->name, prop->flags,
					 template[j].type);
			}

			break;
		}

		if (j == count_props) {
			drm_info("prop %s (%08X) is ignored.", prop->name,
				 prop->flags);
			drmModeFreeProperty(prop);
			continue;
		}

		dst[j].name = template[j].name;
		dst[j].type = type;

		if (prop->flags & DRM_MODE_PROP_ATOMIC)
			dst[j].atomic = true;
		else
			dst[j].atomic = false;

		dst[j].prop_id = props->props[i];

		dst[j].valid = true;
		switch (type) {
		case DRM_PROP_TYPE_BITMASK:
			drm_info("Find bitmask%s property: %s",
				 dst[j].atomic ? " atomic" : "", dst[j].name);
			printf("Find bitmask%s property: %s\n",
				 dst[j].atomic ? " atomic" : "", dst[j].name);
			for (k = 0; k < dst[j].c.ev.count_values; k++) {
				for (m = 0; m < prop->count_enums; m++) {
					if (!strcmp(prop->enums[m].name,
						    dst[j].c.ev.values[k].name))
						break;
				}

				if (m == prop->count_enums)
					continue;

				dst[j].c.ev.values[k].valid = true;
				dst[j].c.ev.values[k].value
					= (1LL << prop->enums[m].value);
				
				drm_info("\t%s - %llu",
					 dst[j].c.ev.values[k].name,
					 dst[j].c.ev.values[k].value);
			}
			break;
		case DRM_PROP_TYPE_ENUM:
			drm_info("Find enum%s property: %s",
				 dst[j].atomic ? " atomic" : "", dst[j].name);
			for (k = 0; k < dst[j].c.ev.count_values; k++) {
				for (m = 0; m < prop->count_enums; m++) {
					if (!strcmp(prop->enums[m].name,
						    dst[j].c.ev.values[k].name))
						break;
				}

				if (m == prop->count_enums)
					continue;

				dst[j].c.ev.values[k].valid = true;
				dst[j].c.ev.values[k].value
					= prop->enums[m].value;
				
				drm_info("\t%s - %llu",
					 dst[j].c.ev.values[k].name,
					 dst[j].c.ev.values[k].value);
				
			}
			break;
		case DRM_PROP_TYPE_RANGE:
			dst[j].c.rv.count_values = prop->count_values;
			if (prop->count_values != 2)
				drm_warn("range value count is not 2");
			for (k = 0; k < dst[j].c.rv.count_values; k++)
				dst[j].c.rv.values[k] = prop->values[k];
			drm_info("Find range%s property: %s",
				 dst[j].atomic ? " atomic" : "", dst[j].name);
			
			drm_info("\t[%" PRId64 " - %" PRId64 "]",
				 dst[j].c.rv.values[0], dst[j].c.rv.values[1]);
			
			break;
		case DRM_PROP_TYPE_SIGNED_RANGE:
			dst[j].c.rv.count_values = prop->count_values;
			if (prop->count_values != 2)
				drm_warn("range value count is not 2");
			for (k = 0; k < dst[j].c.rv.count_values; k++)
				dst[j].c.rv.values[k] = prop->values[k];
			drm_info("Find singed range%s property: %s",
				 dst[j].atomic ? " atomic" : "", dst[j].name);
			
			drm_info("\t[%" PRIu64 " - %" PRIu64 "]",
				 dst[j].c.rv.values[0], dst[j].c.rv.values[1]);
			
			break;
		case DRM_PROP_TYPE_OBJECT:
			drm_info("Find object%s property: %s",
				 dst[j].atomic ? " atomic" : "", dst[j].name);
			break;
		case DRM_PROP_TYPE_BLOB:
			drm_info("Find blob%s property: %s",
				 dst[j].atomic ? " atomic" : "", dst[j].name);
			break;
		default:
			drm_err("unknown property type: %u", type);
			dst[j].valid = false;
			break;
		}

		drmModeFreeProperty(prop);
	}
	drm_debug("prepare properties complete.");
}

static void drm_prop_finish(struct drm_prop *props, u32 count_props)
{
	s32 i;

	for (i = 0; i < count_props; i++) {
		if (props[i].type == DRM_PROP_TYPE_ENUM ||
		    props[i].type == DRM_PROP_TYPE_BITMASK)
			free(props[i].c.ev.values);
	}

	memset(props, 0, count_props * sizeof(*props));
}

static struct drm_pending_state *
drm_pending_state_create(struct drm_scanout *dev)
{
	struct drm_pending_state *ps;

	ps = cb_cache_get(dev->ps_cache, false);
	if (!ps)
		return NULL;
	ps->dev = dev;
	INIT_LIST_HEAD(&ps->output_states);
	return ps;
}

static struct drm_output_state *
drm_output_state_create(struct drm_pending_state *ps, struct drm_output *output)
{
	struct drm_output_state *os;
	struct drm_scanout *dev;

	if (!ps)
		return NULL;

	dev = ps->dev;
	if (!dev)
		return NULL;

	os = cb_cache_get(dev->os_cache, false);
	if (!os)
		return NULL;

	os->dev = dev;
	os->output = output;
	INIT_LIST_HEAD(&os->plane_states);
	list_add_tail(&os->link, &ps->output_states);

	return os;
}

static void drm_fb_ref(struct drm_fb *fb)
{
	if (!fb)
		return;

	drm_debug("[REF] +");
	fb->ref_cnt++;
	drm_debug("[REF] ID: %u %d %lX", fb->fb_id, fb->ref_cnt,
		  (u64)(&fb->base));
	/*
	if (fb->base.info.width != 64 && fb->base.info.width != 1920)
	printf("[REF] ID: %u %d\n", fb->fb_id, fb->ref_cnt);
	if (fb->base.info.width != 64)
		printf("[REF] ID: %u %d\n", fb->fb_id, fb->ref_cnt);
	*/
}

static struct drm_plane_state *
drm_plane_state_create(struct drm_output_state *os, struct drm_plane *plane,
		       struct drm_fb *fb)
{
	struct drm_plane_state *ps;
	struct drm_scanout *dev = os->dev;

	ps = cb_cache_get(dev->pls_cache, false);
	if (!ps)
		return NULL;

	ps->dev = dev;
	ps->fb = fb;
	assert(fb->fb_id);
	drm_fb_ref(ps->fb);
	ps->plane = plane;
	list_add_tail(&ps->link, &os->plane_states);
	return ps;
}

#ifdef USE_DRM_PRIME
static void drm_fb_release_dmabuf(struct drm_fb *fb)
{
	struct drm_gem_close req;
	struct drm_scanout *dev;

	if (fb && fb->handles[0]) {
		dev = fb->dev;
		drm_debug("close GEM");
		memset(&req, 0, sizeof(req));
		req.handle = fb->handles[0];
		drmIoctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &req);

		if (fb->fb_id) {
			drm_debug("Remove DRM FB");
			drmModeRmFB(dev->fd, fb->fb_id);
		}

		if (fb->base.info.fd[0]) {
			drm_debug("close %d", fb->base.info.fd[0]);
			close(fb->base.info.fd[0]);
		}

		cb_cache_put(fb, dev->drm_fb_cache);
	}
}
#else
static void drm_fb_release_dmabuf(struct drm_fb *fb)
{
	struct drm_scanout *dev = fb->dev;

	drm_debug("Release DMA-BUF.");
	if (fb && fb->bo) {
		drm_debug("Destroy gbm bo.");
		gbm_bo_destroy(fb->bo);
	}

	if (fb) {
		if (fb->fb_id) {
			drm_debug("Remove DRM FB");
			drmModeRmFB(dev->fd, fb->fb_id);
		}
		cb_cache_put(fb, dev->drm_fb_cache);
	}
}
#endif

static void drm_fb_release_surface(struct drm_fb *fb)
{
	drm_debug("Release Surface-BUF.");
	if (fb && fb->bo && fb->surface) {
		drm_debug("release surface buffer");
		/* printf("Release surface buffer ID: %u\n", fb->fb_id); */
		gbm_surface_release_buffer(fb->surface, fb->bo);
	}
}

static void drm_fb_release_cursor_bo(struct drm_fb *fb)
{
	struct drm_scanout *dev = fb->dev;

	drm_debug("Release GBM-CURSOR-BO.");

	if (fb && fb->bo) {
		drm_debug("Destroy gbm bo.");
		gbm_bo_destroy(fb->bo);
	}

	if (fb) {
		if (fb->fb_id) {
			drm_debug("Remove GBM CURSOR BO DRM FB");
			drmModeRmFB(dev->fd, fb->fb_id);
		}
		cb_cache_put(fb, dev->drm_fb_cache);
	}
}

static void drm_fb_release_dumb(struct drm_fb *fb)
{
	struct drm_mode_destroy_dumb destroy_arg;
	struct drm_scanout *dev = fb->dev;

	drm_debug("Release Dumb-BUF.");

	if (fb && fb->base.info.maps[0] && fb->base.info.sizes[0]) {
		munmap(fb->base.info.maps[0], fb->base.info.sizes[0]);
	}

	if (fb && fb->handles[0]) {
		memset(&destroy_arg, 0, sizeof(destroy_arg));
		destroy_arg.handle = fb->handles[0];
		drmIoctl(dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
	}

	if (fb && fb->base.info.fd[0] > 0)
		close(fb->base.info.fd[0]);

	if (fb) {
		if (fb->fb_id) {
			drm_debug("Remove DRM FB");
			drmModeRmFB(dev->fd, fb->fb_id);
		}
		cb_cache_put(fb, dev->drm_fb_cache);
	}
}

static void drm_fb_release_buffer(struct drm_fb *fb)
{
	switch (fb->type) {
	case DRM_FB_TYPE_DMABUF:
		drm_fb_release_dmabuf(fb);
		break;
	case DRM_FB_TYPE_GBM_SURFACE:
		drm_fb_release_surface(fb);
		break;
	case DRM_FB_TYPE_DUMB:
		drm_fb_release_dumb(fb);
		break;
	case DRM_FB_TYPE_GBM_BO:
		drm_fb_release_cursor_bo(fb);
		break;
	default:
		break;
	}
}

static void drm_fb_unref(struct drm_fb *fb)
{
	struct cb_buffer *buffer;

	if (!fb)
		return;

	drm_debug("[UNREF] -");
	if (fb->ref_cnt <= 0) {
		drm_err("incorrect ref cnt ! %d", fb->ref_cnt);
		return;
	}

	fb->ref_cnt--;
	drm_debug("[UNREF] ID: %u %d %lX", fb->fb_id, fb->ref_cnt,
		  (u64)(&fb->base));
	/*
	if (fb->base.info.width != 64 && fb->base.info.width != 1920)
	printf("[UNREF] ID: %u %d\n", fb->fb_id, fb->ref_cnt);
	if (fb->base.info.width != 64)
		printf("[UNREF] ID: %u %d\n", fb->fb_id, fb->ref_cnt);
	*/
	if (fb->ref_cnt == 0 && fb->type != DRM_FB_TYPE_GBM_SURFACE) {
		drm_fb_release_buffer(fb);
	} else if (fb->ref_cnt == 1) { /* do not use any more */
		if (fb->type == DRM_FB_TYPE_GBM_SURFACE) {
			drm_fb_release_buffer(fb);
		}
		buffer = &fb->base;
		/* printf("[UNREF] bo complete. %p\n", buffer); */
		cb_signal_emit(&buffer->complete_signal, buffer);
	}
}

static void drm_plane_state_destroy(struct drm_plane_state *pls)
{
	struct drm_scanout *dev;

	if (!pls)
		return;

	drm_debug("destroy plane state");
	if (pls->fb && pls->fb->fb_id)
		drm_fb_unref(pls->fb);
	dev = pls->dev;
	cb_cache_put(pls, dev->pls_cache);
}

static void drm_output_state_destroy(struct drm_output_state *os)
{
	struct drm_plane_state *pls, *next_pls;
	struct drm_scanout *dev;

	drm_debug("destroy output state enter %p", os);
	if (!os)
		return;

	drm_debug("destroy output state");
	list_for_each_entry_safe(pls, next_pls, &os->plane_states, link) {
		list_del(&pls->link);
		drm_plane_state_destroy(pls);
	}

	dev = os->dev;
	cb_cache_put(os, dev->os_cache);
}

static void drm_pending_state_destroy(struct drm_pending_state *ps)
{
	struct drm_output_state *os, *os_next;
	struct drm_scanout *dev;

	if (!ps)
		return;

	list_for_each_entry_safe(os, os_next, &ps->output_states, link) {
		list_del(&os->link);
		drm_output_state_destroy(os);
	}

	dev = ps->dev;
	cb_cache_put(ps, dev->ps_cache);
}

static struct drm_output_state *
drm_get_output_state(struct drm_pending_state *ps, struct drm_output *output)
{
	struct drm_output_state *os;

	list_for_each_entry(os, &ps->output_states, link) {
		if (os->output == output) {
			return os;
		}
	}

	return NULL;
}

static s32 set_crtc_prop(drmModeAtomicReq *req,
			 struct drm_output *output,
			 u32 prop,
			 u64 value)
{
	s32 ret;

	if (!output->props[prop].valid)
		return 0;

	drm_debug("[PROP SET] crtc: %u, %s -> %"PRIu64,
		  output->crtc_id, output->props[prop].name, value);

	ret = drmModeAtomicAddProperty(req, output->crtc_id,
				       output->props[prop].prop_id, value);
	if (ret <= 0) {
		drm_err("[PROP SET] failed to set crtc property %s. (%s)",
			output->props[prop].name, strerror(errno));
		return -1;
	}

	return 0;
}

static s32 set_connector_prop(drmModeAtomicReq *req,
			      struct drm_head *head,
			      u32 prop,
			      u64 value)
{
	s32 ret;

	if (!head->props[prop].valid)
		return 0;

	drm_debug("[PROP SET] connector: %u, %s -> %"PRIu64,
		  head->connector_id, head->props[prop].name, value);

	ret = drmModeAtomicAddProperty(req, head->connector_id,
				       head->props[prop].prop_id, value);
	if (ret <= 0) {
		drm_err("[PROP SET] failed to set connector property %s. (%s)",
			head->props[prop].name, strerror(errno));
		return -1;
	}

	return 0;
}

static s32 set_plane_prop(drmModeAtomicReq *req,
			  struct drm_plane *plane,
			  u32 prop,
			  u64 value)
{
	s32 ret;

	if (!plane->props[prop].valid)
		return 0;

	drm_debug("[PROP SET] Plane: %u, %s -> %"PRIu64,
		  plane->plane_id, plane->props[prop].name, value);

	ret = drmModeAtomicAddProperty(req, plane->plane_id,
				       plane->props[prop].prop_id, value);
	if (ret <= 0) {
		drm_err("[PROP SET] failed to set plane property %s. (%s)",
			plane->props[prop].name, strerror(errno));
		return -1;
	}

	return 0;
}

static u32 drm_refresh_rate_mhz(const drmModeModeInfo *info)
{
	u64 refresh;

	/* Calculate higher precision (mHz) refresh rate */
	refresh = (info->clock * 1000000LL / info->htotal +
		   info->vtotal / 2) / info->vtotal;

	if (info->flags & DRM_MODE_FLAG_INTERLACE)
		refresh *= 2;
	if (info->flags & DRM_MODE_FLAG_DBLSCAN)
		refresh /= 2;
	if (info->vscan > 1)
	    refresh /= info->vscan;

	return refresh;
}

static inline u64 millihz_to_nsec(u32 mhz)
{
	assert(mhz > 0);
	return 1000000000000LL / mhz;
}

static s32 drm_output_commit(drmModeAtomicReq *req,
			     struct drm_output_state *os,
			     u32 *flags)
{
	struct drm_output *output = os->output;
	s32 ret = 0;
	struct drm_head *head = to_drm_head(output->base.head);
	struct drm_plane *plane;
	struct drm_plane_state *pls;

	/* disable all planes first */
	list_for_each_entry(plane, &output->planes, output_link) {
		ret |= set_plane_prop(req, plane, PLANE_PROP_FB_ID, 0);
		ret |= set_plane_prop(req, plane, PLANE_PROP_CRTC_ID, 0);
	}

	if (output->disable_pending) {
		printf("deactive\n");
		output->current_mode = NULL;
		*flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
		output->disable_pending = false;
		ret |= set_crtc_prop(req, output, CRTC_PROP_ACTIVE, 0);
		ret |= set_crtc_prop(req, output, CRTC_PROP_MODE_ID, 0);
		ret |= set_connector_prop(req, head, CONNECTOR_PROP_CRTC_ID, 0);
		return 0;
	} else {
		if (!output->base.head->connected)
			return 0;
		if (output->modeset_pending) {
			printf("do modeset\n");
			output->current_mode = output->pending_mode;
			output->base.refresh = drm_refresh_rate_mhz(
					&output->current_mode->internal);
			output->base.refresh_nsec = millihz_to_nsec(
					output->base.refresh);
			printf("[output %d] refresh rate: %u (mhz)\n",
					output->base.index,
					output->base.refresh);
			printf("[output %d] refresh time: %u (ns)\n",
					output->base.index,
					output->base.refresh_nsec);
			output->pending_mode = NULL;
			if (!output->current_mode->blob_id) {
				drmModeCreatePropertyBlob(output->dev->fd,
					&output->current_mode->internal,
					sizeof(drmModeModeInfo),
					&output->current_mode->blob_id);
			}
			*flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
			output->modeset_pending = false;
		}
		ret |= set_crtc_prop(req, output, CRTC_PROP_ACTIVE, 1);
		ret |= set_crtc_prop(req, output, CRTC_PROP_MODE_ID,
				     output->current_mode->blob_id);
		ret |= set_connector_prop(req, head, CONNECTOR_PROP_CRTC_ID,
					  output->crtc_id);
	}

	list_for_each_entry(pls, &os->plane_states, link) {
		plane = pls->plane;
		ret |= set_plane_prop(req, plane, PLANE_PROP_FB_ID,
				      pls->fb ? pls->fb->fb_id : 0);
		/*
		printf("Commit FB for o %d: p %u: "
		       "%u %d,%d %ux%u -> %d,%d %ux%u\n",
				output->index, plane->plane_id,
				pls->fb->fb_id,
				pls->src_x, pls->src_x,
				pls->src_w, pls->src_h,
				pls->crtc_x, pls->crtc_y,
				pls->crtc_w, pls->crtc_h);
		*/
		ret |= set_plane_prop(req, plane, PLANE_PROP_CRTC_ID,
				      pls->fb ? output->crtc_id : 0);
		ret |= set_plane_prop(req, plane, PLANE_PROP_SRC_X,
				      pls->src_x << 16);
		ret |= set_plane_prop(req, plane, PLANE_PROP_SRC_Y,
				      pls->src_y << 16);
		ret |= set_plane_prop(req, plane, PLANE_PROP_SRC_W,
				      pls->src_w << 16);
		ret |= set_plane_prop(req, plane, PLANE_PROP_SRC_H,
				      pls->src_h << 16);
		ret |= set_plane_prop(req, plane, PLANE_PROP_CRTC_X,
				      pls->crtc_x);
		ret |= set_plane_prop(req, plane, PLANE_PROP_CRTC_Y,
				      pls->crtc_y);
		ret |= set_plane_prop(req, plane, PLANE_PROP_CRTC_W,
				      pls->crtc_w);
		ret |= set_plane_prop(req, plane, PLANE_PROP_CRTC_H,
				      pls->crtc_h);
		if (pls->zpos != -1) {
			ret |= set_plane_prop(req, plane, PLANE_PROP_ZPOS,
				      pls->zpos);
		}

		if (pls->alpha_src_pre_mul) {
			ret |= set_plane_prop(req, plane,
				PLANE_PROP_ALPHA_SRC_PRE_MUL,
				PLANE_ALPHA_SRC_PRE_MUL);
		} else {
			ret |= set_plane_prop(req, plane,
				PLANE_PROP_ALPHA_SRC_PRE_MUL,
				PLANE_ALPHA_SRC_NON_PRE_MUL);
		}
	}

	return ret;
}

static void drm_output_state_switch(struct drm_output_state *os, bool async)
{
	struct drm_output *output = os->output;
	struct drm_plane_state *pls;
	struct drm_plane *plane;

	drm_debug("switch output state");
	if (async) {
		/* switch state */
		output->state_last = output->state_cur;
		drm_debug("last_state: %p", output->state_last);
		list_del(&os->link);
		output->state_cur = os;
		/* page flip done */
		output->page_flip_pending = true;
		/* assign plane with current state */
		list_for_each_entry(pls, &os->plane_states, link) {
			plane = pls->plane;
			plane->state_cur = pls;
		}
	} else {
		/* destroy current plane state */
		if (output->state_cur) {
			drm_output_state_destroy(output->state_cur);
			output->state_cur = NULL;
		}
		/* destroy disable state */
		list_del(&os->link);
		drm_output_state_destroy(os);
		output->state_cur = NULL;
		assert(!output->page_flip_pending);
	}
}

static s32 drm_commit(struct drm_pending_state *ps, bool async)
{
	struct drm_scanout *dev = ps->dev;
	struct drm_output *output;
	struct drm_output_state *os, *next_os;
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	u32 flags = 0;
	s32 ret = 0;
	bool empty = true;

	if (async)
		flags = DRM_MODE_PAGE_FLIP_EVENT |
			DRM_MODE_PAGE_FLIP_ASYNC |
			DRM_MODE_ATOMIC_NONBLOCK;

	list_for_each_entry(output, &dev->outputs, link) {
		os = drm_get_output_state(ps, output);
		if (!os)
			continue;
		if (output->disable_pending ||
		    output->base.head->connected) {
			ret = drm_output_commit(req, os, &flags);
			if (ret) {
				goto out;
			}
			empty = false;
		}
	}

	if (empty) {
		drm_warn("empty commit, do nothing.");
		goto out1;
	}

	drm_debug("commit");
	ret = drmModeAtomicCommit(dev->fd, req, flags, dev);
	if (ret) {
		drm_err("[KMS] failed to commit. (%s)", strerror(errno));
		assert(0);
		goto out;
	}

out1:
	list_for_each_entry_safe(os, next_os, &ps->output_states, link) {
		drm_output_state_switch(os, async);
	}

out:
	drmModeAtomicFree(req);
	drm_pending_state_destroy(ps);

	return 0;
}

static void *drm_scanout_data_alloc(struct scanout *so)
{
	struct drm_scanout *dev = to_dev(so);
	struct drm_pending_state *ps = drm_pending_state_create(dev);
	
	return ps;
}

static void drm_do_scanout(struct scanout *so, void *scanout_data)
{
	struct drm_pending_state *ps = scanout_data;

	if (!ps)
		return;

	drm_commit(ps, true);
}

static s32 drm_scanout_data_fill(struct scanout *so,
				 void *scanout_data,
				 struct scanout_commit_info *commit)
{
	struct drm_pending_state *ps = scanout_data;
	struct drm_output_state *os;
	struct drm_output *output;
	struct drm_plane_state *pls;
	struct fb_info *info;
	bool output_found;
	
	if (list_empty(&commit->fb_commits))
		return -EINVAL;
	
	list_for_each_entry(info, &commit->fb_commits, link) {
		output = to_drm_output(info->output);
		if (!output)
			continue;

		output_found = false;
		list_for_each_entry(os, &ps->output_states, link) {
			if (os->output == output) {
				output_found = true;
				break;
			}
		}

		if (!output_found)
			os = drm_output_state_create(ps, output);

		pls = drm_plane_state_create(os, to_drm_plane(info->plane),
					     to_drm_fb(info->buffer));
/*
		pls->fb = to_drm_fb(info->buffer);
*/
		pls->zpos = info->zpos;
		pls->alpha_src_pre_mul = info->alpha_src_pre_mul;
		pls->src_x = info->src.pos.x;
		pls->src_y = info->src.pos.y;
		pls->src_w = info->src.w;
		pls->src_h = info->src.h;
		pls->crtc_x = info->dst.pos.x;
		pls->crtc_y = info->dst.pos.y;
		pls->crtc_w = info->dst.w;
		pls->crtc_h = info->dst.h;
	}

	return 0;
}

static void drm_output_native_surface_destroy(struct output *o, void *surface)
{
	if (surface) {
		drm_notice("destroy gbm surface %p", surface);
		gbm_surface_destroy((struct gbm_surface *)surface);
	}
}

static void *drm_output_native_surface_create(struct output *o)
{
	struct drm_output *output = to_drm_output(o);
	struct drm_scanout *dev = output->dev;
	struct drm_mode *mode;
	struct gbm_surface *surface;

	if (!o)
		return NULL;

	assert(output->current_mode);
	mode = output->current_mode;
	surface = gbm_surface_create(dev->gbm,
				mode->internal.hdisplay,
				mode->internal.vdisplay,
				dev->gbm_format,
				GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	drm_debug("create surface %ux%u, %u, %p %p",
		mode->internal.hdisplay, mode->internal.vdisplay,
		dev->gbm_format, dev->gbm, surface);
	if (!surface) {
		drm_err("failed to create gbm surface (%s)", strerror(errno));
	} else {
		drm_notice("create gbm surface complete for output %d, %p",
				output->index, surface);
	}

	return surface;
}

static s32 drm_output_disable(struct output *o)
{
	struct drm_output *output = to_drm_output(o);
	struct drm_pending_state *ps;

	drm_info("disabling output...");
	output->disable_pending = true;
	if (output->page_flip_pending) {
		drm_info("page flip pending while disabling, deferred.");
		return -1;
	}
	ps = drm_pending_state_create(output->dev);
	drm_output_state_create(ps, output);
	drm_commit(ps, false);
	drm_info("disable output complete.");
	return 0;
}

static struct cb_mode *drm_output_get_custom_mode(struct output *o)
{
	struct drm_output *output = to_drm_output(o);

	if (output->custom_mode)
		return &output->custom_mode->base;
	else
		return NULL;
}

static struct cb_mode *drm_output_get_preferred_mode(struct output *o)
{
	struct drm_output *output = to_drm_output(o);
	struct drm_mode *mode;

	if (!o)
		return NULL;

	if (!output->base.head->connected)
		return NULL;

	list_for_each_entry(mode, &output->modes, link) {
		if (mode->base.preferred) {
			return &mode->base;
		}
	}

	list_for_each_entry(mode, &output->modes, link) {
		return &mode->base;
	}

	return NULL;
}

static struct cb_mode *drm_output_get_current_mode(struct output *o)
{
	struct drm_output *output = to_drm_output(o);

	if (!o)
		return NULL;

	if (!output->base.head->connected)
		return NULL;

	if (output->current_mode)
		return &output->current_mode->base;

	if (output->modeset_pending)
		return &output->pending_mode->base;

	return NULL;
}

static struct cb_mode *
drm_output_enumerate_mode(struct output *o, struct cb_mode *last)
{
	struct drm_mode *mode, *last_mode;
	bool find_last = true;
	struct drm_output *output = to_drm_output(o);

	if (last) {
		find_last = false;
		last_mode = to_drm_mode(last);
	}

	list_for_each_entry(mode, &output->modes, link) {
		if (output->custom_mode && mode == output->custom_mode)
			continue;

		if (!find_last && mode == last_mode) {
			find_last = true;
			continue;
		}

		if (find_last) {
			return &mode->base;
		}
	}

	return NULL;
}

static struct plane *
drm_output_enumerate_plane(struct output *o, struct plane *last)
{
	struct drm_plane *plane, *last_plane;
	bool find_last = true;
	struct drm_output *output = to_drm_output(o);

	if (last) {
		find_last = false;
		last_plane = to_drm_plane(last);
	}

	list_for_each_entry(plane, &output->planes, output_link) {
		if (!find_last && plane == last_plane) {
			find_last = true;
			continue;
		}

		if (find_last) {
			return &plane->base;
		}
	}

	return NULL;
}

static struct plane *
drm_output_enumerate_plane_by_fmt(struct output *o,
				  struct plane *last,
				  enum cb_pix_fmt fmt)
{
	struct drm_plane *plane, *last_plane;
	bool find_last = true;
	struct drm_output *output = to_drm_output(o);
	u32 fourcc = cb_pix_fmt_to_fourcc(fmt);
	s32 i;

	if (last) {
		find_last = false;
		last_plane = to_drm_plane(last);
	}

	list_for_each_entry(plane, &output->planes, output_link) {
		for (i = 0; i < plane->base.count_formats; i++) {
			if (plane->base.formats[i] == fourcc)
				break;
		}
		if (i == plane->base.count_formats)
			continue;

		if (!find_last && plane == last_plane) {
			find_last = true;
			continue;
		}

		if (find_last) {
			return &plane->base;
		}
	}

	return NULL;
}

static s32 drm_output_enable(struct output *o, struct cb_mode *mode)
{
	struct drm_output *output = to_drm_output(o);
	struct drm_mode *new_mode = to_drm_mode(mode);

	drm_info("enabling output...");
	if (mode) {
		/* use select mode */
		output->pending_mode = new_mode;
		drm_info("Pending mode: %ux%u@%u %.2fMHz",
			 output->pending_mode->base.width,
			 output->pending_mode->base.height,
			 output->pending_mode->base.vrefresh,
			 output->pending_mode->base.pixel_freq / 1000.0f);
	}

	output->modeset_pending = true;

	drm_info("enable output complete.");
	return 0;
}

static void drm_plane_destroy(struct plane *p)
{
	struct drm_plane *plane = to_drm_plane(p);

	if (!plane)
		return;

	if (plane->base.formats)
		free(plane->base.formats);

	drm_prop_finish(plane->props, PLANE_PROP_NR);

	list_del(&plane->link);
	list_del(&plane->output_link);

	free(plane);
}

static struct plane *drm_plane_create(struct drm_scanout *dev,
				      struct drm_output *output, s32 index)
{
	struct drm_plane *plane = NULL;
	drmModeObjectProperties *props;
	drmModePlane *p;
	s32 i;
	u32 type, alpha_src;
	u64 value;
	struct drm_prop *prop;

	drm_info("Create plane ...");
	plane = calloc(1, sizeof(*plane));
	if (!plane)
		goto err;

	drm_debug("index = %d, count_planes = %u", index,
		  dev->pres->count_planes);

	plane->dev = dev;

	if (index >= dev->pres->count_planes)
		goto err;

	plane->plane_id = dev->pres->planes[index];

	p = drmModeGetPlane(dev->fd, plane->plane_id);
	if (!p) {
		drm_err("failed to drm plane. (%s)", strerror(errno));
		goto err;
	}

	if (!(p->possible_crtcs & (1U << output->index))) {
		/* the plane cannot be used for this crtc */
		drmModeFreePlane(p);
		free(plane);
		return NULL;
	}

	INIT_LIST_HEAD(&plane->base.link);
	plane->base.count_formats = p->count_formats;
	plane->base.formats = calloc(plane->base.count_formats, sizeof(u32));
	memcpy(plane->base.formats, p->formats,
	       sizeof(u32) * plane->base.count_formats);
	drmModeFreePlane(p);

	for (i = 0; i < plane->base.count_formats; i++) {
		drm_info("\tformat: %4.4s", (char *)&plane->base.formats[i]);
	}

	props = drmModeObjectGetProperties(dev->fd, plane->plane_id,
					   DRM_MODE_OBJECT_PLANE);
	if (!props) {
		drm_err("failed to get object properties (%s)",strerror(errno));
		goto err;
	}
	drm_prop_prepare(dev, plane_props, plane->props, PLANE_PROP_NR, props);
	plane->base.zpos = drm_get_prop_value(&plane->props[PLANE_PROP_ZPOS],
					      props);
	type = drm_get_prop_value(&plane->props[PLANE_PROP_TYPE], props);
	if (type != (u32)(-1)) {
		prop = &plane->props[PLANE_PROP_TYPE];
		if (!strcmp(prop->c.ev.values[type].name, "Primary"))
			plane->base.type = PLANE_TYPE_PRIMARY;
		else if (!strcmp(prop->c.ev.values[type].name, "Overlay"))
			plane->base.type = PLANE_TYPE_OVERLAY;
		else if (!strcmp(prop->c.ev.values[type].name, "Cursor"))
			plane->base.type = PLANE_TYPE_CURSOR;
	}

	value = drm_get_prop_value(&plane->props[PLANE_PROP_FEATURE], props);
	if (value != (u64)(-1)) {
		prop = &plane->props[PLANE_PROP_FEATURE];
		if (value & prop->c.ev.values[PLANE_FEATURE_IDX_SCALE].value)
			plane->base.scale_support = true;
		else
			plane->base.scale_support = false;
		if (value & prop->c.ev.values[PLANE_FEATURE_IDX_ALPHA].value)
			plane->base.alpha_support = true;
		else
			plane->base.alpha_support = false;
		if (value & prop->c.ev.values[PLANE_FEATURE_IDX_HDR2SDR].value)
			plane->base.hdr2sdr_support = true;
		else
			plane->base.hdr2sdr_support = false;
		if (value & prop->c.ev.values[PLANE_FEATURE_IDX_SDR2HDR].value)
			plane->base.sdr2hdr_support = true;
		else
			plane->base.sdr2hdr_support = false;
		if (value & prop->c.ev.values[PLANE_FEATURE_IDX_AFBDC].value)
			plane->base.afbdc_support = true;
		else
			plane->base.afbdc_support = false;
		if (value & prop->c.ev.values[PLANE_FEATURE_IDX_PDAF_POS].value)
			plane->base.pdaf_pos_support = true;
		else
			plane->base.pdaf_pos_support = false;
		drm_debug("scale support: %d", plane->base.scale_support);
		drm_debug("alpha support: %d", plane->base.alpha_support);
		drm_debug("hdr2sdr support: %d", plane->base.hdr2sdr_support);
		drm_debug("sdr2hdr support: %d", plane->base.sdr2hdr_support);
		drm_debug("afbdc support: %d", plane->base.afbdc_support);
		drm_debug("pdaf pos support: %d", plane->base.pdaf_pos_support);
	}

	alpha_src = drm_get_prop_value(
			&plane->props[PLANE_PROP_ALPHA_SRC_PRE_MUL],
			props);
	if (alpha_src != (u32)(-1)) {
		if (!strcmp(plane_alpha_src_enum[alpha_src].name, "true")) {
			drm_debug("ALPHA_SRC_PRE_MUL: true, %u", alpha_src);
		} else if (!strcmp(plane_alpha_src_enum[alpha_src].name,
			   "false")) {
			drm_debug("ALPHA_SRC_PRE_MUL: false, %u", alpha_src);
		}
		plane->base.alpha_src_pre_mul = alpha_src;
	}
	drmModeFreeObjectProperties(props);

	plane->base.output = &output->base;
	list_add_tail(&plane->link, &dev->planes);
	drm_info("Create plane complete.");

	return &plane->base;

err:
	if (plane)
		drm_plane_destroy(&plane->base);

	return NULL;
}

static void drm_get_and_parse_edid(struct drm_head *head, 
				   drmModeObjectProperties *props)
{
	u32 blob_id;
#ifdef READ_EDID_TRY_MAX
#undef READ_EDID_TRY_MAX
#endif
#define READ_EDID_TRY_MAX 5

	s32 i, blob_get_cnt = READ_EDID_TRY_MAX;
	drmModePropertyBlobPtr blob;
	const u8 *p;

	blob_id = (u32)drm_get_prop_value(&head->props[CONNECTOR_PROP_EDID],
					  props);

	if (blob_id == (u32)(-1))
		return;

retry:
	blob = drmModeGetPropertyBlob(head->dev->fd, blob_id);
	blob_get_cnt--;
	if (!blob || !blob->data || !blob->length) {
		if (blob_get_cnt > 0) {
			if (blob) {
				drmModeFreePropertyBlob(blob);
			}
			usleep(50000);
			drm_warn("failed to get edid blob, retry again.");
			drmModeFreeConnector(head->connector);
			head->connector = drmModeGetConnector(head->dev->fd,
						head->connector_id);
			goto retry;
		}
		goto out;
	}

	if (head->edid.data) {
		free(head->edid.data);
		head->edid.data = NULL;
	}

	head->edid.length = blob->length;
	head->edid.data = (u8 *)malloc(blob->length);
	if (!head->edid.data) {
		head->edid.length = 0;
		goto out;
	}
	memcpy(head->edid.data, blob->data, blob->length);
	drm_debug("Get EDID blob, %u bytes.", blob->length);
	p = head->edid.data;

	/*
	 * VESA enhanced extended display identification data standard
	 * Table 3.29 – Display Product Name (ASCII) String Descriptor
	 * Block Definition
	 * Byte#   Value Display     Product Name Definition
	 * 0 - 4   (00 00 00 FC 00)h Display Product Name (ASCII) String (FCh)
	 * 5 - 17  ASCII String      Up 13 alphanumeric characters
	 */
	memset(head->monitor_name, 0, MONITOR_NAME_LEN);
	for (i = 0x36; i <= 0x6C; i+=18) {
		if (p[i])
			continue;
		if (p[i+1])
			continue;
		if (p[i+2])
			continue;
		if (p[i+3] == 0xFC) {
			memcpy(head->monitor_name, &p[i+5], MONITOR_NAME_LEN-1);
			break;
		}
	}

	for (i = 0; i < 12; i++) {
		if (head->monitor_name[i] == '\r' ||
		    head->monitor_name[i] == '\n') {
			head->monitor_name[i] = '\0';
		} else if (!isprint(head->monitor_name[i])) {
			head->monitor_name[i] = '*';
		}
	}
	drm_debug("Monitor: %s", head->monitor_name);

out:
	if (blob)
		drmModeFreePropertyBlob(blob);
}

static s32 drm_head_copy_edid(struct head *h, u8 *data, size_t *length)
{
	struct drm_head *head = to_drm_head(h);

	if (!data || !length)
		return -EINVAL;

	if (!head->base.connected)
		return -ENOENT;

	if (!head->edid.data || !head->edid.length)
		return -ENOENT;

	*length = head->edid.length;
	memcpy(data, head->edid.data, head->edid.length);

	return 0;
}

static s32 drm_head_add_changed_notify(struct head *h, struct cb_listener *l)
{
	struct drm_head *head = to_drm_head(h);

	if (!l)
		return -EINVAL;

	cb_signal_add(&head->head_changed_signal, l);

	return 0;
}

static void drm_head_destroy(struct head *h)
{
	struct drm_head *head = to_drm_head(h);

	if (!head)
		return;

	if (head->connector)
		drmModeFreeConnector(head->connector);

	drm_prop_finish(head->props, CONNECTOR_PROP_NR);

	list_del(&head->link);

	cb_signal_fini(&head->head_changed_signal);

	if (head->edid.data)
		free(head->edid.data);

	free(head);
}

static struct head *drm_head_create(struct drm_scanout *dev, s32 index)
{
	struct drm_head *head = NULL;
	drmModeObjectProperties *props;

	drm_info("Creating head %d...", index);
	head = calloc(1, sizeof(*head));
	if (!head)
		goto err;

	head->dev = dev;

	head->base.retrieve_edid = drm_head_copy_edid;
	head->base.add_head_changed_notify = drm_head_add_changed_notify;

	cb_signal_init(&head->head_changed_signal);

	if (index >= dev->res->count_connectors)
		goto err;

	head->connector_id = dev->res->connectors[index];

	head->connector = drmModeGetConnector(dev->fd, head->connector_id);
	if (!head->connector) {
		drm_err("failed to get drm connector. (%s)", strerror(errno));
		goto err;
	}

	head->base.connector_name =
		drm_connector_name[head->connector->connector_type];
	drm_info("connector type: %s", head->base.connector_name);

	head->base.connected = 
		head->connector->connection == DRM_MODE_CONNECTED ? true:false;

	if (head->base.connected) {
		drm_info("Connected. Retrieving EDID later.");
	} else {
		drm_info("Disconnected.");
	}

	props = drmModeObjectGetProperties(dev->fd, head->connector_id,
					   DRM_MODE_OBJECT_CONNECTOR);
	if (!props) {
		drm_err("failed to get object properties (%s)",strerror(errno));
		goto err;
	}
	drm_prop_prepare(dev, connector_props, head->props, CONNECTOR_PROP_NR,
			 props);

	if (head->base.connected) {
		drm_get_and_parse_edid(head, props);
		if (head->edid.data && head->edid.length &&
		    strlen(head->monitor_name))
			head->base.monitor_name = head->monitor_name;
	}

	drmModeFreeObjectProperties(props);

	list_add_tail(&head->link, &dev->heads);
	drm_info("Create head complete.");

	return &head->base;

err:
	if (head)
		drm_head_destroy(&head->base);

	return NULL;
}

static void drm_clear_output_modes(struct drm_output *output)
{
	struct drm_mode *mode, *next_mode;

	list_for_each_entry_safe(mode, next_mode, &output->modes, link) {
		list_del(&mode->link);
		if (mode->blob_id) {
			drmModeDestroyPropertyBlob(output->dev->fd,
						   mode->blob_id);
		}
		free(mode);
	}

	output->custom_mode = NULL;
}

static struct cb_mode *drm_output_create_custom_mode(struct output *o,
						     u32 clock,
						     u16 width,
						     u16 hsync_start,
						     u16 hsync_end,
						     u16 htotal,
						     u16 hskew,
						     u16 height,
						     u16 vsync_start,
						     u16 vsync_end,
						     u16 vtotal,
						     u16 vscan,
						     u32 vrefresh,
						     bool interlaced,
						     bool pos_hsync,
						     bool pos_vsync,
						     char *mode_name)
{
	struct drm_output *output = to_drm_output(o);
	struct drm_mode *new_mode;
	u32 flags, type;

	flags = type = 0;
	new_mode = calloc(1, sizeof(*new_mode));
	if (!new_mode)
		goto err;

	new_mode->base.width = width;
	new_mode->base.height = height;
	new_mode->base.vrefresh = vrefresh;
	new_mode->base.pixel_freq = clock;
	new_mode->base.preferred = false;

	new_mode->internal.clock = clock;
	new_mode->internal.hdisplay = width;
	new_mode->internal.hsync_start = hsync_start;
	new_mode->internal.hsync_end = hsync_end;
	new_mode->internal.htotal = htotal;
	new_mode->internal.hskew = hskew;
	new_mode->internal.vdisplay = height;
	new_mode->internal.vsync_start = vsync_start;
	new_mode->internal.vsync_end = vsync_end;
	new_mode->internal.vtotal = vtotal;
	new_mode->internal.vscan = vscan;
	new_mode->internal.vrefresh = vrefresh;
	if (interlaced)
		flags |= DRM_MODE_FLAG_INTERLACE;
	if (pos_hsync)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;
	if (pos_vsync)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;
	if (hskew)
		flags |= DRM_MODE_FLAG_HSKEW;

	type |= DRM_MODE_TYPE_DRIVER;

	new_mode->internal.flags = flags;
	new_mode->internal.type = type;
	strncpy(new_mode->internal.name, mode_name, DRM_DISPLAY_MODE_LEN - 1);

	if (output->custom_mode) {
		/* replace old custom mode with new_mode */
		list_del(&output->custom_mode->link);
		if (output->custom_mode->blob_id) {
			drmModeDestroyPropertyBlob(output->dev->fd,
					output->custom_mode->blob_id);
		}
		free(output->custom_mode);
	}

	output->custom_mode = new_mode;
	list_add_tail(&new_mode->link, &output->modes);

	return &new_mode->base;

err:
	return NULL;
}

static s32 drm_output_switch_mode(struct output *o, struct cb_mode *m)
{
	struct drm_output *output = to_drm_output(o);
	struct drm_mode *mode = to_drm_mode(m);

	if (!m)
		return -EINVAL;

	output->pending_mode = mode;
	drm_info("Pending mode: %ux%u@%u %.2fMHz",
		 output->pending_mode->base.width,
		 output->pending_mode->base.height,
		 output->pending_mode->base.vrefresh,
		 output->pending_mode->base.pixel_freq / 1000.0f);
	output->modeset_pending = true;

	return 0;
}

static s32 drm_output_add_page_flip_notify(struct output *o,
					   struct cb_listener *l)
{
	struct drm_output *output = to_drm_output(o);

	if (!o || !l)
		return -EINVAL;

	cb_signal_add(&output->flipped_signal, l);
	return 0;
}

static u32 drm_waitvblank_pipe(struct drm_output *output)
{
	if (output->index > 1)
		return (output->index << DRM_VBLANK_HIGH_CRTC_SHIFT) &
				DRM_VBLANK_HIGH_CRTC_MASK;
	else if (output->index > 0)
		return DRM_VBLANK_SECONDARY;
	else
		return 0;
}

static s32 drm_output_query_vblank(struct output *o, struct timespec *ts)
{
	struct drm_output *output = to_drm_output(o);
	drmVBlank vbl = {
		.request.type = DRM_VBLANK_RELATIVE,
		.request.sequence = 0,
		.request.signal = 0,
	};
	s32 ret;

	if (!o || !ts)
		return -EINVAL;

	vbl.request.type |= drm_waitvblank_pipe(output);
	ret = drmWaitVBlank(output->dev->fd, &vbl);
	if (ret < 0) {
		drm_err("failed to wait vblank %s", strerror(errno));
	}
	if (vbl.reply.tval_sec <= 0 && vbl.reply.tval_usec <= 0) {
		drm_err("illegal vblank time reply. %ld, %ld",
			vbl.reply.tval_sec, vbl.reply.tval_usec);
		ret = -1;
	}

	if (!ret) {
		ts->tv_sec = vbl.reply.tval_sec;
		ts->tv_nsec = vbl.reply.tval_usec * 1000;
	}

	return ret;
}

static void drm_output_destroy(struct output *o)
{
	struct drm_output *output = to_drm_output(o);
	struct drm_plane *plane, *next_plane;

	if (!output)
		return;

	list_del(&output->link);

	cb_signal_fini(&output->flipped_signal);

	list_for_each_entry_safe(plane, next_plane, &output->planes,
				 output_link) {
		drm_plane_destroy(&plane->base);
	}

	drm_clear_output_modes(output);

	drm_prop_finish(output->props, CRTC_PROP_NR);

	free(output);
}

static struct output *drm_output_create(struct drm_scanout *dev,
					s32 output_index,
					s32 primary_plane_index,
					s32 cursor_plane_index)
{
	struct drm_output *output = NULL;
	struct plane *plane;
	drmModeObjectProperties *props;
	s32 i;

	drm_info("Creating drm output (%d) ...", output_index);
	output = calloc(1, sizeof(*output));
	if (!output)
		goto err;

	output->dev = dev;
	output->index = output_index;
	output->base.index = output_index;
	INIT_LIST_HEAD(&output->planes);

	if (output_index >= dev->res->count_crtcs)
		goto err;

	output->crtc_id = dev->res->crtcs[output_index];

	props = drmModeObjectGetProperties(dev->fd, output->crtc_id,
					   DRM_MODE_OBJECT_CRTC);
	if (!props) {
		drm_err("failed to get object properties (%s)",strerror(errno));
		goto err;
	}
	drm_prop_prepare(dev, crtc_props, output->props, CRTC_PROP_NR, props);
	drmModeFreeObjectProperties(props);

	if (primary_plane_index >= 0) {
		plane = drm_plane_create(dev, output, primary_plane_index);
		if (!plane)
			goto err;
		output->primary = to_drm_plane(plane);
		list_add_tail(&(to_drm_plane(plane))->output_link,
			      &output->planes);
	}

	if (cursor_plane_index >= 0) {
		plane = drm_plane_create(dev, output, cursor_plane_index);
		if (!plane)
			goto err;
		output->cursor = to_drm_plane(plane);
		list_add_tail(&(to_drm_plane(plane))->output_link,
			      &output->planes);
	}

	/* create overlay */
	for (i = 0; i < dev->pres->count_planes; i++) {
		if (i == primary_plane_index)
			continue;
		if (i == cursor_plane_index)
			continue;
		plane = drm_plane_create(dev, output, i);
		if (!plane)
			continue;
		drm_debug("create overlay plane %d", i);
		list_add_tail(&(to_drm_plane(plane))->output_link,
			      &output->planes);
	}

	INIT_LIST_HEAD(&output->modes);

	cb_signal_init(&output->flipped_signal);

	list_add_tail(&output->link, &dev->outputs);
	drm_info("Create drm output complete.");

	return &output->base;

err:
	if (output)
		drm_output_destroy(&output->base);

	return NULL;
}

static void drm_scanout_cursor_bo_destroy(struct scanout *so,
					  struct cb_buffer *buffer)
{
	struct drm_fb *fb = to_drm_fb(buffer);

	drm_debug("Request to release gbm cursor bo");
	drm_fb_unref(fb);
}

static void drm_scanout_dumb_destroy(struct scanout *so,
				     struct cb_buffer *buffer)
{
	struct drm_fb *fb = to_drm_fb(buffer);

	drm_debug("Request to release DUMB");
	drm_fb_unref(fb);
}

static void drm_scanout_cursor_bo_update(struct scanout *so,
					 struct cb_buffer *cursor_buffer,
					 u8 *data,
					 u32 width,
					 u32 height,
					 u32 stride)
{
	struct drm_fb *fb = to_drm_fb(cursor_buffer);
	s32 i;
	u32 buf[fb->base.info.width * fb->base.info.height];

	if (!so || !cursor_buffer || !data || !width || !height || !stride)
		return;

	memset(buf, 0, sizeof(buf));
	for (i = 0; i < height; i++)
		memcpy(buf + i * fb->base.info.width,
		       data + i * stride,
		       width * 4);

	if (gbm_bo_write(fb->bo, buf, sizeof(buf)) < 0)
		drm_err("failed to write cursor bo. %s", strerror(errno));
}

static struct cb_buffer *drm_scanout_cursor_bo_create(
			struct scanout *so, struct cb_buffer_info *info)
{
	struct drm_fb *fb = NULL;
	struct drm_scanout *dev = to_dev(so);
	s32 ret;

	fb = cb_cache_get(dev->drm_fb_cache, true);
	if (!fb)
		goto err;

	fb->type = DRM_FB_TYPE_GBM_BO;

	cb_signal_init(&fb->base.destroy_signal);
	cb_signal_init(&fb->base.flip_signal);
	cb_signal_init(&fb->base.complete_signal);

	switch (info->pix_fmt) {
	case CB_PIX_FMT_ARGB8888:
		fb->fourcc = DRM_FORMAT_ARGB8888;
		drm_debug("create argb8888 cursor bo");
		break;
	default:
		drm_err("unsupported format.");
		goto err;
	}

	fb->bo = gbm_bo_create(dev->gbm, info->width, info->height,
			       GBM_FORMAT_ARGB8888,
			       GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
	if (!fb->bo) {
		drm_err("failed to create gbm cursor bo.(%s)", strerror(errno));
		drm_err("width: %u height: %u gbm: %p", info->width,
			info->height, dev->gbm);
		goto err;
	}

	fb->handles[0] = gbm_bo_get_handle(fb->bo).u32;
	info->strides[0] = gbm_bo_get_stride(fb->bo);
	info->offsets[0] = 0;
	info->planes = 1;
	drm_debug("stride: %u", info->strides[0]);

	fb->base.info = *info;

	ret = drmModeAddFB2(dev->fd, info->width, info->height, fb->fourcc,
			    fb->handles, fb->base.info.strides,
			    fb->base.info.offsets, &fb->fb_id, 0);
	if (ret) {
		drm_err("failed to create drm FB2. (%s)", strerror(errno));
		goto err;
	}
	drm_notice("FB info: %ux%u %u ID: %u", info->width, info->height,
		fb->base.info.strides[0], fb->fb_id);

	fb->dev = dev;

	fb->ref_cnt = 1;

	fb->base.info.type = CB_BUF_TYPE_DMA;

	return &fb->base;

err:
	if (fb->bo)
		gbm_bo_destroy(fb->bo);

	if (fb)
		cb_cache_put(fb, dev->drm_fb_cache);
	return NULL;
}

static struct cb_buffer *drm_scanout_dumb_create(struct scanout *so,
						 struct cb_buffer_info *info)
{
	struct drm_fb *fb = NULL;
	struct drm_scanout *dev = to_dev(so);
	struct drm_mode_create_dumb create_arg;
	struct drm_mode_map_dumb map_arg;
	struct drm_mode_destroy_dumb destroy_arg;
	s32 ret;

	fb = cb_cache_get(dev->drm_fb_cache, true);
	if (!fb)
		goto err;

	fb->type = DRM_FB_TYPE_DUMB;

	cb_signal_init(&fb->base.destroy_signal);
	cb_signal_init(&fb->base.flip_signal);
	cb_signal_init(&fb->base.complete_signal);

	memset(&create_arg, 0, sizeof(create_arg));

	switch (info->pix_fmt) {
	case CB_PIX_FMT_XRGB8888:
		fb->fourcc = DRM_FORMAT_XRGB8888;
		create_arg.bpp = 32;
		create_arg.width = (info->width + 16 - 1) & ~(16 - 1);
		create_arg.height = (info->height + 16 - 1) & ~(16 - 1);
		drm_debug("create xrgb8888 dumb buffer");
		break;
	case CB_PIX_FMT_ARGB8888:
		fb->fourcc = DRM_FORMAT_ARGB8888;
		create_arg.bpp = 32;
		create_arg.width = (info->width + 16 - 1) & ~(16 - 1);
		create_arg.height = (info->height + 16 - 1) & ~(16 - 1);
		drm_debug("create argb8888 dumb buffer");
		break;
	case CB_PIX_FMT_NV12:
		fb->fourcc = DRM_FORMAT_NV12;
		create_arg.bpp = 8;
		create_arg.width = (info->width + 16 - 1) & ~(16 - 1);
		create_arg.height = ((info->height + 16 - 1) & ~(16 - 1))*3/2;
		drm_debug("create nv12 dumb buffer");
		break;
	case CB_PIX_FMT_NV16:
		fb->fourcc = DRM_FORMAT_NV16;
		create_arg.bpp = 8;
		create_arg.width = (info->width + 16 - 1) & ~(16 - 1);
		create_arg.height = ((info->height + 16 - 1) & ~(16 - 1))*2;
		drm_debug("create nv16 dumb buffer");
		break;
	case CB_PIX_FMT_NV24:
		fb->fourcc = DRM_FORMAT_NV24;
		create_arg.bpp = 8;
		create_arg.width = (info->width + 16 - 1) & ~(16 - 1);
		create_arg.height = ((info->height + 16 - 1) & ~(16 - 1))*3;
		drm_debug("create nv24 dumb buffer");
		break;
	default:
		drm_err("unsupported format.");
		goto err;
	}
	
	ret = drmIoctl(dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg);

	if (ret) {
		drm_err("failed to create dumb buffer. (%s)", strerror(errno));
		goto err;
	}

	if (info->pix_fmt == CB_PIX_FMT_NV12) {
		fb->handles[0] = create_arg.handle;
		info->sizes[0] = create_arg.size;
		info->strides[0] = create_arg.pitch;
		info->offsets[0] = 0;
		fb->handles[1] = create_arg.handle;
		info->strides[1] = create_arg.pitch;
		info->offsets[1] = ((info->height + 16 - 1) & ~(16 - 1))
					* info->strides[0];
	} else if (info->pix_fmt == CB_PIX_FMT_NV16) {
		fb->handles[0] = create_arg.handle;
		info->sizes[0] = create_arg.size;
		info->strides[0] = create_arg.pitch;
		info->offsets[0] = 0;
		fb->handles[1] = create_arg.handle;
		info->strides[1] = create_arg.pitch * 2;
		info->offsets[1] = ((info->height + 16 - 1) & ~(16 - 1))
					* info->strides[0];
	} else if (info->pix_fmt == CB_PIX_FMT_NV24) {
		fb->handles[0] = create_arg.handle;
		info->sizes[0] = create_arg.size;
		info->strides[0] = create_arg.pitch;
		info->offsets[0] = 0;
		fb->handles[1] = create_arg.handle;
		info->strides[1] = create_arg.pitch * 2;
		info->offsets[1] = ((info->height + 16 - 1) & ~(16 - 1))
					* info->strides[0];
	} else if (info->pix_fmt == CB_PIX_FMT_ARGB8888) {
		fb->handles[0] = create_arg.handle;
		info->sizes[0] = create_arg.size;
		info->strides[0] = create_arg.pitch;
		info->offsets[0] = 0;
	} else if (info->pix_fmt == CB_PIX_FMT_XRGB8888) {
		fb->handles[0] = create_arg.handle;
		info->sizes[0] = create_arg.size;
		info->strides[0] = create_arg.pitch;
		info->offsets[0] = 0;
	}
	info->planes = 1;
	drm_debug("pitch: %u", info->strides[0]);

	ret = drmPrimeHandleToFD(dev->fd, create_arg.handle, 0,
				 &info->fd[0]);
	if (ret) {
		drm_err("failed to export buffer. (%s)", strerror(errno));
		goto err;
	}

	fb->base.info = *info;

	memset(&map_arg, 0, sizeof(map_arg));
	map_arg.handle = fb->handles[0];
	ret = drmIoctl(dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg);
	if (ret) {
		drm_err("failed to map dumb. (%s)", strerror(errno));
		goto err;
	}
	info->maps[0] = mmap(NULL, info->sizes[0],
			     PROT_WRITE, MAP_SHARED,
			     dev->fd, map_arg.offset);
	if (info->maps[0] == MAP_FAILED) {
		drm_err("failed to mmap. (%s)", strerror(errno));
		goto err;
	}

	drm_debug("dumb buffer: %p size: %lu", info->maps[0], info->sizes[0]);

	fb->base.info = *info;

	drm_debug("width: %u height: %u strides: %u,%u,%u,%u, "
		"offsets: %u,%u,%u,%u", info->width, info->height,
		info->strides[0],
		info->strides[1],
		info->strides[2],
		info->strides[3],
		info->offsets[0],
		info->offsets[1],
		info->offsets[2],
		info->offsets[3]);
	ret = drmModeAddFB2(dev->fd, info->width, info->height, fb->fourcc,
			    fb->handles, fb->base.info.strides,
			    fb->base.info.offsets, &fb->fb_id, 0);
	if (ret) {
		drm_err("failed to create drm FB2. (%s)", strerror(errno));
		goto err;
	}
	drm_notice("FB info: %ux%u %u ID: %u", info->width, info->height,
		fb->base.info.strides[0], fb->fb_id);

	fb->dev = dev;

	fb->ref_cnt = 1;

	fb->base.info.type = CB_BUF_TYPE_DMA;

	return &fb->base;

err:
	if (fb && fb->base.info.maps[0] && fb->base.info.sizes[0]) {
		munmap(fb->base.info.maps[0], fb->base.info.sizes[0]);
	}

	if (fb && fb->handles[0]) {
		memset(&destroy_arg, 0, sizeof(destroy_arg));
		destroy_arg.handle = fb->handles[0];
		drmIoctl(dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
	}

	if (fb && fb->base.info.fd[0] > 0)
		close(fb->base.info.fd[0]);

	if (fb)
		cb_cache_put(fb, dev->drm_fb_cache);
	return NULL;
}

static void drm_scanout_release_dmabuf(struct scanout *so,
				       struct cb_buffer *buffer)
{
	struct drm_fb *fb = to_drm_fb(buffer);

	cb_signal_fini(&fb->base.destroy_signal);
	cb_signal_fini(&fb->base.flip_signal);
	cb_signal_fini(&fb->base.complete_signal);
	drm_debug("Request to release DMA-BUF");
	drm_fb_unref(fb);
}

#ifdef USE_DRM_PRIME
static struct cb_buffer *drm_scanout_import_dmabuf(struct scanout *so,
						   struct cb_buffer_info *info)
{
	struct drm_fb *fb = NULL;
	struct drm_scanout *dev = to_dev(so);
	s32 ret;
	u32 handle = 0;

	fb = cb_cache_get(dev->drm_fb_cache, true);
	if (!fb)
		goto err;

	fb->dev = dev;

	fb->type = DRM_FB_TYPE_DMABUF;

	fb->base.info = *info;
	cb_signal_init(&fb->base.destroy_signal);
	cb_signal_init(&fb->base.flip_signal);
	cb_signal_init(&fb->base.complete_signal);

	/* init list head to prevent del crash */
	INIT_LIST_HEAD(&fb->base.dma_buf_flipped_l.link);
	INIT_LIST_HEAD(&fb->base.dma_buf_completed_l.link);

	switch (info->pix_fmt) {
	case CB_PIX_FMT_XRGB8888:
		fb->fourcc = DRM_FORMAT_XRGB8888;
		break;
	case CB_PIX_FMT_ARGB8888:
		fb->fourcc = DRM_FORMAT_ARGB8888;
		break;
	case CB_PIX_FMT_NV12:
		fb->fourcc = DRM_FORMAT_NV12;
		break;
	case CB_PIX_FMT_NV16:
		fb->fourcc = DRM_FORMAT_NV16;
		break;
	case CB_PIX_FMT_NV24:
		fb->fourcc = DRM_FORMAT_NV24;
		break;
	default:
		drm_err("unsupported format.");
		goto err;
	}

	ret = drmPrimeFDToHandle(dev->fd, fb->base.info.fd[0], &handle);
	if (ret) {
		drm_err("Failed to get handle from fd. (%s), drmfd (%d), "
			"fd (%d)",
			strerror(errno), dev->fd, fb->base.info.fd[0]);
		goto err;
	}
	fb->handles[0] = handle;
	if (info->pix_fmt == CB_PIX_FMT_ARGB8888 ||
	    info->pix_fmt == CB_PIX_FMT_XRGB8888)
		fb->handles[1] = fb->handles[2] = fb->handles[3] = 0;
	else if (info->pix_fmt == CB_PIX_FMT_NV12 ||
		 info->pix_fmt == CB_PIX_FMT_NV16 ||
		 info->pix_fmt == CB_PIX_FMT_NV24) {
		fb->handles[1] = fb->handles[0];
		fb->handles[2] = fb->handles[3] = 0;
	}

	drm_notice("width: %u, height: %u, fourcc: %4.4s, handles: %u,%u,%u,%u "
		"strides: %u,%u,%u,%u, offsets: %u,%u,%u,%u",
		info->width, info->height, (char *)(&fb->fourcc),
		fb->handles[0], fb->handles[1], fb->handles[2], fb->handles[3],
		fb->base.info.strides[0],
		fb->base.info.strides[1],
		fb->base.info.strides[2],
		fb->base.info.strides[3],
		fb->base.info.offsets[0],
		fb->base.info.offsets[1],
		fb->base.info.offsets[2],
		fb->base.info.offsets[3]);
	/* printf("fourcc: %4.4s\n", (char *)&fb->fourcc); */
	ret = drmModeAddFB2(dev->fd, info->width, info->height, fb->fourcc,
			    fb->handles, fb->base.info.strides,
			    fb->base.info.offsets, &fb->fb_id, 0);
	if (ret) {
		drm_err("failed to create drm FB2. (%s)", strerror(errno));
		goto err;
	}
	/*
	printf("FB info: %ux%u %u ID: %u\n", info->width, info->height,
		fb->base.info.strides[0], fb->fb_id);
	*/

	fb->ref_cnt = 1;

	fb->base.info.type = CB_BUF_TYPE_DMA;

	return &fb->base;

err:
	if (fb && handle && fb->base.info.fd[0]) {
		drm_debug("close handle");
		close(fb->base.info.fd[0]);
		fb->base.info.fd[0] = 0;
	}

	if (fb)
		cb_cache_put(fb, dev->drm_fb_cache);
	return NULL;
}
#else
static struct cb_buffer *drm_scanout_import_dmabuf(struct scanout *so,
						   struct cb_buffer_info *info)
{
	struct drm_fb *fb = NULL;
	struct drm_scanout *dev = to_dev(so);
	s32 ret;
	struct gbm_import_fd_data import_data = {
		.width = info->width,
		.height = info->height,
		.format = DRM_FORMAT_XRGB8888,
		.stride = info->strides[0],
		.fd = info->fd[0],
	};

	fb = cb_cache_get(dev->drm_fb_cache, true);
	if (!fb)
		goto err;

	fb->type = DRM_FB_TYPE_DMABUF;

	fb->base.info = *info;
	cb_signal_init(&fb->base.destroy_signal);
	cb_signal_init(&fb->base.flip_signal);
	cb_signal_init(&fb->base.complete_signal);

	switch (info->pix_fmt) {
	case CB_PIX_FMT_XRGB8888:
		fb->fourcc = DRM_FORMAT_XRGB8888;
		break;
	case CB_PIX_FMT_ARGB8888:
		fb->fourcc = DRM_FORMAT_ARGB8888;
		break;
	case CB_PIX_FMT_NV12:
		fb->fourcc = DRM_FORMAT_NV12;
		break;
	case CB_PIX_FMT_NV16:
		fb->fourcc = DRM_FORMAT_NV16;
		break;
	case CB_PIX_FMT_NV24:
		fb->fourcc = DRM_FORMAT_NV24;
		break;
	case CB_PIX_FMT_RGB888:
	case CB_PIX_FMT_RGB565:
	case CB_PIX_FMT_YUYV:
	case CB_PIX_FMT_YUV420:
	case CB_PIX_FMT_YUV422:
	case CB_PIX_FMT_YUV444:
	default:
		drm_err("unsupported format.");
		goto err;
	}

	fb->bo = gbm_bo_import(dev->gbm,
			       GBM_BO_IMPORT_FD, &import_data,
			       GBM_BO_USE_SCANOUT);
	if (!fb->bo) {
		drm_err("Failed to import dma-buf by gbm. (%s)",
			strerror(errno));
		goto err;
	}
	fb->handles[0] = gbm_bo_get_handle(fb->bo).s32;
	if (fb->handles[0] == (u32)(-1)) {
		drm_err("Failed to get dma-buf's handle. (%s)",
			strerror(errno));
		goto err;
	}

	ret = drmModeAddFB2(dev->fd, info->width, info->height, fb->fourcc,
			    fb->handles, fb->base.info.strides,
			    fb->base.info.offsets, &fb->fb_id, 0);
	if (ret) {
		drm_err("failed to create drm FB2. (%s)", strerror(errno));
		goto err;
	}
	drm_notice("FB info: %ux%u %u ID: %u", info->width, info->height,
		fb->base.info.strides[0], fb->fb_id);

	fb->dev = dev;

	fb->ref_cnt = 1;

	fb->base.info.type = CB_BUF_TYPE_DMA;

	return &fb->base;

err:
	if (fb && fb->bo) {
		drm_debug("gbm bo destroy");
		gbm_bo_destroy(fb->bo);
		fb->bo = NULL;
	}

	if (fb)
		cb_cache_put(fb, dev->drm_fb_cache);
	return NULL;
}
#endif

static void drm_fb_destroy_surface_fb(struct gbm_bo *bo, void *data)
{
	struct drm_fb *fb = data;
	struct drm_scanout *dev = fb->dev;

	if (fb) {
		assert(fb->type == DRM_FB_TYPE_GBM_SURFACE);
		if (fb->fb_id) {
			drm_debug("Remove surface DRM FB");
			drmModeRmFB(dev->fd, fb->fb_id);
		}
		if (fb->destroy_surface_fb_cb) {
			fb->destroy_surface_fb_cb(&fb->base,
					fb->destroy_surface_fb_cb_userdata);
		}
		cb_cache_put(fb, dev->drm_fb_cache);
	}
}

static void drm_scanout_put_surface_buf(struct scanout *so,
					struct cb_buffer *buffer)
{
	struct drm_fb *fb = to_drm_fb(buffer);

	drm_debug("release surface buffer manually.");
	/* printf("release surface buffer manually.\n"); */
	drm_fb_release_buffer(fb);
}

static struct cb_buffer *drm_scanout_get_surface_buf(struct scanout *so,
						     void *surface,
						     void (*destroy_cb)(
						     	struct cb_buffer *b,
						     	void *userdata),
						     void *userdata)
{
	struct drm_fb *fb = NULL;
	struct drm_scanout *dev = to_dev(so);
	struct gbm_bo *bo;
	s32 ret;

	/* printf("begin lock front buffer\n"); */
	bo = gbm_surface_lock_front_buffer((struct gbm_surface *)surface);
	/* printf("end lock front buffer\n"); */
	if (!bo) {
		drm_err("failed to lock front buffer: %s", strerror(errno));
		goto err;
	}

	fb = gbm_bo_get_user_data(bo);
	if (fb) {
		/* printf("get bo from userdata fb->id: %u, ref: %d\n",
			fb->fb_id, fb->ref_cnt); */
		return &fb->base;
	}

	fb = cb_cache_get(dev->drm_fb_cache, true);
	if (!fb)
		goto err;

	fb->type = DRM_FB_TYPE_GBM_SURFACE;

	fb->bo = bo;

	fb->base.info.width = gbm_bo_get_width(bo);
	fb->base.info.height = gbm_bo_get_height(bo);
	fb->fourcc = gbm_bo_get_format(bo);

	fb->base.info.strides[0] = gbm_bo_get_stride(bo);
	fb->handles[0] = gbm_bo_get_handle(bo).u32;

	ret = drmModeAddFB2(dev->fd,
			    fb->base.info.width, fb->base.info.height,
			    fb->fourcc,
			    fb->handles, fb->base.info.strides,
			    fb->base.info.offsets, &fb->fb_id, 0);
	if (ret) {
		drm_err("failed to create surface fb: %s", strerror(errno));
		goto err;
	}
	drm_notice("GBM Surface FB info: %ux%u %u ID: %u", fb->base.info.width,
		fb->base.info.height,
		fb->base.info.strides[0], fb->fb_id);

	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_surface_fb);

	cb_signal_init(&fb->base.destroy_signal);
	cb_signal_init(&fb->base.flip_signal);
	cb_signal_init(&fb->base.complete_signal);

	fb->dev = dev;
	fb->ref_cnt = 1;
	fb->surface = (struct gbm_surface *)surface;

	fb->base.info.type = CB_BUF_TYPE_SURFACE;
	fb->destroy_surface_fb_cb = destroy_cb;
	fb->destroy_surface_fb_cb_userdata = userdata;

	return &fb->base;

err:
	if (fb && fb->bo) {
		/* printf("begin release surface buffer ID: %u\n", fb->fb_id);*/
		gbm_surface_release_buffer((struct gbm_surface *)surface,
					   fb->bo);
		/* printf("end release surface buffer ID: %u\n", fb->fb_id);*/
		fb->bo = NULL;
	}

	if (fb)
		cb_cache_put(fb, dev->drm_fb_cache);
	return NULL;
}

static void drm_scanout_pipeline_destroy(struct scanout *so, struct output *o)
{
	struct head *h;
	struct drm_output *output = to_drm_output(o);
	u32 crtc_id;

	if (!so)
		return;

	if (!o)
		return;

	drm_info("Destroy pipeline CRTC_ID[%u] ...", output->crtc_id);
	h = o->head;

	crtc_id = output->crtc_id;
	drm_output_destroy(o);
	drm_head_destroy(h);
	drm_info("Destroy pipeline CRTC_ID[%u] complete.", crtc_id);
}

static void drm_head_update_modes(struct drm_head *head)
{
	struct drm_output *output;
	struct drm_mode *mode, *new_mode;
	struct drm_mode *custom_mode;
	bool preserved = false;
	s32 i;
	drmModeConnectorPtr conn;

	output = to_drm_output(head->base.output);

	/* preserve custom mode */
	if (output->custom_mode) {
		drm_debug("old custom mode is preserved.");
		custom_mode = calloc(1, sizeof(*custom_mode));
		assert(custom_mode);
		memcpy(custom_mode, output->custom_mode, sizeof(*custom_mode));
		custom_mode->blob_id = 0;
		preserved = true;
	}

	drm_clear_output_modes(output);

	conn = head->connector;
	if (!conn)
		return;

	for (i = 0; i < conn->count_modes; i++) {
		if (conn->modes[i].flags & DRM_MODE_FLAG_INTERLACE)
			continue;
		mode = calloc(1, sizeof(*mode));
		memcpy(&mode->internal, &conn->modes[i],sizeof(conn->modes[i]));
		mode->base.width = conn->modes[i].hdisplay;
		mode->base.height = conn->modes[i].vdisplay;
		mode->base.vrefresh = conn->modes[i].vrefresh;
		mode->base.pixel_freq = conn->modes[i].clock;
		mode->base.preferred
			= conn->modes[i].type & DRM_MODE_TYPE_PREFERRED
				? true : false;
		list_add_tail(&mode->link, &output->modes);
		drm_debug("Mode: %ux%u@%u %.2fMHz %s",
			mode->base.width,
			mode->base.height,
			mode->base.vrefresh,
			mode->base.pixel_freq / 1000.0f,
			mode->base.preferred ? "Preferred" : "");
	}

	if (preserved) {
		drm_debug("old custom mode is restored.");
		output->custom_mode = custom_mode;
		list_add_tail(&custom_mode->link, &output->modes);
	}

	new_mode = NULL;
	list_for_each_entry(mode, &output->modes, link) {
		if (mode->base.preferred) {
			new_mode = mode;
			break;
		}
	}

	if (!new_mode) {
		list_for_each_entry(mode, &output->modes, link) {
			new_mode = mode;
			break;
		}
	}

	if (!new_mode) {
		drm_err("Cannot find suitable video timing!");
		return;
	}

	output->pending_mode = new_mode;
	drm_info("Pending mode: %ux%u@%u %.2fMHz",
		 output->pending_mode->base.width,
		 output->pending_mode->base.height,
		 output->pending_mode->base.vrefresh,
		 output->pending_mode->base.pixel_freq / 1000.0f);
	output->modeset_pending = true;
}

static struct output *
drm_scanout_pipeline_create(struct scanout *so, struct pipeline *pipeline_cfg)
{
	struct drm_scanout *dev;
	struct output *output = NULL;
	struct head *head = NULL;

	if (!so) {
		drm_err("so is null");
		return NULL;
	}

	if (!pipeline_cfg) {
		drm_err("pipeline_cfg is null");
		return NULL;
	}

	drm_info("Create pipeline %d -> %d...", pipeline_cfg->output_index,
		 pipeline_cfg->head_index);
	dev = to_dev(so);

	/* create head */
	head = drm_head_create(dev, pipeline_cfg->head_index);
	if (!head)
		goto err;

	/* create output and planes */
	output = drm_output_create(dev, pipeline_cfg->output_index,
				   pipeline_cfg->primary_plane_index,
				   pipeline_cfg->cursor_plane_index);
	if (!output)
		goto err;

	/* finally link output -> head */
	output->head = head;
	head->output = output;
	if (head->connected) {
		drm_head_update_modes(to_drm_head(head));
	}

	output->enable = drm_output_enable;
	output->disable = drm_output_disable;
	output->get_preferred_mode = drm_output_get_preferred_mode;
	output->get_current_mode = drm_output_get_current_mode;
	output->enumerate_mode = drm_output_enumerate_mode;
	output->get_custom_mode = drm_output_get_custom_mode;
	output->enumerate_plane = drm_output_enumerate_plane;
	output->enumerate_plane_by_fmt = drm_output_enumerate_plane_by_fmt;
	output->switch_mode = drm_output_switch_mode;
	output->create_custom_mode = drm_output_create_custom_mode;
	output->native_surface_create = drm_output_native_surface_create;
	output->native_surface_destroy = drm_output_native_surface_destroy;
	output->add_page_flip_notify = drm_output_add_page_flip_notify;
	output->query_vblank = drm_output_query_vblank;

	drm_info("Create pipeline complete");

	return output;
err:
	if (output)
		drm_output_destroy(output);

	if (head)
		drm_head_destroy(head);

	return NULL;
}

static s32 get_drm_dev_sysnum(struct udev *udev, const char *devname, s32 *sn)
{
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	const char *path, *sysnum, *devnode;
	struct udev_device *drm_device = NULL;
	s32 ret;

	if (!udev)
		return -EINVAL;

	if (!devname)
		return -EINVAL;

	if (!sn)
		return -EINVAL;

	e = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(e, "drm");
	udev_enumerate_add_match_sysname(e, "card[0-9]*");
	udev_enumerate_scan_devices(e);
	drm_device = NULL;
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		path = udev_list_entry_get_name(entry);
		drm_device = udev_device_new_from_syspath(udev, path);
		if (!drm_device) {
			continue;
		} else {
			devnode = udev_device_get_devnode(drm_device);
			if (!devnode) {
				udev_device_unref(drm_device);
				continue;
			}
			if (strcmp(devnode, devname)) {
				udev_device_unref(drm_device);
				continue;
			} else {
				sysnum = udev_device_get_sysnum(drm_device);
				if (!sysnum) {
					drm_err("get_sysnum failed. %s",
						strerror(errno));
					udev_device_unref(drm_device);
					drm_device = NULL;
					goto err;
				}
				*sn = atoi(sysnum);
				drm_info("%s's sysnum = %d", devnode, *sn);
				udev_device_unref(drm_device);
				break;
			}
		}
	}

err:
	udev_enumerate_unref(e);
	if (drm_device)
		ret = 0;
	else
		ret = -EFAULT;
	return ret;
}

static void drm_head_update(struct drm_scanout *dev)
{
	struct drm_head *head;
	bool connected;
	drmModeObjectProperties *props;

	list_for_each_entry(head, &dev->heads, link) {
		connected = false;
		if (head->connector) {
			drmModeFreeConnector(head->connector);
			head->connector = NULL;
		}

		head->connector = drmModeGetConnector(dev->fd,
						      head->connector_id);
		if (!head->connector) {
			drm_err("failed to get drm connector. (%s)",
				strerror(errno));
			continue;
		}

		if (head->connector->connection == DRM_MODE_CONNECTED)
			connected = true;
		if (head->base.connected != connected) {
			drm_notice("head %u status changed (%s)",
				   head->connector_id,
				   connected ? "true" : "false");
			head->base.connected = connected;
			drm_prop_finish(head->props, CONNECTOR_PROP_NR);
			props = drmModeObjectGetProperties(
					dev->fd, head->connector_id,
					DRM_MODE_OBJECT_CONNECTOR);
			drm_prop_prepare(dev, connector_props, head->props,
					 CONNECTOR_PROP_NR, props);
			if (head->base.connected) {
				drm_get_and_parse_edid(head, props);
				if (head->edid.data && head->edid.length &&
				    strlen(head->monitor_name)) {
					head->base.monitor_name
						= head->monitor_name;
				}
			} else {
				memset(head->monitor_name, 0, MONITOR_NAME_LEN);
			}
			drmModeFreeObjectProperties(props);
			if (head->base.connected)
				drm_head_update_modes(head);
			cb_signal_emit(&head->head_changed_signal, &head->base);
		}
	}
}

static s32 drm_udev_event_cb(s32 fd, u32 mask, void *data)
{
	struct udev_device *device;
	struct drm_scanout *dev = data;
	const char *sysnum;
	const char *val;

	device = udev_monitor_receive_device(dev->udev_monitor);
	sysnum = udev_device_get_sysnum(device);
	if (!sysnum || atoi(sysnum) != dev->sysnum) {
		udev_device_unref(device);
		return 0;
	}

	val = udev_device_get_property_value(device, "HOTPLUG");
	if (val && (!strcmp(val, "1"))) {
		drm_head_update(dev);
	}
	udev_device_unref(device);

	return 0;
}

static void drm_scanout_destroy(struct scanout *so)
{
	struct drm_scanout *dev;
	struct drm_output *output, *next_output;
	struct drm_head *head, *next_head;

	if (!so)
		return;

	drm_info("Destroy scanout device ...");
	dev = to_dev(so);

	if (dev->udev_drm_source) {
		cb_event_source_remove(dev->udev_drm_source);
		udev_monitor_unref(dev->udev_monitor);
		dev->udev_drm_source = NULL;
	}

	if (dev->udev) {
		udev_unref(dev->udev);
		dev->udev = NULL;
	}

	if (dev->drm_source) {
		cb_event_source_remove(dev->drm_source);
		dev->drm_source = NULL;
	}

	list_for_each_entry_safe(output, next_output, &dev->outputs, link) {
		drm_output_destroy(&output->base);
	}

	list_for_each_entry_safe(head, next_head, &dev->heads, link) {
		drm_head_destroy(&head->base);
	}

	if (dev->gbm)
		gbm_device_destroy(dev->gbm);

	if (dev->fd > 0) {
		if (dev->res)
			drmModeFreeResources(dev->res);
		if (dev->pres)
			drmModeFreePlaneResources(dev->pres);
		close(dev->fd);
	}

	if (dev->pls_cache)
		cb_cache_destroy(dev->pls_cache);
	if (dev->os_cache)
		cb_cache_destroy(dev->os_cache);
	if (dev->ps_cache)
		cb_cache_destroy(dev->ps_cache);
	if (dev->drm_fb_cache)
		cb_cache_destroy(dev->drm_fb_cache);

	free(dev);
	drm_info("Destroy scanout device complete.");
}

static void drm_output_emit_bo_flipped(struct drm_output_state *os)
{
	struct drm_plane_state *pls;
	struct cb_buffer *buffer;

	if (!os)
		return;

	list_for_each_entry(pls, &os->plane_states, link) {
		if (pls->fb) {
			buffer = &pls->fb->base;
			drm_debug("buffer %ux%u dirty: %08X",
				  buffer->info.width, buffer->info.height,
				  buffer->dirty);
			if (scanout_clr_buffer_dirty(buffer, &os->output->base))
				cb_signal_emit(&buffer->flip_signal, buffer);
		}
	}
}

static void drm_output_complete(struct drm_output *output,
				u32 sec, u32 usec)
{
	/* update flipped time stamp for compositor use */
	output->base.sec = sec;
	output->base.usec = usec;

	drm_debug("drm_output_complete, [%d] state_last: %p",
		  output->index, output->state_last);
	/* emit buffer flipped signal */
	drm_output_emit_bo_flipped(output->state_cur);

	drm_output_state_destroy(output->state_last);
	if (output->state_last)
		output->state_last = NULL;

	/* emit output flipped signal for compositor using */
	cb_signal_emit(&output->flipped_signal, &output->base);

	drm_debug("drm_output_complete, leave.");
}

static void page_flip_handler(s32 fd, u32 crtc_id, u32 frame, u32 sec, u32 usec,
			      void *data)
{
	struct drm_scanout *dev = data;
	struct drm_output *output;
	bool found = false;

	drm_debug("CRTC_ID: %u frame: %u (%u, %u)", crtc_id, frame, sec, usec);
	list_for_each_entry(output, &dev->outputs, link) {
		if (output->crtc_id == crtc_id) {
			drm_debug("send page flip to user");
			output->page_flip_pending = false;
			drm_output_complete(output, sec, usec);
			found = true;
		}
	}

	if (!found) {
		drm_err("cannot find crtc_id %u", crtc_id);
	}
}

static s32 drm_event_cb(s32 fd, u32 mask, void *data)
{
	drmEventContext ctx;

	memset(&ctx, 0, sizeof(ctx));
	ctx.version = 3;
	ctx.page_flip_handler2 = page_flip_handler;
	ctx.vblank_handler = NULL;
	drmHandleEvent(fd, &ctx);
	return 0;
}

static void *drm_scanout_get_native_dev(struct scanout *so)
{
	struct drm_scanout *dev = to_dev(so);

	return dev->gbm;
}

static u32 drm_scanout_get_native_format(struct scanout *so)
{
	struct drm_scanout *dev = to_dev(so);

	return dev->gbm_format;
}

static s32 drm_add_buffer_flip_notify(struct scanout *so,
				      struct cb_buffer *buffer,
				      struct cb_listener *l)
{
	if (!so || !buffer || !l)
		return -EINVAL;

	list_del(&l->link);
	cb_signal_add(&buffer->flip_signal, l);
	return 0;
}

static s32 drm_add_buffer_complete_notify(struct scanout *so,
					  struct cb_buffer *buffer,
					  struct cb_listener *l)
{
	if (!so || !buffer || !l)
		return -EINVAL;

	list_del(&l->link);
	cb_signal_add(&buffer->complete_signal, l);
	return 0;
}

static u32 drm_get_clock_type(struct scanout *so)
{
	struct drm_scanout *dev = to_dev(so);
	u64 cap;
	s32 ret;
	
	ret = drmGetCap(dev->fd, DRM_CAP_TIMESTAMP_MONOTONIC, &cap);
	if (ret == 0 && cap == 1) {
		drm_info("DRM TIMESTAMP: MONOTONIC");
		return CLOCK_MONOTONIC;
	} else {
		drm_info("DRM TIMESTAMP: REALTIME");
		return CLOCK_REALTIME;
	}
}

struct scanout *scanout_create(const char *dev_path, struct cb_event_loop *loop)
{
	struct drm_scanout *dev = NULL;
	s32 ret;

	drm_info("Create scanout device [%s] ...", dev_path);
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

	dev->drm_fb_cache = cb_cache_create(sizeof(struct drm_fb), 128);
	if (!dev->drm_fb_cache)
		goto err;
	dev->ps_cache = cb_cache_create(sizeof(struct drm_pending_state), 128);
	if (!dev->ps_cache)
		goto err;
	dev->os_cache = cb_cache_create(sizeof(struct drm_output_state), 128);
	if (!dev->os_cache)
		goto err;
	dev->pls_cache = cb_cache_create(sizeof(struct drm_plane_state), 128);
	if (!dev->pls_cache)
		goto err;

	dev->loop = loop;
	dev->base.destroy = drm_scanout_destroy;
	dev->base.set_dbg_level = drm_scanout_set_dbg_level;

	dev->fd = open(dev_path, O_RDWR | O_CLOEXEC, 0644);
	if (!dev->fd)
		goto err;

	dev->gbm = gbm_create_device(dev->fd);
	dev->gbm_format = GBM_FORMAT_XRGB8888;
	drm_notice("gbm = %p, gbm_format = %u", dev->gbm, dev->gbm_format);

	drmSetMaster(dev->fd);

	ret = drmSetClientCap(dev->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	if (ret) {
		drm_err("DRM driver does not support universal planes. (%s)",
			strerror(errno));
		goto err;
	}

	ret = drmSetClientCap(dev->fd, DRM_CLIENT_CAP_ATOMIC, 1);
	if (ret) {
		drm_err("DRM driver does not support atomic API. (%s)",
			strerror(errno));
		goto err;
	}

	dev->res = drmModeGetResources(dev->fd);
	if (!dev->res) {
		drm_err("failed to get drm resource. (%s)", strerror(errno));
		goto err;
	}

	dev->pres = drmModeGetPlaneResources(dev->fd);
	if (!dev->pres) {
		drm_err("failed to get drm plane resource. (%s)",
			strerror(errno));
		goto err;
	}

	dev->drm_source = cb_event_loop_add_fd(dev->loop, dev->fd,
						CB_EVT_READABLE,
						drm_event_cb, dev);
	if (!dev->drm_source)
		goto err;

	dev->udev = udev_new();
	if (!dev->udev) {
		drm_err("failed to new udev. (%s)", strerror(errno));
		goto err;
	}

	if (get_drm_dev_sysnum(dev->udev, dev_path, &dev->sysnum) < 0)
		goto err;

	dev->udev_monitor = udev_monitor_new_from_netlink(dev->udev, "udev");
	if (!dev->udev_monitor) {
		drm_err("failed to create udev monitor. (%s)", strerror(errno));
		goto err;
	}

	udev_monitor_filter_add_match_subsystem_devtype(dev->udev_monitor,
							"drm", NULL);
	udev_monitor_enable_receiving(dev->udev_monitor);

	dev->udev_drm_source = cb_event_loop_add_fd(dev->loop,
						    udev_monitor_get_fd(
						        dev->udev_monitor),
						    CB_EVT_READABLE,
						    drm_udev_event_cb, dev);
	if (!dev->udev_drm_source)
		goto err;

	INIT_LIST_HEAD(&dev->outputs);
	INIT_LIST_HEAD(&dev->heads);
	INIT_LIST_HEAD(&dev->planes);

	dev->base.pipeline_create = drm_scanout_pipeline_create;
	dev->base.pipeline_destroy = drm_scanout_pipeline_destroy;
	dev->base.get_surface_buf = drm_scanout_get_surface_buf;
	dev->base.put_surface_buf = drm_scanout_put_surface_buf;
	dev->base.import_dmabuf = drm_scanout_import_dmabuf;
	dev->base.release_dmabuf = drm_scanout_release_dmabuf;
	dev->base.dumb_buffer_create = drm_scanout_dumb_create;
	dev->base.dumb_buffer_destroy = drm_scanout_dumb_destroy;
	dev->base.cursor_bo_create = drm_scanout_cursor_bo_create;
	dev->base.cursor_bo_destroy = drm_scanout_cursor_bo_destroy;
	dev->base.cursor_bo_update = drm_scanout_cursor_bo_update;
	dev->base.scanout_data_alloc = drm_scanout_data_alloc;
	dev->base.do_scanout = drm_do_scanout;
	dev->base.fill_scanout_data = drm_scanout_data_fill;
	dev->base.get_native_dev = drm_scanout_get_native_dev;
	dev->base.get_native_format = drm_scanout_get_native_format;
	dev->base.add_buffer_flip_notify = drm_add_buffer_flip_notify;
	dev->base.add_buffer_complete_notify = drm_add_buffer_complete_notify;
	dev->base.get_clock_type = drm_get_clock_type;

	drm_info("Create scanout device complete.");
	return &dev->base;

err:
	if (dev) {
		dev->base.destroy(&dev->base);
	}
	return NULL;
}

