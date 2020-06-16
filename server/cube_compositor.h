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
#ifndef CUBE_COMPOSITOR_H
#define CUBE_COMPOSITOR_H

#include <cube_utils.h>
#include <cube_signal.h>

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

struct cb_buffer_info {
	enum cb_pix_fmt pix_fmt;
	u32 width, height;
	u32 strides[4];
	u32 offsets[4];
	s32 fd[4];
	size_t sizes[4];
	void *maps[4];
	s32 planes;
};

struct cb_buffer {
	struct cb_buffer_info info;
	struct cb_signal destroy_signal;
};

struct pipeline {
	s32 head_index;
	s32 output_index;
	s32 primary_plane_index;
	s32 cursor_plane_index;
	s32 overlay_plane_index;
};

#endif

