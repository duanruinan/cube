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
#ifndef CUBE_VKEY_MAP
#define CUBE_VKEY_MAP

#include <cube_utils.h>

static u8 vk_map[] = {
	0,		/* KEY_RESERVED */
	VK_ESCAPE,	/* 1 */
	VK_1,		/* 2 */
	VK_2,		/* 3 */
	VK_3,		/* 4 */
	VK_4,		/* 5 */
	VK_5,		/* 6 */
	VK_6,		/* 7 */
	VK_7,		/* 8 */
	VK_8,		/* 9 */
	VK_9,		/* 10 */
	VK_0,		/* 11 */
	VK_OEM_MINUS,	/* 12 */
	VK_OEM_PLUS,	/* 13 */
	VK_BACK,	/* 14 */
	VK_TAB,		/* 15 */
	VK_Q,		/* 16 */
	VK_W,		/* 17 */
	VK_E,		/* 18 */
	VK_R,		/* 19 */
	VK_T,		/* 20 */
	VK_Y,		/* 21 */
	VK_U,		/* 22 */
	VK_I,		/* 23 */
	VK_O,		/* 24 */
	VK_P,		/* 25 */
	0,		/* 26 */
	0,		/* 27 */
	VK_RETURN,	/* 28 */
	VK_LCONTROL,	/* 29 */
	VK_A,		/* 30 */
	VK_S,		/* 31 */
	VK_D,		/* 32 */
	VK_F,		/* 33 */
	VK_G,		/* 34 */
	VK_H,		/* 35 */
	VK_J,		/* 36 */
	VK_K,		/* 37 */
	VK_L,		/* 38 */
	0,		/* 39 */
	0,		/* 40 */
	0,		/* 41 */
	VK_LSHIFT,	/* 42 */
	0,		/* 43 */
	VK_Z,		/* 44 */
	VK_X,		/* 45 */
	VK_C,		/* 46 */
	VK_V,		/* 47 */
	VK_B,		/* 48 */
	VK_N,		/* 49 */
	VK_M,		/* 50 */
	VK_OEM_COMMA,	/* 51 */
	VK_OEM_PERIOD,	/* 52 */
	0,		/* 53 */
	VK_RSHIFT,	/* 54 */
	VK_MULTIPLY,	/* 55 */
	VK_MENU,	/* 56 */
	VK_SPACE,	/* 57 */
	VK_CAPITAL,	/* 58 */
	VK_F1,		/* 59 */
	VK_F2,		/* 60 */
	VK_F3,		/* 61 */
	VK_F4,		/* 62 */
	VK_F5,		/* 63 */
	VK_F6,		/* 64 */
	VK_F7,		/* 65 */
	VK_F8,		/* 66 */
	VK_F9,		/* 67 */
	VK_F10,		/* 68 */
	VK_NUMLOCK,	/* 69 */
	VK_SCROLL,	/* 70 */
	VK_NUMPAD7,	/* 71 */
	VK_NUMPAD8,	/* 72 */
	VK_NUMPAD9,	/* 73 */
	VK_SUBTRACT,	/* 74 */
	VK_NUMPAD4,	/* 75 */
	VK_NUMPAD5,	/* 76 */
	VK_NUMPAD6,	/* 77 */
	VK_ADD,		/* 78 */
	VK_NUMPAD1,	/* 79 */
	VK_NUMPAD2,	/* 80 */
	VK_NUMPAD3,	/* 81 */
	VK_NUMPAD0,	/* 82 */
	VK_DECIMAL,	/* 83 */
	0,		/* 84 */
	0,		/* 85 */
	0,		/* 86 */
	VK_F11,		/* 87 */
	VK_F12,		/* 88 */
	0,		/* 89 */
	0,		/* 90 */
	0,		/* 91 */
	0,		/* 92 */
	0,		/* 93 */
	0,		/* 94 */
	0,		/* 95 */
	VK_RETURN,	/* 96 */
	VK_RCONTROL,	/* 97 */
	VK_DIVIDE,	/* 98 */
	VK_SNAPSHOT,	/* 99 */
	VK_MENU,	/* 100 */
	0,		/* 101 */
	VK_HOME,	/* 102 */
	VK_UP,		/* 103 */
	VK_PRIOR,	/* 104 */
	VK_LEFT,	/* 105 */
	VK_RIGHT,	/* 106 */
	VK_END,		/* 107 */
	VK_DOWN,	/* 108 */
	VK_NEXT,	/* 109 */
	VK_Insert,	/* 110 */
	VK_Delete,	/* 111 */
	0,		/* 112 */
	0,		/* 113 */
	0,		/* 114 */
	0,		/* 115 */
	0,		/* 116 */
	0,		/* 117 */
	0,		/* 118 */
	VK_PAUSE,	/* 119 */
	0,		/* 120 */
	0,		/* 121 */
	0,		/* 122 */
	0,		/* 123 */
	0,		/* 124 */
	VK_LWIN,	/* 125 */
	VK_RWIN,	/* 126 */
	VK_APPS,	/* 127 */
	0,		/* 128 */
	0,		/* 129 */
	0,		/* 130 */
	0,		/* 131 */
	0,		/* 132 */
	0,		/* 133 */
	0,		/* 134 */
	0,		/* 135 */
	0,		/* 136 */
	0,		/* 137 */
	0,		/* 138 */
	VK_RMENU,	/* 139 */
};

#endif

