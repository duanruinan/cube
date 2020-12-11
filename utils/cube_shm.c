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
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_event.h>
#include <cube_shm.h>

s32 cb_shm_import(struct cb_shm *shm, size_t size, s32 fd)
{
	if (!shm)
		return -EINVAL;

	memset(shm, 0, sizeof(*shm));
	shm->sz = size;
	shm->fd = fd;
	shm->creator = false;

	shm->map = mmap(NULL, shm->sz, PROT_READ | PROT_WRITE,
			MAP_SHARED, shm->fd, 0);

	if (shm->map == MAP_FAILED) {
		close(shm->fd);
		return -1;
	}

	return 0;
}

s32 cb_shm_init(struct cb_shm *shm, size_t size)
{
	char name[] = "/tmp/cube_XXXXXX";
	s32 ret;

	if (!shm)
		return -EINVAL;

	memset(shm, 0, sizeof(*shm));
	shm->sz = size;
	shm->creator = true;

	if (shm->creator) {
		shm->fd = mkostemp(name, O_CLOEXEC);
		if (shm->fd >= 0)
			unlink(name);
		else
			return -errno;

		do {
			ret = ftruncate(shm->fd, shm->sz);
		} while (ret < 0 && errno == EINTR);
		if (ret < 0) {
			close(shm->fd);
			return -1;
		}
	}

	shm->map = mmap(NULL, shm->sz, PROT_READ | PROT_WRITE,
			MAP_SHARED, shm->fd, 0);

	if (shm->map == MAP_FAILED) {
		close(shm->fd);
		return -1;
	}

	return 0;
}

void cb_shm_release(struct cb_shm *shm)
{
	if (shm->map && shm->map != MAP_FAILED)
		munmap(shm->map, shm->sz);

	shm->map = NULL;

	if (shm->fd >= 0)
		close(shm->fd);

	shm->fd = -1;
	shm->sz = 0;

	shm->creator = false;
}

