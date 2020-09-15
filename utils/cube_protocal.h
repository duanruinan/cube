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
#ifndef CUBE_PROTOCAL_H
#define CUBE_PROTOCAL_H

#include <stdbool.h>
#include <cube_utils.h>
#include <cube_shm.h>

#pragma pack(push)
#pragma pack(8)

#define CB_CMD_OFFSET 0

enum cb_cmd_shift {
	/* 0: server send connection id to client */
	CB_CMD_LINK_ID_ACK_SHIFT = CB_CMD_OFFSET,
	/* 1: client sends a surface's creation request */
	CB_CMD_CREATE_SURFACE_SHIFT,
	/* 2: server feeds back the result */
	CB_CMD_CREATE_SURFACE_ACK_SHIFT,
	/* 3: client sends a view's creation request */
	CB_CMD_CREATE_VIEW_SHIFT,
	/* 4: server feeds back the result */
	CB_CMD_CREATE_VIEW_ACK_SHIFT,

	/*
	 * 5: client sends a share memory/DMA-BUF buffer object creation request
	 */
	CB_CMD_CREATE_BO_SHIFT,
	/* 6: server feeds back the result of BO creation */
	CB_CMD_CREATE_BO_ACK_SHIFT,
	/* 7: destroy buffer */
	CB_CMD_DESTROY_BO_SHIFT,
	/* 8: destroy buffer ack */
	CB_CMD_DESTROY_BO_ACK_SHIFT,

	/*
	 * 9: Client commit the new state of surface to server.
	 * Commit: Changes of buffer object/view/surface
	 *
	 * Client should not render into the BO which has been commited to
	 * server, until received CB_CMD_BO_COMPLETE from server.
	 *
	 * Notice: Client should invoke glFinish before commit
	 *         to ensure the completion of the previouse rendering work.
	 *
	 *         Server should schedule a new repaint request.
	 */
	CB_CMD_COMMIT_SHIFT,
	/* 10: server feeds back the result of BO's attaching operation */
	CB_CMD_COMMIT_ACK_SHIFT,
	/* 11: server notify client the BO flipped event (commited to kernel).*/
	CB_CMD_BO_FLIPPED_SHIFT,
	/* 12: server notify client the BO is no longer in use */
	CB_CMD_BO_COMPLETE_SHIFT,

	/* 13: raw mouse & kbd event report */
	CB_CMD_RAW_INPUT_EVT_SHIFT,

	/* <----------------- DESTROYING STAGE ------------------> */
	/*
	 * 14: Client requests to destroy surface
	 * Server will destroy all resources of the given surface.
	 */
	CB_CMD_DESTROY_SHIFT,
	/* 15: server feeds back the result of the surface's destruction */
	CB_CMD_DESTROY_ACK_SHIFT,

	/* <---------------- cube setting utils ---------------> */
	/* 16: shell */
	CB_CMD_SHELL_SHIFT,
	/* 17: plug in/out */
	CB_CMD_HPD_SHIFT,
	/* 18: */
	CB_CMD_LAST_SHIFT,
};

enum cb_tag {
	CB_TAG_RAW_INPUT = 0,
	CB_TAG_WIN,
	CB_TAG_MAP, /* array of u32 */
	CB_TAG_RESULT, /* for all ACK CMDs. u64 */
	CB_TAG_CREATE_SURFACE, /* cb_surface_info */
	CB_TAG_CREATE_VIEW, /* cb_view_info */
	CB_TAG_CREATE_BO, /* cb_buf_info */
	CB_TAG_COMMIT_INFO, /* cb_commit_info */
	CB_TAG_SHELL, /* cb_shell_info */
	CB_TAG_DESTROY,
};

struct cb_tlv {
	enum cb_tag tag;
	u32 length; /* payload's size */
	u8 payload[0];
};

/*
 * cmd head: flag:4 bytes                       MUST  (Offs 0)
 * cmd head: TAG: 4 bytes WIN/INPUT TAG         MUST  (Offs 4)
 * total length: 4 bytes                        MUST  (Offs 8)
 * payload {                                    MUST
 *     TAG: 4 bytes TAG_MAP                     MUST
 *     length: 4 bytes ---- N                   MUST
 *     map {                                    MUST
 *         offset[0],       offset of TLV(CB_CMD_XXX_SHIFT - CB_CMD_OFFSET)
 *         offset[1], ...
 *         offset[n-1],
 *     };
 *     TAG[0]: 4 bytes TAG_XXX                  OPTS  (Offs offset[0])
 *     length[0]: 4 bytes
 *     payload[0]: length(length[0])
 *     TAG[1]: 4 bytes TAG_XXX                  OPTS  (Offs offset[1])
 *     length[1]: 4 bytes
 *     payload[1]: length(length[1])
 *     ...
 *     TAG[n-1]: 4 bytes TAG_XXX                OPTS  (Offs offset[n-1])
 *     length[n-1]: 4 bytes
 *     payload[n-1]: length(length[n-1])
 * };
 */

#define CB_CMD_MAP_SIZE (sizeof(struct cb_tlv) \
			+ (CB_CMD_LAST_SHIFT - CB_CMD_OFFSET) * sizeof(u32))

struct cb_surface_info {
	u64 surface_id;
	s32 is_opaque;
	struct cb_rect damage;
	u32 width, height;
	struct cb_rect opaque;
};

struct cb_view_info {
	u64 view_id;
	/*
	 * Cannot be composed with other views except the top view
	 */
	bool full_screen;
	bool top_level;
	struct cb_rect area;
	float alpha;
	u32 output_mask;
	u32 primary_output;
};

enum cb_pix_fmt {
	CB_PIX_FMT_UNKNOWN = 0,
	/**
	 * 32-bit ARGB format. B [7:0]  G [15:8]  R [23:16] A [31:24]
	 */
	CB_PIX_FMT_ARGB8888,

	/**
	 * 32-bit XRGB format. B [7:0]  G [15:8]  R [23:16] X [31:24]
	 */
	CB_PIX_FMT_XRGB8888,

	/**
	 * 24-bit RGB 888 format. B [7:0]  G [15:8]  R [23:16]
	 */
	CB_PIX_FMT_RGB888,

	/**
	 * 16-bit RGB 565 format. B [4:0]  G [10:5]  R [15:11]
	 */
	CB_PIX_FMT_RGB565,

	/**
	 * 2 plane YCbCr format, 2x2 subsampled Cb:Cr plane
	 */
	CB_PIX_FMT_NV12,

	/**
	 * 2 plane YCbCr format, 2x1 subsampled Cb:Cr plane
	 */
	CB_PIX_FMT_NV16,

	/**
	 * 2 plane YCbCr format, non-subsampled Cb and Cr planes
	 */
	CB_PIX_FMT_NV24,

	/**
	 * packed YCbCr format, Y0Cb0 Y1Cr0 Y2Cb2 Y3Cr2
	 */
	CB_PIX_FMT_YUYV,

	/**
	 * 3 plane YCbCr format, 2x2 subsampled Cb and Cr planes
	 */
	CB_PIX_FMT_YUV420,

	/**
	 * 3 plane YCbCr format, 2x1 subsampled Cb and Cr planes
	 */
	CB_PIX_FMT_YUV422,

	/**
	 * 3 plane YCbCr format, non-subsampled Cb and Cr planes
	 */
	CB_PIX_FMT_YUV444,
};

#define CB_BUFFER_NAME_LEN 32

enum cb_buffer_type {
	CB_BUF_TYPE_UNKNOWN = 0,
	CB_BUF_TYPE_SHM,
	CB_BUF_TYPE_DMA,
};

struct cb_buffer_info {
	enum cb_pix_fmt pix_fmt;
	enum cb_buffer_type type;
	struct cb_shm shm;
	u32 width, height;
	u32 strides[4];
	u32 offsets[4];
	s32 fd[4];
	size_t sizes[4];
	void *maps[4];
	s32 planes;
	u64 surface_id;
};

struct cb_commit_info {
	u64 bo_id;
	struct cb_rect bo_damage;

	s32 shown; /* 0: hide / 1: show */

	s32 view_x, view_y;
	s32 view_hot_x, view_hot_y;
	u32 view_width, view_height;

	/* 0: z order no change / 1: bring to top / -1: falling down */
	s32 delta_z;
};

enum cb_shell_cmd {
	CB_SHELL_DEBUG_SETTING,
	CB_SHELL_CANVAS_LAYOUT_SETTING,
	CB_SHELL_CANVAS_LAYOUT_QUERY,
};

struct cb_canvas_layout {
	u32 count_heads;
	u32 mode;
	struct cb_rect desktops[8];
};

struct cb_debug_flags {
	u8 common_flag;
	u8 compositor_flag;
	u8 drm_flag;
	u8 gbm_flag;
	u8 ps_flag;
	u8 timer_flag;
	u8 gles_flag;
	u8 egl_flag;
};

struct cb_shell_info {
	enum cb_shell_cmd cmd;
	union {
		struct cb_debug_flags dbg_flags;
		struct cb_canvas_layout layout;
	} value;
};

struct cb_raw_input_event {
	u16 type;
	u16 code;
	union {
		u32 value;
		struct {
			u16 x;
			u16 y;
			s16 dx;
			s16 dy;
		} pos;
	} v;
};

u8 *cb_server_create_linkup_cmd(u64 link_id, u32 *n);
u8 *cb_dup_linkup_cmd(u8 *dst, u8 *src, u32 n, u64 link_id);
u64 cb_client_parse_link_id(u8 *data);
u8 *cb_client_create_surface_cmd(struct cb_surface_info *s, u32 *n);
u8 *cb_dup_create_surface_cmd(u8 *dst, u8 *src, u32 n,
			       struct cb_surface_info *s);
s32 cb_server_parse_create_surface_cmd(u8 *data, struct cb_surface_info *s);
u8 *cb_server_create_surface_id_cmd(u64 surface_id, u32 *n);
u8 *cb_dup_surface_id_cmd(u8 *dst, u8 *src, u32 n, u64 surface_id);
u64 cb_client_parse_surface_id(u8 *data);
u8 *cb_client_create_view_cmd(struct cb_view_info *v, u32 *n);
u8 *cb_dup_create_view_cmd(u8 *dst, u8 *src, u32 n, struct cb_view_info *v);
s32 cb_server_parse_create_view_cmd(u8 *data, struct cb_view_info *v);
u8 *cb_server_create_view_id_cmd(u64 view_id, u32 *n);
u8 *cb_dup_view_id_cmd(u8 *dst, u8 *src, u32 n, u64 view_id);
u64 cb_client_parse_view_id(u8 *data);
u8 *cb_client_create_bo_cmd(struct cb_buffer_info *b, u32 *n);
u8 *cb_dup_create_bo_cmd(u8 *dst, u8 *src, u32 n, struct cb_buffer_info *b);
s32 cb_server_parse_create_bo_cmd(u8 *data, struct cb_buffer_info *b);
u8 *cb_server_create_bo_id_cmd(u64 bo_id, u32 *n);
u8 *cb_dup_bo_id_cmd(u8 *dst, u8 *src, u32 n, u64 bo_id);
u64 cb_client_parse_bo_id(u8 *data);
u8 *cb_client_create_commit_req_cmd(struct cb_commit_info *c, u32 *n);
u8 *cb_dup_commit_req_cmd(u8 *dst, u8 *src, u32 n, struct cb_commit_info *c);
s32 cb_server_parse_commit_req_cmd(u8 *data, struct cb_commit_info *c);
u8 *cb_server_create_commit_ack_cmd(u64 ret, u32 *n);
u8 *cb_dup_commit_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret);
u64 cb_client_parse_commit_ack_cmd(u8 *data);
u8 *cb_server_create_bo_flipped_cmd(u64 ret, u32 *n);
u8 *cb_dup_bo_flipped_cmd(u8 *dst, u8 *src, u32 n, u64 ret);
u64 cb_client_parse_bo_flipped_cmd(u8 *data);
u8 *cb_server_create_bo_complete_cmd(u64 ret, u32 *n);
u8 *cb_dup_bo_complete_cmd(u8 *dst, u8 *src, u32 n, u64 ret);
u64 cb_client_parse_bo_complete_cmd(u8 *data);
u8 *cb_create_shell_cmd(struct cb_shell_info *s, u32 *n);
u8 *cb_dup_shell_cmd(u8 *dst, u8 *src, u32 n, struct cb_shell_info *s);
s32 cb_parse_shell_cmd(u8 *data, struct cb_shell_info *s);
u8 *cb_client_create_destroy_cmd(u64 link_id, u32 *n);
u8 *cb_dup_destroy_cmd(u8 *dst, u8 *src, u32 n, u64 link_id);
u64 cb_server_parse_destroy_cmd(u8 *data);
u8 *cb_server_create_destroy_ack_cmd(u64 ret, u32 *n);
u8 *cb_dup_destroy_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret);
u64 cb_client_parse_destroy_ack_cmd(u8 *data);
u8 *cb_server_create_raw_input_evt_cmd(struct cb_raw_input_event *evts,
				       u32 count_evts, u32 *n);
u8 *cb_server_fill_raw_input_evt_cmd(u8 *dst, struct cb_raw_input_event *evts,
				     u32 count_evts, u32 *n, u32 max_size);
struct cb_raw_input_event *cb_client_parse_raw_input_evt_cmd(u8 *data,
							     u32 *count_evts);
u8 *cb_client_destroy_bo_cmd(u64 bo_id, u32 *n);
u8 *cb_dup_destroy_bo_cmd(u8 *dst, u8 *src, u32 n, u64 bo_id);
u64 cb_server_parse_destroy_bo_cmd(u8 *data);

#define set_hpd_info(pinfo, index, on) do { \
	*(pinfo) |= (1 << (index)); \
	if ((on)) { \
		*(pinfo) |= (1 << ((index) + 8)); \
	} else { \
		*(pinfo) &= ~(1 << ((index) + 8)); \
	} \
} while (0);

#define parse_hpd_info(info, index, pavail, pon) do { \
	if (!((info) & (1 << (index)))) { \
		*(pavail) = 0; \
	} else { \
		*(pavail) = 1; \
		if ((info) & (1 << ((index) + 8))) { \
			*(pon) = 1; \
		} else { \
			*(pon) = 0; \
		} \
	} \
} while (0);

u8 *cb_server_create_hpd_cmd(u64 hpd_info, u32 *n);
u8 *cb_dup_hpd_cmd(u8 *dst, u8 *src, u32 n, u64 hpd_info);
u64 cb_client_parse_hpd_cmd(u8 *data);
void cb_cmd_dump(u8 *data);

/*
 * Raw input command
 * 
 */
enum raw_input_cmd_type {
	INPUT_CMD_TYPE_UNKNOWN = 0,
	INPUT_CMD_TYPE_SET_CURSOR,
	INPUT_CMD_TYPE_SET_CURSOR_RANGE,
	INPUT_CMD_TYPE_SET_CURSOR_POS,
};

#define MAX_DESKTOP_NR 8
struct raw_input_cmd {
	enum raw_input_cmd_type type;
	union {
		struct {
			u32 data[64*64];
			s32 hot_x, hot_y;
			u32 w, h;
		} cursor;
		struct {
			struct cb_rect global_area[MAX_DESKTOP_NR];
			s32 count_rects;
			s32 map[MAX_DESKTOP_NR];
		} range;
	} c;
};

#pragma pack(pop)

#endif

