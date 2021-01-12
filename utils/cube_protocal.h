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

#ifdef __cplusplus
extern "C" {
#endif

#define CB_SERVER_NAME_PREFIX "/tmp"
#define LOG_SERVER_NAME_PREFIX "/tmp"
#define LOG_SERVER_SOCK_NAME "cube_log_server"
#define SERVER_NAME "cube_server"

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

	/*
	 * 8: Client commit the new state of surface to server.
	 * Commit: Changes of buffer object/view/surface
	 *
	 * For DMA-BUF direct show surface:
	 *     Client should not change the BO's content which has been commited
	 *     to server, until received CB_CMD_BO_COMPLETE from server.
	 * For renderable surface: (include DMA-BUF as external texture)
	 *     Client should not render into the BO's content which has been
	 *     commited to server, until received CB_CMD_BO_FLIPPED from server.
	 * All commit operations must be syncronized with vblank signal. It
	 *     means that the later commit must not be made until the
	 *     CB_CMD_BO_FLIPPED message of the former commit is received.
	 * If program commit two bos to server regardless of vblank, the later
	 *     one is enqueued into kernel buffer.
	 * If program change DMA-BUF bo's content regardless of bo complete
	 *     message, a video tearing must occured when the former bo is
	 *     used as DMA transfer's source data.
	 *
	 * Notice: Client should invoke glFinish before commit
	 *         to ensure the completion of the previouse rendering work.
	 *
	 *         Server should schedule a new repaint request.
	 */
	CB_CMD_COMMIT_SHIFT,
	CB_CMD_AF_COMMIT_SHIFT,
	/* 9: server feeds back the result of BO's attaching operation */
	CB_CMD_COMMIT_ACK_SHIFT,
	/* 10: server notify client the BO flipped event (commited to kernel).*/
	CB_CMD_BO_FLIPPED_SHIFT,
	/* 11: server notify client the BO is no longer in use */
	CB_CMD_BO_COMPLETE_SHIFT,

	/* <----------------- DESTROYING STAGE ------------------> */
	/*
	 * 15: Client requests to destroy surface
	 * Server will destroy all resources of the given surface.
	 */
	CB_CMD_DESTROY_SHIFT,
	/* 16: server feeds back the result of the surface's destruction */
	CB_CMD_DESTROY_ACK_SHIFT,

	/* <---------------- cube setting utils ---------------> */
	/* 17: shell */
	CB_CMD_SHELL_SHIFT,
	/* 18: plug in/out */
	CB_CMD_HPD_SHIFT,

	/* 19: MC cmd */
	CB_CMD_MC_COMMIT_SHIFT,

	/* 20: server feeds back the result of mc command */
	CB_CMD_MC_COMMIT_ACK_SHIFT,

	/* 21: */
	CB_CMD_LAST_SHIFT,
};

/*
 * E-EDID descriptor
 *     avail:   available or not.
 *     pipe:    which connector's E-EDID.
 *     edid_sz: E-EDID blob's size, typically is 128 or 256.
 *     edid:    blob data.
 */
struct edid_desc {
	bool avail;
	u64 pipe;
	size_t edid_sz;
	u8 edid[0];
};

enum cb_tag {
	CB_TAG_UNKNOWN = 0,
	CB_TAG_RAW_INPUT,
	CB_TAG_RAW_TOUCH,
	CB_TAG_RAW_INPUT_EN,
	CB_TAG_SET_KBD_LED,
	CB_TAG_GET_KBD_LED_STATUS,
	CB_TAG_GET_KBD_LED_STATUS_ACK,
	CB_TAG_SET_CAPABILITY,
	CB_TAG_GET_EDID,
	CB_TAG_GET_EDID_ACK,
	CB_TAG_WIN,
	CB_TAG_MAP, /* array of u32 */
	CB_TAG_RESULT, /* for all ACK CMDs. u64 */
	CB_TAG_CREATE_SURFACE, /* cb_surface_info */
	CB_TAG_CREATE_VIEW, /* cb_view_info */
	CB_TAG_CREATE_BO, /* cb_buf_info */
	CB_TAG_COMMIT_INFO, /* cb_commit_info */
	CB_TAG_AF_COMMIT_INFO, /* cb_af_commit_info */
	CB_TAG_SHELL, /* cb_shell_info */
	CB_TAG_DESTROY, /* destroy all */
	CB_TAG_VIEW_FOCUS_CHG, /* view focus on / lost */
	CB_TAG_MC_COMMIT_INFO, /* mouse cursor */
	CB_TAG_GUI_INPUT, /* input msg for GUI */
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
	bool is_opaque;
	struct cb_rect damage;
	u32 width, height;
	struct cb_rect opaque;
};

struct cb_view_info {
	u64 view_id;
	/* for float view: server do not send focus on / lost message */
	bool float_view;
	/* for root view: server do not change zpos */
	bool root_view;
	u32 output_mask;
	u32 primary_output;

	u64 surface_id;
	struct cb_rect area;
	float alpha;
	s32 zpos;
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

enum cb_buffer_type {
	CB_BUF_TYPE_UNKNOWN = 0,
	CB_BUF_TYPE_SHM,
	CB_BUF_TYPE_DMA,
	CB_BUF_TYPE_SURFACE,
};

struct cb_buffer_info {
	enum cb_pix_fmt pix_fmt;
	enum cb_buffer_type type;
	u32 width, height;
	u32 strides[4];
	u32 offsets[4];
	s32 fd[4];
	size_t sizes[4];
	void *maps[4];
	s32 planes;
	bool composed;  /* for DMA-BUF used (to be composed or not) */
};

#define DAMAGE_AREA_MAX_NR 2048

/* atomic flush commit info */
struct cb_af_commit_info {
	u64 bo_id;
	u64 surface_id;
	struct cb_rect bo_opaque;

	s32 shown; /* 0: hide / 1: show */

	s32 view_x, view_y;
	u32 view_width, view_height;
	s32 pipe_locked;

	/* 0: z order no change / 1: bring to top / -1: falling down */
	s32 delta_z;

	s32 count_damages;
	struct cb_rect damages[0];
};

struct cb_commit_info {
	u64 bo_id;
	u64 surface_id;
	struct cb_rect bo_damage;
	struct cb_rect bo_opaque;

	s32 shown; /* 0: hide / 1: show */

	s32 view_x, view_y;
	u32 view_width, view_height;
	s32 pipe_locked;

	/* 0: z order no change / 1: bring to top / -1: falling down */
	s32 delta_z;
};

enum cb_shell_cmd {
	CB_SHELL_DEBUG_SETTING,
	CB_SHELL_CANVAS_LAYOUT_SETTING,
	CB_SHELL_CANVAS_LAYOUT_QUERY,
	CB_SHELL_CANVAS_LAYOUT_CHANGED_NOTIFY,
	CB_SHELL_OUTPUT_VIDEO_TIMING_ENUMERATE,
	CB_SHELL_OUTPUT_VIDEO_TIMING_CREAT,
};

#define CB_CONNECTOR_NAME_MAX_LEN 31
#define CB_MONITOR_NAME_MAX_LEN 31

struct output_config {
	s32 pipe;
	struct cb_rect desktop_rc;
	struct cb_rect input_rc;
	void *mode_handle;
	void *custom_mode_handle;
	char monitor_name[CB_MONITOR_NAME_MAX_LEN];
	char connector_name[CB_CONNECTOR_NAME_MAX_LEN];
	u16 width_preferred, height_preferred;
	u32 vrefresh_preferred, pixel_freq_preferred;
};

struct cb_canvas_layout {
	u32 count_heads;
	struct output_config cfg[8];
};

struct mode_info {
	u32 clock;
	u16 width;
	u16 hsync_start;
	u16 hsync_end;
	u16 htotal;
	u16 hskew;
	u16 height;
	u16 vsync_start;
	u16 vsync_end;
	u16 vtotal;
	u16 vscan;
	u32 vrefresh;
	bool interlaced;
	bool pos_hsync;
	bool pos_vsync;
	bool preferred;
};

/* video timing filter pattern */
enum cb_mode_filter_mode {
	CB_MODE_FILTER_MODE_SIZE_OR_CLOCK = 1,
	CB_MODE_FILTER_MODE_SIZE_AND_CLOCK,
};

struct cb_mode_filter {
	enum cb_mode_filter_mode mode;
	u32 min_width, max_width;
	u32 min_height, max_height;
	u32 min_clock, max_clock;
};

struct output_timings_enum {
	s32 pipe;
	void *handle_last;
	void *handle_cur;
	bool filter_en;
	struct cb_mode_filter enum_filter;
};

struct cb_debug_flags {
	u8 clia_flag;
	u8 comp_flag;
	u8 sc_flag;
	u8 rd_flag;
	u8 client_flag;
	u8 touch_flag;
	u8 joystick_flag;
};

struct cb_shell_info {
	enum cb_shell_cmd cmd;
	struct {
		struct cb_debug_flags dbg_flags;
		struct cb_canvas_layout layout;
		struct output_timings_enum ote;
		struct mode_info mode;
		s32 modeset_pipe;
		void *new_mode_handle;
	} value;
};

#pragma pack(1)
struct slot_info {
	u8 slot_id:5;
	u8 pressed:1;
	u8 pos_x_changed:1;
	u8 pos_y_changed:1;
	u8 reserved;
	u16 pos[0];
};

struct touch_event {
	u16 count_slots:5; /* slot number - 1 */
	u16 payload_sz:11; /* payload size */
	u8 payload[0];
};

#pragma pack()

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

#define VK_LBUTTON	0x01
#define VK_RBUTTON	0x02
#define VK_CANCEL	0x03
#define VK_MBUTTON	0x04
#define VK_BACK		0x08
#define VK_TAB		0x09
#define VK_CLEAR	0x0C
#define VK_RETURN	0x0D
#define VK_SHIFT	0x10
#define VK_CONTROL	0x11
#define VK_MENU		0x12
#define VK_PAUSE	0x13
#define VK_CAPITAL	0x14
#define VK_ESCAPE	0x1B
#define VK_SPACE	0x20
#define VK_PRIOR	0x21
#define VK_NEXT		0x22
#define VK_END		0x23
#define VK_HOME		0x24
#define VK_LEFT		0x25
#define VK_UP		0x26
#define VK_RIGHT	0x27
#define VK_DOWN		0x28
#define VK_Select	0x29
#define VK_PRINT	0x2A
#define VK_EXECUTE	0x2B
#define VK_SNAPSHOT	0x2C
#define VK_Insert	0x2D
#define VK_Delete	0x2E
#define VK_HELP		0x2F
#define VK_0		0x30
#define VK_1		0x31
#define VK_2		0x32
#define VK_3		0x33
#define VK_4		0x34
#define VK_5		0x35
#define VK_6		0x36
#define VK_7		0x37
#define VK_8		0x38
#define VK_9		0x39
#define VK_A		0x41
#define VK_B		0x42
#define VK_C		0x43
#define VK_D		0x44
#define VK_E		0x45
#define VK_F		0x46
#define VK_G		0x47
#define VK_H		0x48
#define VK_I		0x49
#define VK_J		0x4A
#define VK_K		0x4B
#define VK_L		0x4C
#define VK_M		0x4D
#define VK_N		0x4E
#define VK_O		0x4F
#define VK_P		0x50
#define VK_Q		0x51
#define VK_R		0x52
#define VK_S		0x53
#define VK_T		0x54
#define VK_U		0x55
#define VK_V		0x56
#define VK_W		0x57
#define VK_X		0x58
#define VK_Y		0x59
#define VK_Z		0x5A
#define VK_LWIN         0x5B
#define VK_RWIN         0x5C
#define VK_APPS         0x5D
#define VK_NUMPAD0	0x60
#define VK_NUMPAD1	0x61
#define VK_NUMPAD2	0x62
#define VK_NUMPAD3	0x63
#define VK_NUMPAD4	0x64
#define VK_NUMPAD5	0x65
#define VK_NUMPAD6	0x66
#define VK_NUMPAD7	0x67
#define VK_NUMPAD8	0x68
#define VK_NUMPAD9	0x69
#define VK_MULTIPLY	0x6A
#define VK_ADD		0x6B
#define VK_SEPARATOR	0x6C
#define VK_SUBTRACT	0x6D
#define VK_DECIMAL	0x6E
#define VK_DIVIDE	0x6F
#define VK_F1		0x70
#define VK_F2		0x71
#define VK_F3		0x72
#define VK_F4		0x73
#define VK_F5		0x74
#define VK_F6		0x75
#define VK_F7		0x76
#define VK_F8		0x77
#define VK_F9		0x78
#define VK_F10		0x79
#define VK_F11		0x7A
#define VK_F12		0x7B
#define VK_NUMLOCK	0x90
#define VK_SCROLL	0x91
#define VK_LSHIFT	0xA0
#define VK_RSHIFT	0xA1
#define VK_LCONTROL	0xA2
#define VK_RCONTROL	0xA3
#define VK_LMENU	0xA4
#define VK_RMENU	0xA5
#define VK_OEM_PLUS	187
#define VK_OEM_COMMA	188
#define VK_OEM_MINUS	189
#define VK_OEM_PERIOD	190

/* server: link ID */
u8 *cb_server_create_linkup_cmd(u64 link_id, u32 *n);
u8 *cb_dup_linkup_cmd(u8 *dst, u8 *src, u32 n, u64 link_id);
/* client: parse link ID */
u64 cb_client_parse_link_id(u8 *data);

#define CB_CLIENT_CAP_NOTIFY_LAYOUT (1 << 0)
#define CB_CLIENT_CAP_RAW_INPUT (1 << 1)
#define CB_CLIENT_CAP_HPD (1 << 2)
#define CB_CLIENT_CAP_MC (1 << 3)
#define CB_CLIENT_CAP_INPUT (1 << 4)

/* client: create set capability command */
u8 *cb_client_create_set_cap_cmd(u64 cap, u32 *n);
u8 *cb_dup_set_cap_cmd(u8 *dst, u8 *src, u32 n, u64 cap);
/* server: parse set capability command */
s32 cb_server_parse_set_cap_cmd(u8 *data, u64 *cap);

/* client: surface create request */
u8 *cb_client_create_surface_cmd(struct cb_surface_info *s, u32 *n);
u8 *cb_dup_create_surface_cmd(u8 *dst, u8 *src, u32 n,
			      struct cb_surface_info *s);
/* server: parse surface create request */
s32 cb_server_parse_create_surface_cmd(u8 *data, struct cb_surface_info *s);

/* server: surface create ack */
u8 *cb_server_create_surface_id_cmd(u64 surface_id, u32 *n);
u8 *cb_dup_surface_id_cmd(u8 *dst, u8 *src, u32 n, u64 surface_id);
/* client: parse surface create ack */
u64 cb_client_parse_surface_id(u8 *data);

/* client: view create request */
u8 *cb_client_create_view_cmd(struct cb_view_info *v, u32 *n);
u8 *cb_dup_create_view_cmd(u8 *dst, u8 *src, u32 n, struct cb_view_info *v);
/* server: parse view create request */
s32 cb_server_parse_create_view_cmd(u8 *data, struct cb_view_info *v);

/* server: view create ack */
u8 *cb_server_create_view_id_cmd(u64 view_id, u32 *n);
u8 *cb_dup_view_id_cmd(u8 *dst, u8 *src, u32 n, u64 view_id);
/* client: parse view create ack */
u64 cb_client_parse_view_id(u8 *data);

/* client: bo create request */
u8 *cb_client_create_bo_cmd(struct cb_buffer_info *b, u32 *n);
u8 *cb_dup_create_bo_cmd(u8 *dst, u8 *src, u32 n, struct cb_buffer_info *b);
/* server: parse bo create request */
s32 cb_server_parse_create_bo_cmd(u8 *data, struct cb_buffer_info *b);

/* server: bo create ack */
u8 *cb_server_create_bo_id_cmd(u64 bo_id, u32 *n);
u8 *cb_dup_bo_id_cmd(u8 *dst, u8 *src, u32 n, u64 bo_id);
/* client: parse bo create ack */
u64 cb_client_parse_bo_id(u8 *data);

/* client: bo destroy request */
u8 *cb_client_destroy_bo_cmd(u64 bo_id, u32 *n);
u8 *cb_dup_destroy_bo_cmd(u8 *dst, u8 *src, u32 n, u64 bo_id);
/* server: parse bo destroy request */
u64 cb_server_parse_destroy_bo_cmd(u8 *data);

/* client: bo commit request */
u8 *cb_client_create_commit_req_cmd(struct cb_commit_info *c, u32 *n);
u8 *cb_dup_commit_req_cmd(u8 *dst, u8 *src, u32 n, struct cb_commit_info *c);
/* server: parse bo commit request */
s32 cb_server_parse_commit_req_cmd(u8 *data, struct cb_commit_info *c);

/*
 * Create buffer for af (atomic flush) commit (both for client and server)
 * Atomic flush commit info is not fixed size, because the number of damage area
 * will be changed during client's rendering process.
 * 
 * So alloc buffer big enough to accommodate 2048 damage area (rects).
 * Client should free it when it is not used (calling "free").
 * After the buffer is allocated, client should get the "cb_af_commit_info"
 * structure from buffer by calling cb_client_get_af_commit_info_from_buffer.
 * Client fills the structure ("damages" is the rect list), then generates
 * tx command by calling "cb_gen_af_commit_cmd".
 * Client send tx command at last.
 */
u8 *cb_client_create_af_commit_buffer(void);
/* get cb_af_commit_info from buffer (for client) */
struct cb_af_commit_info *cb_client_get_af_commit_info_from_buffer(u8 *buffer);
/* generate af commit command (for client) */
s32 cb_gen_af_commit_cmd(u8 *buffer, u32 *n);
/* parse cb_af_commit_info from buffer (for server) */
struct cb_af_commit_info *cb_server_parse_af_commit_req_cmd(u8 *data);

/* server: bo commit ack */
u8 *cb_server_create_commit_ack_cmd(u64 ret, u64 surface_id, u32 *n);
u8 *cb_dup_commit_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret, u64 surface_id);
/* client: parse bo commit ack */
u64 cb_client_parse_commit_ack_cmd(u8 *data, u64 *surface_id);

#define COMMIT_REPLACE 0
#define COMMIT_FAILED ((u64)(-1ULL))

/* server: bo flipped notify */
u8 *cb_server_create_bo_flipped_cmd(u64 ret, u64 surface_id, u32 *n);
u8 *cb_dup_bo_flipped_cmd(u8 *dst, u8 *src, u32 n, u64 ret, u64 surface_id);
/* client: parse bo flipped notify */
u64 cb_client_parse_bo_flipped_cmd(u8 *data, u64 *surface_id);

/* server: bo complete notify */
u8 *cb_server_create_bo_complete_cmd(u64 ret, u64 surface_id, u32 *n);
u8 *cb_dup_bo_complete_cmd(u8 *dst, u8 *src, u32 n, u64 ret, u64 surface_id);
/* client: parse bo complete notify */
u64 cb_client_parse_bo_complete_cmd(u8 *data, u64 *surface_id);

/* client / server: shell cmd */
u8 *cb_create_shell_cmd(struct cb_shell_info *s, u32 *n);
u8 *cb_dup_shell_cmd(u8 *dst, u8 *src, u32 n, struct cb_shell_info *s);
/* server / client: parse shell cmd */
s32 cb_parse_shell_cmd(u8 *data, struct cb_shell_info *s);

/* client: terminate cmd */
u8 *cb_client_create_destroy_cmd(u64 link_id, u32 *n);
u8 *cb_dup_destroy_cmd(u8 *dst, u8 *src, u32 n, u64 link_id);
/* server: parse terminate cmd */
u64 cb_server_parse_destroy_cmd(u8 *data);

/* server: terminate cmd ack */
u8 *cb_server_create_destroy_ack_cmd(u64 ret, u32 *n);
u8 *cb_dup_destroy_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret);
/* client: parse terminate cmd ack */
u64 cb_client_parse_destroy_ack_cmd(u8 *data);

/* client: parse raw event */
struct cb_gui_input_msg *cb_client_parse_input_msg(u8 *data, u32 *count_msg);
struct cb_raw_input_event *cb_client_parse_raw_input_evt_cmd(u8 *data,
							     u32 *count_evts);
struct touch_event *cb_client_parse_raw_touch_evt_cmd(u8 *data, u32 *sz);

/* client: raw input enable cmd */
u8 *cb_client_create_raw_input_en_cmd(u64 en, u32 *n);
u8 *cb_dup_raw_input_en_cmd(u8 *dst, u8 *src, u32 n, u64 en);
/* server: parse raw input enable cmd */
s32 cb_server_parse_raw_input_en_cmd(u8 *data, u64 *en);

/* client: set kbd led evts cmd */
u8 *cb_client_create_set_kbd_led_st_cmd(u32 led_status, u32 *n);
u8 *cb_dup_set_kbd_led_st_cmd(u8 *dst, u8 *src, u32 n, u32 led_status);
/* server: parse set kbd led cmd */
s32 cb_server_parse_set_kbd_led_st_cmd(u8 *data, u32 *led_status);

enum {
	CB_KBD_LED_STATUS_SCROLLL = 0,
	CB_KBD_LED_STATUS_NUML,
	CB_KBD_LED_STATUS_CAPSL,
};

/* client: get kbd led evts cmd */
u8 *cb_client_create_get_kbd_led_st_cmd(u32 *n);
/* server: set kbd led status to cmd */
u8 *cb_server_create_get_kbd_led_st_ack_cmd(u32 led_status, u32 *n);
/* server: dup kbd led status to cmd */
u8 *cb_dup_get_kbd_led_st_ack_cmd(u8 *dst, u8 *src, u32 n, u32 led_status);
/* client: parse kbd led status cmd */
s32 cb_client_parse_get_kbd_led_st_ack_cmd(u8 *data, u32 *led_status);

struct cb_connector_info {
	bool enabled;
	s32 pipe;
	char connector_name[CB_CONNECTOR_NAME_MAX_LEN + 1];
	char monitor_name[CB_MONITOR_NAME_MAX_LEN + 1];
	u16 width, height;
	u32 vrefresh;
	u32 pixel_freq;
	u16 width_cur, height_cur;
	u32 vrefresh_cur;
	u32 pixel_freq_cur;
};

/* server: Hotplug command */
u8 *cb_server_create_hpd_cmd(struct cb_connector_info *conn_info, u32 *n);
u8 *cb_dup_hpd_cmd(u8 *dst, u8 *src, u32 n,struct cb_connector_info *conn_info);
/* client: parse hotplug command */
u64 cb_client_parse_hpd_cmd(u8 *data, struct cb_connector_info *conn_info);

void cb_cmd_dump(u8 *data);

/*
 * mouse cursor command
 * 
 */
enum mc_cmd_type {
	MC_CMD_TYPE_UNKNOWN = 0,
	MC_CMD_TYPE_SET_CURSOR,
	MC_CMD_TYPE_SHOW,
	MC_CMD_TYPE_HIDE,
};

struct cb_mc_info {
	enum mc_cmd_type type;
	u64 bo_id; /* SHM BO, used when type is MC_CMD_TYPE_SET_CURSOR */
	bool alpha_src_pre_mul; /* Display controller do alpha blending ? */
	struct {
		s32 hot_x, hot_y;
		u32 w, h;
	} cursor;
};

/* client: mc commit command */
u8 *cb_client_create_mc_commit_cmd(struct cb_mc_info *info, u32 *n);
u8 *cb_dup_mc_commit_cmd(u8 *dst, u8 *src, u32 n, struct cb_mc_info *info);
/* server: parse mc commit command */
s32 cb_server_parse_mc_commit_cmd(u8 *data, struct cb_mc_info *info);

/* server: mc commit ack */
u8 *cb_server_create_mc_commit_ack_cmd(u64 ret, u32 *n);
u8 *cb_dup_mc_commit_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret);
/* client: parse mc commit ack */
u64 cb_client_parse_mc_commit_ack_cmd(u8 *data);

/* client: create get E-EDID command */
u8 *cb_client_create_get_edid_cmd(u64 pipe, u32 *n);
u8 *cb_dup_get_edid_cmd(u8 *dst, u8 *src, u32 n, u64 pipe);
/* server: parse get E-EDID command, return pipe number. */
s32 cb_server_parse_get_edid_cmd(u8 *data, u64 *pipe);

/* server: create get E-EDID ack command */
u8 *cb_server_create_get_edid_ack_cmd(u64 pipe, u8 *edid, u64 edid_sz,
				      bool avail, u32 *n);
u8 *cb_dup_get_edid_ack_cmd(u8 *dst, u8 *src, u32 n, u64 pipe, u8 *edid,
			    u64 edid_sz, bool avail);
/*
 * client: parse get E-EDID ack command, return length and edid data
 *         return
 *                  0: success
 *            -EINVAL: invalid argument.
 *            -ENOENT: E-EDID not available.
 */
s32 cb_client_parse_get_edid_ack_cmd(u8 *data, u64 *pipe, u8 *edid, u64 *sz);

/* server: view focus change notify */
u8 *cb_server_create_view_focus_chg_cmd(u64 view_id, bool on, u32 *n);
u8 *cb_dup_view_focus_chg_cmd(u8 *dst, u8 *src, u32 n, u64 view_id, bool on);
/* client: parse view focus change notify */
s32 cb_client_parse_view_focus_chg_cmd(u8 *data, u64 *view_id, bool *on);

#pragma pack(pop)

enum cb_gui_input_tag {
	CB_GUI_INP_MOUSE_BTN_DOWN = 0,
	CB_GUI_INP_MOUSE_BTN_UP,
	CB_GUI_INP_MOUSE_BTN_CLK,
	CB_GUI_INP_MOUSE_BTN_DCLK,
	CB_GUI_INP_MOUSE_MOVE,
	CB_GUI_INP_MOUSE_SCROLL,
	CB_GUI_INP_KEY_DOWN,
	CB_GUI_INP_KEY_UP,
	CB_GUI_INP_KEY_PRESS,
	CB_GUI_INP_TOUCH_BTN_CLK,
	CB_GUI_INP_DRAG_BEGIN,
	CB_GUI_INP_DRAG,
	CB_GUI_INP_DRAG_END,
};

enum cb_gui_mouse_btn {
	CB_GUI_MOUSE_BTN_LEFT = 0,
	CB_GUI_MOUSE_BTN_RIGHT,
	CB_GUI_MOUSE_BTN_MIDDLE,
	CB_GUI_TOUCH_BTN,
};

enum cb_gui_key_value {
	CB_GUI_KEY_UP = 0,
	CB_GUI_KEY_DOWN,
};

struct cb_gui_input_msg {
	enum cb_gui_input_tag tag;
	u16 code;
	union {
		u32 kv;
		struct {
			u16 x;
			u16 y;
		} abs;
		struct {
			s16 dx;
			s16 dy;
		} d;
		struct {
			s32 d;
		} sd;
	} v;
};

#define mk_fourcc(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | \
			       ((u32)(c) << 16) | ((u32)(d) << 24))

#define DEBUG_PATH "/tmp/cube_debug"
#define DEBUG_FLAG DEBUG_PATH"/cube_dbg_flag"

static inline enum cb_pix_fmt fourcc_to_cb_pix_fmt(u32 fourcc)
{
	switch (fourcc) {
	case mk_fourcc('A', 'R', '2', '4'):
		return CB_PIX_FMT_ARGB8888;
	case mk_fourcc('X', 'R', '2', '4'):
		return CB_PIX_FMT_XRGB8888;
	case mk_fourcc('R', 'G', '2', '4'):
		return CB_PIX_FMT_RGB888;
	case mk_fourcc('R', 'G', '1', '6'):
		return CB_PIX_FMT_RGB565;
	case mk_fourcc('N', 'V', '1', '2'):
		return CB_PIX_FMT_NV12;
	case mk_fourcc('N', 'V', '1', '6'):
		return CB_PIX_FMT_NV16;
	case mk_fourcc('N', 'V', '2', '4'):
		return CB_PIX_FMT_NV24;
	case mk_fourcc('Y', 'U', 'Y', 'V'):
		return CB_PIX_FMT_YUYV;
	case mk_fourcc('Y', 'U', '1', '2'):
		return CB_PIX_FMT_YUV420;
	case mk_fourcc('Y', 'U', '1', '6'):
		return CB_PIX_FMT_YUV422;
	case mk_fourcc('Y', 'U', '2', '4'):
		return CB_PIX_FMT_YUV444;
	default:
		return CB_PIX_FMT_UNKNOWN;
	}
}

#ifdef __cplusplus
}
#endif

#endif

