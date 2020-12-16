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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <cube_utils.h>
#include "stat_tips.h"

s32 main(s32 argc, char **argv)
{
	struct dashboard_info dash;
	u16 i = 0;
	u32 r, l;
	char rate[16];
	char latency[16];

	dashboard_connect();
	do {
		memset(rate, 0, 16);
		memset(latency, 0, 16);
		r = rand();
		sprintf(rate, "%d bps", r);
		l = rand();
		sprintf(latency, "%d ms", l);
		i++;
		sprintf(dash.ip, "%015u", i);
		strcpy(dash.deployment_site, "abcdef");
		strcpy(dash.rate, rate);
		strcpy(dash.latency, latency);
		set_dashboard(&dash);
		sleep(1);
	} while (1);
	dashboard_disconnect();
	return 0;
}

