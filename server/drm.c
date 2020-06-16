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

enum drm_prop_type {
	DRM_PROP_TYPE_UNKNOWN = 0,
	DRM_PROP_TYPE_ENUM,
	DRM_PROP_TYPE_BLOB,
	DRM_PROP_TYPE_RANGE,
	DRM_PROP_TYPE_SIGNED_RANGE,
	DRM_PROP_TYPE_OBJECT,
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
	CONNECTOR_PROP_DPMS,
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
	CONNECTOR_DPMS_IDX_ON = 0,
	CONNECTOR_DPMS_IDX_OFF,
	CONNECTOR_DPMS_IDX_STANDBY,
	CONNECTOR_DPMS_IDX_SUSPEND,
	CONNECTOR_DPMS_IDX_NR,
};

static struct enum_value connector_dpms_enum[] = {
	[CONNECTOR_DPMS_IDX_ON] = {
		.name = "On",
	},
	[CONNECTOR_DPMS_IDX_OFF] = {
		.name = "Off",
	},
	[CONNECTOR_DPMS_IDX_STANDBY] = {
		.name = "Standby",
	},
	[CONNECTOR_DPMS_IDX_SUSPEND] = {
		.name = "Suspend",
	},
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
	[CONNECTOR_PROP_DPMS] = {
		.name = "DPMS",
		.type = DRM_PROP_TYPE_ENUM,
		.c = {
			.ev = {
				.count_values = CONNECTOR_DPMS_IDX_NR,
				.values = connector_dpms_enum,
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
	PLANE_PROP_NR,
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

struct drm_fb {
	struct cb_buffer base;
	struct gbm_bo *bo;
	u32 handles[4];
	u32 fourcc;
	s32 ref_cnt;
	u32 fb_id;
	void *dev;
};

struct drm_scanout;
struct drm_plane;
struct drm_output;

struct drm_plane_state {
	struct drm_fb *fb;
	struct drm_plane *plane;
	struct list_head link;
	s32 zpos;
	s32 crtc_x, crtc_y;
	u32 crtc_w, crtc_h;
	u32 src_x, src_y;
	u32 src_w, src_h;
};

struct drm_output_state {
	struct drm_output *output;
	enum dpms_state dpms;
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
	struct drm_mode *current_mode, *pending_mode;
	struct list_head modes;

	struct drm_output_state *state_cur, *state_last;

	struct cb_signal output_complete_signal;
	struct cb_signal page_flip_signal;

	bool page_flip_pending;

	struct list_head link;
	struct drm_plane *primary;
	struct drm_plane *cursor;
	struct drm_plane *overlay;

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

	s32 fd;
	struct cb_event_source *drm_source;
	drmModeResPtr res;
	drmModePlaneResPtr pres;

	struct list_head outputs;
	struct list_head heads;
	struct list_head planes;
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

static u64 *drm_get_range_prop_value(struct drm_prop *prop_info)
{
	if ((prop_info->type != DRM_PROP_TYPE_RANGE) &&
	    (prop_info->type != DRM_PROP_TYPE_SIGNED_RANGE))
		return NULL;

	if (!prop_info->valid)
		return NULL;

	return prop_info->c.rv.values;
}

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
		if (template[j].type == DRM_PROP_TYPE_ENUM) {
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
		case DRM_PROP_TYPE_ENUM:
			drm_info("Find enum%s property: %s",
				 dst[j].atomic ? " atomic" : "", dst[j].name);
			for (k = 0; k < dst[j].c.ev.count_values; k++) {
				for (m = 0; m < prop->count_enums; m++) {
					/*drm_info("%s", prop->enums[m].name);*/
					if (!strcmp(prop->enums[m].name,
						    dst[j].c.ev.values[k].name))
						break;
				}

				if (m == prop->count_enums)
					continue;

				dst[j].c.ev.values[k].valid = true;
				dst[j].c.ev.values[k].value
					= prop->enums[m].value;
				/*
				drm_info("\t%s - %llu",
					 dst[j].c.ev.values[k].name,
					 dst[j].c.ev.values[k].value);
				*/
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
			/*
			drm_info("\t[%" PRId64 " - %" PRId64 "]",
				 dst[j].c.rv.values[0], dst[j].c.rv.values[1]);
			*/
			break;
		case DRM_PROP_TYPE_SIGNED_RANGE:
			dst[j].c.rv.count_values = prop->count_values;
			if (prop->count_values != 2)
				drm_warn("range value count is not 2");
			for (k = 0; k < dst[j].c.rv.count_values; k++)
				dst[j].c.rv.values[k] = prop->values[k];
			drm_info("Find singed range%s property: %s",
				 dst[j].atomic ? " atomic" : "", dst[j].name);
			/*
			drm_info("\t[%" PRIu64 " - %" PRIu64 "]",
				 dst[j].c.rv.values[0], dst[j].c.rv.values[1]);
			*/
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
		if (props[i].type == DRM_PROP_TYPE_ENUM)
			free(props[i].c.ev.values);
	}

	memset(props, 0, count_props * sizeof(*props));
}

static void drm_pending_state_destroy(struct drm_pending_state *ps)
{

}

static struct drm_pending_state *
drm_pending_state_create(struct drm_scanout *dev)
{
	struct drm_pending_state *ps;

	ps = calloc(1, sizeof(*ps));
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

	os = calloc(1, sizeof(*os));
	if (!os)
		return NULL;

	os->output = output;
	INIT_LIST_HEAD(&os->plane_states);
	list_add_tail(&os->link, &ps->output_states);

	return os;
}

static struct drm_plane_state *
drm_plane_state_create(struct drm_output_state *os, struct drm_plane *plane)
{
	struct drm_plane_state *ps;

	ps = calloc(1, sizeof(*ps));
	if (!ps)
		return NULL;

	ps->plane = plane;
	list_add_tail(&ps->link, &os->plane_states);
	return ps;
}

static void drm_fb_ref(struct drm_fb *fb)
{
	drm_debug("[REF] +");
	if (fb)
		fb->ref_cnt++;
}

static void drm_fb_release_buffer(struct drm_fb *fb)
{
	struct drm_scanout *dev = fb->dev;

	drm_debug("release buffer");
	if (fb && fb->bo) {
		drm_debug("gbm bo destroy");
		gbm_bo_destroy(fb->bo);
	}

	if (fb) {
		if (fb->fb_id)
			drmModeRmFB(dev->fd, fb->fb_id);
		free(fb);
	}
}

static void drm_fb_unref(struct drm_fb *fb)
{
	if (!fb)
		return;

	drm_debug("[REF] -");
	if (fb->ref_cnt <= 0) {
		drm_err("incorrect ref cnt ! %d", fb->ref_cnt);
		return;
	}

	fb->ref_cnt--;
	if (fb->ref_cnt == 0) {
		drm_fb_release_buffer(fb);
	}
}

static void drm_plane_state_destroy(struct drm_plane_state *pls)
{
	if (!pls)
		return;

	drm_debug("destroy plane state");
	if (pls->fb)
		drm_fb_unref(pls->fb);
	free(pls);
}

static void drm_output_state_destroy(struct drm_output_state *os)
{
	struct drm_plane_state *pls, *next_pls;

	if (!os)
		return;

	drm_debug("destroy output state");
	list_for_each_entry_safe(pls, next_pls, &os->plane_states, link) {
		list_del(&pls->link);
		drm_plane_state_destroy(pls);
	}

	free(os);
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

static s32 drm_output_commit(drmModeAtomicReq *req,
			     struct drm_output_state *os,
			     u32 *flags)
{
	struct drm_output *output = os->output;
	s32 ret = 0;
	struct drm_head *head = to_drm_head(output->base.head);
	struct drm_plane *plane;
	struct drm_plane_state *pls;

	if (os->dpms == DPMS_ON) {
		if (output->modeset_pending) {
			output->current_mode = output->pending_mode;
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
	} else {
		if (output->modeset_pending) {
			output->current_mode = output->pending_mode;
			*flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
		}
		ret |= set_crtc_prop(req, output, CRTC_PROP_ACTIVE, 0);
		ret |= set_crtc_prop(req, output, CRTC_PROP_MODE_ID, 0);
		ret |= set_connector_prop(req, head, CONNECTOR_PROP_CRTC_ID, 0);
	}

	list_for_each_entry(pls, &os->plane_states, link) {
		plane = pls->plane;

		if (pls->fb)
			drm_fb_ref(pls->fb);

		ret |= set_plane_prop(req, plane, PLANE_PROP_FB_ID,
				      pls->fb ? pls->fb->fb_id : 0);
		ret |= set_plane_prop(req, plane, PLANE_PROP_CRTC_ID,
				      output->crtc_id);
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
		ret |= set_plane_prop(req, plane, PLANE_PROP_ZPOS,
				      pls->zpos);
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
		output->state_last = output->state_cur;
	} else {
		drm_output_state_destroy(os);
	}

	list_del(&os->link);

	if (async) {
		output->state_cur = os;
		output->page_flip_pending = true;
	}

	list_for_each_entry(pls, &os->plane_states, link) {
		plane = pls->plane;

		plane->state_cur = pls;
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

	if (async)
		flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK;

	list_for_each_entry(output, &dev->outputs, link) {
		os = drm_get_output_state(ps, output);
		ret = drm_output_commit(req, os, &flags);
		if (ret) {
			goto out;
		}
	}

	drm_debug("commit");
	ret = drmModeAtomicCommit(dev->fd, req, flags, dev);
	if (ret) {
		drm_err("[KMS] failed to commit. (%s)", strerror(errno));
		goto out;
	}

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

		if (!output_found) {
			os = drm_output_state_create(ps, output);
			os->dpms = DPMS_ON;
		}

		pls = drm_plane_state_create(os, to_drm_plane(info->plane));
		pls->fb = to_drm_fb(info->buffer);
		pls->zpos = info->zpos;
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

static void drm_output_disable(struct output *o)
{
	struct drm_output *output = to_drm_output(o);

	drm_info("disabling output...");
	output->modeset_pending = true;
	drm_info("disable output complete.");
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

	list_for_each_entry(plane, &output->planes, link) {
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

static s32 drm_output_add_output_complete_cb(struct output *o,
					     struct cb_listener *l)
{
	struct drm_output *output = to_drm_output(o);

	if (!o || !l)
		return -EINVAL;

	cb_signal_add(&output->output_complete_signal, l);
	return 0;
}

static s32 drm_output_add_page_flip_cb(struct output *o, struct cb_listener *l)
{
	struct drm_output *output = to_drm_output(o);

	if (!o || !l)
		return -EINVAL;

	cb_signal_add(&output->page_flip_signal, l);
	return 0;
}

static s32 drm_output_enable(struct output *o, struct cb_mode *mode)
{
	struct drm_output *output = to_drm_output(o);
	struct drm_mode *new_mode = to_drm_mode(mode);

	drm_info("enabling output...");
	if (mode) {
		output->pending_mode = new_mode;
		output->modeset_pending = true;
	}

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

	free(plane);
}

static struct plane *drm_plane_create(struct drm_scanout *dev,
				      struct drm_output *output, s32 index)
{
	struct drm_plane *plane = NULL;
	drmModeObjectProperties *props;
	drmModePlane *p;
	s32 i;
	u32 type;

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
		if (!strcmp(plane_type_enum[type].name, "Primary"))
			plane->base.type = PLANE_TYPE_PRIMARY;
		else if (!strcmp(plane_type_enum[type].name, "Overlay"))
			plane->base.type = PLANE_TYPE_OVERLAY;
		else if (!strcmp(plane_type_enum[type].name, "Cursor"))
			plane->base.type = PLANE_TYPE_CURSOR;
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
	s32 i;
	drmModePropertyBlobPtr blob;
	const u8 *p;

	blob_id = (u32)drm_get_prop_value(&head->props[CONNECTOR_PROP_EDID],
					  props);

	if (blob_id == (u32)(-1))
		return;

	blob = drmModeGetPropertyBlob(head->dev->fd, blob_id);
	if (!blob->data || !blob->length)
		goto out;

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
	drmModeFreePropertyBlob(blob);
}

static s32 drm_head_copy_edid(struct head *h, u8 *data, size_t *length)
{
	struct drm_head *head = to_drm_head(h);

	if (!data || !length)
		return -EINVAL;

	if (!head->edid.data || !head->edid.length)
		return -ENODEV;

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

	if (head->edid.data)
		free(head->edid.data);

	drm_prop_finish(head->props, CONNECTOR_PROP_NR);

	list_del(&head->link);

	cb_signal_fini(&head->head_changed_signal);

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
}

static void drm_output_destroy(struct output *o)
{
	struct drm_output *output = to_drm_output(o);
	struct drm_plane *plane, *next_plane;

	if (!output)
		return;

	list_del(&output->link);

	list_for_each_entry_safe(plane, next_plane, &output->planes, link) {
		drm_plane_destroy(&plane->base);
	}

	drm_clear_output_modes(output);

	drm_prop_finish(output->props, CRTC_PROP_NR);

	cb_signal_fini(&output->output_complete_signal);
	cb_signal_fini(&output->page_flip_signal);

	free(output);
}

static struct output *drm_output_create(struct drm_scanout *dev,
					s32 output_index,
					s32 primary_plane_index,
					s32 cursor_plane_index,
					s32 overlay_plane_index)
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
	INIT_LIST_HEAD(&output->planes);
	cb_signal_init(&output->output_complete_signal);
	cb_signal_init(&output->page_flip_signal);

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
		list_add_tail(&(to_drm_plane(plane))->link, &output->planes);
	}

	if (cursor_plane_index >= 0) {
		plane = drm_plane_create(dev, output, cursor_plane_index);
		if (!plane)
			goto err;
		output->cursor = to_drm_plane(plane);
		list_add_tail(&(to_drm_plane(plane))->link, &output->planes);
	}

	if (overlay_plane_index >= 0) {
		plane = drm_plane_create(dev, output, overlay_plane_index);
		if (!plane)
			goto err;
		output->overlay = to_drm_plane(plane);
		list_add_tail(&(to_drm_plane(plane))->link, &output->planes);
	}

	/* create other overlay */
	for (i = 0; i < dev->pres->count_planes; i++) {
		if (i == primary_plane_index)
			continue;
		if (i == overlay_plane_index)
			continue;
		if (i == cursor_plane_index)
			continue;
		plane = drm_plane_create(dev, output, i);
		if (!plane)
			continue;
		list_add_tail(&(to_drm_plane(plane))->link, &output->planes);
	}

	INIT_LIST_HEAD(&output->modes);

	list_add_tail(&output->link, &dev->outputs);
	drm_info("Create drm output complete.");

	return &output->base;

err:
	if (output)
		drm_output_destroy(&output->base);

	return NULL;
}

static struct cb_buffer *drm_scanout_import_buffer(struct scanout *so,
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

	fb = calloc(1, sizeof(*fb));
	if (!fb)
		goto err;

	fb->base.info = *info;
	cb_signal_init(&fb->base.destroy_signal);

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

	fb->bo = gbm_bo_import(dev->gbm, GBM_BO_IMPORT_FD, &import_data,
			       GBM_BO_USE_SCANOUT);
	if (!fb->bo) {
		drm_err("failed to import dma-buf by gbm. (%s)",
			strerror(errno));
		goto err;
	}
	fb->handles[0] = gbm_bo_get_handle(fb->bo).s32;
	if (fb->handles[0] == (u32)(-1)) {
		drm_err("failed to get dma-buf's handle.");
		goto err;
	}

	ret = drmModeAddFB2(dev->fd, info->width, info->height, fb->fourcc,
			    fb->handles, fb->base.info.strides,
			    fb->base.info.offsets, &fb->fb_id, 0);
	if (ret) {
		drm_err("failed to create drm FB2. (%s)", strerror(errno));
		goto err;
	}

	fb->dev = dev;

	fb->ref_cnt = 1;

	return &fb->base;

err:
	if (fb && fb->bo) {
		drm_debug("gbm bo destroy");
		gbm_bo_destroy(fb->bo);
		fb->bo = NULL;
	}

	if (fb)
		free(fb);
	return NULL;
}

static void drm_scanout_pipeline_destroy(struct scanout *so, struct output *o)
{
	struct head *h;
	struct drm_output *output = to_drm_output(o);

	if (!so)
		return;

	if (!o)
		return;

	drm_info("Destroy pipeline CRTC_ID[%u] ...", output->crtc_id);
	h = o->head;

	drm_output_destroy(o);
	drm_head_destroy(h);
	drm_info("Destroy pipeline CRTC_ID[%u] complete.", output->crtc_id);
}

static void drm_head_update_modes(struct drm_head *head)
{
	struct drm_output *output;
	struct drm_mode *mode, *new_mode;
	u32 vrefresh = 60;
	s32 i;
	drmModeConnectorPtr conn;

	output = to_drm_output(head->base.output);

	drm_clear_output_modes(output);

	conn = head->connector;
	if (!conn)
		return;

	for (i = 0; i < conn->count_modes; i++) {
		if (conn->modes[i].clock > 350000)
			continue;
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
	}

	new_mode = NULL;
	list_for_each_entry(mode, &output->modes, link) {
		if (mode->base.vrefresh > vrefresh &&
		    mode->base.vrefresh > 110) {
			vrefresh = mode->base.vrefresh;
			new_mode = mode;
		}
	}

	if (!new_mode) {
		list_for_each_entry(mode, &output->modes, link) {
			if (mode->base.preferred) {
				new_mode = mode;
				break;
			}
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
	output->modeset_pending = true;
	drm_info("new_mode: %ux%u@%u %.2fMHz", new_mode->base.width,
		 new_mode->base.height, new_mode->base.vrefresh,
		 new_mode->base.pixel_freq / 1000.0f);
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
				   pipeline_cfg->cursor_plane_index,
				   pipeline_cfg->overlay_plane_index);
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
	output->enumerate_mode = drm_output_enumerate_mode;
	output->enumerate_plane = drm_output_enumerate_plane;
	output->add_output_complete_notify = drm_output_add_output_complete_cb;

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

	free(dev);
	drm_info("Destroy scanout device complete.");
}

static void drm_output_complete(struct drm_output *output, bool init,
				u32 sec, u32 usec)
{
	drm_output_state_destroy(output->state_last);
	cb_signal_emit(&output->page_flip_signal, &output->base);
	if (output->state_last) {
		output->state_last = NULL;
		if (!init) {
			cb_signal_emit(&output->output_complete_signal,
					&output->base);
		}
	}
}

static void page_flip_handler(s32 fd, u32 crtc_id, u32 frame, u32 sec, u32 usec,
			      void *data)
{
	struct drm_scanout *dev = data;
	struct drm_output *output;

	drm_debug("CRTC_ID: %u frame: %u (%u, %u)", crtc_id, frame, sec, usec);
	list_for_each_entry(output, &dev->outputs, link) {
		output->page_flip_pending = false;
		drm_output_complete(output, false, sec, usec);
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

	dev->loop = loop;
	dev->base.destroy = drm_scanout_destroy;
	dev->base.set_dbg_level = drm_scanout_set_dbg_level;

	dev->fd = open(dev_path, O_RDWR | O_CLOEXEC, 0644);
	if (!dev->fd)
		goto err;

	dev->gbm = gbm_create_device(dev->fd);

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
	dev->base.import_buffer = drm_scanout_import_buffer;
	dev->base.scanout_data_alloc = drm_scanout_data_alloc;
	dev->base.do_scanout = drm_do_scanout;
	dev->base.fill_scanout_data = drm_scanout_data_fill;

	drm_info("Create scanout device complete.");
	return &dev->base;

err:
	if (dev) {
		dev->base.destroy(&dev->base);
	}
	return NULL;
}

/* commit info helper functions */
struct scanout_commit_info *scanout_commit_info_alloc(void)
{
	struct scanout_commit_info *info = calloc(1, sizeof(*info));

	if (!info)
		return NULL;
	INIT_LIST_HEAD(&info->fb_commits);

	return info;
}

void scanout_commit_add_fb_info(struct scanout_commit_info *commit,
				struct cb_buffer *buffer,
				struct output *output,
				struct plane *plane,
				struct cb_rect *src,
				struct cb_rect *dst,
				s32 zpos)
{
	struct fb_info *info;

	if (!commit)
		return;

	info = calloc(1, sizeof(*info));
	if (!info)
		return;

	info->buffer = buffer;
	info->output = output;
	info->plane = plane;
	info->src = *src;
	info->dst = *dst;
	info->zpos = zpos;

	list_add_tail(&info->link, &commit->fb_commits);
}

void scanout_commit_info_free(struct scanout_commit_info *commit)
{
	struct fb_info *info, *next_info;

	if (!commit)
		return;

	list_for_each_entry_safe(info, next_info, &commit->fb_commits, link) {
		list_del(&info->link);
		free(info);
	}

	free(commit);
}

